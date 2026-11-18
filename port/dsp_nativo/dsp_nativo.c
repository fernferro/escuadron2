/*
 * DSP nativo: corre los programas del DSP de la GameCube (el sonido de MusyX) traducidos a C
 * al compilar el port (port/recomp/dsprecomp.py), sin emular el procesador.
 *
 * Aca esta lo que rodea al programa: los buzones de mensajes con la CPU, el DMA entre la RAM y
 * la memoria del DSP, el acelerador que lee las muestras de la ARAM (con su decodificador
 * ADPCM) y el hilo donde corre. El comportamiento de cada registro sale de Dolphin
 * (Core/DSP/DSPHWInterface.cpp y DSPAccelerator.cpp, GPL-2.0-or-later).
 *
 * Opciones: --dsp-dolphin usa el DSP de Dolphin (port/dsp) en vez de este;
 * --dsp-verificar corre los dos a la par instruccion por instruccion y avisa la primera
 * diferencia (para probar la traduccion).
 */
#include "dsp_nativo.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

extern const uint16_t dsp_irom_data[0x1000];
extern const uint16_t dsp_coef_data[0x800];
uint8_t* hw_aram_ptr(void);
uint8_t* mem_ptr(uint32_t addr);
void hw_dsp_interrupt_from_dsp(void);
void rt_log(const char* fmt, ...);
void cpu_register_thread(int cat);

DspState g;
volatile int g_dsp_codigo_cambiado;
int g_dsp_quedan;

static u32 s_mail[2];          /* 0 = CPU -> DSP, 1 = DSP -> CPU (bit 31: hay mensaje sin leer) */
#define MAIL_CPU 0
#define MAIL_DSP 1
static pthread_mutex_t s_m = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_cv = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t s_mail_m = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_mail_cv = PTHREAD_COND_INITIALIZER;
static volatile int s_cpu_esperando_mail;   /* la CPU espera que el DSP lea su mensaje */
static volatile int s_kick;
static volatile int s_esperando;   /* el DSP esta parado esperando un mensaje de la CPU */
static const DspPrograma* s_prog;
static uint64_t s_corridas;
static uint64_t s_n_mail_cpu;

/* ---- buzones ---- */
static u32 mail_peek(int mb) { return __atomic_load_n(&s_mail[mb], __ATOMIC_ACQUIRE); }
static u16 mail_read_lo(int mb) {
    u32 v = __atomic_fetch_and(&s_mail[mb], ~0x80000000u, __ATOMIC_SEQ_CST);
    if (mb == MAIL_CPU && __atomic_load_n(&s_cpu_esperando_mail, __ATOMIC_SEQ_CST)) {
        pthread_mutex_lock(&s_mail_m);
        pthread_cond_broadcast(&s_mail_cv);
        pthread_mutex_unlock(&s_mail_m);
    }
    return (u16)v;
}
static u16 mail_read_hi(int mb) { return (u16)(mail_peek(mb) >> 16); }
static void mail_write_lo(int mb, u16 v) {
    u32 o = mail_peek(mb), n;
    do n = ((o & ~0xffffu) | v) | 0x80000000u;
    while (!__atomic_compare_exchange_n(&s_mail[mb], &o, n, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE));
}
static void mail_write_hi(int mb, u16 v) {
    u32 o = mail_peek(mb), n;
    do n = ((o & 0xffffu) | ((u32)v << 16)) & ~0x80000000u;
    while (!__atomic_compare_exchange_n(&s_mail[mb], &o, n, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE));
}

/* ---- acelerador ---- */
#define ACC_START_END_MASK 0x3fffffffu
#define ACC_CUR_MASK 0xbfffffffu
static u8 aram_read(u32 a) { return hw_aram_ptr()[a & 0xFFFFFF]; }
static void aram_write(u32 a, u8 v) { hw_aram_ptr()[a & 0xFFFFFF] = v; }
static void acc_set_cur(u32 a) { g.acc_cur = a & ACC_CUR_MASK; }

