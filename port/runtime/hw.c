/*
 * Registros de hardware de la GameCube (0xCC000000..0xCC00FFFF).
 *
 * Se emulan a nivel de registro para que el codigo original del SDK de
 * Nintendo (VI, DVD, PAD, EXI, AR, AI, GX) funcione sin cambios:
 *   0xCC000000 CP   procesador de comandos (estado de la cola grafica)
 *   0xCC001000 PE   motor de pixeles (interrupciones de token y fin de dibujo)
 *   0xCC002000 VI   video (retrazado vertical y framebuffer)
 *   0xCC003000 PI   interrupciones y cola de la CPU
 *   0xCC005000 DSP  mensajes con el DSP, DMA de ARAM y de audio
 *   0xCC006000 DI   lectora de disco
 *   0xCC006400 SI   controles
 *   0xCC006800 EXI  tarjetas de memoria, reloj/SRAM
 *   0xCC006C00 AI   interfaz de audio
 *   0xCC008000      tubo de escritura de comandos graficos (WGPIPE)
 */
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "runtime.h"
#include <pthread.h>
#include "os.h"
#include "input.h"
#include "memcard.h"

int hw_trace;

/* ---- utilidades --------------------------------------------------------- */

static void unknown(const char* dir, uint32_t a, uint32_t v, int size) {
    static uint32_t seen[512];
    static int nseen;
    int i;
    for (i = 0; i < nseen; i++)
        if (seen[i] == a) return;
    if (nseen < 512) seen[nseen++] = a;
    rt_log("[hw] %s%d no emulado 0x%08X = 0x%X\n", dir, size * 8, a, v);
}

/* ---- VI: video ---------------------------------------------------------- */

static uint16_t vi[0x40];  /* registros de 16 bits, 0xCC002000..0xCC00207F */
static uint64_t vi_next_us;
static uint32_t vi_line;
#define VI_FRAME_US 16683u

static void vi_event(void) {
    /* Retrazado vertical: activa DI0 y DI1 si estan habilitados (bit 12 de la mitad alta). */
    int any = 0, i;
    for (i = 0; i < 2; i++) {
        uint16_t* hi = &vi[(0x30 + 4 * i) / 2];
        if (*hi & 0x1000) { *hi |= 0x8000; any = 1; }
    }
    if (any) os_raise_interrupt(IRQ_VI);
    video_retrace();
}

void hw_debug_vi(void) {
    rt_log("[vi] DI0 %04X%04X DI1 %04X%04X DI2 %04X%04X DI3 %04X%04X DCR %04X\n", vi[0x30 / 2], vi[0x32 / 2], vi[0x34 / 2], vi[0x36 / 2],
           vi[0x38 / 2], vi[0x3A / 2], vi[0x3C / 2], vi[0x3E / 2], vi[2 / 2]);
}

uint32_t vi_xfb_addr(void) {
    uint32_t tfbl = ((uint32_t)vi[0x1C / 2] << 16) | vi[0x1E / 2];
    uint32_t a = tfbl & 0x00FFFFFF;
    if (tfbl & 0x10000000) a <<= 5;
    return a | 0x80000000u;
}

/* Lineas visibles: campo ACV del registro VTR (por campo; en entrelazado se duplica). */
uint32_t vi_xfb_height(void) {
    uint32_t acv = (vi[0] >> 4) & 0x3FF;
    uint32_t interlaced = !(vi[2 / 2] & 0x4);    /* DCR bit 2: no entrelazado */
    uint32_t h = interlaced ? acv * 2 : acv;
    {
        static uint32_t last;
        if (h != last) { last = h; rt_log("[vi] alto de la imagen %u lineas (acv %u, %s), VTO %04X VTE %04X\n", h, acv, interlaced ? "entrelazado" : "progresivo", vi[0x0C / 2], vi[0x0E / 2]); }
    }
    return (h >= 64 && h <= 576) ? h : 480;
}

uint32_t vi_xfb_stride(void) {
    /* registro de "picture configuration": ancho de la linea en unidades de 16 pixeles */
    uint32_t w = vi[0x48 / 2] & 0xFF;
    return w ? w * 16 * 2 : 640 * 2;
}

/* ---- PI ----------------------------------------------------------------- */

static uint32_t pi_mask, pi_fifo_base, pi_fifo_end, pi_fifo_wptr;

/* ---- DI: lectora de disco ------------------------------------------------ */

static uint32_t di[10];
enum { DISR, DICVR, DICMD0, DICMD1, DICMD2, DIMAR, DILEN, DICR, DIIMM, DICFG };

/* Las lecturas del disco van en un hilo aparte, como en la consola (la lectora trabaja sola y
 * avisa con una interrupcion). Leyendo en el hilo del juego, un disco rigido que estaba dormido
 * congelaba todo ~2 s en medio de una mision (el nivel 3, en la PC del usuario). RL_DISCO_SINCRONO=1: como antes. */
static pthread_mutex_t s_di_m = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_di_cv = PTHREAD_COND_INITIALIZER;
static int s_di_hilo = -1;                 /* -1 sin decidir, 0 sincronico, 1 con hilo */
static volatile int s_di_trabajo;
static int s_di_listo;                     /* el hilo termino; falta avisarle al juego */          /* hay una lectura pedida */
static uint32_t s_di_off, s_di_len;
static uint8_t* s_di_dst;
static void di_terminar(void) {
    di[DICR] &= ~1u;
    __atomic_or_fetch(&di[DISR], 0x10u, __ATOMIC_SEQ_CST);  /* TCINT */
    if (di[DISR] & 0x08) os_raise_interrupt(IRQ_DI);
}
static void* di_hilo(void* x) {
    (void)x;
    for (;;) {
        uint64_t t0;
        pthread_mutex_lock(&s_di_m);
        while (!s_di_trabajo) pthread_cond_wait(&s_di_cv, &s_di_m);
        pthread_mutex_unlock(&s_di_m);
        t0 = os_now_us();
        disc_read(s_di_off, s_di_len, s_di_dst);
        {   uint64_t dt = os_now_us() - t0; static int avisos;
            if (dt > 200000 && avisos++ < 10) rt_log("[disco] lectura lenta: %u bytes en %.0f ms (el juego siguio andando)\n", s_di_len, dt / 1000.0); }
        pthread_mutex_lock(&s_di_m);
        s_di_trabajo = 0;
        pthread_mutex_unlock(&s_di_m);
        /* el aviso de "termino" lo da el hilo del juego (hw_update): tocar los registros y
           pedir la interrupcion desde aca, sin el cerrojo del juego, a veces la perdia y el
           juego quedaba esperando para siempre en la pantalla de carga (PC del usuario) */
        __atomic_store_n(&s_di_listo, 1, __ATOMIC_SEQ_CST);
        { void os_despertar(void); os_despertar(); }
    }
    return NULL;
}

