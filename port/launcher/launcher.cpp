/*
 * Escuadron2: launcher del port (estilo Foxhollow).
 *
 * Ventana con Dear ImGui sobre SDL2 + OpenGL 3. Pestanas: Jugar, Controles, Tarjetas de
 * Opciones, Controles, Tarjetas de memoria, Sistema y Registros. Guarda todo en la carpeta de
 * datos del usuario (%APPDATA%\Escuadron2 en Windows, ~/.local/share/Escuadron2 en Linux):
 *   launcher.ini    imagen del disco, tarjeta elegida y argumentos extra
 *   ajustes.ini     opciones de video, audio y juego: las lee el juego y las cambia tambien
 *                   el menu de adentro del juego (F1); misma pantalla (port/launcher/ajustes.h)
 *   controles.ini   lo lee el juego (port/runtime/controles.c)
 *   tarjetas/       tarjetas de memoria (.raw, formato crudo de Dolphin)
 *   escuadron2_log.txt, escuadron2_shaders.cache, capturas/   los escribe el juego
 * y arranca escuadron2(.exe) (al lado del launcher) con esa carpeta como carpeta actual.
 *
 * Nunca trae archivos del juego: hay que elegir una imagen del disco propia (.iso / .gcm).
 * Para pruebas: RL_LAUNCHER_CAPTURAS=CARPETA guarda una imagen de cada pestana y sale.
 */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <string>
#include <vector>
#include <algorithm>
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"
#include <SDL_opengl.h>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <commdlg.h>
#include <shlobj.h>
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <dirent.h>
#include <signal.h>
#define MKDIR(p) mkdir(p, 0755)
#endif

#include "fuentes.inc"   /* Poppins (OFL) incrustada: k_poppins_regular, k_poppins_medium, k_poppins_bold */

#define VERSION_LAUNCHER "1.1"

/* ---- utilidades ---------------------------------------------------------------- */

static std::string s_datos, s_exe_dir;

static std::string unir(const std::string& a, const std::string& b) {
#ifdef _WIN32
    return a + "\\" + b;
#else
    return a + "/" + b;
#endif
}

static bool existe(const std::string& p) {
    FILE* f = fopen(p.c_str(), "rb");
    if (f) fclose(f);
    return f != NULL;
}

static long tam_archivo(const std::string& p) {
    FILE* f = fopen(p.c_str(), "rb");
    long n;
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fclose(f);
    return n;
}

static std::string recortar(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

static void abrir_carpeta(const std::string& p) {
    MKDIR(p.c_str());
#ifdef _WIN32
    ShellExecuteA(NULL, "open", p.c_str(), NULL, NULL, SW_SHOWNORMAL);
#else
    std::string c = "xdg-open '" + p + "' >/dev/null 2>&1 &";
    if (system(c.c_str())) {}
#endif
}

static uint32_t be32(const uint8_t* p) { return ((uint32_t)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }
static uint16_t be16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }

/* ---- configuracion del launcher ---------------------------------------------- */

struct Config {
    std::string iso;
    bool pantalla_completa = true;
    bool panoramico = false;
    int escala = 0;            /* 0 = automatica (x2), 1..4 */
    bool vsync = false;   /* con vsync el hilo grafico espera al monitor y el juego pierde cuadros */
    bool vibracion = true;
    bool camara_bomba = false;   /* la camara de la bomba encimada en la cabina (como el original) */
    bool texturas_hd = true;
    bool musica = true;        /* arte/musica.ogg|mp3|flac|wav, si hay */
    int musica_vol = 60;
    std::string args;
    std::string tarjeta = "Card A.raw";
};
static Config s_cfg;

static void cfg_cargar() {
    FILE* f = fopen(unir(s_datos, "launcher.ini").c_str(), "rb");
    char linea[2048];
    if (!f) return;
    while (fgets(linea, sizeof linea, f)) {
        std::string l = linea;
        size_t eq = l.find('=');
        if (eq == std::string::npos) continue;
        std::string k = recortar(l.substr(0, eq)), v = recortar(l.substr(eq + 1));
        if (k == "iso") s_cfg.iso = v;
        else if (k == "pantalla_completa") s_cfg.pantalla_completa = v == "1";
        else if (k == "panoramico") s_cfg.panoramico = v == "1";
        else if (k == "escala") s_cfg.escala = atoi(v.c_str());
        else if (k == "vsync_v2") s_cfg.vsync = v == "1";   /* "vsync" (version 0.1.0) se ignora: venia prendido */
        else if (k == "vibracion") s_cfg.vibracion = v == "1";
        else if (k == "camara_bomba") s_cfg.camara_bomba = v == "1";
        else if (k == "texturas_hd") s_cfg.texturas_hd = v == "1";
        else if (k == "musica") s_cfg.musica = v == "1";
        else if (k == "musica_volumen") s_cfg.musica_vol = atoi(v.c_str());
        else if (k == "args") s_cfg.args = v;
        else if (k == "tarjeta" && !v.empty()) s_cfg.tarjeta = v;
    }
    fclose(f);
}

static void cfg_guardar() {
    FILE* f = fopen(unir(s_datos, "launcher.ini").c_str(), "wb");
    if (!f) return;
    fprintf(f, "# Escuadron2: opciones del launcher\n");
    fprintf(f, "iso = %s\n", s_cfg.iso.c_str());
    fprintf(f, "pantalla_completa = %d\npanoramico = %d\nescala = %d\nvsync_v2 = %d\nvibracion = %d\ncamara_bomba = %d\n",
            s_cfg.pantalla_completa, s_cfg.panoramico, s_cfg.escala, s_cfg.vsync, s_cfg.vibracion, s_cfg.camara_bomba);
    fprintf(f, "texturas_hd = %d\n", s_cfg.texturas_hd);
    fprintf(f, "musica = %d\nmusica_volumen = %d\n", s_cfg.musica, s_cfg.musica_vol);
    fprintf(f, "args = %s\ntarjeta = %s\n", s_cfg.args.c_str(), s_cfg.tarjeta.c_str());
    fclose(f);
}

/* ---- imagen del disco ----------------------------------------------------------- */

struct Disco {
    bool ok = false;
    std::string error;
    char id[7] = "";
    std::string nombre, empresa, descripcion;
    long tam = 0;
    int version = 0;
    GLuint banner = 0;   /* 96x32 sacado de opening.bnr */
};
static Disco s_disco;

static void disco_leer(const std::string& path) {
    if (s_disco.banner) glDeleteTextures(1, &s_disco.banner);
    s_disco = Disco();
    if (path.empty()) return;
    FILE* f = fopen(path.c_str(), "rb");
    uint8_t h[0x440];
    if (!f) { s_disco.error = "Cannot open the file."; return; }
    if (fread(h, 1, sizeof h, f) != sizeof h || be32(h + 0x1C) != 0xC2339F3Du) {
        s_disco.error = "This is not a GameCube disc image (uncompressed .iso / .gcm).";
        fclose(f); return;
    }
    memcpy(s_disco.id, h, 6); s_disco.id[6] = 0;
    s_disco.version = h[7];
    s_disco.nombre = std::string((const char*)h + 0x20, strnlen((const char*)h + 0x20, 0x3E0));
    fseek(f, 0, SEEK_END);
    s_disco.tam = ftell(f);
    s_disco.ok = true;
    if (strncmp(s_disco.id, "GSWE64", 6) != 0)
        s_disco.error = std::string("This disc is ") + s_disco.id + ": the port is made for the US disc (GSWE64).";
    /* banner: buscar opening.bnr en la FST */
    {
        uint32_t fo = be32(h + 0x424), fs = be32(h + 0x428);
        if (fs > 0 && fs < 0x100000) {
            std::vector<uint8_t> fst(fs);
            fseek(f, (long)fo, SEEK_SET);
            if (fread(fst.data(), 1, fs, f) == fs) {
                uint32_t n = be32(&fst[8]), strtab = n * 12, i;
                for (i = 1; i < n && strtab < fs; i++) {
                    const uint8_t* e = &fst[i * 12];
                    uint32_t no = be32(e) & 0xFFFFFF;
                    if (e[0] == 0 && strtab + no < fs && !strcmp((const char*)&fst[strtab + no], "opening.bnr")) {
                        std::vector<uint8_t> b(0x1960);
                        fseek(f, (long)be32(e + 4), SEEK_SET);
                        if (fread(b.data(), 1, b.size(), f) == b.size() && !memcmp(b.data(), "BNR", 3)) {
                            std::vector<uint32_t> rgba(96 * 32);
                            int bx, by, x, y, k = 0x20;
                            for (by = 0; by < 32; by += 4) for (bx = 0; bx < 96; bx += 4)
                                for (y = 0; y < 4; y++) for (x = 0; x < 4; x++, k += 2) {
                                    uint16_t v = be16(&b[k]);
                                    uint8_t r, g, bl, a;
                                    if (v & 0x8000) { r = (uint8_t)(((v >> 10) & 31) * 255 / 31); g = (uint8_t)(((v >> 5) & 31) * 255 / 31); bl = (uint8_t)((v & 31) * 255 / 31); a = 255; }
                                    else { a = (uint8_t)(((v >> 12) & 7) * 255 / 7); r = (uint8_t)(((v >> 8) & 15) * 17); g = (uint8_t)(((v >> 4) & 15) * 17); bl = (uint8_t)((v & 15) * 17); }
                                    rgba[(by + y) * 96 + bx + x] = (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)bl << 16) | ((uint32_t)a << 24);
                                }
                            glGenTextures(1, &s_disco.banner);
                            glBindTexture(GL_TEXTURE_2D, s_disco.banner);
                            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 96, 32, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
                            s_disco.empresa = std::string((const char*)&b[0x1820 + 0x20], strnlen((const char*)&b[0x1820 + 0x20], 32));
                            s_disco.descripcion = std::string((const char*)&b[0x1820 + 0xC0], strnlen((const char*)&b[0x1820 + 0xC0], 128));
                            if (b[0x1820]) s_disco.nombre = std::string((const char*)&b[0x1820 + 0x40], strnlen((const char*)&b[0x1820 + 0x40], 64));
                            if (s_disco.nombre.empty()) s_disco.nombre = std::string((const char*)h + 0x20, strnlen((const char*)h + 0x20, 0x3E0));
                        }
                        break;
                    }
                }
            }
        }
    }
    fclose(f);
}

/* ---- controles (mismo formato que port/runtime/controles.c) ---------------------- */

enum { B_A, B_B, B_X, B_Y, B_Z, B_L, B_R, B_START, B_CU, B_CD, B_CL, B_CR,
       B_SU, B_SD, B_SL, B_SR, B_KU, B_KD, B_KL, B_KR, NB };
static const char* k_claves[NB] = { "A", "B", "X", "Y", "Z", "L", "R", "START",
    "CRUZ_ARRIBA", "CRUZ_ABAJO", "CRUZ_IZQUIERDA", "CRUZ_DERECHA",
    "STICK_ARRIBA", "STICK_ABAJO", "STICK_IZQUIERDA", "STICK_DERECHA",
    "C_ARRIBA", "C_ABAJO", "C_IZQUIERDA", "C_DERECHA" };
