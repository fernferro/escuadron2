/*
 * Memoria de la GameCube para el port.
 *
 * - 0x7F000000..0x81800000: arreglo plano (VM del juego + 24 MB de RAM).
 * - 0x00000000..0x01800000: direcciones fisicas -> se ven como 0x80000000+.
 * - 0xC0000000..0xC1800000: espejo sin cache de la RAM -> 0x80000000+.
 * - 0xE0000000..0xE0004000: cache bloqueada (scratchpad de 16 KB).
 * - 0xCC000000..0xCC010000: registros de hardware (hw_read/hw_write).
 */
#include <stdio.h>
#include <stdlib.h>
#include "ppc.h"
#include <string.h>
#include "runtime.h"

uint8_t* g_mem;
static uint8_t s_locked_cache[0x4000];
uint8_t* g_lc = s_locked_cache;   /* para el camino rapido de ppc.h */

void mem_init(void) {
    g_mem = (uint8_t*)calloc(1, MEM_SIZE);
    if (!g_mem) {
        fprintf(stderr, "sin memoria\n");
        exit(1);
    }
}

/* Traduce a puntero host; NULL si es hardware o una direccion invalida. */
static uint8_t* xlat(uint32_t a, uint32_t n) {
    uint32_t o;
    if (a < 0x01800000u) a |= 0x80000000u;
    /* Direcciones "fisicas" de la memoria virtual del juego (0x7F000000-0x7FFFFFFF):
     * los DMA (disco, ARAM, cache bloqueada) y GX recortan la direccion a 26, 28 o 29
     * bits; en la consola el juego las traduce con su tabla de paginas, aca la memoria
     * virtual es plana y se reconocen por el rango (que en la consola no existe). */
    else if ((a & 0xFF000000u) == 0x83000000u || (a & 0xFF000000u) == 0x03000000u) a = 0x7C000000u | (a & 0x03FFFFFFu);
    else if ((a & 0xFF000000u) == 0x8F000000u || (a & 0xFF000000u) == 0x0F000000u) a = 0x70000000u | (a & 0x0FFFFFFFu);
    else if ((a & 0xFF000000u) == 0x9F000000u || (a & 0xFF000000u) == 0x1F000000u) a = 0x60000000u | (a & 0x1FFFFFFFu);
    else if (a >= 0xC0000000u && a < 0xC1800000u) a -= 0x40000000u;
    /* Mas alla de los 24 MB: la consola (y Dolphin) ignoran los bits altos de la direccion
     * fisica, asi que 0x82160E30 es un espejo de 0x80160E30. El juego lee ahi por un error
     * propio (una rutina de choques con el terreno usa un indice sin inicializar); antes
     * devolvia 0 y la camara y el terreno quedaban distintos que en la consola. */
    else if (a >= 0x81800000u && a < 0x90000000u && ((a & 0x01FFFFFFu) < 0x01800000u)) a = 0x80000000u | (a & 0x01FFFFFFu);
    else if (a >= 0xE0000000u && a < 0xE0004000u) {
        if (a + n > 0xE0004000u) return NULL;
        return s_locked_cache + (a - 0xE0000000u);
    }
    o = a - MEM_BASE;
    if (o <= MEM_SIZE - n) return g_mem + o;
    return NULL;
}

uint8_t* mem_ptr(uint32_t a) { return xlat(a, 1); }

void hw_wgpipe(uint32_t v, int size);
static int is_hw(uint32_t a) { return (a & 0xFF000000u) == 0xCC000000u || (a & 0xFF000000u) == 0xCD000000u; }

static void bad_access(const char* kind, uint32_t a) {
    static unsigned count;   /* sin signo y sin pasarse: antes daba la vuelta y volvia a avisar */
    if (count < 51) count++;
    if (count <= 50) rt_log("acceso invalido (%s) a 0x%08X\n", kind, a);
    if (count == 50) rt_log("... (se omiten mas avisos de acceso invalido)\n");
}