static u16 acc_current_sample(void) {
    u16 val = 0;
    switch (g.acc_format & 3) {
    case 0: val = aram_read(g.acc_cur >> 1); val = (g.acc_cur & 1) ? (val & 0xf) : (u16)(val >> 4); break;
    case 1: val = aram_read(g.acc_cur); break;
    case 2: val = (u16)((aram_read(g.acc_cur * 2) << 8) | aram_read(g.acc_cur * 2 + 1)); break;
    default: break;
    }
    return val;
}
static u16 acc_read_raw(void) {
    u16 val = acc_current_sample();
    if ((g.acc_format & 3) != 3) g.acc_cur++;
    else g.acc_cur = (g.acc_cur & ~3u) | ((g.acc_cur + 1) & 3);
    if (g.acc_cur - 1 == g.acc_end) {
        g.acc_cur = g.acc_start;
        g.exceptions |= 1u << EXC_ACC_RAW_READ;
    }
    acc_set_cur(g.acc_cur);
    return val;
}
static void acc_write_raw(u16 value) {
    if (g.acc_cur & 0x80000000u) {
        aram_write(g.acc_cur * 2, (u8)(value >> 8));
        aram_write(g.acc_cur * 2 + 1, (u8)value);
        g.acc_cur++;
        g.exceptions |= 1u << EXC_ACC_RAW_WRITE;
    }
}
static u16 acc_read_sample(const s16* coefs) {
    u16 val = 0;
    u8 step = 0;
    s16 raw;
    const int decode = (g.acc_format >> 2) & 3;
    int idx;
    s32 c1, c2;
    if (g.acc_reads_stopped) return 0;
    raw = (decode == 1 || decode == 3) ? (s16)g.acc_input : (s16)acc_current_sample();
    idx = (g.acc_pred_scale >> 4) & 7;
    c1 = coefs[idx * 2]; c2 = coefs[idx * 2 + 1];
    if (decode == 0) {   /* ADPCM */
        int scale;
        s32 v;
        raw &= 0xF;
        scale = 1 << (g.acc_pred_scale & 0xF);
        if (raw >= 8) raw -= 16;
        v = (scale * raw) + ((0x400 + c1 * g.acc_yn1 + c2 * g.acc_yn2) >> 11);
        val = (u16)(s16)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
        step = 2;
        g.acc_yn2 = g.acc_yn1;
        g.acc_yn1 = (s16)val;
        g.acc_cur += 1;
        if ((g.acc_end & 0xf) == 0x0 && g.acc_cur == g.acc_end) g.acc_cur = g.acc_start + 1;
        else if ((g.acc_end & 0xf) == 0x1 && g.acc_cur == g.acc_end - 1) g.acc_cur = g.acc_start;
        else if ((g.acc_cur & 15) == 0) {
            g.acc_pred_scale = aram_read((g.acc_cur & ~15u) >> 1);
            g.acc_cur += 2;
            step += 2;
        }
    } else {             /* PCM */
        u8 gs = 0;
        s32 v;
        switch ((g.acc_format >> 4) & 3) { case 0: gs = 11; break; case 1: gs = 0; break; case 2: gs = 16; break; default: break; }
        v = (((s32)g.acc_gain * raw) >> gs) + (((c1 * g.acc_yn1) >> gs) + ((c2 * g.acc_yn2) >> gs));
        val = (u16)(s16)v;
        g.acc_yn2 = g.acc_yn1;
        g.acc_yn1 = (s16)val;
        step = 2;
        if (decode != 1) g.acc_cur += 1;
    }
    if (g.acc_cur == (g.acc_end + step - 1)) {
        g.acc_cur = g.acc_start;
        g.acc_reads_stopped = true;
        g.exceptions |= 1u << EXC_ACC_SAMPLE_READ;
    }
    acc_set_cur(g.acc_cur);
    return val;
}

/* ---- DMA ---- */
static void dma_to_dsp(u16* dst, u32 addr, u32 size) {
    const uint8_t* p = mem_ptr(addr);
    u32 i;
    if (!p) { rt_log("[dsp] DMA a DSP invalido %08X\n", addr); return; }
    for (i = 0; i < size / 2; i++) dst[i] = (u16)(p[2 * i] << 8 | p[2 * i + 1]);
}
static void dma_from_dsp(const u16* src, u32 addr, u32 size) {
    uint8_t* p = mem_ptr(addr);
    u32 i;
    if (!p) { rt_log("[dsp] DMA desde DSP invalido %08X\n", addr); return; }
    for (i = 0; i < size / 2; i++) { p[2 * i] = (u8)(src[i] >> 8); p[2 * i + 1] = (u8)src[i]; }
    { void hw_dsp_escribio(uint32_t, uint32_t); hw_dsp_escribio(addr, size); }
}
static void do_dma(void) {
    const u32 addr = ((u32)g.ifx[0xce] << 16) | g.ifx[0xcf];
    const u16 ctl = g.ifx[0xc9];
    const u16 dsp_addr = (u16)(g.ifx[0xcd] * 2);
    const u16 len = g.ifx[0xcb];
    if (len > 0x4000) { rt_log("[dsp] DMA demasiado grande %04X\n", len); return; }
    switch (ctl & 3) {
    case 1: dma_from_dsp(g.dram + dsp_addr / 2, addr, len); break;   /* DRAM -> CPU */
    case 0: dma_to_dsp(g.dram + dsp_addr / 2, addr, len); break;     /* CPU -> DRAM */
    case 2: dma_to_dsp(g.iram + dsp_addr / 2, addr, len); g_dsp_codigo_cambiado = 1; break;  /* CPU -> IRAM */
    default: break;
    }
}

