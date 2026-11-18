/*
 * Rasterizador por software del procesador grafico de la GameCube (GX).
 *
 * Reproduce el camino de la GPU:
 *   1. vertices: lectura directa o indexada segun el formato (CP), matriz de
 *      posicion (XF), proyeccion y viewport;
 *   2. canales de color (material/vertice/ambiente, sin luces por ahora);
 *   3. generacion de coordenadas de textura con las matrices de textura;
 *   4. etapas TEV (combinador de colores) con texturas, colores y constantes;
 *   5. comparacion de alfa, profundidad (Z), mezcla y escritura en el EFB;
 *   6. copia del EFB a la XFB (lo que se ve) o a una textura.
 * Es lento pero exacto; sirve de referencia y para guardar cuadros.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "runtime.h"
#include "os.h"
#include "gx.h"
#include "render_int.h"

static uint8_t s_efb[EFB_W * EFB_H * 4];   /* RGBA */
static uint32_t s_zbuf[EFB_W * EFB_H];      /* 24 bits */
static uint8_t s_efb_last[EFB_W * EFB_H * 4]; /* EFB en el momento de la ultima copia a la XFB */
uint8_t s_tmem[1 << 20];                    /* TMEM: paletas (TLUT) */
int render_enabled = 1;
#ifdef RL_GL
int render_gl = 1;                   /* OpenGL por defecto; --sw usa el rasterizador por software */
#else
int render_gl;
#endif
int render_trace_copies;
int render_dump_copies;              /* --dump-copies N: guarda el EFB en cada N-esima copia a la XFB */
static uint32_t s_xfb_copies;
extern const char* video_dump_dir;
void render_dump_efb(const char* path);

/* ---- utilidades ------------------------------------------------------------ */

static float xf_f(uint32_t addr) { return u32_f(gx.xf_mem[addr & 0x7FF]); }
static float xf_rf(uint32_t reg) { return u32_f(gx.xf_regs[reg & 0xFF]); }
static uint8_t clamp8(int v) { return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v); }
static uint32_t bp(int r) { return gx.bp_regs[r]; }

static float read_comp(const uint8_t* p, int fmt, float scale) {
    switch (fmt) {
    case 0: return p[0] * scale;
    case 1: return (int8_t)p[0] * scale;
    case 2: return (uint16_t)((p[0] << 8) | p[1]) * scale;
    case 3: return (int16_t)((p[0] << 8) | p[1]) * scale;
    default: { uint32_t v = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; return u32_f(v); }
    }
}
static const int s_csize[8] = { 1, 1, 2, 2, 4, 4, 4, 4 };

static void read_color(const uint8_t* p, int fmt, uint8_t out[4]) {
    switch (fmt) {
    case 0: { uint16_t v = (uint16_t)((p[0] << 8) | p[1]);  /* RGB565 */
        out[0] = (uint8_t)(((v >> 11) & 31) * 255 / 31); out[1] = (uint8_t)(((v >> 5) & 63) * 255 / 63);
        out[2] = (uint8_t)((v & 31) * 255 / 31); out[3] = 255; break; }
    case 1: case 2: out[0] = p[0]; out[1] = p[1]; out[2] = p[2]; out[3] = 255; break;  /* RGB8, RGBX8 */
    case 3: { uint16_t v = (uint16_t)((p[0] << 8) | p[1]);  /* RGBA4 */
        out[0] = (uint8_t)(((v >> 12) & 15) * 17); out[1] = (uint8_t)(((v >> 8) & 15) * 17);
        out[2] = (uint8_t)(((v >> 4) & 15) * 17); out[3] = (uint8_t)((v & 15) * 17); break; }
    case 4: { uint32_t v = ((uint32_t)p[0] << 16) | (p[1] << 8) | p[2];  /* RGBA6 */
        out[0] = (uint8_t)(((v >> 18) & 63) * 255 / 63); out[1] = (uint8_t)(((v >> 12) & 63) * 255 / 63);
        out[2] = (uint8_t)(((v >> 6) & 63) * 255 / 63); out[3] = (uint8_t)((v & 63) * 255 / 63); break; }
    default: out[0] = p[0]; out[1] = p[1]; out[2] = p[2]; out[3] = p[3]; break;  /* RGBA8 */
    }
}

/* ---- vertices ------------------------------------------------------------- */


static const uint8_t* attr_ptr(const uint8_t* v, int mode, int off, int array, int* ok) {
    if (mode == 1) return v + off;
    {
        uint32_t idx = (mode == 2) ? v[off] : (uint32_t)((v[off] << 8) | v[off + 1]);
        uint32_t a = (gx.cp_array_base[array] + idx * gx.cp_array_stride[array]) | 0x80000000u;
        const uint8_t* p = mem_ptr(a);
        if (!p) { *ok = 0; return v; }
        return p;
    }
}

static void mtx_apply(uint32_t base, const float in[3], float w, float out[3]) {
    int r;
    for (r = 0; r < 3; r++)
        out[r] = xf_f(base + r * 4) * in[0] + xf_f(base + r * 4 + 1) * in[1] + xf_f(base + r * 4 + 2) * in[2] + xf_f(base + r * 4 + 3) * w;
}

