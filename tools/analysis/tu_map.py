#!/usr/bin/env python3
"""
Estima los límites de los archivos fuente originales (TUs) del código del juego.

    python tools/analysis/tu_map.py [salida.json]

Idea: cada archivo .c tiene su propio bloque de constantes en .sdata2, y CodeWarrior rellena
el final de cada bloque con '0000' (0x30303030). Se parte .sdata2 en bloques por esos rellenos;
cada función de .vmtext/.vmtext2 se etiqueta con los bloques que usa, y las funciones contiguas
con el mismo bloque se agrupan. Las funciones sin constantes heredan el grupo vecino.
"""
import json, re, struct, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
d = (ROOT / 'orig/GSWE64/combined.dol').read_bytes()
off = struct.unpack('>18I', d[0:72]); addr = struct.unpack('>18I', d[72:144]); size = struct.unpack('>18I', d[144:216])
def rb(a, n):
    for i in range(18):
        if size[i] and addr[i] <= a < addr[i] + size[i]:
            o = off[i] + a - addr[i]; return d[o:o + n]

SD2 = (0x80096A20, 0x8009C1E0)
markers = [a for a in range(SD2[0], SD2[1], 4) if rb(a, 4) == b'0000']
bounds = [SD2[0]] + [m + 4 for m in markers] + [SD2[1]]
def block_of(a):
    for i in range(len(bounds) - 1):
        if bounds[i] <= a < bounds[i + 1]: return i
    return None

funcs = []
for l in (ROOT / 'config/GSWE64/symbols.txt').read_text().splitlines():
    m = re.match(r'(\S+) = (\.vmtext2?):0x([0-9A-F]+);.*type:function size:0x([0-9A-F]+)', l)
    if m: funcs.append([int(m[3], 16), int(m[4], 16), m[1], m[2], set()])
funcs.sort()
byname = {f[2]: f for f in funcs}
cur = None
for f in sorted((ROOT / 'build/GSWE64/asm').glob('auto_*vmtext*.s')):
    for line in f.read_text(errors='replace').splitlines():
        if line.startswith('.fn '): cur = byname.get(line.split()[1].rstrip(','))
        elif line.startswith('.endfn'): cur = None
        elif cur is not None:
            for lab in re.findall(r'\blbl_(8009[6-9A-C][0-9A-F]{3})@sda21', line):
                b = block_of(int(lab, 16))
                if b is not None: cur[4].add(b)
# agrupar
groups = []
for a, s, n, sec, blocks in funcs:
    key = min(blocks) if blocks else None
    if groups and groups[-1]['section'] == sec and (key is None or groups[-1]['block'] in (None, key)
                                                    or key in groups[-1]['blocks']):
        g = groups[-1]
        if g['block'] is None: g['block'] = key
    else:
        g = dict(section=sec, start=a, block=key, blocks=set(), funcs=[])
        groups.append(g)
    g['funcs'].append(n); g['blocks'] |= blocks; g['end'] = a + s
out = []
for g in groups:
    out.append(dict(section=g['section'], start=f"0x{g['start']:08X}", end=f"0x{g['end']:08X}",
                    size=g['end'] - g['start'], functions=len(g['funcs']), first=g['funcs'][0],
                    sdata2_blocks=sorted(f"0x{bounds[b]:08X}" for b in g['blocks'])))
dst = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / 'build/tu_map.json'
dst.write_text(json.dumps(out, indent=1))
print(len(markers), 'rellenos en .sdata2;', len(out), 'grupos (archivos estimados)')
