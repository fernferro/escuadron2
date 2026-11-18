/*
 * Procesador de comandos graficos (GX) de la GameCube.
 *
 * Todo lo que el juego escribe en el tubo de escritura (0xCC008000) llega aca
 * como un flujo de bytes. Se decodifica comando por comando:
 *   0x08 registro CP, 0x10 registros XF, 0x20-0x38 carga indexada de XF,
 *   0x40 llamada a lista de display, 0x48 invalidar cache, 0x61 registro BP,
 *   0x80-0xBF dibujar primitivas.
 * El estado queda en gx_state (gx.h) para el rasterizador.
 */
#include <pthread.h>
#if defined(__linux__)
#include <execinfo.h>
#include <stdint.h>
#endif
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif
#include "runtime.h"
#include "os.h"
#include "gx.h"

GXState gx;
int gx_draw_limit = -1;
int gx_draw_skip = -1, gx_draw_skip_to = -2;          /* RL_DRAW_SKIP=N: no dibujar el dibujo N (reproduccion) */         /* RL_DRAW_LIMIT: dibujar solo los primeros N dibujos (para buscar errores) */
static int s_draw_count;
uint64_t gx_draw_index_dbg(void) { return (uint64_t)(s_draw_count - 1); }
int gx_trace_draw, gx_trace_index = -1;   /* RL_DRAW_TRACE=N: detalles del dibujo N */
uint64_t gx_busy_us;      /* tiempo ocupado del hilo grafico (estadistica) */
uint64_t gx_replay_v0;
int gx_skip_draws;       /* --dump-rapido: no dibujar los cuadros que no se guardan */
int gx_trace;

static uint8_t s_fifo[4 << 20];   /* comandos a medio llegar (solo el hilo grafico) */
static uint32_t s_fifo_len;
static int s_dl_depth;

void gx_pe_token(uint16_t token, int interrupt);
void gx_pe_finish(void);
void gx_render_draw(int prim, int vat, int count, const uint8_t* data, uint32_t vsize);
void gx_render_copy(void);
void gx_render_bp(int reg, uint32_t v);

static uint32_t be32(const uint8_t* p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static uint16_t be16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }

/* ---- formato de vertices ------------------------------------------------- */

static const int s_comp_size[8] = { 1, 1, 2, 2, 4, 0, 0, 0 };
static const int s_color_size[8] = { 2, 3, 4, 2, 3, 4, 0, 0 };

/* Calcula la disposicion de un vertice para el formato `vat`. */
void gx_vertex_layout(int vat, GXVtxLayout* L) {
    uint32_t lo = gx.cp_vcd_lo, hi = gx.cp_vcd_hi;
    uint32_t a = gx.cp_vat_a[vat], b = gx.cp_vat_b[vat], c = gx.cp_vat_c[vat];
    int off = 0, i;
    /* cache por formato (se llama dos veces por dibujo y casi nunca cambia); solo lo usa el hilo grafico */
    static __thread struct { uint32_t k[5]; int valid; GXVtxLayout L; } cache[8];
    if (cache[vat].valid && cache[vat].k[0] == lo && cache[vat].k[1] == hi && cache[vat].k[2] == a &&
        cache[vat].k[3] == b && cache[vat].k[4] == c) { *L = cache[vat].L; return; }
    memset(L, 0, sizeof *L);
    L->pnmtx = (lo & 1) ? off++ : -1;
    for (i = 0; i < 8; i++) L->texmtx[i] = ((lo >> (1 + i)) & 1) ? off++ : -1;

    /* posicion */
    L->pos_mode = (lo >> 9) & 3;
    L->pos_elems = (a & 1) ? 3 : 2;
    L->pos_fmt = (a >> 1) & 7;
    L->pos_shift = (a >> 4) & 31;
    L->pos_off = off;
    if (L->pos_mode == 1) off += L->pos_elems * s_comp_size[L->pos_fmt];
    else if (L->pos_mode >= 2) off += L->pos_mode - 1;

    /* normal */
    L->nrm_mode = (lo >> 11) & 3;
    L->nrm_elems = ((a >> 9) & 1) ? 9 : 3;
    L->nrm_fmt = (a >> 10) & 7;
    L->nrm_index3 = (a >> 31) & 1;
    L->nrm_off = off;
    if (L->nrm_mode == 1) off += L->nrm_elems * s_comp_size[L->nrm_fmt];
    else if (L->nrm_mode >= 2) off += (L->nrm_mode - 1) * ((L->nrm_elems == 9 && L->nrm_index3) ? 3 : 1);

    /* colores */
    for (i = 0; i < 2; i++) {
        L->col_mode[i] = (lo >> (13 + 2 * i)) & 3;
        L->col_fmt[i] = (a >> (14 + 4 * i)) & 7;
        L->col_off[i] = off;
        if (L->col_mode[i] == 1) off += s_color_size[L->col_fmt[i]];
        else if (L->col_mode[i] >= 2) off += L->col_mode[i] - 1;
    }

    /* coordenadas de textura */
    {
        uint32_t cnt[8], fmt[8], sh[8];
        cnt[0] = (a >> 21) & 1; fmt[0] = (a >> 22) & 7; sh[0] = (a >> 25) & 31;
        cnt[1] = b & 1; fmt[1] = (b >> 1) & 7; sh[1] = (b >> 4) & 31;
        cnt[2] = (b >> 9) & 1; fmt[2] = (b >> 10) & 7; sh[2] = (b >> 13) & 31;
        cnt[3] = (b >> 18) & 1; fmt[3] = (b >> 19) & 7; sh[3] = (b >> 22) & 31;
        cnt[4] = (b >> 27) & 1; fmt[4] = (b >> 28) & 7; sh[4] = c & 31;
        cnt[5] = (c >> 5) & 1; fmt[5] = (c >> 6) & 7; sh[5] = (c >> 9) & 31;
        cnt[6] = (c >> 14) & 1; fmt[6] = (c >> 15) & 7; sh[6] = (c >> 18) & 31;
        cnt[7] = (c >> 23) & 1; fmt[7] = (c >> 24) & 7; sh[7] = (c >> 27) & 31;
        for (i = 0; i < 8; i++) {
            L->tex_mode[i] = (hi >> (2 * i)) & 3;
            L->tex_elems[i] = cnt[i] ? 2 : 1;
            L->tex_fmt[i] = fmt[i];
            L->tex_shift[i] = sh[i];
            L->tex_off[i] = off;
            if (L->tex_mode[i] == 1) off += L->tex_elems[i] * s_comp_size[fmt[i]];
            else if (L->tex_mode[i] >= 2) off += L->tex_mode[i] - 1;
        }
    }
    L->size = off;
    cache[vat].k[0] = lo; cache[vat].k[1] = hi; cache[vat].k[2] = a; cache[vat].k[3] = b; cache[vat].k[4] = c;
    cache[vat].L = *L; cache[vat].valid = 1;
}