static void di_command(void) {
    uint32_t cmd = di[DICMD0] >> 24;
    uint32_t dma = di[DICR] & 2;
    if (hw_trace) rt_log("[di] comando %08X %08X %08X -> %08X (%X bytes)\n", di[DICMD0], di[DICMD1], di[DICMD2], di[DIMAR], di[DILEN]);
    switch (cmd) {
    case 0xA8: { /* lectura */
        uint32_t off = di[DICMD1] << 2, len = di[DILEN];
        uint8_t* dst = mem_ptr(di[DIMAR] & ~0x1Fu);
        if (dma && dst && mem_ptr((di[DIMAR] & ~0x1Fu) + len - 1)) {
            if (s_di_hilo < 0) {
                pthread_t t;
                extern int gl_headless;   /* las pruebas sin ventana quedan como antes: deterministas */
                s_di_hilo = !gl_headless && !getenv("RL_DISCO_SINCRONO") && pthread_create(&t, NULL, di_hilo, NULL) == 0;
                if (s_di_hilo) pthread_detach(t);
            }
            if (s_di_hilo && len) {
                di[DIMAR] += len;
                di[DILEN] = 0;
                pthread_mutex_lock(&s_di_m);
                s_di_off = off; s_di_len = len; s_di_dst = dst; s_di_trabajo = 1;
                pthread_cond_signal(&s_di_cv);
                pthread_mutex_unlock(&s_di_m);
                return;   /* DICR sigue ocupado hasta que el hilo termina (di_terminar) */
            }
            disc_read(off, len, dst);
        }
        else if (dma) rt_log("[di] lectura a direccion invalida 0x%08X\n", di[DIMAR]);
        di[DIMAR] += len;
        di[DILEN] = 0;
        break;
    }
    case 0x12: { /* informacion de la lectora */
        uint8_t* dst = mem_ptr(di[DIMAR]);
        if (dst) {
            memset(dst, 0, 0x20);
            dst[0] = 0x00; dst[1] = 0x00; dst[2] = 0x00; dst[3] = 0x02;  /* revision */
            dst[4] = 0x20; dst[5] = 0x01; dst[6] = 0x06; dst[7] = 0x08;  /* fecha */
            dst[8] = 0x61;
        }
        di[DILEN] = 0;
        break;
    }
    case 0xE0: di[DIIMM] = 0; break;           /* pedir error: ninguno */
    case 0xAB: case 0xE3: case 0xE1: case 0xE4: break;  /* buscar, parar motor, audio */
    case 0xE2: di[DIIMM] = 0; break;           /* estado del audio */
    default:
        rt_log("[di] comando desconocido 0x%02X\n", cmd);
        break;
    }
    di_terminar();
}

/* ---- SI: controles ------------------------------------------------------- */

static uint32_t si_out[4], si_inh[4], si_inl[4], si_poll, si_comcsr, si_sr, si_exilk;
static uint8_t si_buf[128];

static void si_update_inputs(void) {
    int ch;
    for (ch = 0; ch < 4; ch++) {
        PadState p;
        if (!input_get_pad(ch, &p)) {
            si_inh[ch] = 0; si_inl[ch] = 0;
            continue;
        }
        si_inh[ch] = ((uint32_t)p.buttons << 16) | ((uint32_t)p.stick_x << 8) | p.stick_y;
        si_inl[ch] = ((uint32_t)p.c_x << 24) | ((uint32_t)p.c_y << 16) | ((uint32_t)p.trig_l << 8) | p.trig_r;
    }
}

static void si_transfer(void) {
    int ch = (si_comcsr >> 1) & 3;
    uint32_t outlen = (si_comcsr >> 16) & 0x7F;
    uint32_t inlen = (si_comcsr >> 8) & 0x7F;
    uint8_t cmd = si_buf[0];
    PadState p;
    if (!outlen) outlen = 128;
    if (!inlen) inlen = 128;
    (void)outlen;
    si_comcsr &= ~1u;
    if (!input_get_pad(ch, &p)) {
        si_comcsr |= 0x20000000u;               /* error de comunicacion */
        si_sr |= 0x08u << ((3 - ch) * 8);       /* sin respuesta */
    } else {
        memset(si_buf, 0, inlen);
        switch (cmd) {
        case 0x00: case 0xFF:                   /* tipo de dispositivo: control estandar */
            si_buf[0] = 0x09; si_buf[1] = 0x00; si_buf[2] = 0x00;
            break;
        case 0x41: case 0x42:                   /* origen / recalibrar */
            si_buf[0] = 0x00; si_buf[1] = 0x80;
            si_buf[2] = 0x80; si_buf[3] = 0x80; si_buf[4] = 0x80; si_buf[5] = 0x80;
            si_buf[6] = 0x00; si_buf[7] = 0x00; si_buf[8] = 0x00; si_buf[9] = 0x00;
            break;
        case 0x40: {                            /* lectura directa */
            si_update_inputs();
            si_buf[0] = (uint8_t)(si_inh[ch] >> 24); si_buf[1] = (uint8_t)(si_inh[ch] >> 16);
            si_buf[2] = (uint8_t)(si_inh[ch] >> 8); si_buf[3] = (uint8_t)si_inh[ch];
            si_buf[4] = (uint8_t)(si_inl[ch] >> 24); si_buf[5] = (uint8_t)(si_inl[ch] >> 16);
            si_buf[6] = (uint8_t)(si_inl[ch] >> 8); si_buf[7] = (uint8_t)si_inl[ch];
            break;
        }
        default:
            rt_log("[si] comando 0x%02X en el canal %d\n", cmd, ch);
            break;
        }
    }
    si_comcsr |= 0x80000000u;                   /* TCINT */
    if (si_comcsr & 0x40000000u) os_raise_interrupt(IRQ_SI);
}

/* ---- EXI: tarjetas de memoria, reloj/SRAM -------------------------------- */

static uint32_t exi[3][5];
enum { EXI_CSR, EXI_MAR, EXI_LEN, EXI_CR, EXI_DATA };

/* Que dispositivo esta seleccionado en el canal (bits 7-9 de CSR). */
static int exi_dev(int ch) {
    uint32_t cs = (exi[ch][EXI_CSR] >> 7) & 7;
    return cs & 1 ? 0 : cs & 2 ? 1 : cs & 4 ? 2 : -1;
}

static int exi_is_card(int ch) { return ch == 0 && exi_dev(ch) == 0 && mc_present(); }

/* ---- reloj y SRAM (canal 0, dispositivo 1) ----
 * Primer palabra despues de seleccionar: comando. Bit 31 = escribir.
 *   0x20000000        reloj (segundos desde el 1/1/2000)
 *   0x20000100 + n*64 SRAM (64 bytes) desde el byte n
 * La SRAM se arma valida: idioma ingles, estereo, y la suma de control del
 * flashID de la tarjeta de la ranura A, que CARDMount verifica. */
static uint8_t s_sram[64];
static uint32_t s_ipl_cmd, s_ipl_pos;
static int s_ipl_have_cmd;

static void sram_init(void) {
    uint16_t sum = 0, inv = 0;
    int i;
    memset(s_sram, 0, sizeof s_sram);
    s_sram[0x13] = 0x04;                       /* flags: sonido estereo */
    for (i = 12; i < 20; i += 2) {             /* suma desde counterBias hasta el final de OSSram */
        uint16_t w = (uint16_t)((s_sram[i] << 8) | s_sram[i + 1]);
        sum = (uint16_t)(sum + w); inv = (uint16_t)(inv + (uint16_t)~w);
    }
    s_sram[0] = (uint8_t)(sum >> 8); s_sram[1] = (uint8_t)sum;
    s_sram[2] = (uint8_t)(inv >> 8); s_sram[3] = (uint8_t)inv;
    s_sram[20 + 38] = 0xFF;                    /* flashIDCheckSum[0] = ~suma(flashID[0]) = ~0 */
    s_sram[20 + 39] = 0xFF;
}

