#!/usr/bin/env python3
"""Resume build/prof.raw (ver port/runtime/prof.c): funciones con mas muestras, por hilo."""
import subprocess, sys, collections
exe, raw = sys.argv[1], sys.argv[2]
top = int(sys.argv[3]) if len(sys.argv) > 3 else 25
lines = open(raw).read().split('\n')
main_rt = int(lines[0].split()[1], 16)
nm = subprocess.run(['nm', exe], capture_output=True, text=True).stdout
main_off = next(int(l.split()[0], 16) for l in nm.split('\n') if l.endswith(' T main'))
base = main_rt - main_off
import bisect, os
maps = []
for l in lines[1:]:
    if l.startswith('map '):
        f = l.split()
        lo, hi = (int(x, 16) for x in f[1].split('-'))
        maps.append((lo, hi, int(f[3], 16), f[6] if len(f) > 6 else '?'))
symcache = {}
def lib_syms(path):
    if path not in symcache:
        syms = []
        if os.path.exists(path):
            out = subprocess.run(['nm', '-D', '--defined-only', path], capture_output=True, text=True).stdout
            for l in out.split('\n'):
                f = l.split()
                if len(f) >= 3 and f[1] in 'TtWi':
                    syms.append((int(f[0], 16), f[2]))
        syms.sort()
        symcache[path] = syms
    return symcache[path]
def lib_of(a):
    # el primer mapeo del archivo da la base de carga
    for lo, hi, off, n in maps:
        if lo <= a < hi:
            base_lo = min(m[0] - m[2] for m in maps if m[3] == n)
            rel = a - base_lo
            syms = lib_syms(n)
            i = bisect.bisect_right([x[0] for x in syms], rel) - 1
            nm = syms[i][1] if i >= 0 else '?'
            return n.split('/')[-1] + ':' + nm
    return '?'
samples = []
callers = []
for l in lines[1:]:
    if not l or l.startswith('map '):
        continue
    f = l.split()
    samples.append((int(f[0]), int(f[1], 16) - base))
    callers.append([int(x, 16) - base for x in f[2:5]] if len(f) >= 5 else [])
addrs = sorted(set(p for _, p in samples) | set(c for cs in callers for c in cs if c > 0))
out = subprocess.run(['addr2line', '-f', '-e', exe] + ['%x' % a for a in addrs], capture_output=True, text=True).stdout.split('\n')
name = {a: out[2 * i] for i, a in enumerate(addrs)}
for a in addrs:
    if name[a] == '??':
        name[a] = '[' + lib_of(a + base) + ']'
by_tid = collections.defaultdict(collections.Counter)
via = collections.defaultdict(collections.Counter)   # (hilo, funcion) -> cadena de llamadores
for (t, p), cs in zip(samples, callers):
    by_tid[t][name[p]] += 1
    if cs:
        via[(t, name[p])][' <- '.join(name.get(c, '?') for c in cs if c > 0)] += 1
total = len(samples)
for t, c in sorted(by_tid.items(), key=lambda kv: -sum(kv[1].values())):
    n = sum(c.values())
    print('== hilo %d: %d muestras (%.0f%%)' % (t, n, 100.0 * n / total))
    for f, k in c.most_common(top):
        print('  %6.1f%%  %s' % (100.0 * k / n, f))
        for chain, kk in via[(t, f)].most_common(2):
            print('            %4.0f%% <- %s' % (100.0 * kk / k, chain))
