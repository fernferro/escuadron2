#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
/*
 * Salida de video: en cada retrazado se puede guardar el framebuffer externo
 * (XFB, formato YUYV) como imagen PNG para revisarlo.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "runtime.h"
#include "gx.h"
#include "os.h"
#include "input.h"

uint32_t vi_xfb_addr(void);
uint32_t vi_xfb_stride(void);
uint32_t vi_xfb_height(void);

static unsigned s_frames;
const char* video_dump_dir;
unsigned video_dump_every = 60;
unsigned video_max_frames;

static uint8_t clamp(int v) { return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v); }

/* PNG sin compresion (bloques "stored" de zlib): no hace falta libpng. */
static uint32_t s_crc_table[256];

static uint32_t crc32_update(uint32_t c, const uint8_t* p, size_t n) {
    size_t i;
    if (!s_crc_table[1]) {
        uint32_t k, v;
        for (k = 0; k < 256; k++) {
            v = k;
            for (int j = 0; j < 8; j++) v = (v & 1) ? 0xEDB88320u ^ (v >> 1) : v >> 1;
            s_crc_table[k] = v;
        }
    }
    for (i = 0; i < n; i++) c = s_crc_table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c;
}

static void put32(uint8_t* p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }

static void png_chunk(FILE* f, const char* type, const uint8_t* data, uint32_t len) {
    uint8_t b[4];
    uint32_t c;
    put32(b, len);
    fwrite(b, 1, 4, f);
    fwrite(type, 1, 4, f);
    if (len) fwrite(data, 1, len, f);
    c = crc32_update(0xFFFFFFFFu, (const uint8_t*)type, 4);
    c = crc32_update(c, data, len) ^ 0xFFFFFFFFu;
    put32(b, c);
    fwrite(b, 1, 4, f);
}

int video_write_png(const char* path, const uint8_t* rgb, int w, int h) {
    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    FILE* f = fopen(path, "wb");
    size_t row = (size_t)w * 3 + 1, raw_len = row * (size_t)h, nblocks, zlen, i, pos;
    uint8_t* raw;
    uint8_t* z;
    uint8_t ihdr[13];
    uint32_t a = 1, b = 0;
    int y;
    if (!f) return 0;
    raw = (uint8_t*)malloc(raw_len);
    for (y = 0; y < h; y++) {
        raw[(size_t)y * row] = 0;              /* sin filtro */
        memcpy(raw + (size_t)y * row + 1, rgb + (size_t)y * w * 3, (size_t)w * 3);
    }
    nblocks = (raw_len + 65534) / 65535;
    zlen = 2 + raw_len + nblocks * 5 + 4;
    z = (uint8_t*)malloc(zlen);
    z[0] = 0x78; z[1] = 0x01;
    pos = 2;
    for (i = 0; i < raw_len; i += 65535) {
        size_t n = raw_len - i < 65535 ? raw_len - i : 65535;
        z[pos++] = (uint8_t)(i + n >= raw_len);
        z[pos++] = (uint8_t)n; z[pos++] = (uint8_t)(n >> 8);
        z[pos++] = (uint8_t)~n; z[pos++] = (uint8_t)(~n >> 8);
        memcpy(z + pos, raw + i, n);
        pos += n;
    }
    for (i = 0; i < raw_len; i++) { a = (a + raw[i]) % 65521; b = (b + a) % 65521; }
    put32(z + pos, (b << 16) | a);
    pos += 4;
    fwrite(sig, 1, 8, f);
    put32(ihdr, (uint32_t)w); put32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    png_chunk(f, "IHDR", ihdr, 13);
    png_chunk(f, "IDAT", z, (uint32_t)pos);
    png_chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(raw); free(z);
    return 1;
}

extern uint32_t video_xfb_stride;  /* ancho de linea de la ultima copia a la XFB */