static uint8_t ipl_byte(uint8_t in, int write) {
    uint32_t c = s_ipl_cmd & 0x7FFFFFFFu;
    uint32_t p = s_ipl_pos++;
    if (c == 0x20000000u) {                    /* reloj */
        uint32_t t = (uint32_t)(time(NULL) - 946684800);
        return p < 4 ? (uint8_t)(t >> (24 - 8 * p)) : 0;
    }
    if (c >= 0x20000100u && c < 0x20001100u) { /* SRAM */
        uint32_t o = ((c - 0x20000100u) >> 6) + p;
        if (o < sizeof s_sram) {
            if (write && (s_ipl_cmd & 0x80000000u)) s_sram[o] = in;
            return s_sram[o];
        }
    }
    return 0;                                  /* ROM del IPL (fuentes): no la tenemos */
}

static void ipl_imm(uint32_t* data, int len, int rw) {
    int i;
    if (!s_ipl_have_cmd && rw == 1 && len == 4) { s_ipl_cmd = *data; s_ipl_have_cmd = 1; s_ipl_pos = 0; return; }
    {
        uint32_t out = 0;
        for (i = 0; i < len; i++) {
            uint8_t b = ipl_byte((uint8_t)(*data >> (24 - 8 * i)), rw != 0);
            out |= (uint32_t)b << (24 - 8 * i);
        }
        if (rw != 1) *data = out;
    }
}

static void ipl_dma(uint32_t ram, uint32_t len, int rw) {
    uint8_t* p = mem_ptr(ram | 0x80000000u);
    uint32_t i;
    if (!p) return;
    for (i = 0; i < len; i++) {
        uint8_t b = ipl_byte(rw ? p[i] : 0, rw != 0);
        if (rw != 1) p[i] = b;
    }
}

/* La tarjeta avisa que termino de borrar/programar un rato despues (como la
 * de verdad): si avisara enseguida, el manejador correria con el bus EXI
 * todavia bloqueado por la funcion que mando el comando. */
static uint64_t s_card_irq_at;

static void exi_card_irq(void) {
    if (mc_take_interrupt()) s_card_irq_at = os_now_us() + 2000;
}

static void exi_card_irq_fire(void) {
    s_card_irq_at = 0;
    exi[0][EXI_CSR] |= 0x02;                     /* EXIINT */
    if (mc_trace) rt_log("[exi] tarjeta: interrupcion (CSR=%08X)\n", exi[0][EXI_CSR]);
    os_raise_interrupt(IRQ_EXI0_EXI);
}

static void exi_transfer(int ch) {
    uint32_t* r = exi[ch];
    int dma = (r[EXI_CR] >> 1) & 1;
    int rw = (r[EXI_CR] >> 2) & 3;  /* 0 lectura, 1 escritura, 2 ambas */
    if (mc_trace && ch == 0) rt_log("[exi] canal %d disp %d %s %s len=%u data=%08X\n", ch, exi_dev(ch), dma ? "DMA" : "imm",
                                    rw == 0 ? "lee" : rw == 1 ? "escribe" : "lee/escribe", dma ? r[EXI_LEN] : ((r[EXI_CR] >> 4) & 3) + 1, r[EXI_DATA]);
    if (exi_is_card(ch)) {
        if (dma) mc_dma(r[EXI_MAR] & 0x03FFFFE0u, r[EXI_LEN], rw);
        else {
            int len = (int)((r[EXI_CR] >> 4) & 3) + 1, i;
            uint8_t b[4];
            for (i = 0; i < 4; i++) b[i] = (uint8_t)(r[EXI_DATA] >> (24 - 8 * i));
            mc_imm(b, len, rw);
            if (rw != 1) {
                uint32_t v = 0;
                for (i = 0; i < len; i++) v |= (uint32_t)b[i] << (24 - 8 * i);
                r[EXI_DATA] = v;
                if (mc_trace) rt_log("[exi]   -> %08X\n", v);
            }
        }
    } else if (ch == 0 && exi_dev(ch) == 1) {
        if (dma) ipl_dma(r[EXI_MAR] & 0x03FFFFE0u, r[EXI_LEN], rw);
        else ipl_imm(&r[EXI_DATA], (int)((r[EXI_CR] >> 4) & 3) + 1, rw);
    } else if (dma && rw == 0) {
        uint8_t* p = mem_ptr(r[EXI_MAR]);
        if (p) memset(p, 0, r[EXI_LEN]);
    } else if (!dma && rw == 0) {
        r[EXI_DATA] = 0;  /* ningun dispositivo responde */
    }
    r[EXI_CR] &= ~1u;
    r[EXI_CSR] |= 0x08;  /* TCINT */
    if (r[EXI_CSR] & 0x04) os_raise_interrupt(IRQ_EXI0_TC + 3 * ch);
}

/* ---- DSP, ARAM y DMA de audio ------------------------------------------- */

#define ARAM_SIZE (16u << 20)
static uint8_t* aram;
static uint16_t dsp[0x40];
static uint32_t ar_mm, ar_ar, ar_cnt;
static uint32_t dsp_mail_to_cpu;   /* buzon DSP -> CPU (bit 31 = hay mensaje) */
static uint32_t dsp_mail_from_cpu;
static uint64_t aidma_next_us;
static uint32_t aidma_addr, aidma_len;   /* bloque que esta "sonando" */
void audio_push(const uint8_t* be, uint32_t bytes);
int audio_queued_ms(void);
static uint32_t ai_cr, ai_vr, ai_it;
static uint64_t ai_start_us;
#define DSPCR (0x0A / 2)

void dsp_hle_mail(uint32_t mail, uint32_t* reply, int* have_reply);
void dsp_hle_reset(void);
extern int dsp_boot_pending;

static void dsp_raise(uint16_t bit, uint16_t mask, int irq) {
    uint16_t v = __atomic_or_fetch(&dsp[DSPCR], bit, __ATOMIC_SEQ_CST);
    if (v & mask) os_raise_interrupt(irq);
}

/* DSP de bajo nivel (port/dsp/dsp_lle.cpp) */
extern int dsp_lle_enabled;
uint16_t dsp_lle_read_cr(void);
uint16_t dsp_lle_write_cr(uint16_t v);
uint32_t dsp_lle_peek_mail(int cpu);
uint32_t dsp_lle_peek_mail_nowait(int cpu);
uint16_t dsp_lle_read_mail_hi(int cpu);
uint16_t dsp_lle_read_mail_lo(int cpu);
void dsp_lle_write_mail_hi(uint16_t v);
void dsp_lle_write_mail_lo(uint16_t v);
uint8_t* hw_aram_ptr(void) { return aram; }
/* el microcodigo pide interrupcion (DIRQ): llega desde el hilo del DSP */
void hw_dsp_interrupt_from_dsp(void) { dsp_raise(0x80, 0x100, IRQ_DSP_DSP); }
#define DSP_CR_MASK 0x0C07u

