#!/usr/bin/env python3
"""
Recompilador estatico PowerPC (Gekko) -> C para Escuadron2.

    python port/recomp/recomp.py orig/GSWE64/combined.dol config/GSWE64/symbols.txt build/port/gen [--hle port/hle.txt]

Genera:
  build/port/gen/funcs.h        prototipos de todas las funciones
  build/port/gen/code_NNN.c     el codigo traducido (un archivo cada ~150 funciones)
  build/port/gen/table.c        tabla direccion -> funcion para llamadas indirectas
  build/port/gen/report.txt     instrucciones no soportadas y otros avisos

Cada funcion del juego (segun symbols.txt) se traduce a `void f_XXXXXXXX(PPCContext*)`.
Los saltos internos son `goto`, `bl` es una llamada directa en C, `blr` es `return`,
y los saltos/llamadas por registro pasan por `call_indirect`. Los `switch` del juego
(saltos `bctr` a tablas) se resuelven con las tablas `jumptable_*` de symbols.txt.

Las funciones listadas en port/hle.txt no se traducen: las llamadas van a la version
del runtime (`hle_<nombre>`), que reemplaza al hardware/SDK de la GameCube.
"""
import re
import struct
import sys
from collections import defaultdict
from pathlib import Path

FUNCS_PER_FILE = 150


# --------------------------------------------------------------------------
# Imagen DOL

class Image:
    def __init__(self, path):
        d = Path(path).read_bytes()
        off = struct.unpack('>18I', d[0:72]); addr = struct.unpack('>18I', d[72:144])
        size = struct.unpack('>18I', d[144:216])
        self.secs = [(addr[i], size[i], d[off[i]:off[i] + size[i]], i < 7) for i in range(18) if size[i]]

    def read(self, a, n):
        for s, sz, data, _ in self.secs:
            if s <= a and a + n <= s + sz:
                return data[a - s:a - s + n]
        return None

    def u32(self, a):
        b = self.read(a, 4)
        return struct.unpack('>I', b)[0] if b else None

    def is_code(self, a):
        return any(s <= a < s + sz and t for s, sz, _, t in self.secs)


# --------------------------------------------------------------------------
# Utilidades de codificacion

def sext(v, bits):
    m = 1 << (bits - 1)
    return (v ^ m) - m


def mask32(mb, me):
    if mb <= me:
        return ((0xFFFFFFFF >> mb) & (0xFFFFFFFF << (31 - me))) & 0xFFFFFFFF
    return ((0xFFFFFFFF >> mb) | (0xFFFFFFFF << (31 - me))) & 0xFFFFFFFF


def h(v):
    return f'0x{v & 0xFFFFFFFF:X}u'


def ra0(ra):
    """(rA|0): rA o cero si rA es r0."""
    return '0u' if ra == 0 else f'ctx->r[{ra}]'


def ea_d(ra, d):
    base = ra0(ra)
    if d == 0:
        return base
    return f'({base} + {h(d)})'


def ea_x(ra, rb):
    return f'({ra0(ra)} + ctx->r[{rb}])'


SPR_NAMES = {1: 'XER', 8: 'LR', 9: 'CTR'}


# --------------------------------------------------------------------------
# Traduccion de una instruccion

class Ctx:
    """Contexto de traduccion de una funcion."""

    def __init__(self, rc, fn_start, fn_end):
        self.rc = rc
        self.start = fn_start
        self.end = fn_end
        self.labels = set()       # direcciones internas que son destino de saltos
        self.unsupported = []


def cond_expr(bo, bi):
    """Condicion de un salto condicional. Devuelve (pre, expr): pre decrementa CTR."""
    pre = ''
    parts = []
    if not (bo & 0x04):
        pre = 'ctx->ctr--; '
        parts.append('ctx->ctr == 0' if (bo & 0x02) else 'ctx->ctr != 0')
    if not (bo & 0x10):
        bit = f'cr_bit(ctx, {bi})'
        parts.append(bit if (bo & 0x08) else f'!{bit}')
    expr = ' && '.join(f'({p})' for p in parts) if parts else '1'
    return pre, expr


