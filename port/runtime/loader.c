/* Carga la imagen combinada (cargador + VM) en la memoria emulada. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "runtime.h"

static uint32_t be32(const uint8_t* p) { return ((uint32_t)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }

#define VM_PAYLOAD_ADDR 0x80100000u

/* Copia las secciones de un DOL en memoria. Si una seccion de texto esta en
 * 0x80100000 es el rogue2vm.dol embebido en main.dol: se carga tambien (como hace
 * tools/combine_vm.py), asi se puede arrancar directo desde main.dol. */
static int load_image(const uint8_t* data, size_t size, uint32_t* entry, int depth) {
    uint8_t hdr[0x100];
    int i;
    if (size < 0x100) return 0;
    memcpy(hdr, data, sizeof hdr);
    {
        /* Primero se limpia la BSS y despues se copian las secciones: el rango de
         * BSS del encabezado incluye .sdata/.sdata2, que estan en el medio. */
        uint32_t bss = be32(hdr + 0xD8), bsz = be32(hdr + 0xDC);
        uint8_t* p = mem_ptr(bss);
        if (p && bsz) memset(p, 0, bsz);
    }
    for (i = 0; i < 18; i++) {
        uint32_t off = be32(hdr + i * 4), addr = be32(hdr + 0x48 + i * 4), sz = be32(hdr + 0x90 + i * 4);
        uint8_t* dst;
        if (!sz) continue;
        if ((size_t)off + sz > size) return 0;
        if (i < 7 && addr == VM_PAYLOAD_ADDR && depth == 0) {
            if (!load_image(data + off, sz, NULL, 1)) return 0;
            continue;
        }
        dst = mem_ptr(addr);
        if (!dst || !mem_ptr(addr + sz - 1)) rt_fatal("seccion fuera de memoria: 0x%08X\n", addr);
        memcpy(dst, data + off, sz);
    }
    if (entry) *entry = be32(hdr + 0xE0);
    return 1;
}

int dol_load_mem(const uint8_t* data, size_t size, uint32_t* entry) { return load_image(data, size, entry, 0); }

int dol_load(const char* path, uint32_t* entry) {
    FILE* f = fopen(path, "rb");
    long size;
    uint8_t* data;
    int ok;
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    data = (uint8_t*)malloc((size_t)size);
    if (!data || fread(data, 1, (size_t)size, f) != (size_t)size) { fclose(f); free(data); return 0; }
    fclose(f);
    ok = load_image(data, (size_t)size, entry, 0);
    free(data);
    return ok;
}