void video_dump_xfb(const char* path, uint32_t a) {
    uint32_t stride = video_xfb_stride ? video_xfb_stride : 1280;
    int w = 640, h = (int)vi_xfb_height(), x, y;
    uint8_t* rgb;
    const uint8_t* src = mem_ptr(a);
    if (!src || !mem_ptr(a + stride * h - 1)) { rt_log("[video] XFB invalido 0x%08X\n", a); return; }
    rgb = (uint8_t*)malloc((size_t)w * h * 3);
    for (y = 0; y < h; y++) {
        const uint8_t* row = src + y * stride;
        for (x = 0; x < w; x += 2) {
            int y0 = row[x * 2], u = row[x * 2 + 1] - 128, y1 = row[x * 2 + 2], v = row[x * 2 + 3] - 128;
            uint8_t* o = rgb + (y * w + x) * 3;
            int i;
            for (i = 0; i < 2; i++) {
                int yy = (i ? y1 : y0) - 16;
                o[i * 3 + 0] = clamp((298 * yy + 409 * v + 128) >> 8);
                o[i * 3 + 1] = clamp((298 * yy - 100 * u - 208 * v + 128) >> 8);
                o[i * 3 + 2] = clamp((298 * yy + 516 * u + 128) >> 8);
            }
        }
    }
    video_write_png(path, rgb, w, h);
    free(rgb);
}

int video_dump_efb;
static uint32_t s_xfb_at[64];

/* En el hilo grafico, en orden con los comandos: se muestra / guarda el cuadro. */
static uint32_t s_game_frames;
uint32_t video_game_frames(void) { return s_game_frames; }
static uint32_t s_dump_xfb[64];

/* En el hilo grafico, en orden con los comandos. */
static unsigned s_presents;               /* cuadros mostrados en la ventana */
static volatile int s_present_pending;   /* ya hay un cuadro esperando para mostrarse */
static void video_present_gx(uint32_t frame) {
    __atomic_store_n(&s_present_pending, 0, __ATOMIC_RELEASE);
    __atomic_add_fetch(&s_presents, 1, __ATOMIC_RELAXED);
    render_present(s_xfb_at[frame & 63]);
}

static void video_dump_gx(uint32_t frame) {
    uint32_t xfb = s_dump_xfb[frame & 63];
    char p[512];
    snprintf(p, sizeof p, "%s/xfb_%05u.png", video_dump_dir, frame);
    if (!render_dump_xfb(p, xfb)) video_dump_xfb(p, xfb);
    if (video_dump_efb) {
        snprintf(p, sizeof p, "%s/efb_%05u.png", video_dump_dir, frame);
        render_dump_efb(p);
    }
    {
        static uint64_t ld, lc, lv; extern uint64_t interp_insns;
        rt_log("[video] cuadro %u: xfb=0x%08X dibujos=%llu vertices=%llu copias=%llu tex=%llu interp=%llu\n", frame, xfb,
               (unsigned long long)(gx.stats.draws - ld), (unsigned long long)(gx.stats.vertices - lv),
               (unsigned long long)(gx.stats.copies - lc), (unsigned long long)gx.stats.tex_copies, (unsigned long long)interp_insns);
        ld = gx.stats.draws; lc = gx.stats.copies; lv = gx.stats.vertices;
    }
}

/* Retrazado vertical (60 por segundo): se muestra la XFB actual en la ventana. */
/* Depuracion de MusyX (RL_TRACE_VOICES=1): cambios de estado de las voces y streams. */
static void trace_voices(void) {
    static int on = -1;
    static uint8_t vst[64], sst[64];
    uint32_t base;
    int v;
    if (on < 0) on = getenv("RL_TRACE_VOICES") != NULL;
    if (!on) return;
    base = R32(0x80096574);
    if (!base) return;
    for (v = 0; v < 64; v++) {
        uint32_t a = base + v * 0xF4;
        uint8_t st = R8(a + 0xEC);
        if (st != vst[v])
            rt_log("[voz] cuadro %u: voz %d estado %d->%d fmt %d cur %08X ini %08X\n", s_game_frames, v, vst[v], st,
                   R8(a + 0x90), R32(a + 0x20), R32(a + 0x78));
        vst[v] = st;
    }
    for (v = 0; v < 64; v++) {
        uint32_t a = 0x80076A48 + v * 0x60;
        uint8_t st = R8(a + 8);
        if (st != sst[v])
            rt_log("[stream] cuadro %u: stream %d (id %u) estado %d->%d voz %u pos %08X\n", s_game_frames, v, R32(a), sst[v], st,
                   R32(a + 0x48), R32(a + 0x1C));
        sst[v] = st;
    }
}

