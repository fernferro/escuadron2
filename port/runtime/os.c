/*
 * Sistema operativo de la GameCube: hilos, interrupciones y reemplazos (HLE).
 *
 * Hilos: el codigo original de hilos y mensajes (OSCreateThread, OSSleepThread,
 * OSWakeupThread, colas de mensajes...) corre tal cual sobre sus estructuras en
 * memoria emulada. Solo se reemplaza SelectThread, el punto donde el sistema
 * cambia de hilo: cada OSThread del juego corre en un hilo real del sistema
 * operativo del host, y un unico "testigo" (g_big) garantiza que corra uno solo
 * a la vez, como en el procesador unico de la GameCube.
 *
 * Interrupciones: los dispositivos (hw.c) marcan interrupciones pendientes; se
 * entregan en puntos seguros (al rehabilitar interrupciones y cuando no hay
 * hilos listos) llamando al manejador registrado con __OSSetInterruptHandler.
 */
#include <pthread.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <setjmp.h>
#include "runtime.h"
#include "os.h"

/* ---- direcciones del sistema operativo (version USA) ------------------ */
#define OS_CURRENT_CONTEXT    0x800000D4u
#define OS_CURRENT_THREAD     0x800000E4u
#define RUN_QUEUE             0x8008EF78u   /* OSThreadQueue[32] */
#define IDLE_CONTEXT          (RUN_QUEUE + 0x720)
#define RUN_QUEUE_BITS        0x80096660u
#define RUN_QUEUE_HINT        0x80096664u
#define RESCHEDULE            0x80096668u
#define INTERRUPT_TABLE_PTR   0x80096628u   /* puntero a la tabla de manejadores */
#define OS_EXIT_THREAD        0x8002E978u

/* OSThread */
#define THR_STATE    0x2C8
#define THR_PRIORITY 0x2D0
#define THR_QUEUE    0x2DC
#define THR_NEXT     0x2E0
#define THR_PREV     0x2E4
#define VI_CUENTA 0x80096790u   /* contador de retrazados del juego (fn_8003BB2C) */
#define VI_COLA   0x8009679Cu   /* cola de los que esperan el retrazado (fn_8003C448) */
static void rescatar_retrazado(void);

/* OSContext */
#define CTX_GPR   0x000
#define CTX_CR    0x080
#define CTX_LR    0x084
#define CTX_CTR   0x088
#define CTX_XER   0x08C
#define CTX_FPR   0x090
#define CTX_SRR0  0x198
#define CTX_GQR   0x1A4
#define CTX_PSF   0x1C8

typedef struct HostThread {
    uint32_t os_thread;
    PPCContext ctx;
    pthread_t th;
    pthread_cond_t cv;
    int started;
    struct HostThread* next;
} HostThread;

static pthread_mutex_t g_big = PTHREAD_MUTEX_INITIALIZER;
static HostThread* g_threads;
static HostThread* g_running;
static __thread HostThread* t_self;

static PPCContext g_irq_ctx;
static int g_in_irq;
static volatile uint32_t g_pending;  /* bit n = interrupcion del SO n pendiente (atomico: el hilo grafico tambien la usa) */
static pthread_mutex_t g_wake_m = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_wake_cv = PTHREAD_COND_INITIALIZER;
static uint32_t g_dummy_ctx;    /* OSContext que reciben los manejadores */

int os_trace_threads;
uint64_t os_idle_us;     /* tiempo en que ningun hilo del juego tenia trabajo (estadistica) */

/* ---- tiempo ------------------------------------------------------------- */

uint64_t os_now_us(void) {
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (uint64_t)ts.tv_sec * 1000000ull + (uint64_t)ts.tv_nsec / 1000ull;
}

/* ---- esperas cortas en Windows ------------------------------------------------
 * En Windows las esperas con tiempo (pthread_cond_timedwait, Sleep) despiertan cuando
 * tica el reloj del sistema: cada 15,6 ms salvo que timeBeginPeriod(1) lo baje, y en
 * Windows 11 eso se ignora si el sistema decide ahorrar energia con el proceso. Asi el
 * retrazado y los bloques de audio se atendian 15-25 ms tarde (cuadros dobles, "tics" en
 * el sonido). Aca se usa un temporizador de alta resolucion (Windows 10 1803+) y un evento
 * que despierta al hilo cuando llega una interrupcion. */
#ifdef _WIN32
static HANDLE s_win_timer, s_win_wake;
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
static int s_win_hr;
static void win_esperas_init(void) {
    if (s_win_wake) return;
    s_win_wake = CreateEventA(NULL, FALSE, FALSE, NULL);
    s_win_timer = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    s_win_hr = s_win_timer != NULL;
    if (!s_win_timer) s_win_timer = CreateWaitableTimerExW(NULL, NULL, 0, TIMER_ALL_ACCESS);
}
/* duerme hasta 'us' microsegundos o hasta que llegue una interrupcion */
static void win_esperar(uint64_t us) {
    LARGE_INTEGER due;
    HANDLE h[2];
    win_esperas_init();
    due.QuadPart = -(LONGLONG)(us * 10);
    h[0] = s_win_wake; h[1] = s_win_timer;
    if (s_win_timer && SetWaitableTimerEx(s_win_timer, &due, 0, NULL, NULL, NULL, 0)) WaitForMultipleObjects(2, h, FALSE, INFINITE);
    else WaitForSingleObject(s_win_wake, (DWORD)((us + 999) / 1000));
}
/* Lo llama main.c al arrancar: pide no ser frenado por ahorro de energia y mide que tan
 * precisas son las esperas (queda en el registro para diagnosticar). */
void os_win_preparar_esperas(void) {
    typedef BOOL (WINAPI *PFN_SPI)(HANDLE, int, LPVOID, DWORD);
    struct { ULONG Version, ControlMask, StateMask; } st = { 1, 0x1 | 0x4, 0 };   /* EXECUTION_SPEED | IGNORE_TIMER_RESOLUTION: no frenar */
    PFN_SPI spi = (PFN_SPI)(void*)GetProcAddress(GetModuleHandleA("kernel32.dll"), "SetProcessInformation");
    int ok = spi ? spi(GetCurrentProcess(), 4 /* ProcessPowerThrottling */, &st, sizeof st) : 0;
    uint64_t t0, peor = 0, suma = 0;
    int i;
    win_esperas_init();
    for (i = 0; i < 20; i++) {
        uint64_t d;
        t0 = os_now_us();
        win_esperar(1000);
        d = os_now_us() - t0;
        suma += d; if (d > peor) peor = d;
    }
    rt_log("[os] esperas de 1 ms: duran %.2f ms en promedio (peor %.2f ms), temporizador %s, ahorro de energia %s\n",
           suma / 20000.0, peor / 1000.0, s_win_hr ? "de alta resolucion" : "comun", ok ? "desactivado" : "sin cambiar");
    {   /* lo mismo con Sleep(1), para comparar */
        suma = 0; peor = 0;
        for (i = 0; i < 10; i++) { uint64_t d; t0 = os_now_us(); Sleep(1); d = os_now_us() - t0; suma += d; if (d > peor) peor = d; }
        rt_log("[os] Sleep(1): %.2f ms en promedio (peor %.2f ms)\n", suma / 10000.0, peor / 1000.0);
    }
}
#endif

