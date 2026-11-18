#!/usr/bin/env python3
"""C legible (aproximado) de funciones del juego con el decompilador de Ghidra.

Usa solo el decompilador de linea de comandos de Ghidra (decomp_opt) y el modulo del
procesador Gekko/Broadway; se arman con tools/ghidra/armar.sh (no van en el repo).

  python3 tools/ghidra/decompilar.py PLAYERcockpit_vfunc24 0x7FCC3D88 ...
  python3 tools/ghidra/decompilar.py --desensamblar fn_7FC2E760

Les pone nombre a todas las funciones y etiquetas de config/GSWE64/symbols.txt y fija r2 y
r13 (los punteros a los datos chicos) como en __init_registers.
"""
import os, re, struct, subprocess, sys, tempfile

RAIZ = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
GH = os.environ.get('RL_GHIDRA', os.path.join(RAIZ, 'build', 'ghidra'))
DECOMP = os.path.join(GH, 'ghidra', 'Ghidra', 'Features', 'Decompiler', 'src', 'decompile', 'cpp', 'decomp_opt')
DOL = os.path.join(RAIZ, 'orig', 'GSWE64', 'combined.dol')
SIMBOLOS = os.path.join(RAIZ, 'config', 'GSWE64', 'symbols.txt')


def imagen():
    """La memoria del juego (secciones de combined.dol) como un archivo plano."""
    d = open(DOL, 'rb').read()
    off = struct.unpack('>18I', d[0:72]); adr = struct.unpack('>18I', d[72:144]); tam = struct.unpack('>18I', d[144:216])
    secs = [(adr[i], off[i], tam[i]) for i in range(18) if tam[i]]
    lo = min(a for a, o, s in secs); hi = max(a + s for a, o, s in secs)
    img = bytearray(hi - lo)
    for a, o, s in secs:
        img[a - lo:a - lo + s] = d[o:o + s]
    ruta = os.path.join(GH, 'ram.bin')
    if not os.path.exists(ruta) or os.path.getsize(ruta) != len(img):
        open(ruta, 'wb').write(img)
    return ruta, lo


def simbolos():
    fn, todos = {}, {}
    for l in open(SIMBOLOS):
        m = re.match(r'(\S+) = \S+:0x([0-9A-Fa-f]+); // type:(\w+)', l)
        if not m: continue
        nombre, a, tipo = m.group(1), int(m.group(2), 16), m.group(3)
        nombre = re.sub(r'[^A-Za-z0-9_]', '_', nombre)
        todos[nombre] = a
        if tipo == 'function': fn[a] = nombre
    return fn, todos


def main():
    args = sys.argv[1:]
    dis = '--desensamblar' in args
    args = [a for a in args if a != '--desensamblar']
    if not args: sys.exit(__doc__)
    if not os.path.exists(DECOMP): sys.exit('falta %s: correr tools/ghidra/armar.sh' % DECOMP)
    ram, base = imagen()
    fn, todos = simbolos()
    cmds = ['load file PowerPC:BE:32:Gekko_Broadway:default %s' % ram, 'adjust vma 0x%x' % base,
            'set track r2 0x8009ea20', 'set track r13 0x8009ada0']
    for a, n in sorted(fn.items()):
        cmds.append('map function 0x%x %s' % (a, n))
    for a in args:
        ad = int(a, 16) if a.lower().startswith('0x') else todos.get(a)
        if ad is None: sys.exit('no conozco %s' % a)
        cmds.append('load function %s' % fn[ad] if ad in fn else 'load addr 0x%x' % ad)
        if dis:
            cmds.append('disassemble 0x%x 0x%x' % (ad, ad + 0x400))
        else:
            cmds += ['decompile', 'print C']
    cmds.append('quit')
    env = dict(os.environ, SLEIGHHOME=os.path.join(GH, 'ghidra'))
    r = subprocess.run([DECOMP], input='\n'.join(cmds) + '\n', capture_output=True, text=True, env=env)
    # solo lo que interesa: el C (o el desensamblado), sin el eco de los comandos de mapeo
    salida = [l for l in r.stdout.split('\n') if not l.startswith('[decomp]> map ') and 'Function ' not in l]
    print('\n'.join(salida))


if __name__ == '__main__':
    main()