static void ar_dma(void) {
    uint32_t cnt = ar_cnt & 0x7FFFFFFF;
    int to_mm = (ar_cnt >> 31) & 1;
    uint8_t* mm = mem_ptr(ar_mm);
    uint32_t a = ar_ar & (ARAM_SIZE - 1);
    if (mm && a + cnt <= ARAM_SIZE && mem_ptr(ar_mm + cnt - 1)) {
        {   /* depuracion: avisar si se lee ARAM que nunca se escribio */
            static uint8_t* escrito; uint32_t k;
            if (!escrito) escrito = (uint8_t*)calloc(ARAM_SIZE / 256, 1);
            if (to_mm) {
                uint32_t sin = 0;
                for (k = a / 256; k < (a + cnt + 255) / 256; k++) if (!escrito[k]) sin++;
                if (sin) { static int n; if (n++ < 40) rt_log("[ar] lectura de ARAM sin escribir: ar=%08X cnt=%X (%u de %u bloques de 256) -> mm=%08X\n", a, cnt, sin, (cnt + 255) / 256, ar_mm); }
            } else for (k = a / 256; k < (a + cnt + 255) / 256; k++) escrito[k] = 1;
        }
        if (to_mm) memcpy(mm, aram + a, cnt);
        else memcpy(aram + a, mm, cnt);
    } else if (cnt) {
        rt_log("[ar] DMA invalido mm=%08X ar=%08X cnt=%08X\n", ar_mm, ar_ar, ar_cnt);
    }
    ar_cnt = 0;
    {   extern uint64_t hw_ar_dmas, hw_ar_juntados;
        hw_ar_dmas++;
        if (dsp[DSPCR] & 0x20) hw_ar_juntados++;   /* el aviso del DMA anterior todavia no se atendio */
    }
    dsp_raise(0x20, 0x40, IRQ_DSP_ARAM);
}
uint64_t hw_ar_dmas, hw_ar_juntados;

static uint32_t aidma_len_bytes(void) { return (uint32_t)(dsp[0x36 / 2] & 0x7FFF) * 32; }

/* ---- CP y PE ------------------------------------------------------------- */

static uint16_t cp[0x40];
static uint16_t pe[0x10];
#define PE_INTSR (0x0A / 2)

/* El procesador grafico corre en su propio hilo y puede adelantarse al juego. Cada
 * "fin de dibujo" y cada token con interrupcion tiene que llegarle al juego como una
 * interrupcion aparte: si dos se juntaran en una, el juego se queda esperando para
 * siempre la que se perdio (la intro se trababa). Antes el hilo grafico esperaba hasta
 * 100 ms a que el juego atendiera la anterior y despues las juntaba; en Windows, con el
 * disco mas lento, eso pasaba. Ahora se anotan en una cola y se entregan de a una: cuando
 * el juego atiende una (borra el bit de estado) sale la siguiente. */
static pthread_mutex_t s_pe_m = PTHREAD_MUTEX_INITIALIZER;
static uint32_t s_finish_owed;                 /* fines de dibujo pendientes de avisar */
static uint16_t s_tok_q[256];                  /* tokens con interrupcion pendientes */
static uint32_t s_tok_head, s_tok_tail;
uint32_t pe_debug_finish_total, pe_debug_token_total;

/* con s_pe_m tomado: si el estado esta libre y hay algo pendiente, lanzarlo */
static void pe_try_fire_locked(void) {
    uint16_t st = __atomic_load_n(&pe[PE_INTSR], __ATOMIC_SEQ_CST);
    if (!(st & 0x8) && s_finish_owed) {
        s_finish_owed--;
        st = __atomic_or_fetch(&pe[PE_INTSR], 0x8, __ATOMIC_SEQ_CST);
    }
    if (!(st & 0x4) && s_tok_head != s_tok_tail) {
        pe[0x0E / 2] = s_tok_q[s_tok_head++ & 255];
        st = __atomic_or_fetch(&pe[PE_INTSR], 0x4, __ATOMIC_SEQ_CST);
    }
    /* la interrupcion es "por nivel": mientras estado y habilitacion esten prendidos */
    if ((st & 0x8) && (st & 0x2)) os_raise_interrupt(IRQ_PE_FINISH);
    if ((st & 0x4) && (st & 0x1)) os_raise_interrupt(IRQ_PE_TOKEN);
}

void gx_pe_token(uint16_t token, int interrupt) {
    pthread_mutex_lock(&s_pe_m);
    if (!interrupt) pe[0x0E / 2] = token;
    else {
        pe_debug_token_total++;
        if (s_tok_tail - s_tok_head < 256) s_tok_q[s_tok_tail++ & 255] = token;
        pe_try_fire_locked();
    }
    pthread_mutex_unlock(&s_pe_m);
}

void gx_pe_finish(void) {
    void video_note_finish(void);
    video_note_finish();
    pthread_mutex_lock(&s_pe_m);
    pe_debug_finish_total++;
    { extern volatile uint32_t gx_finish_pendiente; __atomic_store_n(&gx_finish_pendiente, 0, __ATOMIC_RELEASE); }
    s_finish_owed++;
    pe_try_fire_locked();
    pthread_mutex_unlock(&s_pe_m);
}

/* el juego escribe el registro de estado: borrar lo atendido y lanzar lo que siga */
static void pe_write_intsr(uint16_t v) {
    pthread_mutex_lock(&s_pe_m);
    {
        uint16_t old = __atomic_load_n(&pe[PE_INTSR], __ATOMIC_SEQ_CST), nv;
        do nv = (uint16_t)(((old & ~(v & 0xC)) & ~0x3u) | (v & 0x3));
        while (!__atomic_compare_exchange_n(&pe[PE_INTSR], &old, nv, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST));
    }
    pe_try_fire_locked();
    pthread_mutex_unlock(&s_pe_m);
}

/* Red de seguridad: si todos los hilos esperan, volver a lanzar lo que este pendiente. */
void hw_pe_kick(void) {
    pthread_mutex_lock(&s_pe_m);
    pe_try_fire_locked();
    pthread_mutex_unlock(&s_pe_m);
}

void hw_debug_pe(void) {
    rt_log("[pe] estado %04X, fines de dibujo %u (pendientes %u), tokens %u (pendientes %u, ultimo %04X)\n",
           pe[PE_INTSR], pe_debug_finish_total, s_finish_owed, pe_debug_token_total,
           s_tok_tail - s_tok_head, pe[0x0E / 2]);
}

/* ---- acceso a registros --------------------------------------------------- */