/* ---- registros -------------------------------------------------------------- */

uint32_t gx_cp_gen = 1;  /* cambia con cada escritura de CP (formato de vertices, arreglos, matrices por defecto) */
static void cp_write(uint8_t reg, uint32_t v) {
    gx_cp_gen++;
    if (reg == 0x50) gx.cp_vcd_lo = v;
    else if (reg == 0x60) gx.cp_vcd_hi = v;
    else if (reg >= 0x70 && reg < 0x78) gx.cp_vat_a[reg & 7] = v;
    else if (reg >= 0x80 && reg < 0x88) gx.cp_vat_b[reg & 7] = v;
    else if (reg >= 0x90 && reg < 0x98) gx.cp_vat_c[reg & 7] = v;
    else if (reg >= 0xA0 && reg < 0xB0) gx.cp_array_base[reg & 15] = v;
    else if (reg >= 0xB0 && reg < 0xC0) gx.cp_array_stride[reg & 15] = v & 0xFF;
    else if (reg == 0x30) gx.cp_matidx_a = v;
    else if (reg == 0x40) gx.cp_matidx_b = v;
}

uint32_t gx_xf_gen;     /* cambia cada vez que cambia algo de XF (render_gl.c) */
uint64_t gx_xf_dirty = ~0ull; /* bloques de 16 palabras del estado de XF que cambiaron (render_gl.c) */
uint32_t gx_xfreg_gen;  /* cambia cuando cambian los registros de XF (no las matrices) */
uint32_t gx_bp_gen;     /* cambia con cada escritura de BP */
static void xf_write(uint32_t addr, uint32_t v) {
    if (addr < 0x1000) {
        if (addr < sizeof gx.xf_mem / 4 && gx.xf_mem[addr] != v) {
            gx.xf_mem[addr] = v; gx_xf_gen++;
            /* misma distribucion que xf_snapshot en render_gl.c */
            if (addr < 0x100) gx_xf_dirty |= 1ull << (addr >> 4);
            else if (addr >= 0x400 && addr < 0x460) gx_xf_dirty |= 1ull << ((256 + addr - 0x400) >> 4);
            else if (addr >= 0x500 && addr < 0x600) gx_xf_dirty |= 1ull << ((352 + addr - 0x500) >> 4);
            else if (addr >= 0x600 && addr < 0x680) gx_xf_dirty |= 1ull << ((608 + addr - 0x600) >> 4);
        }
        return;
    }
    if (addr < 0x1000 + 0x100 && gx.xf_regs[addr - 0x1000] != v) {
        gx.xf_regs[addr - 0x1000] = v; gx_xf_gen++; gx_xfreg_gen++;
        if (addr - 0x1000 < 0x60) gx_xf_dirty |= 1ull << ((736 + addr - 0x1000) >> 4);
    }
}

static void bp_write(uint32_t w) {
    uint8_t reg = (uint8_t)(w >> 24);
    uint32_t v = w & 0xFFFFFF;
    gx_bp_gen++;
    {   /* RL_TRACE_BP=A-B: registra las escrituras de BP entre los dibujos A y B */
        static int a = -2, b;
        if (a == -2) { const char* e = getenv("RL_TRACE_BP"); a = e ? atoi(e) : -1; b = e && strchr(e, '-') ? atoi(strchr(e, '-') + 1) : a; }
        if (a >= 0 && s_draw_count >= a && s_draw_count <= b) rt_log("[bp] dibujo %d: %02X = %06X\n", s_draw_count, reg, v);
    }
    if (reg == 0xFE) { gx.bp_mask = v; return; }
    v = (gx.bp_regs[reg] & ~gx.bp_mask) | (v & gx.bp_mask);
    gx.bp_mask = 0xFFFFFF;
    gx.bp_regs[reg] = v;
    if (reg >= 0xE0 && reg <= 0xE7) {
        int i = (reg - 0xE0) >> 1, half = reg & 1;
        if (v & 0x800000) gx.tev_konst[i][half] = v;
        else gx.tev_reg[i][half] = v;
    }
    gx_render_bp(reg, v);
    switch (reg) {
    /* antes de avisarle al juego que termino de dibujar, los vertices que leen otros hilos
       tienen que estar leidos (despues el juego puede reescribir esa memoria) */
    case 0x45:  /* fin de dibujo */
        if (v & 2) { void gl_vtx_esperar(void); gl_vtx_esperar(); gx_pe_finish(); }
        break;
    case 0x47:  /* token sin interrupcion */
        { void gl_vtx_esperar(void); gl_vtx_esperar(); }
        gx_pe_token((uint16_t)v, 0);
        break;
    case 0x48:  /* token con interrupcion */
        { void gl_vtx_esperar(void); gl_vtx_esperar(); }
        gx_pe_token((uint16_t)v, 1);
        break;
    case 0x52:  /* copia del EFB (a la XFB o a una textura) */
        gx_render_copy();
        break;
    default:
        break;
    }
}

