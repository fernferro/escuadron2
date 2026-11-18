#!/usr/bin/env python3
"""Lista las funciones de un rango con las constantes (.sdata2) y datos (.vmdata) que usan.

    python3 tools/analysis/frefs.py 7FC02EE8 7FC09C7C
"""
import re,sys
from pathlib import Path
lo,hi=int(sys.argv[1],16),int(sys.argv[2],16)
cur=None;out={}
for f in sorted(Path('build/GSWE64/asm').glob('auto_*vmtext*.s')):
    for line in f.read_text(errors='replace').splitlines():
        if line.startswith('.fn '):
            n=line.split()[1].rstrip(',');cur=n;out[n]=[set(),set(),set()]
        elif cur:
            for m in re.findall(r'lbl_(8009[6-9A-B][0-9A-F]{3})',line): out[cur][0].add(m)
            for m in re.findall(r'lbl_(7FD9[F-F][0-9A-F]{3}|7FDA[0-9A-F]{4})',line): out[cur][1].add(m)
            for m in re.findall(r'bl (\w+)',line): out[cur][2].add(m)
syms={}
for l in open('config/GSWE64/symbols.txt'):
    m=re.match(r'(\S+) = \.vmtext:0x([0-9A-F]+);.*size:0x([0-9A-F]+)',l)
    if m: syms[m[1]]=(int(m[2],16),int(m[3],16))
for n,(a,s) in sorted(syms.items(),key=lambda x:x[1]):
    if lo<=a<hi:
        r=out.get(n,[set(),set(),set()])
        print(f"{a:08X} {s:5X} {n:28} sd2={sorted(r[0])[:4]} data={sorted(r[1])[:2]}")