static const char* k_textos[NB] = { "A", "B", "X", "Y", "Z", "L", "R", "Start",
    "D-pad up", "D-pad down", "D-pad left", "D-pad right",
    "Stick up", "Stick down", "Stick left", "Stick right",
    "C-stick up", "C-stick down", "C-stick left", "C-stick right" };

struct Controles {
    std::vector<std::string> tec[NB], joy[NB];
    int stick = 0, cstick = 1;   /* 0 izquierda, 1 derecha, -1 ninguna */
    int dz_stick = 10, dz_c = 10, gatillo = 80, vibracion = 100;
};
static Controles s_ctl;

static void ctl_defecto() {
    Controles c;
    const char* t[NB][2] = { {"Space","J"}, {"Left Shift","K"}, {"U",0}, {"I",0}, {"Z",0}, {"Q",0}, {"E",0}, {"Return",0},
        {"Up",0}, {"Down",0}, {"Left",0}, {"Right",0}, {"W",0}, {"S",0}, {"A",0}, {"D",0},
        {"Keypad 8",0}, {"Keypad 2",0}, {"Keypad 4",0}, {"Keypad 6",0} };
    const char* j[NB] = { "a", "b", "x", "y", "rightshoulder", "lefttrigger", "righttrigger", "start",
        "dpup", "dpdown", "dpleft", "dpright", 0, 0, 0, 0, 0, 0, 0, 0 };
    for (int i = 0; i < NB; i++) {
        for (int k = 0; k < 2; k++) if (t[i][k]) c.tec[i].push_back(t[i][k]);
        if (j[i]) c.joy[i].push_back(j[i]);
    }
    s_ctl = c;
}

static void ctl_guardar() {
    FILE* f = fopen(unir(s_datos, "controles.ini").c_str(), "wb");
    if (!f) return;
    fprintf(f, "# Escuadron2: controles (lo escribe el launcher, lo lee el juego)\n[teclado]\n");
    for (int i = 0; i < NB; i++) {
        std::string v;
        for (size_t k = 0; k < s_ctl.tec[i].size(); k++) v += (k ? ", " : "") + s_ctl.tec[i][k];
        fprintf(f, "%s = %s\n", k_claves[i], v.c_str());
    }
    fprintf(f, "\n[joystick]\nSTICK = %s\nC = %s\n", s_ctl.stick == 1 ? "derecha" : s_ctl.stick < 0 ? "ninguna" : "izquierda",
            s_ctl.cstick == 0 ? "izquierda" : s_ctl.cstick < 0 ? "ninguna" : "derecha");
    for (int i = 0; i < NB; i++) {
        std::string v;
        for (size_t k = 0; k < s_ctl.joy[i].size(); k++) v += (k ? ", " : "") + s_ctl.joy[i][k];
        fprintf(f, "%s = %s\n", k_claves[i], v.c_str());
    }
    fprintf(f, "\n[ajustes]\nzona_muerta_stick = %d\nzona_muerta_c = %d\ngatillo = %d\nvibracion = %d\n",
            s_ctl.dz_stick, s_ctl.dz_c, s_ctl.gatillo, s_ctl.vibracion);
    fclose(f);
}

static void ctl_cargar() {
    ctl_defecto();
    FILE* f = fopen(unir(s_datos, "controles.ini").c_str(), "rb");
    char linea[1024];
    std::string sec;
    if (!f) return;
    while (fgets(linea, sizeof linea, f)) {
        std::string l = recortar(linea);
        if (l.empty() || l[0] == '#') continue;
        if (l[0] == '[') { sec = l.substr(1, l.find(']') - 1); continue; }
        size_t eq = l.find('=');
        if (eq == std::string::npos) continue;
        std::string k = recortar(l.substr(0, eq)), v = recortar(l.substr(eq + 1));
        if (sec == "ajustes") {
            int n = atoi(v.c_str());
            if (k == "zona_muerta_stick") s_ctl.dz_stick = n;
            else if (k == "zona_muerta_c") s_ctl.dz_c = n;
            else if (k == "gatillo") s_ctl.gatillo = n;
            else if (k == "vibracion") s_ctl.vibracion = n;
            continue;
        }
        if (sec == "joystick" && (k == "STICK" || k == "C")) {
            int p = v == "derecha" ? 1 : v == "ninguna" ? -1 : 0;
            if (k == "STICK") s_ctl.stick = p; else s_ctl.cstick = p;
            continue;
        }
        for (int i = 0; i < NB; i++) if (k == k_claves[i]) {
            std::vector<std::string>& dst = sec == "teclado" ? s_ctl.tec[i] : s_ctl.joy[i];
            dst.clear();
            size_t a = 0;
            while (a <= v.size()) {
                size_t c = v.find(',', a);
                std::string t = recortar(v.substr(a, c == std::string::npos ? std::string::npos : c - a));
                if (!t.empty()) dst.push_back(t);
                if (c == std::string::npos) break;
                a = c + 1;
            }
        }
    }
    fclose(f);
}

/* nombres lindos de los botones del joystick (en un control de PS4 / Xbox) */
static std::string nombre_joy(const std::string& s) {
    static const char* t[][2] = { {"a","Cross / A"}, {"b","Circle / B"}, {"x","Square / X"}, {"y","Triangle / Y"},
        {"back","Share / Back"}, {"guide","PS / Guide"}, {"start","Options / Start"}, {"leftstick","L3"}, {"rightstick","R3"},
        {"leftshoulder","L1 / LB"}, {"rightshoulder","R1 / RB"}, {"dpup","D-pad up"}, {"dpdown","D-pad down"},
        {"dpleft","D-pad left"}, {"dpright","D-pad right"}, {"lefttrigger","L2 / LT"}, {"righttrigger","R2 / RT"},
        {"touchpad","Touchpad"}, {"misc1","Mic (PS5) / Share"} };
    for (auto& p : t) if (s == p[0]) return p[1];
    return s;
}

/* ---- tarjetas de memoria ---------------------------------------------------------- */

struct Partida { std::string juego, archivo; time_t fecha; int bloques; };
struct Tarjeta { std::string archivo; long tam; std::vector<Partida> partidas; };
static std::vector<Tarjeta> s_tarjetas;

static std::string carpeta_tarjetas() { return unir(s_datos, "tarjetas"); }

static void leer_partidas(Tarjeta& t) {
    std::string p = unir(carpeta_tarjetas(), t.archivo);
    FILE* f = fopen(p.c_str(), "rb");
    t.tam = tam_archivo(p);
    if (!f) return;
    uint8_t d[2][0x2000];
    if (fseek(f, 0x2000, SEEK_SET) || fread(d, 1, sizeof d, f) != sizeof d) { fclose(f); return; }
    fclose(f);
    int u = be16(d[1] + 0x1FFA) > be16(d[0] + 0x1FFA) ? 1 : 0;   /* el directorio con el contador mas alto */
    for (int i = 0; i < 127; i++) {
        const uint8_t* e = d[u] + i * 0x40;
        if (e[0] == 0xFF) continue;
        Partida pa;
        pa.juego = std::string((const char*)e, 4) + std::string((const char*)e + 4, 2);
        pa.archivo = std::string((const char*)e + 8, strnlen((const char*)e + 8, 32));
        pa.fecha = (time_t)be32(e + 0x28) + 946684800;   /* segundos desde el 1/1/2000 */
        if (pa.fecha > time(NULL) + 86400 * 365) pa.fecha -= 946684800;   /* guardadas por versiones viejas del port */
        pa.bloques = be16(e + 0x38);
        t.partidas.push_back(pa);
    }
}

static void tarjetas_listar() {
    s_tarjetas.clear();
    std::string dir = carpeta_tarjetas();
    MKDIR(dir.c_str());
    std::vector<std::string> nombres;
#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(unir(dir, "*.raw").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) { do nombres.push_back(fd.cFileName); while (FindNextFileA(h, &fd)); FindClose(h); }
#else
    DIR* d = opendir(dir.c_str());
    if (d) { struct dirent* e; while ((e = readdir(d))) { std::string n = e->d_name; if (n.size() > 4 && n.substr(n.size() - 4) == ".raw") nombres.push_back(n); } closedir(d); }
#endif
    if (std::find(nombres.begin(), nombres.end(), s_cfg.tarjeta) == nombres.end()) nombres.push_back(s_cfg.tarjeta);
    std::sort(nombres.begin(), nombres.end());
    for (auto& n : nombres) { Tarjeta t; t.archivo = n; t.tam = -1; leer_partidas(t); s_tarjetas.push_back(t); }
}

static bool copiar(const std::string& de, const std::string& a) {
    FILE* i = fopen(de.c_str(), "rb");
    FILE* o;
    char buf[65536];
    size_t n;
    if (!i) return false;
    o = fopen(a.c_str(), "wb");
    if (!o) { fclose(i); return false; }
    while ((n = fread(buf, 1, sizeof buf, i)) > 0) fwrite(buf, 1, n, o);
    fclose(i); fclose(o);
    return true;
}

/* ---- elegir archivo -------------------------------------------------------------- */

static std::string elegir_archivo(const char* titulo, const char* filtro_win) {
#ifdef _WIN32
    wchar_t buf[1024] = L"";
    wchar_t filtro[256], tit[256];
    OPENFILENAMEW o;
    int n = MultiByteToWideChar(CP_UTF8, 0, filtro_win, -1, filtro, 255);
    for (int i = 0; i < n; i++) if (filtro[i] == L'|') filtro[i] = 0;
    MultiByteToWideChar(CP_UTF8, 0, titulo, -1, tit, 255);
    memset(&o, 0, sizeof o);
    o.lStructSize = sizeof o;
    o.lpstrFilter = filtro;
    o.lpstrFile = buf;
    o.nMaxFile = 1024;
    o.lpstrTitle = tit;
    o.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&o)) return "";
    /* el juego abre los archivos con fopen (ANSI): con letras raras se usa el nombre corto */
    {
        wchar_t corto[1024];
        char u8[2048];
        bool ascii = true;
        for (wchar_t* p = buf; *p; p++) if (*p > 127) ascii = false;
        if (!ascii && GetShortPathNameW(buf, corto, 1024)) wcscpy(buf, corto);
        WideCharToMultiByte(CP_UTF8, 0, buf, -1, u8, sizeof u8, NULL, NULL);
        return u8;
    }
#else
    (void)titulo; (void)filtro_win;
    FILE* p = popen("zenity --file-selection 2>/dev/null", "r");
    char b[1024] = "";
    if (p) { if (fgets(b, sizeof b, p)) {} pclose(p); }
    return recortar(b);
#endif
}

/* ---- el juego --------------------------------------------------------------------- */

#ifdef _WIN32
static HANDLE s_proc;
#else
static pid_t s_proc;
#endif
static bool s_jugando;
static int s_ultimo_codigo = -1;
static Uint32 s_t_inicio;