/* ---- decodificador -------------------------------------------------------- */

static uint32_t parse(const uint8_t* p, uint32_t len);

static void call_display_list(uint32_t addr, uint32_t size) {
    const uint8_t* p = mem_ptr(addr | 0x80000000u);
    uint32_t used;
    if (!p || !mem_ptr((addr | 0x80000000u) + size - 1) || s_dl_depth > 4) {
        rt_log("[gx] lista de display invalida 0x%08X (%u bytes)\n", addr, size);
        return;
    }
    {   static int tr = -1; if (tr < 0) tr = getenv("RL_TRACE_DL") != NULL;
        if (tr) rt_log("[dl] dibujo %d: lista %08X %u bytes\n", s_draw_count, addr, size); }
    s_dl_depth++;
    gx.stats.display_lists++;
    used = parse(p, size);
    if (used != size && gx_trace) rt_log("[gx] lista de display 0x%08X: %u de %u bytes usados\n", addr, used, size);
    s_dl_depth--;
}

uint32_t gx_m0_src, gx_m0_kind;   /* depuracion: de donde salio la ultima carga de la matriz 0 */
static void indexed_xf(int which, uint32_t w) {
    uint32_t index = w >> 16, size = ((w >> 12) & 0xF) + 1, xaddr = w & 0xFFF, i;
    uint32_t base = gx.cp_array_base[12 + which], stride = gx.cp_array_stride[12 + which];
    uint32_t src = (base + index * stride) | 0x80000000u;
    if (xaddr == 0) { gx_m0_src = src; gx_m0_kind = 0x20 + which * 8 + (index << 8); }
    {   /* RL_TRACE_IDX=1 (depuracion): cargas indexadas de la matriz 0 (en que lista y dibujo) */
        static int tr = -1; if (tr < 0) tr = getenv("RL_TRACE_IDX") != NULL;
        if (tr && xaddr == 0) rt_log("[idx] dibujo %d lista %d: matriz desde %08X (arreglo %d indice %u)\n", s_draw_count, s_dl_depth, src, 12 + which, index);
    }
    for (i = 0; i < size; i++) xf_write(xaddr + i, R32(src + 4 * i));
}

/* Decodifica comandos completos; devuelve cuantos bytes consumio. */
static uint32_t parse(const uint8_t* p, uint32_t len) {
    uint32_t pos = 0;
    while (pos < len) {
        uint8_t op = p[pos];
        uint32_t need;
        switch (op) {
        case 0x00: pos++; continue;
        case 0x08:
            if (len - pos < 6) return pos;
            cp_write(p[pos + 1], be32(p + pos + 2));
            pos += 6;
            continue;
        case 0x10: {
            uint32_t h, n, addr, i;
            if (len - pos < 5) return pos;
            h = be32(p + pos + 1);
            n = ((h >> 16) & 0xFFFF) + 1;
            addr = h & 0xFFFF;
            need = 5 + 4 * n;
            if (len - pos < need) return pos;
            if (addr == 0) { gx_m0_src = 0; gx_m0_kind = 0x10; }
            for (i = 0; i < n; i++) xf_write(addr + i, be32(p + pos + 5 + 4 * i));
            pos += need;
            continue;
        }
        case 0x20: case 0x28: case 0x30: case 0x38:
            if (len - pos < 5) return pos;
            indexed_xf((op - 0x20) >> 3, be32(p + pos + 1));
            pos += 5;
            continue;
        case 0x40:
            if (len - pos < 9) return pos;
            call_display_list(be32(p + pos + 1), be32(p + pos + 5));
            pos += 9;
            continue;
        case 0x44: pos++; continue;
        case 0x48: pos++; continue;
        case 0x61:
            if (len - pos < 5) return pos;
            bp_write(be32(p + pos + 1));
            pos += 5;
            continue;
        default:
            if (op >= 0x80 && op < 0xC0) {
                /* solo hace falta el tamano del vertice: se guarda por formato hasta que cambie CP */
                static __thread GXVtxLayout L;
                static __thread int l_vat = -1;
                static __thread uint32_t l_gen;
                uint16_t count;
                if (len - pos < 3) return pos;
                count = be16(p + pos + 1);
                if ((op & 7) != l_vat || l_gen != gx_cp_gen) { gx_vertex_layout(op & 7, &L); l_vat = op & 7; l_gen = gx_cp_gen; }
                need = 3 + (uint32_t)count * (uint32_t)L.size;
                if (len - pos < need) return pos;
                gx.stats.draws++;
                s_draw_count++;
                gx.stats.vertices += count;
                gx_trace_draw = (gx_trace_index >= 0 && s_draw_count - 1 == gx_trace_index);
                {   /* RL_CONTAR_DIBUJOS=1 (pruebas): una linea por dibujo con op, vat, vertices, si va en lista */
                    static int c = -1; if (c < 0) c = getenv("RL_CONTAR_DIBUJOS") != NULL;
                    if (c) { uint32_t q; rt_log("[dib] %u op %02X n %u dl %d tex0 %06X gen %06X cm %06X z %02X ind0 %06X tam %u:", s_draw_count, op & 0xF8, count, s_dl_depth, gx.bp_regs[0x94] & 0xFFFFFF,
                        gx.bp_regs[0x00] & 0xFFFFFF, gx.bp_regs[0x41] & 0xFFFFFF, gx.bp_regs[0x40] & 0xFF, gx.bp_regs[0x10] & 0xFFFFFF, (uint32_t)L.size);
                        for (q = 0; q < (uint32_t)L.size * 2 && q < 96; q++) rt_log("%s%02X", q % (uint32_t)L.size ? "" : " ", p[pos + 3 + q]);
                        rt_log("\n"); }
                }
                {   /* el registro avisa la primera vez que el juego dibuja lineas o puntos */
                    static int vis[8];
                    int pr = (op >> 3) & 7;
                    if (pr >= 5 && !vis[pr]) { vis[pr] = 1; rt_log("[gx] el juego dibuja %s (dibujo %u, %u vertices)\n", pr == 7 ? "puntos" : pr == 6 ? "tiras de lineas" : "lineas", s_draw_count, count); }
                }
                if (!gx_skip_draws && (gx_draw_limit < 0 || s_draw_count < gx_draw_limit) && (s_draw_count - 1 < gx_draw_skip || s_draw_count - 1 > gx_draw_skip_to)) gx_render_draw((op >> 3) & 7, op & 7, count, p + pos + 3, (uint32_t)L.size);
                pos += need;
                continue;
            }
            {
                static int bad;
                if (bad++ < 20) rt_log("[gx] comando desconocido 0x%02X en la cola (se descarta)\n", op);
            }
            pos++;
            continue;
        }
    }
    return pos;
}

