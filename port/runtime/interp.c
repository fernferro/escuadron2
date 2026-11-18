/*
 * Interprete de PowerPC para el codigo que el juego carga en tiempo de ejecucion.
 *
 * Casi todo el juego esta recompilado a C de antemano (port/recomp), pero algunas
 * rutinas viven en los datos del disco y se copian a la RAM recien al cargar un nivel
 * (por ejemplo, los "programas" de sonido de MusyX). Esas direcciones no tienen funcion
 * recompilada, asi que call_indirect las manda aca y se ejecutan instruccion por
 * instruccion, con la misma semantica que genera el recompilador.
 *
 * Modelo de llamadas (igual que el codigo recompilado):
 *   bl / blrl / bctrl  -> llamada: si el destino esta recompilado se llama a esa funcion
 *                         de C; si no, se interpreta de forma recursiva.
 *   blr                -> volver de interp_call.
 *   b / bctr           -> salto dentro del codigo interpretado, o llamada de cola si el
 *                         destino es una funcion recompilada.
 */
#include <math.h>
#include <stdio.h>
#include "runtime.h"

int interp_trace;              /* --trace-interp: lista cada punto de entrada nuevo */
uint64_t interp_insns;         /* instrucciones interpretadas (estadistica) */

#define MAX_SEEN 4096
static uint32_t s_seen[MAX_SEEN];
static int s_nseen;

static void note_entry(uint32_t pc) {
    int i;
    for (i = 0; i < s_nseen; i++) if (s_seen[i] == pc) return;
    if (s_nseen < MAX_SEEN) s_seen[s_nseen++] = pc;
    if (interp_trace || s_nseen <= 16)
        rt_log("[interp] codigo cargado en tiempo de ejecucion: 0x%08X (%d entradas)\n", pc, s_nseen);
}

static int32_t sext16(uint32_t v) { return (int32_t)(int16_t)(v & 0xFFFF); }

static uint32_t mask32(uint32_t mb, uint32_t me) {
    uint32_t a = 0xFFFFFFFFu >> mb, b = 0xFFFFFFFFu << (31 - me);
    return mb <= me ? (a & b) : (a | b);
}

/* Condicion de bc/bclr/bcctr (decrementa CTR si corresponde). */
static int cond_ok(PPCContext* ctx, uint32_t bo, uint32_t bi) {
    int ok = 1;
    if (!(bo & 0x04)) {
        ctx->ctr--;
        ok = (bo & 0x02) ? (ctx->ctr == 0) : (ctx->ctr != 0);
    }
    if (!(bo & 0x10)) {
        uint32_t b = cr_bit(ctx, (int)bi);
        ok = ok && ((bo & 0x08) ? b : !b);
    }
    return ok;
}

void interp_call(PPCContext* ctx, uint32_t pc);

uint32_t interp_forzar_a, interp_forzar_b;   /* pruebas: interpretar tambien estas funciones recompiladas */
static PPCFunc lookup_f(uint32_t t) { return (t >= interp_forzar_a && t < interp_forzar_b) ? NULL : ppc_lookup(t); }
static void call_addr(PPCContext* ctx, uint32_t t) {
    PPCFunc fn = lookup_f(t);
    if (fn) fn(ctx);
    else interp_call(ctx, t);
}

#define RA0 (ra ? ctx->r[ra] : 0u)