/* ritmo: cuantos retrazados dura cada cuadro del juego y cuanto tarda el fin de dibujo */
unsigned video_ritmo_hist[4];
volatile uint64_t video_frame_t0;          /* cuando empezo el cuadro actual del juego */
volatile int video_finish_pend;            /* esperando el primer fin de dibujo del cuadro */
uint64_t video_finish_sum, video_finish_max, video_finish_n;
static uint64_t s_retrace_t;               /* cuando fue el ultimo retrazado */
static uint64_t s_phase_sum, s_phase_n, s_phase_late;
void video_retrace(void) {
    static uint64_t t0; static unsigned f0, g0, p0;
    s_frames++;
    s_retrace_t = os_now_us();
    trace_voices();
    {   /* cada 5 segundos se anota la velocidad (para comparar maquinas) */
        uint64_t now = os_now_us();
        if (!t0) { t0 = now; f0 = s_frames; g0 = s_game_frames; p0 = s_presents; }
        else if (now - t0 >= 5000000u) {
            double sec = (double)(now - t0) / 1e6;
            extern uint64_t gx_busy_us, os_idle_us, gl_stat_draw_calls, gl_stat_tex_uploads;
            static uint64_t b0, i0, d0, c0, u0, k0, sw0, cj0, cg0, cd0;
            uint64_t cj = cpu_category_us(CPU_JUEGO), cg = cpu_category_us(CPU_GRAFICO), cd = cpu_category_us(CPU_DSP);
            double gf = (double)(s_game_frames - g0) > 0 ? (double)(s_game_frames - g0) : 1.0;
            extern uint64_t os_switches;
            extern int dsp_lle_enabled; uint64_t dsp_lle_cycles(void);
            uint64_t k = dsp_lle_enabled ? dsp_lle_cycles() : 0;
            {   /* el juego no avanza y no espera: decir donde gira */
                extern uint64_t os_idle_us; void os_informe_giro(int);
                static int quietas, avisos;
                double ocioso = (double)(os_idle_us - i0) / (sec * 1e4);
                if (s_game_frames == g0 && ocioso < 20) quietas++; else quietas = 0;
                os_informe_giro(quietas == 1 && avisos++ < 3);
            }
            { extern uint32_t hw_ter_max, hw_ter_cortes, hw_ter_polls_max, hw_ter_escapes, hw_ter_salidas_largas; static uint32_t c0, e0, l0; if (hw_ter_max) rt_log("[terreno] pasos maximos %u, cortes %u, vueltas maximas %u x2048, filas terminadas a la fuerza %u, salidas %u\n", hw_ter_max, hw_ter_cortes - c0, hw_ter_polls_max, hw_ter_escapes - e0, hw_ter_salidas_largas - l0); c0 = hw_ter_cortes; e0 = hw_ter_escapes; l0 = hw_ter_salidas_largas; hw_ter_max = 0; hw_ter_polls_max = 0; }
            rt_log("[velocidad] %.1f cuadros del juego/s, %.1f retrazados/s (60 = tiempo real), %.1f mostrados/s | "
                   "grafico ocupado %.0f%%, juego ocioso %.0f%%, dibujos %.0f/s -> %.0f llamadas/s, texturas %.0f/s, DSP %.0f MHz, cambios de hilo %.0f/s | ms de CPU por cuadro: juego %.1f, grafico %.1f, DSP %.1f (%.1f ns por ciclo)\n",
                   (s_game_frames - g0) / sec, (s_frames - f0) / sec, (s_presents - p0) / sec,
                   (double)(gx_busy_us - b0) / (sec * 1e4), (double)(os_idle_us - i0) / (sec * 1e4),
                   (double)(gx.stats.draws - d0) / sec, (double)(gl_stat_draw_calls - c0) / sec, (double)(gl_stat_tex_uploads - u0) / sec, (double)(k - k0) / (sec * 1e6), (double)(os_switches - sw0) / sec,
                   (double)(cj - cj0) / 1000.0 / gf, (double)(cg - cg0) / 1000.0 / gf, (double)(cd - cd0) / 1000.0 / gf, k > k0 ? (double)(cd - cd0) * 1000.0 / (double)(k - k0) : 0.0);
            {   /* segunda linea: donde se va el tiempo del hilo grafico y de la placa */
                extern uint64_t gl_stat_gpu_ns, gl_stat_gpu_frames, gl_stat_swap_us, gl_stat_flush_us, gl_stat_prog_switch, gl_stat_tex_binds, gl_stat_uniform_uploads, gl_stat_shaders;
                static uint64_t gn0, gf0, sw_0, fl0, ps0, tb0, uu0;
                double pf = (double)(s_presents - p0) > 0 ? (double)(s_presents - p0) : 1.0;
                double gfr = (double)(gl_stat_gpu_frames - gf0);
                { extern uint64_t gl_stat_swap_max_us; rt_log("[placa] espera maxima al mostrar %.1f ms\n", gl_stat_swap_max_us / 1000.0); gl_stat_swap_max_us = 0; }
                rt_log("[placa] placa %.1f ms por imagen, esperando al mostrar %.1f ms por imagen, mandando dibujos %.1f ms por cuadro | "
                       "cambios de programa %.0f/s, texturas enlazadas %.0f/s, uniforms %.0f/s, shaders %llu\n",
                       gfr > 0 ? (double)(gl_stat_gpu_ns - gn0) / 1e6 / gfr : 0.0, (double)(gl_stat_swap_us - sw_0) / 1000.0 / pf,
                       (double)(gl_stat_flush_us - fl0) / 1000.0 / gf, (double)(gl_stat_prog_switch - ps0) / sec,
                       (double)(gl_stat_tex_binds - tb0) / sec, (double)(gl_stat_uniform_uploads - uu0) / sec, (unsigned long long)gl_stat_shaders);
                {
                    rt_log("[ritmo] cuadros de 1/2/3+ retrazados: %u/%u/%u, fin de dibujo a los %.1f ms del cuadro (max %.1f), el juego cambia de imagen a los %.1f ms del retrazado (%llu tarde)\n",
                           video_ritmo_hist[1], video_ritmo_hist[2], video_ritmo_hist[3],
                           video_finish_n ? (double)video_finish_sum / video_finish_n / 1000.0 : 0.0, (double)video_finish_max / 1000.0,
                           s_phase_n ? (double)s_phase_sum / s_phase_n / 1000.0 : 0.0, (unsigned long long)s_phase_late);
                    s_phase_sum = s_phase_n = s_phase_late = 0;
                    { extern uint64_t hw_vi_postergados; static uint64_t p0v; rt_log("[ritmo] retrazados demorados esperando el fin de dibujo: %llu\n", (unsigned long long)(hw_vi_postergados - p0v)); p0v = hw_vi_postergados; }
                    { void os_wait_report(double); os_wait_report(sec); }
                    memset(video_ritmo_hist, 0, sizeof video_ritmo_hist); video_finish_sum = video_finish_max = video_finish_n = 0;
                }
                {
                    extern unsigned audio_cortes; extern uint64_t hw_ai_late_max_us; int audio_queued_ms(void);
                    static unsigned ac0;
                    { extern uint64_t hw_ai_juntados, hw_ai_viejos, hw_ai_esperas, hw_hueco_max_us, hw_huecos_20ms, hw_ar_dmas, hw_ar_juntados; static uint64_t j0, v0, e0, h0;
                      rt_log("[audio] cortes %u, cola %d ms, bloque mas atrasado %.1f ms | avisos juntados %llu, bloques viejos %llu, esperas %llu | hardware sin atender: max %.1f ms, %llu veces mas de 20 ms | DMA de ARAM %llu (%llu juntados)\n", audio_cortes - ac0, audio_queued_ms(), (double)hw_ai_late_max_us / 1000.0,
                             (unsigned long long)(hw_ai_juntados - j0), (unsigned long long)(hw_ai_viejos - v0), (unsigned long long)(hw_ai_esperas - e0), hw_hueco_max_us / 1000.0, (unsigned long long)(hw_huecos_20ms - h0), (unsigned long long)hw_ar_dmas, (unsigned long long)hw_ar_juntados);
                      { extern uint64_t os_video_llamadas, os_video_us; static uint64_t ml0, mu0;
                        if (os_video_llamadas != ml0) rt_log("[video] mensajes de radio: %llu llamadas al decodificador, %.1f ms de CPU por segundo\n", (unsigned long long)(os_video_llamadas - ml0), (os_video_us - mu0) / 1000.0 / sec);
                        ml0 = os_video_llamadas; mu0 = os_video_us; }
                      hw_hueco_max_us = 0; h0 = hw_huecos_20ms;
                      j0 = hw_ai_juntados; v0 = hw_ai_viejos; e0 = hw_ai_esperas; }
                    ac0 = audio_cortes; hw_ai_late_max_us = 0;
                }
                gn0 = gl_stat_gpu_ns; gf0 = gl_stat_gpu_frames; sw_0 = gl_stat_swap_us; fl0 = gl_stat_flush_us; ps0 = gl_stat_prog_switch; tb0 = gl_stat_tex_binds; uu0 = gl_stat_uniform_uploads;
            }
            k0 = k; sw0 = os_switches; cj0 = cj; cg0 = cg; cd0 = cd;
            b0 = gx_busy_us; i0 = os_idle_us; d0 = gx.stats.draws; c0 = gl_stat_draw_calls; u0 = gl_stat_tex_uploads;
            t0 = now; f0 = s_frames; g0 = s_game_frames; p0 = s_presents;
        }
    }
    s_xfb_at[s_frames & 63] = vi_xfb_addr();
    /* Si el hilo grafico todavia no mostro el cuadro anterior (por ejemplo porque la
     * placa espera al monitor), no se encola otro: si no, la cola crece sin fin y el
     * juego se queda esperando a la GPU. */
    if (!__atomic_exchange_n(&s_present_pending, 1, __ATOMIC_ACQ_REL))
        gx_host_command(video_present_gx, s_frames);
}

