#!/usr/bin/env python3
"""Copia funciones ya traducidas (build/port/gen) a un archivo aparte, renombradas
rapida_XXXXXXXX, para compilarlas con matematica de punto flotante "relajada": los
paired singles sin redondear a simple precision en cada paso (RS) y con -O3. Solo para
codigo donde unos bits de diferencia no importan (el decodificador de video de los
mensajes de radio). Uso: rapidas.py GEN SALIDA.c 80055F60 8005AD80 ..."""
import sys, glob, re
gen, out, addrs = sys.argv[1], sys.argv[2], [a.upper() for a in sys.argv[3:]]
src = {}
for f in sorted(glob.glob(gen + '/code_*.c')):
    t = open(f).read()
    for a in addrs:
        m = re.search(r'^void f_%s\(PPCContext\* restrict ctx\) \{\n.*?^\}\n' % a, t, re.S | re.M)
        if m: src[a] = m.group(0)
falta = [a for a in addrs if a not in src]
if falta: sys.exit('no encontre: ' + ' '.join(falta))
with open(out, 'w') as o:
    o.write('/* Generado por port/tools/rapidas.py: no editar. */\n#include "funcs.h"\n#define RS(x) (x)\n\n')
    for a in addrs: o.write('void rapida_%s(PPCContext* restrict ctx);\n' % a)
    o.write('\n')
    for a in addrs:
        body = src[a].replace('void f_%s(' % a, 'void rapida_%s(' % a, 1)
        for b in addrs:   # las llamadas entre ellas tambien van a la version rapida
            body = body.replace('f_%s(ctx)' % b, 'rapida_%s(ctx)' % b)
        o.write(body + '\n')