static std::string comillas(const std::string& s) { return "\"" + s + "\""; }
static void aj_guardar();
static void aj_cargar();

static std::string carpeta_fotos() {   /* donde el juego guarda las fotos F12 (render_gl.c) */
#ifdef _WIN32
    char p[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_MYPICTURES, NULL, 0, p) == S_OK) { std::string d = unir(p, "Escuadron2"); MKDIR(d.c_str()); return d; }
#endif
    return unir(s_datos, "capturas");
}

/* ---- armar el juego la primera vez (herramientas/armar/armar.py) --------------------------
 * El paquete no trae nada del juego: con el disco del usuario se traduce y se compila
 * escuadron2_game.exe en su PC. Queda al lado del launcher (o en la carpeta de datos si esa no se
 * puede escribir). */
#ifdef _WIN32
static const char* k_exe_juego = "escuadron2_game.exe";
#else
static const char* k_exe_juego = "escuadron2_game";
#endif
static std::string carpeta_juego() {   /* donde se arma el juego */
    return s_datos == s_exe_dir ? s_exe_dir : unir(s_datos, "juego");
}
static std::string exe_juego() {
    std::string a = unir(s_exe_dir, k_exe_juego);
    if (existe(a)) return a;
    return unir(unir(s_datos, "juego"), k_exe_juego);
}
static bool hay_armador() { return existe(unir(unir(unir(s_exe_dir, "herramientas"), "armar"), "armar.py")); }
static bool falta_armar() { return !existe(exe_juego()) && hay_armador(); }

static struct {
    bool activo = false, listo = false, error = false;
    std::string paso, mensaje, ultimas;
    int avance = 0;
    Uint32 t0 = 0;
    SDL_mutex* m = nullptr;
} s_arm;

static void arm_linea(const std::string& l) {
    SDL_LockMutex(s_arm.m);
    if (l.rfind("PASO ", 0) == 0) s_arm.paso = l.substr(5);
    else if (l.rfind("AVANCE ", 0) == 0) s_arm.avance = atoi(l.c_str() + 7);
    else if (l.rfind("ERROR ", 0) == 0) s_arm.mensaje = l.substr(6);
    else if (!l.empty()) { s_arm.ultimas += l + "\n"; if (s_arm.ultimas.size() > 4000) s_arm.ultimas.erase(0, s_arm.ultimas.size() - 4000); }
    SDL_UnlockMutex(s_arm.m);
}

static int arm_hilo(void*) {
    std::string h = unir(s_exe_dir, "herramientas");
    std::string salida = carpeta_juego();
    MKDIR(salida.c_str());
    std::string reg = unir(s_datos, "armado_log.txt");
    FILE* lg = fopen(reg.c_str(), "w");
    bool ok = false;
#ifdef _WIN32
    std::string py = unir(unir(h, "python"), "python.exe");
    std::string cmd = comillas(py) + " -I " + comillas(unir(unir(h, "armar"), "armar.py")) + " --iso " + comillas(s_cfg.iso) +
                      " --salida " + comillas(salida) + " --trabajo " + comillas(unir(s_datos, "armado"));
    SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };
    HANDLE rd, wr;
    if (CreatePipe(&rd, &wr, &sa, 0)) {
        SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
        STARTUPINFOA si; PROCESS_INFORMATION pi;
        memset(&si, 0, sizeof si); si.cb = sizeof si;
        si.dwFlags = STARTF_USESTDHANDLES; si.hStdOutput = wr; si.hStdError = wr; si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        std::vector<char> c(cmd.begin(), cmd.end()); c.push_back(0);
        if (CreateProcessA(py.c_str(), c.data(), NULL, NULL, TRUE, CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS, NULL, s_datos.c_str(), &si, &pi)) {
            CloseHandle(wr); wr = NULL;
            char buf[4096]; DWORD n; std::string l;
            while (ReadFile(rd, buf, sizeof buf, &n, NULL) && n) {
                if (lg) { fwrite(buf, 1, n, lg); fflush(lg); }
                for (DWORD i = 0; i < n; i++) { if (buf[i] == '\n') { arm_linea(l); l.clear(); } else if (buf[i] != '\r') l += buf[i]; }
            }
            WaitForSingleObject(pi.hProcess, INFINITE);
            DWORD code = 1; GetExitCodeProcess(pi.hProcess, &code);
            CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
            ok = code == 0;
        } else arm_linea("ERROR Could not start the bundled Python (herramientas\\python\\python.exe).");
        if (wr) CloseHandle(wr);
        CloseHandle(rd);
    }
#else
    std::string cmd = "python3 -I " + comillas(unir(unir(h, "armar"), "armar.py")) + " --iso " + comillas(s_cfg.iso) +
                      " --salida " + comillas(salida) + " --trabajo " + comillas(unir(s_datos, "armado")) + " 2>&1";
    FILE* p = popen(cmd.c_str(), "r");
    if (p) {
        char buf[4096];
        while (fgets(buf, sizeof buf, p)) {
            if (lg) { fputs(buf, lg); fflush(lg); }
            std::string l = buf; while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
            arm_linea(l);
        }
        ok = pclose(p) == 0;
    }
#endif
    if (lg) fclose(lg);
    SDL_LockMutex(s_arm.m);
    s_arm.activo = false;
    s_arm.listo = ok && existe(exe_juego());
    s_arm.error = !s_arm.listo;
    if (s_arm.error && s_arm.mensaje.empty()) s_arm.mensaje = "Something went wrong while preparing the game. Details in armado_log.txt (System tab > Launcher data).";
    SDL_UnlockMutex(s_arm.m);
    return 0;
}

static void arm_empezar() {
    if (s_arm.activo) return;
    if (!s_arm.m) s_arm.m = SDL_CreateMutex();
    s_arm.activo = true; s_arm.listo = false; s_arm.error = false;
    s_arm.paso = "Starting"; s_arm.mensaje.clear(); s_arm.ultimas.clear(); s_arm.avance = 0;
    s_arm.t0 = SDL_GetTicks();
    SDL_DetachThread(SDL_CreateThread(arm_hilo, "armar", NULL));
}

static bool lanzar() {
    std::vector<std::string> a;
    std::string exe = exe_juego();
    std::string tarjeta = unir(carpeta_tarjetas(), s_cfg.tarjeta);
    a.push_back("--iso"); a.push_back(s_cfg.iso);
    a.push_back("--memcard"); a.push_back(tarjeta);
    aj_guardar();   /* video, audio y demas: el juego lee ajustes.ini de su carpeta actual (s_datos) */
    {   /* argumentos extra, separados por espacios */
        std::string x = s_cfg.args, t;
        for (size_t i = 0; i <= x.size(); i++) {
            if (i == x.size() || x[i] == ' ') { if (!t.empty()) a.push_back(t); t.clear(); }
            else t += x[i];
        }
    }
    ctl_guardar();
#ifdef _WIN32
    std::string cmd = comillas(exe);
    for (auto& s : a) cmd += " " + comillas(s);
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof si); si.cb = sizeof si;
    memset(&pi, 0, sizeof pi);
    std::vector<char> c(cmd.begin(), cmd.end()); c.push_back(0);
    if (!CreateProcessA(exe.c_str(), c.data(), NULL, NULL, FALSE, 0, NULL, s_datos.c_str(), &si, &pi)) return false;
    CloseHandle(pi.hThread);
    s_proc = pi.hProcess;
#else
    pid_t p = fork();
    if (p < 0) return false;
    if (p == 0) {
        std::vector<char*> v;
        if (chdir(s_datos.c_str())) _exit(127);
        v.push_back((char*)exe.c_str());
        for (auto& s : a) v.push_back((char*)s.c_str());
        v.push_back(NULL);
        execv(exe.c_str(), v.data());
        _exit(127);
    }
    s_proc = p;
#endif
    s_jugando = true;
    s_t_inicio = SDL_GetTicks();
    return true;
}

static void revisar_juego() {
    if (!s_jugando) return;
#ifdef _WIN32
    DWORD c;
    if (WaitForSingleObject(s_proc, 0) == WAIT_OBJECT_0) {
        GetExitCodeProcess(s_proc, &c);
        CloseHandle(s_proc);
        s_ultimo_codigo = (int)c;
        s_jugando = false;
    }
#else
    int st;
    if (waitpid(s_proc, &st, WNOHANG) == s_proc) { s_ultimo_codigo = WIFEXITED(st) ? WEXITSTATUS(st) : 128; s_jugando = false; }
#endif
    if (!s_jugando) { tarjetas_listar(); aj_cargar(); }
}

#include <cfloat>
#include "estilo.h"
#include "ajustes.h"
#include "musica.h"

/* ---- opciones compartidas con el juego (ajustes.ini) ----------------------------------- */

static Ajustes s_aj;
static AjustesUI s_aj_ui;

static void aj_guardar() { ajustes_guardar(s_aj, unir(s_datos, "ajustes.ini")); }

/* se lee cada vez que se vuelve al launcher: el menu del juego (F1) puede haberlas cambiado */
static void aj_cargar() {
    if (ajustes_cargar(s_aj, unir(s_datos, "ajustes.ini"))) return;
    /* la primera vez: lo que estaba en launcher.ini (versiones anteriores) */
    s_aj.pantalla_completa = s_cfg.pantalla_completa; s_aj.panoramico = s_cfg.panoramico;
    s_aj.escala = s_cfg.escala; s_aj.vsync = s_cfg.vsync; s_aj.vibracion = s_cfg.vibracion;
    s_aj.camara_bomba = s_cfg.camara_bomba; s_aj.texturas_hd = s_cfg.texturas_hd;
    aj_guardar();
}

static void pest_opciones(ImVec2 org, ImVec2 tam, float t) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilledMultiColor(org, ImVec2(org.x + tam.x, org.y + tam.y), IM_COL32(4, 6, 16, 255), IM_COL32(6, 8, 22, 255),
                                IM_COL32(2, 3, 9, 255), IM_COL32(3, 4, 12, 255));
    fondo_estrellas(dl, org, ImVec2(org.x + tam.x, org.y + tam.y), t);
    s_aj_ui.en_juego = false;
    { static bool p; const char* e = getenv("RL_LAUNCHER_OPCIONES"); if (e && !p) { p = true; s_aj_ui.pest = atoi(e); } }   /* pruebas */
    if (ajustes_ui(s_aj, s_aj_ui, ImVec2(org.x, org.y + 70), ImVec2(tam.x, tam.y - 70))) aj_guardar();
}

/* ---- pestanas ------------------------------------------------------------------------ */

enum { P_JUGAR, P_OPCIONES, P_CONTROLES, P_TARJETAS, P_AJUSTES, P_REGISTROS, NP };
static int s_pest = P_JUGAR;
static const char* k_pest[NP] = { "Play", "Options", "Controls", "Memory cards", "System", "Logs" };

static std::string s_aviso;   /* mensaje abajo (copias, errores) */
static Uint32 s_aviso_t;
static void avisar(const std::string& m) { s_aviso = m; s_aviso_t = SDL_GetTicks(); }

