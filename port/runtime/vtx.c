/*
 * Decodificacion rapida de vertices de GX.
 *
 * Hace lo mismo que decode_vertex (render.c) pero prepara una sola vez por dibujo todo
 * lo que no cambia entre vertices: matrices convertidas a float, colores de material y
 * ambiente, luces y modos de generacion de coordenadas de textura. decode_vertex queda
 * como referencia (RL_VTX_CHECK=1 compara las dos).
 */
#include <math.h>
#include <string.h>
#include "runtime.h"
#include "render_int.h"
#include "vtx.h"

static float xf_f(uint32_t addr) { return u32_f(gx.xf_mem[addr & 0x7FF]); }
static float xf_rf(uint32_t reg) { return u32_f(gx.xf_regs[reg & 0xFF]); }

static const int s_csize[8] = { 1, 1, 2, 2, 4, 4, 4, 4 };

static inline float read_comp(const uint8_t* p, int fmt, float scale) {
    switch (fmt) {
    case 0: return p[0] * scale;
    case 1: return (int8_t)p[0] * scale;
    case 2: return (uint16_t)((p[0] << 8) | p[1]) * scale;
    case 3: return (int16_t)((p[0] << 8) | p[1]) * scale;
    default: { uint32_t v = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; return u32_f(v); }
    }
}

static inline void read_color(const uint8_t* p, int fmt, float out[4]) {
    switch (fmt) {
    case 0: { uint16_t v = (uint16_t)((p[0] << 8) | p[1]);
        out[0] = (float)(((v >> 11) & 31) * 255 / 31); out[1] = (float)(((v >> 5) & 63) * 255 / 63);
        out[2] = (float)((v & 31) * 255 / 31); out[3] = 255; break; }
    case 1: case 2: out[0] = p[0]; out[1] = p[1]; out[2] = p[2]; out[3] = 255; break;
    case 3: { uint16_t v = (uint16_t)((p[0] << 8) | p[1]);
        out[0] = (float)(((v >> 12) & 15) * 17); out[1] = (float)(((v >> 8) & 15) * 17);
        out[2] = (float)(((v >> 4) & 15) * 17); out[3] = (float)((v & 15) * 17); break; }
    case 4: { uint32_t v = ((uint32_t)p[0] << 16) | (p[1] << 8) | p[2];
        out[0] = (float)(((v >> 18) & 63) * 255 / 63); out[1] = (float)(((v >> 12) & 63) * 255 / 63);
        out[2] = (float)(((v >> 6) & 63) * 255 / 63); out[3] = (float)((v & 63) * 255 / 63); break; }
    default: out[0] = p[0]; out[1] = p[1]; out[2] = p[2]; out[3] = p[3]; break;
    }
}

static inline const uint8_t* attr_ptr(const VtxSetup* S, const uint8_t* v, int mode, int off, int array) {
    if (mode == 1) return v + off;
    {
        uint32_t idx = (mode == 2) ? v[off] : (uint32_t)((v[off] << 8) | v[off + 1]);
        uint32_t a = S->array_base[array] + idx * S->array_stride[array];
        if (S->array_ptr[array] && a - S->array_base[array] < S->array_span[array]) return S->array_ptr[array] + (a - S->array_base[array]);
        {
            const uint8_t* p = mem_ptr(a | 0x80000000u);
            return p ? p : S->zero;
        }
    }
}

/* matrices bajo demanda: se convierten la primera vez que un vertice las usa */
static inline const float* pos_mtx(VtxSetup* S, uint32_t idx) {
    idx &= 63;
    if (!(S->pm_valid & (1ull << idx))) {
        int k;
        for (k = 0; k < 12; k++) S->pm[idx][k] = xf_f(idx * 4 + (uint32_t)k);
        S->pm_valid |= 1ull << idx;
    }
    return S->pm[idx];
}
static inline const float* nrm_mtx(VtxSetup* S, uint32_t idx) {
    idx &= 31;
    if (!(S->nm_valid & (1u << idx))) {
        int k;
        for (k = 0; k < 9; k++) S->nm[idx][k] = xf_f(0x400 + idx * 3 + (uint32_t)k);
        S->nm_valid |= 1u << idx;
    }
    return S->nm[idx];
}
static inline const float* tex_mtx(VtxSetup* S, uint32_t idx) {
    idx &= 63;
    if (!(S->tm_valid & (1ull << idx))) {
        int k;
        for (k = 0; k < 12; k++) S->tm[idx][k] = xf_f(idx * 4 + (uint32_t)k);
        S->tm_valid |= 1ull << idx;
    }
    return S->tm[idx];
}
static inline const float* post_mtx(VtxSetup* S, uint32_t idx) {
    idx &= 63;
    if (!(S->ptm_valid & (1ull << idx))) {
        int k;
        for (k = 0; k < 12; k++) S->ptm[idx][k] = xf_f(0x500 + idx * 4 + (uint32_t)k);
        S->ptm_valid |= 1ull << idx;
    }
    return S->ptm[idx];
}