uint8_t mem_slow_r8(uint32_t a) {
    uint8_t* p = xlat(a, 1);
    if (p) return *p;
    if (is_hw(a)) return (uint8_t)hw_read(a, 1);
    bad_access("r8", a);
    return 0;
}
uint16_t mem_slow_r16(uint32_t a) {
    uint8_t* p = xlat(a, 2);
    if (p) return (uint16_t)((p[0] << 8) | p[1]);
    if (is_hw(a)) return (uint16_t)hw_read(a, 2);
    bad_access("r16", a);
    return 0;
}
uint32_t mem_slow_r32(uint32_t a) {
    uint8_t* p = xlat(a, 4);
    if (p) return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
    if (is_hw(a)) return hw_read(a, 4);
    bad_access("r32", a);
    {   /* quien leyo (direccion del codigo de la PC; ver con addr2line sobre escuadron2) */
        static int n;
        extern int main(int, char**);
        if (n++ < 8) rt_log("  leido desde %p (main en %p)\n", __builtin_return_address(0), (void*)main);
    }
    return 0;
}
uint64_t mem_slow_r64(uint32_t a) {
    return ((uint64_t)mem_slow_r32(a) << 32) | mem_slow_r32(a + 4);
}
void mem_slow_w8(uint32_t a, uint8_t v) {
    if ((a & 0xFFFFF000u) == 0xCC008000u) { hw_wgpipe(v, 1); return; }
    uint8_t* p = xlat(a, 1);
    if (p) { *p = v; return; }
    if (is_hw(a)) { hw_write(a, v, 1); return; }
    bad_access("w8", a);
}
void mem_slow_w16(uint32_t a, uint16_t v) {
    if ((a & 0xFFFFF000u) == 0xCC008000u) { hw_wgpipe(v, 2); return; }
    uint8_t* p = xlat(a, 2);
    if (p) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; return; }
    if (is_hw(a)) { hw_write(a, v, 2); return; }
    bad_access("w16", a);
}
void mem_slow_w32(uint32_t a, uint32_t v) {
    if ((a & 0xFFFFF000u) == 0xCC008000u) { hw_wgpipe(v, 4); return; }
    uint8_t* p = xlat(a, 4);
    if (p) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; return; }
    if (is_hw(a)) { hw_write(a, v, 4); return; }
    bad_access("w32", a);
}
void mem_slow_w64(uint32_t a, uint64_t v) {
    if (is_hw(a)) {  /* stfd al tubo de escritura de GX: dos palabras */
        hw_write(a, (uint32_t)(v >> 32), 4);
        hw_write(a, (uint32_t)v, 4);
        return;
    }
    mem_slow_w32(a, (uint32_t)(v >> 32));
    mem_slow_w32(a + 4, (uint32_t)v);
}

/* ---- paired singles cuantizados --------------------------------------- */

double ppc_qscale[64], ppc_dqscale[64];   /* para PSQ_L/PSQ_ST en linea (ppc.h) */
static double s_qscale[64], s_dqscale[64];
static int s_qinit;
__attribute__((constructor)) static void qscale_init(void) {
    int k;
    for (k = 0; k < 64; k++) { ppc_qscale[k] = ldexp(1.0, (k & 0x20) ? k - 64 : k); ppc_dqscale[k] = 1.0 / ppc_qscale[k]; }
}
static double ldexp_scale(int scale) {
    /* escala con signo de 6 bits (tabla: ldexp es lento) */
    if (!s_qinit) {
        int k;
        for (k = 0; k < 64; k++) { s_qscale[k] = ldexp(1.0, (k & 0x20) ? k - 64 : k); s_dqscale[k] = 1.0 / s_qscale[k]; }
        s_qinit = 1;
    }
    return s_qscale[scale & 0x3F];
}

