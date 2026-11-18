#!/usr/bin/env python3
"""Convierte una captura del port (cuadro_N.gxc, de --capturar o F12) en un archivo DFF
del "FIFO player" de Dolphin, para dibujar el mismo cuadro con el renderizador por
software de Dolphin y comparar (solo para depurar; el port no usa Dolphin).

uso: python3 port/tools/gxc_a_dff.py cuadro_N.gxc salida.dff
"""
import struct, sys

MEM_BASE, MEM_SIZE = 0x7F000000, 0x02800000
FIFO_START, FIFO_END = 0x001FAB00, 0x0020AAE0   # la FIFO del juego (registro [pi])

def main():
    src, dst = sys.argv[1], sys.argv[2]
    d = open(src, 'rb').read()
    magic, xfb, gsz, clen = struct.unpack_from('<4I', d, 0)
    assert magic == 0x47584331, 'no es una captura'
    o = 16
    g = d[o:o + gsz]; o += gsz
    tmem = d[o:o + (1 << 20)]; o += 1 << 20
    ram = d[o:o + MEM_SIZE]; o += MEM_SIZE
    cmds = d[o:o + clen]
    u = lambda off: struct.unpack_from('<I', g, off)[0]
    vcd_lo, vcd_hi = u(0), u(4)
    vat_a = [u(8 + 4 * i) for i in range(8)]
    vat_b = [u(40 + 4 * i) for i in range(8)]
    vat_c = [u(72 + 4 * i) for i in range(8)]
    base = [u(104 + 4 * i) for i in range(16)]
    stride = [u(168 + 4 * i) for i in range(16)]
    mia, mib = u(232), u(236)
    xf_mem = [u(240 + 4 * i) for i in range(0x800)]
    xf_regs = [u(240 + 0x2000 + 4 * i) for i in range(0x100)]
    bp = [u(240 + 0x2400 + 4 * i) for i in range(0x100)]
    tevr = [[u(240 + 0x2800 + 4 + 8 * r + 4 * k) for k in range(2)] for r in range(4)]
    tevk = [[u(240 + 0x2800 + 4 + 32 + 8 * r + 4 * k) for k in range(2)] for r in range(4)]

    cp = [0] * 256
    cp[0x30], cp[0x40], cp[0x50], cp[0x60] = mia, mib, vcd_lo, vcd_hi
    for i in range(8):
        cp[0x70 + i], cp[0x80 + i], cp[0x90 + i] = vat_a[i], vat_b[i], vat_c[i]
    for i in range(16):
        cp[0xA0 + i], cp[0xB0 + i] = base[i], stride[i]
    xfm = xf_mem + [0] * (4096 - 0x800)
    xfr = xf_regs[:88]

    # Los registros de color del TEV y las constantes comparten direcciones de BP: se
    # cargan los dos al principio del cuadro (constantes con el bit 23).
    pre = b''
    def bpw(reg, val):
        return bytes([0x61]) + struct.pack('>I', (reg << 24) | (val & 0xFFFFFF))
    for r in range(4):
        pre += bpw(0xE0 + 2 * r, tevk[r][0] | (1 << 23)) + bpw(0xE1 + 2 * r, tevk[r][1] | (1 << 23))
        pre += bpw(0xE0 + 2 * r, tevr[r][0] & ~(1 << 23)) + bpw(0xE1 + 2 * r, tevr[r][1] & ~(1 << 23))
        pre += bpw(0xE1 + 2 * r, tevr[r][1] & ~(1 << 23)) + bpw(0xE1 + 2 * r, tevr[r][1] & ~(1 << 23))
    fifo = pre + cmds

    phys = ram[0x01000000:0x01000000 + 0x01800000]
    hdr_size, fl_size = 128, 64
    off = hdr_size + fl_size
    out = bytearray(off)
    bp_off = len(out); out += struct.pack('<256I', *[v & 0xFFFFFFFF for v in bp])
    cp_off = len(out); out += struct.pack('<256I', *cp)
    xm_off = len(out); out += struct.pack('<4096I', *xfm)
    xr_off = len(out); out += struct.pack('<88I', *xfr)
    tm_off = len(out); out += tmem
    fd_off = len(out); out += fifo
    mu_off = len(out); out += bytes(24)
    md_off = len(out); out += phys
    struct.pack_into('<IIQIB3x', out, mu_off, 0, 0, md_off, len(phys), 1)  # fifoPosition, address, dataOffset, size, type
    struct.pack_into('<QIIIQI', out, hdr_size, fd_off, len(fifo), FIFO_START, FIFO_END, mu_off, 1)
    hdr = struct.pack('<IIIQIQIQIQIQIIQIII8s24s', 0x0d01f1f0, 6, 1, bp_off, 256, cp_off, 256, xm_off, 4096,
                      xr_off, 88, hdr_size, 1, 0, tm_off, 1 << 20, 0x01800000, 0x04000000, b'GSWE64\0\0', bytes(24))
    out[0:128] = hdr
    open(dst, 'wb').write(out)
    print('%s: %d bytes de comandos, xfb %08X' % (dst, len(fifo), xfb))

main()