volatile uint32_t vtx_fetch_gen;   /* cambia cada vez que vtx_setup_fetch rehace algun VtxSetup */
/* Lo que necesita vtx_fetch: arreglos, escalas e indices de matriz por defecto. */
void vtx_setup_fetch(const GXVtxLayout* L, VtxSetup* S) {
    int i;
    /* si nada cambio desde la ultima vez para este mismo S, no rehacer nada */
    if (S->fetch_valid && !memcmp(&S->L, L, sizeof *L) && S->fetch_matidx[0] == gx.cp_matidx_a && S->fetch_matidx[1] == gx.cp_matidx_b &&
        !memcmp(S->array_base, gx.cp_array_base, sizeof S->array_base) && !memcmp(S->array_stride, gx.cp_array_stride, sizeof S->array_stride))
        return;
    memset(S, 0, offsetof(VtxSetup, mat));
    vtx_fetch_gen++;
    S->L = *L;
    S->pos_scale = 1.0f / (float)(1 << L->pos_shift);
    for (i = 0; i < 16; i++) {
        S->array_base[i] = gx.cp_array_base[i];
        S->array_stride[i] = gx.cp_array_stride[i];
    }
    /* punteros directos a los arreglos que se usan (indices de 8 o 16 bits) */
    {
        int arr[12], modes[12], n = 0;
        arr[n] = 0; modes[n++] = L->pos_mode;
        arr[n] = 1; modes[n++] = L->nrm_mode;
        arr[n] = 2; modes[n++] = L->col_mode[0];
        arr[n] = 3; modes[n++] = L->col_mode[1];
        for (i = 0; i < 8; i++) { arr[n] = 4 + i; modes[n++] = L->tex_mode[i]; }
        for (i = 0; i < n; i++) {
            int a = arr[i];
            uint32_t span;
            uint8_t* p;
            if (modes[i] < 2) continue;
            span = S->array_stride[a] * (modes[i] == 2 ? 256u : 65536u) + 64u;
            p = mem_ptr(S->array_base[a] | 0x80000000u);
            if (p && mem_ptr((S->array_base[a] | 0x80000000u) + span - 1)) { S->array_ptr[a] = p; S->array_span[a] = span; }
        }
    }
    S->def_pmi = gx.cp_matidx_a & 0x3F;
    for (i = 0; i < 8; i++)
        S->def_tmi[i] = (i < 4) ? (gx.cp_matidx_a >> (6 + 6 * i)) & 0x3F : (gx.cp_matidx_b >> (6 * (i - 4))) & 0x3F;
    for (i = 0; i < 8; i++) S->tex_scale[i] = 1.0f / (float)(1 << L->tex_shift[i]);
    S->fetch_matidx[0] = gx.cp_matidx_a; S->fetch_matidx[1] = gx.cp_matidx_b;
    S->fetch_valid = 1;
}

/* --panoramico (16:9): las proyecciones en perspectiva se angostan a 3/4 y la imagen se
 * muestra estirada a 16:9, asi se ve mas a los costados sin deformar el 3D (como el
 * "widescreen hack" de Dolphin; lo que es 2D, como el HUD, queda estirado). */
int gl_widescreen;
/* Si el juego ya armo la proyeccion en 16:9 (la camara principal, ver video.c: se le cambia la
 * relacion de aspecto para que tambien recorte el mundo en 16:9) no hay que achicarla; las demas
 * proyecciones en perspectiva (4:3) se achican como antes. p0 escala x, p2 escala y. */
int gl_ws_aplastar(float p0, float p1, float p2) {
    float r;
    if (!gl_widescreen) return 0;
    /* descentrada (p1 != 0): es del HUD (el globo del radar, la nave del indicador de vida), que
       se dibuja en una esquina con su propia perspectiva; tiene que estirarse como el resto del
       HUD (que es plano) para seguir encajando en su marco */
    if (p1 > 0.05f || p1 < -0.05f) return 0;
    if (p2 == 0.0f) return 1;
    r = p0 / p2; if (r < 0) r = -r;
    return !(r > 0.53f && r < 0.60f);
}