/* ---- registros de hardware (0xFFxx) ---- */
void dsp_write_ifx(u16 address, u16 value) {
    switch (address & 0xff) {
    case 0xfb: if (value & 1) hw_dsp_interrupt_from_dsp(); break;      /* DIRQ */
    case 0xfc: mail_write_hi(MAIL_DSP, value); break;
    case 0xfd: mail_write_lo(MAIL_DSP, value); break;
    case 0xfe: mail_write_hi(MAIL_CPU, value); break;
    case 0xff: mail_write_lo(MAIL_CPU, value); break;
    case 0xcb:
        g.ifx[0xcb] = value;
        g.ifx[0xc9] |= 4;
        if (!g.ifx[0xef]) do_dma();
        g.ifx[0xc9] &= ~4;
        g.ifx[0xcb] = 0;
        break;
    case 0xcd: case 0xce: case 0xcf: case 0xc9: g.ifx[address & 0xff] = value; break;
    case 0xd4: g.acc_start = (((u32)value << 16) | (u16)g.acc_start) & ACC_START_END_MASK; break;
    case 0xd5: g.acc_start = ((g.acc_start >> 16 << 16) | value) & ACC_START_END_MASK; break;
    case 0xd6: g.acc_end = (((u32)value << 16) | (u16)g.acc_end) & ACC_START_END_MASK; break;
    case 0xd7: g.acc_end = ((g.acc_end >> 16 << 16) | value) & ACC_START_END_MASK; break;
    case 0xd8: acc_set_cur(((u32)value << 16) | (u16)g.acc_cur); break;
    case 0xd9: acc_set_cur(((u32)(u16)(g.acc_cur >> 16) << 16) | value); break;
    case 0xd1: g.acc_format = value; break;
    case 0xde: g.acc_gain = (s16)value; break;
    case 0xdb: g.acc_yn1 = (s16)value; break;
    case 0xdc: g.acc_yn2 = (s16)value; g.acc_reads_stopped = false; break;
    case 0xda: g.acc_pred_scale = value & 0x7f; break;
    case 0xd3: acc_write_raw(value); break;
    case 0xdf: g.acc_input = value; break;
    default: g.ifx[address & 0xff] = value; break;
    }
}
u16 dsp_read_ifx(u16 address) {
    switch (address & 0xff) {
    case 0xfc: {   /* el DSP mira si la CPU ya leyo su mensaje */
        u32 m = mail_peek(MAIL_DSP);
        if (m & 0x80000000u) g.idle_wait = 1;
        return (u16)(m >> 16);
    }
    case 0xfd: return mail_read_lo(MAIL_DSP);
    case 0xfe: {   /* el DSP mira si hay un mensaje de la CPU */
        u32 m = mail_peek(MAIL_CPU);
        if (!(m & 0x80000000u)) g.idle_wait = 1;
        return (u16)(m >> 16);
    }
    case 0xff: return mail_read_lo(MAIL_CPU);
    case 0xc9: return g.ifx[0xc9];
    case 0xd4: return (u16)(g.acc_start >> 16);
    case 0xd5: return (u16)g.acc_start;
    case 0xd6: return (u16)(g.acc_end >> 16);
    case 0xd7: return (u16)g.acc_end;
    case 0xd8: return (u16)(g.acc_cur >> 16);
    case 0xd9: return (u16)g.acc_cur;
    case 0xd1: return g.acc_format;
    case 0xde: return (u16)g.acc_gain;
    case 0xdb: return (u16)g.acc_yn1;
    case 0xdc: return (u16)g.acc_yn2;
    case 0xda: return g.acc_pred_scale;
    case 0xdd: return acc_read_sample((const s16*)&g.ifx[0xa0]);
    case 0xd3: return acc_read_raw();
    case 0xdf: return g.acc_input;
    default: return g.ifx[address & 0xff];
    }
}

