/*
 * Estado del procesador Gekko y acceso a memoria para el codigo recompilado.
 *
 * Cada funcion del juego se traduce a `void f_XXXXXXXX(PPCContext* ctx)`.
 * Los registros viven en el contexto; la memoria de la GameCube es un arreglo
 * plano en big-endian (MEM_BASE .. MEM_BASE+MEM_SIZE). Todo lo que cae fuera de
 * ese rango (registros de hardware, espejos sin cache, cache bloqueada) pasa
 * por las funciones lentas de memory.c.
 */
#ifndef PPC_H
#define PPC_H

#include <stdint.h>
#include <string.h>
#include <math.h>

#ifdef _MSC_VER
#include <stdlib.h>
#define bswap16(x) _byteswap_ushort(x)
#define bswap32(x) _byteswap_ulong(x)
#define bswap64(x) _byteswap_uint64(x)
#define PPC_INLINE static __forceinline
#else
#define bswap16(x) __builtin_bswap16(x)
#define bswap32(x) __builtin_bswap32(x)
#define bswap64(x) __builtin_bswap64(x)
#define PPC_INLINE static inline __attribute__((always_inline))
#endif

typedef struct PPCContext {
    uint32_t r[32];
    double f[32];      /* ps0 (o el valor double) */
    double ps1[32];    /* segunda mitad de los paired singles */
    uint32_t cr;       /* CR completo, numeracion PowerPC (bit 0 = el mas alto) */
    uint32_t lr;
    uint32_t ctr;
    uint32_t xer_ca;
    uint32_t xer_so;
    uint32_t fpscr;
    uint32_t msr;
    uint32_t gqr[8];
    uint32_t spr[1024];
    void* thread;      /* hilo emulado al que pertenece (runtime) */
} PPCContext;

typedef void (*PPCFunc)(PPCContext* ctx);

/* ---- memoria ---------------------------------------------------------- */

/* 0x7F000000..0x81800000: memoria virtual del juego (VM) + RAM principal. */
#define MEM_BASE 0x7F000000u
#define MEM_SIZE 0x02800000u

extern uint8_t* g_mem;

uint8_t  mem_slow_r8(uint32_t a);
uint16_t mem_slow_r16(uint32_t a);
uint32_t mem_slow_r32(uint32_t a);
uint64_t mem_slow_r64(uint32_t a);
void hw_wgpipe(uint32_t v, int size);
void mem_slow_w8(uint32_t a, uint8_t v);
void mem_slow_w16(uint32_t a, uint16_t v);
void mem_slow_w32(uint32_t a, uint32_t v);
void mem_slow_w64(uint32_t a, uint64_t v);

/* cache bloqueada (0xE0000000, 16 KB): el juego arma ahi matrices y vertices. Camino
   rapido en linea para no pasar por mem_slow_* en cada acceso. */
extern uint8_t* g_lc;
#define LC_OFF(a, n) ((uint32_t)(a) - 0xE0000000u <= 0x4000u - (n))
/* Puntero host para una direccion de la GameCube (solo memoria normal). */
uint8_t* mem_ptr(uint32_t a);

/* Los saltos hacia atras (bucles) cuentan; cada tanto se entregan las interrupciones
 * pendientes, como haria el hardware en medio de una espera activa. */
extern int32_t rt_loop_budget;
void os_loop_poll(PPCContext* ctx);
#define RT_LOOP_POLL(ctx) do { if (__builtin_expect(--rt_loop_budget < 0, 0)) os_loop_poll(ctx); } while (0)