void vtx_setup(const GXVtxLayout* L, VtxSetup* S) {
    int i, part, li;
    vtx_setup_fetch(L, S);
    memset(&S->mat, 0, offsetof(VtxSetup, pm) - offsetof(VtxSetup, mat));
    S->persp = gx.xf_regs[0x26] == 0;
    for (i = 0; i < 6; i++) S->proj[i] = xf_rf(0x20 + (uint32_t)i);
    if (S->persp && gl_ws_aplastar(S->proj[0], S->proj[1], S->proj[2])) { S->proj[0] *= 0.75f; S->proj[1] *= 0.75f; }   /* --panoramico */
    /* canales de color */
    S->any_light = 0;
    for (i = 0; i < 2; i++) {
        uint32_t matr = gx.xf_regs[0x0C + i], ambr = gx.xf_regs[0x0A + i];
        int k;
        for (k = 0; k < 4; k++) {
            S->mat[i][k] = (float)((matr >> (24 - 8 * k)) & 255);
            S->amb[i][k] = (float)((ambr >> (24 - 8 * k)) & 255);
        }
        for (part = 0; part < 2; part++) {
            uint32_t ctrl = gx.xf_regs[(part ? 0x10 : 0x0E) + i];
            ChanSetup* c = &S->chan[i][part];
            c->mat_vtx = ctrl & 1;
            c->light = (ctrl >> 1) & 1;
            c->amb_vtx = (ctrl >> 6) & 1;
            c->mask = ((ctrl >> 2) & 15) | (((ctrl >> 11) & 15) << 4);
            c->diff = (int)((ctrl >> 7) & 3);
            c->atten = (int)((ctrl >> 9) & 3);
            if (c->light && L->nrm_mode) S->any_light |= c->mask;
        }
    }
    for (li = 0; li < 8; li++) {
        uint32_t lb = 0x600 + (uint32_t)li * 0x10, lc;
        int k;
        if (!(S->any_light & (1u << li))) continue;
        lc = gx.xf_mem[lb + 3];
        for (k = 0; k < 4; k++) S->lights[li].col[k] = (float)((lc >> (24 - 8 * k)) & 255);
        for (k = 0; k < 3; k++) { S->lights[li].a[k] = xf_f(lb + 4 + (uint32_t)k); S->lights[li].kk[k] = xf_f(lb + 7 + (uint32_t)k); }
        for (k = 0; k < 3; k++) { S->lights[li].pos[k] = xf_f(lb + 10 + (uint32_t)k); S->lights[li].dir[k] = xf_f(lb + 13 + (uint32_t)k); }
    }
    /* coordenadas de textura */
    S->ntex = (int)(gx.xf_regs[0x3F] & 15);
    if (S->ntex > 8) S->ntex = 8;
    S->dual = gx.xf_regs[0x12] & 1;
    for (i = 0; i < S->ntex; i++) {
        uint32_t tg = gx.xf_regs[0x40 + i];
        S->tg_type[i] = (tg >> 4) & 7;
        S->tg_src[i] = (tg >> 7) & 31;
        S->tg_proj[i] = (tg >> 1) & 1;
        S->tg_post[i] = gx.xf_regs[0x50 + i] & 0x3F;
        S->tex_scale[i] = 1.0f / (float)(1 << L->tex_shift[i]);
    }
}

