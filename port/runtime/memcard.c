/*
 * Tarjeta de memoria en la ranura A (EXI canal 0, dispositivo 0).
 *
 * Se emula una Memory Card 251 (16 Mbit = 2 MB) oficial a nivel de protocolo,
 * igual que la consola la ve por el bus EXI. Los datos viven en un archivo
 * ("memcard_A.raw", el mismo formato crudo que usa Dolphin). Si el archivo no
 * existe se crea una tarjeta ya formateada, para que el juego no pida formatear.
 *
 * Comandos (primer byte despues de seleccionar el chip):
 *   0x00 ID del dispositivo        0x83 leer estado       0x89 borrar estado
 *   0x52 leer (5 bytes de comando + latencia + DMA)      0xF2 programar pagina (128 bytes)
 *   0xF1 borrar sector (8 KB)      0xF4 borrar todo        0x81 habilitar interrupcion
 *   0x85 leer ID del fabricante    0x87 dormir / 0x88 despertar
 * Programar y borrar avisan que terminaron con la interrupcion EXI del canal.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "runtime.h"
#include "memcard.h"

#define MC_SIZE_MBIT 16u
#define MC_BYTES (MC_SIZE_MBIT * 1024u * 1024u / 8u)
#define MC_BLOCK 0x2000u
#define MC_BLOCKS (MC_BYTES / MC_BLOCK)

static uint8_t* s_data;
static FILE* s_file;
static int s_present;

static uint8_t s_cmd;
static uint32_t s_pos;            /* bytes transferidos desde que se selecciono el chip */
static uint8_t s_args[8];
static uint32_t s_addr;
static uint8_t s_status = 0x41;  /* listo y desbloqueado */
static int s_int_enabled, s_int_pending;
int mc_trace;

static void put16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }

/* Suma de control del SDK (__CARDCheckSum): palabras de 16 bits y su complemento. */
static void checksum(const uint8_t* p, uint32_t len, uint8_t* out_sum, uint8_t* out_inv) {
    uint16_t s = 0, si = 0;
    uint32_t i;
    for (i = 0; i + 1 < len; i += 2) {
        uint16_t w = (uint16_t)((p[i] << 8) | p[i + 1]);
        s = (uint16_t)(s + w);
        si = (uint16_t)(si + (uint16_t)~w);
    }
    if (s == 0xFFFF) s = 0;
    if (si == 0xFFFF) si = 0;
    put16(out_sum, s);
    put16(out_inv, si);
}

/* Formatea la tarjeta como lo haria CARDFormat con una SRAM en cero (asi la ve el juego). */
static void format_card(void) {
    uint8_t* h = s_data;
    uint64_t rnd = 0;                      /* hora de formateo = 0 */
    int i, b;
    memset(s_data, 0xFF, MC_BYTES);
    memset(h, 0xFF, MC_BLOCK);
    for (i = 0; i < 12; i++) {
        rnd = (rnd * 1103515245ull + 12345ull) >> 16;
        h[i] = (uint8_t)(0 + rnd);         /* flashID de la SRAM = 0 */
        rnd = ((rnd * 1103515245ull + 12345ull) >> 16) & 0x7FFF;
    }
    memset(h + 12, 0, 8);                  /* hora de formateo */
    memset(h + 20, 0, 4);                  /* sesgo de la SRAM */
    memset(h + 24, 0, 4);                  /* idioma */
    memset(h + 28, 0, 4);
    put16(h + 0x20, 0);                    /* dispositivo: ranura A */
    put16(h + 0x22, MC_SIZE_MBIT);
    put16(h + 0x24, 0);                    /* codificacion ANSI */
    checksum(h, 0x1FC, h + 0x1FC, h + 0x1FE);
    /* directorio (dos copias, bloques 1 y 2) */
    for (b = 1; b <= 2; b++) {
        uint8_t* d = s_data + b * MC_BLOCK;
        memset(d, 0xFF, MC_BLOCK);
        put16(d + 0x1FFA, (uint16_t)(b - 1)); /* contador de actualizaciones */
        checksum(d, 0x1FFC, d + 0x1FFC, d + 0x1FFE);
    }
    /* tabla de bloques (dos copias, bloques 3 y 4) */
    for (b = 3; b <= 4; b++) {
        uint8_t* t = s_data + b * MC_BLOCK;
        memset(t, 0, MC_BLOCK);
        put16(t + 4, (uint16_t)(b - 3));   /* contador de actualizaciones */
        put16(t + 6, (uint16_t)(MC_BLOCKS - 5));
        put16(t + 8, 4);                   /* ultimo bloque usado */
        checksum(t + 4, MC_BLOCK - 4, t + 0, t + 2);
    }
}

static void save_range(uint32_t a, uint32_t n) {
    if (!s_file || !n) return;
    fseek(s_file, (long)a, SEEK_SET);
    fwrite(s_data + a, 1, n, s_file);
    fflush(s_file);
}

void mc_init(const char* path) {
    s_data = (uint8_t*)malloc(MC_BYTES);
    s_file = fopen(path, "r+b");
    if (s_file && fread(s_data, 1, MC_BYTES, s_file) == MC_BYTES) {
        rt_log("[tarjeta] usando %s\n", path);
    } else {
        if (s_file) fclose(s_file);
        format_card();
        s_file = fopen(path, "w+b");
        if (s_file) {
            fwrite(s_data, 1, MC_BYTES, s_file);
            fflush(s_file);
            rt_log("[tarjeta] tarjeta nueva formateada en %s\n", path);
        } else {
            rt_log("[tarjeta] no pude crear %s: las partidas no se guardaran\n", path);
        }
    }
    s_present = 1;
}