/* ---- interrupciones ----------------------------------------------------- */

void os_raise_interrupt(int irq) {
    __atomic_or_fetch(&g_pending, 1u << irq, __ATOMIC_SEQ_CST);
#ifdef _WIN32
    if (s_win_wake) SetEvent(s_win_wake);
#endif
    pthread_mutex_lock(&g_wake_m);              /* despertar al bucle de espera si esta dormido */
    pthread_cond_broadcast(&g_wake_cv);
    pthread_mutex_unlock(&g_wake_m);
}
/* despertar al bucle de espera sin pedir interrupcion (lo usa la lectora de disco) */
void os_despertar(void) {
#ifdef _WIN32
    if (s_win_wake) SetEvent(s_win_wake);
#endif
    pthread_mutex_lock(&g_wake_m);
    pthread_cond_broadcast(&g_wake_cv);
    pthread_mutex_unlock(&g_wake_m);
}
void os_clear_interrupt(int irq) { __atomic_and_fetch(&g_pending, ~(1u << irq), __ATOMIC_SEQ_CST); }
int os_interrupt_pending(int irq) { return (g_pending >> irq) & 1; }

static void deliver_one(int irq) {
    uint32_t table = R32(INTERRUPT_TABLE_PTR);
    uint32_t handler = table ? R32(table + 4 * irq) : 0;
    uint32_t saved_ctx = R32(OS_CURRENT_CONTEXT);
    PPCContext* c = &g_irq_ctx;
    __atomic_and_fetch(&g_pending, ~(1u << irq), __ATOMIC_SEQ_CST);
    {   /* RL_PE_DEMORA_MS=N: atender tarde el fin de dibujo (para probar que no se pierdan avisos) */
        static int d = -1;
        if (d < 0) { const char* e = getenv("RL_PE_DEMORA_MS"); d = e ? atoi(e) : 0; }
        if (d && irq == IRQ_PE_FINISH) { struct timespec w = { d / 1000, (d % 1000) * 1000000L }; nanosleep(&w, NULL); }
    }
    if (!handler) return;
    /* el manejador corre en su propio contexto y pila */
    c->r[1] = OS_IRQ_STACK_TOP - 0x40;
    c->r[2] = t_self ? t_self->ctx.r[2] : c->r[2];
    c->r[13] = t_self ? t_self->ctx.r[13] : c->r[13];
    c->r[3] = (uint32_t)irq;
    c->r[4] = g_dummy_ctx;
    c->msr = 0;  /* interrupciones deshabilitadas dentro del manejador */
    memcpy(c->gqr, t_self ? t_self->ctx.gqr : c->gqr, sizeof c->gqr);
    W32(OS_CURRENT_CONTEXT, g_dummy_ctx);
    g_in_irq++;
    call_indirect(c, handler);
    g_in_irq--;
    W32(OS_CURRENT_CONTEXT, saved_ctx);
}

static void rescatar_retrazado(void);
/* Entrega las interrupciones pendientes. Devuelve 1 si entrego alguna. */
static int deliver_pending(void) {
    int any = 0;
    gx_fifo_flush();
    hw_update();
    while (g_pending) {
        int irq;
        for (irq = 0; irq < 32; irq++)
            if (g_pending & (1u << irq)) break;
        if (irq == IRQ_VI) {
            /* Despues del manejador del retrazado la cola de espera tiene que quedar vacia
             * (fn_8003BB2C despierta siempre a VI_COLA). Si el contador avanzo y alguien sigue
             * en la cola tres retrazados seguidos, se perdio el despertar: el arranque quedaba
             * quieto en la primera pantalla de carga (PC del usuario, de vez en cuando). */
            static int atascado;
            uint32_t antes = R32(VI_CUENTA);
            deliver_one(irq);
            if (R32(VI_CUENTA) != antes && R32(VI_COLA)) { if (++atascado >= 3) { atascado = 0; rescatar_retrazado(); } }
            else atascado = 0;
        } else deliver_one(irq);
        any = 1;
        hw_update();
    }
    return any;
}

static uint32_t select_thread(PPCContext* ctx, int yield);

/* Punto seguro: interrupciones habilitadas en el hilo actual. */
static void safe_point(PPCContext* ctx) {
    if (g_in_irq || !(ctx->msr & 0x8000)) return;
    if (deliver_pending() && R32(RUN_QUEUE_HINT)) {
        /* como __OSDispatchInterrupt: replanificar si se desperto un hilo */
        select_thread(ctx, 0);
    }
}

int32_t rt_loop_budget = 2048;
static void os_dump_threads(void);
/* Muestras de donde gira el juego (bucles largos): si deja de dar cuadros sin estar
 * esperando, el registro dice en que funciones esta (os_informe_giro, lo llama video.c). */
#define GIROK 16
static struct { uint32_t a[3]; uint32_t n; } s_giro[GIROK];
static uint64_t s_giro_t;
static void giro_muestra(PPCContext* ctx) {
    uint64_t now = os_now_us();
    uint32_t a[3], sp;
    int k, i, peor = 0;
    if (now - s_giro_t < 2000) return;
    s_giro_t = now;
    a[0] = ctx->lr; a[1] = a[2] = 0;
    sp = ctx->r[1];
    for (k = 1; k < 3 && sp && mem_ptr(sp); k++) {
        sp = R32(sp);
        if (!sp || !mem_ptr(sp + 4)) break;
        a[k] = R32(sp + 4);
    }
    for (i = 0; i < GIROK; i++) {
        if (s_giro[i].n && !memcmp(s_giro[i].a, a, sizeof a)) { s_giro[i].n++; return; }
        if (s_giro[i].n < s_giro[peor].n) peor = i;
    }
    memcpy(s_giro[peor].a, a, sizeof a); s_giro[peor].n = 1;
}
void os_informe_giro(int mostrar) {
    int i;
    if (mostrar) {
        rt_log("[os] el juego no da cuadros y no esta esperando; gira en:\n");
        for (i = 0; i < GIROK; i++) {
            uint32_t s0 = 0, s1 = 0, s2 = 0;
            if (s_giro[i].n < 3) continue;
            rt_log("[os]   %u muestras: %s+0x%X <- %s+0x%X <- %s+0x%X\n", s_giro[i].n,
                   rt_func_name(s_giro[i].a[0], &s0), s_giro[i].a[0] - s0, rt_func_name(s_giro[i].a[1], &s1), s_giro[i].a[1] - s1,
                   rt_func_name(s_giro[i].a[2], &s2), s_giro[i].a[2] - s2);
        }
        os_dump_threads();
    }
    memset(s_giro, 0, sizeof s_giro);
}