void vtx_decode(VtxSetup* S, const uint8_t* v, Vtx* o) {
    const GXVtxLayout* L = &S->L;
    float pos[3] = { 0, 0, 0 }, ep[3], nrm[3] = { 0, 0, 1 };
    uint32_t pmi = (L->pnmtx >= 0) ? v[L->pnmtx] : S->def_pmi;
    int i;
    if (L->pos_mode) {
        const uint8_t* p = attr_ptr(S, v, L->pos_mode, L->pos_off, 0);
        int cs = s_csize[L->pos_fmt];
        pos[0] = read_comp(p, L->pos_fmt, S->pos_scale);
        pos[1] = read_comp(p + cs, L->pos_fmt, S->pos_scale);
        if (L->pos_elems == 3) pos[2] = read_comp(p + 2 * cs, L->pos_fmt, S->pos_scale);
    }
    if (L->nrm_mode) {
        const uint8_t* p = attr_ptr(S, v, L->nrm_mode, L->nrm_off, 1);
        int cs = s_csize[L->nrm_fmt];
        float sc = L->nrm_fmt == 1 ? 1.0f / 64 : L->nrm_fmt == 3 ? 1.0f / 16384 : 1.0f;
        nrm[0] = read_comp(p, L->nrm_fmt, sc); nrm[1] = read_comp(p + cs, L->nrm_fmt, sc); nrm[2] = read_comp(p + 2 * cs, L->nrm_fmt, sc);
    }
    {
        const float* m = pos_mtx(S, pmi);
        ep[0] = m[0] * pos[0] + m[1] * pos[1] + m[2] * pos[2] + m[3];
        ep[1] = m[4] * pos[0] + m[5] * pos[1] + m[6] * pos[2] + m[7];
        ep[2] = m[8] * pos[0] + m[9] * pos[1] + m[10] * pos[2] + m[11];
    }
    if (S->persp) {
        o->x = S->proj[0] * ep[0] + S->proj[1] * ep[2];
        o->y = S->proj[2] * ep[1] + S->proj[3] * ep[2];
        o->z = S->proj[4] * ep[2] + S->proj[5];
        o->w = -ep[2];
    } else {
        o->x = S->proj[0] * ep[0] + S->proj[1];
        o->y = S->proj[2] * ep[1] + S->proj[3];
        o->z = S->proj[4] * ep[2] + S->proj[5];
        o->w = 1.0f;
    }
    /* colores: lo que depende solo de la luz (direccion, distancia, N.L) se calcula una
     * vez por vertice y se reusa en los dos canales y en color/alfa */
    {
        float n[3] = { 0, 0, 0 };
        float l_ndl[8], l_dlen[8], l_cs[8];
        uint32_t ldone = 0;
        int have_n = 0;
        for (i = 0; i < 2; i++) {
            float vc[4] = { 255, 255, 255, 255 };
            int have_vc = 0, part;
            if (L->col_mode[i]) {
                read_color(attr_ptr(S, v, L->col_mode[i], L->col_off[i], 2 + i), L->col_fmt[i], vc);
                have_vc = 1;
            }
            for (part = 0; part < 2; part++) {
                const ChanSetup* c = &S->chan[i][part];
                float mat[4], lit[4];
                const float* amb;
                int k, li, k0 = part ? 3 : 0, k1 = part ? 4 : 3;
                const float* mt = (c->mat_vtx && have_vc) ? vc : S->mat[i];
                for (k = k0; k < k1; k++) mat[k] = mt[k];
                if (c->light && L->nrm_mode) {
                    if (!have_n) {
                        const float* nm = nrm_mtx(S, pmi);
                        float len;
                        n[0] = nm[0] * nrm[0] + nm[1] * nrm[1] + nm[2] * nrm[2];
                        n[1] = nm[3] * nrm[0] + nm[4] * nrm[1] + nm[5] * nrm[2];
                        n[2] = nm[6] * nrm[0] + nm[7] * nrm[1] + nm[8] * nrm[2];
                        len = n[0] * n[0] + n[1] * n[1] + n[2] * n[2];
                        if (len > 0) { len = 1.0f / sqrtf(len); n[0] *= len; n[1] *= len; n[2] *= len; }
                        have_n = 1;
                    }
                    amb = (c->amb_vtx && have_vc) ? vc : S->amb[i];
                    for (k = k0; k < k1; k++) lit[k] = amb[k];
                    for (li = 0; li < 8; li++) {
                        const LightSetup* l = &S->lights[li];
                        float ndl, att = 1.0f;
                        if (!(c->mask & (1u << li))) continue;
                        if (!(ldone & (1u << li))) {
                            float lp0 = l->pos[0] - ep[0], lp1 = l->pos[1] - ep[1], lp2 = l->pos[2] - ep[2];
                            float d2 = lp0 * lp0 + lp1 * lp1 + lp2 * lp2, dlen = sqrtf(d2), ld0, ld1, ld2;
                            if (dlen > 0) { float id = 1.0f / dlen; ld0 = lp0 * id; ld1 = lp1 * id; ld2 = lp2 * id; }
                            else { ld0 = ld1 = 0; ld2 = 1; }
                            l_ndl[li] = n[0] * ld0 + n[1] * ld1 + n[2] * ld2;
                            l_dlen[li] = dlen;
                            l_cs[li] = ld0 * l->dir[0] + ld1 * l->dir[1] + ld2 * l->dir[2];
                            ldone |= 1u << li;
                        }
                        ndl = l_ndl[li];
                        /* como Dolphin: atenuacion 1 = especular, 3 = foco; difuso 1 = con signo, 2 = recortado */
                        if (c->atten == 1) {
                            float a = ndl >= 0 ? fmaxf(0.0f, n[0] * l->dir[0] + n[1] * l->dir[1] + n[2] * l->dir[2]) : 0.0f;
                            float d0 = l->kk[0], d1 = l->kk[1], d2 = l->kk[2], den;
                            if (c->diff == 0) { float q = d0 * d0 + d1 * d1 + d2 * d2; if (q > 0) { q = 1.0f / sqrtf(q); d0 *= q; d1 *= q; d2 *= q; } }
                            den = d0 + d1 * a + d2 * a * a;
                            att = den != 0 ? fmaxf(0.0f, l->a[0] + l->a[1] * a + l->a[2] * a * a) / den : 0.0f;
                        } else if (c->atten == 3) {
                            float cs = fmaxf(0.0f, l_cs[li]), dlen = l_dlen[li];
                            float num = l->a[0] + l->a[1] * cs + l->a[2] * cs * cs, den = l->kk[0] + l->kk[1] * dlen + l->kk[2] * dlen * dlen;
                            att = den != 0 ? fmaxf(0.0f, num) / den : 0.0f;
                        }
                        if (c->diff == 2) ndl = ndl < 0 ? 0 : ndl;
                        else if (c->diff == 0) ndl = 1.0f;
                        {
                            float f = ndl * att;
                            for (k = k0; k < k1; k++) lit[k] += l->col[k] * f;
                        }
                    }
                    for (k = k0; k < k1; k++) mat[k] = mat[k] * fminf(255.0f, fmaxf(0.0f, lit[k])) * (1.0f / 255.0f);
                }
                if (part == 0) { o->col[i][0] = mat[0]; o->col[i][1] = mat[1]; o->col[i][2] = mat[2]; }
                else o->col[i][3] = mat[3];
            }
        }
    }
    /* coordenadas de textura */
    for (i = 0; i < S->ntex; i++) {
        uint32_t src = S->tg_src[i];
        float in[3] = { 0, 0, 1 }, out[3];
        uint32_t tmi = L->texmtx[i] >= 0 ? v[L->texmtx[i]] : S->def_tmi[i];
        switch (src) {
        case 0: in[0] = pos[0]; in[1] = pos[1]; in[2] = pos[2]; break;
        case 1: in[0] = nrm[0]; in[1] = nrm[1]; in[2] = nrm[2]; break;
        default:
            if (src >= 5 && src <= 12) {
                int t = (int)src - 5;
                if (L->tex_mode[t]) {
                    const uint8_t* p = attr_ptr(S, v, L->tex_mode[t], L->tex_off[t], 4 + t);
                    in[0] = read_comp(p, L->tex_fmt[t], S->tex_scale[t]);
                    in[1] = L->tex_elems[t] == 2 ? read_comp(p + s_csize[L->tex_fmt[t]], L->tex_fmt[t], S->tex_scale[t]) : 0.0f;
                } else { in[0] = in[1] = 0; }
                in[2] = 1.0f;
            }
            break;
        }
        if (S->tg_type[i] == 0) {
            const float* m = tex_mtx(S, tmi);
            out[0] = m[0] * in[0] + m[1] * in[1] + m[2] * in[2] + m[3];
            out[1] = m[4] * in[0] + m[5] * in[1] + m[6] * in[2] + m[7];
            out[2] = S->tg_proj[i] ? m[8] * in[0] + m[9] * in[1] + m[10] * in[2] + m[11] : 1.0f;
        } else if (S->tg_type[i] == 1) {   /* relieve: la coordenada de otra etapa (sin tangentes, sin corrimiento) */
            int es = (int)((gx.xf_regs[0x40 + i] >> 12) & 7);
            if (es < i) { o->tex[i][0] = o->tex[es][0]; o->tex[i][1] = o->tex[es][1]; o->tex[i][2] = o->tex[es][2]; }
            else { o->tex[i][0] = 0; o->tex[i][1] = 0; o->tex[i][2] = 1; }
            continue;
        } else if (S->tg_type[i] == 2 || S->tg_type[i] == 3) {
            int c = S->tg_type[i] - 2;
            out[0] = o->col[c][0] / 255.0f; out[1] = o->col[c][1] / 255.0f; out[2] = 1.0f;
        } else {
            out[0] = in[0]; out[1] = in[1]; out[2] = 1.0f;
        }
        if (S->dual && S->tg_type[i] == 0) {
            const float* m = post_mtx(S, S->tg_post[i]);
            float nn[3] = { out[0], out[1], S->tg_proj[i] ? out[2] : 1.0f };   /* sin dividir por q (como la consola) */
            if (gx.xf_regs[0x50 + i] & 0x100) { float l = sqrtf(nn[0] * nn[0] + nn[1] * nn[1] + nn[2] * nn[2]); if (l > 0) { nn[0] /= l; nn[1] /= l; nn[2] /= l; } }
            out[0] = m[0] * nn[0] + m[1] * nn[1] + m[2] * nn[2] + m[3];
            out[1] = m[4] * nn[0] + m[5] * nn[1] + m[6] * nn[2] + m[7];
            out[2] = S->tg_proj[i] ? m[8] * nn[0] + m[9] * nn[1] + m[10] * nn[2] + m[11] : 1.0f;
        }
        o->tex[i][0] = out[0]; o->tex[i][1] = out[1]; o->tex[i][2] = out[2];
    }
}