static int interp_op31(PPCContext* ctx, uint32_t pc, uint32_t w) {
    uint32_t rd = (w >> 21) & 31, ra = (w >> 16) & 31, rb = (w >> 11) & 31, Rc = w & 1;
    uint32_t xo = (w >> 1) & 0x3FF, xo9 = xo & 0x1FF;
    uint32_t* R = ctx->r;
    uint32_t A = R[ra], B = R[rb], S = R[rd];
    uint64_t t;
    uint32_t spr;

    /* aritmetica (formas XO; OE se ignora) */
    switch (xo9) {
    case 266: R[rd] = A + B; goto recd;
    case 40:  R[rd] = B - A; goto recd;
    case 104: R[rd] = 0u - A; goto recd;
    case 235: R[rd] = A * B; goto recd;
    case 75:  R[rd] = (uint32_t)(((int64_t)(int32_t)A * (int64_t)(int32_t)B) >> 32); goto recd;
    case 11:  R[rd] = (uint32_t)(((uint64_t)A * (uint64_t)B) >> 32); goto recd;
    case 491: R[rd] = divw((int32_t)A, (int32_t)B); goto recd;
    case 459: R[rd] = divwu(A, B); goto recd;
    case 10:  t = (uint64_t)A + B; goto carry;
    case 8:   t = (uint64_t)(uint32_t)~A + B + 1; goto carry;
    case 138: t = (uint64_t)A + B + ctx->xer_ca; goto carry;
    case 136: t = (uint64_t)(uint32_t)~A + B + ctx->xer_ca; goto carry;
    case 202: t = (uint64_t)A + ctx->xer_ca; goto carry;
    case 200: t = (uint64_t)(uint32_t)~A + ctx->xer_ca; goto carry;
    case 234: t = (uint64_t)A + ctx->xer_ca + 0xFFFFFFFFull; goto carry;
    case 232: t = (uint64_t)(uint32_t)~A + ctx->xer_ca + 0xFFFFFFFFull; goto carry;
    default: break;
    }
    switch (xo) {
    /* logicas: resultado en rA */
    case 28:  R[ra] = S & B; goto reca;
    case 444: R[ra] = S | B; goto reca;
    case 316: R[ra] = S ^ B; goto reca;
    case 124: R[ra] = ~(S | B); goto reca;
    case 476: R[ra] = ~(S & B); goto reca;
    case 284: R[ra] = ~(S ^ B); goto reca;
    case 60:  R[ra] = S & ~B; goto reca;
    case 412: R[ra] = S | ~B; goto reca;
    case 24:  R[ra] = slw(S, B); goto reca;
    case 536: R[ra] = srw(S, B); goto reca;
    case 26:  R[ra] = cntlzw(S); goto reca;
    case 954: R[ra] = (uint32_t)(int32_t)(int8_t)S; goto reca;
    case 922: R[ra] = (uint32_t)(int32_t)(int16_t)S; goto reca;
    case 792: R[ra] = sraw(ctx, S, B & 0x3F); goto reca;
    case 824: R[ra] = sraw(ctx, S, rb); goto reca;
    case 0:   cmp_s(ctx, (int)(rd >> 2), (int32_t)A, (int32_t)B); return 1;
    case 32:  cmp_u(ctx, (int)(rd >> 2), A, B); return 1;
    /* cargas y guardados indexados */
    case 23:  R[rd] = R32(RA0 + B); return 1;
    case 55:  { uint32_t ea = A + B; R[rd] = R32(ea); R[ra] = ea; return 1; }
    case 87:  R[rd] = R8(RA0 + B); return 1;
    case 119: { uint32_t ea = A + B; R[rd] = R8(ea); R[ra] = ea; return 1; }
    case 279: R[rd] = R16(RA0 + B); return 1;
    case 311: { uint32_t ea = A + B; R[rd] = R16(ea); R[ra] = ea; return 1; }
    case 343: R[rd] = (uint32_t)(int32_t)(int16_t)R16(RA0 + B); return 1;
    case 375: { uint32_t ea = A + B; R[rd] = (uint32_t)(int32_t)(int16_t)R16(ea); R[ra] = ea; return 1; }
    case 151: W32(RA0 + B, S); return 1;
    case 183: { uint32_t ea = A + B; W32(ea, S); R[ra] = ea; return 1; }
    case 215: W8(RA0 + B, (uint8_t)S); return 1;
    case 247: { uint32_t ea = A + B; W8(ea, (uint8_t)S); R[ra] = ea; return 1; }
    case 407: W16(RA0 + B, (uint16_t)S); return 1;
    case 439: { uint32_t ea = A + B; W16(ea, (uint16_t)S); R[ra] = ea; return 1; }
    case 534: R[rd] = bswap32(R32(RA0 + B)); return 1;
    case 662: W32(RA0 + B, bswap32(S)); return 1;
    case 790: R[rd] = bswap16(R16(RA0 + B)); return 1;
    case 918: W16(RA0 + B, bswap16((uint16_t)S)); return 1;
    case 535: ctx->f[rd] = ctx->ps1[rd] = (double)u32_f(R32(RA0 + B)); return 1;
    case 567: { uint32_t ea = A + B; ctx->f[rd] = ctx->ps1[rd] = (double)u32_f(R32(ea)); R[ra] = ea; return 1; }
    case 599: ctx->f[rd] = u64_d(R64(RA0 + B)); return 1;
    case 631: { uint32_t ea = A + B; ctx->f[rd] = u64_d(R64(ea)); R[ra] = ea; return 1; }
    case 663: W32(RA0 + B, f_u32((float)ctx->f[rd])); return 1;
    case 695: { uint32_t ea = A + B; W32(ea, f_u32((float)ctx->f[rd])); R[ra] = ea; return 1; }
    case 727: W64(RA0 + B, d_u64(ctx->f[rd])); return 1;
    case 759: { uint32_t ea = A + B; W64(ea, d_u64(ctx->f[rd])); R[ra] = ea; return 1; }
    case 983: W32(RA0 + B, fpr_lo(ctx->f[rd])); return 1;
    /* registros especiales */
    case 339:
        spr = ((w >> 16) & 31) | (((w >> 11) & 31) << 5);
        if (spr == 1) R[rd] = (ctx->xer_so << 31) | (ctx->xer_ca << 29);
        else if (spr == 8) R[rd] = ctx->lr;
        else if (spr == 9) R[rd] = ctx->ctr;
        else if (spr >= 912 && spr <= 919) R[rd] = ctx->gqr[spr - 912];
        else R[rd] = ctx->spr[spr];
        return 1;
    case 467:
        spr = ((w >> 16) & 31) | (((w >> 11) & 31) << 5);
        if (spr == 1) { ctx->xer_so = (S >> 31) & 1; ctx->xer_ca = (S >> 29) & 1; }
        else if (spr == 8) ctx->lr = S;
        else if (spr == 9) ctx->ctr = S;
        else if (spr >= 912 && spr <= 919) ctx->gqr[spr - 912] = S;
        else { ctx->spr[spr] = S; if (spr == 923) ppc_dma(ctx); }
        return 1;
    case 371:
        spr = ((w >> 16) & 31) | (((w >> 11) & 31) << 5);
        R[rd] = spr == 268 ? (uint32_t)ppc_timebase() : (uint32_t)(ppc_timebase() >> 32);
        return 1;
    case 19:  R[rd] = ctx->cr; return 1;
    case 144: {
        uint32_t crm = (w >> 12) & 0xFF, m = 0;
        int i;
        for (i = 0; i < 8; i++) if (crm & (0x80u >> i)) m |= 0xFu << (28 - 4 * i);
        ctx->cr = (ctx->cr & ~m) | (S & m);
        return 1;
    }
    case 83:  R[rd] = ctx->msr; return 1;
    case 146: ctx->msr = S; return 1;
    case 512: cr_set(ctx, (int)(rd >> 2), (ctx->xer_so << 3) | (ctx->xer_ca << 1)); ctx->xer_so = ctx->xer_ca = 0; return 1;
    case 595: case 659: R[rd] = 0; return 1;
    case 210: case 242: case 306: case 54: case 86: case 470: case 982:
    case 598: case 854: case 278: case 246:
        return 1;
    case 1014: {
        uint32_t ea = (RA0 + B) & ~31u;
        int i;
        for (i = 0; i < 32; i += 4) W32(ea + (uint32_t)i, 0);
        return 1;
    }
    default:
        ppc_unimplemented(ctx, pc, w);
        return 1;
    }
carry:
    ctx->xer_ca = (uint32_t)(t >> 32);
    R[rd] = (uint32_t)t;
recd:
    if (Rc) rc0(ctx, R[rd]);
    return 1;
reca:
    if (Rc) rc0(ctx, R[ra]);
    return 1;
}