/* ---- que programa hay en la memoria de instrucciones ---- */
static void elegir_programa(void) {
    const DspPrograma* p;
    static const DspPrograma* ultimo;
    s_prog = NULL;
    for (p = g_dsp_programas; p->nombre; p++)
        if (!memcmp(g.iram, p->imagen, (size_t)p->largo * 2)) { s_prog = p; break; }
    if (!s_prog) s_prog = &g_dsp_programa_rom;
    if (s_prog != ultimo) { rt_log("[dsp] programa del DSP: %s\n", s_prog->nombre); ultimo = s_prog; }
}

/* ---- registro de control (DSPCR de la CPU) ---- */
static void reset_dsp(void) {
    int i;
    g.pc = 0x8000;
    for (i = 0; i < 4; i++) g.r.wr[i] = 0xffff;
    g_dsp_codigo_cambiado = 1;
}
static void write_control(u16 val) {
    if (val & CR_RESET) { reset_dsp(); val &= ~CR_RESET; }
    if ((g.control_reg & CR_INIT) && !(val & CR_INIT)) {
        g.pc = 0;
        dma_to_dsp(g.iram, 0x81000000u, 0x1000);
        g_dsp_codigo_cambiado = 1;
        val &= ~CR_INIT;
        val |= CR_INIT_CODE;
        g.control_reg_init_code_clear_time = 0;
    }
    g.control_reg = val;
}
static u16 read_control(void) {
    const u16 v = g.control_reg;
    if (g.control_reg & CR_INIT_CODE) {
        if (g.control_reg_init_code_clear_time++ >= 1) g.control_reg &= ~CR_INIT_CODE;
    }
    return v;
}

/* Despertar al hilo del DSP: la CPU escribio o leyo un mensaje, o toco el registro de
   control. Se usa un candado aparte (s_wait_m) para que la CPU nunca espere a que termine
   una corrida del DSP, y sin carreras (no se pierde ningun aviso). */
static pthread_mutex_t s_wait_m = PTHREAD_MUTEX_INITIALIZER;
static volatile int s_dsp_durmiendo;   /* el hilo del DSP esta (o va a estar) en espera_corta */
static void kick(void) {
    /* Se llama en cada mensaje: tomar el candado y despertar cuesta llamadas al sistema.
       Solo hace falta si el hilo del DSP esta durmiendo (orden secuencial: o el hilo ve
       s_kick antes de dormirse, o aca se ve que duerme y se lo despierta). */
    __atomic_store_n(&s_kick, 1, __ATOMIC_SEQ_CST);
    if (!__atomic_load_n(&s_dsp_durmiendo, __ATOMIC_SEQ_CST)) return;
    pthread_mutex_lock(&s_wait_m);
    pthread_cond_signal(&s_cv);
    pthread_mutex_unlock(&s_wait_m);
}

static void espera_corta(long us) {   /* con s_m tomado: lo suelta mientras espera */
    struct timespec ts;
    long long ns;
    timespec_get(&ts, TIME_UTC);
    ns = (long long)ts.tv_nsec + us * 1000LL;
    ts.tv_sec += (time_t)(ns / 1000000000LL);
    ts.tv_nsec = (long)(ns % 1000000000LL);
    pthread_mutex_unlock(&s_m);
    pthread_mutex_lock(&s_wait_m);
    __atomic_store_n(&s_dsp_durmiendo, 1, __ATOMIC_SEQ_CST);
    if (!__atomic_load_n(&s_kick, __ATOMIC_SEQ_CST)) pthread_cond_timedwait(&s_cv, &s_wait_m, &ts);
    __atomic_store_n(&s_dsp_durmiendo, 0, __ATOMIC_SEQ_CST);
    s_kick = 0;
    pthread_mutex_unlock(&s_wait_m);
    pthread_mutex_lock(&s_m);
}

/* instrucciones por corrida (el candado se suelta entre corridas: mas chico = la CPU espera
   menos para tocar los registros del DSP). RL_DSP_PRESUPUESTO=N; antes 200000. */
