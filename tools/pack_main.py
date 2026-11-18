#!/usr/bin/env python3
"""
Reempaqueta el ELF enlazado en un main.dol con el mismo formato que el original.

    python tools/pack_main.py build/GSWE64/combined.elf build/GSWE64/main.dol

Formato del original ("GSYS VM"):
  main.dol
    T0 .init   T1 .text   D0..D7 extab, extabindex, .ctors, .dtors, .rodata, .data, .sdata, .sdata2
    T2 = rogue2vm.dol completo, guardado en 0x80100000 (va último en el archivo)
    BSS = .bss .. fin de .sbss
  rogue2vm.dol (lo copia a la ARAM el gestor de VM del cargador)
    T0 .vmtext (0x7FC00000)   T1 .vmtext2   D0 .vmdata   BSS .vmbss   entrada = 0

Ajustes respecto de la salida directa del linker:
  - La BSS del VM la borra el propio gestor de VM, no el arranque, así que se
    quita de la tabla _bss_init_info que el linker genera en .init.
  - En el análisis, .vmbss se extiende hasta 0x80003100 (ver combine_vm.py);
    el tamaño real de la BSS del VM termina en VM_BSS_END.
Todo el relleno entre secciones es cero, alineado a 0x20.
"""
import struct
import sys
from pathlib import Path

VM_PAYLOAD_ADDR = 0x80100000
VM_BSS_END = 0x7FE1EBA0  # fin real de la BSS del VM (encabezado de rogue2vm.dol)

LOADER_TEXT = [".init", ".text"]
LOADER_DATA = ["extab", "extabindex", ".ctors", ".dtors", ".rodata", ".data", ".sdata", ".sdata2"]
VM_TEXT = [".vmtext", ".vmtext2"]
VM_DATA = [".vmdata"]

SHT_NOBITS = 8


def read_elf(path: Path):
    b = path.read_bytes()
    if b[:4] != b"\x7fELF" or b[4] != 1 or b[5] != 2:
        raise SystemExit("Se esperaba un ELF32 big-endian")
    e_entry, = struct.unpack(">I", b[0x18:0x1C])
    e_shoff, = struct.unpack(">I", b[0x20:0x24])
    e_shentsize, e_shnum, e_shstrndx = struct.unpack(">HHH", b[0x2E:0x34])
    hdrs = []
    for i in range(e_shnum):
        o = e_shoff + i * e_shentsize
        hdrs.append(struct.unpack(">10I", b[o:o + 40]))
    strtab = hdrs[e_shstrndx]
    stroff = strtab[4]

    def name(off):
        end = b.index(b"\0", stroff + off)
        return b[stroff + off:end].decode()

    secs = {}
    for h in hdrs:
        sh_name, sh_type, _flags, sh_addr, sh_offset, sh_size = h[:6]
        n = name(sh_name)
        data = None if sh_type == SHT_NOBITS else b[sh_offset:sh_offset + sh_size]
        secs[n] = (sh_addr, sh_size, data)
    return secs, e_entry


def pad32(data: bytes) -> bytes:
    return data + b"\0" * (-len(data) % 0x20)


def write_dol(entries, bss, entry, padded_sizes=True) -> bytes:
    """entries: lista de (ranura, addr, bytes) en el orden en que van en el archivo."""
    out = bytearray(0x100)
    O, A, S = [0] * 18, [0] * 18, [0] * 18
    for slot, addr, data in entries:
        O[slot], A[slot] = len(out), addr
        S[slot] = len(pad32(data)) if padded_sizes else len(data)
        out += pad32(data)
    out[0:216] = struct.pack(">54I", *(O + A + S))
    out[216:228] = struct.pack(">3I", bss[0], bss[1], entry)
    return bytes(out)


def strip_vm_bss_entries(init: bytearray, init_addr: int, bss_addr: int, bss_size: int) -> int:
    """Quita de _bss_init_info las entradas fuera de la memoria del cargador."""
    key = struct.pack(">II", bss_addr, bss_size)
    pos = init.find(key)
    if pos < 0 or pos % 4:
        raise SystemExit("No se encontró _bss_init_info en .init")
    entries, p = [], pos
    while True:
        a, s = struct.unpack(">II", init[p:p + 8])
        p += 8
        if s == 0:
            break
        entries.append((a, s))
    keep = [(a, s) for a, s in entries if a >= init_addr]
    removed = len(entries) - len(keep)
    table = b"".join(struct.pack(">II", a, s) for a, s in keep)
    table += b"\0" * (p - pos - len(table))  # terminador + ceros donde estaban las quitadas
    init[pos:p] = table
    return removed


def pack(secs, entry):
    """secs: {nombre: (addr, size, bytes|None)}. Devuelve (main_dol, vm_dol, quitadas)."""
    missing = [n for n in LOADER_TEXT + LOADER_DATA + VM_TEXT + VM_DATA + [".bss", ".sbss", ".vmbss"]
               if n not in secs]
    if missing:
        raise SystemExit(f"Faltan secciones en el ELF: {missing}")

    # --- rogue2vm.dol ---
    vm_bss_addr = secs[".vmbss"][0]
    vm_entries = [(i, secs[n][0], secs[n][2]) for i, n in enumerate(VM_TEXT)]
    vm_entries += [(7 + i, secs[n][0], secs[n][2]) for i, n in enumerate(VM_DATA)]
    # rogue2vm.dol guarda los tamaños reales (sin redondear a 0x20) en su encabezado
    vm_dol = write_dol(vm_entries, (vm_bss_addr, VM_BSS_END - vm_bss_addr), 0, padded_sizes=False)

    # --- main.dol ---
    init_addr, _, init_data = secs[".init"]
    init = bytearray(init_data)
    bss_addr, bss_size, _ = secs[".bss"]
    removed = strip_vm_bss_entries(init, init_addr, bss_addr, bss_size)

    entries = [(0, init_addr, bytes(init)), (1, secs[".text"][0], secs[".text"][2])]
    entries += [(7 + i, secs[n][0], secs[n][2]) for i, n in enumerate(LOADER_DATA)]
    entries.append((2, VM_PAYLOAD_ADDR, vm_dol))
    sbss_addr, sbss_size, _ = secs[".sbss"]
    main_dol = write_dol(entries, (bss_addr, sbss_addr + sbss_size - bss_addr), entry)

    return main_dol, vm_dol, removed


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    secs, entry = read_elf(Path(sys.argv[1]))
    main_dol, vm_dol, removed = pack(secs, entry)
    Path(sys.argv[2]).write_bytes(main_dol)
    print(f"main.dol escrito ({len(main_dol):#x} bytes); rogue2vm.dol {len(vm_dol):#x} bytes; "
          f"{removed} entrada(s) de BSS del VM quitadas de _bss_init_info")
    return 0


if __name__ == "__main__":
    sys.exit(main())