PPC_INLINE uint8_t R8(uint32_t a) {
    uint32_t o = a - MEM_BASE;
    if (o < MEM_SIZE) return g_mem[o];
    if (LC_OFF(a, 1)) return g_lc[a - 0xE0000000u];
    return mem_slow_r8(a);
}
PPC_INLINE uint16_t R16(uint32_t a) {
    uint32_t o = a - MEM_BASE;
    if (o < MEM_SIZE - 1) { uint16_t v; memcpy(&v, g_mem + o, 2); return bswap16(v); }
    if (LC_OFF(a, 2)) { uint16_t v; memcpy(&v, g_lc + (a - 0xE0000000u), 2); return bswap16(v); }
    return mem_slow_r16(a);
}
PPC_INLINE uint32_t R32(uint32_t a) {
    uint32_t o = a - MEM_BASE;
    if (o < MEM_SIZE - 3) { uint32_t v; memcpy(&v, g_mem + o, 4); return bswap32(v); }
    if (LC_OFF(a, 4)) { uint32_t v; memcpy(&v, g_lc + (a - 0xE0000000u), 4); return bswap32(v); }
    return mem_slow_r32(a);
}
PPC_INLINE uint64_t R64(uint32_t a) {
    uint32_t o = a - MEM_BASE;
    if (o < MEM_SIZE - 7) { uint64_t v; memcpy(&v, g_mem + o, 8); return bswap64(v); }
    if (LC_OFF(a, 8)) { uint64_t v; memcpy(&v, g_lc + (a - 0xE0000000u), 8); return bswap64(v); }
    return mem_slow_r64(a);
}
PPC_INLINE void W8(uint32_t a, uint8_t v) {
    uint32_t o = a - MEM_BASE;
    if (o < MEM_SIZE) { g_mem[o] = v; return; }
    if ((a & 0xFFFFF000u) == 0xCC008000u) { hw_wgpipe(v, 1); return; }   /* tubo de GX: lo mas comun fuera de la RAM */
    if (LC_OFF(a, 1)) { g_lc[a - 0xE0000000u] = v; return; }
    mem_slow_w8(a, v);
}
PPC_INLINE void W16(uint32_t a, uint16_t v) {
    uint32_t o = a - MEM_BASE;
    if (o < MEM_SIZE - 1) { v = bswap16(v); memcpy(g_mem + o, &v, 2); return; }
    if ((a & 0xFFFFF000u) == 0xCC008000u) { hw_wgpipe(v, 2); return; }   /* tubo de GX: lo mas comun fuera de la RAM */
    if (LC_OFF(a, 2)) { v = bswap16(v); memcpy(g_lc + (a - 0xE0000000u), &v, 2); return; }
    mem_slow_w16(a, v);
}
PPC_INLINE void W32(uint32_t a, uint32_t v) {
    uint32_t o = a - MEM_BASE;
    if (o < MEM_SIZE - 3) { v = bswap32(v); memcpy(g_mem + o, &v, 4); return; }
    if ((a & 0xFFFFF000u) == 0xCC008000u) { hw_wgpipe(v, 4); return; }   /* tubo de GX: lo mas comun fuera de la RAM */
    if (LC_OFF(a, 4)) { v = bswap32(v); memcpy(g_lc + (a - 0xE0000000u), &v, 4); return; }
    mem_slow_w32(a, v);
}
PPC_INLINE void W64(uint32_t a, uint64_t v) {
    uint32_t o = a - MEM_BASE;
    if (o < MEM_SIZE - 7) { v = bswap64(v); memcpy(g_mem + o, &v, 8); return; }
    if (LC_OFF(a, 8)) { v = bswap64(v); memcpy(g_lc + (a - 0xE0000000u), &v, 8); return; }
    mem_slow_w64(a, v);
}

/* ---- conversiones de bits --------------------------------------------- */

PPC_INLINE float u32_f(uint32_t v) { float f; memcpy(&f, &v, 4); return f; }
PPC_INLINE uint32_t f_u32(float f) { uint32_t v; memcpy(&v, &f, 4); return v; }
PPC_INLINE double u64_d(uint64_t v) { double d; memcpy(&d, &v, 8); return d; }
PPC_INLINE uint64_t d_u64(double d) { uint64_t v; memcpy(&v, &d, 8); return v; }

/* Redondeo a simple precision, como hace el Gekko en las operaciones "s". */
PPC_INLINE double RS(double d) { return (double)(float)d; }

/* ---- registro de condicion -------------------------------------------- */

#define CR_LT 8u
#define CR_GT 4u
#define CR_EQ 2u
#define CR_SO 1u

PPC_INLINE void cr_set(PPCContext* ctx, int field, uint32_t v) {
    int sh = 28 - 4 * field;
    ctx->cr = (ctx->cr & ~(0xFu << sh)) | (v << sh);
}
PPC_INLINE uint32_t cr_bit(PPCContext* ctx, int bit) { return (ctx->cr >> (31 - bit)) & 1u; }
PPC_INLINE void cr_setbit(PPCContext* ctx, int bit, uint32_t v) {
    uint32_t m = 1u << (31 - bit);
    ctx->cr = v ? (ctx->cr | m) : (ctx->cr & ~m);
}
PPC_INLINE void cmp_s(PPCContext* ctx, int field, int32_t a, int32_t b) {
    cr_set(ctx, field, (a < b ? CR_LT : a > b ? CR_GT : CR_EQ) | ctx->xer_so);
}
PPC_INLINE void cmp_u(PPCContext* ctx, int field, uint32_t a, uint32_t b) {
    cr_set(ctx, field, (a < b ? CR_LT : a > b ? CR_GT : CR_EQ) | ctx->xer_so);
}
PPC_INLINE void rc0(PPCContext* ctx, uint32_t v) { cmp_s(ctx, 0, (int32_t)v, 0); }
PPC_INLINE void fcmp(PPCContext* ctx, int field, double a, double b) {
    uint32_t c;
    if (a != a || b != b) c = CR_SO;
    else if (a < b) c = CR_LT;
    else if (a > b) c = CR_GT;
    else c = CR_EQ;
    cr_set(ctx, field, c);
}