static void barra(float w) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGui::SetCursorPos(ImVec2(40, 26));
    for (int i = 0; i < NP; i++) {
        ImGui::PushFont(s_f_medio);
        ImVec2 tam = ImGui::CalcTextSize(k_pest[i]);
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("pest", ImVec2(tam.x, tam.y + 10))) s_pest = i;
        bool sobre = ImGui::IsItemHovered();
        ImGui::PopID();
        dl->AddText(p, i == s_pest ? IM_COL32(255, 255, 255, 255) : sobre ? IM_COL32(230, 230, 235, 255) : C_TENUE, k_pest[i]);
        if (i == s_pest) dl->AddRectFilled(ImVec2(p.x, p.y + tam.y + 6), ImVec2(p.x + tam.x, p.y + tam.y + 8), C_ACENTO, 1);
        ImGui::PopFont();
        ImGui::SameLine(0, 36);
    }
    if (mus_hay()) {   /* parlante: prende y apaga la musica */
        ImVec2 c(w - 46, 40);
        ImGui::SetCursorScreenPos(ImVec2(c.x - 18, c.y - 18));
        if (ImGui::InvisibleButton("musica", ImVec2(36, 36))) { s_cfg.musica = !s_cfg.musica; cfg_guardar(); }
        bool sobre = ImGui::IsItemHovered();
        ImU32 col = sobre ? IM_COL32(255, 255, 255, 255) : s_cfg.musica ? C_ACENTO : C_TENUE;
        ImVec2 q[4] = { ImVec2(c.x - 11, c.y - 5), ImVec2(c.x - 5, c.y - 5), ImVec2(c.x - 5, c.y + 5), ImVec2(c.x - 11, c.y + 5) };
        dl->AddConvexPolyFilled(q, 4, col);
        dl->AddTriangleFilled(ImVec2(c.x - 6, c.y - 5), ImVec2(c.x + 3, c.y - 12), ImVec2(c.x + 3, c.y + 12), col);
        dl->AddTriangleFilled(ImVec2(c.x - 6, c.y - 5), ImVec2(c.x + 3, c.y + 12), ImVec2(c.x - 6, c.y + 5), col);
        if (s_cfg.musica) {
            dl->PathArcTo(ImVec2(c.x + 3, c.y), 7, -0.9f, 0.9f); dl->PathStroke(col, ImDrawFlags_None, 2.0f);
            dl->PathArcTo(ImVec2(c.x + 3, c.y), 12, -0.9f, 0.9f); dl->PathStroke(col, ImDrawFlags_None, 2.0f);
        } else {
            dl->AddLine(ImVec2(c.x + 7, c.y - 6), ImVec2(c.x + 15, c.y + 6), col, 2);
            dl->AddLine(ImVec2(c.x + 15, c.y - 6), ImVec2(c.x + 7, c.y + 6), col, 2);
        }
        if (sobre) ImGui::SetTooltip(s_cfg.musica ? "Turn music off" : "Turn music on");
    }
    ImGui::NewLine();
}

static void encabezado(const char* t, const char* sub) {
    ImGui::PushFont(s_f_titulo); ImGui::TextUnformatted(t); ImGui::PopFont();
    if (sub) { ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]); ImGui::TextUnformatted(sub); ImGui::PopStyleColor(); }
    ImGui::Dummy(ImVec2(0, 8));
}

static void elegir_disco() {
    std::string p = elegir_archivo("Choose your disc image (GSWE64)", "GameCube image (*.iso, *.gcm)|*.iso;*.gcm|All files|*.*|");
    if (p.empty()) return;
    s_cfg.iso = p;
    cfg_guardar();
    disco_leer(p);
}

/* arte opcional (carpeta arte/ de los datos o del programa): fondos que se van turnando,
   portada, foto del control y del disco. Si no hay, se ve el fondo de estrellas. */
static std::vector<Imagen> s_fondos;
static Imagen s_portada, s_control, s_disco_img;

static void arte_cargar() {
    std::string dirs[2] = { unir(s_datos, "arte"), unir(s_exe_dir, "arte") };
    for (auto& d : dirs) {
        std::vector<std::string> n;
#ifdef _WIN32
        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA(unir(d, "*").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) { do n.push_back(fd.cFileName); while (FindNextFileA(h, &fd)); FindClose(h); }
#else
        DIR* dd = opendir(d.c_str());
        if (dd) { struct dirent* e; while ((e = readdir(dd))) n.push_back(e->d_name); closedir(dd); }
#endif
        std::sort(n.begin(), n.end());
        for (auto& a : n) {
            std::string b = a.substr(0, a.find('.'));
            if (b.rfind("fondo", 0) == 0) { Imagen im = imagen_de_archivo(unir(d, a)); if (im.tex) s_fondos.push_back(im); }
            else if (b == "portada" && !s_portada.tex) s_portada = imagen_de_archivo(unir(d, a));
            else if (b == "control" && !s_control.tex) s_control = imagen_de_archivo(unir(d, a));
            else if (b == "disco" && !s_disco_img.tex) s_disco_img = imagen_de_archivo(unir(d, a));
        }
        if (!s_fondos.empty()) break;
    }
}

static void pest_jugar(ImVec2 org, ImVec2 tam, float t) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 fin(org.x + tam.x, org.y + tam.y);
    if (!s_fondos.empty()) {   /* fondos que se van turnando cada 12 s, con fundido */
        const float dur = 12, fun = 1.5f;
        int n = (int)s_fondos.size(), i = (int)(t / dur) % n;
        float f = t - (float)(int)(t / dur) * dur;
        float zoom = 1.0f + 0.04f * (f / dur);   /* acercamiento lento */
        ImVec2 m(tam.x * (zoom - 1) * 0.5f, tam.y * (zoom - 1) * 0.5f);
        dl->AddRectFilled(org, fin, IM_COL32(0, 0, 0, 255));
        dibujar_cubrir(dl, s_fondos[i], ImVec2(org.x - m.x, org.y - m.y), ImVec2(fin.x + m.x, fin.y + m.y));
        if (n > 1 && f > dur - fun) dibujar_cubrir(dl, s_fondos[(i + 1) % n], org, fin, (int)(255 * (f - (dur - fun)) / fun));
        velo(dl, org, fin);
    } else {
        fondo_estrellas(dl, org, fin, t);   /* sin el banner del disco: el launcher no muestra nada del juego */
    }
    /* portada y titulo abajo a la izquierda */
    float x0 = org.x + 52, y0 = org.y + tam.y - 150;
    if (s_portada.tex) {
        float ph = std::min(250.0f, tam.y * 0.32f), pw = ph * s_portada.w / s_portada.h;
        ImVec2 a(x0, fin.y - 52 - ph);
        dl->AddRectFilled(ImVec2(a.x + 6, a.y + 10), ImVec2(a.x + pw + 6, a.y + ph + 10), IM_COL32(0, 0, 0, 120), 10);
        dl->AddImageRounded((ImTextureID)(intptr_t)s_portada.tex, a, ImVec2(a.x + pw, a.y + ph), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 255), 8);
        x0 += pw + 30;
        y0 = fin.y - 52 - 110;
    }
    dl->AddText(s_f_medio, s_f_medio->LegacySize, ImVec2(x0 + 2, y0 - 30), C_ACENTO, "PC PORT");
    dl->AddText(s_f_grande, s_f_grande->LegacySize, ImVec2(x0, y0 - 8), IM_COL32(255, 255, 255, 255), "ESCUADRON2");
    dl->AddText(s_f_normal, s_f_normal->LegacySize, ImVec2(x0 + 2, y0 + 44), IM_COL32(230, 230, 235, 255), "GAMECUBE  |  GSWE64");
    {
        char info[256];
        if (s_disco.ok) snprintf(info, sizeof info, "%s   |   %.0f MB   |   version 1.%d   |   port %s", s_disco.id, s_disco.tam / 1048576.0, s_disco.version, VERSION_LAUNCHER);
        else snprintf(info, sizeof info, "No disc image   |   port %s", VERSION_LAUNCHER);
        dl->AddText(s_f_normal, s_f_normal->LegacySize, ImVec2(x0 + 2, y0 + 86), C_TENUE, info);
    }
    /* boton */
    ImGui::SetCursorScreenPos(ImVec2(org.x + tam.x - 52 - 180, org.y + tam.y - 120));
    ImGui::PushFont(s_f_medio);
    if (s_jugando) {
        ImGui::BeginDisabled();
        ImGui::Button("Playing...", ImVec2(180, 58));
        ImGui::EndDisabled();
    } else if (!s_disco.ok) {
        if (boton_naranja("Choose disc", ImVec2(180, 58))) elegir_disco();
    } else if (s_arm.activo) {
        ImGui::BeginDisabled();
        ImGui::Button("Preparing...", ImVec2(180, 58));
        ImGui::EndDisabled();
    } else if (falta_armar()) {
        if (boton_naranja("Prepare game", ImVec2(180, 58))) arm_empezar();
    } else if (!existe(exe_juego())) {
        ImGui::BeginDisabled();
        ImGui::Button("Play", ImVec2(180, 58));
        ImGui::EndDisabled();
    } else if (boton_naranja("Play", ImVec2(180, 58))) {
        if (!lanzar()) avisar("Could not start the game: escuadron2 is missing next to the launcher.");
    }
    ImGui::PopFont();
    if (!s_jugando && !s_arm.activo && !existe(exe_juego()) && !hay_armador()) {   /* zip sin descomprimir entero */
        float w = 560, x = org.x + tam.x - 52 - w, y = org.y + tam.y - 150 - 150;
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + 132), IM_COL32(12, 20, 44, 235), 12);
        dl->AddText(s_f_normal, s_f_normal->LegacySize, ImVec2(x + 16, y + 14), IM_COL32(255, 150, 130, 255), "The herramientas folder is missing");
        std::string m = "Extract the WHOLE zip to a folder of your own (right click > Extract All) and open Escuadron2.exe from there. "
                        "The herramientas folder must be next to it. Now running from: " + s_exe_dir;
        dl->AddText(s_f_normal, s_f_normal->LegacySize, ImVec2(x + 16, y + 44), IM_COL32(235, 235, 240, 255), m.c_str(), NULL, w - 32);
    } else
    if (s_arm.activo || s_arm.error || (s_disco.ok && falta_armar())) {   /* panel del armado */
        float w = 560, x = org.x + tam.x - 52 - w, y = org.y + tam.y - 150 - 150;
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + 132), IM_COL32(12, 20, 44, 235), 12);
        std::string paso, msj; int av;
        if (s_arm.m) SDL_LockMutex(s_arm.m);
        paso = s_arm.paso; msj = s_arm.mensaje; av = s_arm.avance;
        if (s_arm.m) SDL_UnlockMutex(s_arm.m);
        if (s_arm.activo) {
            char t[200];
            Uint32 seg = (SDL_GetTicks() - s_arm.t0) / 1000;
            snprintf(t, sizeof t, "Preparing the game from your disc (first time only)  -  %u:%02u", seg / 60, seg % 60);
            dl->AddText(s_f_normal, s_f_normal->LegacySize, ImVec2(x + 16, y + 14), C_ACENTO, t);
            dl->AddText(s_f_normal, s_f_normal->LegacySize, ImVec2(x + 16, y + 44), IM_COL32(235, 235, 240, 255), paso.c_str());
            dl->AddRectFilled(ImVec2(x + 16, y + 84), ImVec2(x + w - 16, y + 100), IM_COL32(40, 50, 80, 255), 8);
            dl->AddRectFilled(ImVec2(x + 16, y + 84), ImVec2(x + 16 + (w - 32) * av / 100.0f, y + 100), IM_COL32(255, 212, 38, 255), 8);
            char pc[16]; snprintf(pc, sizeof pc, "%d%%", av);
            dl->AddText(s_f_normal, s_f_normal->LegacySize, ImVec2(x + w - 60, y + 104), C_TENUE, pc);
        } else if (s_arm.error) {
            dl->AddText(s_f_normal, s_f_normal->LegacySize, ImVec2(x + 16, y + 14), IM_COL32(255, 150, 130, 255), "Could not prepare the game");
            dl->AddText(s_f_normal, s_f_normal->LegacySize, ImVec2(x + 16, y + 44), IM_COL32(235, 235, 240, 255), msj.c_str(), NULL, w - 32);
        } else {
            dl->AddText(s_f_normal, s_f_normal->LegacySize, ImVec2(x + 16, y + 14), C_ACENTO, "One more step");
            dl->AddText(s_f_normal, s_f_normal->LegacySize, ImVec2(x + 16, y + 44), IM_COL32(235, 235, 240, 255),
                        "The first time, the launcher builds the game from your own disc. It takes a few minutes and only happens once. Press Prepare game.", NULL, w - 32);
        }
    }
    if (!s_disco.ok || !s_disco.error.empty()) {
        const char* m = s_disco.ok ? s_disco.error.c_str() : s_cfg.iso.empty() ?
            "To play you need your own copy of the game: choose the disc image (.iso or .gcm)." : s_disco.error.c_str();
        ImVec2 ts = ImGui::CalcTextSize(m, NULL, false, 520);
        ImVec2 p(org.x + tam.x - 52 - 540, org.y + 30);
        dl->AddRectFilled(p, ImVec2(p.x + 540, p.y + ts.y + 28), IM_COL32(12, 20, 44, 235), 12);
        dl->AddText(s_f_normal, s_f_normal->LegacySize, ImVec2(p.x + 14, p.y + 14), IM_COL32(240, 240, 245, 255), m, NULL, 512);
    }
    if (!s_jugando && s_ultimo_codigo > 0) {
        ImGui::SetCursorScreenPos(ImVec2(org.x + tam.x - 52 - 540, org.y + tam.y - 52));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.6f, 0.5f, 1));
        ImGui::Text("The game closed with an error (%d). Check the Logs tab.", s_ultimo_codigo);
        ImGui::PopStyleColor();
    }
}