static void interp_ps(PPCContext* ctx, uint32_t pc, uint32_t w) {
    uint32_t rd = (w >> 21) & 31, ra = (w >> 16) & 31, rb = (w >> 11) & 31, rc = (w >> 6) & 31;
    uint32_t xo5 = (w >> 1) & 31, xo6 = (w >> 1) & 0x3F, xo10 = (w >> 1) & 0x3FF;
    double A0 = ctx->f[ra], B0 = ctx->f[rb], C0 = ctx->f[rc];
    double A1 = ctx->ps1[ra], B1 = ctx->ps1[rb], C1 = ctx->ps1[rc];
    double t0, t1;

    if (xo6 == 6 || xo6 == 7 || xo6 == 38 || xo6 == 39) {
        int W = (int)((w >> 10) & 1), I = (int)((w >> 7) & 7);
        int upd = xo6 == 38 || xo6 == 39;
        uint32_t ea = (upd ? ctx->r[ra] : RA0) + ctx->r[rb];
        if (xo6 == 6 || xo6 == 38) psq_load(ctx, (int)rd, ea, W, I);
        else psq_store(ctx, (int)rd, ea, W, I);
        if (upd) ctx->r[ra] = ea;
        return;
    }
    if (xo10 == 1014) {
        uint32_t ea = (RA0 + ctx->r[rb]) & ~31u;
        int i;
        for (i = 0; i < 32; i += 4) W32(ea + (uint32_t)i, 0);
        return;
    }
    if (xo5 >= 10) {
        int ok = 1;
        switch (xo5) {
        case 21: t0 = A0 + B0; t1 = A1 + B1; break;
        case 20: t0 = A0 - B0; t1 = A1 - B1; break;
        case 25: t0 = A0 * C0; t1 = A1 * C1; break;
        case 18: t0 = A0 / B0; t1 = A1 / B1; break;
        case 29: t0 = A0 * C0 + B0; t1 = A1 * C1 + B1; break;
        case 28: t0 = A0 * C0 - B0; t1 = A1 * C1 - B1; break;
        case 30: t0 = -(A0 * C0 - B0); t1 = -(A1 * C1 - B1); break;
        case 31: t0 = -(A0 * C0 + B0); t1 = -(A1 * C1 + B1); break;
        case 12: t0 = A0 * C0; t1 = A1 * C0; break;
        case 13: t0 = A0 * C1; t1 = A1 * C1; break;
        case 14: t0 = A0 * C0 + B0; t1 = A1 * C0 + B1; break;
        case 15: t0 = A0 * C1 + B0; t1 = A1 * C1 + B1; break;
        case 10: t0 = A0 + B1; t1 = C1; break;
        case 11: t0 = C0; t1 = A0 + B1; break;
        case 23: t0 = (A0 >= 0.0) ? C0 : B0; t1 = (A1 >= 0.0) ? C1 : B1; break;
        case 24: t0 = 1.0 / B0; t1 = 1.0 / B1; break;
        case 26: t0 = 1.0 / sqrt(B0); t1 = 1.0 / sqrt(B1); break;
        default: ok = 0; t0 = t1 = 0; break;
        }
        if (ok) { ctx->f[rd] = RS(t0); ctx->ps1[rd] = RS(t1); return; }
    }
    switch (xo10) {
    case 40:  ctx->f[rd] = -B0; ctx->ps1[rd] = -B1; return;
    case 72:  ctx->f[rd] = B0; ctx->ps1[rd] = B1; return;
    case 136: ctx->f[rd] = -fabs(B0); ctx->ps1[rd] = -fabs(B1); return;
    case 264: ctx->f[rd] = fabs(B0); ctx->ps1[rd] = fabs(B1); return;
    case 528: ctx->f[rd] = RS(A0); ctx->ps1[rd] = RS(B0); return;
    case 560: ctx->f[rd] = RS(A0); ctx->ps1[rd] = RS(B1); return;
    case 592: ctx->f[rd] = RS(A1); ctx->ps1[rd] = RS(B0); return;
    case 624: ctx->f[rd] = RS(A1); ctx->ps1[rd] = RS(B1); return;
    case 0: case 32: fcmp(ctx, (int)(rd >> 2), A0, B0); return;
    case 64: case 96: fcmp(ctx, (int)(rd >> 2), A1, B1); return;
    default: ppc_unimplemented(ctx, pc, w); return;
    }
}

