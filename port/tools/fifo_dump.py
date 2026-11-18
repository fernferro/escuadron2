#!/usr/bin/env python3
"""Decodifica un volcado de la cola grafica (escuadron2 --dump-fifo) y lista los comandos.

    python port/tools/fifo_dump.py build/fifo.bin [--max N] [--from OFFSET]
"""
import struct, sys
args = sys.argv[1:]
path = args[0]
mx = int(args[args.index('--max') + 1]) if '--max' in args else 200
start = int(args[args.index('--from') + 1], 0) if '--from' in args else 0
d = open(path, 'rb').read()
cp = {}
CS = [1, 1, 2, 2, 4, 0, 0, 0]
COL = [2, 3, 4, 2, 3, 4, 0, 0]
def vsize(vat):
    lo = cp.get(0x50, 0); hi = cp.get(0x60, 0)
    a = cp.get(0x70 + vat, 0); b = cp.get(0x80 + vat, 0); c = cp.get(0x90 + vat, 0)
    s = (lo & 1) + bin((lo >> 1) & 0xFF).count('1')
    m = (lo >> 9) & 3; el = 3 if a & 1 else 2; f = (a >> 1) & 7
    s += el * CS[f] if m == 1 else (m - 1 if m else 0)
    m = (lo >> 11) & 3; el = 9 if (a >> 9) & 1 else 3; f = (a >> 10) & 7
    s += el * CS[f] if m == 1 else ((m - 1) * (3 if el == 9 and (a >> 31) else 1) if m else 0)
    for i in range(2):
        m = (lo >> (13 + 2 * i)) & 3; f = (a >> (14 + 4 * i)) & 7
        s += COL[f] if m == 1 else (m - 1 if m else 0)
    cnt = [(a >> 21) & 1, b & 1, (b >> 9) & 1, (b >> 18) & 1, (b >> 27) & 1, (c >> 5) & 1, (c >> 14) & 1, (c >> 23) & 1]
    fmt = [(a >> 22) & 7, (b >> 1) & 7, (b >> 10) & 7, (b >> 19) & 7, (b >> 28) & 7, (c >> 6) & 7, (c >> 15) & 7, (c >> 24) & 7]
    for i in range(8):
        m = (hi >> (2 * i)) & 3
        s += (2 if cnt[i] else 1) * CS[fmt[i]] if m == 1 else (m - 1 if m else 0)
    return s
p = 0; n = 0
while p < len(d):
    op = d[p]; show = p >= start and n < mx
    if op == 0: p += 1; continue
    if op == 0x08:
        reg = d[p + 1]; v = struct.unpack('>I', d[p + 2:p + 6])[0]; cp[reg] = v
        if show: print(f'{p:06X} CP  {reg:02X} = {v:08X}'); n += 1
        p += 6
    elif op == 0x10:
        h = struct.unpack('>I', d[p + 1:p + 5])[0]; cnt = (h >> 16) + 1
        if show: print(f'{p:06X} XF  {h & 0xFFFF:04X} x{cnt}'); n += 1
        p += 5 + 4 * cnt
    elif op in (0x20, 0x28, 0x30, 0x38):
        if show: print(f'{p:06X} XFI {op:02X} {d[p+1:p+5].hex()}'); n += 1
        p += 5
    elif op == 0x40:
        a, s = struct.unpack('>II', d[p + 1:p + 9])
        if show: print(f'{p:06X} DL  {a:08X} {s:X}'); n += 1
        p += 9
    elif op in (0x44, 0x48): p += 1
    elif op == 0x61:
        v = struct.unpack('>I', d[p + 1:p + 5])[0]
        if show: print(f'{p:06X} BP  {v >> 24:02X} = {v & 0xFFFFFF:06X}'); n += 1
        p += 5
    elif 0x80 <= op < 0xC0:
        c = struct.unpack('>H', d[p + 1:p + 3])[0]; vs = vsize(op & 7)
        if show: print(f'{p:06X} DRAW prim={(op >> 3) & 7} vat={op & 7} n={c} vsize={vs} -> {3 + c * vs} bytes'); n += 1
        p += 3 + c * vs
    else:
        print(f'{p:06X} ?? {op:02X}  contexto: {d[max(0,p-16):p].hex()} | {d[p:p+16].hex()}')
        break