def translate(c: 'Ctx', pc, w):
    """Devuelve una lista de lineas C para la instruccion `w` en `pc`."""
    rc = c.rc
    op = w >> 26
    rd = (w >> 21) & 31
    ra = (w >> 16) & 31
    rb = (w >> 11) & 31
    rcc = (w >> 6) & 31
    simm = sext(w & 0xFFFF, 16)
    uimm = w & 0xFFFF
    Rc = w & 1
    R = lambda n: f'ctx->r[{n}]'
    F = lambda n: f'ctx->f[{n}]'
    P = lambda n: f'ctx->ps1[{n}]'
    rec = (lambda v: [f'rc0(ctx, {v});']) if Rc else (lambda v: [])
    nxt = pc + 4

    def unsupported(name='?'):
        c.unsupported.append((pc, w, name))
        return [f'ppc_unimplemented(ctx, {h(pc)}, {h(w)}); /* {name} */']

    # ---- saltos ------------------------------------------------------------
    if op == 18:  # b, bl, ba, bla
        li = sext(w & 0x03FFFFFC, 26)
        aa = (w >> 1) & 1
        lk = w & 1
        tgt = (li if aa else pc + li) & 0xFFFFFFFF
        return rc.branch(c, pc, tgt, lk, '1', '')
    if op == 16:  # bc
        bo, bi = rd, ra
        bd = sext(w & 0xFFFC, 16)
        aa = (w >> 1) & 1
        lk = w & 1
        tgt = (bd if aa else pc + bd) & 0xFFFFFFFF
        pre, expr = cond_expr(bo, bi)
        return rc.branch(c, pc, tgt, lk, expr, pre)
    if op == 19:
        xo = (w >> 1) & 0x3FF
        if xo == 16:  # bclr
            bo, bi, lk = rd, ra, w & 1
            pre, expr = cond_expr(bo, bi)
            if lk:
                body = [f'{{ uint32_t t_ = ctx->lr; ctx->lr = {h(nxt)}; call_indirect(ctx, t_); }}']
            else:
                body = ['return;']
            return wrap_cond(pre, expr, body)
        if xo == 528:  # bcctr
            bo, bi, lk = rd, ra, w & 1
            pre, expr = cond_expr(bo, bi)
            if lk:
                body = [f'ctx->lr = {h(nxt)}; call_indirect(ctx, ctx->ctr);']
            else:
                body = rc.bctr(c, pc)
            return wrap_cond(pre, expr, body)
        crbd, crba, crbb = rd, ra, rb
        A = f'cr_bit(ctx, {crba})'
        B = f'cr_bit(ctx, {crbb})'
        crops = {257: f'{A} & {B}', 129: f'{A} & !{B}', 289: f'!({A} ^ {B})', 225: f'!({A} & {B})',
                 33: f'!({A} | {B})', 449: f'{A} | {B}', 417: f'{A} | !{B}', 193: f'{A} ^ {B}'}
        if xo in crops:
            return [f'cr_setbit(ctx, {crbd}, {crops[xo]});']
        if xo == 0:  # mcrf
            return [f'cr_set(ctx, {rd >> 2}, (ctx->cr >> {28 - 4 * (ra >> 2)}) & 0xF);']
        if xo == 150:  # isync
            return []
        if xo == 50:  # rfi
            return unsupported('rfi')
        return unsupported(f'op19/{xo}')
    if op == 17:  # sc
        return [f'ppc_syscall(ctx, {h(pc)});']

    # ---- enteros con inmediato ---------------------------------------------
    if op == 14:  # addi / li
        return [f'{R(rd)} = {h(simm)};'] if ra == 0 else [f'{R(rd)} = {R(ra)} + {h(simm)};']
    if op == 15:  # addis / lis
        v = (uimm << 16) & 0xFFFFFFFF
        return [f'{R(rd)} = {h(v)};'] if ra == 0 else [f'{R(rd)} = {R(ra)} + {h(v)};']
    if op == 12 or op == 13:  # addic(.)
        lines = [f'{{ uint64_t t_ = (uint64_t){R(ra)} + {h(simm)}; ctx->xer_ca = (uint32_t)(t_ >> 32); {R(rd)} = (uint32_t)t_; }}']
        if op == 13:
            lines.append(f'rc0(ctx, {R(rd)});')
        return lines
    if op == 8:  # subfic
        return [f'{{ uint64_t t_ = (uint64_t)(uint32_t)~{R(ra)} + {h(simm)} + 1; ctx->xer_ca = (uint32_t)(t_ >> 32); {R(rd)} = (uint32_t)t_; }}']
    if op == 7:  # mulli
        return [f'{R(rd)} = {R(ra)} * {h(simm)};']
    if op == 11:  # cmpi
        return [f'cmp_s(ctx, {rd >> 2}, (int32_t){R(ra)}, {simm});']
    if op == 10:  # cmpli
        return [f'cmp_u(ctx, {rd >> 2}, {R(ra)}, {h(uimm)});']
    if op == 24:  # ori
        if rd == 0 and ra == 0 and uimm == 0:
            return []  # nop
        return [f'{R(ra)} = {R(rd)} | {h(uimm)};']
    if op == 25:
        return [f'{R(ra)} = {R(rd)} | {h(uimm << 16)};']
    if op == 26:
        return [f'{R(ra)} = {R(rd)} ^ {h(uimm)};']
    if op == 27:
        return [f'{R(ra)} = {R(rd)} ^ {h(uimm << 16)};']
    if op == 28:
        return [f'{R(ra)} = {R(rd)} & {h(uimm)};', f'rc0(ctx, {R(ra)});']
    if op == 29:
        return [f'{R(ra)} = {R(rd)} & {h(uimm << 16)};', f'rc0(ctx, {R(ra)});']
    if op in (20, 21, 23):  # rlwimi, rlwinm, rlwnm
        sh = rb
        mb = (w >> 6) & 31
        me = (w >> 1) & 31
        m = mask32(mb, me)
        if op == 21:
            rot = f'rotl32({R(rd)}, {sh})' if sh else R(rd)
            lines = [f'{R(ra)} = {rot} & {h(m)};']
        elif op == 23:
            lines = [f'{R(ra)} = rotl32({R(rd)}, {R(rb)} & 31) & {h(m)};']
        else:
            rot = f'rotl32({R(rd)}, {sh})' if sh else R(rd)
            lines = [f'{R(ra)} = ({rot} & {h(m)}) | ({R(ra)} & {h(~m)});']
        return lines + rec(R(ra))

    # ---- cargas y guardados --------------------------------------------------
    loads = {32: ('R32', 'u32', 0), 33: ('R32', 'u32', 1), 34: ('R8', 'u8', 0), 35: ('R8', 'u8', 1),
             40: ('R16', 'u16', 0), 41: ('R16', 'u16', 1), 42: ('R16', 's16', 0), 43: ('R16', 's16', 1)}
    if op in loads:
        fn, kind, upd = loads[op]
        ea = ea_d(ra, simm) if not upd else f'({R(ra)} + {h(simm)})'
        val = f'{fn}(ea_)'
        if kind == 's16':
            val = f'(uint32_t)(int32_t)(int16_t){val}'
        lines = [f'{{ uint32_t ea_ = {ea}; {R(rd)} = {val};']
        if upd:
            lines[0] += f' {R(ra)} = ea_;'
        lines[0] += ' }'
        return lines
    stores = {36: ('W32', 0, 'uint32_t'), 37: ('W32', 1, 'uint32_t'), 38: ('W8', 0, 'uint8_t'), 39: ('W8', 1, 'uint8_t'),
              44: ('W16', 0, 'uint16_t'), 45: ('W16', 1, 'uint16_t')}
    if op in stores:
        fn, upd, ty = stores[op]
        ea = ea_d(ra, simm) if not upd else f'({R(ra)} + {h(simm)})'
        line = f'{{ uint32_t ea_ = {ea}; {fn}(ea_, ({ty}){R(rd)});'
        if upd:
            line += f' {R(ra)} = ea_;'
        return [line + ' }']
    if op == 46:  # lmw
        return [f'{{ uint32_t ea_ = {ea_d(ra, simm)}; for (int i_ = {rd}; i_ < 32; i_++, ea_ += 4) ctx->r[i_] = R32(ea_); }}']
    if op == 47:  # stmw
        return [f'{{ uint32_t ea_ = {ea_d(ra, simm)}; for (int i_ = {rd}; i_ < 32; i_++, ea_ += 4) W32(ea_, ctx->r[i_]); }}']
    if op in (48, 49):  # lfs(u)
        ea = ea_d(ra, simm) if op == 48 else f'({R(ra)} + {h(simm)})'
        upd = f' {R(ra)} = ea_;' if op == 49 else ''
        return [f'{{ uint32_t ea_ = {ea}; {F(rd)} = {P(rd)} = (double)u32_f(R32(ea_));{upd} }}']
    if op in (50, 51):  # lfd(u)
        ea = ea_d(ra, simm) if op == 50 else f'({R(ra)} + {h(simm)})'
        upd = f' {R(ra)} = ea_;' if op == 51 else ''
        return [f'{{ uint32_t ea_ = {ea}; {F(rd)} = u64_d(R64(ea_));{upd} }}']
    if op in (52, 53):  # stfs(u)
        ea = ea_d(ra, simm) if op == 52 else f'({R(ra)} + {h(simm)})'
        upd = f' {R(ra)} = ea_;' if op == 53 else ''
        return [f'{{ uint32_t ea_ = {ea}; W32(ea_, f_u32((float){F(rd)}));{upd} }}']
    if op in (54, 55):  # stfd(u)
        ea = ea_d(ra, simm) if op == 54 else f'({R(ra)} + {h(simm)})'
        upd = f' {R(ra)} = ea_;' if op == 55 else ''
        return [f'{{ uint32_t ea_ = {ea}; W64(ea_, d_u64({F(rd)}));{upd} }}']
    if op in (56, 57, 60, 61):  # psq_l(u), psq_st(u)
        W = (w >> 15) & 1
        I = (w >> 12) & 7
        d = sext(w & 0xFFF, 12)
        upd = op in (57, 61)
        ea = f'({R(ra)} + {h(d)})' if upd else ea_d(ra, d)
        fn = 'PSQ_L' if op in (56, 57) else 'PSQ_ST'
        line = f'{{ uint32_t ea_ = {ea}; {fn}(ctx, {rd}, ea_, {W}, {I});'
        if upd:
            line += f' {R(ra)} = ea_;'
        return [line + ' }']

    # ---- extendidas (31) ---------------------------------------------------
    if op == 31:
        return translate31(c, pc, w, rd, ra, rb, Rc, unsupported)

    # ---- punto flotante ----------------------------------------------------
    if op == 59:
        xo = (w >> 1) & 31
        A, B, C, D = F(ra), F(rb), F(rcc), F(rd)
        exprs = {18: f'{A} / {B}', 20: f'{A} - {B}', 21: f'{A} + {B}', 25: f'{A} * {C}',
                 28: f'{A} * {C} - {B}', 29: f'{A} * {C} + {B}', 30: f'-({A} * {C} - {B})',
                 31: f'-({A} * {C} + {B})', 24: f'1.0 / {B}', 22: f'sqrt({B})'}
        if xo in exprs:
            lines = [f'{D} = {P(rd)} = RS({exprs[xo]});']
            if Rc:
                lines.append('/* Rc en FP: CR1 no se usa */')
            return lines
        return unsupported(f'op59/{xo}')
    if op == 63:
        xo5 = (w >> 1) & 31
        xo = (w >> 1) & 0x3FF
        A, B, C, D = F(ra), F(rb), F(rcc), F(rd)
        if xo5 >= 16 and xo5 not in (0,):
            exprs = {18: f'{A} / {B}', 20: f'{A} - {B}', 21: f'{A} + {B}', 25: f'{A} * {C}',
                     28: f'{A} * {C} - {B}', 29: f'{A} * {C} + {B}', 30: f'-({A} * {C} - {B})',
                     31: f'-({A} * {C} + {B})', 22: f'sqrt({B})', 26: f'1.0 / sqrt({B})',
                     23: f'({A} >= 0.0) ? {C} : {B}'}
            if xo5 in exprs:
                return [f'{D} = {exprs[xo5]};']
        if xo == 0:
            return [f'fcmp(ctx, {rd >> 2}, {A}, {B});']
        if xo == 32:
            return [f'fcmp(ctx, {rd >> 2}, {A}, {B});']
        if xo == 12:
            return [f'{D} = {P(rd)} = RS({B});']
        if xo == 14:
            return [f'{D} = fct_box(fctiw_rn({B}, ctx->fpscr));']
        if xo == 15:
            return [f'{D} = fct_box(fctiw_trunc({B}));']
        if xo == 40:
            return [f'{D} = -{B};']
        if xo == 72:
            return [f'{D} = {B};']
        if xo == 136:
            return [f'{D} = -fabs({B});']
        if xo == 264:
            return [f'{D} = fabs({B});']
        if xo == 583:
            return [f'{D} = u64_d((uint64_t)ctx->fpscr);']
        if xo == 711:
            return [f'ctx->fpscr = fpscr_mascara(ctx->fpscr, {(w >> 17) & 0xFF}u, fpr_lo({B}));']
        if xo == 134:   # mtfsfi: solo importa el modo de redondeo (lo usan piso y techo)
            return [f'ctx->fpscr = fpscr_campo(ctx->fpscr, {rd >> 2}u, {(w >> 12) & 0xF}u);']
        if xo == 38:
            return [f'ctx->fpscr |= {1 << (31 - rd)}u;']
        if xo == 70:
            return [f'ctx->fpscr &= ~{1 << (31 - rd)}u;']
        if xo == 64:
            return ['/* mcrfs: los bits de excepcion del FPSCR no se emulan */']
        return unsupported(f'op63/{xo}')

    # ---- paired singles (4) --------------------------------------------------
    if op == 4:
        return translate_ps(c, pc, w, rd, ra, rb, rcc, unsupported)

    return unsupported(f'op{op}')