/* Un cuadro del juego (cambio de XFB). Los guiones de botones, los volcados y
 * --frames cuentan estos cuadros: no dependen de la velocidad de la PC. */
int video_fast_dump;   /* --dump-rapido */
static void set_skip_gx(uint32_t skip) { extern int gx_skip_draws; gx_skip_draws = (int)skip; }

volatile int video_capture_request;   /* F12 */
static volatile uint64_t s_fin_t[8], s_fin_cpu[8]; static volatile int s_fin_n;
volatile uint64_t gx_cpu_finish_t;
void video_note_finish(void) {             /* lo llama el PE (hilo grafico) */
    { int k = s_fin_n; if (k < 8) { s_fin_t[k] = os_now_us(); s_fin_cpu[k] = gx_cpu_finish_t; s_fin_n = k + 1; } }
    if (__atomic_exchange_n(&video_finish_pend, 0, __ATOMIC_ACQ_REL)) {
        uint64_t d = os_now_us() - video_frame_t0;
        video_finish_sum += d; video_finish_n++;
        if (d > video_finish_max) video_finish_max = d;
    }
}
void video_game_frame(void) {
    extern uint32_t gx_capture_frame;
    {
        static unsigned last_f;
        unsigned d = s_frames - last_f;
        last_f = s_frames;
        video_ritmo_hist[d >= 3 ? 3 : d]++;
        {   /* RL_RITMO_TRAZA=1: detalle de los cuadros que duraron 2 o mas retrazados */
            static int tr = -1; static uint64_t prev_t0, win_t; static int win_n;
            extern uint64_t g_t_cola_us, g_t_sync_us, g_t_tex_us, g_prog_nuevos_total, gx_busy_us, gl_stat_swap_us, gl_stat_tex_uploads, gl_stat_draw_calls;
            static uint64_t a_cola, a_sync, a_graf, a_swap, a_ntex, a_tex, a_prog, a_dib;
            uint64_t tnow = os_now_us();
            if (tr < 0) tr = getenv("RL_RITMO_TRAZA") != NULL ? 1000000 : 6;   /* por defecto, hasta 6 cada 5 s */
            if (tnow - win_t > 5000000) { win_t = tnow; win_n = 0; }
            if (d >= 2 && d < 6 && prev_t0 && win_n++ < tr) {
                char b[256]; int n = 0, k;
                uint64_t now = os_now_us();
                n += snprintf(b + n, sizeof b - n, "[cuadro] %u: duro %u retrazados (%.1f ms); fines de dibujo a", s_game_frames, d, (now - prev_t0) / 1000.0);
                for (k = 0; k < s_fin_n && k < 8; k++) n += snprintf(b + n, sizeof b - n, " %.1f (pedido a %.1f)", (double)(s_fin_t[k] - prev_t0) / 1000.0, (double)((int64_t)(s_fin_cpu[k] - prev_t0)) / 1000.0);
                rt_log("%s ms | juego: esperando lugar en la cola %.1f, esperando al grafico %.1f | grafico: trabajando %.1f, mostrando %.1f, texturas %llu (%.1f ms), programas nuevos %llu, dibujos %llu\n", b,
                       (g_t_cola_us - a_cola) / 1000.0, (g_t_sync_us - a_sync) / 1000.0, (gx_busy_us - a_graf) / 1000.0, (gl_stat_swap_us - a_swap) / 1000.0,
                       (unsigned long long)(gl_stat_tex_uploads - a_ntex), (g_t_tex_us - a_tex) / 1000.0,
                       (unsigned long long)(g_prog_nuevos_total - a_prog), (unsigned long long)(gl_stat_draw_calls - a_dib));
            }
            a_cola = g_t_cola_us; a_sync = g_t_sync_us; a_graf = gx_busy_us; a_swap = gl_stat_swap_us; a_ntex = gl_stat_tex_uploads;
            a_tex = g_t_tex_us; a_prog = g_prog_nuevos_total; a_dib = gl_stat_draw_calls;
            prev_t0 = os_now_us();
            s_fin_n = 0;
        }
        video_frame_t0 = os_now_us();
        __atomic_store_n(&video_finish_pend, 1, __ATOMIC_RELEASE);
        if (s_retrace_t) {   /* en que momento del retrazado el juego termina el cuadro */
            uint64_t ph = video_frame_t0 - s_retrace_t;
            s_phase_sum += ph; s_phase_n++;
            if (ph > 13000) s_phase_late++;
        }
    }
    void gx_capture_begin(uint32_t); void gx_capture_end(uint32_t);
    s_game_frames++;
    {   /* RL_TIRON_MS=N RL_TIRON_CADA=K (pruebas): cada K cuadros el juego se frena N ms (como un tiron de shaders o F12) */
        static int ms = -1, cada;
        if (ms < 0) { const char* e = getenv("RL_TIRON_MS"); ms = e ? atoi(e) : 0; e = getenv("RL_TIRON_CADA"); cada = e ? atoi(e) : 120; }
        if (ms && cada > 0 && s_game_frames % (unsigned)cada == 0) { struct timespec w = { ms / 1000, (ms % 1000) * 1000000L }; nanosleep(&w, NULL); }
    }
    if (video_capture_request) {   /* F12: capturar el cuadro que viene */
        video_capture_request = 0;
        if (!gx_capture_frame || gx_capture_frame < s_game_frames) gx_capture_frame = s_game_frames + 3;
    }
    if (gx_capture_frame && s_game_frames == gx_capture_frame - 2) gx_host_command(gx_capture_begin, s_game_frames);
    if (gx_capture_frame && s_game_frames == gx_capture_frame) {
        /* RL_CAPTURAR_CADA=K RL_CAPTURAR_N=M: M capturas, una cada K cuadros */
        static int every = -1, left;
        gx_host_command(gx_capture_end, vi_xfb_addr());
        if (every < 0) { const char* e = getenv("RL_CAPTURAR_CADA"); every = e ? atoi(e) : 0; e = getenv("RL_CAPTURAR_N"); left = e ? atoi(e) - 1 : 0; }
        if (every >= 3 && left > 0) { left--; gx_capture_frame += (uint32_t)every; }
    }
    {
        static long until = -2;
        if (until == -2) { const char* e = getenv("RL_FAST_UNTIL"); until = e ? atol(e) : -1; }
        if (until >= 0 && (long)s_game_frames == until) { video_fast_dump = 0; gx_host_command(set_skip_gx, 0u); }
    }
    if (video_fast_dump && video_dump_every) {
        /* se dibujan solo el cuadro que se va a guardar y el anterior (por los efectos
         * que usan la imagen del cuadro previo) */
        unsigned next = s_game_frames + 1;
        int draw = (next % video_dump_every == 0) || ((next + 1) % video_dump_every == 0);
        gx_host_command(set_skip_gx, draw ? 0u : 1u);
    }
    { void prof_frame(unsigned frame); prof_frame(s_game_frames); }
    {   /* 16:9: las camaras del juego (en el objeto apuntado por 0x80095DC8; hay 4 de ~0x384
           bytes) guardan la relacion de aspecto (1.3333, en +0x1D0) y valores sacados de
           ella: tangente del medio angulo horizontal (+0x188, vertical en +0x19C) y escala x de la
           proyeccion (+0x148, la y en +0x15C). Se pasan a 16:9 para que el juego tambien dibuje
           lo que queda a los costados (antes solo se ensanchaba la imagen y el escenario
           aparecia tarde en los bordes). Las que el juego vuelve a calcular quedan bien solas. */
        extern int gl_widescreen;
        static int viejo = -1;
        uint32_t g = R32(0x80095DC8u), o;
        float de = gl_widescreen ? 4.0f / 3.0f : 16.0f / 9.0f, a_ = gl_widescreen ? 16.0f / 9.0f : 4.0f / 3.0f;
        if (viejo < 0) viejo = getenv("RL_WS_VIEJO") != NULL;   /* pruebas: como antes */
        static int puesto;   /* solo se vuelve a 4:3 lo que se cambio aca */
        if (gl_widescreen) puesto = 1;
        if (!viejo && (gl_widescreen || puesto) && g >= 0x80000000u && g < 0x817F0000u)
            for (int k = 0; k < 4; k++) {   /* las 4 camaras: relacion de aspecto en +0x1D0 */
                static const uint32_t base[4] = { 0x0u, 0x384u, 0x704u, 0xA84u };
                float f;
                o = base[k] + 0x1D0u;
                f = u32_f(R32(g + o));
                {   /* +0x274: la relacion de aspecto de la que sale todo: fn_7FC14CEC la pasa a +0x1D0
                       y con ella arma la piramide de vision con que el juego recorta el escenario */
                    uint32_t a2 = g + base[k] + 0x274u;
                    float f2 = u32_f(R32(a2));
                    if (f2 > de - 0.002f && f2 < de + 0.002f) W32(a2, f_u32(a_));
                }
                if (f < de - 0.002f || f > de + 0.002f) continue;
                W32(g + o, f_u32(a_));
                if (o >= 0x88u) {   /* la camara de esta relacion: los valores derivados */
                    float tv = u32_f(R32(g + o - 0x34u)), th = u32_f(R32(g + o - 0x48u));
                    float sy = u32_f(R32(g + o - 0x74u)), sx = u32_f(R32(g + o - 0x88u));
                    if (tv > 0.01f && th > tv * de * 0.99f && th < tv * de * 1.01f) W32(g + o - 0x48u, f_u32(tv * a_));
                    if (sy > 0.01f && sx > sy / de * 0.99f && sx < sy / de * 1.01f) W32(g + o - 0x88u, f_u32(sy / a_));
                }
            }
    }
#if defined(__linux__)
    {   /* depuracion: RL_VIGILAR=DIR,LARGO[,r] anota que codigo escribe (o lee, con r) esa memoria:
           se protege la pagina en cada cuadro y el manejador de SIGSEGV anota la instruccion */
        void vigilar_cuadro(void);
        vigilar_cuadro();
    }
#endif
    {   /* trucos prendidos en las opciones: se mantienen sus bits en la partida (trucos.h) */
        extern unsigned g_trucos_a, g_trucos_b;
        if (g_trucos_a && (R32(0x7FE1CAF4u) & g_trucos_a) != g_trucos_a) W32(0x7FE1CAF4u, R32(0x7FE1CAF4u) | g_trucos_a);
        if (g_trucos_b && (R32(0x7FE1CAF8u) & g_trucos_b) != g_trucos_b) W32(0x7FE1CAF8u, R32(0x7FE1CAF8u) | g_trucos_b);
    }
    input_frame();
    {   /* RL_VOLCAR_RAM=prefijo:N1,N2,...  guarda toda la memoria (VM + RAM) en esos cuadros
           (prefijo_N.bin), para buscar donde guarda el juego algo comparando volcados */
        static const char* e; static int leido;
        if (!leido) { leido = 1; e = getenv("RL_VOLCAR_RAM"); }
        if (e) {
            const char* dp = strchr(e, ':');
            const char* q = dp ? dp + 1 : NULL;
            while (q && *q) {
                if ((unsigned)strtoul(q, (char**)&q, 10) == s_game_frames) {
                    char path[512]; FILE* f;
                    gx_sync();
                    snprintf(path, sizeof path, "%.*s_%u.bin", (int)(dp - e), e, s_game_frames);
                    f = fopen(path, "wb");
                    if (f) { fwrite(g_mem, 1, MEM_SIZE, f); fclose(f); rt_log("[ram] volcada en %s\n", path); }
                    break;
                }
                if (*q == ',') q++; else break;
            }
        }
    }
    if (video_dump_dir && video_dump_every && s_game_frames % video_dump_every == 0) {
        /* la XFB nueva ya esta completa: es la que el juego pide mostrar */
        s_dump_xfb[s_game_frames & 63] = vi_xfb_addr();
        gx_host_command(video_dump_gx, s_game_frames);
    }
    if (video_max_frames && s_game_frames >= video_max_frames) {
        gx_sync();
        rt_log("[video] %u cuadros del juego (%u retrazados): fin de la prueba\n", s_game_frames, s_frames);
        exit(0);
    }
}