void decode_vertex(const GXVtxLayout* L, const uint8_t* v, Vtx* o) {
    float pos[3] = { 0, 0, 0 }, ep[3], nrm[3] = { 0, 0, 1 };
    uint32_t pmi = (L->pnmtx >= 0) ? v[L->pnmtx] : (gx.cp_matidx_a & 0x3F);
    int i, ok = 1;
    float ps = 1.0f / (float)(1 << L->pos_shift);
    /* posicion */
    if (L->pos_mode) {
        const uint8_t* p = attr_ptr(v, L->pos_mode, L->pos_off, 0, &ok);
        int cs = s_csize[L->pos_fmt];
        pos[0] = read_comp(p, L->pos_fmt, ps);
        pos[1] = read_comp(p + cs, L->pos_fmt, ps);
        if (L->pos_elems == 3) pos[2] = read_comp(p + 2 * cs, L->pos_fmt, ps);
    }
    if (L->nrm_mode) {
        const uint8_t* p = attr_ptr(v, L->nrm_mode, L->nrm_off, 1, &ok);
        int cs = s_csize[L->nrm_fmt];
        float sc = L->nrm_fmt == 1 ? 1.0f / 64 : L->nrm_fmt == 3 ? 1.0f / 16384 : 1.0f;
        nrm[0] = read_comp(p, L->nrm_fmt, sc); nrm[1] = read_comp(p + cs, L->nrm_fmt, sc); nrm[2] = read_comp(p + 2 * cs, L->nrm_fmt, sc);
    }
    mtx_apply(pmi * 4, pos, 1.0f, ep);
    /* proyeccion */
    {
        float p0 = xf_rf(0x20), p1 = xf_rf(0x21), p2 = xf_rf(0x22), p3 = xf_rf(0x23), p4 = xf_rf(0x24), p5 = xf_rf(0x25);
        if (gx.xf_regs[0x26] == 0) {      /* perspectiva */
            o->x = p0 * ep[0] + p1 * ep[2];
            o->y = p2 * ep[1] + p3 * ep[2];
            o->z = p4 * ep[2] + p5;
            o->w = -ep[2];
        } else {                          /* ortografica */
            o->x = p0 * ep[0] + p1;
            o->y = p2 * ep[1] + p3;
            o->z = p4 * ep[2] + p5;
            o->w = 1.0f;
        }
    }
    /* canales de color: material (registro o vertice), ambiente y luces */
    {
        float n[3];
        uint32_t nb = 0x400 + pmi * 3;
        int has_nrm = L->nrm_mode != 0;
        n[0] = xf_f(nb) * nrm[0] + xf_f(nb + 1) * nrm[1] + xf_f(nb + 2) * nrm[2];
        n[1] = xf_f(nb + 3) * nrm[0] + xf_f(nb + 4) * nrm[1] + xf_f(nb + 5) * nrm[2];
        n[2] = xf_f(nb + 6) * nrm[0] + xf_f(nb + 7) * nrm[1] + xf_f(nb + 8) * nrm[2];
        {
            float len = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
            if (len > 0) { n[0] /= len; n[1] /= len; n[2] /= len; }
        }
        for (i = 0; i < 2; i++) {
            uint8_t vc[4] = { 255, 255, 255, 255 };
            int have_vc = 0, part;
            if (L->col_mode[i]) {
                const uint8_t* p = attr_ptr(v, L->col_mode[i], L->col_off[i], 2 + i, &ok);
                read_color(p, L->col_fmt[i], vc);
                have_vc = 1;
            }
            for (part = 0; part < 2; part++) {   /* 0 = color (RGB), 1 = alfa */
                uint32_t ctrl = gx.xf_regs[(part ? 0x10 : 0x0E) + i];
                uint32_t matr = gx.xf_regs[0x0C + i], ambr = gx.xf_regs[0x0A + i];
                float mat[4], amb[4], lit[4];
                int k, li;
                for (k = 0; k < 4; k++) {
                    mat[k] = (float)((matr >> (24 - 8 * k)) & 255);
                    amb[k] = (float)((ambr >> (24 - 8 * k)) & 255);
                }
                if ((ctrl & 1) && have_vc) for (k = 0; k < 4; k++) mat[k] = vc[k];
                if ((ctrl & 2) && has_nrm) {        /* iluminacion habilitada */
                    uint32_t mask = ((ctrl >> 2) & 15) | (((ctrl >> 11) & 15) << 4);
                    int diff = (int)((ctrl >> 7) & 3), atten = (int)((ctrl >> 9) & 3);
                    if ((ctrl & 0x40) && have_vc) for (k = 0; k < 4; k++) amb[k] = vc[k];
                    for (k = 0; k < 4; k++) lit[k] = amb[k];
                    for (li = 0; li < 8; li++) {
                        uint32_t lb = 0x600 + li * 0x10;
                        float lp[3], ld[3], dlen, ndl, att = 1.0f;
                        uint32_t lc;
                        if (!(mask & (1u << li))) continue;
                        lc = gx.xf_mem[lb + 3];
                        lp[0] = xf_f(lb + 10) - ep[0]; lp[1] = xf_f(lb + 11) - ep[1]; lp[2] = xf_f(lb + 12) - ep[2];
                        dlen = sqrtf(lp[0] * lp[0] + lp[1] * lp[1] + lp[2] * lp[2]);
                        if (dlen > 0) { ld[0] = lp[0] / dlen; ld[1] = lp[1] / dlen; ld[2] = lp[2] / dlen; }
                        else { ld[0] = ld[1] = 0; ld[2] = 1; }
                        ndl = n[0] * ld[0] + n[1] * ld[1] + n[2] * ld[2];
                        {   /* como Dolphin: atenuacion 1 = especular, 3 = foco; difuso 1 = con signo, 2 = recortado */
                            float a0 = xf_f(lb + 4), a1 = xf_f(lb + 5), a2 = xf_f(lb + 6);
                            float k0 = xf_f(lb + 7), k1 = xf_f(lb + 8), k2 = xf_f(lb + 9);
                            float dx = xf_f(lb + 13), dy = xf_f(lb + 14), dz = xf_f(lb + 15);
                            if (atten == 1) {
                                float a = ndl >= 0 ? fmaxf(0.0f, n[0] * dx + n[1] * dy + n[2] * dz) : 0.0f, den;
                                if (diff == 0) { float q = k0 * k0 + k1 * k1 + k2 * k2; if (q > 0) { q = 1.0f / sqrtf(q); k0 *= q; k1 *= q; k2 *= q; } }
                                den = k0 + k1 * a + k2 * a * a;
                                att = den != 0 ? fmaxf(0.0f, a0 + a1 * a + a2 * a * a) / den : 0.0f;
                            } else if (atten == 3) {
                                float cs = fmaxf(0.0f, ld[0] * dx + ld[1] * dy + ld[2] * dz);
                                float den = k0 + k1 * dlen + k2 * dlen * dlen;
                                att = den != 0 ? fmaxf(0.0f, a0 + a1 * cs + a2 * cs * cs) / den : 0.0f;
                            }
                        }
                        if (diff == 2) ndl = ndl < 0 ? 0 : ndl;
                        else if (diff == 0) ndl = 1.0f;
                        for (k = 0; k < 4; k++) lit[k] += ((lc >> (24 - 8 * k)) & 255) * ndl * att;
                    }
                    for (k = 0; k < 4; k++) mat[k] = mat[k] * fminf(255.0f, fmaxf(0.0f, lit[k])) / 255.0f;
                }
                if (part == 0) { o->col[i][0] = mat[0]; o->col[i][1] = mat[1]; o->col[i][2] = mat[2]; }
                else o->col[i][3] = mat[3];
            }
        }
    }
    /* coordenadas de textura */
    {
        uint32_t ntex = gx.xf_regs[0x3F] & 15;
        float raw[8][2];
        for (i = 0; i < 8; i++) {
            raw[i][0] = raw[i][1] = 0;
            if (L->tex_mode[i]) {
                const uint8_t* p = attr_ptr(v, L->tex_mode[i], L->tex_off[i], 4 + i, &ok);
                float ts = 1.0f / (float)(1 << L->tex_shift[i]);
                int cs = s_csize[L->tex_fmt[i]];
                raw[i][0] = read_comp(p, L->tex_fmt[i], ts);
                if (L->tex_elems[i] == 2) raw[i][1] = read_comp(p + cs, L->tex_fmt[i], ts);
            }
        }
        for (i = 0; i < (int)ntex && i < 8; i++) {
            uint32_t tg = gx.xf_regs[0x40 + i];
            uint32_t type = (tg >> 4) & 7, src = (tg >> 7) & 31, proj = (tg >> 1) & 1;
            float in[3] = { 0, 0, 1 }, out[3];
            uint32_t tmi;
            if (L->texmtx[i] >= 0) tmi = v[L->texmtx[i]];
            else tmi = (i < 4) ? (gx.cp_matidx_a >> (6 + 6 * i)) & 0x3F : (gx.cp_matidx_b >> (6 * (i - 4))) & 0x3F;
            switch (src) {
            case 0: in[0] = pos[0]; in[1] = pos[1]; in[2] = pos[2]; break;
            case 1: in[0] = nrm[0]; in[1] = nrm[1]; in[2] = nrm[2]; break;
            default:
                if (src >= 5 && src <= 12) { in[0] = raw[src - 5][0]; in[1] = raw[src - 5][1]; in[2] = 1.0f; }
                break;
            }
            if (type == 0) {
                if (proj) mtx_apply(tmi * 4, in, 1.0f, out);
                else {
                    out[0] = xf_f(tmi * 4) * in[0] + xf_f(tmi * 4 + 1) * in[1] + xf_f(tmi * 4 + 2) * in[2] + xf_f(tmi * 4 + 3);
                    out[1] = xf_f(tmi * 4 + 4) * in[0] + xf_f(tmi * 4 + 5) * in[1] + xf_f(tmi * 4 + 6) * in[2] + xf_f(tmi * 4 + 7);
                    out[2] = 1.0f;
                }
            } else if (type == 1) {      /* relieve: la coordenada de otra etapa */
                uint32_t es = (tg >> 12) & 7;
                if ((int)es < i) { o->tex[i][0] = o->tex[es][0]; o->tex[i][1] = o->tex[es][1]; o->tex[i][2] = o->tex[es][2]; }
                else { o->tex[i][0] = 0; o->tex[i][1] = 0; o->tex[i][2] = 1; }
                continue;
            } else if (type == 2 || type == 3) {
                out[0] = o->col[type - 2][0] / 255.0f; out[1] = o->col[type - 2][1] / 255.0f; out[2] = 1.0f;
            } else {
                out[0] = in[0]; out[1] = in[1]; out[2] = 1.0f;
            }
            /* matriz de "post-transformacion" (dual tex) */
            if ((gx.xf_regs[0x12] & 1) && type == 0) {
                uint32_t dt = gx.xf_regs[0x50 + i];
                uint32_t pi = (dt & 0x3F) * 4 + 0x500;
                float o2[3];
                float n[3] = { out[0], out[1], proj ? out[2] : 1.0f };   /* sin dividir por q (como la consola) */
                if (dt & 0x100) { float l = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]); if (l > 0) { n[0] /= l; n[1] /= l; n[2] /= l; } }
                mtx_apply(pi, n, 1.0f, o2);
                out[0] = o2[0]; out[1] = o2[1]; out[2] = proj ? o2[2] : 1.0f;
            }
            o->tex[i][0] = out[0]; o->tex[i][1] = out[1]; o->tex[i][2] = out[2];
        }
    }
    (void)ok;
}

/* ---- texturas ------------------------------------------------------------ */


static int tex_regs(int map, int* mode0, int* img0, int* img3, int* tlut) {
    if (map < 4) { *mode0 = 0x80 + map; *img0 = 0x88 + map; *img3 = 0x94 + map; *tlut = 0x98 + map; }
    else { *mode0 = 0xA0 + map - 4; *img0 = 0xA8 + map - 4; *img3 = 0xB4 + map - 4; *tlut = 0xB8 + map - 4; }
    return 1;
}

void tex_info(int map, TexInfo* t) {
    int m0, i0, i3, tl;
    uint32_t mode, img;
    tex_regs(map, &m0, &i0, &i3, &tl);
    mode = bp(m0); img = bp(i0);
    t->w = (int)(img & 0x3FF) + 1;
    t->h = (int)((img >> 10) & 0x3FF) + 1;
    t->fmt = (int)((img >> 20) & 0xF);
    t->wrap_s = mode & 3;
    t->wrap_t = (mode >> 2) & 3;
    t->mag_linear = (mode >> 4) & 1;
    t->data = mem_ptr(((bp(i3) & 0xFFFFFF) << 5) | 0x80000000u);
    t->tlut = (bp(tl) & 0x3FF) << 9;
    t->tlut_fmt = (bp(tl) >> 10) & 3;
}

