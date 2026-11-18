/*
 * Texturas en alta definicion con el formato de los paquetes de Dolphin.
 *
 * Un paquete es una carpeta con archivos tex1_ANCHOxALTO[_m]_HASH[_TLUT]_FORMATO.png (o .dds):
 *   HASH   XXH64 (semilla 0) de los bytes de la textura en la RAM (solo el nivel 0, en bloques)
 *   TLUT   XXH64 de la parte de la paleta que usa la textura (solo texturas con paleta)
 *   _m     la textura tiene mipmaps; FORMATO: numero del formato de GX (14 = CMPR)
 * Comodines: "_$" en lugar de la paleta, o "$" en lugar del hash. "_arb" se ignora.
 * Ver Dolphin, Source/Core/VideoCommon/TextureInfo.cpp y HiresTextures.cpp.
 *
 * Se buscan en la carpeta texturas_hd/ (o la de RL_TEXTURAS_HD) y todas sus subcarpetas: se
 * puede copiar ahi tal cual la carpeta GSWE64 de un paquete de Dolphin (Load/Textures/GSWE64).
 * Las imagenes se leen en otro hilo la primera vez que el juego usa esa textura; mientras tanto
 * se ve la original. RL_VOLCAR_TEXTURAS=1 guarda cada textura que usa el juego con ese nombre en
 * texturas_volcadas/ (sirve para hacer paquetes o para ver si los nombres coinciden).
 */
int hd_desactivado;   /* --sin-texturas-hd */
#ifdef RL_GL
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "texturas_hd.h"
#ifdef _WIN32
#include <direct.h>
#else
#include <unistd.h>
#endif
#include "runtime.h"

#define XXH_INLINE_ALL
#include "xxhash/xxhash.h"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_FAILURE_USERMSG
#include "stb/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

struct HdTex {
    char* nombre;              /* sin extension ni "_arb" */
    char* ruta;
    SDL_atomic_t estado;       /* HD_NADA, HD_CARGANDO, HD_LISTA, HD_FALLO */
    HdImagen img;
    unsigned gl;               /* textura de OpenGL (la pone render_gl.c) */
    struct HdTex* sig;
};

#define CUBETAS 8192
static HdTex* s_tabla[CUBETAS];
static int s_cant, s_activo;
static SDL_mutex* s_mx;
static SDL_cond* s_cv;
static HdTex* s_cola[1024];
static int s_cola_ini, s_cola_fin;

uint64_t hd_xxh64(const void* p, size_t n) { return XXH64(p, n, 0); }

static uint32_t hash_txt(const char* s) { uint32_t h = 2166136261u; while (*s) h = (h ^ (uint8_t)*s++) * 16777619u; return h; }

static void agregar(const char* ruta, const char* archivo) {
    char nombre[512];
    size_t n = strlen(archivo);
    const char* punto = strrchr(archivo, '.');
    char* arb;
    uint32_t c;
    HdTex* t;
    if (!punto || n >= sizeof nombre || strncmp(archivo, "tex1_", 5)) return;
#ifdef _WIN32
    if (_stricmp(punto, ".png") && _stricmp(punto, ".dds")) return;
#else
    if (strcasecmp(punto, ".png") && strcasecmp(punto, ".dds")) return;
#endif
    memcpy(nombre, archivo, (size_t)(punto - archivo));
    nombre[punto - archivo] = 0;
    if (strstr(nombre, "_mip")) return;   /* niveles de mipmap sueltos: se generan solos */
    if ((arb = strstr(nombre, "_arb")) != NULL) memmove(arb, arb + 4, strlen(arb + 4) + 1);
    c = hash_txt(nombre) % CUBETAS;
    for (t = s_tabla[c]; t; t = t->sig) if (!strcmp(t->nombre, nombre)) return;   /* el primero gana */
    t = (HdTex*)calloc(1, sizeof *t);
    t->nombre = strdup(nombre);
    t->ruta = strdup(ruta);
    t->sig = s_tabla[c];
    s_tabla[c] = t;
    s_cant++;
}

static void recorrer(const char* dir, int prof) {
    char p[1024];
    if (prof > 8) return;
#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    HANDLE h;
    snprintf(p, sizeof p, "%s\\*", dir);
    h = FindFirstFileA(p, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.cFileName[0] == '.') continue;
        snprintf(p, sizeof p, "%s\\%s", dir, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) recorrer(p, prof + 1);
        else agregar(p, fd.cFileName);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR* d = opendir(dir);
    struct dirent* e;
    if (!d) return;
    while ((e = readdir(d)) != NULL) {
        struct stat st;
        if (e->d_name[0] == '.') continue;
        snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
        if (stat(p, &st)) continue;
        if (S_ISDIR(st.st_mode)) recorrer(p, prof + 1);
        else agregar(p, e->d_name);
    }
    closedir(d);
#endif
}