static int s_presupuesto = 40000;
static void* dsp_hilo(void* arg) {
    (void)arg;
    if (getenv("RL_DSP_PRESUPUESTO")) s_presupuesto = atoi(getenv("RL_DSP_PRESUPUESTO"));
    if (s_presupuesto < 1000) s_presupuesto = 1000;
    cpu_register_thread(2);
    pthread_mutex_lock(&s_m);
    for (;;) {
        int r;
        if (g.control_reg & CR_HALT) { espera_corta(4000); continue; }
        if (g_dsp_codigo_cambiado) { g_dsp_codigo_cambiado = 0; elegir_programa(); }
        if (g.exceptions) CheckExceptions();
        g.idle_wait = 0;
        {   /* RL_DSP_HIST=N: histograma de direcciones durante N millones de instrucciones (depuracion) */
            static long hist_left = -2; static uint32_t* hist;
            if (hist_left == -2) { const char* e = getenv("RL_DSP_HIST"); hist_left = e ? atol(e) * 1000000L : -1; if (e) hist = (uint32_t*)calloc(65536, 4); }
            if (hist_left > 0) {
                int k;
                for (k = 0; k < 1000 && hist_left > 0; k++) {
                    hist[g.pc]++;
                    hist_left--;
                    r = s_prog->ejecutar_paso(1);
                    if (r != DSP_FIN_PRESUPUESTO) break;
                }
                if (hist_left == 0) {
                    int i, j;
                    rt_log("[dsp] direcciones mas ejecutadas:\n");
                    for (j = 0; j < 40; j++) {
                        int best = 0;
                        for (i = 0; i < 65536; i++) if (hist[i] > hist[best]) best = i;
                        if (!hist[best]) break;
                        rt_log("[dsp]   %04X %u\n", best, hist[best]);
                        hist[best] = 0;
                    }
                    hist_left = -1;
                }
                if (r == DSP_FIN_ESPERA) espera_corta(500);
                pthread_mutex_unlock(&s_m); pthread_mutex_lock(&s_m);
                continue;
            }
        }
        {
            static uint64_t t_ej, n_esp, n_cor, t_last;
            struct timespec a, b;
            timespec_get(&a, TIME_UTC);
            {
                static int paso = -1;
                if (paso < 0) paso = getenv("RL_DSP_PASO") != NULL;
                r = paso ? s_prog->ejecutar_paso(2000000) : s_prog->ejecutar(s_presupuesto);
                if (paso) { static uint64_t ins, tl; ins += 2000000 - (uint64_t)g_dsp_quedan; if (b.tv_sec != 0 && 0) tl = 0;
                    { struct timespec c; timespec_get(&c, TIME_UTC); if (c.tv_sec - (time_t)tl >= 5) { if (tl) rt_log("[dsp] %.1f millones de instrucciones por segundo\n", ins / 5e6); tl = (uint64_t)c.tv_sec; ins = 0; } } }
            }
            timespec_get(&b, TIME_UTC);
            t_ej += (uint64_t)((b.tv_sec - a.tv_sec) * 1000000000LL + (b.tv_nsec - a.tv_nsec));
            n_cor++; if (r == DSP_FIN_ESPERA) n_esp++;
            if (getenv("RL_DSP_STATS") && b.tv_sec - t_last >= 5) {
                if (t_last) rt_log("[dsp] %llu corridas, %llu esperas, %.1f ms ejecutando por segundo, %llu mensajes de la CPU\n", (unsigned long long)n_cor, (unsigned long long)n_esp, (double)t_ej / 5e6, (unsigned long long)s_n_mail_cpu);
                t_last = b.tv_sec; t_ej = 0; n_esp = 0; n_cor = 0;
            }
        }
        s_corridas++;
        switch (r) {
        case DSP_FIN_ESPERA:
            if (!(mail_peek(MAIL_CPU) & 0x80000000u)) __atomic_store_n(&s_esperando, 1, __ATOMIC_RELEASE);
            espera_corta(4000);   /* la CPU avisa (kick) cuando hay algo nuevo */
            __atomic_store_n(&s_esperando, 0, __ATOMIC_RELEASE);
            break;
        case DSP_FIN_DESCONOCIDO: {
            static int avisos;
            if (avisos++ < 10) rt_log("[dsp] el programa %s salto a %04X, que no esta traducido\n", s_prog->nombre, g.pc);
            if (g.pc < 0x8000) g_dsp_codigo_cambiado = 1;   /* quizas cambio el programa */
            espera_corta(500);
            break;
        }
        default:
            break;
        }
        /* soltar el candado un momento para que la CPU pueda tocar los registros */
        pthread_mutex_unlock(&s_m);
        pthread_mutex_lock(&s_m);
    }
    return NULL;
}