static void tlut_color(uint32_t off, int fmt, uint32_t idx, uint8_t o[4]) {
    const uint8_t* p = s_tmem + ((off + idx * 2) & (sizeof s_tmem - 2));
    uint16_t v = (uint16_t)((p[0] << 8) | p[1]);
    if (fmt == 0) { o[0] = o[1] = o[2] = p[1]; o[3] = p[0]; }        /* IA8 */
    else if (fmt == 1) { o[0] = (uint8_t)(((v >> 11) & 31) * 255 / 31); o[1] = (uint8_t)(((v >> 5) & 63) * 255 / 63); o[2] = (uint8_t)((v & 31) * 255 / 31); o[3] = 255; }
    else {                                                            /* RGB5A3 */
        if (v & 0x8000) { o[0] = (uint8_t)(((v >> 10) & 31) * 255 / 31); o[1] = (uint8_t)(((v >> 5) & 31) * 255 / 31); o[2] = (uint8_t)((v & 31) * 255 / 31); o[3] = 255; }
        else { o[0] = (uint8_t)(((v >> 8) & 15) * 17); o[1] = (uint8_t)(((v >> 4) & 15) * 17); o[2] = (uint8_t)((v & 15) * 17); o[3] = (uint8_t)(((v >> 12) & 7) * 255 / 7); }
    }
}

/* Lee el texel (x, y) de una textura en su formato nativo (en bloques). */
void texel(const TexInfo* t, int x, int y, uint8_t o[4]) {
    static const int bw[16] = { 8, 8, 8, 4, 4, 4, 4, 0, 8, 8, 4, 0, 0, 0, 8, 0 };
    static const int bh[16] = { 8, 4, 4, 4, 4, 4, 4, 0, 8, 4, 4, 0, 0, 0, 8, 0 };
    int f = t->fmt, bwid, bhgt, blocks_w, bx, by, ix, iy;
    const uint8_t* d = t->data;
    if (!d) { o[0] = o[1] = o[2] = 255; o[3] = 255; return; }
    bwid = bw[f]; bhgt = bh[f];
    if (!bwid) { o[0] = 255; o[1] = 0; o[2] = 255; o[3] = 255; return; }
    blocks_w = (t->w + bwid - 1) / bwid;
    bx = x / bwid; by = y / bhgt; ix = x % bwid; iy = y % bhgt;
    switch (f) {
    case 0: { /* I4 */
        const uint8_t* b = d + (by * blocks_w + bx) * 32;
        uint8_t v = b[iy * 4 + ix / 2];
        v = (ix & 1) ? (v & 15) : (v >> 4);
        o[0] = o[1] = o[2] = o[3] = (uint8_t)(v * 17);
        return;
    }
    case 1: { /* I8 */
        uint8_t v = d[(by * blocks_w + bx) * 32 + iy * 8 + ix];
        o[0] = o[1] = o[2] = o[3] = v;
        return;
    }
    case 2: { /* IA4 */
        uint8_t v = d[(by * blocks_w + bx) * 32 + iy * 8 + ix];
        o[0] = o[1] = o[2] = (uint8_t)((v & 15) * 17); o[3] = (uint8_t)((v >> 4) * 17);
        return;
    }
    case 3: { /* IA8 */
        const uint8_t* p = d + (by * blocks_w + bx) * 32 + (iy * 4 + ix) * 2;
        o[0] = o[1] = o[2] = p[1]; o[3] = p[0];
        return;
    }
    case 4: { /* RGB565 */
        const uint8_t* p = d + (by * blocks_w + bx) * 32 + (iy * 4 + ix) * 2;
        read_color(p, 0, o);
        return;
    }
    case 5: { /* RGB5A3 */
        const uint8_t* p = d + (by * blocks_w + bx) * 32 + (iy * 4 + ix) * 2;
        uint16_t v = (uint16_t)((p[0] << 8) | p[1]);
        if (v & 0x8000) { o[0] = (uint8_t)(((v >> 10) & 31) * 255 / 31); o[1] = (uint8_t)(((v >> 5) & 31) * 255 / 31); o[2] = (uint8_t)((v & 31) * 255 / 31); o[3] = 255; }
        else { o[0] = (uint8_t)(((v >> 8) & 15) * 17); o[1] = (uint8_t)(((v >> 4) & 15) * 17); o[2] = (uint8_t)((v & 15) * 17); o[3] = (uint8_t)(((v >> 12) & 7) * 255 / 7); }
        return;
    }
    case 6: { /* RGBA8: dos mitades de 32 bytes (AR y GB) */
        const uint8_t* b = d + (by * blocks_w + bx) * 64;
        int k = (iy * 4 + ix) * 2;
        o[3] = b[k]; o[0] = b[k + 1]; o[1] = b[32 + k]; o[2] = b[32 + k + 1];
        return;
    }
    case 8: { /* C4 */
        const uint8_t* b = d + (by * blocks_w + bx) * 32;
        uint8_t v = b[iy * 4 + ix / 2];
        tlut_color(t->tlut, t->tlut_fmt, (ix & 1) ? (v & 15) : (v >> 4), o);
        return;
    }
    case 9: { /* C8 */
        tlut_color(t->tlut, t->tlut_fmt, d[(by * blocks_w + bx) * 32 + iy * 8 + ix], o);
        return;
    }
    case 10: { /* C14X2 */
        const uint8_t* p = d + (by * blocks_w + bx) * 32 + (iy * 4 + ix) * 2;
        tlut_color(t->tlut, t->tlut_fmt, ((p[0] << 8) | p[1]) & 0x3FFF, o);
        return;
    }
    case 14: { /* CMPR: bloques de 8x8 con 4 sub-bloques DXT1 de 4x4 */
        const uint8_t* b = d + (by * blocks_w + bx) * 32 + ((iy / 4) * 2 + (ix / 4)) * 8;
        uint16_t c0 = (uint16_t)((b[0] << 8) | b[1]), c1 = (uint16_t)((b[2] << 8) | b[3]);
        uint8_t p0[4], p1[4], pal[4][4];
        int sel, k;
        read_color(b, 0, p0); read_color(b + 2, 0, p1);
        for (k = 0; k < 4; k++) { pal[0][k] = p0[k]; pal[1][k] = p1[k]; }
        if (c0 > c1) {
            for (k = 0; k < 3; k++) { pal[2][k] = (uint8_t)((2 * p0[k] + p1[k]) / 3); pal[3][k] = (uint8_t)((p0[k] + 2 * p1[k]) / 3); }
            pal[2][3] = pal[3][3] = 255;
        } else {
            for (k = 0; k < 3; k++) { pal[2][k] = (uint8_t)((p0[k] + p1[k]) / 2); pal[3][k] = pal[2][k]; }
            pal[2][3] = 255; pal[3][3] = 0;
        }
        sel = (b[4 + (iy & 3)] >> (6 - 2 * (ix & 3))) & 3;
        o[0] = pal[sel][0]; o[1] = pal[sel][1]; o[2] = pal[sel][2]; o[3] = pal[sel][3];
        return;
    }
    }
    o[0] = 255; o[1] = 0; o[2] = 255; o[3] = 255;
}

/* Decodifica la textura entera a RGBA8 recorriendo los bloques (mucho mas rapido que
 * llamar a texel() por cada pixel: las paletas y los colores de CMPR se calculan una vez
 * por bloque). Mismo resultado que texel() (RL_TEX_CHECK=1 lo compara). */