def wrap_cond(pre, expr, body):
    if expr == '1':
        return ([pre] if pre else []) + body
    return [f'{pre}if ({expr}) {{'] + ['    ' + b for b in body] + ['}']


def translate31(c, pc, w, rd, ra, rb, Rc, unsupported):
    xo = (w >> 1) & 0x3FF
    xo9 = xo & 0x1FF
    R = lambda n: f'ctx->r[{n}]'
    F = lambda n: f'ctx->f[{n}]'
    P = lambda n: f'ctx->ps1[{n}]'
    rec = (lambda v: [f'rc0(ctx, {v});']) if Rc else (lambda v: [])
    D, A, B = R(rd), R(ra), R(rb)
    # aritmetica (formas XO, el bit OE se ignora)
    arith = {
        266: f'{D} = {A} + {B};',
        40: f'{D} = {B} - {A};',
        104: f'{D} = 0u - {A};',
        235: f'{D} = {A} * {B};',
        75: f'{D} = (uint32_t)(((int64_t)(int32_t){A} * (int64_t)(int32_t){B}) >> 32);',
        11: f'{D} = (uint32_t)(((uint64_t){A} * (uint64_t){B}) >> 32);',
        491: f'{D} = divw((int32_t){A}, (int32_t){B});',
        459: f'{D} = divwu({A}, {B});',
        10: f'{{ uint64_t t_ = (uint64_t){A} + {B}; ctx->xer_ca = (uint32_t)(t_ >> 32); {D} = (uint32_t)t_; }}',
        8: f'{{ uint64_t t_ = (uint64_t)(uint32_t)~{A} + {B} + 1; ctx->xer_ca = (uint32_t)(t_ >> 32); {D} = (uint32_t)t_; }}',
        138: f'{{ uint64_t t_ = (uint64_t){A} + {B} + ctx->xer_ca; ctx->xer_ca = (uint32_t)(t_ >> 32); {D} = (uint32_t)t_; }}',
        136: f'{{ uint64_t t_ = (uint64_t)(uint32_t)~{A} + {B} + ctx->xer_ca; ctx->xer_ca = (uint32_t)(t_ >> 32); {D} = (uint32_t)t_; }}',
        202: f'{{ uint64_t t_ = (uint64_t){A} + ctx->xer_ca; ctx->xer_ca = (uint32_t)(t_ >> 32); {D} = (uint32_t)t_; }}',
        200: f'{{ uint64_t t_ = (uint64_t)(uint32_t)~{A} + ctx->xer_ca; ctx->xer_ca = (uint32_t)(t_ >> 32); {D} = (uint32_t)t_; }}',
        234: f'{{ uint64_t t_ = (uint64_t){A} + ctx->xer_ca + 0xFFFFFFFFull; ctx->xer_ca = (uint32_t)(t_ >> 32); {D} = (uint32_t)t_; }}',
        232: f'{{ uint64_t t_ = (uint64_t)(uint32_t)~{A} + ctx->xer_ca + 0xFFFFFFFFull; ctx->xer_ca = (uint32_t)(t_ >> 32); {D} = (uint32_t)t_; }}',
    }
    if xo9 in arith:
        return [arith[xo9]] + rec(D)
    # logicas: rS en rd, resultado en ra
    S = R(rd)
    logic = {28: f'{S} & {B}', 444: f'{S} | {B}', 316: f'{S} ^ {B}', 124: f'~({S} | {B})',
             476: f'~({S} & {B})', 284: f'~({S} ^ {B})', 60: f'{S} & ~{B}', 412: f'{S} | ~{B}',
             24: f'slw({S}, {B})', 536: f'srw({S}, {B})', 26: f'cntlzw({S})',
             954: f'(uint32_t)(int32_t)(int8_t){S}', 922: f'(uint32_t)(int32_t)(int16_t){S}'}
    if xo in logic:
        return [f'{A} = {logic[xo]};'] + rec(A)
    if xo == 792:  # sraw
        return [f'{A} = sraw(ctx, {S}, {B} & 0x3F);'] + rec(A)
    if xo == 824:  # srawi
        sh = rb
        return [f'{A} = sraw(ctx, {S}, {sh});'] + rec(A)
    if xo == 0:
        return [f'cmp_s(ctx, {rd >> 2}, (int32_t){A}, (int32_t){B});']
    if xo == 32:
        return [f'cmp_u(ctx, {rd >> 2}, {A}, {B});']
    # cargas/guardados indexados
    lx = {23: ('R32', 'u', 0), 55: ('R32', 'u', 1), 87: ('R8', 'u', 0), 119: ('R8', 'u', 1),
          279: ('R16', 'u', 0), 311: ('R16', 'u', 1), 343: ('R16', 's', 0), 375: ('R16', 's', 1)}
    if xo in lx:
        fn, sg, upd = lx[xo]
        ea = f'({A} + {B})' if upd else f'({"0u" if ra == 0 else A} + {B})'
        val = f'{fn}(ea_)'
        if sg == 's':
            val = f'(uint32_t)(int32_t)(int16_t){val}'
        line = f'{{ uint32_t ea_ = {ea}; {D} = {val};'
        if upd:
            line += f' {A} = ea_;'
        return [line + ' }']
    sx = {151: ('W32', 'uint32_t', 0), 183: ('W32', 'uint32_t', 1), 215: ('W8', 'uint8_t', 0),
          247: ('W8', 'uint8_t', 1), 407: ('W16', 'uint16_t', 0), 439: ('W16', 'uint16_t', 1)}
    if xo in sx:
        fn, ty, upd = sx[xo]
        ea = f'({A} + {B})' if upd else f'({"0u" if ra == 0 else A} + {B})'
        line = f'{{ uint32_t ea_ = {ea}; {fn}(ea_, ({ty}){S});'
        if upd:
            line += f' {A} = ea_;'
        return [line + ' }']
    if xo == 534:
        return [f'{D} = bswap32(R32({ea_x(ra, rb)}));']
    if xo == 662:
        return [f'W32({ea_x(ra, rb)}, bswap32({S}));']
    if xo == 790:
        return [f'{D} = bswap16(R16({ea_x(ra, rb)}));']
    if xo == 918:
        return [f'W16({ea_x(ra, rb)}, bswap16((uint16_t){S}));']
    fx = {535: 'lfsx', 567: 'lfsux', 599: 'lfdx', 631: 'lfdux', 663: 'stfsx', 695: 'stfsux',
          727: 'stfdx', 759: 'stfdux', 983: 'stfiwx'}
    if xo in fx:
        name = fx[xo]
        upd = 'u' in name[3:]
        ea = f'({A} + {B})' if upd else ea_x(ra, rb)
        tail = f' {A} = ea_;' if upd else ''
        if name.startswith('lfs'):
            body = f'{F(rd)} = {P(rd)} = (double)u32_f(R32(ea_));'
        elif name.startswith('lfd'):
            body = f'{F(rd)} = u64_d(R64(ea_));'
        elif name.startswith('stfs'):
            body = f'W32(ea_, f_u32((float){F(rd)}));'
        elif name.startswith('stfd'):
            body = f'W64(ea_, d_u64({F(rd)}));'
        else:
            body = f'W32(ea_, fpr_lo({F(rd)}));'
        return [f'{{ uint32_t ea_ = {ea}; {body}{tail} }}']
    # registros especiales
    spr = ((w >> 16) & 31) | (((w >> 11) & 31) << 5)
    if xo == 339:  # mfspr
        if spr == 1:
            return [f'{D} = (ctx->xer_so << 31) | (ctx->xer_ca << 29);']
        if spr == 8:
            return [f'{D} = ctx->lr;']
        if spr == 9:
            return [f'{D} = ctx->ctr;']
        if 912 <= spr <= 919:
            return [f'{D} = ctx->gqr[{spr - 912}];']
        return [f'{D} = ctx->spr[{spr}];']
    if xo == 467:  # mtspr
        if spr == 1:
            return [f'ctx->xer_so = ({S} >> 31) & 1; ctx->xer_ca = ({S} >> 29) & 1;']
        if spr == 8:
            return [f'ctx->lr = {S};']
        if spr == 9:
            return [f'ctx->ctr = {S};']
        if 912 <= spr <= 919:
            return [f'ctx->gqr[{spr - 912}] = {S};']
        if spr == 923:  # DMA_L: copia entre la cache bloqueada y la RAM
            return [f'ctx->spr[923] = {S}; ppc_dma(ctx);']
        return [f'ctx->spr[{spr}] = {S};']
    if xo == 371:  # mftb
        tbr = spr
        if tbr == 268:
            return [f'{D} = (uint32_t)ppc_timebase();']
        return [f'{D} = (uint32_t)(ppc_timebase() >> 32);']
    if xo == 19:
        return [f'{D} = ctx->cr;']
    if xo == 144:  # mtcrf
        crm = (w >> 12) & 0xFF
        m = 0
        for i in range(8):
            if crm & (0x80 >> i):
                m |= 0xF << (28 - 4 * i)
        return [f'ctx->cr = (ctx->cr & {h(~m)}) | ({S} & {h(m)});']
    if xo == 83:
        return [f'{D} = ctx->msr;']
    if xo == 146:
        return [f'ctx->msr = {S};']
    if xo == 512:  # mcrxr
        return [f'cr_set(ctx, {rd >> 2}, (ctx->xer_so << 3) | (ctx->xer_ca << 1)); ctx->xer_so = ctx->xer_ca = 0;']
    if xo in (595, 659):  # mfsr, mfsrin
        return [f'{D} = 0;']
    if xo in (210, 242, 306, 54, 86, 470, 982, 598, 854, 278, 246):  # mtsr, tlbie, caches, sync, eieio
        return []
    if xo == 1014:  # dcbz
        return [f'{{ uint32_t ea_ = {ea_x(ra, rb)} & ~31u; for (int i_ = 0; i_ < 32; i_ += 4) W32(ea_ + i_, 0); }}']
    if xo == 4:
        return unsupported('tw')
    return unsupported(f'op31/{xo}')