FILE* gx_fifo_dump;
int gx_sync_mode;                 /* --gx-sync: decodificar en el hilo del juego (depuracion) */

/*
 * El procesador grafico corre en su propio hilo, como en la consola: el juego
 * escribe comandos en una cola circular y sigue; este hilo los decodifica y
 * dibuja. Las sincronizaciones del juego (tokens y "fin de dibujo" del PE)
 * llegan como interrupciones cuando el hilo grafico las alcanza.
 *
 * Ademas de los bytes de GX, la cola lleva "comandos del anfitrion" (por
 * ejemplo, mostrar un cuadro en el retrazado vertical), que se ejecutan en
 * orden cuando el hilo grafico llega a esa posicion de la cola.
 */
#define RING_SIZE (32u << 20)
static uint8_t* s_ring;
static volatile uint64_t s_wpos, s_rpos;        /* posiciones absolutas (no se reinician) */
static pthread_mutex_t s_ring_m = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_ring_cv = PTHREAD_COND_INITIALIZER;     /* hay datos / hay lugar */
static pthread_cond_t s_idle_cv = PTHREAD_COND_INITIALIZER;     /* el hilo grafico termino todo */
static int s_consumer_waiting, s_producer_waiting, s_busy;
static pthread_t s_thread;
static int s_thread_started;

#define MAX_HOST_CMDS 256
typedef struct { uint64_t pos; GXHostFn fn; uint32_t arg; } HostCmd;
static HostCmd s_hcmd[MAX_HOST_CMDS];
static volatile uint32_t s_hcmd_w, s_hcmd_r;

/* ---- captura de un cuadro (para depurar el dibujado sin correr el juego) ---------
 * --capturar N guarda en build/cuadro_N.gxc el estado de GX al empezar el cuadro N,
 * los bytes de la cola de ese cuadro y la RAM al terminarlo. --reproducir lo vuelve
 * a dibujar en segundos. */
static uint8_t* s_cap;
static size_t s_cap_len, s_cap_cap;
static GXState* s_cap_state;
static uint8_t* s_cap_tmem;
extern uint8_t s_tmem[1 << 20];
uint32_t gx_capture_frame;      /* 0 = no capturar */

static void cap_append(const uint8_t* d, uint32_t n) {
    if (!s_cap) return;
    if (s_cap_len + n > s_cap_cap) {
        s_cap_cap = (s_cap_len + n) * 2;
        s_cap = (uint8_t*)realloc(s_cap, s_cap_cap);
    }
    memcpy(s_cap + s_cap_len, d, n);
    s_cap_len += n;
}

static uint32_t s_cap_frame;
static uint8_t* s_cap_efb;
static size_t s_cap_efb_n;
void gx_capture_begin(uint32_t frame) {
    s_cap_frame = frame + 2;
    s_cap_cap = 1u << 20; s_cap_len = 0;
    s_cap = (uint8_t*)malloc(s_cap_cap);
    s_cap_state = (GXState*)malloc(sizeof(GXState));
    memcpy(s_cap_state, &gx, sizeof gx);
    s_cap_tmem = (uint8_t*)malloc(sizeof s_tmem);
    memcpy(s_cap_tmem, s_tmem, sizeof s_tmem);
    s_cap_len = 0;
    {   /* las copias del EFB a textura que hay en la placa en este momento */
        extern int render_gl;
        uint8_t* gl_save_efb_copies(size_t* out_n);
        free(s_cap_efb); s_cap_efb = NULL; s_cap_efb_n = 0;
        if (render_gl) s_cap_efb = gl_save_efb_copies(&s_cap_efb_n);
    }
    /* lo que quedo a medias en la cola de comandos pertenece a este cuadro */
    cap_append(s_fifo, s_fifo_len);
}