/* Corte del bucle de la grilla del terreno (ver hle_terrenoSpans): cada vuelta de
 * os_loop_poll son unas 2048 vueltas de bucle del codigo recompilado. */
static __thread jmp_buf* t_ter_jb;
static __thread uint32_t t_ter_polls;
static __thread int t_ter_en_paso;
static __thread uint32_t t_ter_sp;
static uint32_t s_ter_poll_lim;
uint32_t hw_ter_polls_max, hw_ter_escapes, hw_ter_salidas_largas;
#define TER_ESTADO 0x7FDF5E60u
void os_loop_poll(PPCContext* ctx) {
    if (t_ter_jb && !t_ter_en_paso && !g_in_irq) {
        if (++t_ter_polls > hw_ter_polls_max) hw_ter_polls_max = t_ter_polls;
        if (t_ter_polls >= s_ter_poll_lim) {
            if (t_ter_polls < s_ter_poll_lim + 3 && ctx->r[1] == t_ter_sp) {
                /* Dar por terminada la fila: la celda actual (sp+0x58) queda justo antes de
                   la final (+0x260) y la final pedida (+0x264) igual a esa; en la vuelta
                   siguiente el juego llega a la final y pide el paso siguiente como siempre,
                   asi su estado queda en orden. */
                uint32_t fin = R32(TER_ESTADO + 0x260), paso = R32(TER_ESTADO + 0x25C);
                W32(TER_ESTADO + 0x264, fin);
                W32(ctx->r[1] + 0x58, fin - paso);
                if (hw_ter_escapes++ < 5) rt_log("[terreno] el recorrido de la grilla se paso de la celda final: se termina la fila\n");
            } else if (t_ter_polls >= s_ter_poll_lim + 3) {
                longjmp(*t_ter_jb, 1);
            }
        }
    }
    giro_muestra(ctx);
    gx_fifo_flush();
    static int off = -1;
    if (off < 0) off = getenv("RL_NO_LOOP_POLL") != NULL;
    if (off) { rt_loop_budget = 0x7FFFFFFF; return; }
    rt_loop_budget = 2048;
    safe_point(ctx);
}

void hle_OSDisableInterrupts(PPCContext* ctx) {
    uint32_t old = ctx->msr;
    ctx->msr &= ~0x8000u;
    ctx->r[3] = (old >> 15) & 1;
}

void hle_OSEnableInterrupts(PPCContext* ctx) {
    uint32_t old = ctx->msr;
    ctx->msr |= 0x8000u;
    ctx->r[3] = (old >> 15) & 1;
    safe_point(ctx);
}

void hle_OSRestoreInterrupts(PPCContext* ctx) {
    uint32_t old = ctx->msr;
    if (ctx->r[3]) ctx->msr |= 0x8000u;
    else ctx->msr &= ~0x8000u;
    ctx->r[3] = (old >> 15) & 1;
    safe_point(ctx);
}

/* ---- hilos ---------------------------------------------------------------- */

static HostThread* find_thread(uint32_t os_thread) {
    HostThread* t;
    for (t = g_threads; t; t = t->next)
        if (t->os_thread == os_thread) return t;
    return NULL;
}

static void* thread_main(void* arg) {
    HostThread* self = (HostThread*)arg;
    uint32_t ctxa = self->os_thread;  /* el OSContext esta al principio del OSThread */
    PPCContext* c = &self->ctx;
    uint32_t pc;
    int i;
    t_self = self;
    cpu_register_thread(CPU_JUEGO);
    pthread_mutex_lock(&g_big);
    while (g_running != self) pthread_cond_wait(&self->cv, &g_big);
    for (i = 0; i < 32; i++) c->r[i] = R32(ctxa + CTX_GPR + 4 * i);
    for (i = 0; i < 32; i++) {
        c->f[i] = u64_d(R64(ctxa + CTX_FPR + 8 * i));
        c->ps1[i] = u64_d(R64(ctxa + CTX_PSF + 8 * i));
    }
    for (i = 0; i < 8; i++) c->gqr[i] = R32(ctxa + CTX_GQR + 4 * i);
    c->cr = R32(ctxa + CTX_CR);
    c->lr = R32(ctxa + CTX_LR);
    c->ctr = R32(ctxa + CTX_CTR);
    c->msr = 0x8000;  /* los hilos nuevos arrancan con interrupciones habilitadas */
    pc = R32(ctxa + CTX_SRR0);
    if (os_trace_threads) rt_log("[os] arranca hilo 0x%08X en 0x%08X\n", self->os_thread, pc);
    call_indirect(c, pc);
    /* la funcion del hilo volvio: OSExitThread(valor) */
    if (os_trace_threads) rt_log("[os] hilo 0x%08X termino\n", self->os_thread);
    call_indirect(c, OS_EXIT_THREAD);
    rt_fatal("OSExitThread volvio\n");
    return NULL;
}

static HostThread* new_thread(uint32_t os_thread) {
    HostThread* t = (HostThread*)calloc(1, sizeof *t);
    t->os_thread = os_thread;
    pthread_cond_init(&t->cv, NULL);
    t->next = g_threads;
    g_threads = t;
    return t;
}

uint64_t os_switches;   /* estadistica: cambios de hilo del juego */
static void switch_to(HostThread* t) {
    HostThread* self = t_self;
    if (t == self) return;
    os_switches++;
    g_running = t;
    if (!t->started) {
        pthread_attr_t attr;
        t->started = 1;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, 64u << 20);  /* el codigo recompilado usa mucha pila del host */
        if (pthread_create(&t->th, &attr, thread_main, t) != 0) rt_fatal("pthread_create\n");
        pthread_attr_destroy(&attr);
    } else {
        pthread_cond_signal(&t->cv);
    }
    while (g_running != self) pthread_cond_wait(&self->cv, &g_big);
}