def translate_ps(c, pc, w, rd, ra, rb, rcc, unsupported):
    xo5 = (w >> 1) & 31
    xo6 = (w >> 1) & 0x3F
    xo10 = (w >> 1) & 0x3FF
    A0, B0, C0, D0 = (f'ctx->f[{x}]' for x in (ra, rb, rcc, rd))
    A1, B1, C1, D1 = (f'ctx->ps1[{x}]' for x in (ra, rb, rcc, rd))

    def both(e0, e1):
        return [f'{{ double t0_ = RS({e0}), t1_ = RS({e1}); {D0} = t0_; {D1} = t1_; }}']

    if xo6 in (6, 7, 38, 39):  # psq_lx, psq_stx, psq_lux, psq_stux
        W = (w >> 10) & 1
        I = (w >> 7) & 7
        upd = xo6 in (38, 39)
        ea = f'(ctx->r[{ra}] + ctx->r[{rb}])' if upd else ea_x(ra, rb)
        fn = 'PSQ_L' if xo6 in (6, 38) else 'PSQ_ST'
        line = f'{{ uint32_t ea_ = {ea}; {fn}(ctx, {rd}, ea_, {W}, {I});'
        if upd:
            line += f' ctx->r[{ra}] = ea_;'
        return [line + ' }']
    if xo10 == 1014:  # dcbz_l
        return [f'{{ uint32_t ea_ = {ea_x(ra, rb)} & ~31u; for (int i_ = 0; i_ < 32; i_ += 4) W32(ea_ + i_, 0); }}']
    t5 = {
        21: (f'{A0} + {B0}', f'{A1} + {B1}'),
        20: (f'{A0} - {B0}', f'{A1} - {B1}'),
        25: (f'{A0} * {C0}', f'{A1} * {C1}'),
        18: (f'{A0} / {B0}', f'{A1} / {B1}'),
        29: (f'{A0} * {C0} + {B0}', f'{A1} * {C1} + {B1}'),
        28: (f'{A0} * {C0} - {B0}', f'{A1} * {C1} - {B1}'),
        30: (f'-({A0} * {C0} - {B0})', f'-({A1} * {C1} - {B1})'),
        31: (f'-({A0} * {C0} + {B0})', f'-({A1} * {C1} + {B1})'),
        12: (f'{A0} * {C0}', f'{A1} * {C0}'),          # ps_muls0
        13: (f'{A0} * {C1}', f'{A1} * {C1}'),          # ps_muls1
        14: (f'{A0} * {C0} + {B0}', f'{A1} * {C0} + {B1}'),  # ps_madds0
        15: (f'{A0} * {C1} + {B0}', f'{A1} * {C1} + {B1}'),  # ps_madds1
        10: (f'{A0} + {B1}', f'{C1}'),                 # ps_sum0
        11: (f'{C0}', f'{A0} + {B1}'),                 # ps_sum1
        23: (f'({A0} >= 0.0) ? {C0} : {B0}', f'({A1} >= 0.0) ? {C1} : {B1}'),  # ps_sel
        24: (f'1.0 / {B0}', f'1.0 / {B1}'),            # ps_res
        26: (f'1.0 / sqrt({B0})', f'1.0 / sqrt({B1})'),  # ps_rsqrte
    }
    if xo5 in t5 and xo5 >= 10:
        e0, e1 = t5[xo5]
        return both(e0, e1)
    moves = {
        40: (f'-{B0}', f'-{B1}'),                      # ps_neg
        72: (B0, B1),                                  # ps_mr
        136: (f'-fabs({B0})', f'-fabs({B1})'),         # ps_nabs
        264: (f'fabs({B0})', f'fabs({B1})'),           # ps_abs
        528: (A0, B0),                                 # ps_merge00
        560: (A0, B1),                                 # ps_merge01
        592: (A1, B0),                                 # ps_merge10
        624: (A1, B1),                                 # ps_merge11
    }
    if xo10 in moves:
        e0, e1 = moves[xo10]
        if xo10 >= 528:
            return [f'{{ double t0_ = RS({e0}), t1_ = RS({e1}); {D0} = t0_; {D1} = t1_; }}']
        return [f'{{ double t0_ = {e0}, t1_ = {e1}; {D0} = t0_; {D1} = t1_; }}']
    if xo10 in (0, 32):
        return [f'fcmp(ctx, {rd >> 2}, {A0}, {B0});']
    if xo10 in (64, 96):
        return [f'fcmp(ctx, {rd >> 2}, {A1}, {B1});']
    return unsupported(f'ps/{xo10}')