void interp_call(PPCContext* ctx, uint32_t pc) {
    note_entry(pc);
    for (;;) {
        uint32_t w = R32(pc);
        uint32_t op = w >> 26;
        uint32_t rd = (w >> 21) & 31, ra = (w >> 16) & 31, rb = (w >> 11) & 31, rc = (w >> 6) & 31;
        int32_t simm = sext16(w);
        uint32_t uimm = w & 0xFFFF;
        uint32_t* R = ctx->r;
        uint32_t nxt = pc + 4;
        interp_insns++;

        switch (op) {
        /* ---- saltos ---- */
        case 18: {
            int32_t li = ((int32_t)(w << 6)) >> 6;
            uint32_t tgt = ((w & 2) ? (uint32_t)(li & ~3) : pc + (uint32_t)(li & ~3));
            if (w & 1) { ctx->lr = nxt; call_addr(ctx, tgt); pc = nxt; continue; }
            { PPCFunc fn = lookup_f(tgt); if (fn) { fn(ctx); return; } }
            if (tgt <= pc) RT_LOOP_POLL(ctx);
            pc = tgt; continue;
        }
        case 16: {
            int32_t bd = sext16(w & 0xFFFC);
            uint32_t tgt = (w & 2) ? (uint32_t)bd : pc + (uint32_t)bd;
            if (!cond_ok(ctx, rd, ra)) { pc = nxt; continue; }
            if (w & 1) { ctx->lr = nxt; call_addr(ctx, tgt); pc = nxt; continue; }
            { PPCFunc fn = lookup_f(tgt); if (fn) { fn(ctx); return; } }
            if (tgt <= pc) RT_LOOP_POLL(ctx);
            pc = tgt; continue;
        }
        case 19: {
            uint32_t xo = (w >> 1) & 0x3FF;
            if (xo == 16) {
                if (!cond_ok(ctx, rd, ra)) break;
                if (w & 1) { uint32_t t = ctx->lr; ctx->lr = nxt; call_addr(ctx, t); break; }
                return;
            }
            if (xo == 528) {
                if (!cond_ok(ctx, rd, ra)) break;
                if (w & 1) { ctx->lr = nxt; call_addr(ctx, ctx->ctr); break; }
                { PPCFunc fn = ppc_lookup(ctx->ctr); if (fn) { fn(ctx); return; } }
                pc = ctx->ctr; continue;
            }
            {
                uint32_t a = cr_bit(ctx, (int)ra), b = cr_bit(ctx, (int)rb), v;
                switch (xo) {
                case 257: v = a & b; break;
                case 129: v = a & !b; break;
                case 289: v = !(a ^ b); break;
                case 225: v = !(a & b); break;
                case 33:  v = !(a | b); break;
                case 449: v = a | b; break;
                case 417: v = a | !b; break;
                case 193: v = a ^ b; break;
                case 0:   cr_set(ctx, (int)(rd >> 2), (ctx->cr >> (28 - 4 * (ra >> 2))) & 0xF); goto next;
                case 150: goto next;
                default:  ppc_unimplemented(ctx, pc, w); goto next;
                }
                cr_setbit(ctx, (int)rd, v);
            }
            break;
        }
        case 17: ppc_syscall(ctx, pc); break;

        /* ---- enteros con inmediato ---- */
        case 14: R[rd] = (ra ? R[ra] : 0u) + (uint32_t)simm; break;
        case 15: R[rd] = (ra ? R[ra] : 0u) + (uimm << 16); break;
        case 12: case 13: {
            uint64_t t = (uint64_t)R[ra] + (uint32_t)simm;
            ctx->xer_ca = (uint32_t)(t >> 32); R[rd] = (uint32_t)t;
            if (op == 13) rc0(ctx, R[rd]);
            break;
        }
        case 8: {
            uint64_t t = (uint64_t)(uint32_t)~R[ra] + (uint32_t)simm + 1;
            ctx->xer_ca = (uint32_t)(t >> 32); R[rd] = (uint32_t)t;
            break;
        }
        case 7:  R[rd] = R[ra] * (uint32_t)simm; break;
        case 11: cmp_s(ctx, (int)(rd >> 2), (int32_t)R[ra], simm); break;
        case 10: cmp_u(ctx, (int)(rd >> 2), R[ra], uimm); break;
        case 24: R[ra] = R[rd] | uimm; break;
        case 25: R[ra] = R[rd] | (uimm << 16); break;
        case 26: R[ra] = R[rd] ^ uimm; break;
        case 27: R[ra] = R[rd] ^ (uimm << 16); break;
        case 28: R[ra] = R[rd] & uimm; rc0(ctx, R[ra]); break;
        case 29: R[ra] = R[rd] & (uimm << 16); rc0(ctx, R[ra]); break;
        case 20: case 21: case 23: {
            uint32_t mb = (w >> 6) & 31, me = (w >> 1) & 31, m = mask32(mb, me);
            if (op == 21) R[ra] = rotl32(R[rd], rb) & m;
            else if (op == 23) R[ra] = rotl32(R[rd], R[rb] & 31) & m;
            else R[ra] = (rotl32(R[rd], rb) & m) | (R[ra] & ~m);
            if (w & 1) rc0(ctx, R[ra]);
            break;
        }

        /* ---- cargas y guardados ---- */
        case 32: R[rd] = R32((ra ? R[ra] : 0u) + (uint32_t)simm); break;
        case 33: { uint32_t ea = R[ra] + (uint32_t)simm; R[rd] = R32(ea); R[ra] = ea; break; }
        case 34: R[rd] = R8((ra ? R[ra] : 0u) + (uint32_t)simm); break;
        case 35: { uint32_t ea = R[ra] + (uint32_t)simm; R[rd] = R8(ea); R[ra] = ea; break; }
        case 40: R[rd] = R16((ra ? R[ra] : 0u) + (uint32_t)simm); break;
        case 41: { uint32_t ea = R[ra] + (uint32_t)simm; R[rd] = R16(ea); R[ra] = ea; break; }
        case 42: R[rd] = (uint32_t)(int32_t)(int16_t)R16((ra ? R[ra] : 0u) + (uint32_t)simm); break;
        case 43: { uint32_t ea = R[ra] + (uint32_t)simm; R[rd] = (uint32_t)(int32_t)(int16_t)R16(ea); R[ra] = ea; break; }
        case 36: W32((ra ? R[ra] : 0u) + (uint32_t)simm, R[rd]); break;
        case 37: { uint32_t ea = R[ra] + (uint32_t)simm; W32(ea, R[rd]); R[ra] = ea; break; }
        case 38: W8((ra ? R[ra] : 0u) + (uint32_t)simm, (uint8_t)R[rd]); break;
        case 39: { uint32_t ea = R[ra] + (uint32_t)simm; W8(ea, (uint8_t)R[rd]); R[ra] = ea; break; }
        case 44: W16((ra ? R[ra] : 0u) + (uint32_t)simm, (uint16_t)R[rd]); break;
        case 45: { uint32_t ea = R[ra] + (uint32_t)simm; W16(ea, (uint16_t)R[rd]); R[ra] = ea; break; }
        case 46: { uint32_t ea = (ra ? R[ra] : 0u) + (uint32_t)simm, i; for (i = rd; i < 32; i++, ea += 4) R[i] = R32(ea); break; }
        case 47: { uint32_t ea = (ra ? R[ra] : 0u) + (uint32_t)simm, i; for (i = rd; i < 32; i++, ea += 4) W32(ea, R[i]); break; }
        case 48: case 49: {
            uint32_t ea = (op == 48 ? (ra ? R[ra] : 0u) : R[ra]) + (uint32_t)simm;
            ctx->f[rd] = ctx->ps1[rd] = (double)u32_f(R32(ea));
            if (op == 49) R[ra] = ea;
            break;
        }
        case 50: case 51: {
            uint32_t ea = (op == 50 ? (ra ? R[ra] : 0u) : R[ra]) + (uint32_t)simm;
            ctx->f[rd] = u64_d(R64(ea));
            if (op == 51) R[ra] = ea;
            break;
        }
        case 52: case 53: {
            uint32_t ea = (op == 52 ? (ra ? R[ra] : 0u) : R[ra]) + (uint32_t)simm;
            W32(ea, f_u32((float)ctx->f[rd]));
            if (op == 53) R[ra] = ea;
            break;
        }
        case 54: case 55: {
            uint32_t ea = (op == 54 ? (ra ? R[ra] : 0u) : R[ra]) + (uint32_t)simm;
            W64(ea, d_u64(ctx->f[rd]));
            if (op == 55) R[ra] = ea;
            break;
        }
        case 56: case 57: case 60: case 61: {
            int Wb = (int)((w >> 15) & 1), I = (int)((w >> 12) & 7);
            int32_t d = ((int32_t)(w << 20)) >> 20;
            int upd = op == 57 || op == 61;
            uint32_t ea = (upd ? R[ra] : (ra ? R[ra] : 0u)) + (uint32_t)d;
            if (op == 56 || op == 57) psq_load(ctx, (int)rd, ea, Wb, I);
            else psq_store(ctx, (int)rd, ea, Wb, I);
            if (upd) R[ra] = ea;
            break;
        }

        case 31: interp_op31(ctx, pc, w); break;

        /* ---- punto flotante ---- */
        case 59: {
            uint32_t xo = (w >> 1) & 31;
            double A = ctx->f[ra], B = ctx->f[rb], C = ctx->f[rc], v;
            switch (xo) {
            case 18: v = A / B; break;
            case 20: v = A - B; break;
            case 21: v = A + B; break;
            case 25: v = A * C; break;
            case 28: v = A * C - B; break;
            case 29: v = A * C + B; break;
            case 30: v = -(A * C - B); break;
            case 31: v = -(A * C + B); break;
            case 24: v = 1.0 / B; break;
            case 22: v = sqrt(B); break;
            default: ppc_unimplemented(ctx, pc, w); goto next;
            }
            ctx->f[rd] = ctx->ps1[rd] = RS(v);
            break;
        }
        case 63: {
            uint32_t xo5 = (w >> 1) & 31, xo = (w >> 1) & 0x3FF;
            double A = ctx->f[ra], B = ctx->f[rb], C = ctx->f[rc];
            if (xo5 >= 16) {
                int ok = 1;
                double v = 0;
                switch (xo5) {
                case 18: v = A / B; break;
                case 20: v = A - B; break;
                case 21: v = A + B; break;
                case 25: v = A * C; break;
                case 28: v = A * C - B; break;
                case 29: v = A * C + B; break;
                case 30: v = -(A * C - B); break;
                case 31: v = -(A * C + B); break;
                case 22: v = sqrt(B); break;
                case 26: v = 1.0 / sqrt(B); break;
                case 23: v = (A >= 0.0) ? C : B; break;
                default: ok = 0; break;
                }
                if (ok) { ctx->f[rd] = v; break; }
            }
            switch (xo) {
            case 0: case 32: fcmp(ctx, (int)(rd >> 2), A, B); break;
            case 12:  ctx->f[rd] = ctx->ps1[rd] = RS(B); break;
            case 14:  ctx->f[rd] = fct_box(fctiw_rn(B, ctx->fpscr)); break;
            case 15:  ctx->f[rd] = fct_box(fctiw_trunc(B)); break;
            case 40:  ctx->f[rd] = -B; break;
            case 72:  ctx->f[rd] = B; break;
            case 136: ctx->f[rd] = -fabs(B); break;
            case 264: ctx->f[rd] = fabs(B); break;
            case 583: ctx->f[rd] = u64_d((uint64_t)ctx->fpscr); break;
            case 711: ctx->fpscr = fpscr_mascara(ctx->fpscr, (w >> 17) & 0xFF, fpr_lo(B)); break;
            case 134: ctx->fpscr = fpscr_campo(ctx->fpscr, rd >> 2, (w >> 12) & 0xF); break;
            case 38:  ctx->fpscr |= 1u << (31 - rd); break;
            case 70:  ctx->fpscr &= ~(1u << (31 - rd)); break;
            case 64: break;
            default: ppc_unimplemented(ctx, pc, w); break;
            }
            break;
        }
        case 4: interp_ps(ctx, pc, w); break;
        default: ppc_unimplemented(ctx, pc, w); break;
        }
next:
        pc = nxt;
    }
}