/* Depuracion: estado de cada hilo del juego y su pila de llamadas. */
static void os_dump_threads(void);
/* para el registro [lento]: que estaba haciendo el juego cuando se quedo quieto */
volatile const char* os_juego_esperando;   /* espera del lado de la PC (NULL: ninguna) */
void os_volcar_hilos(const char* por) {
    const char* e = (const char*)os_juego_esperando;
    rt_log("[os] %s; espera de la PC: %s\n", por, e ? e : "ninguna");
    { void gl_vj_estado(void); gl_vj_estado(); }
    os_dump_threads();
}
static void os_dump_threads(void) {
    HostThread* t;
    void hw_debug_vi(void);
    {
        uint32_t table = R32(INTERRUPT_TABLE_PTR), h = table ? R32(table + 4 * IRQ_VI) : 0, s1 = 0;
        rt_log("[os] interrupciones pendientes %08X, en_irq %d, manejador VI %08X (%s)\n", g_pending, g_in_irq, h, rt_func_name(h, &s1));
    }
    hw_debug_vi();
    rt_log("[os] retrazados contados por el juego %u, cola de espera del retrazado %08X..%08X\n", R32(VI_CUENTA), R32(VI_COLA), R32(VI_COLA + 4));
    { void hw_debug_audio(void), hw_debug_pe(void); hw_debug_audio(); hw_debug_pe(); }
    for (t = g_threads; t; t = t->next) {
        uint32_t sp = t->ctx.r[1], s0 = 0;
        int k;
        rt_log("[os] hilo %08X estado %u prio %u cola %08X lr %s (%08X)\n", t->os_thread, R16(t->os_thread + THR_STATE),
               R32(t->os_thread + THR_PRIORITY), R32(t->os_thread + THR_QUEUE), rt_func_name(t->ctx.lr, &s0), t->ctx.lr);
        for (k = 0; k < 14 && sp && mem_ptr(sp); k++) {
            uint32_t lr;
            sp = R32(sp);
            if (!sp || !mem_ptr(sp + 4)) break;
            lr = R32(sp + 4);
            rt_log("[os]     <- %s+0x%X (%08X)\n", rt_func_name(lr, &s0), lr - s0, lr);
        }
    }
}

/* Quien espera: tiempo ocioso por cadena de llamadas del hilo que se duerme (registro [espera]) */
#define WAITK 24
static struct { uint32_t a[3]; uint64_t us; uint32_t n; } s_waitk[WAITK];
static void wait_key(uint32_t a[3]) {
    uint32_t sp;
    int k;
    a[0] = a[1] = a[2] = 0;
    if (!t_self) return;
    a[0] = t_self->ctx.lr;
    sp = t_self->ctx.r[1];
    for (k = 1; k < 3 && sp && mem_ptr(sp); k++) {
        sp = R32(sp);
        if (!sp || !mem_ptr(sp + 4)) break;
        a[k] = R32(sp + 4);
    }
}
static void wait_add(const uint32_t a[3], uint64_t us) {
    int i, free_i = -1, min_i = 0;
    for (i = 0; i < WAITK; i++) {
        if (s_waitk[i].n && !memcmp(s_waitk[i].a, a, 12)) { s_waitk[i].us += us; s_waitk[i].n++; return; }
        if (!s_waitk[i].n && free_i < 0) free_i = i;
        if (s_waitk[i].us < s_waitk[min_i].us) min_i = i;
    }
    i = free_i >= 0 ? free_i : min_i;
    memcpy(s_waitk[i].a, a, 12); s_waitk[i].us = us; s_waitk[i].n = 1;
}
void os_wait_report(double sec) {
    int r, i;
    for (r = 0; r < 4; r++) {
        int best = -1;
        uint32_t s0 = 0, s1 = 0, s2 = 0;
        for (i = 0; i < WAITK; i++) if (s_waitk[i].n && (best < 0 || s_waitk[i].us > s_waitk[best].us)) best = i;
        if (best < 0 || s_waitk[best].us < 1000) break;
        rt_log("[espera] %.0f%% (%u veces): %s <- %s <- %s\n", (double)s_waitk[best].us / (sec * 1e4), s_waitk[best].n,
               rt_func_name(s_waitk[best].a[0], &s0), rt_func_name(s_waitk[best].a[1], &s1), rt_func_name(s_waitk[best].a[2], &s2));
        s_waitk[best].n = 0; s_waitk[best].us = 0;
    }
    memset(s_waitk, 0, sizeof s_waitk);
}