static void put_px(uint8_t* out, int w, int h, int x, int y, const uint8_t c[4]) {
    if (x < w && y < h) memcpy(out + ((size_t)y * w + x) * 4, c, 4);
}
void texture_decode_rgba(const TexInfo* t, uint8_t* out) {
    static const int bw[16] = { 8, 8, 8, 4, 4, 4, 4, 0, 8, 8, 4, 0, 0, 0, 8, 0 };
    static const int bh[16] = { 8, 4, 4, 4, 4, 4, 4, 0, 8, 4, 4, 0, 0, 0, 8, 0 };
    int f = t->fmt, w = t->w, h = t->h;
    const uint8_t* d = t->data;
    int bwid, bhgt, nbx, nby, bx, by, ix, iy;
    uint8_t pal[256][4];
    if (!d || !bw[f] || f == 10) {          /* sin datos, formato raro o C14X2: el camino lento */
        int x, y;
        for (y = 0; y < h; y++)
            for (x = 0; x < w; x++) texel(t, x, y, out + ((size_t)y * w + x) * 4);
        return;
    }
    bwid = bw[f]; bhgt = bh[f];
    nbx = (w + bwid - 1) / bwid; nby = (h + bhgt - 1) / bhgt;
    if (f == 8 || f == 9) {
        int n = f == 8 ? 16 : 256, k;
        for (k = 0; k < n; k++) tlut_color(t->tlut, t->tlut_fmt, (uint32_t)k, pal[k]);
    }
    for (by = 0; by < nby; by++)
        for (bx = 0; bx < nbx; bx++) {
            const uint8_t* b = d + (size_t)(by * nbx + bx) * (f == 6 ? 64 : 32);
            int x0 = bx * bwid, y0 = by * bhgt;
            uint8_t c[4];
            switch (f) {
            case 0:
                for (iy = 0; iy < 8; iy++) for (ix = 0; ix < 8; ix++) {
                    uint8_t v = b[iy * 4 + ix / 2]; v = (ix & 1) ? (v & 15) : (v >> 4);
                    c[0] = c[1] = c[2] = c[3] = (uint8_t)(v * 17); put_px(out, w, h, x0 + ix, y0 + iy, c);
                }
                break;
            case 1:
                for (iy = 0; iy < 4; iy++) for (ix = 0; ix < 8; ix++) {
                    c[0] = c[1] = c[2] = c[3] = b[iy * 8 + ix]; put_px(out, w, h, x0 + ix, y0 + iy, c);
                }
                break;
            case 2:
                for (iy = 0; iy < 4; iy++) for (ix = 0; ix < 8; ix++) {
                    uint8_t v = b[iy * 8 + ix];
                    c[0] = c[1] = c[2] = (uint8_t)((v & 15) * 17); c[3] = (uint8_t)((v >> 4) * 17); put_px(out, w, h, x0 + ix, y0 + iy, c);
                }
                break;
            case 3:
                for (iy = 0; iy < 4; iy++) for (ix = 0; ix < 4; ix++) {
                    const uint8_t* q = b + (iy * 4 + ix) * 2;
                    c[0] = c[1] = c[2] = q[1]; c[3] = q[0]; put_px(out, w, h, x0 + ix, y0 + iy, c);
                }
                break;
            case 4:
                for (iy = 0; iy < 4; iy++) for (ix = 0; ix < 4; ix++) {
                    read_color(b + (iy * 4 + ix) * 2, 0, c); put_px(out, w, h, x0 + ix, y0 + iy, c);
                }
                break;
            case 5:
                for (iy = 0; iy < 4; iy++) for (ix = 0; ix < 4; ix++) {
                    const uint8_t* q = b + (iy * 4 + ix) * 2;
                    uint16_t v = (uint16_t)((q[0] << 8) | q[1]);
                    if (v & 0x8000) { c[0] = (uint8_t)(((v >> 10) & 31) * 255 / 31); c[1] = (uint8_t)(((v >> 5) & 31) * 255 / 31); c[2] = (uint8_t)((v & 31) * 255 / 31); c[3] = 255; }
                    else { c[0] = (uint8_t)(((v >> 8) & 15) * 17); c[1] = (uint8_t)(((v >> 4) & 15) * 17); c[2] = (uint8_t)((v & 15) * 17); c[3] = (uint8_t)(((v >> 12) & 7) * 255 / 7); }
                    put_px(out, w, h, x0 + ix, y0 + iy, c);
                }
                break;
            case 6:
                for (iy = 0; iy < 4; iy++) for (ix = 0; ix < 4; ix++) {
                    int k = (iy * 4 + ix) * 2;
                    c[3] = b[k]; c[0] = b[k + 1]; c[1] = b[32 + k]; c[2] = b[32 + k + 1]; put_px(out, w, h, x0 + ix, y0 + iy, c);
                }
                break;
            case 8:
                for (iy = 0; iy < 8; iy++) for (ix = 0; ix < 8; ix++) {
                    uint8_t v = b[iy * 4 + ix / 2];
                    put_px(out, w, h, x0 + ix, y0 + iy, pal[(ix & 1) ? (v & 15) : (v >> 4)]);
                }
                break;
            case 9:
                for (iy = 0; iy < 4; iy++) for (ix = 0; ix < 8; ix++) put_px(out, w, h, x0 + ix, y0 + iy, pal[b[iy * 8 + ix]]);
                break;
            case 14: {
                int sb;
                for (sb = 0; sb < 4; sb++) {
                    const uint8_t* s4 = b + sb * 8;
                    int sx = x0 + (sb & 1) * 4, sy = y0 + (sb >> 1) * 4, k;
                    uint16_t c0 = (uint16_t)((s4[0] << 8) | s4[1]), c1 = (uint16_t)((s4[2] << 8) | s4[3]);
                    uint8_t p0[4], p1[4], cp[4][4];
                    read_color(s4, 0, p0); read_color(s4 + 2, 0, p1);
                    for (k = 0; k < 4; k++) { cp[0][k] = p0[k]; cp[1][k] = p1[k]; }
                    if (c0 > c1) {
                        for (k = 0; k < 3; k++) { cp[2][k] = (uint8_t)((2 * p0[k] + p1[k]) / 3); cp[3][k] = (uint8_t)((p0[k] + 2 * p1[k]) / 3); }
                        cp[2][3] = cp[3][3] = 255;
                    } else {
                        for (k = 0; k < 3; k++) { cp[2][k] = (uint8_t)((p0[k] + p1[k]) / 2); cp[3][k] = cp[2][k]; }
                        cp[2][3] = 255; cp[3][3] = 0;
                    }
                    for (iy = 0; iy < 4; iy++) for (ix = 0; ix < 4; ix++)
                        put_px(out, w, h, sx + ix, sy + iy, cp[(s4[4 + iy] >> (6 - 2 * ix)) & 3]);
                }
                break;
            }
            }
        }
    {
        static int check = -1;
        if (check < 0) check = getenv("RL_TEX_CHECK") != NULL;
        if (check) {
            static int bad;
            int x, y;
            for (y = 0; y < h; y++) for (x = 0; x < w; x++) {
                uint8_t r[4];
                texel(t, x, y, r);
                if (memcmp(r, out + ((size_t)y * w + x) * 4, 4) && bad++ < 20)
                    rt_log("[tex] diferencia fmt %d en %d,%d\n", f, x, y);
            }
        }
    }
}

static int wrap(int v, int n, int mode) {
    if (mode == 0) return v < 0 ? 0 : v >= n ? n - 1 : v;      /* clamp */
    if (mode == 2) {                                           /* espejo */
        int p = n * 2, m = ((v % p) + p) % p;
        return m < n ? m : p - 1 - m;
    }
    return ((v % n) + n) % n;                                  /* repetir */
}

static void sample(const TexInfo* t, float s, float tt, uint8_t o[4]) {
    float u = s * t->w - 0.5f, v = tt * t->h - 0.5f;
    if (!t->mag_linear) {
        texel(t, wrap((int)floorf(u + 0.5f), t->w, t->wrap_s), wrap((int)floorf(v + 0.5f), t->h, t->wrap_t), o);
        return;
    } else {
        int x0 = (int)floorf(u), y0 = (int)floorf(v), k;
        float fx = u - x0, fy = v - y0;
        uint8_t a[4], b[4], c[4], d[4];
        int xa = wrap(x0, t->w, t->wrap_s), xb = wrap(x0 + 1, t->w, t->wrap_s);
        int ya = wrap(y0, t->h, t->wrap_t), yb = wrap(y0 + 1, t->h, t->wrap_t);
        texel(t, xa, ya, a); texel(t, xb, ya, b); texel(t, xa, yb, c); texel(t, xb, yb, d);
        for (k = 0; k < 4; k++) {
            float top = a[k] + (b[k] - a[k]) * fx, bot = c[k] + (d[k] - c[k]) * fx;
            o[k] = clamp8((int)(top + (bot - top) * fy + 0.5f));
        }
    }
}

/* ---- TEV --------------------------------------------------------------- */

typedef struct { int r, g, b, a; } C4i;

static C4i tev_pair(uint32_t ra, uint32_t bg) {
    C4i c;
    c.r = (int)(ra & 0x7FF); if (c.r & 0x400) c.r -= 0x800;
    c.a = (int)((ra >> 12) & 0x7FF); if (c.a & 0x400) c.a -= 0x800;
    c.b = (int)(bg & 0x7FF); if (c.b & 0x400) c.b -= 0x800;
    c.g = (int)((bg >> 12) & 0x7FF); if (c.g & 0x400) c.g -= 0x800;
    return c;
}

/* registros de color 0xE0-0xE7: pares (RA, BG) para PREV, C0, C1, C2 */
static C4i tev_reg_color(int i) { return tev_pair(gx.tev_reg[i][0], gx.tev_reg[i][1]); }

static C4i s_konst[4];

static void load_konst(void) {
    int i;
    for (i = 0; i < 4; i++) {
        /* los registros konstantes comparten direccion (0xE0..) con el bit 23 encendido */
        s_konst[i] = tev_pair(gx.tev_konst[i][0], gx.tev_konst[i][1]);
    }
}