/* ---- --dsp-verificar: los dos DSP a la par, instruccion por instruccion ---- */
int dsp_verificar;     /* 1 comparando, 2 ya se encontro una diferencia (sigue solo Dolphin) */
typedef struct {
    uint16_t pc, sr, cr, control_reg;
    uint16_t ar[4], ix[4], wr[4], st[4];
    uint64_t ac[2], prod;
    uint32_t ax[2];
    uint8_t exceptions, ptrs[4];
    uint32_t mail[2];
} DspFoto;
void dsp_dolphin_init_sin_hilo(void);
int dsp_dolphin_paso(void);
void dsp_dolphin_tomar_excepciones(void);
void dsp_dolphin_foto(DspFoto* f);
const uint16_t* dsp_dolphin_dram(void);

static void foto_nativa(DspFoto* f) {
    int i;
    f->pc = g.pc; f->sr = g.r.sr; f->cr = g.r.cr; f->control_reg = g.control_reg;
    for (i = 0; i < 4; i++) { f->ar[i] = g.r.ar[i]; f->ix[i] = g.r.ix[i]; f->wr[i] = g.r.wr[i]; f->st[i] = g.r.st[i]; f->ptrs[i] = g.reg_stack_ptrs[i]; }
    f->ac[0] = g.r.ac[0].val; f->ac[1] = g.r.ac[1].val; f->prod = g.r.prod.val;
    f->ax[0] = g.r.ax[0].val; f->ax[1] = g.r.ax[1].val;
    f->exceptions = g.exceptions;
    f->mail[0] = mail_peek(MAIL_CPU); f->mail[1] = mail_peek(MAIL_DSP);
}
static void log_foto(const char* q, const DspFoto* f) {
    rt_log("[dsp-ver] %s: pc %04X sr %04X cr %02X ctl %04X ar %04X %04X %04X %04X ix %04X %04X %04X %04X wr %04X %04X %04X %04X\n"
           "[dsp-ver]     st %04X %04X %04X %04X (%d %d %d %d) ac0 %010llX ac1 %010llX ax0 %08X ax1 %08X prod %010llX exc %02X mail %08X %08X\n",
           q, f->pc, f->sr, f->cr, f->control_reg, f->ar[0], f->ar[1], f->ar[2], f->ar[3], f->ix[0], f->ix[1], f->ix[2], f->ix[3],
           f->wr[0], f->wr[1], f->wr[2], f->wr[3], f->st[0], f->st[1], f->st[2], f->st[3], f->ptrs[0], f->ptrs[1], f->ptrs[2], f->ptrs[3],
           (unsigned long long)(f->ac[0] & 0xFFFFFFFFFFull), (unsigned long long)(f->ac[1] & 0xFFFFFFFFFFull), f->ax[0], f->ax[1],
           (unsigned long long)f->prod, f->exceptions, f->mail[0], f->mail[1]);
}
static void* dsp_hilo_verificar(void* arg) {
    uint64_t pasos = 0;
    u16 hist[16];
    int hn = 0;
    (void)arg;
    cpu_register_thread(2);
    rt_log("[dsp-ver] comparando el DSP nativo con el de Dolphin instruccion por instruccion\n");
    for (;;) {
        int dp, np = 0;
        DspFoto a, b;
        pthread_mutex_lock(&s_m);     /* la CPU no toca los buzones mientras dan el paso los dos */
        dp = dsp_dolphin_paso();
        if (dsp_verificar == 1 && !(g.control_reg & CR_HALT)) {
            int r, k;
            hist[hn++ & 15] = g.pc;
            for (k = 0; k < 3; k++) {
                if (g_dsp_codigo_cambiado) { g_dsp_codigo_cambiado = 0; elegir_programa(); }
                if (g.exceptions) CheckExceptions();
                r = s_prog->ejecutar_paso(1);
                if (r != DSP_FIN_CODIGO) break;
            }
            if (r == DSP_FIN_DESCONOCIDO) rt_log("[dsp-ver] el nativo salto a %04X (no traducido)\n", g.pc);
            np = 1;
        }
        if (dsp_verificar == 1) {
            memset(&a, 0, sizeof a); memset(&b, 0, sizeof b);
            dsp_dolphin_foto(&a);
            if (a.exceptions) { dsp_dolphin_tomar_excepciones(); memset(&a, 0, sizeof a); dsp_dolphin_foto(&a); }
            foto_nativa(&b);
            /* el bit de "sin leer" de los buzones lo cambia la CPU desde otro hilo: no se compara */
            a.mail[0] = a.mail[1] = b.mail[0] = b.mail[1] = 0;
            pasos++;
            if (memcmp(&a, &b, sizeof a) != 0 || ((pasos & 4095) == 0 && memcmp(dsp_dolphin_dram(), g.dram, sizeof g.dram) != 0)) {
                int i;
                rt_log("[dsp-ver] DIFERENCIA despues de %llu instrucciones (programa %s)\n", (unsigned long long)pasos, s_prog ? s_prog->nombre : "?");
                log_foto("dolphin", &a);
                log_foto("nativo ", &b);
                rt_log("[dsp-ver] ultimas direcciones del nativo:");
                for (i = 0; i < 16; i++) rt_log(" %04X", hist[(hn + i) & 15]);
                rt_log("\n");
                for (i = 0; i < 0x1000; i++)
                    if (dsp_dolphin_dram()[i] != g.dram[i]) { rt_log("[dsp-ver] DRAM %04X: dolphin %04X nativo %04X\n", i, dsp_dolphin_dram()[i], g.dram[i]); break; }
                dsp_verificar = 2;
            } else if ((pasos % 50000000ull) == 0) {
                rt_log("[dsp-ver] %llu instrucciones iguales\n", (unsigned long long)pasos);
            }
        }
        pthread_mutex_unlock(&s_m);
        if (!dp && !np) {
            struct timespec w = { 0, 200000 };
            nanosleep(&w, NULL);
        }
    }
    return NULL;
}