void gx_capture_end(uint32_t xfb) {
    void video_dump_xfb(const char* path, uint32_t xfb);
    char path[256];
    FILE* f;
    uint32_t hdr[4];
    if (!s_cap) return;
    {   /* en la carpeta build/ si existe (desarrollo); si no, en capturas/ (F12 del usuario) */
        FILE* t = fopen("build/.captura_prueba", "wb");
        if (t) { fclose(t); remove("build/.captura_prueba"); snprintf(path, sizeof path, "build/cuadro_%u.gxc", s_cap_frame); }
        else {
#ifdef _WIN32
            _mkdir("capturas");
#else
            mkdir("capturas", 0755);
#endif
            snprintf(path, sizeof path, "capturas/cuadro_%u.gxc", s_cap_frame);
        }
    }
    f = fopen(path, "wb");
    if (f) {
        hdr[0] = 0x47584331u; hdr[1] = xfb; hdr[2] = (uint32_t)sizeof(GXState); hdr[3] = (uint32_t)s_cap_len;
        fwrite(hdr, sizeof hdr, 1, f);
        fwrite(s_cap_state, sizeof(GXState), 1, f);
        fwrite(s_cap_tmem, sizeof s_tmem, 1, f);
        fwrite(mem_ptr(MEM_BASE), 1, MEM_SIZE, f);
        fwrite(s_cap, 1, s_cap_len, f);
        if (s_cap_efb && s_cap_efb_n) fwrite(s_cap_efb, 1, s_cap_efb_n, f);   /* opcional: los lectores viejos lo ignoran */
        fclose(f);
        rt_log("[gx] cuadro capturado en %s (%zu bytes de comandos)\n", path, s_cap_len);
        {   /* y una foto de lo que se ve */
            char png[272];
            snprintf(png, sizeof png, "%.*s.png", (int)(strlen(path) - 4), path);
            if (!render_dump_xfb(png, xfb)) video_dump_xfb(png, xfb);
        }
    }
    free(s_cap); s_cap = NULL;
    free(s_cap_state); free(s_cap_tmem);
}

int gx_en_fifo(const void* p) { return (const uint8_t*)p >= s_fifo && (const uint8_t*)p < s_fifo + sizeof s_fifo; }
static void consume(const uint8_t* data, uint32_t n) {
    uint32_t i = 0;
    { extern volatile const char* gl_etapa; gl_etapa = "procesando comandos de GX"; }
    cap_append(data, n);
    while (i < n) {
        uint32_t chunk = n - i, room = (uint32_t)sizeof s_fifo - s_fifo_len, used;
        if (chunk > room) chunk = room;
        memcpy(s_fifo + s_fifo_len, data + i, chunk);
        s_fifo_len += chunk;
        i += chunk;
        used = parse(s_fifo, s_fifo_len);
        { void gl_vtx_esperar_fifo(void); gl_vtx_esperar_fifo(); }   /* los dibujos que apuntan a s_fifo (que se mueve abajo) */
        if (used) {
            memmove(s_fifo, s_fifo + used, s_fifo_len - used);
            s_fifo_len -= used;
        } else if (s_fifo_len == sizeof s_fifo) {
            rt_log("[gx] comando mas grande que la cola: se descartan %u bytes\n", s_fifo_len);
            s_fifo_len = 0;
        }
    }
}

static void run_host_cmds(uint64_t upto) {
    while (s_hcmd_r != s_hcmd_w) {
        HostCmd* c = &s_hcmd[s_hcmd_r % MAX_HOST_CMDS];
        if (c->pos > upto) break;
        c->fn(c->arg);
        __atomic_store_n(&s_hcmd_r, s_hcmd_r + 1, __ATOMIC_RELEASE);
    }
}