static int kcolor_sel(int sel, int comp, const C4i* k) {
    /* 0..7: constantes 1, 7/8, 3/4, 5/8, 1/2, 3/8, 1/4, 1/8 */
    static const int frac[8] = { 255, 223, 191, 159, 128, 96, 64, 32 };
    if (sel < 8) return frac[sel];
    if (sel >= 12 && sel <= 15) { const C4i* c = &k[sel - 12]; return comp == 0 ? c->r : comp == 1 ? c->g : comp == 2 ? c->b : c->a; }
    if (sel >= 16) {
        int kk = (sel - 16) & 3, which = (sel - 16) >> 2;   /* K0_R, K1_R.. K0_G.. */
        const C4i* c = &k[kk];
        return which == 0 ? c->r : which == 1 ? c->g : which == 2 ? c->b : c->a;
    }
    return 255;
}

typedef struct {
    int ntex;
    TexInfo tex[8];
} DrawCtx;

static int s_tev_swap[4][4];

static void load_swap_tables(void) {
    int i;
    for (i = 0; i < 4; i++) {
        uint32_t a = bp(0xF6 + 2 * i), b = bp(0xF7 + 2 * i);
        s_tev_swap[i][0] = a & 3;
        s_tev_swap[i][1] = (a >> 2) & 3;
        s_tev_swap[i][2] = b & 3;
        s_tev_swap[i][3] = (b >> 2) & 3;
    }
}

static void swizzle(const int in[4], int table, int out[4]) {
    int k;
    for (k = 0; k < 4; k++) out[k] = in[s_tev_swap[table][k]];
}

/* Calcula el color final de un pixel. Devuelve 0 si la comparacion de alfa lo descarta. */
static int shade(const DrawCtx* dc, const float col[2][4], const float tc[8][2], int out[4]) {
    int nstages = (int)((bp(0x00) >> 10) & 15) + 1;
    int regs[4][4];
    int s, k;
    for (k = 0; k < 4; k++) {
        C4i c = tev_reg_color(k);
        regs[k][0] = c.r; regs[k][1] = c.g; regs[k][2] = c.b; regs[k][3] = c.a;
    }
    for (s = 0; s < nstages; s++) {
        uint32_t order = bp(0x28 + s / 2) >> ((s & 1) * 12);
        int texmap = order & 7, texcoord = (order >> 3) & 7, texen = (order >> 6) & 1, chan = (order >> 7) & 7;
        uint32_t cenv = bp(0xC0 + 2 * s), aenv = bp(0xC1 + 2 * s);
        uint32_t ksel = bp(0xF6 + s / 2);
        int kc = (int)((ksel >> ((s & 1) ? 14 : 4)) & 31), ka = (int)((ksel >> ((s & 1) ? 19 : 9)) & 31);
        int texc[4] = { 255, 255, 255, 255 }, rasc[4] = { 0, 0, 0, 0 }, tmp[4];
        int ras_swap = (int)(aenv & 3), tex_swap = (int)((aenv >> 2) & 3);
        int a[4], b[4], c[4], d[4], res[4];
        int i;
        if (texen && texmap < dc->ntex + 8) {
            uint8_t t[4];
            sample(&dc->tex[texmap], tc[texcoord][0], tc[texcoord][1], t);
            tmp[0] = t[0]; tmp[1] = t[1]; tmp[2] = t[2]; tmp[3] = t[3];
            swizzle(tmp, tex_swap, texc);
        }
        if (chan == 0 || chan == 1) {
            tmp[0] = (int)col[chan][0]; tmp[1] = (int)col[chan][1]; tmp[2] = (int)col[chan][2]; tmp[3] = (int)col[chan][3];
            swizzle(tmp, ras_swap, rasc);
        } else if (chan == 5 || chan == 6) {  /* alfa bump / alfa normalizado */
            rasc[0] = rasc[1] = rasc[2] = rasc[3] = 0;
        }
        /* entradas de color */
        for (i = 0; i < 4; i++) {
            int sel = (int)((cenv >> (12 - 4 * i)) & 15), v[3];
            int ch;
            for (ch = 0; ch < 3; ch++) {
                switch (sel) {
                case 0: v[ch] = regs[0][ch]; break;
                case 1: v[ch] = regs[0][3]; break;
                case 2: v[ch] = regs[1][ch]; break;
                case 3: v[ch] = regs[1][3]; break;
                case 4: v[ch] = regs[2][ch]; break;
                case 5: v[ch] = regs[2][3]; break;
                case 6: v[ch] = regs[3][ch]; break;
                case 7: v[ch] = regs[3][3]; break;
                case 8: v[ch] = texc[ch]; break;
                case 9: v[ch] = texc[3]; break;
                case 10: v[ch] = rasc[ch]; break;
                case 11: v[ch] = rasc[3]; break;
                case 12: v[ch] = 255; break;
                case 13: v[ch] = 128; break;
                case 14: v[ch] = kcolor_sel(kc, ch, s_konst); break;
                default: v[ch] = 0; break;
                }
            }
            if (i == 0) { a[0] = v[0]; a[1] = v[1]; a[2] = v[2]; }
            else if (i == 1) { b[0] = v[0]; b[1] = v[1]; b[2] = v[2]; }
            else if (i == 2) { c[0] = v[0]; c[1] = v[1]; c[2] = v[2]; }
            else { d[0] = v[0]; d[1] = v[1]; d[2] = v[2]; }
        }
        /* entradas de alfa */
        for (i = 0; i < 4; i++) {
            int sel = (int)((aenv >> (13 - 3 * i)) & 7), v;
            switch (sel) {
            case 0: v = regs[0][3]; break;
            case 1: v = regs[1][3]; break;
            case 2: v = regs[2][3]; break;
            case 3: v = regs[3][3]; break;
            case 4: v = texc[3]; break;
            case 5: v = rasc[3]; break;
            case 6: v = kcolor_sel(ka, 3, s_konst); break;
            default: v = 0; break;
            }
            if (i == 0) a[3] = v; else if (i == 1) b[3] = v; else if (i == 2) c[3] = v; else d[3] = v;
        }
        /* operacion: (d + lerp(a, b, c) +/- bias) * escala, o comparaciones */
        for (k = 0; k < 4; k++) {
            uint32_t env = k < 3 ? cenv : aenv;
            int bias = (int)((env >> 16) & 3), sub = (int)((env >> 18) & 1), clampv = (int)((env >> 19) & 1);
            int scale = (int)((env >> 20) & 3), dest = (int)((env >> 22) & 3);
            int va = a[k] & 255, vb = b[k] & 255, vc = c[k] & 255, vd = d[k];
            int r;
            if (bias == 3) {                      /* modos de comparacion */
                int op = (scale << 1) | sub;
                int cmp;
                switch (op) {
                case 0: cmp = (a[0] & 255) > (b[0] & 255); break;  /* R8 GT */
                case 1: cmp = (a[0] & 255) == (b[0] & 255); break;
                case 2: cmp = ((a[0] & 255) | ((a[1] & 255) << 8)) > ((b[0] & 255) | ((b[1] & 255) << 8)); break;
                case 3: cmp = ((a[0] & 255) | ((a[1] & 255) << 8)) == ((b[0] & 255) | ((b[1] & 255) << 8)); break;
                case 4: cmp = ((a[0] & 255) | ((a[1] & 255) << 8) | ((a[2] & 255) << 16)) > ((b[0] & 255) | ((b[1] & 255) << 8) | ((b[2] & 255) << 16)); break;
                case 5: cmp = ((a[0] & 255) | ((a[1] & 255) << 8) | ((a[2] & 255) << 16)) == ((b[0] & 255) | ((b[1] & 255) << 8) | ((b[2] & 255) << 16)); break;
                case 6: cmp = va > vb; break;      /* por componente */
                default: cmp = va == vb; break;
                }
                r = vd + (cmp ? vc : 0);
            } else {
                int lerp = (va * (256 - (vc + (vc >> 7))) + vb * (vc + (vc >> 7))) >> 8;
                r = sub ? vd - lerp : vd + lerp;
                if (bias == 1) r += 128; else if (bias == 2) r -= 128;
                if (scale == 1) r *= 2; else if (scale == 2) r *= 4; else if (scale == 3) r /= 2;
            }
            if (clampv) r = r < 0 ? 0 : r > 255 ? 255 : r;
            else r = r < -1024 ? -1024 : r > 1023 ? 1023 : r;
            res[k] = r;
            (void)dest;
        }
        {
            int dc_ = (int)((cenv >> 22) & 3), da = (int)((aenv >> 22) & 3);
            regs[dc_][0] = res[0]; regs[dc_][1] = res[1]; regs[dc_][2] = res[2];
            regs[da][3] = res[3];
        }
        if (s == nstages - 1) {
            int dc_ = (int)((cenv >> 22) & 3), da = (int)((aenv >> 22) & 3);
            out[0] = clamp8(regs[dc_][0]); out[1] = clamp8(regs[dc_][1]); out[2] = clamp8(regs[dc_][2]); out[3] = clamp8(regs[da][3]);
        }
    }
    /* comparacion de alfa (BP 0xF3) */
    {
        uint32_t ac = bp(0xF3);
        int r0 = (int)(ac & 255), r1 = (int)((ac >> 8) & 255);
        int f0 = (int)((ac >> 16) & 7), f1 = (int)((ac >> 19) & 7), logic = (int)((ac >> 22) & 3);
        int al = out[3], p0, p1, pass;
        int fns[2] = { f0, f1 }, refs[2] = { r0, r1 }, res2[2], q;
        for (q = 0; q < 2; q++) {
            int fv = fns[q], rv = refs[q];
            res2[q] = fv == 0 ? 0 : fv == 1 ? al < rv : fv == 2 ? al == rv : fv == 3 ? al <= rv : fv == 4 ? al > rv : fv == 5 ? al != rv : fv == 6 ? al >= rv : 1;
        }
        p0 = res2[0]; p1 = res2[1];
        pass = logic == 0 ? (p0 && p1) : logic == 1 ? (p0 || p1) : logic == 2 ? (p0 != p1) : (p0 == p1);
        if (!pass) return 0;
    }
    return 1;
}