uint32_t hw_read(uint32_t a, int size) {
    gx_fifo_flush();
    uint32_t off = a & 0xFFFF;
    uint32_t v = 0;
    if ((a & 0xFF000000u) != 0xCC000000u) { unknown("r", a, 0, size); return 0; }
    if (off < 0x1000) {                         /* CP */
        if (off == 0x00) return 0x000C;         /* GP inactivo, sin errores de cola */
        if (off >= 0x40) { static uint32_t vistos; uint32_t b = 1u << (((off - 0x40) >> 1) & 31); if (!(vistos & b)) { vistos |= b; rt_log("[cp] el juego lee el registro del CP %04X\n", off); } }
        v = cp[(off & 0x7F) / 2];
        if (size == 4) v = (v << 16) | cp[((off & 0x7F) + 2) / 2];
        return v;
    }
    if (off < 0x2000) {                         /* PE */
        if ((off & 0xFFF) >= 0x10) { static uint32_t vistos; uint32_t b = 1u << (((off & 0xFFF) >> 1) & 31); if (!(vistos & b)) { vistos |= b; rt_log("[pe] el juego lee el registro del PE %04X\n", off); } }
        v = pe[(off & 0x1F) / 2];
        return v;
    }
    if (off < 0x3000) {                         /* VI */
        uint32_t i = (off & 0xFF) / 2;
        if (off == 0x202C) {                    /* posicion actual del haz */
            vi_line = (uint32_t)(((os_now_us() % VI_FRAME_US) * 525) / VI_FRAME_US) + 1;
            return size == 4 ? (vi_line << 16) : vi_line;
        }
        if (off == 0x202E) return 1;
        if (i >= 0x40) return 0;
        v = vi[i];
        if (size == 4) v = (v << 16) | vi[i + 1];
        return v;
    }
    if (off < 0x4000) {                         /* PI */
        switch (off) {
        case 0x3000: return 0;                  /* causa: el despacho lo hace el runtime */
        case 0x3004: return pi_mask;
        case 0x300C: return pi_fifo_base;
        case 0x3010: return pi_fifo_end;
        case 0x3014: return pi_fifo_wptr;
        case 0x302C: return 0x20000000;         /* revision de la consola */
        default: unknown("r", a, 0, size); return 0;
        }
    }
    if (off >= 0x5000 && off < 0x5200) {        /* DSP */
        uint32_t i = (off & 0x1FF) / 2;
        switch (off) {
        case 0x5000:
            if (dsp_lle_enabled) {
                uint32_t m = dsp_lle_peek_mail(1);
                return size == 4 ? m : m >> 16;
            }
            return size == 4 ? dsp_mail_from_cpu : dsp_mail_from_cpu >> 16;
        case 0x5002: return dsp_lle_enabled ? (dsp_lle_peek_mail_nowait(1) & 0xFFFF) : (dsp_mail_from_cpu & 0xFFFF);
        case 0x5004:
            if (dsp_lle_enabled) {
                uint32_t hi = dsp_lle_read_mail_hi(0);
                if (size == 4) return (hi << 16) | dsp_lle_read_mail_lo(0);
                return hi;
            }
            if (!(dsp_mail_to_cpu & 0x80000000u) && dsp_boot_pending && !(dsp[DSPCR] & 0x4)) {
                dsp_boot_pending = 0;           /* la ROM del DSP saluda */
                dsp_mail_to_cpu = 0x8071FEEDu;
            }
            return size == 4 ? dsp_mail_to_cpu : dsp_mail_to_cpu >> 16;
        case 0x5006: {
            if (dsp_lle_enabled) return dsp_lle_read_mail_lo(0);
            uint32_t lo = dsp_mail_to_cpu & 0xFFFF;
            dsp_mail_to_cpu &= ~0x80000000u;    /* leer la parte baja consume el mensaje */
            return lo;
        }
        case 0x5020: return size == 4 ? ar_mm : ar_mm >> 16;
        case 0x5022: return ar_mm & 0xFFFF;
        case 0x5024: return size == 4 ? ar_ar : ar_ar >> 16;
        case 0x5026: return ar_ar & 0xFFFF;
        case 0x502A: return ar_cnt & 0xFFFF;
        case 0x5028: return size == 4 ? ar_cnt : ar_cnt >> 16;
        case 0x5016: return dsp[i] | 1;         /* ARAM inicializada */
        case 0x500A:
            if (dsp_lle_enabled) return (dsp[DSPCR] & ~DSP_CR_MASK) | (dsp_lle_read_cr() & DSP_CR_MASK);
            return dsp[DSPCR];
        case 0x503A: {                          /* bloques de DMA de audio restantes */
            return 0;
        }
        default:
            if (i < 0x40) {
                v = dsp[i];
                if (size == 4) v = (v << 16) | dsp[i + 1];
            }
            return v;
        }
    }
    if (off >= 0x6000 && off < 0x6040) {        /* DI */
        uint32_t i = (off - 0x6000) / 4;
        if (i < 10) return di[i];
        return 0;
    }
    if (off >= 0x6400 && off < 0x6500) {        /* SI */
        uint32_t r = off - 0x6400;
        if (r < 0x30) {
            int ch = r / 0x0C, k = r % 0x0C;
            si_update_inputs();
            if (k == 0) return si_out[ch];
            if (k == 4) return si_inh[ch];
            return si_inl[ch];
        }
        if (r == 0x30) return si_poll;
        if (r == 0x34) return si_comcsr;
        if (r == 0x38) {                        /* SISR: RDST = hay datos nuevos de sondeo */
            uint32_t v = si_sr;
            int ch;
            for (ch = 0; ch < 4; ch++) {
                PadState p;
                if ((si_poll >> (7 - ch)) & 1 && input_get_pad(ch, &p)) v |= 0x20u << ((3 - ch) * 8);
            }
            return v;
        }
        if (r == 0x3C) return si_exilk;
        if (r >= 0x80) {
            uint32_t b = r - 0x80;
            return ((uint32_t)si_buf[b] << 24) | ((uint32_t)si_buf[b + 1] << 16) | ((uint32_t)si_buf[b + 2] << 8) | si_buf[b + 3];
        }
        return 0;
    }
    if (off >= 0x6800 && off < 0x683C) {        /* EXI */
        uint32_t r = off - 0x6800;
        int ch = r / 0x14, k = (r % 0x14) / 4;
        if (k == EXI_CSR && ch == 0 && mc_present()) return exi[ch][k] | 0x1000;  /* EXT: hay tarjeta en la ranura A */
        return exi[ch][k];
    }
    if (off >= 0x6C00 && off < 0x6C20) {        /* AI */
        switch (off) {
        case 0x6C00: return ai_cr;
        case 0x6C04: return ai_vr;
        case 0x6C08:                            /* contador de muestras del streaming */
            return (ai_cr & 1) ? (uint32_t)((os_now_us() - ai_start_us) * 48 / 1000) : 0;
        case 0x6C0C: return ai_it;
        }
        return 0;
    }
    unknown("r", a, 0, size);
    return 0;
}

__attribute__((noinline)) void hw_debug_trap(uint32_t a, uint32_t v) { __asm__ volatile("" ::: "memory"); (void)a; (void)v; }

/* Datos del tubo de escritura (WGPIPE) a la memoria, en el puntero de la cola. */
static inline void hw_wgpipe_mem(uint32_t v, int size) {
    uint32_t wp = pi_fifo_wptr;
    if (!pi_fifo_end || wp + (uint32_t)size <= pi_fifo_end) {
        uint32_t o = (wp | 0x80000000u) - MEM_BASE;   /* la RAM es plana: sin pasar por mem_ptr */
        if (o <= MEM_SIZE - 4) {
            uint8_t* dst = g_mem + o;
            if (size == 4) { uint32_t b = __builtin_bswap32(v); memcpy(dst, &b, 4); }
            else if (size == 2) { uint16_t b = __builtin_bswap16((uint16_t)v); memcpy(dst, &b, 2); }
            else dst[0] = (uint8_t)v;
            pi_fifo_wptr = wp + (uint32_t)size;
            return;
        }
    }
    {
        int i;
        for (i = size - 1; i >= 0; i--) {
            uint8_t* d = mem_ptr(pi_fifo_wptr | 0x80000000u);
            if (d) *d = (uint8_t)(v >> (8 * i));
            pi_fifo_wptr++;
            if (pi_fifo_end && pi_fifo_wptr > pi_fifo_end) pi_fifo_wptr = pi_fifo_base;
        }
    }
}

/* Camino rapido para las escrituras al WGPIPE (0xCC008000), que son la mayoria. */
void hw_wgpipe(uint32_t v, int size) {
    hw_wgpipe_mem(v, size);
    if (cp[1] & 0x10) gx_fifo_write(v, size);
}