# --------------------------------------------------------------------------
# Recompilador

class Recompiler:
    def __init__(self, img, funcs, jumptables, hle):
        self.img = img
        self.funcs = funcs            # lista ordenada de (addr, size, name)
        self.starts = {a: (s, n) for a, s, n in funcs}
        self.sorted_starts = [a for a, _, _ in funcs]
        self.jumptables = jumptables  # lista de listas de destinos
        self.hle = hle                # addr -> nombre hle
        self.report = []
        self.jt_by_func = defaultdict(set)
        for targets in jumptables:
            for t in targets:
                f = self.func_containing(t)
                if f is not None:
                    self.jt_by_func[f].add(t)

    def func_containing(self, a):
        import bisect
        i = bisect.bisect_right(self.sorted_starts, a) - 1
        if i < 0:
            return None
        s = self.sorted_starts[i]
        size = self.starts[s][0]
        return s if s <= a < s + size else None

    def cname(self, addr):
        if addr in self.hle:
            return f'hle_{self.hle[addr]}'
        return f'f_{addr:08X}'

    def branch(self, c, pc, tgt, lk, expr, pre):
        nxt = pc + 4
        if lk and tgt == nxt:
            return wrap_cond(pre, expr, [f'ctx->lr = {h(nxt)}; /* bl a la siguiente: solo carga LR */'])
        if lk:
            if tgt in self.starts or tgt in self.hle:
                body = [f'ctx->lr = {h(nxt)}; {self.cname(tgt)}(ctx);']
            else:
                body = [f'ctx->lr = {h(nxt)}; call_indirect(ctx, {h(tgt)});']
            return wrap_cond(pre, expr, body)
        if c.start <= tgt < c.end:
            c.labels.add(tgt)
            if tgt <= pc:   # bucle: punto seguro para interrupciones (esperas activas)
                return wrap_cond(pre, expr, [f'RT_LOOP_POLL(ctx); goto L_{tgt:08X};'])
            return wrap_cond(pre, expr, [f'goto L_{tgt:08X};'])
        # salto fuera de la funcion: llamada de cola
        if tgt in self.starts or tgt in self.hle:
            body = [f'{self.cname(tgt)}(ctx); return;']
        else:
            self.report.append(f'{pc:08X}: salto a 0x{tgt:08X} fuera de toda funcion conocida')
            body = [f'call_indirect(ctx, {h(tgt)}); return;']
        return wrap_cond(pre, expr, body)

    def bctr(self, c, pc):
        targets = sorted(self.jt_by_func.get(c.start, ()))
        if not targets:
            return ['call_indirect(ctx, ctx->ctr); return;']
        lines = ['switch (ctx->ctr) {']
        for t in targets:
            c.labels.add(t)
            lines.append(f'case {h(t)}: goto L_{t:08X};')
        lines.append('default: call_indirect(ctx, ctx->ctr); return;')
        lines.append('}')
        return lines

    def function(self, addr, size, name):
        c = Ctx(self, addr, addr + size)
        body = []
        last_uncond = False
        for pc in range(addr, addr + size, 4):
            w = self.img.u32(pc)
            if w is None:
                body.append((pc, [f'ppc_unimplemented(ctx, {h(pc)}, 0); /* fuera de la imagen */']))
                continue
            lines = translate(c, pc, w)
            body.append((pc, lines))
        for pc, w, n in c.unsupported:
            self.report.append(f'{pc:08X}: {w:08X} no soportada ({n}) en {name}')
        out = [f'/* {name} @ 0x{addr:08X}, 0x{size:X} bytes */', f'void f_{addr:08X}(PPCContext* restrict ctx) {{']
        for pc, lines in body:
            if pc in c.labels:
                out.append(f'L_{pc:08X}:;')
            for l in lines:
                out.append('    ' + l)
        # caida al final: continua en la funcion siguiente
        last = body[-1][1] if body else []
        ends = bool(last) and (last[-1].endswith('return;') or last[-1].startswith('goto ')
                               or (last[0].startswith('switch') and last[-1] == '}'))
        nxt = addr + size
        if not ends:
            if nxt in self.starts or nxt in self.hle:
                out.append(f'    {self.cname(nxt)}(ctx); /* cae en la funcion siguiente */')
            else:
                out.append('    return;')
        out.append('}')
        return out