/* Para la transformacion en la GPU (render_gl.c, VS_XF): solo se leen los atributos
 * del vertice; matrices, luces y coordenadas de textura las calcula el shader.
 * Formato de salida (XVTX_BASE + 8 * nraw bytes):
 *   0 posicion (3 float) | 12 normal (3 float) | 24 color0 RGBA8 | 28 color1 RGBA8 |
 *   32 u16: estado, matriz de posicion, matrices de textura 0|1<<8, 2|3<<8 |
 *   40 u16: 4|5<<8, 6|7<<8, banderas (1 normal, 2 color0, 4 color1), 0 |
 *   48 coordenadas de textura crudas 0..nraw-1 (2 float) */
void vtx_fetch(VtxSetup* S, const uint8_t* v, uint8_t* out, int nraw, uint32_t snap) {
    const GXVtxLayout* L = &S->L;
    float* f = (float*)out;
    uint16_t* ix = (uint16_t*)(out + 32);
    uint32_t pmi = (L->pnmtx >= 0) ? v[L->pnmtx] : S->def_pmi;
    uint32_t tm[8], flags = 0;
    int i;
    f[0] = f[1] = f[2] = 0;
    if (L->pos_mode) {
        const uint8_t* p = attr_ptr(S, v, L->pos_mode, L->pos_off, 0);
        int cs = s_csize[L->pos_fmt];
        f[0] = read_comp(p, L->pos_fmt, S->pos_scale);
        f[1] = read_comp(p + cs, L->pos_fmt, S->pos_scale);
        if (L->pos_elems == 3) f[2] = read_comp(p + 2 * cs, L->pos_fmt, S->pos_scale);
    }
    if (L->nrm_mode) {
        const uint8_t* p = attr_ptr(S, v, L->nrm_mode, L->nrm_off, 1);
        int cs = s_csize[L->nrm_fmt];
        float sc = L->nrm_fmt == 1 ? 1.0f / 64 : L->nrm_fmt == 3 ? 1.0f / 16384 : 1.0f;
        f[3] = read_comp(p, L->nrm_fmt, sc); f[4] = read_comp(p + cs, L->nrm_fmt, sc); f[5] = read_comp(p + 2 * cs, L->nrm_fmt, sc);
        flags |= 1;
    } else { f[3] = 0; f[4] = 0; f[5] = 1; }
    for (i = 0; i < 2; i++) {
        uint8_t* c = out + 24 + 4 * i;
        if (L->col_mode[i]) {
            float vc[4];
            read_color(attr_ptr(S, v, L->col_mode[i], L->col_off[i], 2 + i), L->col_fmt[i], vc);
            c[0] = (uint8_t)vc[0]; c[1] = (uint8_t)vc[1]; c[2] = (uint8_t)vc[2]; c[3] = (uint8_t)vc[3];
            flags |= 2u << i;
        } else { c[0] = c[1] = c[2] = c[3] = 255; }
    }
    for (i = 0; i < 8; i++) tm[i] = (L->texmtx[i] >= 0 ? v[L->texmtx[i]] : S->def_tmi[i]) & 63;
    ix[0] = (uint16_t)snap; ix[1] = (uint16_t)(pmi & 63);
    ix[2] = (uint16_t)(tm[0] | tm[1] << 8); ix[3] = (uint16_t)(tm[2] | tm[3] << 8);
    ix[4] = (uint16_t)(tm[4] | tm[5] << 8); ix[5] = (uint16_t)(tm[6] | tm[7] << 8);
    ix[6] = (uint16_t)flags; ix[7] = 0;
    f = (float*)(out + 48);
    for (i = 0; i < nraw; i++) {
        if (L->tex_mode[i]) {
            const uint8_t* p = attr_ptr(S, v, L->tex_mode[i], L->tex_off[i], 4 + i);
            f[2 * i] = read_comp(p, L->tex_fmt[i], S->tex_scale[i]);
            f[2 * i + 1] = L->tex_elems[i] == 2 ? read_comp(p + s_csize[L->tex_fmt[i]], L->tex_fmt[i], S->tex_scale[i]) : 0.0f;
        } else { f[2 * i] = 0; f[2 * i + 1] = 0; }
    }
}