/* ---- mezcla y escritura del pixel ------------------------------------------- */

static int blend_factor(int f, int is_src, const int src[4], const uint8_t dst[4], int comp) {
    switch (f) {
    case 0: return 0;
    case 1: return 255;
    case 2: return is_src ? dst[comp] : src[comp];       /* DSTCLR / SRCCLR */
    case 3: return 255 - (is_src ? dst[comp] : src[comp]);
    case 4: return src[3];
    case 5: return 255 - src[3];
    case 6: return dst[3];
    default: return 255 - dst[3];
    }
}

static void write_pixel(int x, int y, uint32_t z, const int c[4]) {
    uint32_t zmode = bp(0x40), cmode = bp(0x41), cmode1 = bp(0x42);
    int idx = y * EFB_W + x;
    uint8_t* d = s_efb + idx * 4;
    int out[4], k;
    if (zmode & 1) {
        int fn = (int)((zmode >> 1) & 7);
        uint32_t cur = s_zbuf[idx];
        int pass = fn == 0 ? 0 : fn == 1 ? z < cur : fn == 2 ? z == cur : fn == 3 ? z <= cur : fn == 4 ? z > cur : fn == 5 ? z != cur : fn == 6 ? z >= cur : 1;
        if (!pass) return;
        if (zmode & 0x10) s_zbuf[idx] = z;
    }
    if (cmode & 1) {                                     /* mezcla */
        int sf = (int)((cmode >> 8) & 7), df = (int)((cmode >> 5) & 7), sub = (int)((cmode >> 11) & 1);
        for (k = 0; k < 4; k++) {
            if (sub) out[k] = d[k] - c[k];
            else out[k] = (c[k] * blend_factor(sf, 1, c, d, k) + d[k] * blend_factor(df, 0, c, d, k)) / 255;
        }
    } else {
        for (k = 0; k < 4; k++) out[k] = c[k];
    }
    if (cmode & 8) { d[0] = clamp8(out[0]); d[1] = clamp8(out[1]); d[2] = clamp8(out[2]); }   /* escribir color */
    if (cmode & 0x10) d[3] = (cmode1 & 0x100) ? (uint8_t)(cmode1 & 255) : clamp8(out[3]);       /* escribir alfa */
}

/* ---- triangulos ------------------------------------------------------------ */

static void to_screen(Vtx* v) {
    /* El viewport y la tijera de GX llevan sumado un desplazamiento de 342; el
     * desplazamiento de la tijera (BP 0x59) mueve el origen del EFB. */
    float iw = v->w != 0 ? 1.0f / v->w : 1.0f;
    uint32_t so = bp(0x59);
    float offx = (float)((int)((so & 0x3FF) * 2) - 342), offy = (float)((int)(((so >> 10) & 0x3FF) * 2) - 342);
    v->sx = v->x * iw * xf_rf(0x1A) + xf_rf(0x1D) - 342.0f - offx;
    v->sy = v->y * iw * xf_rf(0x1B) + xf_rf(0x1E) - 342.0f - offy;
    v->sz = v->z * iw * xf_rf(0x1C) + xf_rf(0x1F);
}

static DrawCtx s_dc;

static void raster_tri(Vtx* a, Vtx* b, Vtx* c) {
    float area, minx, maxx, miny, maxy;
    int x0, x1, y0, y1, x, y, i, k;
    uint32_t sc0 = bp(0x20), sc1 = bp(0x21), so = bp(0x59);
    int scx0, scy0, scx1, scy1, offx, offy;
    int cull = (int)((bp(0x00) >> 14) & 3);
    if (a->w <= 0 || b->w <= 0 || c->w <= 0) return;      /* sin recorte contra el plano cercano por ahora */
    area = (b->sx - a->sx) * (c->sy - a->sy) - (c->sx - a->sx) * (b->sy - a->sy);
    if (area == 0) return;
    /* GX: CULL_FRONT=1 descarta los de frente (horario en pantalla), CULL_BACK=2 los de atras */
    if (cull == 3) return;
    if (cull == 2 && area > 0) return;
    if (cull == 1 && area < 0) return;
    offx = (int)((so & 0x3FF) * 2) - 342; offy = (int)(((so >> 10) & 0x3FF) * 2) - 342;
    scy0 = (int)(sc0 & 0x7FF) - 342 - offy; scx0 = (int)((sc0 >> 12) & 0x7FF) - 342 - offx;
    scy1 = (int)(sc1 & 0x7FF) - 342 - offy; scx1 = (int)((sc1 >> 12) & 0x7FF) - 342 - offx;
    minx = fminf(a->sx, fminf(b->sx, c->sx)); maxx = fmaxf(a->sx, fmaxf(b->sx, c->sx));
    miny = fminf(a->sy, fminf(b->sy, c->sy)); maxy = fmaxf(a->sy, fmaxf(b->sy, c->sy));
    x0 = (int)floorf(minx); x1 = (int)ceilf(maxx); y0 = (int)floorf(miny); y1 = (int)ceilf(maxy);
    if (x0 < scx0) x0 = scx0; if (y0 < scy0) y0 = scy0;
    if (x1 > scx1) x1 = scx1; if (y1 > scy1) y1 = scy1;
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0;
    if (x1 >= EFB_W) x1 = EFB_W - 1; if (y1 >= EFB_H) y1 = EFB_H - 1;
    if (x0 > x1 || y0 > y1) return;
    {
        float ia = 1.0f / area;
        float wa = 1.0f / a->w, wb = 1.0f / b->w, wc = 1.0f / c->w;
        uint32_t ntex = gx.xf_regs[0x3F] & 15;
        for (y = y0; y <= y1; y++) {
            float py = y + 0.5f;
            for (x = x0; x <= x1; x++) {
                float px = x + 0.5f;
                float l0 = ((b->sx - px) * (c->sy - py) - (c->sx - px) * (b->sy - py)) * ia;
                float l1 = ((c->sx - px) * (a->sy - py) - (a->sx - px) * (c->sy - py)) * ia;
                float l2 = 1.0f - l0 - l1;
                float pw, col[2][4], tc[8][2], zf;
                int out[4];
                uint32_t z;
                if (l0 < 0 || l1 < 0 || l2 < 0) continue;
                pw = l0 * wa + l1 * wb + l2 * wc;
                pw = pw != 0 ? 1.0f / pw : 0;
                for (i = 0; i < 2; i++)
                    for (k = 0; k < 4; k++)
                        col[i][k] = (l0 * a->col[i][k] * wa + l1 * b->col[i][k] * wb + l2 * c->col[i][k] * wc) * pw;
                for (i = 0; i < (int)ntex && i < 8; i++) {
                    float q = (l0 * a->tex[i][2] * wa + l1 * b->tex[i][2] * wb + l2 * c->tex[i][2] * wc) * pw;
                    float s = (l0 * a->tex[i][0] * wa + l1 * b->tex[i][0] * wb + l2 * c->tex[i][0] * wc) * pw;
                    float t = (l0 * a->tex[i][1] * wa + l1 * b->tex[i][1] * wb + l2 * c->tex[i][1] * wc) * pw;
                    if (q != 0 && q != 1.0f) { s /= q; t /= q; }
                    tc[i][0] = s; tc[i][1] = t;
                }
                for (; i < 8; i++) tc[i][0] = tc[i][1] = 0;
                zf = l0 * a->sz + l1 * b->sz + l2 * c->sz;
                z = zf < 0 ? 0 : zf > 16777215.0f ? 16777215u : (uint32_t)zf;
                if (!shade(&s_dc, (const float(*)[4])col, (const float(*)[2])tc, out)) continue;
                write_pixel(x, y, z, out);
            }
        }
    }
}

/* ---- recorte contra el plano cercano --------------------------------------
 * Un triangulo que cruza el plano de la camara (w <= 0) se recorta en el
 * espacio de recorte antes de proyectarlo; sin esto desaparecen el piso y las
 * paredes que pasan pegadas a la camara. */
#define NEAR_W 1e-5f

static void vtx_lerp(const Vtx* a, const Vtx* b, float t, Vtx* o) {
    const float* pa = (const float*)a;
    const float* pb = (const float*)b;
    float* po = (float*)o;
    size_t i, n = sizeof(Vtx) / sizeof(float);
    for (i = 0; i < n; i++) po[i] = pa[i] + (pb[i] - pa[i]) * t;
}

