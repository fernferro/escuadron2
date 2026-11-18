#!/usr/bin/env python3
"""Genera docs/clases.md (árbol de herencia) desde classes.json."""
import json, sys
from collections import defaultdict
c = json.load(open(sys.argv[1])); out = open(sys.argv[2], 'w', encoding='utf-8')
def parent(n):
    b = c[n]['bases']; return b[-1] if b else None
kids = defaultdict(list)
for n in c: kids[parent(n)].append(n)
def nv(n): return sum(len(v['funcs']) for v in c[n]['vtables'][:1])
out.write('# Clases C++ del juego (desde las RTTI de CodeWarrior)\n\n')
out.write(f'{len(c)} clases. Entre paréntesis: cantidad de métodos virtuales y dirección de la vtable.\n')
out.write('Generado por `tools/analysis/class_tree.py`; no editar a mano.\n\n```\n')
def walk(n, depth):
    v = c[n]['vtables']
    extra = f'  ({nv(n)} virtuales, vtable 0x{v[0]["addr"]:08X})' if v else ''
    out.write('    ' * depth + n + extra + '\n')
    for k in sorted(kids[n]): walk(k, depth + 1)
for r in sorted(kids[None], key=lambda r: (-len(kids[r]), r)): walk(r, 0)
out.write('```\n')