void hw_write(uint32_t a, uint32_t v, int size) {
    gx_fifo_flush();
    if (a == 0xCC006014u && (v >> 24) != 0x00 && (v >> 24) != 0x80 && (v >> 24) != 0x01) hw_debug_trap(a, v);
    uint32_t off = a & 0xFFFF;
    if ((a & 0xFFFF0000u) == 0xCC000000u && off >= 0x8000 && off < 0x9000) {
        /* WGPIPE: los datos van a la memoria, al puntero de escritura de la cola de
         * la CPU. Si esa cola esta enlazada con la del procesador grafico (bit 4
         * de CP_CR), el GP los consume enseguida; si no (por ejemplo mientras se
         * arma una lista de display), solo quedan en la memoria. */
        hw_wgpipe_mem(v, size);
        if (cp[1] & 0x10) gx_fifo_write(v, size);
        return;
    }
    if ((a & 0xFF000000u) != 0xCC000000u) { unknown("w", a, v, size); return; }
    if (off < 0x1000) {                         /* CP */
        uint32_t i = (off & 0x7F) / 2;
        if (size == 4) { cp[i] = (uint16_t)(v >> 16); cp[i + 1] = (uint16_t)v; }
        else cp[i] = (uint16_t)v;
        return;
    }
    if (off < 0x2000) {                         /* PE */
        uint32_t i = (off & 0x1F) / 2;
        if (i == PE_INTSR) { pe_write_intsr((uint16_t)v); return; }
        pe[i] = (uint16_t)v;
        return;
    }
    if (off < 0x3000) {                         /* VI */
        uint32_t i = (off & 0xFF) / 2;
        if (i >= 0x40) return;
        static uint32_t last_xfb;
        if (size == 4) { vi[i] = (uint16_t)(v >> 16); vi[i + 1] = (uint16_t)v; }
        else vi[i] = (uint16_t)v;
        /* un cuadro nuevo cuando se completa la direccion de la XFB (el juego escribe las dos
           mitades de 16 bits por separado: contar solo al escribir la de abajo) */
        if ((i == 0x1E / 2 || (size == 4 && i == 0x1C / 2)) && vi_xfb_addr() != last_xfb) { last_xfb = vi_xfb_addr(); video_game_frame(); }
        return;
    }
    if (off < 0x4000) {                         /* PI */
        switch (off) {
        case 0x3000: return;
        case 0x3004: pi_mask = v; return;
        case 0x300C: pi_fifo_base = v & 0x03FFFFE0; return;
        case 0x3010: pi_fifo_end = v & 0x03FFFFE0; { static int n; if (n++ < 4) rt_log("[pi] FIFO %08X-%08X\n", pi_fifo_base, pi_fifo_end); } return;
        case 0x3014: pi_fifo_wptr = v & 0x03FFFFE0; return;
        case 0x3018: case 0x3024: return;       /* reset de la FIFO, reset del sistema */
        default: unknown("w", a, v, size); return;
        }
    }
    if (off >= 0x5000 && off < 0x5200) {        /* DSP */
        uint32_t i = (off & 0x1FF) / 2;
        switch (off) {
        case 0x5000:
            if (dsp_lle_enabled) {
                dsp_lle_write_mail_hi((uint16_t)(size == 4 ? v >> 16 : v));
                if (size == 4) dsp_lle_write_mail_lo((uint16_t)v);
                return;
            }
            if (size == 4) { dsp_mail_from_cpu = v; goto mail_done; }
            dsp_mail_from_cpu = (dsp_mail_from_cpu & 0xFFFF) | (v << 16);
            return;
        case 0x5002:
            if (dsp_lle_enabled) { dsp_lle_write_mail_lo((uint16_t)v); return; }
            dsp_mail_from_cpu = (dsp_mail_from_cpu & 0xFFFF0000u) | (v & 0xFFFF);
        mail_done: {
            uint32_t reply = 0;
            int have = 0;
            dsp_hle_mail(dsp_mail_from_cpu, &reply, &have);
            dsp_mail_from_cpu &= 0x7FFFFFFF;    /* el DSP lo leyo */
            if (have) {
                dsp_mail_to_cpu = reply | 0x80000000u;
                dsp_raise(0x80, 0x100, IRQ_DSP_DSP);
            }
            return;
        }
        case 0x500A: {                          /* DSPCR: los bits de interrupcion se limpian con 1 */
            if (dsp_lle_enabled) {
                uint16_t cr = dsp_lle_write_cr((uint16_t)(v & DSP_CR_MASK));
                uint16_t old, nv;
                if (v & 1) dsp[0x36 / 2] = 0;   /* el reset apaga el DMA de audio */
                do {
                    old = __atomic_load_n(&dsp[DSPCR], __ATOMIC_SEQ_CST);
                    nv = (uint16_t)((old & 0xA8 & ~(v & 0xA8)) | (v & 0x0150) | (cr & DSP_CR_MASK));
                } while (!__atomic_compare_exchange_n(&dsp[DSPCR], &old, nv, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST));
                if (nv & 0x80 && nv & 0x100) os_raise_interrupt(IRQ_DSP_DSP);
                if (nv & 0x20 && nv & 0x40) os_raise_interrupt(IRQ_DSP_ARAM);
                if (nv & 0x08 && nv & 0x10) os_raise_interrupt(IRQ_DSP_AI);
                return;
            }
            uint16_t ints = (uint16_t)(v & 0xA8);
            uint16_t keep = dsp[DSPCR] & 0xA8 & ~ints;
            dsp[DSPCR] = (uint16_t)((v & ~0xA8u) | keep);
            if (v & 1) { dsp[DSPCR] &= ~1u; dsp_hle_reset(); }  /* el reset termina enseguida */
            dsp[DSPCR] &= ~0x0400u;             /* DMA de ARAM nunca ocupado */
            return;
        }
        case 0x5020: ar_mm = (size == 4) ? v : (ar_mm & 0xFFFF) | (v << 16); return;
        case 0x5022: ar_mm = (ar_mm & 0xFFFF0000u) | (v & 0xFFFF); return;
        case 0x5024: ar_ar = (size == 4) ? v : (ar_ar & 0xFFFF) | (v << 16); return;
        case 0x5026: ar_ar = (ar_ar & 0xFFFF0000u) | (v & 0xFFFF); return;
        case 0x5028:
            if (size == 4) { ar_cnt = v; ar_dma(); }
            else ar_cnt = (ar_cnt & 0xFFFF) | (v << 16);
            return;
        case 0x502A: ar_cnt = (ar_cnt & 0xFFFF0000u) | (v & 0xFFFF); ar_dma(); return;
        case 0x5036:
            dsp[i] = (uint16_t)v;
            if ((v & 0x8000) && !aidma_next_us) {
                aidma_next_us = os_now_us() + 1000;
                aidma_addr = ((uint32_t)dsp[0x30 / 2] << 16 | dsp[0x32 / 2]) & 0x03FFFFE0u;
                aidma_len = aidma_len_bytes();
            }
            if (!(v & 0x8000)) aidma_next_us = 0;
            return;
        default:
            if (i < 0x40) {
                if (size == 4) { dsp[i] = (uint16_t)(v >> 16); dsp[i + 1] = (uint16_t)v; }
                else dsp[i] = (uint16_t)v;
            }
            return;
        }
    }
    if (off >= 0x6000 && off < 0x6040) {        /* DI */
        uint32_t i = (off - 0x6000) / 4;
        if (i == DISR) {
            di[DISR] = (di[DISR] & ~(v & 0x54)) & ~0x2Au;
            di[DISR] |= v & 0x2A;
            di[DISR] &= ~(v & 0x54);
            return;
        }
        if (i == DICVR) { di[DICVR] = (di[DICVR] & ~(v & 0x4)) | (v & 0x2); return; }
        if (i < 10) di[i] = v;
        if (i == DICR && (v & 1)) di_command();
        return;
    }
    if (off >= 0x6400 && off < 0x6500) {        /* SI */
        uint32_t r = off - 0x6400;
        if (r < 0x30) {
            if (r % 0x0C == 0) {
                si_out[r / 0x0C] = v;
                /* el byte de abajo del comando 0x40 lleva el motor de vibracion: 1 = vibrar */
                if (r == 0 && (v >> 16) == 0x40) {
                    extern volatile int g_pad_rumble;
                    static int nlog;
                    int on = (v & 3) == 1;
                    if (on != g_pad_rumble && nlog < 6) { nlog++; rt_log("[pad] vibracion %s\n", on ? "si" : "no"); }
                    g_pad_rumble = on;
                }
            }
            return;
        }
        if (r == 0x30) { si_poll = v; return; }
        if (r == 0x34) {
            uint32_t clr = v & 0x80000000u;
            si_comcsr = (v & ~0x80000000u) | (si_comcsr & 0x80000000u & ~clr);
            if (v & 1) si_transfer();
            return;
        }
        if (r == 0x38) { si_sr &= ~(v & 0x0F0F0F0Fu); return; }
        if (r == 0x3C) { si_exilk = v; return; }
        if (r >= 0x80) {
            uint32_t b = r - 0x80;
            si_buf[b] = (uint8_t)(v >> 24); si_buf[b + 1] = (uint8_t)(v >> 16);
            si_buf[b + 2] = (uint8_t)(v >> 8); si_buf[b + 3] = (uint8_t)v;
        }
        return;
    }
    if (off >= 0x6800 && off < 0x683C) {        /* EXI */
        uint32_t r = off - 0x6800;
        int ch = r / 0x14, k = (r % 0x14) / 4;
        if (k == EXI_CSR) {
            /* los bits de interrupcion (1, 3, 11) se limpian escribiendo 1 */
            uint32_t ints = 0x80Au;
            int was_card = exi_is_card(ch), was_ipl = ch == 0 && exi_dev(0) == 1;
            exi[ch][EXI_CSR] = (v & ~ints) | (exi[ch][EXI_CSR] & ints & ~(v & ints));
            if (ch == 0 && (exi_dev(0) == 1) != was_ipl) s_ipl_have_cmd = 0;   /* nuevo comando al seleccionar */
            if (ch == 0 && mc_present()) {
                int is_card = exi_is_card(ch);
                if (is_card && !was_card) mc_select(1);
                if (!is_card && was_card) { mc_select(0); exi_card_irq(); }
            }
            return;
        }
        exi[ch][k] = v;
        if (k == EXI_CR && (v & 1)) exi_transfer(ch);
        return;
    }
    if (off >= 0x6C00 && off < 0x6C20) {        /* AI */
        switch (off) {
        case 0x6C00:
            if ((v & 1) && !(ai_cr & 1)) ai_start_us = os_now_us();
            { static uint32_t last = 0xFFFFFFFF; static int n; if ((v & ~0x38u) != last) { last = v & ~0x38u; if (n++ < 24 || (v & 0x40)) rt_log("[ai] AICR %08X (DMA a %s kHz)\n", v, (v & 0x40) ? "32" : "48"); } }
            ai_cr = v & ~0x20u;                 /* SCRESET termina enseguida */
            return;
        case 0x6C04: ai_vr = v; return;
        case 0x6C0C: ai_it = v; return;
        }
        return;
    }
    unknown("w", a, v, size);
}