def parse_symbols(path):
    funcs, jts = [], []
    for l in Path(path).read_text().splitlines():
        m = re.match(r'(\S+) = (\S+):0x([0-9A-F]+); // (.*)', l)
        if not m:
            continue
        name, sec, a, attrs = m[1], m[2], int(m[3], 16), m[4]
        sz = re.search(r'size:0x([0-9A-F]+)', attrs)
        size = int(sz[1], 16) if sz else 0
        if 'type:function' in attrs and size:
            funcs.append((a, size, name))
        elif name.startswith('jumptable_') and size:
            jts.append((a, size))
    funcs.sort()
    return funcs, jts


WRAP = set()   # funciones envueltas: se traducen igual (f_X) y las llamadas van a hle_<nombre>


def parse_hle(path, funcs):
    by_name = {n: a for a, _, n in funcs}
    hle = {}
    if not path or not Path(path).exists():
        return hle
    for l in Path(path).read_text().splitlines():
        l = l.split('#', 1)[0].strip()
        if not l:
            continue
        if l.split()[-1] == 'envolver':
            l = ' '.join(l.split()[:-1])
            envolver = True
        else:
            envolver = False
        key = l.split()[0]
        if re.match(r'0x[0-9A-Fa-f]+$', key):
            a = int(key, 16)
            name = l.split()[1] if len(l.split()) > 1 else f'{a:08X}'
        else:
            if key not in by_name:
                print(f'aviso: {key} (hle.txt) no existe en symbols.txt', file=sys.stderr)
                continue
            a = by_name[key]
            name = l.split()[1] if len(l.split()) > 1 else key
        hle[a] = name
        if envolver:
            WRAP.add(a)
    return hle