/* ---- enteros ----------------------------------------------------------- */

PPC_INLINE uint32_t rotl32(uint32_t v, uint32_t n) { n &= 31; return n ? (v << n) | (v >> (32 - n)) : v; }
PPC_INLINE uint32_t cntlzw(uint32_t v) {
#ifdef _MSC_VER
    unsigned long i; return _BitScanReverse(&i, v) ? 31 - i : 32;
#else
    return v ? (uint32_t)__builtin_clz(v) : 32;
#endif
}
PPC_INLINE uint32_t divw(int32_t a, int32_t b) {
    if (b == 0 || (a == INT32_MIN && b == -1)) return a < 0 ? 0xFFFFFFFFu : 0;
    return (uint32_t)(a / b);
}
PPC_INLINE uint32_t divwu(uint32_t a, uint32_t b) { return b ? a / b : 0; }
PPC_INLINE uint32_t sraw(PPCContext* ctx, uint32_t s, uint32_t n) {
    int32_t v = (int32_t)s;
    if (n & 0x20) { ctx->xer_ca = v < 0; return v < 0 ? 0xFFFFFFFFu : 0; }
    n &= 31;
    ctx->xer_ca = (v < 0) && n && (s & ((1u << n) - 1));
    return (uint32_t)(v >> n);
}
PPC_INLINE uint32_t slw(uint32_t s, uint32_t n) { return (n & 0x20) ? 0 : s << (n & 31); }
PPC_INLINE uint32_t srw(uint32_t s, uint32_t n) { return (n & 0x20) ? 0 : s >> (n & 31); }

/* ---- punto flotante ---------------------------------------------------- */

PPC_INLINE uint32_t fctiw_trunc(double d) {
    if (d != d) return 0x80000000u;
    if (d >= 2147483647.0) return 0x7FFFFFFFu;
    if (d <= -2147483648.0) return 0x80000000u;
    return (uint32_t)(int32_t)d;
}
PPC_INLINE uint32_t fctiw_round(double d) {
    if (d != d) return 0x80000000u;
    if (d >= 2147483647.0) return 0x7FFFFFFFu;
    if (d <= -2147483648.0) return 0x80000000u;
    return (uint32_t)(int32_t)nearbyint(d);
}
/* fctiw con el modo de redondeo del FPSCR (bits RN): 0 al mas cercano, 1 hacia cero,
 * 2 hacia +infinito (techo), 3 hacia -infinito (piso). El juego tiene funciones de piso y
 * techo que cambian el modo con mtfsfi antes del fctiw y lo restauran despues. */
extern int rt_redondeo_viejo;   /* pruebas: RL_REDONDEO_VIEJO=1 ignora el modo (como antes) */
PPC_INLINE uint32_t fctiw_rn(double d, uint32_t fpscr) {
    if (rt_redondeo_viejo) return fctiw_round(d);
    switch (fpscr & 3) {
    case 1: return fctiw_trunc(d);
    case 2: return fctiw_round(ceil(d));
    case 3: return fctiw_round(floor(d));
    default: return fctiw_round(d);
    }
}
/* mtfsfi / mtfsb0 / mtfsb1 / mtfsf con mascara (numeracion de bits de PowerPC: 0 = el mas alto) */
PPC_INLINE uint32_t fpscr_campo(uint32_t fpscr, uint32_t campo, uint32_t imm) {
    uint32_t sh = 4 * (7 - (campo & 7));
    return (fpscr & ~(0xFu << sh)) | ((imm & 0xF) << sh);
}
PPC_INLINE uint32_t fpscr_mascara(uint32_t fpscr, uint32_t fm, uint32_t v) {
    uint32_t m = 0; int k;
    for (k = 0; k < 8; k++) if (fm & (0x80u >> k)) m |= 0xF0000000u >> (4 * k);
    return (fpscr & ~m) | (v & m);
}
/* Resultado de fctiw*: el entero queda en los 32 bits bajos del registro. */
PPC_INLINE double fct_box(uint32_t v) { return u64_d(0xFFF8000000000000ull | v); }
PPC_INLINE uint32_t fpr_lo(double d) { return (uint32_t)d_u64(d); }

