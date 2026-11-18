#!/usr/bin/env python3
"""
Compara rápido una función compilada contra el original (sin enlazar).

    python tools/fcmp.py src/gsys/gsys_resource.c gsysGetResourceLength [más funciones...]

Compila con la misma línea que el build (lee build.ninja), y compara instrucción por
instrucción ignorando los campos con relocaciones. Muestra las diferencias lado a lado.
"""
import re, struct, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
B = ROOT / 'build/binutils/powerpc-eabi-'
src = sys.argv[1]; funcs = sys.argv[2:]
obj = f'build/GSWE64/src/{Path(src).relative_to("src").with_suffix(".o").as_posix()}'
subprocess.run(['ninja', obj], cwd=ROOT, check=True, stdout=subprocess.DEVNULL)

d = (ROOT / 'orig/GSWE64/combined.dol').read_bytes()
off = struct.unpack('>18I', d[0:72]); addr = struct.unpack('>18I', d[72:144]); size = struct.unpack('>18I', d[144:216])
def rb(a, n):
    for i in range(18):
        if size[i] and addr[i] <= a < addr[i] + size[i]:
            o = off[i] + a - addr[i]; return d[o:o + n]
syms = {}
for l in (ROOT / 'config/GSWE64/symbols.txt').read_text().splitlines():
    m = re.match(r'(\S+) = \S+:0x([0-9A-F]+);.*size:0x([0-9A-F]+)', l)
    if m: syms[m[1]] = (int(m[2], 16), int(m[3], 16))

nm = subprocess.run([f'{B}nm', '-S', obj], cwd=ROOT, capture_output=True, text=True).stdout
relocs = subprocess.run([f'{B}objdump', '-r', obj], cwd=ROOT, capture_output=True, text=True).stdout
reloc_off = {int(m[1], 16) & ~3 for m in re.finditer(r'^([0-9a-f]{8}) ', relocs, re.M)}
ob = subprocess.run([f'{B}objcopy', '-O', 'binary', '-j', '.vmtext', obj, '/dev/stdout'],
                    cwd=ROOT, capture_output=True).stdout
import tempfile
def dis(b):  # desensamblar bytes crudos con objdump
    with tempfile.NamedTemporaryFile(suffix='.bin') as t:
        t.write(b); t.flush()
        return subprocess.run([f'{B}objdump', '-D', '-b', 'binary', '-m', 'powerpc:common', '-EB', '-M', 'gekko', t.name],
                              capture_output=True, text=True).stdout.splitlines()
ok_all = True
for fn in funcs:
    m = re.search(rf'^([0-9a-f]+) ([0-9a-f]+) T {re.escape(fn)}$', nm, re.M)
    if not m: print(f'{fn}: no está en el objeto'); ok_all = False; continue
    fo, fs = int(m[1], 16), int(m[2], 16)
    oa, osz = syms[fn]
    mine, orig = ob[fo:fo + fs], rb(oa, osz)
    def ins(b):
        return [l.split('\t', 2)[-1] for l in dis(b) if re.match(r'\s+[0-9a-f]+:', l)]
    A, Bi = ins(orig), ins(mine)
    diffs = 0
    for i in range((max(len(orig), len(mine)) + 3) // 4):
        w1 = orig[i*4:i*4+4]; w2 = mine[i*4:i*4+4]
        same = w1 == w2 or (len(w1) == 4 and len(w2) == 4 and (fo + i*4) in reloc_off
                             and (w1[0] >> 2) == (w2[0] >> 2))
        if not same: diffs += 1
    status = 'OK' if diffs == 0 and fs == osz else f'DIFIERE ({diffs} instr., tamaño {fs:#x} vs original {osz:#x})'
    print(f'{fn}: {status}')
    if status != 'OK':
        ok_all = False
        for i in range((max(len(orig), len(mine)) + 3) // 4):
            a = A[i] if i < len(A) else ''; b = Bi[i] if i < len(Bi) else ''
            w1 = orig[i*4:i*4+4]; w2 = mine[i*4:i*4+4]
            same = w1 == w2 or (len(w1) == 4 and len(w2) == 4 and (fo + i*4) in reloc_off
                                and (w1[0] >> 2) == (w2[0] >> 2))
            mark = ' ' if same else '*'
            print(f'  {mark} {i*4:4x}  {a:34} | {b}')
sys.exit(0 if ok_all else 1)