/* Espera eventos de hardware hasta que haya algun hilo listo. */
static void idle_until_ready(void) {
    int spins = 0;
    uint32_t wk[3];
    uint64_t t_idle0 = os_now_us();
    wait_key(wk);
    gx_fifo_flush();
    while (R32(RUN_QUEUE_BITS) == 0) {
        if (!deliver_pending()) {
            uint64_t next = hw_next_event_us();
            uint64_t now = os_now_us();
            if (next > now) {
                uint64_t wait = next - now;
                struct timespec ts;
                uint64_t until;
                if (wait > 20000) wait = 20000;
                {   /* RL_SUENO_GRUESO=1: imita los temporizadores de Windows (despertar ~15 ms tarde) */
                    static int g = -1;
                    if (g < 0) { const char* e = getenv("RL_SUENO_GRUESO"); g = e ? (atoi(e) > 1 ? atoi(e) : 15600) : 0; }
                    if (g && wait < (uint64_t)g) wait = (uint64_t)g;
                }
                timespec_get(&ts, TIME_UTC);
                until = (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec + wait * 1000ull;
                ts.tv_sec = (time_t)(until / 1000000000ull);
                ts.tv_nsec = (long)(until % 1000000000ull);
                {
                    uint64_t t0 = os_now_us();
                    pthread_mutex_unlock(&g_big);
#ifdef _WIN32
                    (void)ts;
                    if (!g_pending) win_esperar(wait);
#else
                    pthread_mutex_lock(&g_wake_m);
                    if (!g_pending) pthread_cond_timedwait(&g_wake_cv, &g_wake_m, &ts);
                    pthread_mutex_unlock(&g_wake_m);
#endif
                    pthread_mutex_lock(&g_big);
                    os_idle_us += os_now_us() - t0;
                }
            }
            if ((spins & 63) == 63) { void hw_pe_kick(void); hw_pe_kick(); }
            if (++spins == 2000) { rt_log("[os] aviso: todos los hilos esperan desde hace mucho\n"); os_dump_threads(); }
            {   static uint64_t ult;
                static uint32_t cuenta0;
                uint64_t ahora = os_now_us();
                if (ahora - t_idle0 < 1500000) { cuenta0 = 0; }
                else if (!cuenta0) { cuenta0 = R32(VI_CUENTA) | 0x80000000u; ult = ahora; }
                else if (ahora - ult > 1500000) {
                    ult = ahora;
                    if ((R32(VI_CUENTA) | 0x80000000u) != cuenta0) rescatar_retrazado();
                    cuenta0 = R32(VI_CUENTA) | 0x80000000u;
                }
            }
        }
    }
    wait_add(wk, os_now_us() - t_idle0);
}

static void set_run(uint32_t thr);
/* Rescate: en una PC con Windows el juego quedo quieto al arrancar esperando el retrazado
 * (fn_8003C448, VIWaitForRetrace) aunque los retrazados seguian llegando. Si todos los hilos
 * esperan hace mas de 1,5 s y el contador de retrazados del juego avanzo, se despierta a los
 * que esperan en esa cola (vuelven a mirar el contador, asi que despertar de mas no hace dano). */
static void rescatar_retrazado(void) {
    HostThread* t;
    for (t = g_threads; t; t = t->next) {
        uint32_t thr = t->os_thread, q, p, n, e;
        int esta = 0;
        if (R16(thr + THR_STATE) != 4 || R32(thr + THR_QUEUE) != VI_COLA) continue;
        q = VI_COLA;
        for (e = R32(q); e && mem_ptr(e); e = R32(e + THR_NEXT)) if (e == thr) { esta = 1; break; }
        if (esta) {
            p = R32(thr + THR_PREV); n = R32(thr + THR_NEXT);
            if (p) W32(p + THR_NEXT, n); else W32(q, n);
            if (n) W32(n + THR_PREV, p); else W32(q + 4, p);
        }
        {   static int n;
            if (n++ < 20) rt_log("[os] rescate: hilo %08X dormido esperando el retrazado (en la cola: %s); se despierta\n", thr, esta ? "si" : "NO"); }
        set_run(thr);
    }
}
static void set_run(uint32_t thr) {
    uint32_t prio = R32(thr + THR_PRIORITY);
    uint32_t q = RUN_QUEUE + prio * 8;
    uint32_t tail;
    W16(thr + THR_STATE, 1);
    W32(thr + THR_QUEUE, q);
    tail = R32(q + 4);
    if (!tail) W32(q, thr);
    else W32(tail + THR_NEXT, thr);
    W32(thr + THR_PREV, tail);
    W32(thr + THR_NEXT, 0);
    W32(q + 4, thr);
    W32(RUN_QUEUE_BITS, R32(RUN_QUEUE_BITS) | (1u << (31 - prio)));
    W32(RUN_QUEUE_HINT, 1);
}

static uint32_t select_thread(PPCContext* ctx, int yield) {
    uint32_t cur, bits, prio, q, next, nn;
    HostThread* t;
    (void)ctx;
    if ((int32_t)R32(RESCHEDULE) > 0) return 0;
    if (g_in_irq) return 0;  /* dentro de un manejador no se cambia de hilo */
    cur = R32(OS_CURRENT_THREAD);
    if (R32(OS_CURRENT_CONTEXT) != cur) return 0;
    if (cur) {
        if (R16(cur + THR_STATE) == 2) {
            if (!yield) {
                bits = R32(RUN_QUEUE_BITS);
                if ((int32_t)R32(cur + THR_PRIORITY) <= (int32_t)cntlzw(bits)) return 0;
            }
            set_run(cur);
        }
        if (!t_self->os_thread) t_self->os_thread = cur;
    }
    W32(OS_CURRENT_THREAD, 0);
    if (R32(RUN_QUEUE_BITS) == 0) {
        W32(OS_CURRENT_CONTEXT, IDLE_CONTEXT);
        idle_until_ready();
    }
    W32(RUN_QUEUE_HINT, 0);
    bits = R32(RUN_QUEUE_BITS);
    prio = cntlzw(bits);
    q = RUN_QUEUE + prio * 8;
    next = R32(q);
    nn = R32(next + THR_NEXT);
    if (!nn) W32(q + 4, 0);
    else W32(nn + THR_PREV, 0);
    W32(q, nn);
    if (!nn) W32(RUN_QUEUE_BITS, bits & ~(1u << (31 - prio)));
    W32(next + THR_QUEUE, 0);
    W16(next + THR_STATE, 2);
    W32(OS_CURRENT_THREAD, next);
    W32(OS_CURRENT_CONTEXT, next);
    if (next != cur || !t_self->os_thread) {
        t = find_thread(next);
        if (!t) t = new_thread(next);
        if (os_trace_threads) rt_log("[os] cambio de hilo 0x%08X -> 0x%08X\n", cur, next);
        switch_to(t);
    }
    return 0;
}

void hle_SelectThread(PPCContext* ctx) { ctx->r[3] = select_thread(ctx, (int)ctx->r[3]); }

void hle_OSLoadContext(PPCContext* ctx) {
    rt_fatal("OSLoadContext(0x%08X) llamado fuera del planificador (LR=0x%08X)\n", ctx->r[3], ctx->lr);
}

/* ---- arranque del modulo -------------------------------------------------- */

void os_init_host(PPCContext* main_ctx) {
    HostThread* t = new_thread(0);  /* el OSThread se asigna cuando OSInit lo crea */
    (void)main_ctx;
    t->started = 1;
    t_self = t;
    cpu_register_thread(CPU_JUEGO);
    g_running = t;
    pthread_mutex_lock(&g_big);
    g_dummy_ctx = OS_IRQ_CONTEXT;
}

PPCContext* os_main_ctx(void) { return &t_self->ctx; }

/* ---- servicios de depuracion ---------------------------------------------- */

/* OSReport(fmt, ...): se formatea con el sprintf del propio juego. */
void hle_OSReport(PPCContext* ctx) {
    PPCContext c = *ctx;
    int i;
    char* s;
    for (i = 10; i > 3; i--) c.r[i] = c.r[i - 1];
    c.r[4] = ctx->r[3];
    c.r[3] = OS_REPORT_BUF;
    c.r[1] = ctx->r[1] - 0x100;
    call_indirect(&c, OS_SPRINTF);
    s = (char*)mem_ptr(OS_REPORT_BUF);
    if (s) {
        s[OS_REPORT_BUF_SIZE - 1] = 0;
        fprintf(stdout, "%s", s);
        fflush(stdout);
    }
}

/* gsysVMInit: la memoria virtual del juego no hace falta en la PC (todo es
 * memoria plana). Solo se registra el monton "User VM" de 0x7F000000 a
 * 0x7FC00000 que el juego usa como segundo arena. */
void hle_gsysVMInit(PPCContext* ctx) {
    (void)ctx;
    W32(0x80095CC8u, 0x7F000000u);
    W32(0x80095CCCu, 0x7FC00000u);
    /* Memoria disponible para el VM (la consulta fn_800094B0: 0x80095CC4 - (paginas fijas + 16) * 4 KB).
     * Si queda en 0 el juego cree que no hay memoria virtual y pone todo en el monton
     * principal, que se llena (OUT OF MEMORY) y despues escribe sobre el SO. */
    W32(0x80095CC4u, 0x01000000u);
    W16(0x80095CDCu, 0);
    rt_log("[vm] memoria virtual reemplazada por memoria plana (monton 0x7F000000-0x7FC00000)\n");
}

/* ---- memoria virtual del juego --------------------------------------------
 * En la consola el juego trae paginas de 4 KB desde la ARAM y, para los DMA (disco,
 * ARAM), pide la direccion fisica de la pagina. Aca la memoria virtual es plana: se
 * devuelve la direccion recortada a 26 bits, que memory.c vuelve a reconocer como
 * direccion de la memoria virtual (0x7C000000-0x7FFFFFFF). */
static uint32_t vm_phys(uint32_t va) {
    if (va >= 0x7C000000u && va < 0x80000000u) return va & 0x03FFFFFFu;
    return va & 0x01FFFFFFu;
}
void hle_gsysVMLockPage(PPCContext* ctx) { ctx->r[3] = vm_phys(ctx->r[3] & ~0xFFFu); }
void hle_gsysVMUnlockPage(PPCContext* ctx) { (void)ctx; }
void hle_gsysVMReleaseRange(PPCContext* ctx) { (void)ctx; }
void hle_gsysVMEvictPage(PPCContext* ctx) { (void)ctx; }
void hle_gsysVMPageIn(PPCContext* ctx) {
    static int warned;
    if (warned++ < 5) rt_log("[vm] pedido de pagina 0x%08X (no deberia pasar con memoria plana)\n", ctx->r[3]);
    ctx->r[3] = vm_phys(ctx->r[3] & ~0xFFFu);
}

/* ver port/hle.txt (0x80026D28) */
void hle_salDspLatency(PPCContext* ctx) { ctx->r[3] = 0; }

/* levelLoad (envuelta): anota que nivel se carga y, para probar, RL_NIVEL=N cambia el
 * nivel elegido en el menu por el N (la variable global del nivel esta en 0x7FDE822C y
 * la etapa en 0x7FDE8230). */
void f_7FC0260C(PPCContext* ctx);
void hle_levelLoad(PPCContext* ctx) {
    static int forced, menu;
    const char* e = getenv("RL_NIVEL");
    uint32_t cur = R32(0x7FDE822Cu);
    if (cur == 19 || cur == 20) menu = 1;    /* 19/20 = hangar de los menus (19 con partida guardada); 22 = presentacion */
    /* se cambia la primera mision que se elige desde el menu (no la presentacion) */
    if (e && !forced && menu && cur != 19 && cur != 20 && cur != 22) {
        forced = 1;
        W32(0x7FDE822Cu, (uint32_t)atoi(e));
        if (getenv("RL_ETAPA")) W32(0x7FDE8230u, (uint32_t)atoi(getenv("RL_ETAPA")));
    }
    rt_log("[nivel] cargando nivel %u etapa %u\n", R32(0x7FDE822Cu), R32(0x7FDE8230u));
    {   extern volatile int gl_juego_cargando;   /* el hilo grafico aprovecha para compilar shaders */
        gl_juego_cargando = !getenv("RL_SIN_SHADERS_EN_CARGA") && !getenv("RL_GL_NULL");
        { extern volatile int gl_dump_tras_carga; gl_dump_tras_carga = 1; }   /* desde el principio de la carga */
        f_7FC0260C(ctx);
        gl_juego_cargando = 0; }
}

/* ---- terreno: corte de seguridad ---------------------------------------------
 * fn_7FCBE5F0 arma las franjas visibles de la grilla del terreno llamando a fn_7FCBE108
 * hasta que devuelve 0. En la PC del usuario a veces no termina (los niveles 3 y 4, cerca del
 * suelo y de las montanas): el juego se congela. Se cuentan los pasos de cada llamada y,
 * si pasan de RL_TERRENO_LIMITE (por defecto 4000; lo normal es menos de 200), se corta. */
void f_7FCBE5F0(PPCContext* ctx);
void f_7FCBE108(PPCContext* ctx);
static uint32_t s_ter_pasos;
static int s_ter_dentro;
uint32_t hw_ter_max, hw_ter_cortes;
void interp_call(PPCContext* ctx, uint32_t pc);
extern uint32_t interp_forzar_a, interp_forzar_b;
void hle_terrenoSpans(PPCContext* ctx) {
    static int verif = -1;
    static unsigned ncall;
    static uint8_t* snap, *res;
    PPCContext c1;
    int comparar = 0;
    uint32_t msr0 = ctx->msr;
    if (verif < 0) verif = getenv("RL_VERIFICAR_TERRENO") != NULL;
    if (verif && (ncall++ % 30) == 0) {
        /* RL_VERIFICAR_TERRENO=1 (pruebas): correr la funcion tambien con el interprete
           (desde el mismo estado) y comparar memoria y registros con la recompilada */
        if (!snap) { snap = (uint8_t*)malloc(MEM_SIZE); res = (uint8_t*)malloc(MEM_SIZE); }
        memcpy(snap, g_mem, MEM_SIZE);
        c1 = *ctx; c1.msr &= ~0x8000u;
        interp_forzar_a = 0x7FCB0000u; interp_forzar_b = 0x7FCD0000u;
        interp_call(&c1, 0x7FCBE5F0u);
        interp_forzar_a = interp_forzar_b = 0;
        memcpy(res, g_mem, MEM_SIZE);
        memcpy(g_mem, snap, MEM_SIZE);
        ctx->msr &= ~0x8000u;
        comparar = 1;
    }
    s_ter_pasos = 0;
    s_ter_dentro++;
    if (!s_ter_poll_lim) { const char* e = getenv("RL_TERRENO_LIMITE_VUELTAS"); s_ter_poll_lim = e ? (uint32_t)atoi(e) : 3; if (!s_ter_poll_lim) s_ter_poll_lim = 0xFFFFFFFFu; }
    {   /* El bucle de esta funcion recorre la grilla celda por celda hasta llegar a la celda
           final, que compara por igualdad. En la PC del usuario a veces se la pasa de largo
           y sigue de a 2 bytes hacia atras por toda la memoria (miles de millones de vueltas):
           el juego queda en 0 FPS. Si da mas de ~6000 vueltas sin pedir un paso (lo normal es
           menos de 2048), se da por terminada la fila (ver os_loop_poll); si ni asi termina,
           se sale como si la funcion hubiera vuelto. */
        jmp_buf jb;
        jmp_buf* prev = t_ter_jb;
        PPCContext guard = *ctx;
        uint32_t polls0 = t_ter_polls;
        int en0 = t_ter_en_paso;
        uint32_t sp0 = t_ter_sp;
        t_ter_polls = 0; t_ter_en_paso = 0; t_ter_sp = ctx->r[1] - 0x128u;   /* marco de fn_7FCBE5F0 */
        if (setjmp(jb) == 0) {
            t_ter_jb = &jb;
            f_7FCBE5F0(ctx);
        } else {
            *ctx = guard;
            ctx->r[3] = 0;
            if (hw_ter_salidas_largas++ < 5) rt_log("[terreno] el recorrido de la grilla no termina: se sale de la funcion\n");
        }
        t_ter_jb = prev; t_ter_polls = polls0; t_ter_en_paso = en0; t_ter_sp = sp0;
    }
    if (comparar) {
        uint32_t i, nd = 0, first[8];
        ctx->msr = msr0; c1.msr = msr0;
        for (i = 0; i < MEM_SIZE; i += 4) if (memcmp(res + i, g_mem + i, 4)) { if (nd < 8) first[nd] = MEM_BASE + i; nd++; }
        {   int k, rd = 0, fd = 0;
            for (k = 0; k < 32; k++) { if (c1.r[k] != ctx->r[k]) rd++; if (c1.f[k] != ctx->f[k] && !(c1.f[k] != c1.f[k] && ctx->f[k] != ctx->f[k])) fd++; }
            rt_log("[verif] terreno llamada %u: %u palabras distintas, %d registros enteros, %d de punto flotante, pasos %u", ncall, nd, rd, fd, s_ter_pasos);
            for (k = 0; k < (int)nd && k < 8; k++) rt_log(" %08X(%08X/%08X)", first[k], R32(first[k]), (uint32_t)(res[first[k]-MEM_BASE] << 24 | res[first[k]-MEM_BASE+1] << 16 | res[first[k]-MEM_BASE+2] << 8 | res[first[k]-MEM_BASE+3]));
            rt_log("\n");
        }
    }
    s_ter_dentro--;
    if (s_ter_pasos > hw_ter_max) hw_ter_max = s_ter_pasos;
}
void hle_terrenoPaso(PPCContext* ctx) {
    static uint32_t limite;
    if (!limite) { const char* e = getenv("RL_TERRENO_LIMITE"); limite = e ? (uint32_t)atoi(e) : 4000; if (!limite) limite = 0xFFFFFFFFu; }
    if (s_ter_dentro && ++s_ter_pasos > limite) {
        if (hw_ter_cortes++ < 5) rt_log("[terreno] la grilla del terreno no termina: se corta despues de %u pasos\n", limite);
        ctx->r[3] = 0;
        return;
    }
    t_ter_en_paso++;
    f_7FCBE108(ctx);
    t_ter_en_paso--;
    t_ter_polls = 0;
}

/* ---- ensayo del terreno sobre la RAM de una captura (--ensayo-terreno) ------------------
 * Las capturas F12 guardan toda la RAM al final del cuadro. Se carga esa RAM y se corre la
 * parte del juego que arma y dibuja el terreno cercano (fn_7FCBCCC4 + fn_7FCC163C, como
 * fn_7FC0099C), contando cuantas franjas cercanas manda (fn_7FCBB2B4). Sirve para saber si
 * con el estado del juego del usuario el terreno sale bien aca (=> la PC del usuario calcula
 * distinto) o tambien falta (=> el estado ya venia mal). */
uint32_t ensayo_franjas;
int rt_redondeo_viejo;
int ensayo_terreno(const char* path) {
    void f_7FCBCCC4(PPCContext* ctx);
    void f_7FCC163C(PPCContext* ctx);
    uint32_t hdr[4];
    PPCContext* ctx;
    FILE* f = fopen(path, "rb");
    long skip;
    if (!f) { fprintf(stderr, "no pude abrir %s\n", path); return 1; }
    rt_redondeo_viejo = getenv("RL_REDONDEO_VIEJO") != NULL;
    if (fread(hdr, sizeof hdr, 1, f) != 1 || hdr[0] != 0x47584331u) { fprintf(stderr, "no es una captura\n"); return 1; }
    skip = (long)hdr[2] + (1L << 20);   /* GXState + TMEM (1 MB) */
    fseek(f, (long)sizeof hdr + skip, SEEK_SET);
    if (fread(g_mem, 1, MEM_SIZE, f) != MEM_SIZE) { fprintf(stderr, "captura corta\n"); return 1; }
    fclose(f);
    ctx = (PPCContext*)calloc(1, sizeof *ctx);
    ctx->msr = 0x2032;
    ctx->r[1] = 0x817F8000u;
    ctx->r[2] = 0x8009EA20u;
    ctx->r[13] = 0x8009ADA0u;
    {   /* GQR del hilo actual del juego (su contexto guardado) */
        int k; uint32_t th = R32(0x800000E4u);
        fprintf(stderr, "hilo actual %08X\n", th);
        if (th) for (k = 0; k < 8; k++) ctx->gqr[k] = R32(th + CTX_GQR + 4 * k);
        for (k = 0; k < 8; k++) fprintf(stderr, "gqr%d=%08X ", k, ctx->gqr[k]); fprintf(stderr, "\n"); }
    {   extern FILE* gx_fifo_dump; gx_fifo_dump = fopen("build/ensayo_fifo.bin", "wb"); }
    ctx->r[3] = 0x7FDE8004u; f_7FCBCCC4(ctx);
    fprintf(stderr, "tras fn_7FCBCCC4: franjas %u\n", ensayo_franjas);
    ctx->r[1] = 0x817F8000u;
    ctx->r[3] = 0x7FDE8004u; f_7FCC163C(ctx);
    fprintf(stderr, "tras fn_7FCC163C: franjas %u, pasos maximos %u\n", ensayo_franjas, hw_ter_max);
    {   /* captura sintetica: el estado y la RAM de la captura + solo los comandos del terreno */
        extern FILE* gx_fifo_dump;
        FILE* in = fopen(path, "rb"); FILE* out = fopen("build/ensayo.gxc", "wb");
        long n = ftell(gx_fifo_dump); uint8_t* buf; size_t pre = sizeof hdr + (size_t)skip;
        fclose(gx_fifo_dump); gx_fifo_dump = NULL;
        buf = (uint8_t*)malloc(pre); fread(buf, 1, pre, in);
        ((uint32_t*)buf)[3] = (uint32_t)n;
        fwrite(buf, 1, pre, out); fwrite(g_mem, 1, MEM_SIZE, out);
        free(buf); buf = (uint8_t*)malloc((size_t)n);
        {   FILE* d = fopen("build/ensayo_fifo.bin", "rb"); fread(buf, 1, (size_t)n, d); fclose(d); }
        fwrite(buf, 1, (size_t)n, out); fclose(out); fclose(in);
        fprintf(stderr, "build/ensayo.gxc: %ld bytes de comandos\n", n);
    }
    return 0;
}

/* ---- video de los mensajes de radio: decodificador de bloques (hle.txt) ---- */
void f_80055F60(PPCContext* ctx);
void rapida_80055F60(PPCContext* ctx);
uint64_t os_video_llamadas, os_video_us;
static uint64_t os_cpu_us(void) {
#ifdef _WIN32
    FILETIME c, e, k, u; ULARGE_INTEGER x;
    GetThreadTimes(GetCurrentThread(), &c, &e, &k, &u);
    x.LowPart = u.dwLowDateTime; x.HighPart = u.dwHighDateTime;
    return x.QuadPart / 10;
#else
    struct timespec ts; clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
#endif
}
void hle_videoBloques(PPCContext* ctx) {
    static int exacto = -1;
    uint64_t t0 = os_cpu_us();
    if (exacto < 0) exacto = getenv("RL_VIDEO_EXACTO") != NULL;
    if (exacto) f_80055F60(ctx); else rapida_80055F60(ctx);
    os_video_us += os_cpu_us() - t0;   /* tiempo de CPU de este hilo (sin las esperas) */
    os_video_llamadas++;
}

/* RL_TRAZA_VIEWPORT=1 (depuracion): quien llama a GXSetViewport con un viewport que no es la
   pantalla entera (por ejemplo la pasada a media resolucion de la cabina del nivel 6) */
void f_80037A9C(PPCContext* ctx);
uint32_t video_game_frames(void);
void hle_vistaGX(PPCContext* ctx) {
    static int traza = -1, n;
    if (traza < 0) { const char* e = getenv("RL_TRAZA_VIEWPORT"); traza = e ? atoi(e) : 0; }
    if (traza && n < 300 && ctx->f[3] != 640.0 && ctx->f[3] != 128.0 && video_game_frames() >= (uint32_t)traza) {
        uint32_t sp = ctx->r[1], s0 = 0;
        const char* nm;
        int k;
        n++;
        nm = rt_func_name(ctx->lr, &s0);
        rt_log("[viewport] %g %g %g %g cuadro %u <- %s+0x%X", ctx->f[1], ctx->f[2], ctx->f[3], ctx->f[4], video_game_frames(), nm, ctx->lr - s0);
        for (k = 0; k < 8 && sp && mem_ptr(sp); k++) {
            uint32_t lr;
            sp = R32(sp);
            if (!sp || !mem_ptr(sp + 4)) break;
            lr = R32(sp + 4);
            nm = rt_func_name(lr, &s0);
            rt_log(" <- %s+0x%X", nm, lr - s0);
        }
        rt_log("\n");
    }
    f_80037A9C(ctx);
}

/* RL_TRAZA_MIRA=1 (depuracion): la camara de la computadora de puntería (fn_7FCD771C) */
void f_7FCD771C(PPCContext* ctx);
static float rf32(uint32_t a) { uint32_t v = R32(a); float f; memcpy(&f, &v, 4); return f; }
static void traza_v3(const char* n, uint32_t a) { rt_log(" %s(%08X)=%.3f,%.3f,%.3f", n, a, rf32(a), rf32(a + 4), rf32(a + 8)); }
void hle_camaraMira(PPCContext* ctx) {
    static int traza = -1, n;
    uint32_t pl;
    if (traza < 0) traza = getenv("RL_TRAZA_MIRA") != NULL;
    pl = R32(0x7FDEFE14);
    if (traza && n < 60) {
        rt_log("[mira] antes cuadro %u flag %d lock %d", video_game_frames(), R8(0x7FDF7D5C), R8(0x80095E8A));
        if (pl) { traza_v3("fwd9c", pl + 0x9C); traza_v3("posA8", pl + 0xA8); }
        traza_v3("off", 0x7FDF7DB4); traza_v3("v81e0", 0x7FDE81E0); traza_v3("lock", 0x7FDF17D0); traza_v3("dc0", 0x7FDF7DC0);
        rt_log("\n");
    }
    f_7FCD771C(ctx);
    if (traza && n < 60) {
        int i;
        n++;
        rt_log("[mira] despues"); traza_v3("ojo", 0x7FDF7D94); traza_v3("d88", 0x7FDF7D88); traza_v3("v81e0", 0x7FDE81E0); traza_v3("off", 0x7FDF7DB4);
        rt_log(" mtx7d70:");
        for (i = 0; i < 12; i++) rt_log(" %.3f", rf32(0x7FDF7D70 + 4 * i));
        rt_log("\n");
    }
}

/* Camara de la bomba en la cabina (fn_7FCD812C): al tirar una bomba o un torpedo en vista de
   cabina, el juego prende la computadora de punteria en un modo que encima sobre toda la ventana
   de la cabina la vista desde el proyectil (con la bomba, mirando para abajo). Confunde (parece
   la escena corrida), asi que por defecto no se prende; --camara-bomba (o "Camara de la bomba"
   en el launcher) deja el comportamiento original. La computadora de punteria normal (boton Y)
   llama con r3 = 0 y no se toca. */
int g_camara_bomba;
/* opciones del menu del juego (menu.cpp, ajustes.ini leido en main.c) */
unsigned g_trucos_a, g_trucos_b;   /* bits de trucos a mantener prendidos (trucos.h) */
int g_contador_fps;      /* 0 no, 1 arriba a la izquierda, 2 arriba a la derecha */
int g_aviso_shaders = 1; /* "Compilando N shaders" abajo a la izquierda */
char g_ajustes_ruta[1024] = "ajustes.ini";
void f_7FCD812C(PPCContext* ctx);
void hle_miraBomba(PPCContext* ctx) {
    if (ctx->r[3] & 0xFF) {
        static int avisado;
        if (!g_camara_bomba) {
            if (!avisado++) rt_log("[juego] camara de la bomba en la cabina: apagada (--camara-bomba para verla como el original)\n");
            return;
        }
    }
    f_7FCD812C(ctx);
}
