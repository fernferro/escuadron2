#!/usr/bin/env python3
"""
Construye referencias cruzadas a partir del ensamblador generado por dtk.

    python tools/analysis/xref.py build/GSWE64/asm orig/GSWE64/combined.dol config/GSWE64/symbols.txt out.json

Para cada función: dirección, tamaño, llamadas (bl), quién la llama, textos que usa
y en qué objetos de datos aparece como puntero (vtables / tablas de callbacks).
"""
import json, re, struct, sys
from pathlib import Path
from collections import defaultdict

asm_dir, dol_path, sym_path, out_path = map(Path, sys.argv[1:5])

d = dol_path.read_bytes()
off = struct.unpack('>18I', d[0:72]); addr = struct.unpack('>18I', d[72:144]); size = struct.unpack('>18I', d[144:216])
def rb(a, n):
    for i in range(18):
        if size[i] and addr[i] <= a < addr[i] + size[i]:
            o = off[i] + a - addr[i]
            return d[o:o + n]
    return None

syms = {}
for l in sym_path.read_text().splitlines():
    m = re.match(r'(\S+) = (\S+):0x([0-9A-F]+);.*?(?:type:(\w+))?.*?(?:size:0x([0-9A-F]+))?', l)
    m2 = re.match(r'(\S+) = (\S+):0x([0-9A-F]+); // (.*)', l)
    if not m2: continue
    name, sec, a, attrs = m2[1], m2[2], int(m2[3], 16), m2[4]
    t = re.search(r'type:(\w+)', attrs); s = re.search(r'size:0x([0-9A-F]+)', attrs)
    dk = re.search(r'data:(\w+)', attrs)
    syms[name] = dict(addr=a, sec=sec, type=t[1] if t else None, size=int(s[1], 16) if s else 0,
                      data=dk[1] if dk else None)

def cstring_at(a):
    b = rb(a, 96)
    if not b or b'\0' not in b: return None
    t = b.split(b'\0')[0]
    if len(t) < 3 or not all(32 <= c < 127 or c in (9, 10) for c in t): return None
    return t.decode('ascii')

def string_of(name):
    s = syms.get(name)
    if not s or s['data'] not in ('string', 'string_table'): return None
    b = rb(s['addr'], s['size'] or 64)
    if not b: return None
    t = b.split(b'\0')[0]
    try: return t.decode('ascii')
    except UnicodeDecodeError: return None

funcs = {}
cur = None
cur_obj = None
data_ptrs = defaultdict(list)   # fn -> [objects that point to it]
ref_re = re.compile(r'\b((?:fn|lbl|func)_[0-9A-F]{8}|[A-Za-z_][\w@$]*)(?:\+0x[0-9A-F]+)?@(?:ha|l|sda21|h)')
for f in sorted(asm_dir.glob('*.s')):
    for line in f.read_text(errors='replace').splitlines():
        if line.startswith('.fn '):
            n = line.split()[1].rstrip(',')
            cur = funcs.setdefault(n, dict(addr=syms.get(n, {}).get('addr'), size=syms.get(n, {}).get('size'),
                                           calls=[], strings=[], refs=[]))
            cur_obj = None
            regs = {}
        elif line.startswith('.endfn'):
            cur = None
        elif line.startswith('.obj ') or line.startswith('.sym '):
            cur_obj = line.split()[1].rstrip(',')
        elif line.startswith('.endobj'):
            cur_obj = None
        elif cur is not None and '*/\t' in line:
            ins = line.split('*/\t', 1)[1]
            m = re.match(r'(bl|b)\s+(\S+)', ins)
            if m and m[1] == 'bl':
                cur['calls'].append(m[2])
            # seguimiento simple de registros base (lis/addi LABEL@l, addi rD, rS, imm)
            m = re.match(r'(addi|li)\s+(r\d+),\s*(?:(r\d+),\s*)?(\S+?)(?:@(l|sda21))?$', ins.strip())
            if m:
                op, rd, rs, val, rel = m.groups()
                a = None
                if rel and re.match(r'(lbl|fn)_[0-9A-F]{8}$', val.split('+')[0]):
                    base = int(val.split('+')[0][-8:], 16)
                    add = int(val.split('+')[1], 16) if '+' in val else 0
                    a = base + add
                elif op == 'addi' and rs in regs and re.match(r'-?0x[0-9a-fA-F]+$', val):
                    a = regs[rs] + int(val, 16)
                if a is not None:
                    regs[rd] = a
                    t = cstring_at(a)
                    if t: cur['strings'].append(t)
                elif rd in regs:
                    del regs[rd]
            for r in ref_re.findall(ins):
                s = string_of(r)
                if s is not None: cur['strings'].append(s)
                elif r.startswith(('lbl_', 'fn_')) or r in syms: cur['refs'].append(r)
        elif cur_obj and '.4byte' in line:
            for t in re.findall(r'\b(fn_[0-9A-F]{8}|[A-Za-z_]\w*)\b', line.split('.4byte', 1)[1]):
                if t in syms and syms[t]['type'] == 'function':
                    data_ptrs[t].append(cur_obj)

callers = defaultdict(set)
for n, f in funcs.items():
    for c in f['calls']: callers[c].add(n)
for n, f in funcs.items():
    f['callers'] = sorted(callers.get(n, ()))
    f['calls'] = sorted(set(f['calls']))
    f['strings'] = list(dict.fromkeys(f['strings']))
    f['refs'] = sorted(set(f['refs']))
    f['in_data'] = sorted(set(data_ptrs.get(n, ())))
json.dump(funcs, open(out_path, 'w'), indent=0)
print(len(funcs), 'funciones;', sum(1 for f in funcs.values() if f['strings']), 'usan textos;',
      sum(1 for f in funcs.values() if f['in_data']), 'aparecen en tablas de datos')