/* ---- API para hw.c (los mismos nombres que el DSP de Dolphin: dsp_lle_*) ---- */
int dsp_lle_enabled;
int dsp_usar_dolphin;      /* --dsp-dolphin */
/* 1 si el DSP nativo todavia esta trabajando (o tiene un mensaje de la CPU sin leer) */
int dsp_nativo_ocupado(void) {
    if (dsp_usar_dolphin || !dsp_lle_enabled) return 0;
    if (mail_peek(MAIL_CPU) & 0x80000000u) return 1;
    return !__atomic_load_n(&s_esperando, __ATOMIC_ACQUIRE);
}
int dsp_use_interpreter;   /* --dsp-interprete (solo con --dsp-dolphin) */

void dsp_dolphin_init(void);
uint16_t dsp_dolphin_read_cr(void);
uint16_t dsp_dolphin_write_cr(uint16_t v);
uint32_t dsp_dolphin_peek_mail(int cpu);
uint32_t dsp_dolphin_peek_mail_nowait(int cpu);
uint16_t dsp_dolphin_read_mail_hi(int cpu);
uint16_t dsp_dolphin_read_mail_lo(int cpu);
void dsp_dolphin_write_mail_hi(uint16_t v);
void dsp_dolphin_write_mail_lo(uint16_t v);
uint64_t dsp_dolphin_cycles(void);

static void nat_init(void) {
    pthread_t th;
    if (dsp_verificar) dsp_dolphin_init_sin_hilo();
    memset(&g, 0, sizeof g);
    g.irom = dsp_irom_data;
    g.coef = dsp_coef_data;
    {
        int i;
        for (i = 0; i < 0x1000; i++) g.iram[i] = 0x0021;
        for (i = 0; i < 4; i++) g.r.wr[i] = 0xffff;
    }
    g.r.sr |= SR_INT_ENABLE | SR_EXT_INT_ENABLE;
    g.control_reg = CR_INIT | CR_HALT;
    reset_dsp();
    rt_log("[dsp] DSP nativo (programas traducidos a C)\n");
    dsp_lle_enabled = 1;
    pthread_create(&th, NULL, dsp_verificar ? dsp_hilo_verificar : dsp_hilo, NULL);
    pthread_detach(th);
}