/* captura de un boton: cual y desde que (0 teclado, 1 joystick) */
static int s_capturando = -1, s_cap_modo = 0;
static int s_modo_ctl = 0;   /* 0 joystick, 1 teclado */
static SDL_GameController* s_pad;

static void celda_control(int b, float ancho) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 tam(ancho, 52);
    ImGui::PushID(b);
    bool clic = ImGui::InvisibleButton("c", tam);
    bool der = ImGui::IsItemClicked(ImGuiMouseButton_Right);
    bool sobre = ImGui::IsItemHovered();
    ImGui::PopID();
    bool cap = s_capturando == b;
    dl->AddRectFilled(p, ImVec2(p.x + tam.x, p.y + tam.y), cap ? IM_COL32(72, 20, 24, 255) : sobre ? IM_COL32(32, 31, 37, 255) : C_FILA, 10);
    /* insignia del boton */
    ImU32 col = b == B_A ? IM_COL32(20, 190, 120, 255) : b == B_B ? IM_COL32(220, 40, 40, 255) : b == B_START ? IM_COL32(230, 230, 230, 255)
              : (b >= B_KU && b <= B_KR) ? IM_COL32(245, 196, 20, 255) : IM_COL32(210, 210, 220, 255);
    dl->AddCircleFilled(ImVec2(p.x + 26, p.y + 26), 13, col);
    const char* corto = b == B_START ? "S" : b <= B_R ? k_claves[b] : (b >= B_CU && b <= B_CR) ? "+" : (b >= B_SU && b <= B_SR) ? "O" : "C";
    ImVec2 cs = ImGui::CalcTextSize(corto);
    dl->AddText(ImVec2(p.x + 26 - cs.x / 2, p.y + 26 - cs.y / 2), IM_COL32(20, 19, 23, 255), corto);
    dl->AddText(ImVec2(p.x + 50, p.y + 6), C_TENUE, k_textos[b]);
    std::string v;
    if (cap) v = s_modo_ctl ? "Press a key... (Esc cancels)" : "Press a button... (Esc cancels)";
    else {
        const std::vector<std::string>& l = s_modo_ctl ? s_ctl.tec[b] : s_ctl.joy[b];
        for (size_t k = 0; k < l.size(); k++) v += (k ? ", " : "") + (s_modo_ctl ? l[k] : nombre_joy(l[k]));
        if (v.empty()) v = (!s_modo_ctl && b >= B_SU) ? (b >= B_KU ? "controller stick" : "controller stick") : "-";
    }
    dl->AddText(ImVec2(p.x + 50, p.y + 26), cap ? C_ACENTO : IM_COL32(240, 240, 245, 255), v.c_str());
    if (clic) { s_capturando = b; s_cap_modo = s_modo_ctl; }
    if (der) { (s_modo_ctl ? s_ctl.tec[b] : s_ctl.joy[b]).clear(); ctl_guardar(); }
}

/* llega un evento de SDL mientras se captura */
static bool capturar(const SDL_Event& e) {
    if (s_capturando < 0) return false;
    if (e.type == SDL_KEYDOWN && e.key.keysym.scancode == SDL_SCANCODE_ESCAPE) { s_capturando = -1; return true; }
    if (s_cap_modo == 1 && e.type == SDL_KEYDOWN) {
        s_ctl.tec[s_capturando] = { SDL_GetScancodeName(e.key.keysym.scancode) };
        s_capturando = -1; ctl_guardar(); return true;
    }
    if (s_cap_modo == 0 && e.type == SDL_CONTROLLERBUTTONDOWN) {
        s_ctl.joy[s_capturando] = { SDL_GameControllerGetStringForButton((SDL_GameControllerButton)e.cbutton.button) };
        s_capturando = -1; ctl_guardar(); return true;
    }
    if (s_cap_modo == 0 && e.type == SDL_CONTROLLERAXISMOTION && e.caxis.value > 20000 &&
        (e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT || e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT)) {
        s_ctl.joy[s_capturando] = { SDL_GameControllerGetStringForAxis((SDL_GameControllerAxis)e.caxis.axis) };
        s_capturando = -1; ctl_guardar(); return true;
    }
    return false;
}

static void deslizador(const char* id, const char* t, const char* sub, int* v, int mn, int mx, float ancho) {
    char s[128];
    snprintf(s, sizeof s, sub, *v);
    renglon(t, s, ancho, 300);
    ImGui::SetNextItemWidth(300);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.2f, 0.19f, 0.23f, 1));
    if (ImGui::SliderInt(id, v, mn, mx, "")) ctl_guardar();
    ImGui::PopStyleColor();
}


/* ---- dibujo del control con lo que hace cada boton en el juego ------------------------------ */

static std::string asignado(int b) {   /* que tecla o boton tiene asignado ahora */
    const std::vector<std::string>& l = s_modo_ctl ? s_ctl.tec[b] : s_ctl.joy[b];
    if (l.empty()) return "-";
    return s_modo_ctl ? l[0] : nombre_joy(l[0]);
}
static std::string asignado_palanca(int b0, int cual) {   /* las 4 direcciones juntas */
    if (!s_modo_ctl) {
        int lado = cual == 0 ? s_ctl.stick : s_ctl.cstick;
        return lado == 0 ? "Left stick" : lado == 1 ? "Right stick" : "-";
    }
    std::string r;
    for (int d = 0; d < 4; d++) { if (d) r += " "; r += s_ctl.tec[b0 + d].empty() ? "-" : s_ctl.tec[b0 + d][0]; }
    return r;
}

