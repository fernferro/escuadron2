#!/usr/bin/env python3
"""Muestra el ensamblador original (compacto) de una o más funciones.

    python tools/fasm.py levelLoad fn_7FC0284C
"""
import re, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
want = set(sys.argv[1:])
for f in sorted((ROOT / 'build/GSWE64/asm').glob('*.s')):
    cur = None
    for line in f.read_text(errors='replace').splitlines():
        if line.startswith('.fn '):
            n = line.split()[1].rstrip(',')
            cur = n if n in want else None
            if cur: print(f'=== {cur}'); want.discard(n)
            continue
        if cur is None: continue
        if line.startswith('.endfn'): cur = None; continue
        m = re.match(r'/\* ([0-9A-F]{8}) [0-9A-F]{8}  [0-9A-F ]{11} \*/\t(.*)', line)
        if m: print(f'{m[1][-4:]} {m[2]}')
        elif line.startswith('.L_'): print(line)