/* ---- paired singles cuantizados (psq_l / psq_st) ----------------------- */

void psq_load(PPCContext* ctx, int rd, uint32_t ea, int w, int i);
void psq_store(PPCContext* ctx, int rs, uint32_t ea, int w, int i);
/* Camino rapido en linea cuando el GQR dice "float" (sin cuantizar), que es lo comun. */
/* escalas de cuantizacion: 2^n y 2^-n para los 64 valores de 6 bits con signo (memory.c) */
extern double ppc_qscale[64], ppc_dqscale[64];
PPC_INLINE double psq_lq(uint32_t type, double dq, uint32_t ea) {
    switch (type) {
    case 4: return (double)R8(ea) * dq;
    case 5: return (double)R16(ea) * dq;
    case 6: return (double)(int8_t)R8(ea) * dq;
    default: return (double)(int16_t)R16(ea) * dq;
    }
}
PPC_INLINE void psq_sq(uint32_t type, double q, double v, uint32_t ea) {
    double x = v * q;
    switch (type) {
    case 4: W8(ea, (uint8_t)(x < 0 ? 0 : x > 255 ? 255 : x)); break;
    case 5: W16(ea, (uint16_t)(x < 0 ? 0 : x > 65535 ? 65535 : x)); break;
    case 6: W8(ea, (uint8_t)(int8_t)(x < -128 ? -128 : x > 127 ? 127 : x)); break;
    default: W16(ea, (uint16_t)(int16_t)(x < -32768 ? -32768 : x > 32767 ? 32767 : x)); break;
    }
}
/* Camino rapido en linea: "float" (sin cuantizar) y los enteros de 8/16 bits (tipos 4-7,
   comunes en el sonido y los vertices). Los tipos 1-3 (reservados) van a la funcion. */
PPC_INLINE void PSQ_L(PPCContext* ctx, int rd, uint32_t ea, int w, int i) {
    uint32_t gqr = ctx->gqr[i], type = (gqr >> 16) & 7;
    if (type == 0) {
        ctx->f[rd] = (double)u32_f(R32(ea));
        ctx->ps1[rd] = w ? 1.0 : (double)u32_f(R32(ea + 4));
        return;
    }
    if (type >= 4) {
        double dq = ppc_dqscale[(gqr >> 24) & 0x3F];
        uint32_t sz = (type & 1) ? 2 : 1;
        ctx->f[rd] = psq_lq(type, dq, ea);
        ctx->ps1[rd] = w ? 1.0 : psq_lq(type, dq, ea + sz);
        return;
    }
    psq_load(ctx, rd, ea, w, i);
}
PPC_INLINE void PSQ_ST(PPCContext* ctx, int rs, uint32_t ea, int w, int i) {
    uint32_t gqr = ctx->gqr[i], type = gqr & 7;
    if (type == 0) {
        W32(ea, f_u32((float)ctx->f[rs]));
        if (!w) W32(ea + 4, f_u32((float)ctx->ps1[rs]));
        return;
    }
    if (type >= 4) {
        double q = ppc_qscale[(gqr >> 8) & 0x3F];
        uint32_t sz = (type & 1) ? 2 : 1;
        psq_sq(type, q, ctx->f[rs], ea);
        if (!w) psq_sq(type, q, ctx->ps1[rs], ea + sz);
        return;
    }
    psq_store(ctx, rs, ea, w, i);
}

/* ---- llamadas indirectas y otros servicios del runtime ----------------- */

void call_indirect(PPCContext* ctx, uint32_t addr);
void interp_call(PPCContext* ctx, uint32_t pc);
void ppc_dma(PPCContext* ctx);                   /* DMA de la cache bloqueada (memory.c) */   /* interp.c */
void ppc_unimplemented(PPCContext* ctx, uint32_t pc, uint32_t insn);
void ppc_syscall(PPCContext* ctx, uint32_t pc);
uint64_t ppc_timebase(void);

/* Llamada a una funcion recompilada desde codigo del host (HLE). */
PPCFunc ppc_lookup(uint32_t addr);

#endif