void dsp_lle_init(void) {
    if (dsp_usar_dolphin) dsp_dolphin_init();
    else nat_init();
}
uint16_t dsp_lle_read_cr(void) {
    u16 v;
    if (dsp_usar_dolphin) return dsp_dolphin_read_cr();
    /* El juego lee este registro muy seguido (interrupciones del DSP, DMA de ARAM). Antes
       tomaba el candado del DSP, que el hilo del DSP tiene durante cada corrida: en escenas
       con mucho sonido (el nivel 6) el juego quedaba esperando. Sin el bit de arranque no hay
       nada que modificar: se lee sin candado. */
    if (!dsp_verificar) {
        v = __atomic_load_n(&g.control_reg, __ATOMIC_ACQUIRE);
        if (!(v & CR_INIT_CODE)) return v;
    }
    pthread_mutex_lock(&s_m);
    v = read_control();
    if (dsp_verificar) v = dsp_dolphin_read_cr();
    pthread_mutex_unlock(&s_m);
    return v;
}
uint16_t dsp_lle_write_cr(uint16_t v) {
    if (dsp_usar_dolphin) return dsp_dolphin_write_cr(v);
    pthread_mutex_lock(&s_m);
    if (dsp_verificar) dsp_dolphin_write_cr(v);
    write_control(v);
    if (v & CR_EXTERNAL_INT) {
        if (IsSRFlagSet(SR_EXT_INT_ENABLE)) {
            g.exceptions |= 1u << EXC_EXTERNAL;
            g.control_reg &= ~CR_EXTERNAL_INT;
        }
        CheckExceptions();
    }
    pthread_mutex_unlock(&s_m);
    kick();
    return dsp_lle_read_cr();
}
uint32_t dsp_lle_peek_mail_nowait(int cpu) {
    if (dsp_usar_dolphin || dsp_verificar) return dsp_dolphin_peek_mail_nowait(cpu);
    return mail_peek(cpu ? MAIL_CPU : MAIL_DSP);
}
uint32_t dsp_lle_peek_mail(int cpu) {
    u32 m;
    if (dsp_usar_dolphin || dsp_verificar) return dsp_dolphin_peek_mail_nowait(cpu);
    m = mail_peek(cpu ? MAIL_CPU : MAIL_DSP);
    if (cpu && (m & 0x80000000u)) {
        /* la CPU espera que el DSP lea su mensaje: dormir un poco en vez de girar */
        struct timespec ts;
        long long ns;
        kick();
        timespec_get(&ts, TIME_UTC);
        ns = (long long)ts.tv_nsec + 1000000LL;
        ts.tv_sec += (time_t)(ns / 1000000000LL);
        ts.tv_nsec = (long)(ns % 1000000000LL);
        { extern volatile const char* os_juego_esperando; os_juego_esperando = "que el DSP lea un mensaje";
        pthread_mutex_lock(&s_mail_m);
        __atomic_store_n(&s_cpu_esperando_mail, 1, __ATOMIC_SEQ_CST);
        if (__atomic_load_n(&s_mail[MAIL_CPU], __ATOMIC_SEQ_CST) & 0x80000000u) pthread_cond_timedwait(&s_mail_cv, &s_mail_m, &ts);
        __atomic_store_n(&s_cpu_esperando_mail, 0, __ATOMIC_SEQ_CST);
        pthread_mutex_unlock(&s_mail_m);
        os_juego_esperando = 0; }
        m = mail_peek(MAIL_CPU);
    }
    return m;
}
uint16_t dsp_lle_read_mail_hi(int cpu) {
    if (dsp_usar_dolphin || dsp_verificar) return dsp_dolphin_read_mail_hi(cpu);
    return mail_read_hi(cpu ? MAIL_CPU : MAIL_DSP);
}
uint16_t dsp_lle_read_mail_lo(int cpu) {
    u16 v;
    if (dsp_usar_dolphin) return dsp_dolphin_read_mail_lo(cpu);
    if (dsp_verificar) pthread_mutex_lock(&s_m);
    v = mail_read_lo(cpu ? MAIL_CPU : MAIL_DSP);
    if (dsp_verificar) { v = dsp_dolphin_read_mail_lo(cpu); pthread_mutex_unlock(&s_m); }
    if (!cpu) kick();   /* el DSP puede estar esperando que la CPU lea su mensaje */
    return v;
}
void dsp_lle_write_mail_hi(uint16_t v) {
    if (dsp_usar_dolphin) { dsp_dolphin_write_mail_hi(v); return; }
    if (dsp_verificar) pthread_mutex_lock(&s_m);
    if (dsp_verificar) dsp_dolphin_write_mail_hi(v);
    mail_write_hi(MAIL_CPU, v);
    if (dsp_verificar) pthread_mutex_unlock(&s_m);
}
void dsp_lle_write_mail_lo(uint16_t v) {
    s_n_mail_cpu++;
    if (dsp_usar_dolphin) { dsp_dolphin_write_mail_lo(v); return; }
    if (dsp_verificar) pthread_mutex_lock(&s_m);
    if (dsp_verificar) dsp_dolphin_write_mail_lo(v);
    mail_write_lo(MAIL_CPU, v);
    if (dsp_verificar) pthread_mutex_unlock(&s_m);
    kick();
}
uint64_t dsp_lle_cycles(void) {
    if (dsp_usar_dolphin) return dsp_dolphin_cycles();
    return 0;
}
