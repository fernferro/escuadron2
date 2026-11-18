/*
 * Escuadron2 para PC: arranque.
 *
 *   escuadron2 [--disc carpeta] [--dump carpeta] [--frames N] [--pad "120:START,..."]
 *
 * Carga orig/GSWE64/combined.dol, prepara la memoria baja como lo haria el
 * cargador de la consola y ejecuta el arranque original: __init_registers,
 * OSInit, __init_user (constructores) y main, que salta a gameMain.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "runtime.h"
#include "os.h"
#include "input.h"
#include "gx.h"
#include "memcard.h"
#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
static void win_box(const char* msg) { MessageBoxA(NULL, msg, "Escuadron2: error", MB_OK | MB_ICONERROR); }
/* Si el programa se cae, anotar donde en escuadron2_log.txt (direccion relativa al .exe, para
   buscarla despues en el binario) */
static LONG WINAPI win_crash(EXCEPTION_POINTERS* ep) {
    extern FILE* rt_log_file;
    HMODULE base = GetModuleHandleA(NULL);
    uintptr_t at = (uintptr_t)ep->ExceptionRecord->ExceptionAddress;
    if (rt_log_file) {
        fprintf(rt_log_file, "\n[caida] excepcion %08lX en %p (escuadron2.exe+0x%llX)\n", (unsigned long)ep->ExceptionRecord->ExceptionCode,
                (void*)at, (unsigned long long)(at - (uintptr_t)base));
        if (ep->ExceptionRecord->NumberParameters >= 2)
            fprintf(rt_log_file, "[caida] acceso %s a %p\n", ep->ExceptionRecord->ExceptionInformation[0] ? "de escritura" : "de lectura",
                    (void*)ep->ExceptionRecord->ExceptionInformation[1]);
        fflush(rt_log_file);
    }
    MessageBoxA(NULL, "El juego se cayo. Mandar el archivo escuadron2_log.txt.", "Escuadron2: error", MB_OK | MB_ICONERROR);
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

int dol_load(const char* path, uint32_t* entry);
void gx_init(void);
extern const char* video_dump_dir;
extern unsigned video_dump_every, video_max_frames;
extern int hw_trace, gx_trace, dsp_trace;
const uint8_t* disc_fst(uint32_t* size);

#define F___init_registers 0x800031F0u
#define F_OSInit           0x80027C38u
#define F___init_user      0x80006B18u
#define F_main             0x80006A40u

void disc_read(uint32_t offset, uint32_t len, uint8_t* dst);
static void setup_low_memory(void) {
    uint8_t boot[0x20];
    uint32_t fst_size;
    const uint8_t* fst = disc_fst(&fst_size);
    disc_read(0, sizeof boot, boot);    /* boot.bin = el principio del disco */
    memcpy(mem_ptr(0x80000000u), boot, sizeof boot);
    W32(0x80000020u, 0x0D15EA5Eu);   /* arranque normal */
    W32(0x80000024u, 1);
    W32(0x80000028u, 0x01800000u);   /* 24 MB */
    W32(0x8000002Cu, 0x00000003u);   /* consola de venta */
    W32(0x80000030u, 0);             /* arena bajo: lo decide OSInit */
    W32(0x80000034u, OS_ARENA_HI);
    W32(0x80000038u, OS_FST_ADDR);
    W32(0x8000003Cu, fst_size);
    W32(0x800000CCu, 0);             /* NTSC */
    W32(0x800000F0u, 0x01800000u);
    W32(0x800000F8u, 162000000u);    /* reloj del bus */
    W32(0x800000FCu, 486000000u);    /* reloj de la CPU */
    memcpy(mem_ptr(OS_FST_ADDR), fst, fst_size);
    /* bi2.bin (0x440 del disco): de aca salen el soporte de nombres largos y la version de los controles */
    disc_read(0x440, 0x2000, mem_ptr(OS_BI2_ADDR));
    W32(0x800000F4u, OS_BI2_ADDR);
}

static int ends_with(const char* s, const char* suf) {
    size_t a = strlen(s), b = strlen(suf), i;
    if (a < b) return 0;
    for (i = 0; i < b; i++) { char c = s[a - b + i]; if (c >= 'A' && c <= 'Z') c += 32; if (c != suf[i]) return 0; }
    return 1;
}

static int file_exists(const char* p) {
    FILE* f = fopen(p, "rb");
    if (f) fclose(f);
    return f != NULL;
}

static void call(PPCContext* ctx, uint32_t addr, const char* name) {
    PPCFunc fn = ppc_lookup(addr);
    if (!fn) rt_fatal("no existe %s\n", name);
    rt_log("[arranque] %s\n", name);
    fn(ctx);
}

extern const char* rt_exe_path;
/* ---- ajustes.ini --------------------------------------------------------------------
 * Las opciones que escriben el launcher y el menu del juego (F1), clave = valor
 * (port/launcher/ajustes.h). Se busca en la carpeta actual (el launcher arranca el juego en su
 * carpeta de datos) y si no, al lado del programa; el menu guarda en el mismo lugar.
 * Se leen antes que escuadron2.ini y la linea de comandos, que mandan sobre esto. */
extern int g_contador_fps, g_aviso_shaders;   /* os.c */
extern char g_ajustes_ruta[1024];

#include "trucos.h"
static void truco_poner(const char* clave, int v) {
    extern unsigned g_trucos_a, g_trucos_b;
    int i;
    for (i = 0; i < NTRUCOS; i++)
        if (!strcmp(k_trucos[i].clave, clave)) {
            unsigned* w = k_trucos[i].palabra == TRUCOS_A ? &g_trucos_a : &g_trucos_b;
            if (v) *w |= k_trucos[i].mascara; else *w &= ~k_trucos[i].mascara;
            return;
        }
}

static void ajustes_leer(const char* exe) {
    extern int gl_fullscreen, gl_widescreen, gl_scale, gl_vsync, gl_filtro_salida, gl_msaa, gl_aniso_nivel,
        gl_preparar_shaders, hd_desactivado, audio_volumen, g_camara_bomba, g_rumble_enabled;
    char linea[256];
    FILE* f = fopen("ajustes.ini", "rb");
    if (!f && exe) {   /* al lado del programa (tambien para guardar si no existe en ningun lado) */
        const char* sl = strrchr(exe, '/');
        const char* bs = strrchr(exe, '\\');
        if (bs && (!sl || bs > sl)) sl = bs;
        if (sl && (size_t)(sl - exe) < sizeof g_ajustes_ruta - 16) {
            memcpy(g_ajustes_ruta, exe, (size_t)(sl - exe + 1));
            strcpy(g_ajustes_ruta + (sl - exe + 1), "ajustes.ini");
            f = fopen(g_ajustes_ruta, "rb");
        }
    }
    if (!f) return;
    while (fgets(linea, sizeof linea, f)) {
        char k[64]; int v;
        if (linea[0] == '#' || sscanf(linea, " %63[a-z_0-9] = %d", k, &v) != 2) continue;
        if (!strcmp(k, "pantalla_completa")) gl_fullscreen = v;
        else if (!strcmp(k, "panoramico")) gl_widescreen = v;
        else if (!strcmp(k, "escala")) gl_scale = v;
        else if (!strcmp(k, "vsync")) gl_vsync = v;
        else if (!strcmp(k, "filtro")) gl_filtro_salida = v;
        else if (!strcmp(k, "contador_fps")) g_contador_fps = v;
        else if (!strcmp(k, "msaa")) gl_msaa = v;
        else if (!strcmp(k, "anisotropico")) gl_aniso_nivel = v;
        else if (!strcmp(k, "texturas_hd")) hd_desactivado = !v;
        else if (!strcmp(k, "preparar_todo")) gl_preparar_shaders = v;
        else if (!strcmp(k, "aviso_shaders")) g_aviso_shaders = v;
        else if (!strcmp(k, "volumen")) audio_volumen = v;
        else if (!strcmp(k, "camara_bomba")) g_camara_bomba = v;
        else if (!strcmp(k, "vibracion")) g_rumble_enabled = v;
        else if (!strncmp(k, "truco_", 6)) truco_poner(k + 6, v);
    }
    fclose(f);
    rt_log("[config] %s cargado\n", g_ajustes_ruta);
}

int main(int argc, char** argv) {
    rt_exe_path = argv[0];
    const char* dol = "orig/GSWE64/combined.dol";
    const char* sys_dir = "orig/GSWE64/sys";
    const char* disc_dir = "build/disc";
    const char* game_dir = NULL;
    const char* iso = NULL;
    static char dol_buf[1024], sys_buf[1024];
    PPCContext* ctx;
    uint32_t entry;
    int i;
    const char* replay_in = NULL; const char* replay_out = NULL;
    const char* ensayo = NULL;
    int use_dsp_hle = 0;
    const char* memcard = NULL;
    int use_memcard = 1;
    static char mc_buf[1024];
#ifdef _WIN32
    { extern FILE* rt_log_file; extern void (*rt_fatal_hook)(const char*); rt_log_file = fopen("escuadron2_log.txt", "w"); rt_fatal_hook = win_box; SetUnhandledExceptionFilter(win_crash); }
    {   char exe[1024]; GetModuleFileNameA(NULL, exe, sizeof exe);
        rt_log("[version] escuadron2 compilado el %s %s (%s)\n", __DATE__, __TIME__, exe); }
    {   /* nombres de archivos de versiones anteriores (rogue2_*): se renombran una sola vez */
        const char* viejos[][2] = { { "rogue2_shaders.cache", "escuadron2_shaders.cache" }, { "rogue2_shaders.usados", "escuadron2_shaders.usados" },
                                    { "rogue2.ini", "escuadron2.ini" } };
        int i;
        for (i = 0; i < 3; i++) {
            FILE* n = fopen(viejos[i][1], "rb");
            if (n) { fclose(n); continue; }
            if (rename(viejos[i][0], viejos[i][1]) == 0) rt_log("[config] %s renombrado a %s\n", viejos[i][0], viejos[i][1]);
        }
    }
    {   /* una sola copia abierta: una colgada en segundo plano le saca la placa y el procesador a la nueva */
        HANDLE m = CreateMutexA(NULL, TRUE, "Escuadron2_una_sola_copia");
        if (m && GetLastError() == ERROR_ALREADY_EXISTS) {
            MessageBoxA(NULL, "Ya hay un Escuadron2 abierto (puede estar colgado y escondido).\n"
                        "Cerralo desde el Administrador de tareas (Ctrl+Shift+Esc) y volve a abrir el juego.",
                        "Escuadron2", MB_OK | MB_ICONWARNING);
            return 1;
        }
    }
    timeBeginPeriod(1);   /* sin esto las esperas cortas de Windows duran 15 ms y el juego va en camara lenta */
    { void os_win_preparar_esperas(void); os_win_preparar_esperas(); }
#endif
    ajustes_leer(argv[0]);
    {   /* escuadron2.ini (en la carpeta actual o al lado del programa): las mismas opciones de la
           linea de comandos, una o varias por renglon; '#' empieza un comentario. Van antes que
           las de la linea de comandos. */
        static char* nargv[512];
        static char text[16384];
        int nargc = 0, k;
        FILE* f = fopen("escuadron2.ini", "rb");
        if (!f && argv[0]) {
            char path[1024]; const char* sl = strrchr(argv[0], '/');
            const char* bs = strrchr(argv[0], '\\');
            if (bs && (!sl || bs > sl)) sl = bs;
            if (sl && (size_t)(sl - argv[0]) < sizeof path - 16) {
                memcpy(path, argv[0], (size_t)(sl - argv[0] + 1));
                strcpy(path + (sl - argv[0] + 1), "escuadron2.ini");
                f = fopen(path, "rb");
            }
        }
        nargv[nargc++] = argv[0];
        if (f) {
            size_t n = fread(text, 1, sizeof text - 1, f);
            char* q = text;
            fclose(f);
            text[n] = 0;
            while (*q && nargc < 500) {
                while (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n') q++;
                if (*q == '#') { while (*q && *q != '\n') q++; continue; }
                if (!*q) break;
                if (*q == '"') {   /* "texto con espacios" */
                    nargv[nargc++] = ++q;
                    while (*q && *q != '"') q++;
                } else {
                    nargv[nargc++] = q;
                    while (*q && *q != ' ' && *q != '\t' && *q != '\r' && *q != '\n') q++;
                }
                if (*q) *q++ = 0;
            }
            rt_log("[config] escuadron2.ini: %d opciones\n", nargc - 1);
        }
        for (k = 1; k < argc && nargc < 511; k++) nargv[nargc++] = argv[k];
        nargv[nargc] = NULL;
        argc = nargc; argv = nargv;
    }
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--disc") && i + 1 < argc) disc_dir = argv[++i];
        else if (!strcmp(argv[i], "--game") && i + 1 < argc) { game_dir = argv[++i]; if (ends_with(game_dir, ".iso") || ends_with(game_dir, ".gcm")) { iso = game_dir; game_dir = NULL; } }
        else if (!strcmp(argv[i], "--iso") && i + 1 < argc) iso = argv[++i];
        else if (!strcmp(argv[i], "--memcard") && i + 1 < argc) memcard = argv[++i];
        else if (!strcmp(argv[i], "--sin-tarjeta")) use_memcard = 0;
        else if (!strcmp(argv[i], "--trace-memcard")) { extern int mc_trace; mc_trace = 1; }
        else if (!strcmp(argv[i], "--dump") && i + 1 < argc) video_dump_dir = argv[++i];
        else if (!strcmp(argv[i], "--every") && i + 1 < argc) video_dump_every = (unsigned)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--capturar") && i + 1 < argc) { extern uint32_t gx_capture_frame; gx_capture_frame = (uint32_t)atoi(argv[++i]); }
        else if (!strcmp(argv[i], "--reproducir") && i + 2 < argc) { replay_in = argv[++i]; replay_out = argv[++i]; }
        else if (!strcmp(argv[i], "--ensayo-terreno") && i + 1 < argc) { ensayo = argv[++i]; }
        else if (!strcmp(argv[i], "--dump-rapido")) { extern int video_fast_dump; video_fast_dump = 1; }
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) video_max_frames = (unsigned)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--pad") && i + 1 < argc) input_set_script(argv[++i]);
        else if (!strcmp(argv[i], "--trace-hw")) hw_trace = 1;
        else if (!strcmp(argv[i], "--trace-gx")) gx_trace = 1;
        else if (!strcmp(argv[i], "--trace-dsp")) dsp_trace = 1;
        else if (!strcmp(argv[i], "--dsp-hle")) use_dsp_hle = 1;
        else if (!strcmp(argv[i], "--wav") && i + 1 < argc) { extern const char* audio_wav_path; audio_wav_path = argv[++i]; }
        else if (!strcmp(argv[i], "--sin-audio")) { extern int audio_disabled; audio_disabled = 1; }
        else if (!strcmp(argv[i], "--trace-threads")) os_trace_threads = 1;
        else if (!strcmp(argv[i], "--gx-sync")) gx_sync_mode = 1;
        else if (!strcmp(argv[i], "--gl")) { extern int render_gl; render_gl = 1; }
        else if (!strcmp(argv[i], "--sw")) { extern int render_gl; render_gl = 0; }
        else if (!strcmp(argv[i], "--headless")) { extern int gl_headless; gl_headless = 1; }
        else if (!strcmp(argv[i], "--vsync")) { extern int gl_vsync; gl_vsync = 1; }
        else if (!strcmp(argv[i], "--dsp-interprete")) { extern int dsp_use_interpreter, dsp_usar_dolphin; dsp_use_interpreter = 1; dsp_usar_dolphin = 1; }
        else if (!strcmp(argv[i], "--dsp-dolphin")) { extern int dsp_usar_dolphin; dsp_usar_dolphin = 1; }
        else if (!strcmp(argv[i], "--dsp-verificar")) { extern int dsp_verificar; dsp_verificar = 1; }
        else if (!strcmp(argv[i], "--sin-texturas-hd")) { extern int hd_desactivado; hd_desactivado = 1; }
        else if (!strcmp(argv[i], "--sin-vibracion")) { extern int g_rumble_enabled; g_rumble_enabled = 0; }
        else if (!strcmp(argv[i], "--camara-bomba")) { extern int g_camara_bomba; g_camara_bomba = 1; }
        else if (!strcmp(argv[i], "--subir-en-hilos")) { extern int gl_subir_en_hilos; gl_subir_en_hilos = 1; }
        else if (!strcmp(argv[i], "--pantalla-completa")) { extern int gl_fullscreen; gl_fullscreen = 1; }
        else if (!strcmp(argv[i], "--panoramico")) { extern int gl_widescreen; gl_widescreen = 1; }
        else if (!strcmp(argv[i], "--escala") && i + 1 < argc) { extern int gl_scale; gl_scale = atoi(argv[++i]); }
        else if (!strcmp(argv[i], "--msaa") && i + 1 < argc) { extern int gl_msaa; gl_msaa = atoi(argv[++i]); }
        else if (!strcmp(argv[i], "--anisotropico") && i + 1 < argc) { extern int gl_aniso_nivel; gl_aniso_nivel = atoi(argv[++i]); }
        else if (!strcmp(argv[i], "--filtro-nitido")) { extern int gl_filtro_salida; gl_filtro_salida = 1; }
        else if (!strcmp(argv[i], "--contador-fps")) g_contador_fps = 1;
        else if (!strcmp(argv[i], "--volumen") && i + 1 < argc) { extern int audio_volumen; audio_volumen = atoi(argv[++i]); }
        else if (!strcmp(argv[i], "--truco") && i + 1 < argc) truco_poner(argv[++i], 1);
        else if (!strcmp(argv[i], "--preparar-shaders")) { extern int gl_preparar_shaders; gl_preparar_shaders = 1; }
        else if (!strcmp(argv[i], "--dump-copies") && i + 1 < argc) { extern int render_dump_copies; render_dump_copies = atoi(argv[++i]); }
        else if (!strcmp(argv[i], "--trace-copies")) { extern int render_trace_copies; render_trace_copies = 1; }
        else if (!strcmp(argv[i], "--dump-efb")) { extern int video_dump_efb; video_dump_efb = 1; }
        else if (!strcmp(argv[i], "--trace-interp")) { extern int interp_trace; interp_trace = 1; }
        else if (!strcmp(argv[i], "--dump-fifo") && i + 1 < argc) { extern FILE* gx_fifo_dump; gx_fifo_dump = fopen(argv[++i], "wb"); }
        else { fprintf(stderr, "opcion desconocida: %s\n", argv[i]); return 2; }
    }
    /* Carpeta del juego: DIR/sys/main.dol (+ boot.bin, bi2.bin, fst.bin) y DIR/Rogue2/...
     * Sin --game: si al lado hay sys/ y Rogue2/ se usa la carpeta actual. */
    if (!game_dir && !file_exists(dol)) {
        if (file_exists("sys/main.dol")) game_dir = ".";
    }
    if (game_dir) {
        snprintf(sys_buf, sizeof sys_buf, "%s/sys", game_dir);
        snprintf(dol_buf, sizeof dol_buf, "%s/sys/main.dol", game_dir);
        sys_dir = sys_buf; dol = dol_buf; disc_dir = game_dir;
    }
    { void prof_start(void); prof_start(); }
    mem_init();
    if (ensayo) {   /* --ensayo-terreno CAPTURA: corre el dibujo del terreno cercano sobre la RAM de una captura */
        int ensayo_terreno(const char* path);
        hw_init();
        return ensayo_terreno(ensayo);
    }
    if (replay_in) {   /* --reproducir CAPTURA SALIDA.png: dibuja un cuadro capturado y sale */
        int gx_replay(const char* path, const char* out_png);
        hw_init();
        return gx_replay(replay_in, replay_out);
    }
    if (iso) {   /* imagen del disco: el codigo del juego (main.dol) sale de ahi */
        int disc_open_iso(const char* path);
        uint32_t disc_dol_offset(uint32_t* size);
        int dol_load_mem(const uint8_t* data, size_t size, uint32_t* entry);
        uint32_t dsize, doff;
        uint8_t* d;
        if (!disc_open_iso(iso)) rt_fatal("no pude abrir la imagen del disco %s\n(tiene que ser un .iso o .gcm del juego (GSWE64))\n", iso);
        doff = disc_dol_offset(&dsize);
        d = (uint8_t*)malloc(dsize ? dsize : 1);
        disc_read(doff, dsize, d);
        if (!dsize || !dol_load_mem(d, dsize, &entry)) rt_fatal("no pude cargar el codigo del juego desde %s\n", iso);
        free(d);
    } else {
        if (!dol_load(dol, &entry)) rt_fatal("no pude cargar %s\nUsar: escuadron2 --iso JUEGO.iso  o  escuadron2 --game CARPETA (con CARPETA/sys/main.dol y CARPETA/Rogue2/...)\n", dol);
        if (!disc_open(sys_dir, disc_dir)) rt_fatal("no pude abrir el disco en %s\n", sys_dir);
    }
    setup_low_memory();
    if (use_memcard) {
        /* la tarjeta de memoria (ranura A) vive junto a los datos del juego */
        if (!memcard) {
            snprintf(mc_buf, sizeof mc_buf, "%s/memcard_A.raw", game_dir ? game_dir : iso ? "." : "build");
            memcard = mc_buf;
        }
        mc_init(memcard);
    }
    hw_init();
    if (!use_dsp_hle) { void dsp_lle_init(void); dsp_lle_init(); }
    gx_init();
    os_init_host(NULL);
    ctx = os_main_ctx();
    memset(ctx, 0, sizeof *ctx);
    ctx->msr = 0x00002032;  /* FP habilitado, interrupciones deshabilitadas como al arrancar */
    call(ctx, F___init_registers, "__init_registers");
    call(ctx, F_OSInit, "OSInit");
    call(ctx, F___init_user, "__init_user");
    ctx->r[3] = 0;
    ctx->r[4] = 0;
    call(ctx, F_main, "main");
    rt_log("main volvio\n");
    return 0;
}