static void* gx_thread_main(void* arg) {
    (void)arg;
    cpu_register_thread(CPU_GRAFICO);
    gx_backend_thread_init();
    for (;;) {
        uint64_t w, r;
        {   /* antes de dormir, esperar un poco girando: despertar al hilo cuesta una
               llamada al sistema y el juego manda los comandos de a poco */
            uint64_t t_spin = 0;
            int k;
            for (k = 0; __atomic_load_n(&s_wpos, __ATOMIC_ACQUIRE) == s_rpos &&
                        __atomic_load_n(&s_hcmd_w, __ATOMIC_ACQUIRE) == s_hcmd_r; k++) {
                if ((k & 63) == 63) {
                    uint64_t now = os_now_us();
                    if (!t_spin) t_spin = now;
                    else if (now - t_spin > 100) break;
                    sched_yield();
                }
#if defined(__x86_64__) || defined(__i386__)
                __builtin_ia32_pause();
#endif
            }
        }
        pthread_mutex_lock(&s_ring_m);
        while ((w = s_wpos) == (r = s_rpos) && s_hcmd_r == s_hcmd_w) {
            s_busy = 0;
            pthread_cond_broadcast(&s_idle_cv);
            if (s_producer_waiting) pthread_cond_broadcast(&s_ring_cv);
            s_consumer_waiting = 1;
            { extern volatile const char* gl_etapa; gl_etapa = "esperando comandos del juego"; }
            pthread_cond_wait(&s_ring_cv, &s_ring_m);
            s_consumer_waiting = 0;
        }
        s_busy = 1;
        pthread_mutex_unlock(&s_ring_m);
        {
            uint64_t t0 = os_now_us();
        /* procesar hasta el proximo comando del anfitrion (o todo lo que haya) */
        while (r < w) {
            uint64_t stop = w;
            uint32_t off, n;
            if (s_hcmd_r != s_hcmd_w) {
                uint64_t hp = s_hcmd[s_hcmd_r % MAX_HOST_CMDS].pos;
                if (hp < stop) stop = hp;
            }
            if (stop <= r) { run_host_cmds(r); continue; }
            off = (uint32_t)(r % RING_SIZE);
            n = (uint32_t)(stop - r);
            if (n > RING_SIZE - off) n = RING_SIZE - off;
            consume(s_ring + off, n);
            r += n;
            __atomic_store_n(&s_rpos, r, __ATOMIC_RELEASE);
            run_host_cmds(r);
            if (__atomic_load_n(&s_producer_waiting, __ATOMIC_ACQUIRE)) {
                pthread_mutex_lock(&s_ring_m);
                pthread_cond_broadcast(&s_ring_cv);
                pthread_mutex_unlock(&s_ring_m);
            }
        }
        run_host_cmds(r);
        gx_backend_idle();
            gx_busy_us += os_now_us() - t0;
        }
    }
    return NULL;
}

static void start_thread(void) {
    s_ring = (uint8_t*)malloc(RING_SIZE);
    s_thread_started = 1;
    {
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, 16u << 20);
        pthread_create(&s_thread, &attr, gx_thread_main, NULL);
    }
}

/* tiempos acumulados para el detalle de los cuadros lentos ([cuadro] en video.c) */
uint64_t g_t_cola_us, g_t_sync_us, g_t_graf_us, g_t_tex_us, g_prog_nuevos_total;
static void ring_push(const uint8_t* b, uint32_t n) {
    uint64_t w = s_wpos;
    uint32_t i;
    if (w + n - __atomic_load_n(&s_rpos, __ATOMIC_ACQUIRE) > RING_SIZE) {
        uint64_t te = os_now_us();
        pthread_mutex_lock(&s_ring_m);
        while (w + n - s_rpos > RING_SIZE) {
            s_producer_waiting = 1;
            if (s_consumer_waiting) pthread_cond_broadcast(&s_ring_cv);
            pthread_cond_wait(&s_ring_cv, &s_ring_m);
        }
        s_producer_waiting = 0;
        pthread_mutex_unlock(&s_ring_m);
        g_t_cola_us += os_now_us() - te;
    }
    {   /* copia en uno o dos tramos (la cola es circular) */
        uint32_t at = (uint32_t)(w % RING_SIZE), first = RING_SIZE - at < n ? RING_SIZE - at : n;
        memcpy(s_ring + at, b, first);
        if (first < n) memcpy(s_ring, b + first, n - first);
        (void)i;
    }
    __atomic_store_n(&s_wpos, w + n, __ATOMIC_RELEASE);
}

/* Despierta al hilo grafico si esta dormido (se llama cada tanto, no en cada byte). */
static void kick(void) {
    if (s_consumer_waiting) {
        pthread_mutex_lock(&s_ring_m);
        pthread_cond_broadcast(&s_ring_cv);
        pthread_mutex_unlock(&s_ring_m);
    }
}

/* Las escrituras al tubo se juntan de a cientos de bytes antes de pasarlas a la cola
 * del hilo grafico; gx_fifo_flush() las manda (se llama en cada acceso a otro registro
 * del hardware, al esperar, al entregar interrupciones y en los bucles). */
static uint8_t s_stage[2048];
static uint32_t s_stage_n;

void gx_fifo_flush(void) {
    if (!s_stage_n) return;
    if (!s_thread_started) start_thread();
    ring_push(s_stage, s_stage_n);
    s_stage_n = 0;
    kick();
}