#if defined(__linux__)
#include <signal.h>
#include <sys/mman.h>
#include <ucontext.h>
static uintptr_t s_vg_pag, s_vg_ini, s_vg_fin;
static int s_vg_lect;
static uint64_t s_vg_rip[64]; static int s_vg_n[64], s_vg_k;
static void vg_manejador(int sig, siginfo_t* si, void* uc_) {
    uintptr_t a = (uintptr_t)si->si_addr;
    ucontext_t* uc = (ucontext_t*)uc_;
    (void)sig;
    if (a < s_vg_pag || a >= s_vg_pag + 4096) { signal(SIGSEGV, SIG_DFL); return; }
    if (a >= s_vg_ini && a < s_vg_fin) {
        uint64_t rip = (uint64_t)uc->uc_mcontext.gregs[REG_RIP];
        int i;
        for (i = 0; i < s_vg_k && s_vg_rip[i] != rip; i++) {}
        if (i == s_vg_k && s_vg_k < 64) { s_vg_rip[s_vg_k] = rip; s_vg_n[s_vg_k++] = 0; }
        if (i < 64) s_vg_n[i]++;
    }
    mprotect((void*)s_vg_pag, 4096, PROT_READ | PROT_WRITE);
    uc->uc_mcontext.gregs[REG_EFL] |= 0x100;   /* un paso y se vuelve a proteger (SIGTRAP) */
}
static void vg_paso(int sig, siginfo_t* si, void* uc_) {
    ucontext_t* uc = (ucontext_t*)uc_;
    (void)sig; (void)si;
    uc->uc_mcontext.gregs[REG_EFL] &= ~0x100;
    mprotect((void*)s_vg_pag, 4096, s_vg_lect ? PROT_NONE : PROT_READ);
}
void vigilar_cuadro(void) {
    static int ini = -1; static uint32_t dir, largo;
    if (ini < 0) {
        const char* e = getenv("RL_VIGILAR");
        ini = 0;
        if (e) {
            struct sigaction sa;
            dir = (uint32_t)strtoul(e, NULL, 16);
            largo = strchr(e, ',') ? (uint32_t)strtoul(strchr(e, ',') + 1, NULL, 16) : 4;
            s_vg_lect = strstr(e, ",r") != NULL;
            memset(&sa, 0, sizeof sa); sa.sa_sigaction = vg_manejador; sa.sa_flags = SA_SIGINFO;
            sigaction(SIGSEGV, &sa, NULL);
            sa.sa_sigaction = vg_paso; sigaction(SIGTRAP, &sa, NULL);
            ini = 1;
        }
    }
    if (ini != 1) return;
    {   static long desde = -2; extern unsigned video_cuadro_actual(void);
        if (desde == -2) { const char* d = getenv("RL_VIGILAR_DESDE"); desde = d ? atol(d) : 0; }
        if ((long)video_cuadro_actual() < desde) return; }
    {   /* la direccion puede ser relativa al objeto de las camaras: prefijo 'g' no; se usa tal cual */
        uintptr_t h = (uintptr_t)(g_mem + (dir - MEM_BASE));
        static unsigned c;
        s_vg_pag = h & ~(uintptr_t)4095; s_vg_ini = h; s_vg_fin = h + largo;
        { static int dicho; int r = mprotect((void*)s_vg_pag, 4096, s_vg_lect ? PROT_NONE : PROT_READ);
          if (!dicho) { dicho = 1; rt_log("[vigilar] %08X..+%X, mprotect %d\n", dir, largo, r); } }
        if (++c % 25 == 0) {
            int i;
            extern char __executable_start;
            for (i = 0; i < s_vg_k; i++) rt_log("[vigilar] +%llx %d veces\n", (unsigned long long)(s_vg_rip[i] - (uintptr_t)&__executable_start), s_vg_n[i]);
        }
    }
}
#endif
unsigned video_cuadro_actual(void) { return s_game_frames; }