/* Distancia al plano cercano de GX: en el espacio de recorte lo visible es -w <= z <= 0. */
static float near_dist(const Vtx* v) { return v->z + v->w; }

static void draw_tri(Vtx* a, Vtx* b, Vtx* c) {
    Vtx in[3], out[6];
    int m = 0, i;
    float da = near_dist(a), db = near_dist(b), dc = near_dist(c);
    if (da >= 0 && db >= 0 && dc >= 0 && a->w > NEAR_W && b->w > NEAR_W && c->w > NEAR_W) { raster_tri(a, b, c); return; }
    if (da < 0 && db < 0 && dc < 0) return;
    in[0] = *a; in[1] = *b; in[2] = *c;
    for (i = 0; i < 3; i++) {
        const Vtx* p = &in[i];
        const Vtx* q = &in[(i + 1) % 3];
        float dp = near_dist(p), dq = near_dist(q);
        if (dp >= 0) out[m++] = *p;
        if ((dp >= 0) != (dq >= 0)) {
            vtx_lerp(p, q, dp / (dp - dq), &out[m]);
            m++;
        }
    }
    for (i = 0; i < m; i++) {
        if (out[i].w <= NEAR_W) return;                 /* no deberia pasar con una proyeccion normal */
        to_screen(&out[i]);
    }
    for (i = 1; i + 1 < m; i++) raster_tri(&out[0], &out[i], &out[i + 1]);
}

/* Lineas y puntos: se dibujan como rectangulos del ancho de BP 0x22 (en sextos de pixel). */
static void draw_quad_screen(const Vtx* a, const Vtx* b, float dx, float dy) {
    Vtx q[4];
    q[0] = *a; q[1] = *a; q[2] = *b; q[3] = *b;
    q[0].sx -= dx; q[0].sy -= dy; q[1].sx += dx; q[1].sy += dy;
    q[2].sx += dx; q[2].sy += dy; q[3].sx -= dx; q[3].sy -= dy;
    {
        uint32_t saved = gx.bp_regs[0x00];
        gx.bp_regs[0x00] &= ~(3u << 14);                  /* sin descarte de caras */
        raster_tri(&q[0], &q[1], &q[2]);
        raster_tri(&q[0], &q[2], &q[3]);
        gx.bp_regs[0x00] = saved;
    }
}

static void draw_line(const Vtx* a, const Vtx* b) {
    float wdt = (float)(bp(0x22) & 0xFF) / 6.0f, h = wdt * 0.5f;
    float ddx, ddy;
    if (a->w <= NEAR_W || b->w <= NEAR_W) return;
    if (h < 0.5f) h = 0.5f;
    ddx = fabsf(b->sx - a->sx); ddy = fabsf(b->sy - a->sy);
    if (ddx > ddy) draw_quad_screen(a, b, 0, h);      /* linea mas horizontal: se ensancha en y */
    else draw_quad_screen(a, b, h, 0);
}

static void draw_point(const Vtx* a) {
    float sz = (float)((bp(0x22) >> 8) & 0xFF) / 6.0f, h = sz * 0.5f;
    Vtx l = *a, r = *a;
    if (a->w <= NEAR_W) return;
    if (h < 0.5f) h = 0.5f;
    l.sx -= h; r.sx += h;
    draw_quad_screen(&l, &r, 0, h);
}

void gx_render_draw(int prim, int vat, int count, const uint8_t* data, uint32_t vsize) {
    GXVtxLayout L;
    Vtx* v;
    int i;
    if (!render_enabled || count <= 0) return;
    if (render_gl) { gl_render_draw(prim, vat, count, data, vsize); return; }
    gx_vertex_layout(vat, &L);
    v = (Vtx*)malloc(sizeof(Vtx) * (size_t)count);
    for (i = 0; i < count; i++) {
        decode_vertex(&L, data + (size_t)i * vsize, &v[i]);
        if (v[i].w > NEAR_W) to_screen(&v[i]);
    }
    s_dc.ntex = 0;
    for (i = 0; i < 8; i++) tex_info(i, &s_dc.tex[i]);
    load_konst();
    load_swap_tables();
    switch (prim) {
    case 0: case 1:                                      /* quads */
        for (i = 0; i + 3 < count; i += 4) { draw_tri(&v[i], &v[i + 1], &v[i + 2]); draw_tri(&v[i], &v[i + 2], &v[i + 3]); }
        break;
    case 2:                                              /* triangulos */
        for (i = 0; i + 2 < count; i += 3) draw_tri(&v[i], &v[i + 1], &v[i + 2]);
        break;
    case 3:                                              /* tira */
        for (i = 0; i + 2 < count; i++) {
            if (i & 1) draw_tri(&v[i + 1], &v[i], &v[i + 2]);
            else draw_tri(&v[i], &v[i + 1], &v[i + 2]);
        }
        break;
    case 4:                                              /* abanico */
        for (i = 1; i + 1 < count; i++) draw_tri(&v[0], &v[i], &v[i + 1]);
        break;
    case 5:                                              /* lineas */
        for (i = 0; i + 1 < count; i += 2) draw_line(&v[i], &v[i + 1]);
        break;
    case 6:                                              /* tira de lineas */
        for (i = 0; i + 1 < count; i++) draw_line(&v[i], &v[i + 1]);
        break;
    case 7:                                              /* puntos */
        for (i = 0; i < count; i++) draw_point(&v[i]);
        break;
    default:
        break;
    }
    free(v);
}

/* ---- carga de paletas a TMEM (BP 0x64/0x65) --------------------------------- */

void gx_render_bp(int reg, uint32_t v) {
    if (reg == 0x65) {
        uint32_t src = ((bp(0x64) & 0x1FFFFF) << 5) | 0x80000000u;
        uint32_t tmem = (v & 0x3FF) << 9;
        uint32_t bytes = ((v >> 10) & 0x7FF) * 32;
        const uint8_t* p = mem_ptr(src);
        if (p && tmem + bytes <= sizeof s_tmem) memcpy(s_tmem + tmem, p, bytes);
    }
}

/* ---- copias del EFB --------------------------------------------------------- */

uint32_t video_xfb_stride;

/* ---- copia del EFB a una textura ------------------------------------------
 * El juego usa esto para efectos (brillos, sombras, pantallas reducidas).
 * Formato destino = ((fmt >> 1) | ((fmt & 1) << 3)) con fmt = bits 3..6 de BP 0x52;
 * bit 15 = convertir a intensidad (luma), bit 9 = reducir a la mitad. */
static void efb_src(int x, int y, int zsrc, uint8_t o[4]) {
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= EFB_W) x = EFB_W - 1;
    if (y >= EFB_H) y = EFB_H - 1;
    if (zsrc) {
        uint32_t z = s_zbuf[y * EFB_W + x];
        o[0] = (uint8_t)(z >> 16); o[1] = (uint8_t)(z >> 8); o[2] = (uint8_t)z; o[3] = 0xFF;
    } else {
        const uint8_t* p = s_efb + (y * EFB_W + x) * 4;
        o[0] = p[0]; o[1] = p[1]; o[2] = p[2]; o[3] = p[3];
    }
}