static void dibujar_control(float ancho) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 o = ImGui::GetCursorScreenPos();
    const float H = 420, cx = o.x + ancho * 0.5f, cy = o.y + 215;
    const float s = 1.45f;   /* escala del cuerpo */
    const ImU32 cuerpo = IM_COL32(58, 52, 104, 255), borde = IM_COL32(88, 80, 150, 255), gris = IM_COL32(190, 190, 200, 255);
    const ImU32 linea = IM_COL32(150, 148, 165, 200), txt = IM_COL32(240, 240, 245, 255), tenue = IM_COL32(150, 148, 160, 255);
    dl->AddRectFilled(o, ImVec2(o.x + ancho, o.y + H), C_PANEL, 14);
    /* cuerpo: centro, dos agarres y hombros */
    dl->AddRectFilled(ImVec2(cx - 70 * s, cy - 92 * s), ImVec2(cx - 30 * s, cy - 80 * s), gris, 4);   /* L */
    dl->AddRectFilled(ImVec2(cx + 30 * s, cy - 92 * s), ImVec2(cx + 70 * s, cy - 80 * s), gris, 4);   /* R */
    dl->AddRectFilled(ImVec2(cx + 34 * s, cy - 100 * s), ImVec2(cx + 62 * s, cy - 92 * s), IM_COL32(120, 110, 200, 255), 3);   /* Z */
    dl->AddCircleFilled(ImVec2(cx - 78 * s, cy + 30 * s), 62 * s, borde, 48);
    dl->AddCircleFilled(ImVec2(cx + 78 * s, cy + 30 * s), 62 * s, borde, 48);
    dl->AddRectFilled(ImVec2(cx - 130 * s, cy - 82 * s), ImVec2(cx + 130 * s, cy + 30 * s), borde, 46);
    dl->AddCircleFilled(ImVec2(cx - 78 * s, cy + 30 * s), 58 * s, cuerpo, 48);
    dl->AddCircleFilled(ImVec2(cx + 78 * s, cy + 30 * s), 58 * s, cuerpo, 48);
    dl->AddRectFilled(ImVec2(cx - 126 * s, cy - 78 * s), ImVec2(cx + 126 * s, cy + 26 * s), cuerpo, 42);
    /* palanca, cruz, palanca C, start */
    ImVec2 pStick(cx - 82 * s, cy - 30 * s), pCruz(cx - 42 * s, cy + 22 * s), pC(cx + 42 * s, cy + 22 * s), pStart(cx, cy - 34 * s);
    dl->AddCircleFilled(pStick, 24 * s, IM_COL32(40, 36, 70, 255), 32);
    dl->AddCircleFilled(pStick, 17 * s, gris, 32);
    dl->AddRectFilled(ImVec2(pCruz.x - 14 * s, pCruz.y - 4.5f * s), ImVec2(pCruz.x + 14 * s, pCruz.y + 4.5f * s), gris, 2);
    dl->AddRectFilled(ImVec2(pCruz.x - 4.5f * s, pCruz.y - 14 * s), ImVec2(pCruz.x + 4.5f * s, pCruz.y + 14 * s), gris, 2);
    dl->AddCircleFilled(pC, 18 * s, IM_COL32(40, 36, 70, 255), 32);
    dl->AddCircleFilled(pC, 12 * s, IM_COL32(245, 196, 20, 255), 32);
    dl->AddCircleFilled(pStart, 6 * s, gris, 16);
    /* botones de la derecha */
    ImVec2 pA(cx + 88 * s, cy - 30 * s), pB(cx + 60 * s, cy - 8 * s), pX(cx + 118 * s, cy - 36 * s), pY(cx + 84 * s, cy - 60 * s);
    dl->AddCircleFilled(pA, 16 * s, IM_COL32(20, 170, 110, 255), 32);
    dl->AddCircleFilled(pB, 9 * s, IM_COL32(215, 40, 40, 255), 24);
    dl->AddRectFilled(ImVec2(pX.x - 6 * s, pX.y - 14 * s), ImVec2(pX.x + 6 * s, pX.y + 14 * s), gris, 6);
    dl->AddRectFilled(ImVec2(pY.x - 14 * s, pY.y - 6 * s), ImVec2(pY.x + 14 * s, pY.y + 6 * s), gris, 6);
    auto letra = [&](ImVec2 p, const char* t, ImU32 c) { ImVec2 ts = ImGui::CalcTextSize(t); dl->AddText(ImVec2(p.x - ts.x / 2, p.y - ts.y / 2), c, t); };
    letra(pA, "A", IM_COL32(255, 255, 255, 255)); letra(pB, "B", IM_COL32(255, 255, 255, 255));
    /* etiquetas: titulo (accion en el juego) y lo asignado ahora */
    struct Et { ImVec2 punto; int lado; float y; const char* nombre; const char* accion; std::string asig; };
    float xi = o.x + 28, xd = o.x + ancho - 28;
    Et ets[] = {
        { ImVec2(cx - 50 * s, cy - 88 * s), 0, o.y + 30,  "L", "Brake", asignado(B_L) },
        { pStick, 0, o.y + 130, "Stick", "Fly", asignado_palanca(B_SU, 0) },
        { pStart, 0, o.y + 230, "Start", "Pause", asignado(B_START) },
        { pCruz, 0, o.y + 330, "D-pad", "Wingman orders", s_modo_ctl ? asignado_palanca(B_CU, 2) : std::string("D-pad") },
        { ImVec2(cx + 50 * s, cy - 88 * s), 1, o.y + 20, "R", "Boost", asignado(B_R) },
        { ImVec2(cx + 48 * s, cy - 98 * s), 1, o.y + 75, "Z", "Roll", asignado(B_Z) },
        { pY, 1, o.y + 130, "Y", "Change view", asignado(B_Y) },
        { pX, 1, o.y + 185, "X", "Targeting computer", asignado(B_X) },
        { pA, 1, o.y + 240, "A", "Fire (hold: burst)", asignado(B_A) },
        { pB, 1, o.y + 295, "B", "Secondary fire", asignado(B_B) },
        { pC, 1, o.y + 350, "C-stick", "Look around", asignado_palanca(B_KU, 1) },
    };
    for (auto& e : ets) {
        float x = e.lado ? xd : xi;
        ImVec2 t1 = ImGui::CalcTextSize(e.accion), t2 = ImGui::CalcTextSize(e.asig.c_str());
        float tw = std::max(t1.x, t2.x) + ImGui::CalcTextSize(e.nombre).x + 12;
        float bx = e.lado ? x - tw : x;          /* caja de texto */
        float lx = e.lado ? bx - 10 : bx + tw + 10;
        float ly = e.y + 18;
        dl->AddLine(ImVec2(lx, ly), ImVec2(e.lado ? lx - 24 : lx + 24, ly), linea, 1.5f);
        dl->AddLine(ImVec2(e.lado ? lx - 24 : lx + 24, ly), e.punto, linea, 1.5f);
        dl->AddCircleFilled(e.punto, 3, linea);
        dl->AddText(s_f_medio, s_f_medio->LegacySize, ImVec2(bx, e.y), C_ACENTO, e.nombre);
        float tx = bx + ImGui::CalcTextSize(e.nombre).x + 12;
        dl->AddText(ImVec2(tx, e.y), txt, e.accion);
        dl->AddText(ImVec2(tx, e.y + 20), tenue, e.asig.c_str());
    }
    ImGui::Dummy(ImVec2(ancho, H + 12));
}

static void pest_controles(float ancho) {
    encabezado("Controls", "Click a button to change it; right-click clears it. Saved automatically.");
    {
        bool joy = s_modo_ctl == 0;
        if (joy) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.45f, 0.36f, 0.05f, 1));
        if (ImGui::Button("Controller")) s_modo_ctl = 0;
        if (joy) ImGui::PopStyleColor();
        ImGui::SameLine();
        if (!joy) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.45f, 0.36f, 0.05f, 1));
        if (ImGui::Button("Keyboard")) s_modo_ctl = 1;
        if (!joy) ImGui::PopStyleColor();
        ImGui::SameLine(ancho - 200);
        if (ImGui::Button("Restore defaults", ImVec2(200, 0))) { ctl_defecto(); ctl_guardar(); }
    }
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    if (s_modo_ctl == 0) ImGui::Text("Controller: %s", s_pad ? SDL_GameControllerName(s_pad) : "none connected (plug one in and it shows up)");
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0, 6));
    dibujar_control(std::min(ancho, 1100.0f));
    float col = (ancho - 48 - 12) / 2;
    struct { const char* t; int a, b; } grupos[] = { {"Buttons", B_A, B_START}, {"D-pad", B_CU, B_CR}, {"Stick", B_SU, B_SR}, {"C-stick", B_KU, B_KR} };
    for (int g = 0; g < 4; g++) {
        if (s_modo_ctl == 0 && g >= 2) break;   /* en el joystick las palancas van enteras (abajo) */
        panel(grupos[g].t, NULL, ancho);
        for (int b = grupos[g].a; b <= grupos[g].b; b++) {
            celda_control(b, col);
            if (((b - grupos[g].a) & 1) == 0 && b < grupos[g].b) ImGui::SameLine();
        }
        fin_panel(ancho);
    }
    if (s_modo_ctl == 0) {
        panel("Sticks", "Which controller stick acts as the main stick and which as the C-stick.", ancho);
        const char* op[] = { "Left", "Right", "None" };
        int a = s_ctl.stick < 0 ? 2 : s_ctl.stick, c = s_ctl.cstick < 0 ? 2 : s_ctl.cstick;
        renglon("Main stick", NULL, ancho, 260);
        ImGui::SetNextItemWidth(260);
        if (ImGui::Combo("##st", &a, op, 3)) { s_ctl.stick = a == 2 ? -1 : a; ctl_guardar(); }
        renglon("C-stick", NULL, ancho, 260);
        ImGui::SetNextItemWidth(260);
        if (ImGui::Combo("##cs", &c, op, 3)) { s_ctl.cstick = c == 2 ? -1 : c; ctl_guardar(); }
        fin_panel(ancho);
    }
    panel("Sensitivity", "How far a stick or trigger must move before it counts.", ancho);
    deslizador("##dz1", "Stick dead zone", "%d %%", &s_ctl.dz_stick, 0, 50, ancho);
    deslizador("##dz2", "C-stick dead zone", "%d %%", &s_ctl.dz_c, 0, 50, ancho);
    deslizador("##gat", "Trigger point", "L and R count as pressed at %d %% of their travel", &s_ctl.gatillo, 10, 100, ancho);
    fin_panel(ancho);
    panel("Rumble", "How strongly the controller vibrates.", ancho);
    deslizador("##vib", "Rumble strength", "%d %%", &s_ctl.vibracion, 0, 100, ancho - 110);
    ImGui::SameLine();
    if (ImGui::Button("Test") && s_pad) SDL_GameControllerRumble(s_pad, (Uint16)(0xA000 * s_ctl.vibracion / 100), (Uint16)(0xC000 * s_ctl.vibracion / 100), 600);
    fin_panel(ancho);
    if (s_control.tex) {   /* arte/control.jpg: el control de GameCube con lo que hace cada boton */
        panel("On the GameCube controller", "What each button does in the original game (the ones above are mapped the same way).", ancho);
        float w = ancho - 48, h = w * s_control.h / s_control.w;
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddImageRounded((ImTextureID)(intptr_t)s_control.tex, p, ImVec2(p.x + w, p.y + h), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 255), 10);
        ImGui::Dummy(ImVec2(w, h));
        fin_panel(ancho);
    }
}

