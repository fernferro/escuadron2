#!/usr/bin/env python3
"""Traduce a C los programas del DSP de la GameCube (el sonido de MusyX).

    python3 port/recomp/dsprecomp.py SALIDA.c nombre=archivo.bin@base ...

Cada programa ("microcodigo") que el juego carga en la memoria de instrucciones del DSP se
traduce a una funcion C, igual que el codigo PowerPC del juego. Junto con cada programa se
traduce la ROM del DSP (base 0x8000), porque los programas llaman a rutinas de la ROM.

La semantica de cada instruccion esta en port/dsp_nativo/dsp_ops.h; aca solo se resuelve el
flujo (saltos, llamadas, bucles por hardware, excepciones) y que funcion corresponde a cada
instruccion (tablas en dsp_tablas.py).

Cada funcion generada tiene la forma
    int dsp_ejecutar_NOMBRE(int presupuesto)
y devuelve DSP_FIN_* (dsp_nativo.h) cuando se acaba el presupuesto, el DSP se detiene (HALT),
espera un mensaje o salta a una direccion que no esta en el programa traducido.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dsp_tablas import PLANTILLAS, PLANTILLAS_EXT, FUNCIONES, FUNCIONES_EXT


def _first(table, w, key=lambda t: (t[1], t[2])):
    for t in table:
        opc, mask = key(t)
        if (w & mask) == opc:
            return t
    return None


_tmpl_cache = {}
_func_cache = {}


def plantilla(w):
    if w not in _tmpl_cache:
        _tmpl_cache[w] = _first(PLANTILLAS, w)
    return _tmpl_cache[w]


def funcion(w):
    if w not in _func_cache:
        t = _first(FUNCIONES, w, key=lambda t: (t[0], t[1]))
        _func_cache[w] = t[2] if t else 'nop'
    return _func_cache[w]


def funcion_ext(w):
    e = w & (0x7F if (w >> 12) == 3 else 0xFF)
    t = _first(FUNCIONES_EXT, e, key=lambda t: (t[0], t[1]))
    return t[2] if t else None


def tam(w):
    t = plantilla(w)
    return t[3] if t else 1


# instrucciones despues de las cuales Dolphin revisa excepciones (DSPAnalyzer.cpp)
_EXC_OPS = (0x00c0, 0x00e0, 0x1600, 0x1800, 0x1880, 0x1900, 0x1980, 0x1a00, 0x1a80, 0x1b00,
            0x1b80, 0x2000, 0x2800, 0x2c00)


def revisa_excepciones(w):
    t = plantilla(w)
    if not t:
        return False
    return t[1] in _EXC_OPS or t[4]


def _escritores_de_banderas():
    """Instrucciones que calculan las banderas de comparacion (UpdateSR16/64) en dsp_ops.h."""
    import re
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'dsp_nativo', 'dsp_ops.h')
    if not os.path.exists(path):   # en el paquete del armador: herramientas/armar/include
        path = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'include', 'dsp_ops.h')
    src = open(path).read()
    out = set()
    for m in re.finditer(r'DSPI void op_(\w+)\(u16 opc, u16 imm_, int fl\)\n\{(.*?)\n\}', src, re.S):
        if re.search(r'UpdateSR(16|64)', m.group(2)):
            out.add(m.group(1))
    return out


ESCRITORES = _escritores_de_banderas()
LECTORES_REG = ('mrr', 'sr', 'srr', 'srrd', 'srri', 'srrn')


def lee_banderas(w):
    """1 si la instruccion puede leer las banderas de comparacion de $sr."""
    t = plantilla(w)
    if not t:
        return False
    if (w & 0xfff0) in (0x0290, 0x02b0, 0x02d0, 0x02f0, 0x0270) or (w & 0xff00) == 0x1700:
        return (w & 0xf) != 0xf or (w & 0xfff0) in (0x02f0,)
    f = funcion(w)
    if f in LECTORES_REG and (w & 0x1f) == 0x13:
        return True
    if ((w & 0xffe0) in (0x0040, 0x0060)) and (w & 0x1f) == 0x13:
        return True
    return False


class Imagen:
    def __init__(self, base, words):
        self.base = base
        self.words = words

    def leer(self, a):
        o = a - self.base
        return self.words[o] if 0 <= o < len(self.words) else None


def analizar(imagenes):
    """Inicios de instruccion (barrido lineal + destinos de saltos) y finales de bucle."""
    mem = {}
    for im in imagenes:
        for i, w in enumerate(im.words):
            mem[(im.base + i) & 0xFFFF] = w
    starts = set()
    loop_end = set()
    for im in imagenes:
        a = im.base
        end = im.base + len(im.words)
        while a < end:
            w = mem[a]
            starts.add(a)
            n = tam(w)
            t = plantilla(w)
            if t and ((w & 0xffe0) == 0x0060 or (w & 0xff00) == 0x1100):
                loop_end.add(mem.get(a + 1, 0))
            elif t and ((w & 0xffe0) == 0x0040 or (w & 0xff00) == 0x1000):
                loop_end.add((a + 1) & 0xFFFF)
            a += n
    # destinos de saltos fijos que caigan en medio (decodificaciones solapadas)
    pend = [a for a in starts]
    while pend:
        a = pend.pop()
        w = mem.get(a)
        if w is None:
            continue
        if (w & 0xfff0) in (0x0290, 0x02b0) or (w & 0xffe0) == 0x0060 or (w & 0xff00) == 0x1100:
            d = mem.get(a + 1)
            if d is not None and d in mem and d not in starts and (w & 0xfff0) in (0x0290, 0x02b0):
                starts.add(d)
                pend.append(d)
    return mem, starts, loop_end


def banderas_vivas(mem, a, loop_end, loop_start, prof=0, j=None):
    """Despues de la instruccion en a, alguien lee las banderas antes de que se vuelvan a calcular?"""
    if prof > 4:
        return True
    if j is None:
        j = (a + tam(mem.get(a, 0))) & 0xFFFF
    for _ in range(40):
        if ((j - 1) & 0xFFFF) in loop_end:
            # fin de un bucle por hardware: puede volver al inicio o seguir
            st = loop_start.get((j - 1) & 0xFFFF)
            if st is None:
                return True
            if banderas_vivas(mem, a, loop_end, loop_start, prof + 1, st):
                return True
            loop_end_ok = loop_end - {(j - 1) & 0xFFFF}
            return banderas_vivas(mem, a, loop_end_ok, loop_start, prof + 1, j)
        w = mem.get(j)
        if w is None:
            return True
        if lee_banderas(w):
            return True
        t = plantilla(w)
        if t and funcion(w) in ESCRITORES and not ((w & 0xfff0) in (0x0290, 0x02b0, 0x02d0, 0x02f0)):
            return False
        if w == 0x0021:
            return False
        if (w & 0xfff0) == 0x0290:          # JMP siempre (la condicional ya se trato)
            j = mem.get(j + 1, 0)
            prof += 1
            if prof > 4:
                return True
            continue
        if (w & 0xfff0) in (0x02b0, 0x02d0, 0x02f0) or (w & 0xff10) in (0x1700, 0x1710):
            return True
        if (w & 0xffe0) in (0x0040, 0x0060) or (w & 0xff00) in (0x1000, 0x1100) or (w & 0xfff0) == 0x0270:
            return True
        j = (j + tam(w)) & 0xFFFF
    return True


def generar(nombre, imagenes, out, pasos=(0, 1)):
    mem, starts, loop_end = analizar(imagenes)
    vivas = {}
    # inicio de cada bucle (si un final tiene un unico inicio conocido)
    loop_start = {}
    multi = set()
    for x in starts:
        w = mem[x]
        if (w & 0xffe0) == 0x0060 or (w & 0xff00) == 0x1100:
            e, st = mem.get(x + 1, 0), (x + 2) & 0xFFFF
        elif (w & 0xffe0) == 0x0040 or (w & 0xff00) == 0x1000:
            e, st = (x + 1) & 0xFFFF, (x + 1) & 0xFFFF
        else:
            continue
        if e in loop_start and loop_start[e] != st:
            multi.add(e)
        loop_start[e] = st
    for e in multi:
        del loop_start[e]
    S = sorted(starts)
    L = lambda a: 'L_%04x' % (a & 0xFFFF)
    o = out.append
    o('/* ---- programa del DSP "%s" ---- */' % nombre)
    for paso in pasos:
        fn = 'dsp_ejecutar_%s%s' % (nombre, '_paso' if paso else '')
        o('int %s(int presupuesto)' % fn)
        o('{')
        o('    int quedan = presupuesto;')
        o('despachar:')
        o('    if (g_dsp_codigo_cambiado) RET(DSP_FIN_CODIGO);')
        o('    switch (g.pc) {')
        for a in S:
            o('    case 0x%04x: goto %s;' % (a, L(a)))
        o('    default: RET(DSP_FIN_DESCONOCIDO);')
        o('    }')
        for a in S:
            w = mem[a]
            n = tam(w)
            w1 = mem.get((a + 1) & 0xFFFF, 0)
            nxt = (a + n) & 0xFFFF
            t = plantilla(w)
            name = t[0] if t else 'CW'
            o('%s: /* %04x %s */' % (L(a), w, name))
            if paso:
                o('    if (--quedan < 0) { g.pc = 0x%04x; RET(DSP_FIN_PRESUPUESTO); }' % a)

            def salto(dest, dinamico=False):
                # despues de saltar, Dolphin revisa el fin de bucle con pc-1
                if dinamico:
                    o('        if (dsp_es_fin_de_bucle((u16)(g.pc - 1))) HandleLoop(g.pc);')
                    o('        if (--quedan < 0) RET(DSP_FIN_PRESUPUESTO);')
                    o('        goto despachar;')
                    return
                d = dest & 0xFFFF
                if ((d - 1) & 0xFFFF) in loop_end:
                    o('        g.pc = 0x%04x; HandleLoop(0x%04x); goto despachar;' % (d, d))
                    return
                if d <= a:   # hacia atras: punto para ceder el hilo
                    o('        if (--quedan < 0 || g.idle_wait) { g.pc = 0x%04x; RET(g.idle_wait ? DSP_FIN_ESPERA : DSP_FIN_PRESUPUESTO); }' % d)
                if d in starts:
                    o('        goto %s;' % L(d))
                else:
                    o('        g.pc = 0x%04x; goto despachar;' % d)

            if not t:
                body = None    # palabra de datos: Dolphin la ejecuta como "nop"
            elif (w & 0xfff0) == 0x0290:          # Jcc
                o('    if (CheckCondition(0x%x)) {' % (w & 0xf))
                salto(w1)
                o('    }')
            elif (w & 0xfff0) == 0x02b0:          # CALLcc
                o('    if (CheckCondition(0x%x)) {' % (w & 0xf))
                o('        StoreStack(STACK_CALL, 0x%04x);' % nxt)
                salto(w1)
                o('    }')
            elif (w & 0xfff0) == 0x02d0:          # RETcc
                o('    if (CheckCondition(0x%x)) {' % (w & 0xf))
                o('        g.pc = PopStack(STACK_CALL);')
                salto(None, True)
                o('    }')
            elif (w & 0xfff0) == 0x02f0:          # RTIcc
                o('    if (CheckCondition(0x%x)) {' % (w & 0xf))
                o('        g.r.sr = PopStack(STACK_DATA);')
                o('        g.pc = PopStack(STACK_CALL);')
                salto(None, True)
                o('    }')
            elif (w & 0xff10) == 0x1700:          # JMPRcc
                o('    if (CheckCondition(0x%x)) {' % (w & 0xf))
                o('        g.pc = OpReadRegister(%d);' % ((w >> 5) & 7))
                salto(None, True)
                o('    }')
            elif (w & 0xff10) == 0x1710:          # CALLRcc
                o('    if (CheckCondition(0x%x)) {' % (w & 0xf))
                o('        u16 d_ = OpReadRegister(%d);' % ((w >> 5) & 7))
                o('        StoreStack(STACK_CALL, 0x%04x);' % nxt)
                o('        g.pc = d_;')
                salto(None, True)
                o('    }')
            elif (w & 0xfff0) == 0x0270:          # IFcc
                n2 = tam(mem.get(nxt, 0))
                o('    if (!CheckCondition(0x%x)) {' % (w & 0xf))
                salto(nxt + n2)
                o('    }')
            elif w == 0x0021:                     # HALT
                o('    g.control_reg |= CR_HALT; g.pc = 0x%04x; RET(DSP_FIN_HALT);' % a)
                continue
            elif (w & 0xffe0) in (0x0040,) or (w & 0xff00) == 0x1000:   # LOOP / LOOPI
                if (w & 0xff00) == 0x1000:
                    o('    { const u16 cnt_ = 0x%x;' % (w & 0xff))
                else:
                    o('    { const u16 cnt_ = OpReadRegister(%d);' % (w & 0x1f))
                o('      if (cnt_ != 0) {')
                o('        StoreStack(STACK_CALL, 0x%04x); StoreStack(STACK_LOOPADDR, 0x%04x); StoreStack(STACK_LOOPCNT, cnt_);' % (nxt, nxt))
                o('      } else {')
                n2 = tam(mem.get(nxt, 0))
                salto(nxt + n2)
                o('      } }')
            elif (w & 0xffe0) == 0x0060 or (w & 0xff00) == 0x1100:    # BLOOP / BLOOPI
                if (w & 0xff00) == 0x1100:
                    o('    { const u16 cnt_ = 0x%x;' % (w & 0xff))
                else:
                    o('    { const u16 cnt_ = OpReadRegister(%d);' % (w & 0x1f))
                o('      if (cnt_ != 0) {')
                o('        StoreStack(STACK_CALL, 0x%04x); StoreStack(STACK_LOOPADDR, 0x%04x); StoreStack(STACK_LOOPCNT, cnt_);' % (nxt, w1))
                o('      } else {')
                n2 = tam(mem.get(w1, 0))
                salto(w1 + n2)
                o('      } }')
            else:
                f = funcion(w)
                fl = 1
                if not paso and f in ESCRITORES:
                    if a not in vivas:
                        vivas[a] = banderas_vivas(mem, a, loop_end, loop_start)
                    fl = 1 if vivas[a] else 0
                ext = funcion_ext(w) if t[4] else None
                if ext and ext != 'nop_ext':
                    o('    { DspWB wb_ = DSP_WB_INIT; op_%s(0x%04x, 0, 1, &wb_); op_%s(0x%04x, 0x%04x, %d); dsp_wb_apply(&wb_); }' % (ext, w, f, w, w1, fl))
                else:
                    o('    op_%s(0x%04x, 0x%04x, %d);' % (f, w, w1, fl))
            # despues de la instruccion: fin de bucle y excepciones
            fin = ((nxt - 1) & 0xFFFF) in loop_end
            exc = t is not None and revisa_excepciones(w)
            if fin:
                st = loop_start.get((nxt - 1) & 0xFFFF)
                ir = ('if (g.pc == 0x%04x) goto %s; ' % (st, L(st))) if (st is not None and st in starts and not exc) else ''
                o('    if (HandleLoop(0x%04x)) { if (--quedan < 0 || g.idle_wait) RET(g.idle_wait ? DSP_FIN_ESPERA : DSP_FIN_PRESUPUESTO); %sgoto despachar; }' % (nxt, ir))
                if exc:
                    o('    if (g.exceptions && CheckExceptions()) goto despachar;')
            elif exc:
                # las excepciones solo aparecen al tocar los registros de hardware (0xFFxx)
                o('    if (g.exceptions) { g.pc = 0x%04x; if (CheckExceptions()) goto despachar; }' % nxt)
            if nxt not in starts:
                o('    g.pc = 0x%04x; goto despachar;' % nxt)
        o('}')
        o('')
    # tabla de finales de bucle (para los saltos dinamicos)
    return loop_end


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    out_path = sys.argv[1]
    rom = None
    progs = []
    dol = None
    for arg in sys.argv[2:]:
        if arg.startswith('--dol='):
            dol = open(arg[6:], 'rb').read()
            continue
        name, rest = arg.split('=', 1)
        path, base = rest.split('@')
        if path.startswith('dol:'):          # dol:DESPLAZAMIENTO:LARGO:SHA1 (dentro de --dol)
            _, off, ln, sha = path.split(':')
            data = dol[int(off, 0):int(off, 0) + int(ln, 0)]
            import hashlib
            if hashlib.sha1(data).hexdigest() != sha:
                sys.exit('dsprecomp: el programa %s no coincide con el esperado (sha1)' % name)
        else:
            data = open(path, 'rb').read()
        words = list(struct.unpack('>%dH' % (len(data) // 2), data[:len(data) // 2 * 2]))
        im = Imagen(int(base, 16), words)
        if name == 'rom':
            rom = im
        else:
            progs.append((name, im))
    out = ['/* Generado por port/recomp/dsprecomp.py: programas del DSP traducidos a C. No editar. */',
           '#include "dsp_nativo.h"', '#include "dsp_ops.h"',
           '#define RET(x) do { g_dsp_quedan = quedan; return (x); } while (0)', '']
    ends = set()
    tabla = []
    out2 = list(out)     # las variantes "paso a paso" van a otro archivo (se compilan con menos optimizacion)
    for name, im in progs:
        imgs = [im] + ([rom] if rom else [])
        ends |= generar(name, imgs, out, (0,))
        generar(name, imgs, out2, (1,))
        out.append('int dsp_ejecutar_%s_paso(int presupuesto);' % name)
        tabla.append((name, im))
    if rom:
        ends |= generar('rom', [rom], out, (0,))
        generar('rom', [rom], out2, (1,))
        out.append('int dsp_ejecutar_rom_paso(int presupuesto);')
    open(out_path.replace('.c', '_paso.c'), 'w').write('\n'.join(out2) + '\n')
    # imagenes para reconocer que programa hay en la memoria del DSP
    for name, im in progs:
        out.append('static const u16 s_img_%s[%d] = {' % (name, len(im.words)))
        for i in range(0, len(im.words), 16):
            out.append('    ' + ', '.join('0x%04x' % w for w in im.words[i:i + 16]) + ',')
        out.append('};')
    out.append('const DspPrograma g_dsp_programas[] = {')
    for name, im in progs:
        out.append('    { "%s", s_img_%s, %d, dsp_ejecutar_%s, dsp_ejecutar_%s_paso },' % (name, name, len(im.words), name, name))
    out.append('    { 0, 0, 0, 0, 0 }')
    out.append('};')
    if rom:
        out.append('const DspPrograma g_dsp_programa_rom = { "rom", 0, 0, dsp_ejecutar_rom, dsp_ejecutar_rom_paso };')
    out.append('static const u8 s_fin_bucle[0x10000 / 8] = {')
    bits = bytearray(0x2000)
    for e in ends:
        bits[e >> 3] |= 1 << (e & 7)
    for i in range(0, len(bits), 32):
        out.append('    ' + ', '.join(str(b) for b in bits[i:i + 32]) + ',')
    out.append('};')
    out.append('int dsp_es_fin_de_bucle(u16 a) { return (s_fin_bucle[a >> 3] >> (a & 7)) & 1; }')
    open(out_path, 'w').write('\n'.join(out) + '\n')


if __name__ == '__main__':
    main()
