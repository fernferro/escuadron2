#!/usr/bin/env python3
"""Convierte archivos en arreglos de C: incrustar.py SALIDA.inc nombre=archivo ..."""
import sys
out = [ "/* generado por port/tools/incrustar.py: no editar */\n" ]
for arg in sys.argv[2:]:
    nombre, ruta = arg.split("=", 1)
    datos = open(ruta, "rb").read()
    out.append("static const unsigned char %s[%d] = {\n" % (nombre, len(datos)))
    for i in range(0, len(datos), 24):
        out.append(",".join(str(b) for b in datos[i:i + 24]) + ",\n")
    out.append("};\n")
open(sys.argv[1], "w").write("".join(out))
