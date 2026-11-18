#!/usr/bin/env python3
"""
Renombra símbolos en config/GSWE64/symbols.txt a partir del análisis.
Solo renombra símbolos auto-generados (fn_/lbl_); nunca toca nombres ya puestos a mano.

    python tools/analysis/apply_names.py classes.json xref.json config/GSWE64/symbols.txt [--dry-run]

Reglas (de más a menos confiable):
  1. RTTI / vtables:   __RTTI__<mangled>, __vt__<mangled>          (convención CodeWarrior)
  2. Métodos virtuales: <Clase>_vfuncNN  (NN = posición en la vtable; la clase es la más base
                        en cuyas vtables aparece la función)
  3. Mensajes "nombre(): ..." / "nombre: ..." dentro de la función → ese nombre.
"""
import json, re, sys
from collections import defaultdict

classes = json.load(open(sys.argv[1]))
xref = json.load(open(sys.argv[2]))
sym_path = sys.argv[3]
dry = '--dry-run' in sys.argv

def mangle(name):
    parts = name.split('::')
    if len(parts) == 1:
        return f'{len(name)}{name}'
    return f'Q{len(parts)}' + ''.join(f'{len(p)}{p}' for p in parts)

def cident(name):
    return re.sub(r'\W', '_', name.replace('::', '_'))

AUTO = re.compile(r'^(fn|lbl)_[0-9A-F]{8}$')
names = {}          # addr -> nuevo nombre
reason = {}

def propose(addr, new, why):
    if addr in names: return
    names[addr] = new; reason[addr] = why

# --- 1. RTTI y vtables
for cname, c in classes.items():
    propose(c['rtti'], f'__RTTI__{mangle(cname)}', 'rtti')
    for v in c['vtables']:
        suffix = '' if v['this_offset'] == 0 else f'_{v["this_offset"]:X}'
        propose(v['addr'], f'__vt__{mangle(cname)}{suffix}', 'vtable')

# --- 2. métodos virtuales
ancestors = {n: set(c['bases']) for n, c in classes.items()}
def is_ancestor(a, b):  # a es ancestro de b (o igual)
    return a == b or a in ancestors.get(b, ())
appear = defaultdict(list)   # func addr -> [(clase, slot)]
for cname, c in classes.items():
    for v in c['vtables']:
        if v['this_offset'] != 0: continue
        for i, f in enumerate(v['funcs_addr']):
            if f: appear[f].append((cname, i))
for f, lst in appear.items():
    owners = [c for c, _ in lst if all(is_ancestor(c, o) for o, _ in lst)]
    slots = {s for _, s in lst}
    if len(owners) >= 1 and len(slots) == 1:
        owner = owners[0]
        propose(f, f'{cident(owner)}_vfunc{slots.pop():02d}', 'vfunc')

# --- 3. nombres en mensajes: "nombre(): ...", "nombre() - ...", "Clase::metodo() : ..."
# Se vota por función (el primer texto de un bloque puede venir de un registro base compartido).
MSG = re.compile(r'^\s*\**\s*([A-Za-z_][A-Za-z0-9_:]{3,})\s*\(\)\s*[:\-]')
by_name = defaultdict(list)
for fname, f in xref.items():
    if not AUTO.match(fname) or f['addr'] is None: continue
    votes = defaultdict(int)
    for s in f['strings']:
        m = MSG.match(s)
        if m: votes[m[1]] += 1
    if not votes: continue
    best = sorted(votes.items(), key=lambda kv: -kv[1])
    if len(best) > 1 and best[0][1] == best[1][1]: continue
    by_name[cident(best[0][0])].append(f['addr'])
for n, addrs in by_name.items():
    if len(addrs) == 1:
        propose(addrs[0], n, 'mensaje')

# --- aplicar
lines = open(sym_path).read().splitlines()
taken = set()
for l in lines:
    m = re.match(r'(\S+) = ', l)
    if m and not AUTO.match(m[1]): taken.add(m[1])
out, count = [], defaultdict(int)
for l in lines:
    m = re.match(r'(\S+) = (\S+):0x([0-9A-F]+);(.*)', l)
    if m and AUTO.match(m[1]):
        a = int(m[3], 16)
        new = names.get(a)
        if new and new not in taken:
            taken.add(new); count[reason[a]] += 1
            l = f'{new} = {m[2]}:0x{m[3]};{m[4]}'
    out.append(l)
print('renombrados:', dict(count), 'total', sum(count.values()))
if not dry:
    open(sym_path, 'w').write('\n'.join(out) + '\n')
