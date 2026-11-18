#!/usr/bin/env python3
"""
Escuadron2: main.dol es un cargador que lleva embebido rogue2vm.dol
(sección de texto en 0x80100000). En tiempo de ejecución, GSYS VM copia ese
DOL a la ARAM y lo mapea en 0x7FC00000-0x7FE1EBA0.

Ambos fueron enlazados como UN solo programa (los .ctors y las vtables del
cargador apuntan a funciones en 0x7FC...), así que para analizarlo se arma
una imagen combinada con cada sección en su dirección real:

    python tools/combine_vm.py orig/GSWE64/sys/main.dol orig/GSWE64/combined.dol

La BSS del VM se agrega como sección de datos llena de ceros.
Esta imagen es solo para análisis; no es ejecutable.
"""
import struct
import sys

VM_PAYLOAD_ADDR = 0x80100000


def parse(b: bytes):
    off = struct.unpack(">18I", b[0:72])
    addr = struct.unpack(">18I", b[72:144])
    size = struct.unpack(">18I", b[144:216])
    bss_a, bss_s, entry = struct.unpack(">3I", b[216:228])
    texts = [(addr[i], b[off[i]:off[i] + size[i]]) for i in range(7) if size[i]]
    datas = [(addr[i], b[off[i]:off[i] + size[i]]) for i in range(7, 18) if size[i]]
    return texts, datas, bss_a, bss_s, entry


def main() -> None:
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    m = open(sys.argv[1], "rb").read()
    texts, datas, bss_a, bss_s, entry = parse(m)
    payload = [t for t in texts if t[0] == VM_PAYLOAD_ADDR]
    if not payload:
        sys.exit("No se encontró el DOL embebido en 0x80100000")
    texts = [t for t in texts if t[0] != VM_PAYLOAD_ADDR]
    vt, vd, vba, vbs, _ = parse(payload[0][1])
    # La BSS del VM (0x7FDE8000-0x7FE1EBA0) se agrega como sección de ceros y se
    # extiende hasta la primera sección del cargador (0x80003100): dtk ordena las
    # secciones por dirección y su cálculo de alineación entra en un bucle
    # infinito si una sección termina por debajo de 0x80000000 y la siguiente
    # empieza por encima. Los tipos y nombres reales se fijan en splits.txt.
    first_loader = min(a for a, _ in texts + datas)
    texts += vt
    datas += vd + [(vba, b"\0" * (first_loader - vba))]
    if len(texts) > 7 or len(datas) > 11:
        sys.exit("Demasiadas secciones para un DOL")

    out = bytearray(0x100)
    O, A, S = [0] * 18, [0] * 18, [0] * 18
    for k, (a, b) in enumerate(texts):
        O[k], A[k], S[k] = len(out), a, len(b)
        out += b
        while len(out) % 32:
            out += b"\0"
    for k, (a, b) in enumerate(datas):
        O[7 + k], A[7 + k], S[7 + k] = len(out), a, len(b)
        out += b
        while len(out) % 32:
            out += b"\0"
    out[0:216] = struct.pack(">54I", *(O + A + S))
    out[216:228] = struct.pack(">3I", bss_a, bss_s, entry)
    open(sys.argv[2], "wb").write(out)
    for i in range(18):
        if S[i]:
            kind = f"T{i}" if i < 7 else f"D{i - 7}"
            print(f"{kind}  0x{A[i]:08X}-0x{A[i] + S[i]:08X}  ({S[i]:#x} bytes)")


if __name__ == "__main__":
    main()
