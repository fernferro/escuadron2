#!/usr/bin/env python3
"""Arma el instalador: pega los archivos (comprimidos con zlib) al final del programa instalador.

  empaquetar_instalador.py PROGRAMA.exe SALIDA.exe destino=origen [destino=origen ...]

'destino' es la ruta dentro de la carpeta instalada (con /). Formato: ver port/launcher/instalador.cpp.
Nunca meter archivos del juego.
"""
import struct, sys, zlib

prog, salida, pares = sys.argv[1], sys.argv[2], sys.argv[3:]
datos = open(prog, "rb").read()
out = open(salida, "wb")
out.write(datos)
pos = len(datos)
indice, total = [], 0
for par in pares:
    destino, origen = par.split("=", 1)
    crudo = open(origen, "rb").read()
    comp = zlib.compress(crudo, 9)
    out.write(comp)
    indice.append((destino, pos, len(comp), len(crudo)))
    print("  %-34s %7.1f MB -> %6.1f MB" % (destino, len(crudo) / 1048576, len(comp) / 1048576))
    pos += len(comp)
    total += len(crudo)
idx = pos
b = struct.pack("<I", len(indice))
for d, p, c, t in indice:
    n = d.encode("utf-8")
    b += struct.pack("<H", len(n)) + n + struct.pack("<QQQ", p, c, t)
out.write(b)
out.write(b"RLPAQ001" + struct.pack("<QQQ", idx, len(datos), total))
out.close()
import os, stat
os.chmod(salida, os.stat(salida).st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
print("%s: %.1f MB (instalado ocupa %.1f MB)" % (salida, (idx + len(b) + 32) / 1048576, total / 1048576))
