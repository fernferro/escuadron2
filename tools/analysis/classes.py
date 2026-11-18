#!/usr/bin/env python3
"""
Reconstruye las clases C++ del juego a partir de las RTTI de CodeWarrior.

    python tools/analysis/classes.py orig/GSWE64/combined.dol config/GSWE64/symbols.txt build/GSWE64/asm out.json

Formato CodeWarrior (GC):
  typeinfo  = { const char* name; BaseEntry* bases }          (8 bytes)
  BaseEntry = { typeinfo* base; int offset } ... terminada en 0
  vtable    = { typeinfo* rtti; int this_offset; void* vfuncs[] }
"""
import json, re, struct, sys
from collections import defaultdict
from pathlib import Path

dol_path, sym_path, asm_dir, out_path = map(Path, sys.argv[1:5])
d = dol_path.read_bytes()
off = struct.unpack('>18I', d[0:72]); addr = struct.unpack('>18I', d[72:144]); size = struct.unpack('>18I', d[144:216])
SECS = [(addr[i], size[i], off[i], i < 7) for i in range(18) if size[i]]
def rb(a, n):
    for s, sz, o, _ in SECS:
        if s <= a and a + n <= s + sz: return d[o + a - s:o + a - s + n]
    return None
def u32(a):
    b = rb(a, 4); return struct.unpack('>I', b)[0] if b else None
def is_code(a):
    return any(s <= a < s + sz and t for s, sz, o, t in SECS)
def cstr(a):
    b = rb(a, 128)
    if not b or b'\0' not in b: return None
    t = b.split(b'\0')[0]
    if len(t) < 2 or not all(32 < c < 127 for c in t): return None
    return t.decode()

sym_at, sym_size = {}, {}
for l in sym_path.read_text().splitlines():
    m = re.match(r'(\S+) = (\S+):0x([0-9A-F]+); // (.*)', l)
    if m:
        a = int(m[3], 16); sym_at[a] = m[1]
        s = re.search(r'size:0x([0-9A-F]+)', m[4]); sym_size[a] = int(s[1], 16) if s else 0

IDENT = re.compile(r'^[A-Za-z_][\w:<>,\* ]*$')
# 1) typeinfos: objetos de 8 bytes {nombre, bases|0}
typeinfo = {}
for a, n in sym_at.items():
    if sym_size.get(a) not in (8, 0xC) or is_code(a): continue
    p_name, p_bases = u32(a), u32(a + 4)
    name = cstr(p_name) if p_name else None
    if not name or not IDENT.match(name): continue
    if p_bases and rb(p_bases, 4) is None: continue
    typeinfo[a] = dict(name=name, bases_ptr=p_bases or 0)
# bases
for a, t in typeinfo.items():
    bases, p = [], t['bases_ptr']
    while p:
        b = u32(p)
        if not b or b not in typeinfo: break
        bases.append(dict(rtti=b, offset=u32(p + 4))); p += 8
    t['bases'] = bases

# 2) vtables: {rtti, offset, fn...}
# Las listas de bases tienen la misma forma {rtti, offset}: excluirlas.
in_base_list = set()
for t in typeinfo.values():
    p = t['bases_ptr']
    for _ in t['bases']:
        in_base_list.add(p); p += 8
    if t['bases_ptr']: in_base_list.add(p)
vtables = {}
for a, n in sym_at.items():
    if is_code(a) or a in in_base_list: continue
    r = u32(a)
    if r not in typeinfo: continue
    o = u32(a + 4)
    if o is None or o > 0x10000 and o < 0xFFFF0000: continue
    fns, p = [], a + 8
    end = a + (sym_size.get(a) or 0x400)
    while p < end:
        v = u32(p)
        if v is None or not (v == 0 or is_code(v)): break
        if v and v in sym_at and p != a + 8 and (p in sym_at and p != a): break
        fns.append(v); p += 4
    if not any(fns): continue
    vtables[a] = dict(rtti=r, this_offset=o if o < 0x80000000 else o - (1 << 32), funcs=fns)

# 3) qué funciones referencian cada vtable (constructores/destructores)
refs = defaultdict(set)
cur = None
for f in sorted(asm_dir.glob('*.s')):
    for line in f.read_text(errors='replace').splitlines():
        if line.startswith('.fn '): cur = line.split()[1].rstrip(',')
        elif line.startswith('.endfn'): cur = None
        elif cur and '@ha' in line or (cur and '@l' in line):
            for lab in re.findall(r'\b(lbl_[0-9A-F]{8})@', line):
                refs[int(lab[4:], 16)].add(cur)

classes = {}
for a, t in typeinfo.items():
    classes[t['name']] = dict(rtti=a, bases=[typeinfo[b['rtti']]['name'] for b in t['bases']],
                              base_offsets=[b['offset'] for b in t['bases']], vtables=[])
for a, v in vtables.items():
    cname = typeinfo[v['rtti']]['name']
    classes[cname]['vtables'].append(dict(addr=a, this_offset=v['this_offset'],
                                          funcs=[sym_at.get(f, hex(f)) for f in v['funcs']],
                                          funcs_addr=v['funcs'],
                                          referenced_by=sorted(refs.get(a, ()))))
json.dump(classes, open(out_path, 'w'), indent=1)
nv = sum(len(c['vtables']) for c in classes.values())
print(len(classes), 'clases con RTTI;', nv, 'vtables;',
      sum(1 for c in classes.values() if c['bases']), 'con clase base')