/* ---- vtx_fetch_n: lo mismo que vtx_fetch para todo un dibujo, por columnas ----------------
 * En vez de decidir formato y modo de cada atributo en cada vertice, se recorre un atributo
 * a la vez para todos los vertices con el formato ya elegido (el bucle de adentro queda chico
 * y sin saltos). En el nivel 3 el juego manda ~150 mil vertices por cuadro. */

#define AP(v, mode, off, arr) attr_ptr(S, (v), (mode), (off), (arr))
#define COL_LOOP(READ, CS)                                                         \
    for (i = 0; i < n; i++) {                                                      \
        const uint8_t* p = AP(data + (size_t)i * vsize, mode, off, arr);           \
        float* f = (float*)(out + (size_t)i * st + dst);                           \
        for (k = 0; k < ne; k++, p += (CS)) f[k] = (READ);                         \
        for (; k < ne_pad; k++) f[k] = 0.0f;                                       \
    }

#define COL_FMTS(PTR)                                                                       \
    switch (fmt) {                                                                          \
    case 0: COL_LOOP2(PTR, p[0] * sc, 1) break;                                             \
    case 1: COL_LOOP2(PTR, (int8_t)p[0] * sc, 1) break;                                     \
    case 2: COL_LOOP2(PTR, (uint16_t)((p[0] << 8) | p[1]) * sc, 2) break;                   \
    case 3: COL_LOOP2(PTR, (int16_t)((p[0] << 8) | p[1]) * sc, 2) break;                    \
    default: COL_LOOP2(PTR, u32_f(((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]), 4) break; \
    }
/* el bucle de componentes desenrollado para 1, 2 y 3 componentes (lo normal) */
#define COL_LOOP2(PTR, READ, CS)                                                   \
    switch (ne * 4 + ne_pad) {                                                     \
    case 3 * 4 + 3: for (i = 0; i < n; i++) { const uint8_t* v = data + (size_t)i * vsize; const uint8_t* p = (PTR); \
        float* f = (float*)(out + (size_t)i * st + dst); f[0] = (READ); p += (CS); f[1] = (READ); p += (CS); f[2] = (READ); } break; \
    case 2 * 4 + 3: for (i = 0; i < n; i++) { const uint8_t* v = data + (size_t)i * vsize; const uint8_t* p = (PTR); \
        float* f = (float*)(out + (size_t)i * st + dst); f[0] = (READ); p += (CS); f[1] = (READ); f[2] = 0.0f; } break; \
    case 2 * 4 + 2: for (i = 0; i < n; i++) { const uint8_t* v = data + (size_t)i * vsize; const uint8_t* p = (PTR); \
        float* f = (float*)(out + (size_t)i * st + dst); f[0] = (READ); p += (CS); f[1] = (READ); } break; \
    case 1 * 4 + 2: for (i = 0; i < n; i++) { const uint8_t* v = data + (size_t)i * vsize; const uint8_t* p = (PTR); \
        float* f = (float*)(out + (size_t)i * st + dst); f[0] = (READ); f[1] = 0.0f; } break; \
    default:                                                                       \
    for (i = 0; i < n; i++) {                                                      \
        const uint8_t* v = data + (size_t)i * vsize;                               \
        const uint8_t* p = (PTR);                                                  \
        float* f = (float*)(out + (size_t)i * st + dst);                           \
        for (k = 0; k < ne; k++, p += (CS)) f[k] = (READ);                         \
        for (; k < ne_pad; k++) f[k] = 0.0f;                                       \
    } }

static void columna(VtxSetup* S, const uint8_t* data, int vsize, uint8_t* out, size_t st, int n,
                    int mode, int off, int arr, int fmt, int ne, int ne_pad, float sc, size_t dst) {
    int i, k;
    const uint8_t* base = S->array_ptr[arr];
    uint32_t paso = S->array_stride[arr];
    /* el arreglo entero esta en la RAM (vtx_setup_fetch lo comprobo para todos los indices posibles) */
    if (mode == 1) { COL_FMTS(v + off) }
    else if (mode == 2 && base) { COL_FMTS(base + (size_t)v[off] * paso) }
    else if (mode == 3 && base) { COL_FMTS(base + (size_t)((v[off] << 8) | v[off + 1]) * paso) }
    else { COL_FMTS(AP(v, mode, off, arr)) }
}

void vtx_fetch_n(VtxSetup* S, const uint8_t* data, int vsize, uint8_t* out, size_t st, int n, int nraw, uint32_t snap, const uint8_t lay[8]) {
    const GXVtxLayout* L = &S->L;
    uint16_t ix[8];
    int i, j, por_vertice = 0;
    uint32_t flags = (L->nrm_mode ? 1u : 0) | (L->col_mode[0] ? 2u : 0) | (L->col_mode[1] ? 4u : 0);
    if (n <= 0) return;
    /* cabecera: lo que no depende del vertice */
    ix[0] = (uint16_t)snap; ix[1] = (uint16_t)(S->def_pmi & 63);
    for (j = 0; j < 4; j++) ix[2 + j] = (uint16_t)((S->def_tmi[2 * j] & 63) | (S->def_tmi[2 * j + 1] & 63) << 8);
    ix[6] = (uint16_t)flags; ix[7] = 0;
    for (j = 0; j < 8; j++) if (L->texmtx[j] >= 0) por_vertice = 1;
    for (i = 0; i < n; i++) {
        uint8_t* o = out + (size_t)i * st;
        memcpy(o + 32, ix, 16);
        memcpy(o + XVTX_BASE + 8 * nraw, lay, 8);
    }
    if (L->pnmtx >= 0)
        for (i = 0; i < n; i++) ((uint16_t*)(out + (size_t)i * st + 32))[1] = (uint16_t)(data[(size_t)i * vsize + L->pnmtx] & 63);
    if (por_vertice)
        for (i = 0; i < n; i++) {
            const uint8_t* v = data + (size_t)i * vsize;
            uint16_t* q = (uint16_t*)(out + (size_t)i * st + 32);
            uint32_t tm[8];
            for (j = 0; j < 8; j++) tm[j] = (L->texmtx[j] >= 0 ? v[L->texmtx[j]] : S->def_tmi[j]) & 63;
            for (j = 0; j < 4; j++) q[2 + j] = (uint16_t)(tm[2 * j] | tm[2 * j + 1] << 8);
        }
    /* posicion */
    if (L->pos_mode) columna(S, data, vsize, out, st, n, L->pos_mode, L->pos_off, 0, L->pos_fmt, L->pos_elems == 3 ? 3 : 2, 3, S->pos_scale, 0);
    else for (i = 0; i < n; i++) memset(out + (size_t)i * st, 0, 12);
    /* normal */
    if (L->nrm_mode) columna(S, data, vsize, out, st, n, L->nrm_mode, L->nrm_off, 1, L->nrm_fmt, 3, 3,
                             L->nrm_fmt == 1 ? 1.0f / 64 : L->nrm_fmt == 3 ? 1.0f / 16384 : 1.0f, 12);
    else for (i = 0; i < n; i++) { float* f = (float*)(out + (size_t)i * st); f[3] = 0; f[4] = 0; f[5] = 1; }
    /* colores */
    for (j = 0; j < 2; j++) {
        if (L->col_mode[j]) {
            int mode = L->col_mode[j], off = L->col_off[j], fmt = L->col_fmt[j];
            const uint8_t* cb = S->array_ptr[2 + j];
            uint32_t cp = S->array_stride[2 + j];
            /* lo comun (RGBA8 o RGB8, directo o con indice): copiar los bytes sin pasar por float */
            if ((fmt == 5 || fmt == 1 || fmt == 2) && (mode == 1 || cb)) {
                uint32_t a_or = fmt == 5 ? 0u : 0xFF000000u;   /* bytes en memoria: R G B A (little endian: A arriba) */
                for (i = 0; i < n; i++) {
                    const uint8_t* v = data + (size_t)i * vsize;
                    const uint8_t* p = mode == 1 ? v + off : mode == 2 ? cb + (size_t)v[off] * cp : cb + (size_t)((v[off] << 8) | v[off + 1]) * cp;
                    uint32_t c = 0;
                    memcpy(&c, p, fmt == 1 ? 3 : 4);   /* RGB8 tiene 3 bytes */
                    if (fmt == 1) c &= 0x00FFFFFFu;
                    c |= a_or;
                    memcpy(out + (size_t)i * st + 24 + 4 * j, &c, 4);
                }
                continue;
            }
            for (i = 0; i < n; i++) {
                float vc[4];
                uint8_t* c = out + (size_t)i * st + 24 + 4 * j;
                read_color(AP(data + (size_t)i * vsize, mode, off, 2 + j), fmt, vc);
                c[0] = (uint8_t)vc[0]; c[1] = (uint8_t)vc[1]; c[2] = (uint8_t)vc[2]; c[3] = (uint8_t)vc[3];
            }
        } else for (i = 0; i < n; i++) memset(out + (size_t)i * st + 24 + 4 * j, 255, 4);
    }
    /* coordenadas de textura crudas */
    for (j = 0; j < nraw; j++) {
        if (L->tex_mode[j]) columna(S, data, vsize, out, st, n, L->tex_mode[j], L->tex_off[j], 4 + j, L->tex_fmt[j],
                                    L->tex_elems[j] == 2 ? 2 : 1, 2, S->tex_scale[j], (size_t)(XVTX_BASE + 8 * j));
        else for (i = 0; i < n; i++) memset(out + (size_t)i * st + XVTX_BASE + 8 * j, 0, 8);
    }
}