/* ---- eventos ----------------------------------------------------------- */

void hw_init(void) {
    sram_init();
    aram = (uint8_t*)calloc(1, ARAM_SIZE);
    vi_next_us = os_now_us() + VI_FRAME_US;
    dsp[DSPCR] = 0;
    di[DICVR] = 0;  /* tapa cerrada */
    si_sr = 0;
}

uint64_t hw_ai_late_max_us;   /* lo mas tarde que se atendio un bloque de audio (registro) */
/* Si el hilo del juego se durmio de mas (en Windows los temporizadores despiertan hasta
 * ~15 ms tarde), los bloques de audio vencidos se atenderian de a varios seguidos, sin dejar
 * que el juego y el DSP mezclen el siguiente: se mandaria a la placa un pedazo viejo y la
 * musica se escucha trabada. Por eso no se atiende un bloque mientras el DSP siga trabajando
 * tenga un aviso para el juego sin atender, ni antes de 1,5 ms del anterior (limite 8 ms). */
static uint64_t ai_defer_until;
/* Escrituras del DSP a la RAM (el DSP mezcla cada bloque de audio y lo copia con su DMA).
 * Se anotan para saber si el bloque que va a sonar ya fue escrito de nuevo o si todavia
 * tiene lo de la vuelta anterior (el juego usa una ronda de 4 bloques: sonaria un pedazo
 * de 20 ms atras, un "tic" en la musica). */
static struct { uint32_t addr, len, seq; } s_dspw[32];
static volatile uint32_t s_dspw_seq;
void hw_dsp_escribio(uint32_t addr, uint32_t len) {
    uint32_t n = __atomic_add_fetch(&s_dspw_seq, 1, __ATOMIC_ACQ_REL);
    s_dspw[n & 31].addr = addr & 0x01FFFFFFu; s_dspw[n & 31].len = len; s_dspw[n & 31].seq = n;
}
static struct { uint32_t addr, seq; } s_ai_blk[8];   /* bloques de audio: hasta donde se habia escrito cuando sonaron */
static uint32_t ai_blk_slot(uint32_t addr) {
    uint32_t i, viejo = 0;
    for (i = 0; i < 8; i++) if (s_ai_blk[i].addr == addr) return i;
    for (i = 1; i < 8; i++) if (s_ai_blk[i].seq < s_ai_blk[viejo].seq) viejo = i;
    s_ai_blk[viejo].addr = addr; s_ai_blk[viejo].seq = 0;
    return viejo;
}
/* 1 si desde la ultima vez que sono este bloque el DSP no escribio nada encima */
static int ai_bloque_viejo(uint32_t addr, uint32_t len) {
    uint32_t a = addr & 0x01FFFFFFu, i = ai_blk_slot(a), desde = s_ai_blk[i].seq, hasta = __atomic_load_n(&s_dspw_seq, __ATOMIC_ACQUIRE), k;
    if (!desde) return 0;                       /* primera vez que suena */
    if (hasta - desde >= 32) return 0;          /* se perdio la cuenta: no trabar */
    for (k = desde + 1; k - desde <= hasta - desde; k++) {
        uint32_t wa = s_dspw[k & 31].addr, wl = s_dspw[k & 31].len;
        if (s_dspw[k & 31].seq == k && wa < a + len && a < wa + wl) return 0;
    }
    return 1;
}
static void ai_bloque_sono(uint32_t addr) {
    uint32_t a = addr & 0x01FFFFFFu;
    s_ai_blk[ai_blk_slot(a)].seq = __atomic_load_n(&s_dspw_seq, __ATOMIC_ACQUIRE);
}
uint64_t hw_ai_juntados, hw_ai_viejos, hw_ai_esperas;   /* estadisticas para el registro */
/* Cuando le toca sonar a un bloque, se espera (sin limite fijo: mientras la placa de sonido
 * tenga mas de 15 ms encolados) si:
 *  - el juego todavia no atendio el aviso del bloque anterior: si se avisa otra vez antes,
 *    los dos avisos se juntan en uno, el juego pide al DSP una sola mezcla y un bloque de la
 *    ronda queda sin escribir (en Windows el hilo del juego a veces se atrasa 10-25 ms);
 *  - el DSP todavia no escribio el bloque que va a sonar;
 *  - pasaron menos de 1,5 ms desde el anterior (el DSP necesita tiempo para mezclar).
 * RL_AUDIO_SIN_ESPERA_DSP=1 lo apaga. */