/* ---- lectura de imagenes ------------------------------------------------------------------ */

static uint8_t* leer_archivo(const char* ruta, size_t* n) {
    FILE* f = fopen(ruta, "rb");
    uint8_t* d;
    long t;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    t = ftell(f);
    fseek(f, 0, SEEK_SET);
    d = (uint8_t*)malloc(t > 0 ? (size_t)t : 1);
    if (t <= 0 || fread(d, 1, (size_t)t, f) != (size_t)t) { free(d); fclose(f); return NULL; }
    fclose(f);
    *n = (size_t)t;
    return d;
}

static uint32_t le32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

/* DDS con BC1/BC2/BC3/BC7 (o RGBA sin comprimir); los niveles van uno detras del otro */
static int leer_dds(const uint8_t* d, size_t n, HdImagen* im) {
    uint32_t alto, ancho, mips, fourcc, flags_pf, bits, desde = 128, bloque = 0, i;
    if (n < 128 || memcmp(d, "DDS ", 4)) return 0;
    alto = le32(d + 12); ancho = le32(d + 16); mips = le32(d + 28);
    flags_pf = le32(d + 80); fourcc = le32(d + 84); bits = le32(d + 88);
    if (!mips) mips = 1;
    if (mips > HD_MAX_NIVELES) mips = HD_MAX_NIVELES;
    im->comprimida = 1;
    if (flags_pf & 4) {
        if (fourcc == 0x31545844u) { im->formato_gl = 0x83F1; bloque = 8; }        /* DXT1 -> COMPRESSED_RGBA_S3TC_DXT1 */
        else if (fourcc == 0x33545844u) { im->formato_gl = 0x83F2; bloque = 16; }  /* DXT3 */
        else if (fourcc == 0x35545844u) { im->formato_gl = 0x83F3; bloque = 16; }  /* DXT5 */
        else if (fourcc == 0x30315844u && n >= 148) {                               /* DX10 */
            uint32_t dxgi = le32(d + 128);
            desde = 148;
            if (dxgi == 98 || dxgi == 99) { im->formato_gl = dxgi == 99 ? 0x8E8D : 0x8E8C; bloque = 16; }  /* BC7 (sRGB / UNORM) */
            else if (dxgi == 71 || dxgi == 72) { im->formato_gl = 0x83F1; bloque = 8; }
            else if (dxgi == 74 || dxgi == 75) { im->formato_gl = 0x83F2; bloque = 16; }
            else if (dxgi == 77 || dxgi == 78) { im->formato_gl = 0x83F3; bloque = 16; }
            else if (dxgi == 28 || dxgi == 29) { im->comprimida = 0; }
            else return 0;
        } else return 0;
    } else if ((flags_pf & 0x40) && bits == 32) {
        im->comprimida = 0;   /* se asume orden RGBA (A8B8G8R8) */
    } else return 0;
    im->w = (int)ancho; im->h = (int)alto;
    for (i = 0; i < mips; i++) {
        uint32_t w = ancho >> i ? ancho >> i : 1, h = alto >> i ? alto >> i : 1;
        uint32_t t = im->comprimida ? ((w + 3) / 4) * ((h + 3) / 4) * bloque : w * h * 4;
        if (desde + t > n) break;
        im->nivel[i] = (uint8_t*)malloc(t);
        memcpy(im->nivel[i], d + desde, t);
        im->tam[i] = t;
        desde += t;
        im->niveles = (int)i + 1;
    }
    return im->niveles > 0;
}

static void cargar(HdTex* t) {
    size_t n = 0;
    uint8_t* d = leer_archivo(t->ruta, &n);
    int ok = 0;
    memset(&t->img, 0, sizeof t->img);
    if (d) {
        if (n >= 4 && !memcmp(d, "DDS ", 4)) ok = leer_dds(d, n, &t->img);
        else {
            int w, h, c;
            uint8_t* px = stbi_load_from_memory(d, (int)n, &w, &h, &c, 4);
            if (px) {
                t->img.w = w; t->img.h = h; t->img.niveles = 1;
                t->img.nivel[0] = px; t->img.tam[0] = (uint32_t)w * h * 4;
                ok = 1;
            }
        }
        free(d);
    }
    if (!ok) rt_log("[hd] no se pudo leer %s\n", t->ruta);
    SDL_AtomicSet(&t->estado, ok ? HD_LISTA : HD_FALLO);
}