int mc_present(void) { return s_present; }

/* El chip se selecciono (cs = 1) o se libero (cs = 0). */
void mc_select(int cs) {
    if (cs) { s_pos = 0; s_cmd = 0xFF; return; }
    /* al soltar el chip terminan las operaciones de escritura */
    switch (s_cmd) {
    case 0xF1:                                    /* borrar sector */
        if (s_pos >= 3) {
            uint32_t a = (((uint32_t)s_args[1] << 17) | ((uint32_t)s_args[2] << 9)) & (MC_BYTES - 1) & ~(MC_BLOCK - 1);
            memset(s_data + a, 0xFF, MC_BLOCK);
            save_range(a, MC_BLOCK);
            if (mc_trace) rt_log("[tarjeta] borrar sector %06X\n", a);
            s_int_pending = 1;
        }
        break;
    case 0xF4:                                    /* borrar todo */
        if (s_pos >= 3) {
            memset(s_data, 0xFF, MC_BYTES);
            save_range(0, MC_BYTES);
            s_int_pending = 1;
        }
        break;
    case 0xF2:                                    /* programar: los datos ya se escribieron */
        if (s_pos >= 5) s_int_pending = 1;
        break;
    default:
        break;
    }
    s_cmd = 0xFF;
}

static uint32_t array_addr(void) {
    return ((((uint32_t)s_args[1] & 0x7F) << 17) | ((uint32_t)s_args[2] << 9) |
            (((uint32_t)s_args[3] & 3) << 7) | ((uint32_t)s_args[4] & 0x7F)) & (MC_BYTES - 1);
}

/* Un byte por el bus: `in` es lo que manda la consola, devuelve lo que responde la tarjeta. */
static uint8_t xfer_byte(uint8_t in) {
    uint8_t out = 0xFF;
    uint32_t p = s_pos++;
    if (p == 0) {
        s_cmd = in;
        s_args[0] = in;
        switch (in) {
        case 0x89: s_status &= (uint8_t)~0x18; break;   /* borrar errores */
        case 0x87: case 0x88: break;
        default: break;
        }
        return 0xFF;
    }
    if (p < sizeof s_args) s_args[p] = in;
    switch (s_cmd) {
    case 0x00:                                     /* ID: 00 | xx 00 00 00 10 */
        if (p >= 2 && p <= 5) out = (uint8_t)(MC_SIZE_MBIT >> (8 * (5 - p)));
        else out = 0;
        break;
    case 0x83:                                     /* estado */
        out = s_status;
        break;
    case 0x85:                                     /* ID del fabricante (Nintendo) */
        if (p == 2) out = 0xC2; else if (p == 3) out = 0x21; else out = 0;
        break;
    case 0x81:                                     /* habilitar interrupcion */
        if (p == 1) s_int_enabled = in & 1;
        break;
    case 0x52:                                     /* lectura por bytes (despues de la latencia) */
        if (p == 4) s_addr = array_addr();
        else if (p >= 5 + 4) { out = s_data[s_addr]; s_addr = (s_addr + 1) & (MC_BYTES - 1); }
        break;
    case 0xF2:                                     /* programar por bytes */
        if (p == 4) s_addr = array_addr();
        else if (p >= 5) {
            uint32_t a = (s_addr & ~0x7Fu) | ((s_addr + (p - 5)) & 0x7F);
            s_data[a] = in;
            save_range(a, 1);
        }
        break;
    default:
        break;
    }
    return out;
}

void mc_imm(uint8_t* buf, int len, int rw) {
    int i;
    for (i = 0; i < len; i++) {
        uint8_t o = xfer_byte(rw == 0 ? 0 : buf[i]);
        if (rw != 1) buf[i] = o;
    }
}

void mc_dma(uint32_t ram_addr, uint32_t len, int rw) {
    uint8_t* p = mem_ptr(ram_addr | 0x80000000u);
    if (!p || !mem_ptr((ram_addr | 0x80000000u) + len - 1)) return;
    if (s_cmd == 0x52 && rw == 0) {                /* lectura de un bloque */
        uint32_t a;
        if (s_pos >= 5) {
            a = array_addr();
            if (s_pos > 5 + 4) a = s_addr;
        } else a = 0;
        if (a + len > MC_BYTES) len = MC_BYTES - a;
        memcpy(p, s_data + a, len);
        s_addr = (a + len) & (MC_BYTES - 1);
        s_pos += len;
        if (mc_trace) rt_log("[tarjeta] leer %06X (%u bytes)\n", a, len);
        return;
    }
    if (s_cmd == 0xF2 && rw == 1) {                /* programar una pagina */
        uint32_t a = array_addr(), i;
        for (i = 0; i < len; i++) {
            uint32_t d = (a & ~0x7Fu) | ((a + i) & 0x7F);
            s_data[d] = p[i];
        }
        save_range(a & ~0x7Fu, 128);
        s_pos += len;
        if (mc_trace) rt_log("[tarjeta] programar %06X (%u bytes)\n", a, len);
        return;
    }
    {
        uint32_t i;
        for (i = 0; i < len; i++) {
            uint8_t o = xfer_byte(rw == 0 ? 0 : p[i]);
            if (rw != 1) p[i] = o;
        }
    }
}

/* Devuelve 1 (una vez) si la tarjeta termino una operacion y pide interrupcion. */
int mc_take_interrupt(void) {
    if (s_int_pending) { s_int_pending = 0; return 1; }
    return 0;
}