static void pest_tarjetas(float ancho) {
    encabezado("Memory cards", "Your saved games. Each card is a file; the active one is used by the game.");
    static int sel = -1;
    if (sel < 0 || sel >= (int)s_tarjetas.size()) {
        sel = 0;
        for (size_t i = 0; i < s_tarjetas.size(); i++) if (s_tarjetas[i].archivo == s_cfg.tarjeta) sel = (int)i;
    }
    if (ImGui::Button("Import...")) {
        std::string p = elegir_archivo("Choose a memory card (Dolphin .raw)", "Memory card (*.raw)|*.raw|All files|*.*|");
        if (!p.empty()) {
            size_t sl = p.find_last_of("/\\");
            std::string n = sl == std::string::npos ? p : p.substr(sl + 1);
            if (n.size() < 4 || n.substr(n.size() - 4) != ".raw") n += ".raw";
            if (copiar(p, unir(carpeta_tarjetas(), n))) { avisar("Card imported: " + n); tarjetas_listar(); }
            else avisar("Could not copy the card.");
        }
    }
    ImGui::SameLine();
    if (boton_naranja("New card")) {
        for (char c = 'A'; c <= 'Z'; c++) {
            std::string n = std::string("Card ") + c + ".raw";
            bool hay = false;
            for (auto& t : s_tarjetas) if (t.archivo == n) hay = true;
            if (!hay) { s_cfg.tarjeta = n; cfg_guardar(); tarjetas_listar(); avisar(n + ": created empty the next time you play."); break; }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Open folder")) abrir_carpeta(carpeta_tarjetas());
    ImGui::Dummy(ImVec2(0, 6));
    float izq = 330;
    ImGui::BeginGroup();
    for (size_t i = 0; i < s_tarjetas.size(); i++) {
        Tarjeta& t = s_tarjetas[i];
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::PushID((int)i);
        if (ImGui::InvisibleButton("t", ImVec2(izq, 74))) sel = (int)i;
        bool sobre = ImGui::IsItemHovered();
        ImGui::PopID();
        dl->AddRectFilled(p, ImVec2(p.x + izq, p.y + 74), (int)i == sel ? IM_COL32(44, 42, 50, 255) : sobre ? IM_COL32(36, 35, 41, 255) : C_PANEL, 12);
        std::string n = t.archivo.substr(0, t.archivo.size() - 4);
        dl->AddText(s_f_medio, s_f_medio->LegacySize, ImVec2(p.x + 18, p.y + 12), IM_COL32(255, 255, 255, 255), n.c_str());
        char s[96];
        if (t.tam < 0) snprintf(s, sizeof s, "Empty (created when you play)");
        else snprintf(s, sizeof s, "%d saved game%s", (int)t.partidas.size(), t.partidas.size() == 1 ? "" : "s");
        dl->AddText(ImVec2(p.x + 18, p.y + 42), C_TENUE, s);
        if (t.archivo == s_cfg.tarjeta) {
            ImVec2 ts = ImGui::CalcTextSize("Active");
            ImVec2 q(p.x + izq - ts.x - 34, p.y + 14);
            dl->AddRectFilled(q, ImVec2(q.x + ts.x + 20, q.y + ts.y + 8), IM_COL32(92, 20, 24, 255), 20);
            dl->AddText(ImVec2(q.x + 10, q.y + 4), IM_COL32(255, 150, 150, 255), "Active");
        }
        ImGui::Dummy(ImVec2(0, 2));
    }
    ImGui::EndGroup();
    ImGui::SameLine(0, 20);
    if (sel >= 0 && sel < (int)s_tarjetas.size()) {
        Tarjeta& t = s_tarjetas[sel];
        float w = ancho - izq - 20;
        ImGui::BeginGroup();
        panel(t.archivo.substr(0, t.archivo.size() - 4).c_str(), NULL, w);
        std::string ruta = unir(carpeta_tarjetas(), t.archivo);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        ImGui::TextWrapped("%s", ruta.c_str());
        if (t.tam >= 0) ImGui::Text("Size %.1f MB", t.tam / 1048576.0);
        ImGui::PopStyleColor();
        if (t.archivo != s_cfg.tarjeta) { if (boton_naranja("Use this card")) { s_cfg.tarjeta = t.archivo; cfg_guardar(); } }
        ImGui::Dummy(ImVec2(0, 6));
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        ImGui::TextUnformatted("PARTIDAS GUARDADAS");
        ImGui::PopStyleColor();
        if (t.partidas.empty()) ImGui::TextDisabled("None yet.");
        for (auto& pa : t.partidas) {
            char f[64];
            struct tm* tmv = gmtime(&pa.fecha);
            if (tmv) strftime(f, sizeof f, "%d/%m/%Y %H:%M", tmv); else f[0] = 0;
            ImGui::Text("%s", pa.archivo.c_str());
            ImGui::SameLine(w * 0.55f);
            ImGui::TextDisabled("%s   %s   %d blocks", pa.juego.c_str(), f, pa.bloques);
        }
        fin_panel(w);
        ImGui::EndGroup();
    }
}

static void pest_ajustes(float ancho) {
    encabezado("System", "Disc image, arguments and where the launcher keeps its files. Video, audio and game settings are in Options.");
    panel("Extra arguments", "Added when the game starts (for example --escala 3). They override what is set in Options.", ancho);
    bool g = false;
    {
        char b[512];
        snprintf(b, sizeof b, "%s", s_cfg.args.c_str());
        ImGui::SetNextItemWidth(ancho - 48);
        if (ImGui::InputText("##args", b, sizeof b)) { s_cfg.args = b; g = true; }
    }
    fin_panel(ancho);
    if (g) cfg_guardar();

    panel("Launcher music", "Put a musica.ogg, musica.mp3, musica.flac or musica.wav file in the launcher's arte folder (Folders, below) and it loops here. It goes quiet while you play.", ancho);
    if (mus_hay()) ImGui::TextDisabled("Playing: %s", s_mus.ruta.c_str());
    else ImGui::TextDisabled("No music file found (it is looked for when the launcher opens).");
    renglon("Music", NULL, ancho, 56);
    if (interruptor("mus", &s_cfg.musica)) cfg_guardar();
    renglon("Volume", NULL, ancho, 300);
    ImGui::SetNextItemWidth(300);
    if (ImGui::SliderInt("##musvol", &s_cfg.musica_vol, 0, 100, "%d%%")) cfg_guardar();
    fin_panel(ancho);

    panel("Disc image", "The launcher never includes the game files: you need your own copy (uncompressed .iso or .gcm).", ancho);
    if (s_disco_img.tex && s_disco.ok) {   /* el disco girando despacio */
        ImVec2 p = ImGui::GetCursorScreenPos();
        float r = 46, ang = SDL_GetTicks() / 1000.0f * 0.6f;
        ImVec2 c(p.x + r, p.y + r), q[4];
        for (int k = 0; k < 4; k++) { float a = ang + k * 1.5707963f + 0.785398f; q[k] = ImVec2(c.x + SDL_cosf(a) * r * 1.41421f, c.y + SDL_sinf(a) * r * 1.41421f); }
        ImGui::GetWindowDrawList()->AddImageQuad((ImTextureID)(intptr_t)s_disco_img.tex, q[0], q[1], q[2], q[3], ImVec2(1, 1), ImVec2(0, 1), ImVec2(0, 0), ImVec2(1, 0));
        ImGui::Dummy(ImVec2(2 * r, 2 * r));
        ImGui::SameLine(0, 20);
        ImGui::BeginGroup();
        ImGui::Dummy(ImVec2(0, 14));
    }
    if (s_disco.ok) {
        ImGui::PushFont(s_f_medio); ImGui::TextUnformatted(s_disco.nombre.c_str()); ImGui::PopFont();
        ImGui::SameLine(); ImGui::TextDisabled("  %s   v1.%d", s_disco.id, s_disco.version);
    } else if (!s_cfg.iso.empty()) ImGui::TextColored(ImVec4(1, 0.6f, 0.5f, 1), "%s", s_disco.error.c_str());
    else ImGui::TextDisabled("You have not chosen one yet.");
    if (!s_cfg.iso.empty()) ImGui::TextDisabled("%s", s_cfg.iso.c_str());
    if (s_disco_img.tex && s_disco.ok) ImGui::EndGroup();
    if (s_disco.ok && !s_disco.error.empty()) ImGui::TextColored(ImVec4(1, 0.7f, 0.4f, 1), "%s", s_disco.error.c_str());
    if (ImGui::Button(s_cfg.iso.empty() ? "Choose disc..." : "Choose another disc...")) elegir_disco();
    if (!s_cfg.iso.empty()) { ImGui::SameLine(); if (ImGui::Button("Remove")) { s_cfg.iso.clear(); cfg_guardar(); disco_leer(""); } }
    fin_panel(ancho);

    panel("Folders", NULL, ancho);
    struct { const char* t; const char* sub; std::string p; } c[] = {
        { "Launcher data", "Options, controls, logs and shader cache", s_datos },
        { "Memory cards", "Your saves, one file per card", carpeta_tarjetas() },
        { "Captures", "F12: capture to send when something goes wrong (.png + .gxc)", unir(s_datos, "capturas") },
        { "Photos", "Shift+F12: plain screenshot (Pictures\\Escuadron2)", carpeta_fotos() },
        { "HD textures", "Copy the GSWE64 folder of a Dolphin texture pack here", unir(s_datos, "texturas_hd") },
        { "Launcher art", "fondo*.jpg, portada, control, disco and musica (.ogg, .mp3, .flac or .wav)", unir(s_datos, "arte") },
        { "Program", "Where the port is installed", s_exe_dir } };
    for (int i = 0; i < 6; i++) {
        renglon(c[i].t, c[i].sub, ancho, 90, 36);
        ImGui::PushID(i);
        if (ImGui::Button("Open", ImVec2(90, 0))) abrir_carpeta(c[i].p);
        ImGui::PopID();
        ImGui::TextDisabled("%s", c[i].p.c_str());
        ImGui::Dummy(ImVec2(0, 4));
    }
    fin_panel(ancho);
    panel("Shader cache", "Shaders already compiled by a previous version: with it the game does not stutter or render slower at first.", ancho);
    {
        std::string cn = unir(s_datos, "escuadron2_shaders.cache");
        long t = tam_archivo(cn);
        if (t > 0) ImGui::Text("%.0f MB of compiled shaders.", t / 1048576.0);
        else ImGui::TextColored(ImVec4(1, 0.7f, 0.5f, 1), "None yet: they are compiled while you play (the first sessions are slower).");
        if (ImGui::Button("Import escuadron2_shaders.cache...")) {
            std::string p = elegir_archivo("Choose escuadron2_shaders.cache from the previous version", "Shader cache (*.cache)|*.cache|All files|*.*|");
            if (!p.empty()) avisar(copiar(p, cn) ? "Shader cache imported." : "Could not copy the cache.");
        }
    }
    fin_panel(ancho);
    ImGui::TextDisabled("Launcher %s. Poppins font (SIL Open Font License). UI built with Dear ImGui (MIT).", VERSION_LAUNCHER);
}

static void pest_registros(float ancho) {
    encabezado("Logs", "If something goes wrong, send the escuadron2_log.txt file (and an F12 capture if something looks wrong).");
    std::string log = unir(s_datos, "escuadron2_log.txt");
    if (ImGui::Button("Open folder")) abrir_carpeta(s_datos);
    ImGui::SameLine();
    if (ImGui::Button("Open F12 captures")) { std::string c = unir(s_datos, "capturas"); MKDIR(c.c_str()); abrir_carpeta(c); }
    ImGui::SameLine();
    static std::string texto;
    static Uint32 leido;
    if (ImGui::Button("Refresh") || SDL_GetTicks() - leido > 3000 || leido == 0) {
        leido = SDL_GetTicks() | 1;
        FILE* f = fopen(log.c_str(), "rb");
        texto.clear();
        if (f) {
            long n; fseek(f, 0, SEEK_END); n = ftell(f);
            long d = n > 60000 ? n - 60000 : 0;
            fseek(f, d, SEEK_SET);
            texto.resize((size_t)(n - d));
            if (fread(&texto[0], 1, texto.size(), f) != texto.size()) texto.clear();
            fclose(f);
        }
    }
    ImGui::Dummy(ImVec2(0, 6));
    panel("Last session", log.c_str(), ancho);
    if (texto.empty()) ImGui::TextDisabled("No log yet: it appears after you play.");
    else {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.06f, 0.055f, 0.07f, 1));
        ImGui::BeginChild("log", ImVec2(ancho - 48, 420), true);
        ImGui::PushFont(s_f_chico);
        ImGui::TextUnformatted(texto.c_str());
        ImGui::PopFont();
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 5) ImGui::SetScrollHereY(1.0f);
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }
    fin_panel(ancho);
}

/* ---- principal ------------------------------------------------------------------------ */