static int ai_esperar_dsp(uint64_t now) {
    static int off = -1;
    static uint64_t last;
    int q, pend;
    if (off < 0) off = getenv("RL_AUDIO_SIN_ESPERA_DSP") != NULL;
    if (off) { last = now; return 0; }
    q = audio_queued_ms();
    { static int viejo = -1; if (viejo < 0) viejo = getenv("RL_AUDIO_LIMITE_8MS") != NULL; if (viejo) q = now - aidma_next_us > 8000 ? 0 : 100; }   /* comparar con antes */
    if (q >= 0 ? q <= 15 : now - aidma_next_us > 40000) {   /* no se puede esperar mas: suena lo que haya */
        if (os_interrupt_pending(IRQ_DSP_AI)) hw_ai_juntados++;
        if (aidma_len && ai_bloque_viejo(aidma_addr, aidma_len)) hw_ai_viejos++;
        last = now;
        return 0;
    }
    pend = os_interrupt_pending(IRQ_DSP_DSP) || os_interrupt_pending(IRQ_DSP_AI);
    if (pend || (aidma_len && ai_bloque_viejo(aidma_addr, aidma_len))) {
        if (now - aidma_next_us < 300) hw_ai_esperas++;
        ai_defer_until = now + 300;
        return 1;
    }
    if (now - last < 1500) { ai_defer_until = last + 1500; return 1; }   /* minimo 1,5 ms entre bloques: el DSP mezcla el siguiente */
    last = now;
    return 0;
}
/* El juego dibuja un cuadro, pide "fin de dibujo" y espera el retrazado; si el fin de
 * dibujo llega despues del retrazado, espera otro mas y ese cuadro dura el doble (en la
 * pausa el juego iba a 52 cuadros por segundo y la musica, que avanza con los cuadros, se
 * escuchaba lenta y trabada). En la consola el procesador grafico termina a tiempo; aca el
 * hilo grafico puede venir unos ms atrasado. Si el juego ya pidio el fin de dibujo y el hilo
 * grafico todavia no llego, el retrazado se demora hasta 6 ms (el siguiente sigue en su hora,
 * asi que siguen siendo 60 por segundo). RL_VI_SIN_ESPERA=1 lo apaga. */
volatile uint32_t gx_finish_requests, gx_finish_pendiente;
static int vi_postergado;
uint64_t hw_vi_postergados;
static int vi_esperar_dibujo(uint64_t now) {
    static int off = -1;
    if (off < 0) off = getenv("RL_VI_SIN_ESPERA") != NULL;
    if (off || now - vi_next_us > 6000) return 0;
    if (!__atomic_load_n(&gx_finish_pendiente, __ATOMIC_ACQUIRE)) return 0;
    if (!vi_postergado) { vi_postergado = 1; hw_vi_postergados++; }
    return 1;
}

uint64_t hw_hueco_max_us, hw_huecos_20ms;   /* lo mas que paso sin atender el hardware (estadistica) */
void hw_update(void) {
    uint64_t now = os_now_us();
    {   static uint64_t prev;
        if (prev && now - prev > hw_hueco_max_us) hw_hueco_max_us = now - prev;
        if (prev && now - prev > 20000) hw_huecos_20ms++;
        prev = now; }
    if (now >= vi_next_us && vi_esperar_dibujo(now)) {
        /* el retrazado se demora un poco: ver vi_esperar_dibujo */
    } else if (now >= vi_next_us) {
        vi_postergado = 0;
        vi_next_us += VI_FRAME_US;
        if (vi_next_us < now) vi_next_us = now + VI_FRAME_US;
        vi_event();
    }
    if (aidma_next_us && now >= aidma_next_us && !ai_esperar_dsp(now)) {
        /* un bloque de audio de 32 kHz estereo de 16 bits: 4 bytes por muestra */
        uint32_t len;
        uint64_t dur;
        int q;
        if (now - aidma_next_us > hw_ai_late_max_us) hw_ai_late_max_us = now - aidma_next_us;
        if (aidma_len) { audio_push(mem_ptr(aidma_addr), aidma_len); ai_bloque_sono(aidma_addr); }
        /* el siguiente bloque: lo que el juego dejo configurado */
        aidma_addr = ((uint32_t)dsp[0x30 / 2] << 16 | dsp[0x32 / 2]) & 0x03FFFFE0u;
        aidma_len = len = aidma_len_bytes();
        dur = len ? (uint64_t)len * 1000000ull / (32000ull * 4) : 5000;
        /* Mantener la cola de la placa de sonido cerca de 80 ms. La correccion es suave
           (hasta 2%): antes se aceleraba o frenaba el audio un 10% de golpe cada vez que la
           cola cruzaba un limite, y como el juego avanza la musica con cada bloque, el ritmo
           se escuchaba desparejo ("trabado"). Solo si la cola casi se vacia se corrige fuerte. */
        q = audio_queued_ms();
        if (q >= 0) {
            static double avg = -1;
            double f;
            if (avg < 0) avg = q;
            avg += (q - avg) * 0.02;            /* promedio de ~50 bloques (~250 ms) */
            f = 1.0 + (avg - 80.0) / 80.0 * 0.05;
            if (f < 0.97) f = 0.97;
            if (f > 1.03) f = 1.03;
            if (q < 15) f = 0.9;                /* casi vacia: rellenar rapido */
            else if (q > 200) f = 1.1;          /* demasiado atrasada: vaciar */
            dur = (uint64_t)((double)dur * f);
        }
        aidma_next_us += dur;
        if (aidma_next_us + 20000 < now) aidma_next_us = now + dur;
        dsp_raise(0x08, 0x10, IRQ_DSP_AI);
    }
    if (s_card_irq_at && now >= s_card_irq_at) exi_card_irq_fire();
    if (__atomic_load_n(&s_di_listo, __ATOMIC_SEQ_CST)) { __atomic_store_n(&s_di_listo, 0, __ATOMIC_SEQ_CST); di_terminar(); }
}

uint64_t hw_next_event_us(void) {
    uint64_t n = vi_next_us;
    if (__atomic_load_n(&s_di_listo, __ATOMIC_SEQ_CST)) return 0;
    if (vi_postergado) { uint64_t t = os_now_us() + 300; if (t > n) n = t; }
    if (aidma_next_us) {
        uint64_t a = aidma_next_us > ai_defer_until ? aidma_next_us : ai_defer_until;
        if (a < n) n = a;
    }
    if (s_card_irq_at && s_card_irq_at < n) n = s_card_irq_at;
    return n;
}

/* Para el registro cuando el juego se traba: estado del audio. */
void hw_debug_audio(void) {
    int audio_queued_ms(void);
    uint64_t dsp_lle_cycles(void);
    uint64_t now = os_now_us();
    rt_log("[audio] DMA de audio %s (proximo en %lld us, %u bytes desde %08X), cola de la placa %d ms, DSPCR %04X, ciclos del DSP %llu\n",
           aidma_next_us ? "activo" : "parado", aidma_next_us ? (long long)(aidma_next_us - now) : 0LL, aidma_len, aidma_addr,
           audio_queued_ms(), dsp[DSPCR], (unsigned long long)dsp_lle_cycles());
}