def main():
    args = sys.argv[1:]
    hle_path = None
    if '--hle' in args:
        i = args.index('--hle'); hle_path = args[i + 1]; del args[i:i + 2]
    if len(args) != 3:
        print(__doc__); return 2
    img = Image(args[0])
    funcs, jts = parse_symbols(args[1])
    out = Path(args[2]); out.mkdir(parents=True, exist_ok=True)
    jumptables = []
    for a, size in jts:
        targets = [img.u32(a + i) for i in range(0, size, 4)]
        jumptables.append([t for t in targets if t and img.is_code(t)])
    hle = parse_hle(hle_path, funcs)
    rc = Recompiler(img, funcs, jumptables, hle)

    # prototipos
    hdr = ['/* Generado por port/recomp/recomp.py: no editar. */', '#ifndef FUNCS_H', '#define FUNCS_H',
           '#include "ppc.h"']
    for a, s, n in funcs:
        if a not in hle or a in WRAP:
            hdr.append(f'void f_{a:08X}(PPCContext* ctx); /* {n} */')
    for a, n in sorted(hle.items()):
        hdr.append(f'void hle_{n}(PPCContext* ctx); /* 0x{a:08X} */')
    hdr.append('#endif')
    (out / 'funcs.h').write_text('\n'.join(hdr) + '\n')

    todo = [f for f in funcs if f[0] not in hle or f[0] in WRAP]
    nfiles = 0
    for i in range(0, len(todo), FUNCS_PER_FILE):
        chunk = todo[i:i + FUNCS_PER_FILE]
        lines = ['/* Generado por port/recomp/recomp.py: no editar. */', '#include "funcs.h"', '']
        for a, s, n in chunk:
            lines += rc.function(a, s, n)
            lines.append('')
        (out / f'code_{nfiles:03d}.c').write_text('\n'.join(lines))
        nfiles += 1

    # tabla para llamadas indirectas (ordenada por direccion)
    tab = ['/* Generado por port/recomp/recomp.py: no editar. */', '#include "funcs.h"',
           'typedef struct { uint32_t addr; PPCFunc fn; } FuncEntry;',
           'const FuncEntry g_func_table[] = {']
    for a, s, n in funcs:
        tab.append(f'    {{ 0x{a:08X}u, {rc.cname(a)} }},')
    tab.append('};')
    tab.append(f'const unsigned g_func_count = {len(funcs)};')
    names = ['const char* const g_func_names[] = {'] + [f'    "{n}",' for a, s, n in funcs] + ['};']
    (out / 'table.c').write_text('\n'.join(tab + names) + '\n')

    (out / 'report.txt').write_text('\n'.join(rc.report) + '\n')
    unsup = sum(1 for r in rc.report if 'no soportada' in r)
    print(f'{len(todo)} funciones traducidas en {nfiles} archivos; {len(hle)} reemplazadas por HLE; '
          f'{unsup} instrucciones no soportadas; {len(rc.report) - unsup} otros avisos (ver report.txt)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