static void carpetas() {
    char* b = SDL_GetBasePath();
    s_exe_dir = b ? b : ".";
    SDL_free(b);
    while (!s_exe_dir.empty() && (s_exe_dir.back() == '/' || s_exe_dir.back() == '\\')) s_exe_dir.pop_back();
    const char* e = getenv("RL_LAUNCHER_DATOS");
    if (e) s_datos = e;
    else {
#ifdef _WIN32
        char p[MAX_PATH];
        std::string appdata = SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, p) == S_OK ? unir(p, "Escuadron2") : unir(s_exe_dir, "datos");
        /* Modo portatil: si la carpeta del launcher se puede escribir (no es Archivos de programa),
         * todo (registro, opciones, tarjetas, cache de shaders) queda ahi mismo, igual que abriendo
         * escuadron2_game.exe directo. La primera vez se traen las cosas que habia en %APPDATA%\Escuadron2. */
        std::string prueba = unir(s_exe_dir, "rl_prueba_escritura.tmp");
        FILE* f = fopen(prueba.c_str(), "wb");
        if (f) {
            fclose(f); remove(prueba.c_str());
            s_datos = s_exe_dir;
            if (!existe(unir(s_datos, "launcher.ini")) && existe(unir(appdata, "launcher.ini"))) {
                const char* sueltos[] = { "launcher.ini", "ajustes.ini", "controles.ini", "escuadron2_shaders.cache", "escuadron2_shaders.usados" };
                for (const char* n : sueltos)
                    if (!existe(unir(s_datos, n)) && existe(unir(appdata, n))) copiar(unir(appdata, n), unir(s_datos, n));
                std::string td = unir(appdata, "tarjetas"), ta = unir(s_datos, "tarjetas");
                WIN32_FIND_DATAA fd;
                HANDLE h = FindFirstFileA(unir(td, "*.raw").c_str(), &fd);
                MKDIR(ta.c_str());
                if (h != INVALID_HANDLE_VALUE) {
                    do if (!existe(unir(ta, fd.cFileName))) copiar(unir(td, fd.cFileName), unir(ta, fd.cFileName));
                    while (FindNextFileA(h, &fd));
                    FindClose(h);
                }
            }
        } else s_datos = appdata;
#else
        const char* h = getenv("HOME");
        s_datos = std::string(h ? h : ".") + "/.local/share/Escuadron2";
        MKDIR((std::string(h ? h : ".") + "/.local").c_str());
        MKDIR((std::string(h ? h : ".") + "/.local/share").c_str());
#endif
    }
    MKDIR(s_datos.c_str());
    MKDIR(carpeta_tarjetas().c_str());
    {   /* nombres de archivos de versiones anteriores */
        const char* viejos[][2] = { { "rogue2_shaders.cache", "escuadron2_shaders.cache" }, { "rogue2_shaders.usados", "escuadron2_shaders.usados" },
                                    { "rogue2.ini", "escuadron2.ini" } };
        for (auto& v : viejos)
            if (!existe(unir(s_datos, v[1])) && existe(unir(s_datos, v[0]))) rename(unir(s_datos, v[0]).c_str(), unir(s_datos, v[1]).c_str());
    }
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    if (getenv("RL_LAUNCHER_CAPTURAS") && !getenv("SDL_VIDEODRIVER")) SDL_setenv("SDL_VIDEODRIVER", "offscreen", 1);
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS5_RUMBLE, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS4_RUMBLE, "1");
    SDL_SetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON, "1");
    SDL_SetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON_SMALL, "1");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_TIMER) != 0) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Escuadron2", SDL_GetError(), NULL);
        return 1;
    }
    carpetas();
    cfg_cargar();
    aj_cargar();
    ctl_cargar();
    tarjetas_listar();
    /* tarjeta vieja del port (memcard_A.raw al lado del juego): traerla la primera vez */
    {
        std::string nueva = unir(carpeta_tarjetas(), s_cfg.tarjeta);
        const char* lugares_t[] = { "", "C:\\escuadron2\\build\\port-win", "C:\\escuadron2", "D:\\escuadron2\\build\\port-win", "D:\\escuadron2" };
        for (const char* l : lugares_t) {
            std::string vieja = unir(*l ? std::string(l) : s_exe_dir, "memcard_A.raw");
            if (!existe(nueva) && existe(vieja) && copiar(vieja, nueva)) { tarjetas_listar(); break; }
        }
        /* y la cache de shaders, para no volver a compilarlos */
        std::string cn = unir(s_datos, "escuadron2_shaders.cache");
        if (!existe(cn)) {   /* buscarla donde la dejaban las versiones sueltas del port */
            const char* lugares[] = { "", "C:\\escuadron2\\build\\port-win", "C:\\escuadron2", "C:\\escuadron2\\build",
                                      "D:\\escuadron2\\build\\port-win", "D:\\escuadron2" };
            for (const char* l : lugares) {
                std::string cv = unir(*l ? std::string(l) : s_exe_dir, "escuadron2_shaders.cache");
                if (existe(cv) && copiar(cv, cn)) break;
            }
        }
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    const char* capt = getenv("RL_LAUNCHER_CAPTURAS");
    SDL_Window* win = SDL_CreateWindow("Escuadron2", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 820,
                                       SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | (capt ? SDL_WINDOW_HIDDEN : 0));
    if (!win) { SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Escuadron2", SDL_GetError(), NULL); return 1; }
    SDL_GLContext gl = SDL_GL_CreateContext(win);
    SDL_GL_MakeCurrent(win, gl);
    SDL_GL_SetSwapInterval(capt ? 0 : 1);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = NULL;
    {
        ImFontConfig fc; fc.FontDataOwnedByAtlas = false;
        s_f_normal = io.Fonts->AddFontFromMemoryTTF((void*)k_poppins_regular, (int)sizeof k_poppins_regular, 17, &fc);
        s_f_chico = io.Fonts->AddFontFromMemoryTTF((void*)k_poppins_regular, (int)sizeof k_poppins_regular, 14, &fc);
        s_f_medio = io.Fonts->AddFontFromMemoryTTF((void*)k_poppins_medium, (int)sizeof k_poppins_medium, 19, &fc);
        s_f_titulo = io.Fonts->AddFontFromMemoryTTF((void*)k_poppins_medium, (int)sizeof k_poppins_medium, 28, &fc);
        s_f_grande = io.Fonts->AddFontFromMemoryTTF((void*)k_poppins_bold, (int)sizeof k_poppins_bold, 46, &fc);
    }
    estilo();
    ImGui_ImplSDL2_InitForOpenGL(win, gl);
    ImGui_ImplOpenGL3_Init("#version 330 core");
    disco_leer(s_cfg.iso);
    arte_cargar();
    mus_iniciar({ unir(s_datos, "arte"), unir(s_exe_dir, "arte") });
    if (!existe(unir(s_datos, "controles.ini"))) ctl_guardar();
    int cuadro = 0;
    bool seguir = true;
    while (seguir) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (capturar(e)) continue;
            ImGui_ImplSDL2_ProcessEvent(&e);
            if (e.type == SDL_QUIT) seguir = false;
            if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_CLOSE) seguir = false;
            if (e.type == SDL_CONTROLLERDEVICEADDED && !s_pad) {
                s_pad = SDL_GameControllerOpen(e.cdevice.which);
                if (s_pad && SDL_GameControllerHasLED(s_pad)) SDL_GameControllerSetLED(s_pad, 210, 30, 30);
            }
            if (e.type == SDL_CONTROLLERDEVICEREMOVED && s_pad && e.cdevice.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(s_pad))) {
                SDL_GameControllerClose(s_pad); s_pad = NULL;
                for (int k = 0; k < SDL_NumJoysticks() && !s_pad; k++) if (SDL_IsGameController(k)) s_pad = SDL_GameControllerOpen(k);
            }
            if (e.type == SDL_DROPFILE) {   /* soltar un .iso sobre la ventana */
                std::string p = e.drop.file;
                SDL_free(e.drop.file);
                s_cfg.iso = p; cfg_guardar(); disco_leer(p);
            }
        }
        revisar_juego();
        mus_actualizar(s_cfg.musica && !s_jugando && !capt, s_cfg.musica_vol / 100.0f);
        if (capt) s_pest = cuadro / 3;
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        int w, h;
        SDL_GetWindowSize(win, &w, &h);
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2((float)w, (float)h));
        ImGui::Begin("principal", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings);
        float t = SDL_GetTicks() / 1000.0f;
        if (s_pest == P_JUGAR) {
            pest_jugar(ImVec2(0, 0), ImVec2((float)w, (float)h), t);
            barra((float)w);
        } else if (s_pest == P_OPCIONES) {
            pest_opciones(ImVec2(0, 0), ImVec2((float)w, (float)h), t);
            barra((float)w);
        } else {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2((float)w, (float)h), IM_COL32(8, 12, 28, 255), IM_COL32(10, 13, 30, 255), IM_COL32(3, 4, 11, 255), IM_COL32(4, 5, 12, 255));
            barra((float)w);
            float ancho = std::min((float)w - 160, 1240.0f);
            float x0 = ((float)w - ancho) * 0.5f;
            ImGui::SetCursorPos(ImVec2(x0, 92));
            ImGui::BeginChild("contenido", ImVec2(ancho + 20, (float)h - 100), false);
            ImGui::Dummy(ImVec2(0, 10));
            if (s_pest == P_CONTROLES) pest_controles(ancho);
            else if (s_pest == P_TARJETAS) pest_tarjetas(ancho);
            else if (s_pest == P_AJUSTES) pest_ajustes(ancho);
            else pest_registros(ancho);
            ImGui::Dummy(ImVec2(0, 30));
            ImGui::EndChild();
        }
        if (!s_aviso.empty() && SDL_GetTicks() - s_aviso_t < 5000) {
            ImDrawList* dl = ImGui::GetForegroundDrawList();
            ImVec2 ts = ImGui::CalcTextSize(s_aviso.c_str());
            ImVec2 p(((float)w - ts.x) * 0.5f - 18, (float)h - 70);
            dl->AddRectFilled(p, ImVec2(p.x + ts.x + 36, p.y + ts.y + 22), IM_COL32(44, 42, 50, 245), 12);
            dl->AddText(ImVec2(p.x + 18, p.y + 11), IM_COL32(255, 255, 255, 255), s_aviso.c_str());
        }
        ImGui::End();
        ImGui::Render();
        int fw, fh;
        SDL_GL_GetDrawableSize(win, &fw, &fh);
        glViewport(0, 0, fw, fh);
        glClearColor(0.07f, 0.07f, 0.09f, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        if (capt && cuadro % 3 == 2) {
            char p[512];
            snprintf(p, sizeof p, "%s/pestana_%d.bmp", capt, s_pest);
            guardar_bmp(p, fw, fh);
            if (s_pest == NP - 1) seguir = false;
        }
        SDL_GL_SwapWindow(win);
        cuadro++;
        if (s_jugando && !capt) SDL_Delay(50);   /* mientras se juega, el launcher casi no usa la CPU */
    }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(gl);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
