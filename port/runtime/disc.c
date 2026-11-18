/*
 * Disco virtual: o bien la imagen del disco (.iso / .gcm, tambien las .nkit.iso) o bien los
 * archivos extraidos con Dolphin (sys/ y la carpeta de archivos). Con los archivos sueltos
 * se reconstruyen las lecturas del disco; las zonas que faltan se leen como ceros.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "runtime.h"
#include "os.h"

typedef struct {
    uint32_t offset, size;
    char path[512];
    FILE* f;
} DiscFile;

static DiscFile s_files[256];
static int s_nfiles;
static uint8_t s_fst[0x10000];
static uint32_t s_fst_size;

static uint32_t be32(const uint8_t* p) { return ((uint32_t)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }

static void add(uint32_t off, uint32_t size, const char* path) {
    DiscFile* d;
    if (s_nfiles >= 256) return;
    d = &s_files[s_nfiles++];
    d->offset = off;
    d->size = size;
    snprintf(d->path, sizeof d->path, "%s", path);
    d->f = fopen(path, "rb");
}

static uint32_t file_size(const char* path) {
    FILE* f = fopen(path, "rb");
    long n;
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fclose(f);
    return (uint32_t)n;
}

const uint8_t* disc_fst(uint32_t* size) {
    *size = s_fst_size;
    return s_fst;
}

int disc_open(const char* sys_dir, const char* files_dir) {
    char p[512];
    uint8_t boot[0x440];
    FILE* f;
    uint32_t n, strtab, i;
    char dirs[64][256];
    uint32_t dir_end[64];
    int depth = 0;

    snprintf(p, sizeof p, "%s/boot.bin", sys_dir);
    f = fopen(p, "rb");
    if (!f || fread(boot, 1, sizeof boot, f) != sizeof boot) return 0;
    fclose(f);
    add(0, 0x440, p);
    snprintf(p, sizeof p, "%s/bi2.bin", sys_dir);
    add(0x440, 0x2000, p);
    snprintf(p, sizeof p, "%s/apploader.img", sys_dir);
    add(0x2440, file_size(p), p);
    snprintf(p, sizeof p, "%s/main.dol", sys_dir);
    add(be32(boot + 0x420), file_size(p), p);
    snprintf(p, sizeof p, "%s/fst.bin", sys_dir);
    add(be32(boot + 0x424), be32(boot + 0x428), p);
    f = fopen(p, "rb");
    if (!f) return 0;
    s_fst_size = (uint32_t)fread(s_fst, 1, sizeof s_fst, f);
    fclose(f);

    /* recorrer la FST para ubicar cada archivo en el disco */
    n = be32(s_fst + 8);
    strtab = n * 12;
    dirs[0][0] = 0;
    dir_end[0] = n;
    for (i = 1; i < n; i++) {
        const uint8_t* e = s_fst + i * 12;
        const char* name = (const char*)s_fst + strtab + (be32(e) & 0xFFFFFF);
        while (depth > 0 && i >= dir_end[depth]) depth--;
        if (e[0] == 1) {
            depth++;
            snprintf(dirs[depth], sizeof dirs[depth], "%s%s/", dirs[depth - 1], name);
            dir_end[depth] = be32(e + 8);
        } else {
            snprintf(p, sizeof p, "%s/%s%s", files_dir, dirs[depth], name);
            add(be32(e + 4), be32(e + 8), p);
        }
    }
    {
        int have = 0, k;
        for (k = 0; k < s_nfiles; k++) have += s_files[k].f != NULL;
        rt_log("[disco] %d de %d archivos presentes\n", have, s_nfiles);
    }
    return 1;
}

/* Imagen del disco (GCM): un solo archivo que cubre todo el disco. */
int disc_open_iso(const char* path) {
    FILE* f = fopen(path, "rb");
    uint8_t boot[0x440];
    uint32_t fst_off, fst_size;
    long n;
    if (!f) return 0;
    if (fread(boot, 1, sizeof boot, f) != sizeof boot || be32(boot + 0x1C) != 0xC2339F3Du) { fclose(f); return 0; }   /* firma de disco de GameCube */
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fst_off = be32(boot + 0x424); fst_size = be32(boot + 0x428);
    if (fst_size > sizeof s_fst || (long)fst_off + (long)fst_size > n) { fclose(f); return 0; }
    fseek(f, (long)fst_off, SEEK_SET);
    s_fst_size = (uint32_t)fread(s_fst, 1, fst_size, f);
    s_nfiles = 0;
    s_files[0].offset = 0; s_files[0].size = (uint32_t)n; s_files[0].f = f;
    snprintf(s_files[0].path, sizeof s_files[0].path, "%s", path);
    s_nfiles = 1;
    rt_log("[disco] imagen %s: %.6s, %ld MB\n", path, (const char*)boot, n >> 20);
    return 1;
}

/* Codigo del juego en el disco: tamano del main.dol (por su cabecera). */
uint32_t disc_dol_offset(uint32_t* size) {
    uint8_t boot[0x440], h[0x100];
    uint32_t off, end = 0;
    int k;
    disc_read(0, sizeof boot, boot);
    off = be32(boot + 0x420);
    disc_read(off, sizeof h, h);
    for (k = 0; k < 18; k++) {
        uint32_t o = be32(h + 4 * k), sz = be32(h + 0x90 + 4 * k);
        if (o + sz > end) end = o + sz;
    }
    *size = end;
    return off;
}

void disc_read(uint32_t offset, uint32_t len, uint8_t* dst) {
    int i;
    memset(dst, 0, len);
    for (i = 0; i < s_nfiles; i++) {
        DiscFile* d = &s_files[i];
        uint32_t a = offset, b = offset + len;
        uint32_t fa = d->offset, fb = d->offset + d->size;
        if (b <= fa || a >= fb) continue;
        if (a < fa) a = fa;
        if (b > fb) b = fb;
        if (!d->f) {
            static int warned[256];
            if (!warned[i]) { warned[i] = 1; rt_log("[disco] falta %s (se lee como ceros)\n", d->path); }
            continue;
        }
        fseek(d->f, (long)(a - fa), SEEK_SET);
        if (fread(dst + (a - offset), 1, b - a, d->f) != b - a) rt_log("[disco] lectura corta en %s\n", d->path);
    }
}