static int hilo_carga(void* x) {
    (void)x;
    for (;;) {
        HdTex* t;
        SDL_LockMutex(s_mx);
        while (s_cola_ini == s_cola_fin) SDL_CondWait(s_cv, s_mx);
        t = s_cola[s_cola_ini++ % 1024];
        SDL_UnlockMutex(s_mx);
        cargar(t);
    }
    return 0;
}

/* ---- interfaz ------------------------------------------------------------------------------ */

void hd_iniciar(void) {
    const char* dir = getenv("RL_TEXTURAS_HD");
    const char* no = getenv("RL_SIN_TEXTURAS_HD");
    Uint32 t0 = SDL_GetTicks();
    if (no || hd_desactivado) { rt_log("[hd] texturas en alta definicion apagadas en las opciones\n"); return; }
    if (!dir) dir = "texturas_hd";
    recorrer(dir, 0);
    if (!s_cant) {   /* tambien la carpeta de Dolphin, si esta al lado */
        recorrer("Load/Textures/GSWE64", 0);
        recorrer("GSWE64", 0);   /* la carpeta del paquete copiada suelta en la carpeta de datos */
    }
    if (!s_cant) {   /* la que deja el instalador: al lado del programa */
        extern const char* rt_exe_path;
        static char d[1024];
        const char* sl = rt_exe_path ? strrchr(rt_exe_path, '/') : NULL;
        const char* bs = rt_exe_path ? strrchr(rt_exe_path, '\\') : NULL;
        if (bs && (!sl || bs > sl)) sl = bs;
        if (sl && (size_t)(sl - rt_exe_path) < sizeof d - 16) {
            memcpy(d, rt_exe_path, (size_t)(sl - rt_exe_path + 1));
            strcpy(d + (sl - rt_exe_path + 1), "texturas_hd");
            recorrer(d, 0);
            if (s_cant) dir = d;
        }
        recorrer("Load/Textures/GSW", 0);
    }
    if (!s_cant) {   /* que el registro diga donde se busco */
        char cwd[512] = "?";
#ifdef _WIN32
        if (!_getcwd(cwd, sizeof cwd)) strcpy(cwd, "?");
#else
        if (!getcwd(cwd, sizeof cwd)) strcpy(cwd, "?");
#endif
        rt_log("[hd] 0 texturas en alta definicion: no hay archivos tex1_*.png/.dds en %s\\%s ni al lado del programa\n", cwd, dir);
        return;
    }
    s_activo = 1;
    s_mx = SDL_CreateMutex();
    s_cv = SDL_CreateCond();
    SDL_DetachThread(SDL_CreateThread(hilo_carga, "texturas_hd", NULL));
    SDL_DetachThread(SDL_CreateThread(hilo_carga, "texturas_hd2", NULL));
    rt_log("[hd] %d texturas en alta definicion (%s), %u ms\n", s_cant, dir, SDL_GetTicks() - t0);
}

int hd_activo(void) { return s_activo; }

HdTex* hd_buscar(const char* nombre) {
    HdTex* t;
    if (!s_activo) return NULL;
    for (t = s_tabla[hash_txt(nombre) % CUBETAS]; t; t = t->sig) if (!strcmp(t->nombre, nombre)) return t;
    return NULL;
}

int hd_estado(HdTex* t) {
    if (SDL_AtomicGet(&t->estado) == HD_NADA) {
        SDL_LockMutex(s_mx);
        if (SDL_AtomicGet(&t->estado) == HD_NADA && s_cola_fin - s_cola_ini < 1024) {
            SDL_AtomicSet(&t->estado, HD_CARGANDO);
            s_cola[s_cola_fin++ % 1024] = t;
            SDL_CondSignal(s_cv);
        }
        SDL_UnlockMutex(s_mx);
    }
    return SDL_AtomicGet(&t->estado);
}

const HdImagen* hd_imagen(HdTex* t) { return SDL_AtomicGet(&t->estado) == HD_LISTA ? &t->img : NULL; }

void hd_soltar_imagen(HdTex* t) {
    int i;
    for (i = 0; i < t->img.niveles; i++) {
        free(t->img.nivel[i]);   /* stb_image tambien usa malloc */
        t->img.nivel[i] = NULL;
    }
    t->img.niveles = 0;
}

int hd_png_rgba(const char* ruta, const uint8_t* rgba, int w, int h) { return stbi_write_png(ruta, w, h, 4, rgba, w * 4); }

unsigned* hd_gl(HdTex* t) { return &t->gl; }
const char* hd_nombre(HdTex* t) { return t->nombre; }
#endif