static double load_q(uint32_t type, double dequant, uint32_t ea, uint32_t* size) {
    switch (type) {
    case 4: *size = 1; return (double)R8(ea) * dequant;
    case 5: *size = 2; return (double)R16(ea) * dequant;
    case 6: *size = 1; return (double)(int8_t)R8(ea) * dequant;
    case 7: *size = 2; return (double)(int16_t)R16(ea) * dequant;
    default: *size = 4; return (double)u32_f(R32(ea));
    }
}

void psq_load(PPCContext* ctx, int rd, uint32_t ea, int w, int i) {
    uint32_t gqr = ctx->gqr[i];
    uint32_t type = (gqr >> 16) & 7;
    double dq;
    if (type < 4) dq = 1.0;
    else { ldexp_scale(0); dq = s_dqscale[(gqr >> 24) & 0x3F]; }
    uint32_t size;
    double v0 = load_q(type, dq, ea, &size);
    double v1 = w ? 1.0 : load_q(type, dq, ea + size, &size);
    ctx->f[rd] = v0;
    ctx->ps1[rd] = v1;
}

static void store_q(uint32_t type, double q, double v, uint32_t ea, uint32_t* size) {
    double x = v * q;
    switch (type) {
    case 4: *size = 1; W8(ea, (uint8_t)(x < 0 ? 0 : x > 255 ? 255 : x)); break;
    case 5: *size = 2; W16(ea, (uint16_t)(x < 0 ? 0 : x > 65535 ? 65535 : x)); break;
    case 6: *size = 1; W8(ea, (uint8_t)(int8_t)(x < -128 ? -128 : x > 127 ? 127 : x)); break;
    case 7: *size = 2; W16(ea, (uint16_t)(int16_t)(x < -32768 ? -32768 : x > 32767 ? 32767 : x)); break;
    default: *size = 4; W32(ea, f_u32((float)v)); break;
    }
}

void psq_store(PPCContext* ctx, int rs, uint32_t ea, int w, int i) {
    uint32_t gqr = ctx->gqr[i];
    uint32_t type = gqr & 7;
    double q = type < 4 ? 1.0 : ldexp_scale((int)((gqr >> 8) & 0x3F));
    uint32_t size;
    store_q(type, q, ctx->f[rs], ea, &size);
    if (!w) store_q(type, q, ctx->ps1[rs], ea + size, &size);
}

/* DMA de la cache bloqueada (registros DMA_U = spr 922, DMA_L = spr 923).
 * El juego calcula matrices en la cache bloqueada (0xE0000000) y las copia a la
 * RAM con este DMA para que las lea el procesador grafico. */
void ppc_dma(PPCContext* ctx) {
    uint32_t u = ctx->spr[922], l = ctx->spr[923];
    uint32_t lines, bytes, ram, lc;
    uint8_t* pr;
    uint8_t* pl;
    if (!(l & 2)) return;                          /* T: iniciar */
    lines = ((u & 0x1F) << 2) | ((l >> 2) & 3);
    if (!lines) lines = 128;
    bytes = lines * 32;
    ram = u & 0xFFFFFFE0u;
    lc = l & 0xFFFFFFE0u;
    {
        static int n;
        if (((ram & 0x0FFFFFFFu) < 0x00100000u || !xlat(ram, bytes)) && n++ < 50) rt_log("[dma] rara: %s ram=%08X lc=%08X %u bytes lr=%08X r28=%08X r29=%08X\n", (l & 0x10) ? "carga" : "guarda", ram, lc, bytes, ctx->lr, ctx->r[28], ctx->r[29]);
    }
    pr = xlat(ram, bytes);
    pl = xlat(lc, bytes);
    if (pr && pl) {
        if (l & 0x10) memcpy(pl, pr, bytes);        /* LD: RAM -> cache */
        else memcpy(pr, pl, bytes);                /* cache -> RAM */
    } else {
        static int warned;
        if (warned++ < 10) rt_log("[dma] copia invalida ram=%08X lc=%08X %u bytes\n", ram, lc, bytes);
    }
    ctx->spr[923] = l & ~2u;                       /* termino enseguida */
}