static void gx_fifo_write_lento(uint32_t value, int size);
static int s_fifo_lento = -1;   /* volcado, modo sincronico o RL_QUIEN_DIBUJA: el camino con todo */
void gx_fifo_write(uint32_t value, int size) {
    uint8_t* b;
    if (__builtin_expect(s_fifo_lento != 0, 0)) {
        if (s_fifo_lento < 0) s_fifo_lento = gx_fifo_dump || gx_sync_mode || getenv("RL_QUIEN_DIBUJA") != NULL;
        if (s_fifo_lento || gx_fifo_dump || gx_sync_mode) { s_fifo_lento = 1; gx_fifo_write_lento(value, size); return; }
    }
    if (__builtin_expect(gx_fifo_dump != NULL || gx_sync_mode, 0)) { gx_fifo_write_lento(value, size); return; }
    b = s_stage + s_stage_n;
    if (size == 4) {
        uint32_t t = __builtin_bswap32(value);
        if (__builtin_expect((value >> 24) == 0x45 && (value & 3) == 2 && s_stage_n && s_stage[s_stage_n - 1] == 0x61, 0)) {
            extern volatile uint64_t gx_cpu_finish_t;
            extern volatile uint32_t gx_finish_requests;
            gx_cpu_finish_t = os_now_us();
            __atomic_add_fetch(&gx_finish_requests, 1, __ATOMIC_RELEASE);
            { extern volatile uint32_t gx_finish_pendiente; __atomic_store_n(&gx_finish_pendiente, 1, __ATOMIC_RELEASE); }
        }
        memcpy(b, &t, 4);
    } else if (size == 2) { uint16_t t = __builtin_bswap16((uint16_t)value); memcpy(b, &t, 2); }
    else b[0] = (uint8_t)value;
    s_stage_n += (uint32_t)size;
    if (__builtin_expect(s_stage_n >= sizeof s_stage - 8, 0)) {
        if (!s_thread_started) start_thread();
        ring_push(s_stage, s_stage_n);
        s_stage_n = 0;
        if (s_consumer_waiting) kick();
    }
}
static void gx_fifo_write_lento(uint32_t value, int size) {
    uint8_t* b;
    int i;
    if (gx_fifo_dump || gx_sync_mode) {
        uint8_t t[4];
        for (i = 0; i < size; i++) t[i] = (uint8_t)(value >> (8 * (size - 1 - i)));
        if (gx_fifo_dump) fwrite(t, 1, (size_t)size, gx_fifo_dump);
        if (gx_sync_mode) { consume(t, (uint32_t)size); return; }
    }
#if defined(__linux__)
    {   /* RL_QUIEN_DIBUJA=XX (pruebas): quien (en el codigo del juego) manda el comando XX */
        static int want = -2, n;
        if (want == -2) { const char* e = getenv("RL_QUIEN_DIBUJA"); want = e ? (int)strtol(e, NULL, 16) : -1; }
        if (want >= 0 && n < 400 && ((size == 1 && (int)value == want) || (size == 4 && (int)(value >> 24) == want))) {
            void* bt[10]; int k, m = backtrace(bt, 10); extern int main(int, char**);
            n++;
            rt_log("[quien] %02X:", want);
            for (k = 1; k < m; k++) rt_log(" %lx", (unsigned long)((uintptr_t)bt[k] - (uintptr_t)main));
            rt_log("\n");
        }
    }
#endif
    b = s_stage + s_stage_n;
    if (size == 4 && (value >> 24) == 0x45 && (value & 3) == 2 && s_stage_n && s_stage[s_stage_n - 1] == 0x61) {
        extern volatile uint64_t gx_cpu_finish_t;   /* cuando el juego pidio el fin de dibujo (registro [cuadro]) */
        extern volatile uint32_t gx_finish_requests;
        gx_cpu_finish_t = os_now_us();
        __atomic_add_fetch(&gx_finish_requests, 1, __ATOMIC_RELEASE);
        { extern volatile uint32_t gx_finish_pendiente; __atomic_store_n(&gx_finish_pendiente, 1, __ATOMIC_RELEASE); }
    }
    if (size == 4) { uint32_t t = __builtin_bswap32(value); memcpy(b, &t, 4); }
    else if (size == 2) { uint16_t t = __builtin_bswap16((uint16_t)value); memcpy(b, &t, 2); }
    else b[0] = (uint8_t)value;
    s_stage_n += (uint32_t)size;
    if (s_stage_n >= sizeof s_stage - 8) {
        if (!s_thread_started) start_thread();
        ring_push(s_stage, s_stage_n);
        s_stage_n = 0;
        if (s_consumer_waiting) kick();
    }
}

/* Encola una funcion para que el hilo grafico la ejecute en orden con los comandos. */
void gx_host_command(GXHostFn fn, uint32_t arg) {
    if (gx_sync_mode) { fn(arg); return; }
    gx_fifo_flush();
    if (!s_thread_started) start_thread();
    while (s_hcmd_w - __atomic_load_n(&s_hcmd_r, __ATOMIC_ACQUIRE) >= MAX_HOST_CMDS) { kick(); sched_yield(); }
    s_hcmd[s_hcmd_w % MAX_HOST_CMDS].pos = s_wpos;
    s_hcmd[s_hcmd_w % MAX_HOST_CMDS].fn = fn;
    s_hcmd[s_hcmd_w % MAX_HOST_CMDS].arg = arg;
    __atomic_store_n(&s_hcmd_w, s_hcmd_w + 1, __ATOMIC_RELEASE);
    pthread_mutex_lock(&s_ring_m);
    pthread_cond_broadcast(&s_ring_cv);
    pthread_mutex_unlock(&s_ring_m);
}

/* Espera a que el hilo grafico procese todo lo encolado. */
void gx_sync(void) {
    gx_fifo_flush();
    if (gx_sync_mode || !s_thread_started) return;
    {   uint64_t te = os_now_us();
    pthread_mutex_lock(&s_ring_m);
    pthread_cond_broadcast(&s_ring_cv);
    while (s_busy || s_wpos != s_rpos || s_hcmd_r != s_hcmd_w) pthread_cond_wait(&s_idle_cv, &s_ring_m);
    pthread_mutex_unlock(&s_ring_m);
    g_t_sync_us += os_now_us() - te; }
}

void gx_init(void) {
    memset(&gx, 0, sizeof gx); gx_cp_gen++;
    gx.bp_mask = 0xFFFFFF;
}

static const char* replay_dump_path;
static void replay_dump(uint32_t xfb) {
    char p[600];
    void video_dump_xfb(const char* path, uint32_t xfb);
    if (!render_dump_xfb(replay_dump_path, xfb)) video_dump_xfb(replay_dump_path, xfb);
    snprintf(p, sizeof p, "%s.efb.png", replay_dump_path);
    render_dump_efb(p);
}

