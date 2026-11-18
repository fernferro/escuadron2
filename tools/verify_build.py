#!/usr/bin/env python3
"""
Verifica el DOL enlazado comparando CONTENIDO por dirección contra el main.dol original.

    python tools/verify_build.py orig/GSWE64/sys/main.dol build/GSWE64/combined.dol [-o build/GSWE64/ok]

Por qué no un hash: el original es un cargador con rogue2vm.dol embebido, y lo
que enlazamos es una imagen combinada con todo en sus direcciones reales. El
encabezado del DOL (orden de secciones, rango de BSS, relleno) nunca va a ser
idéntico, pero cada byte de código y datos sí tiene que serlo.

Reglas:
  - Cada sección de código/datos del original (cargador + VM) debe existir en el
    DOL enlazado en la misma dirección y con los mismos bytes.
  - Bytes extra al final de una sección enlazada están permitidos solo si son 0.
  - Las BSS del original (cargador y VM) deben quedar cubiertas por BSS o ceros.
"""
import struct
import sys
from pathlib import Path

VM_PAYLOAD_ADDR = 0x80100000


def parse(b: bytes):
    off = struct.unpack(">18I", b[0:72])
    addr = struct.unpack(">18I", b[72:144])
    size = struct.unpack(">18I", b[144:216])
    bss_a, bss_s, entry = struct.unpack(">3I", b[216:228])
    secs = []
    for i in range(18):
        if size[i]:
            name = f"T{i}" if i < 7 else f"D{i - 7}"
            secs.append((name, addr[i], b[off[i]:off[i] + size[i]]))
    return secs, (bss_a, bss_s), entry


def reference(main_dol: bytes):
    """Secciones reales (nombre, dirección, bytes) y rangos BSS del juego."""
    secs, bss, entry = parse(main_dol)
    out, bsss = [], [bss]
    for name, a, d in secs:
        if a == VM_PAYLOAD_ADDR:
            vsecs, vbss, _ = parse(d)
            out += [(f"VM:{n}", va, vd) for n, va, vd in vsecs]
            bsss.append(vbss)
        else:
            out.append((f"loader:{name}", a, d))
    return out, bsss, entry


class Memory:
    def __init__(self, secs, bss):
        self.secs = secs
        self.bss = bss

    def read(self, addr, size):
        for _, a, d in self.secs:
            if a <= addr and addr + size <= a + len(d):
                return d[addr - a:addr - a + size]
        return None

    extra_bss = ()

    def zero_or_bss(self, addr, size):
        for ba, bs in (self.bss, *self.extra_bss):
            if ba <= addr and addr + size <= ba + bs:
                return True
        data = self.read(addr, size)
        return data is not None and not any(data)


def main() -> int:
    args = sys.argv[1:]
    ok_path = None
    if "-o" in args:
        i = args.index("-o")
        ok_path = Path(args[i + 1])
        del args[i:i + 2]
    if len(args) != 2:
        print(__doc__)
        return 2
    ref_secs, ref_bss, ref_entry = reference(Path(args[0]).read_bytes())
    built_raw = Path(args[1]).read_bytes()
    built_secs, built_bss, built_entry = parse(built_raw)
    if any(a == VM_PAYLOAD_ADDR for _, a, _ in built_secs):
        # main.dol reempaquetado: expandir el rogue2vm.dol embebido
        built_secs, built_bsss, built_entry = reference(built_raw)
        built_bss = built_bsss[0]
        mem = Memory(built_secs, built_bss)
        mem.extra_bss = built_bsss[1:]
    else:
        mem = Memory(built_secs, built_bss)

    errors = 0
    total_words = bad_words = 0
    for name, a, d in ref_secs:
        got = mem.read(a, len(d))
        if got is None:
            print(f"FALTA   {name:10} 0x{a:08X} (+0x{len(d):X}): no está en el DOL enlazado")
            errors += 1
            continue
        diffs = [i for i in range(0, len(d), 4) if d[i:i + 4] != got[i:i + 4]]
        total_words += len(d) // 4
        bad_words += len(diffs)
        if diffs:
            first = ", ".join(f"0x{a + i:08X}" for i in diffs[:5])
            print(f"DIFIERE {name:10} 0x{a:08X} (+0x{len(d):X}): {len(diffs)} palabras ({first}...)")
            errors += 1
        else:
            print(f"OK      {name:10} 0x{a:08X} (+0x{len(d):X})")
    for ba, bs in ref_bss:
        if bs and not mem.zero_or_bss(ba, bs):
            print(f"BSS     0x{ba:08X} (+0x{bs:X}) no queda cubierta por BSS/ceros")
            errors += 1
    if built_entry != ref_entry:
        print(f"ENTRADA 0x{built_entry:08X} != 0x{ref_entry:08X}")
        errors += 1

    if errors:
        pct = 100.0 * (total_words - bad_words) / max(total_words, 1)
        print(f"\nNO COINCIDE: {errors} problema(s); {pct:.2f}% de las palabras iguales.")
        return 1
    print("\nCOINCIDE: todo el código y los datos son idénticos al original.")
    if ok_path:
        ok_path.write_text("ok\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
