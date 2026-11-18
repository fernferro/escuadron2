#!/usr/bin/env python3
"""Junta las claves de los shaders especializados (configuracion de las etapas del TEV)
de uno o mas archivos escuadron2_shaders.cache y genera port/runtime/shader_claves.inc.

El port las compila en segundo plano al arrancar (si la placa compila en paralelo), asi
la primera vez que aparece un efecto en una mision el shader ya esta listo. Los binarios
de la cache dependen de la placa y del driver; las claves no: son del juego.

uso: python3 port/tools/claves_shaders.py SALIDA.inc CACHE1 [CACHE2 ...]
"""
import struct, sys

KEY = 16 + 16 * 4 * 4   # SpecKey: xf, zf, ns, pad + st[16][4] (int32)

def claves(path):
    d = open(path, 'rb').read()
    magic, _ = struct.unpack('<II', d[:8])
    if magic != 0x31435352:
        return
    o = 8
    while o + KEY + 20 <= len(d):
        k = struct.unpack('<%di' % (KEY // 4), d[o:o + KEY])
        ln = struct.unpack('<5I', d[o + KEY:o + KEY + 20])[2]
        o += KEY + 20 + ln
        xf, zf, ns, pad = k[:4]
        if not (0 <= ns <= 16):
            break
        st = tuple(k[4:4 + 4 * ns])
        yield (xf, zf, ns, pad, st)

def main():
    out, caches = sys.argv[1], sys.argv[2:]
    # pad: 1 = la prueba de alfa siempre pasa. Las caches viejas (antes de esa clave) tienen
    # pad 0 en todas: ahi no se sabe y se compilan las dos variantes (2).
    todas = {}
    for c in caches:
        ks = list(claves(c))
        sabe = any(k[3] for k in ks)
        for xf, zf, ns, pad, st in ks:
            key = (xf, zf, ns, st)
            p = (1 << pad) if sabe else 3
            todas[key] = todas.get(key, 0) | p
    filas = []
    for (xf, zf, ns, st), m in sorted(todas.items()):
        filas.append('{%d,%d,%d,%d,{%s}},' % (xf, zf, ns, m, ','.join('0x%X' % (v & 0xFFFFFFFF) for v in st)))
    with open(out, 'w') as f:
        f.write('/* generado por port/tools/claves_shaders.py: %d configuraciones de etapas del TEV */\n' % len(filas))
        f.write('\n'.join(filas) + '\n')
    print(len(filas), 'claves')

main()