static void efb_copy_to_texture(uint32_t cmd, int x0, int y0, int w, int h) {
    uint32_t dst = (gx.bp_regs[0x4B] << 5) & 0x01FFFFFFu;
    uint32_t stride = (gx.bp_regs[0x4D] & 0x3FF) << 5;    /* bytes por fila de bloques */
    int fmt = (int)((cmd >> 3) & 0xF);
    int tf = (fmt >> 1) | ((fmt & 1) << 3);
    int intensity = (cmd >> 15) & 1;
    int half = (cmd >> 9) & 1;
    int zsrc = (gx.bp_regs[0x43] & 7) == 3;              /* EFB en modo Z24: se copia la profundidad */
    int ow = half ? w / 2 : w, oh = half ? h / 2 : h;
    int bw, bh, bpp;                                      /* tamano del bloque y bits por texel */
    int bx, by, x, y;
    uint8_t* base;
    gx.stats.tex_copies++;
    switch (tf) {
    case 0: bw = 8; bh = 8; bpp = 4; break;              /* I4 / R4 */
    case 1: case 2: case 7: case 8: case 9: case 10: bw = 8; bh = 4; bpp = 8; break;  /* I8, IA4, A8, R8, G8, B8 (como Dolphin: EFBCopyFormat) */
    case 6: bw = 4; bh = 4; bpp = 32; break;             /* RGBA8 */
    case 11: case 12: case 3: case 4: case 5: default: bw = 4; bh = 4; bpp = 16; break;
    }
    if (tf == 2 && !intensity) { bw = 8; bh = 4; bpp = 8; }
    base = mem_ptr(0x80000000u | dst);
    if (!base || !stride) return;
    for (by = 0; by < oh; by += bh)
        for (bx = 0; bx < ow; bx += bw) {
            uint8_t* blk = base + (uint32_t)(by / bh) * stride + (uint32_t)(bx / bw) * (bpp == 32 ? 64u : 32u);
            if (!mem_ptr(0x80000000u | (dst + (uint32_t)(blk - base) + (bpp == 32 ? 63u : 31u)))) return;
            for (y = 0; y < bh; y++)
                for (x = 0; x < bw; x++) {
                    uint8_t c[4];
                    int i = y * bw + x, Y;
                    int sx = bx + x, sy = by + y;
                    if (half) {
                        uint8_t q[4][4];
                        int k;
                        efb_src(x0 + sx * 2, y0 + sy * 2, zsrc, q[0]);
                        efb_src(x0 + sx * 2 + 1, y0 + sy * 2, zsrc, q[1]);
                        efb_src(x0 + sx * 2, y0 + sy * 2 + 1, zsrc, q[2]);
                        efb_src(x0 + sx * 2 + 1, y0 + sy * 2 + 1, zsrc, q[3]);
                        for (k = 0; k < 4; k++) c[k] = (uint8_t)((q[0][k] + q[1][k] + q[2][k] + q[3][k] + 2) / 4);
                    } else {
                        efb_src(x0 + sx, y0 + sy, zsrc, c);
                    }
                    if (sx >= ow || sy >= oh) c[0] = c[1] = c[2] = c[3] = 0;
                    Y = intensity && !zsrc ? clamp8(((66 * c[0] + 129 * c[1] + 25 * c[2] + 128) >> 8) + 16) : c[0];
                    switch (tf) {
                    case 0: {                             /* I4 / R4 */
                        uint8_t v = (uint8_t)(Y >> 4);
                        if (i & 1) blk[i / 2] = (uint8_t)((blk[i / 2] & 0xF0) | v);
                        else blk[i / 2] = (uint8_t)((blk[i / 2] & 0x0F) | (v << 4));
                        break;
                    }
                    case 1: case 8: blk[i] = (uint8_t)Y; break;          /* I8 / R8 */
                    case 7: blk[i] = c[3]; break;                        /* A8 */
                    case 9: blk[i] = intensity ? (uint8_t)Y : c[1]; break;  /* G8 */
                    case 10: blk[i] = intensity ? (uint8_t)Y : c[2]; break; /* B8 */
                    case 2:                                              /* IA4 / RA4 */
                        blk[i] = (uint8_t)((c[3] & 0xF0) | (Y >> 4)); break;
                    case 3:                                              /* IA8 / RA8 */
                        blk[i * 2] = c[3]; blk[i * 2 + 1] = (uint8_t)Y; break;
                    case 11:                                             /* RG8 */
                        blk[i * 2] = c[0]; blk[i * 2 + 1] = c[1]; break;
                    case 12:                                             /* GB8 */
                        blk[i * 2] = c[1]; blk[i * 2 + 1] = c[2]; break;
                    case 4: {                                            /* RGB565 */
                        uint16_t v = (uint16_t)(((c[0] >> 3) << 11) | ((c[1] >> 2) << 5) | (c[2] >> 3));
                        blk[i * 2] = (uint8_t)(v >> 8); blk[i * 2 + 1] = (uint8_t)v; break;
                    }
                    case 5: {                                            /* RGB5A3 */
                        uint16_t v;
                        if (c[3] >= 0xE0) v = (uint16_t)(0x8000 | ((c[0] >> 3) << 10) | ((c[1] >> 3) << 5) | (c[2] >> 3));
                        else v = (uint16_t)(((c[3] >> 5) << 12) | ((c[0] >> 4) << 8) | ((c[1] >> 4) << 4) | (c[2] >> 4));
                        blk[i * 2] = (uint8_t)(v >> 8); blk[i * 2 + 1] = (uint8_t)v; break;
                    }
                    case 6:                                              /* RGBA8: AR en 32 bytes, GB en los siguientes */
                        blk[i * 2] = c[3]; blk[i * 2 + 1] = c[0];
                        blk[32 + i * 2] = c[1]; blk[32 + i * 2 + 1] = c[2]; break;
                    default:
                        blk[i * 2] = c[0]; blk[i * 2 + 1] = c[1]; break;
                    }
                }
        }
}

/* BP 0x52: copia del EFB. bit 14 = a la XFB; bit 11 = limpiar el EFB despues. */
void gx_render_copy(void) {
    uint32_t cmd = gx.bp_regs[0x52];
    uint32_t tl = gx.bp_regs[0x49], wh = gx.bp_regs[0x4A];
    int x0 = tl & 0x3FF, y0 = (tl >> 10) & 0x3FF;
    int w = (wh & 0x3FF) + 1, h = ((wh >> 10) & 0x3FF) + 1;
    gx.stats.copies++;
    if (render_trace_copies) rt_log("[copia] cmd=%06X src=(%d,%d %dx%d) dst=%08X stride=%X fmt=%X yscale=%X\n", cmd, x0, y0, w, h, gx.bp_regs[0x4B] << 5, gx.bp_regs[0x4D], gx.bp_regs[0x43], gx.bp_regs[0x4E]);
    if (render_gl) { gl_render_copy(cmd, x0, y0, w, h); return; }
    if (cmd & (1u << 14)) {
        uint32_t dst = (gx.bp_regs[0x4B] << 5) | 0x80000000u;
        uint32_t stride = (gx.bp_regs[0x4D] & 0x3FF) << 5;
        int x, y;
        video_xfb_stride = stride;
        memcpy(s_efb_last, s_efb, sizeof s_efb);
        s_xfb_copies++;
        if (render_dump_copies && video_dump_dir && s_xfb_copies % (uint32_t)render_dump_copies == 0) {
            char path[512];
            snprintf(path, sizeof path, "%s/copia_%06u.png", video_dump_dir, s_xfb_copies);
            render_dump_efb(path);
        }
        for (y = 0; y < h && y0 + y < EFB_H; y++) {
            uint8_t* out = mem_ptr(dst + (uint32_t)y * stride);
            if (!out) break;
            for (x = 0; x + 1 < w && x0 + x + 1 < EFB_W; x += 2) {
                const uint8_t* a = s_efb + ((y0 + y) * EFB_W + x0 + x) * 4;
                const uint8_t* b = a + 4;
                int r = (a[0] + b[0]) / 2, g = (a[1] + b[1]) / 2, bl = (a[2] + b[2]) / 2;
                int Y0 = ((66 * a[0] + 129 * a[1] + 25 * a[2] + 128) >> 8) + 16;
                int Y1 = ((66 * b[0] + 129 * b[1] + 25 * b[2] + 128) >> 8) + 16;
                int U = ((-38 * r - 74 * g + 112 * bl + 128) >> 8) + 128;
                int V = ((112 * r - 94 * g - 18 * bl + 128) >> 8) + 128;
                out[x * 2 + 0] = clamp8(Y0); out[x * 2 + 1] = clamp8(U);
                out[x * 2 + 2] = clamp8(Y1); out[x * 2 + 3] = clamp8(V);
            }
        }
    } else {
        efb_copy_to_texture(cmd, x0, y0, w, h);
    }
    if (cmd & (1u << 11)) {
        uint32_t ar = gx.bp_regs[0x4F], gb = gx.bp_regs[0x50];
        uint8_t r = (uint8_t)ar, a = (uint8_t)(ar >> 8), g = (uint8_t)(gb >> 8), b = (uint8_t)gb;
        uint32_t zc = gx.bp_regs[0x51] & 0xFFFFFF;
        int x, y;
        for (y = y0; y < y0 + h && y < EFB_H; y++)
            for (x = x0; x < x0 + w && x < EFB_W; x++) {
                uint8_t* p = s_efb + (y * EFB_W + x) * 4;
                p[0] = r; p[1] = g; p[2] = b; p[3] = a;
                s_zbuf[y * EFB_W + x] = zc;
            }
    }
}

/* Ganchos del hilo grafico (el rasterizador por software no necesita nada). */
void gx_backend_thread_init(void) { if (render_gl) gl_thread_init(); }
void gx_backend_idle(void) { if (render_gl) gl_idle(); }
void render_present(uint32_t xfb_addr) { if (render_gl) gl_present(xfb_addr); }
int render_dump_xfb(const char* path, uint32_t xfb) { return render_gl ? gl_dump_xfb(path, xfb) : 0; }

/* Guarda el EFB actual como PNG (para depurar). */
int video_write_png(const char* path, const uint8_t* rgb, int w, int h);
void render_dump_efb(const char* path) {
    if (render_gl) { gl_dump_efb(path); return; }
    uint8_t* rgb = (uint8_t*)malloc(EFB_W * 480 * 3);
    int i;
    for (i = 0; i < EFB_W * 480; i++) { rgb[i * 3] = s_efb_last[i * 4]; rgb[i * 3 + 1] = s_efb_last[i * 4 + 1]; rgb[i * 3 + 2] = s_efb_last[i * 4 + 2]; }
    video_write_png(path, rgb, EFB_W, 480);
    free(rgb);
}