static uint8_t* s_replay_efb;
static size_t s_replay_efb_n;
static void replay_load_efb(uint32_t arg) {
    void gl_load_efb_copies(const uint8_t* d, size_t n);
    extern int render_gl;
    (void)arg;
    if (render_gl) gl_load_efb_copies(s_replay_efb, s_replay_efb_n);
}

/* Reproduce un cuadro capturado y guarda la imagen. */
int gx_replay(const char* path, const char* out_png) {
    FILE* f = fopen(path, "rb");
    uint32_t hdr[4];
    uint8_t* cmds;
    if (!f) { rt_log("[gx] no se pudo abrir %s\n", path); return 1; }
    if (fread(hdr, sizeof hdr, 1, f) != 1 || hdr[0] != 0x47584331u || hdr[2] != sizeof(GXState)) { rt_log("[gx] archivo de captura invalido\n"); fclose(f); return 1; }
    if ((gx_cp_gen++, fread(&gx, sizeof gx, 1, f)) != 1 || fread(s_tmem, sizeof s_tmem, 1, f) != 1 ||
        fread(mem_ptr(MEM_BASE), 1, MEM_SIZE, f) != MEM_SIZE) { fclose(f); return 1; }
    cmds = (uint8_t*)malloc(hdr[3] ? hdr[3] : 1);
    if (fread(cmds, 1, hdr[3], f) != hdr[3]) { fclose(f); return 1; }
    {   /* copias del EFB guardadas (capturas nuevas) */
        long here = ftell(f), end;
        fseek(f, 0, SEEK_END); end = ftell(f); fseek(f, here, SEEK_SET);
        if (end > here && !getenv("RL_SIN_COPIAS_CAPTURA")) {
            s_replay_efb_n = (size_t)(end - here);
            s_replay_efb = (uint8_t*)malloc(s_replay_efb_n);
            if (fread(s_replay_efb, 1, s_replay_efb_n, f) != s_replay_efb_n) s_replay_efb_n = 0;
        }
    }
    fclose(f);
    {
        const char* lim = getenv("RL_DRAW_LIMIT");
        if (lim) gx_draw_limit = atoi(lim);
        lim = getenv("RL_DRAW_TRACE");
        if (lim) gx_trace_index = atoi(lim);
        lim = getenv("RL_DRAW_SKIP");
        if (lim) { gx_draw_skip = atoi(lim); gx_draw_skip_to = strchr(lim, '-') ? atoi(strchr(lim, '-') + 1) : gx_draw_skip; }
    }
    s_draw_count = 0;
    gx_sync_mode = 0;
    {   extern uint64_t gx_replay_v0, gx_replay_v0d; gx_replay_v0 = gx.stats.vertices; gx_replay_v0d = gx.stats.draws; }
    {
        uint32_t i;
        int loop, loops = getenv("RL_REPLAY_LOOPS") ? atoi(getenv("RL_REPLAY_LOOPS")) : 1;
        static GXState gx0;
        uint64_t t0;
        if (loops < 1) loops = 1;
        gx0 = gx;
        if (!s_thread_started) start_thread();
        if (s_replay_efb_n) { gx_host_command(replay_load_efb, 0); gx_sync(); }
        t0 = os_now_us();
        for (loop = 0; loop < loops; loop++) {
            if (loop) { gx_sync(); gx = gx0; gx_cp_gen++; gx_xf_gen++; gx_xfreg_gen++; gx_bp_gen++; gx_xf_dirty = ~0ull; }
            for (i = 0; i < hdr[3]; i += 4096) {
                uint32_t n = hdr[3] - i < 4096 ? hdr[3] - i : 4096;
                ring_push(cmds + i, n);
                kick();
            }
        }
        gx_sync();
        if (loops > 1) rt_log("[gx] %d repeticiones: %.2f ms por cuadro\n", loops, (os_now_us() - t0) / 1000.0 / loops);
        { extern uint64_t gl_vj_esperas, gl_vj_trabajos, gl_vj_fifo_trab;
          rt_log("[gx] lectura de vertices: %llu trabajos (%llu de la cola), el hilo grafico tuvo que esperar %llu veces\n",
                 (unsigned long long)gl_vj_trabajos, (unsigned long long)gl_vj_fifo_trab, (unsigned long long)gl_vj_esperas); }
    }
    gx_sync();
    { void gl_perfil_informe(uint32_t); gx_host_command(gl_perfil_informe, 0); gx_sync(); }
    {
        static char outp[512];
        snprintf(outp, sizeof outp, "%s", out_png);
        replay_dump_path = outp;
        gx_host_command(replay_dump, hdr[1]);
        gx_sync();
    }
    { extern uint64_t gl_stat_draw_calls, gl_stat_tex_uploads, gl_stat_xf_snaps, gl_stat_uniform_uploads, gl_stat_shaders;
      rt_log("[gx] reproducidos %u bytes, %d dibujos (%llu vertices) -> %llu llamadas a OpenGL, %llu texturas subidas, %llu estados de XF, %llu bloques de uniforms, %llu shaders\n", hdr[3], s_draw_count, (unsigned long long)(gx.stats.vertices - gx_replay_v0),
             (unsigned long long)gl_stat_draw_calls, (unsigned long long)gl_stat_tex_uploads, (unsigned long long)gl_stat_xf_snaps, (unsigned long long)gl_stat_uniform_uploads, (unsigned long long)gl_stat_shaders); }
    return 0;
}
