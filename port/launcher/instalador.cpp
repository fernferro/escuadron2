/*
 * Instalador de Escuadron2 (port a PC), con la misma cara que el launcher.
 *
 * Es un solo .exe: el programa (este codigo) y, pegados al final, los archivos a instalar
 * comprimidos con zlib (los agrega port/tools/empaquetar_instalador.py). Al final de todo
 * hay un "remolque" de 32 bytes:  "RLPAQ001" | indice (u64) | tamano del programa (u64) | total (u64)
 * y el indice: cantidad (u32) y por archivo: largo del nombre (u16), nombre, posicion, comprimido, tamano (u64).
 *
 * Instala para todos (Archivos de programa, pide permiso de administrador) o solo para el
 * usuario (%LOCALAPPDATA%\Programs), crea accesos directos y la entrada de "Agregar o quitar
 * programas". Se copia a si mismo (sin los archivos) como Desinstalar.exe en la carpeta.
 * Nunca trae archivos del juego: el launcher le pide al usuario su propia imagen del disco.
 *
 * Pruebas: RL_INSTALADOR_CAPTURAS=CARPETA guarda una imagen de cada paso y sale;
 *          RL_INSTALADOR_AUTO=CARPETA instala ahi sin preguntar (para probar la extraccion).
 */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <string>
#include <vector>
#include <algorithm>
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"
#include <SDL_opengl.h>
#include "zlib.h"

#ifdef _WIN32
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0601   /* Windows 7 o mas nuevo */
#undef WINVER
#define WINVER 0x0601
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <objbase.h>
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#include <unistd.h>
#define MKDIR(p) mkdir(p, 0755)
#endif

#include "fuentes.inc"
#include "estilo.h"

#define VERSION_PORT "0.1.0"

/* idioma: espanol o ingles (por defecto el de Windows; se cambia arriba a la derecha) */
static int s_en;
static const char* T(const char* es, const char* en) { return s_en ? en : es; }
static void idioma_del_sistema() {
    s_en = 1;   /* siempre en ingles al abrir (el boton de idioma deja pasar a castellano; --es tambien) */
}
#define CLAVE_DESINST "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Escuadron2PC"

/* ---- archivos del paquete ------------------------------------------------------------- */

struct Entrada { std::string nombre; uint64_t pos, comp, tam; };
static std::vector<Entrada> s_ents;
static uint64_t s_tam_programa, s_total;
static std::string s_yo;   /* este .exe */

static uint64_t le64(const uint8_t* p) { uint64_t v = 0; for (int i = 7; i >= 0; i--) v = (v << 8) | p[i]; return v; }
/* posiciones de 64 bits: con las texturas el instalador pasa los 2 GB */
static int buscar(FILE* f, int64_t pos, int desde) {
#ifdef _WIN32
    return _fseeki64(f, pos, desde);
#else
    return fseeko(f, (off_t)pos, desde);
#endif
}
static int64_t posicion(FILE* f) {
#ifdef _WIN32
    return _ftelli64(f);
#else
    return (int64_t)ftello(f);
#endif
}
/* partes opcionales del paquete: se reconocen por la carpeta */
enum { C_BASE, C_TEXTURAS, C_MUSICA };
static int componente(const std::string& n) {
    if (n.rfind("texturas_hd/", 0) == 0) return C_TEXTURAS;
    if (n.rfind("arte/musica.", 0) == 0) return C_MUSICA;
    return C_BASE;
}
static bool s_con_texturas = true, s_con_musica = true;

static std::string ruta_propia() {
#ifdef _WIN32
    wchar_t w[MAX_PATH]; char u[MAX_PATH * 3];
    GetModuleFileNameW(NULL, w, MAX_PATH);
    WideCharToMultiByte(CP_UTF8, 0, w, -1, u, sizeof u, NULL, NULL);
    return u;
#else
    char b[4096]; ssize_t n = readlink("/proc/self/exe", b, sizeof b - 1);
    if (n < 0) return "";
    b[n] = 0; return b;
#endif
}

#ifdef _WIN32
static std::wstring W(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, NULL, 0);
    std::wstring w(n ? n - 1 : 0, 0);
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    return w;
}
static std::string U(const wchar_t* w) {
    char b[MAX_PATH * 3]; WideCharToMultiByte(CP_UTF8, 0, w, -1, b, sizeof b, NULL, NULL); return b;
}
static FILE* abrir(const std::string& p, const char* m) { return _wfopen(W(p).c_str(), W(m).c_str()); }
#else
static FILE* abrir(const std::string& p, const char* m) { return fopen(p.c_str(), m); }
#endif

static bool leer_paquete() {
    FILE* f = abrir(s_yo, "rb");
    uint8_t t[32];
    if (!f) return false;
    if (buscar(f, -32, SEEK_END) || fread(t, 1, 32, f) != 32 || memcmp(t, "RLPAQ001", 8)) { fclose(f); return false; }
    uint64_t idx = le64(t + 8);
    s_tam_programa = le64(t + 16);
    s_total = le64(t + 24);
    buscar(f, 0, SEEK_END);
    int64_t fin = posicion(f);
    std::vector<uint8_t> d((size_t)(fin - 32 - (int64_t)idx));
    buscar(f, (int64_t)idx, SEEK_SET);
    if (fread(d.data(), 1, d.size(), f) != d.size()) { fclose(f); return false; }
    fclose(f);
    size_t k = 4;
    uint32_t n = d[0] | (d[1] << 8) | (d[2] << 16) | ((uint32_t)d[3] << 24);
    for (uint32_t i = 0; i < n && k + 2 <= d.size(); i++) {
        Entrada e;
        uint16_t l = (uint16_t)(d[k] | (d[k + 1] << 8)); k += 2;
        e.nombre.assign((const char*)&d[k], l); k += l;
        e.pos = le64(&d[k]); e.comp = le64(&d[k + 8]); e.tam = le64(&d[k + 16]); k += 24;
        s_ents.push_back(e);
    }
    return !s_ents.empty();
}

/* ---- utilidades de archivos ------------------------------------------------------------ */

static std::string unir(const std::string& a, const std::string& b) {
#ifdef _WIN32
    std::string r = a + "\\" + b;
    for (auto& c : r) if (c == '/') c = '\\';
    return r;
#else
    return a + "/" + b;
#endif
}

static void crear_carpetas(const std::string& p) {
    for (size_t i = 1; i <= p.size(); i++) {
        if (i == p.size() || p[i] == '/' || p[i] == '\\') {
            std::string s = p.substr(0, i);
            if (s.size() == 2 && s[1] == ':') continue;
#ifdef _WIN32
            CreateDirectoryW(W(s).c_str(), NULL);
#else
            MKDIR(s.c_str());
#endif
        }
    }
}

static bool existe(const std::string& p) { FILE* f = abrir(p, "rb"); if (f) fclose(f); return f != NULL; }

static bool elegido(const Entrada& e) {
    int c = componente(e.nombre);
    return c == C_BASE || (c == C_TEXTURAS && s_con_texturas) || (c == C_MUSICA && s_con_musica);
}
static uint64_t tam_componente(int c) { uint64_t t = 0; for (auto& e : s_ents) if (componente(e.nombre) == c) t += e.tam; return t; }
static uint64_t tam_elegido() { uint64_t t = 0; for (auto& e : s_ents) if (elegido(e)) t += e.tam; return t; }

/* ---- opciones -------------------------------------------------------------------------- */

static bool s_todos = false, s_escritorio = true, s_menu = true, s_abrir = true;
static std::string s_carpeta;
static bool s_carpeta_tocada;

static std::string carpeta_defecto(bool todos) {
#ifdef _WIN32
    PWSTR p = NULL;
    std::string r;
    if (SUCCEEDED(SHGetKnownFolderPath(todos ? FOLDERID_ProgramFiles : FOLDERID_UserProgramFiles, KF_FLAG_CREATE, NULL, &p))) r = U(p);
    CoTaskMemFree(p);
    if (r.empty()) r = todos ? "C:\\Program Files" : U(_wgetenv(L"LOCALAPPDATA")) + "\\Programs";
    return r + "\\Escuadron2";
#else
    const char* h = getenv("HOME");
    return std::string(h ? h : ".") + (todos ? "/Escuadron2-todos" : "/Escuadron2");
#endif
}

static bool soy_admin() {
#ifdef _WIN32
    return IsUserAnAdmin() != FALSE;
#else
    return geteuid() == 0;
#endif
}

static std::string elegir_carpeta(const std::string& inicial) {
#ifdef _WIN32
    std::string r;
    IFileOpenDialog* d = NULL;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&d)))) {
        DWORD o; d->GetOptions(&o);
        d->SetOptions(o | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
        d->SetTitle(s_en ? L"Choose where to install Escuadron2" : L"Elegi donde instalar Escuadron2");
        (void)inicial;
        if (SUCCEEDED(d->Show(NULL))) {
            IShellItem* it;
            if (SUCCEEDED(d->GetResult(&it))) {
                PWSTR p;
                if (SUCCEEDED(it->GetDisplayName(SIGDN_FILESYSPATH, &p))) { r = U(p); CoTaskMemFree(p); }
                it->Release();
            }
        }
        d->Release();
    }
    if (!r.empty()) {   /* si eligio una carpeta cualquiera, instalar en una subcarpeta propia */
        size_t sl = r.find_last_of('\\');
        std::string ult = sl == std::string::npos ? r : r.substr(sl + 1);
        if (ult != "Escuadron2") r = unir(r, "Escuadron2");
    }
    return r;
#else
    (void)inicial;
    return "";
#endif
}

/* ---- instalacion (en otro hilo) --------------------------------------------------------- */

static SDL_atomic_t s_hecho;      /* bytes ya escritos */
static SDL_atomic_t s_estado;     /* 0 nada, 1 trabajando, 2 listo, 3 error */
static std::string s_error, s_actual;
static SDL_mutex* s_mx;

static void poner_actual(const std::string& s) { SDL_LockMutex(s_mx); s_actual = s; SDL_UnlockMutex(s_mx); }
static void fallar(const std::string& s) { SDL_LockMutex(s_mx); s_error = s; SDL_UnlockMutex(s_mx); SDL_AtomicSet(&s_estado, 3); }

#ifdef _WIN32
static void acceso_directo(const std::string& lnk, const std::string& destino, const std::string& carpeta, const char* desc) {
    IShellLinkW* sl = NULL;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void**)&sl))) return;
    sl->SetPath(W(destino).c_str());
    sl->SetWorkingDirectory(W(carpeta).c_str());
    sl->SetDescription(W(desc).c_str());
    sl->SetIconLocation(W(destino).c_str(), 0);
    IPersistFile* pf = NULL;
    if (SUCCEEDED(sl->QueryInterface(IID_IPersistFile, (void**)&pf))) { pf->Save(W(lnk).c_str(), TRUE); pf->Release(); }
    sl->Release();
}
static std::string carpeta_especial(int csidl) {
    wchar_t p[MAX_PATH];
    if (SHGetFolderPathW(NULL, csidl, NULL, 0, p) != S_OK) return "";
    return U(p);
}
static void reg_txt(HKEY k, const char* n, const std::string& v) {
    std::wstring w = W(v);
    RegSetValueExW(k, W(n).c_str(), 0, REG_SZ, (const BYTE*)w.c_str(), (DWORD)((w.size() + 1) * 2));
}
static void reg_num(HKEY k, const char* n, DWORD v) { RegSetValueExW(k, W(n).c_str(), 0, REG_DWORD, (const BYTE*)&v, 4); }
#endif

static int hilo_instalar(void*) {
#ifdef _WIN32
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
#endif
    std::string dir = s_carpeta;
    std::vector<std::string> hechos;
    crear_carpetas(dir);
    FILE* in = abrir(s_yo, "rb");
    if (!in) { fallar(T("No se pudo leer el instalador.", "Could not read the installer.")); return 0; }
    std::vector<uint8_t> bi(1 << 16), bo(1 << 18);
    uint64_t total = 0;
    for (auto& e : s_ents) {
        if (!elegido(e)) continue;
        std::string dst = unir(dir, e.nombre);
        size_t sl = dst.find_last_of("/\\");
        if (sl != std::string::npos) crear_carpetas(dst.substr(0, sl));
        poner_actual(e.nombre);
        FILE* out = abrir(dst + ".nuevo", "wb");
        if (!out) { fclose(in); fallar(T("No se pudo escribir ", "Could not write ") + dst + T(". Si el juego o el launcher estan abiertos, cerralos y proba de nuevo.", ". If the game or the launcher are open, close them and try again.")); return 0; }
        z_stream z; memset(&z, 0, sizeof z);
        inflateInit(&z);
        buscar(in, (int64_t)e.pos, SEEK_SET);
        uint64_t queda = e.comp;
        int r = Z_OK;
        if (e.comp == e.tam) {   /* guardado tal cual (imagenes y musica ya comprimidas) */
            while (queda) {
                size_t n = (size_t)std::min<uint64_t>(queda, bo.size());
                if (fread(bo.data(), 1, n, in) != n || fwrite(bo.data(), 1, n, out) != n) break;
                queda -= n; total += n;
                SDL_AtomicSet(&s_hecho, (int)(total >> 10));
            }
            r = queda ? Z_ERRNO : Z_STREAM_END;
        }
        while (r != Z_STREAM_END && r != Z_ERRNO) {
            if (z.avail_in == 0 && queda) {
                size_t n = (size_t)std::min<uint64_t>(queda, bi.size());
                if (fread(bi.data(), 1, n, in) != n) break;
                queda -= n; z.next_in = bi.data(); z.avail_in = (uInt)n;
            }
            z.next_out = bo.data(); z.avail_out = (uInt)bo.size();
            r = inflate(&z, Z_NO_FLUSH);
            if (r != Z_OK && r != Z_STREAM_END) break;
            size_t sal = bo.size() - z.avail_out;
            if (sal && fwrite(bo.data(), 1, sal, out) != sal) { r = Z_ERRNO; break; }
            total += sal;
            SDL_AtomicSet(&s_hecho, (int)(total >> 10));
            if (z.avail_in == 0 && !queda && r != Z_STREAM_END && sal == 0) break;
        }
        inflateEnd(&z);
        bool ok = fclose(out) == 0 && r == Z_STREAM_END;
        if (!ok) { fclose(in); fallar(T("El instalador esta danado o el disco esta lleno (", "The installer is damaged or the disk is full (") + e.nombre + ")."); return 0; }
#ifdef _WIN32
        if (!MoveFileExW(W(dst + ".nuevo").c_str(), W(dst).c_str(), MOVEFILE_REPLACE_EXISTING)) {
            fclose(in); fallar(T("No se pudo reemplazar ", "Could not replace ") + dst + T(". Cerra el juego y el launcher y proba de nuevo.", ". Close the game and the launcher and try again.")); return 0;
        }
#else
        rename((dst + ".nuevo").c_str(), dst.c_str());
        if (e.nombre.find(".exe") == std::string::npos && e.nombre.find('.') == std::string::npos) chmod(dst.c_str(), 0755);
#endif
        hechos.push_back(e.nombre);
    }
    fclose(in);
    poner_actual(T("Desinstalador y accesos directos", "Uninstaller and shortcuts"));
    /* desinstalador: este mismo programa sin los archivos pegados */
    {
        FILE* a = abrir(s_yo, "rb");
        FILE* b = abrir(unir(dir, "Desinstalar.exe"), "wb");
        if (a && b) {
            uint64_t q = s_tam_programa;
            while (q) { size_t n = (size_t)std::min<uint64_t>(q, bi.size()); if (fread(bi.data(), 1, n, a) != n) break; fwrite(bi.data(), 1, n, b); q -= n; }
        }
        if (a) fclose(a);
        if (b) fclose(b);
        hechos.push_back("Desinstalar.exe");
    }
    std::vector<std::string> accesos;
#ifdef _WIN32
    {
        std::string exe = unir(dir, "Escuadron2.exe");
        if (s_escritorio) {
            std::string d = carpeta_especial(s_todos ? CSIDL_COMMON_DESKTOPDIRECTORY : CSIDL_DESKTOPDIRECTORY);
            if (!d.empty()) { std::string l = unir(d, "Escuadron2.lnk"); acceso_directo(l, exe, dir, T("Escuadron2 (port a PC)", "Escuadron2 (PC port)")); accesos.push_back(l); }
        }
        if (s_menu) {
            std::string d = carpeta_especial(s_todos ? CSIDL_COMMON_PROGRAMS : CSIDL_PROGRAMS);
            if (!d.empty()) {
                std::string l = unir(d, "Escuadron2.lnk");
                acceso_directo(l, exe, dir, T("Escuadron2 (port a PC)", "Escuadron2 (PC port)"));
                accesos.push_back(l);
            }
        }
        HKEY k;
        if (RegCreateKeyExW(s_todos ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER, W(CLAVE_DESINST).c_str(), 0, NULL, 0, KEY_WRITE, NULL, &k, NULL) == ERROR_SUCCESS) {
            reg_txt(k, "DisplayName", "Escuadron2 (port a PC)");
            reg_txt(k, "DisplayVersion", VERSION_PORT);
            reg_txt(k, "Publisher", "Proyecto Escuadron2");
            reg_txt(k, "DisplayIcon", exe + ",0");
            reg_txt(k, "InstallLocation", dir);
            reg_txt(k, "UninstallString", "\"" + unir(dir, "Desinstalar.exe") + "\"");
            reg_num(k, "EstimatedSize", (DWORD)(tam_elegido() >> 10));
            reg_num(k, "NoModify", 1);
            reg_num(k, "NoRepair", 1);
            RegCloseKey(k);
        }
    }
#endif
    /* lista de lo instalado, para el desinstalador */
    {
        FILE* m = abrir(unir(dir, "instalados.txt"), "wb");
        if (m) {
            fprintf(m, "# Escuadron2: lo que instalo el instalador (lo usa Desinstalar.exe)\nmodo = %s\n", s_todos ? "todos" : "usuario");
            for (auto& h : hechos) fprintf(m, "archivo = %s\n", h.c_str());
            for (auto& a : accesos) fprintf(m, "acceso = %s\n", a.c_str());
            fprintf(m, "archivo = instalados.txt\n");
            fclose(m);
        }
    }
    SDL_AtomicSet(&s_estado, 2);
    return 0;
}

/* ---- desinstalacion ------------------------------------------------------------------- */

static bool s_desinstalar, s_borrar_datos;
static std::string s_dir_desinst;
static std::vector<std::string> s_arch_desinst, s_acc_desinst;
static bool s_desinst_todos;

static bool leer_instalados(const std::string& dir) {
    FILE* f = abrir(unir(dir, "instalados.txt"), "rb");
    char l[2048];
    if (!f) return false;
    s_arch_desinst.clear(); s_acc_desinst.clear();
    while (fgets(l, sizeof l, f)) {
        std::string s = l;
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        if (s.rfind("modo = ", 0) == 0) s_desinst_todos = s.substr(7) == "todos";
        else if (s.rfind("archivo = ", 0) == 0) s_arch_desinst.push_back(s.substr(10));
        else if (s.rfind("acceso = ", 0) == 0) s_acc_desinst.push_back(s.substr(9));
    }
    fclose(f);
    return true;
}

static std::string carpeta_datos() {
#ifdef _WIN32
    return U(_wgetenv(L"APPDATA")) + "\\Escuadron2";
#else
    const char* h = getenv("HOME");
    return std::string(h ? h : ".") + "/.local/share/Escuadron2";
#endif
}

static void borrar_arbol(const std::string& d) {
#ifdef _WIN32
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(W(unir(d, "*")).c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            std::string n = U(fd.cFileName);
            if (n == "." || n == "..") continue;
            std::string p = unir(d, n);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) borrar_arbol(p);
            else DeleteFileW(W(p).c_str());
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(W(d).c_str());
#else
    std::string c = "rm -rf '" + d + "'";
    if (system(c.c_str())) {}
#endif
}

static int hilo_desinstalar(void*) {
    int i = 0;
    for (auto& a : s_arch_desinst) {
        std::string p = unir(s_dir_desinst, a);
        poner_actual(a);
#ifdef _WIN32
        if (!DeleteFileW(W(p).c_str()) && GetFileAttributesW(W(p).c_str()) != INVALID_FILE_ATTRIBUTES)
            MoveFileExW(W(p).c_str(), NULL, MOVEFILE_DELAY_UNTIL_REBOOT);
#else
        remove(p.c_str());
#endif
        SDL_AtomicSet(&s_hecho, ++i);
    }
    for (auto& a : s_acc_desinst) {
#ifdef _WIN32
        DeleteFileW(W(a).c_str());
#else
        remove(a.c_str());
#endif
    }
    /* subcarpetas que quedaron vacias (licencias/...) y la carpeta misma, si quedo vacia */
    for (int pasada = 0; pasada < 8; pasada++)   /* de las mas hondas para arriba (texturas_hd/GSWE64/...) */
        for (auto& a : s_arch_desinst) {
            std::string c = a;
            for (int k = 0; k <= pasada; k++) { size_t sl = c.find_last_of("/\\"); if (sl == std::string::npos) { c.clear(); break; } c = c.substr(0, sl); }
            if (c.empty()) continue;
#ifdef _WIN32
            RemoveDirectoryW(W(unir(s_dir_desinst, c)).c_str());
#else
            rmdir(unir(s_dir_desinst, c).c_str());
#endif
        }
#ifdef _WIN32
    if (!RemoveDirectoryW(W(s_dir_desinst).c_str())) MoveFileExW(W(s_dir_desinst).c_str(), NULL, MOVEFILE_DELAY_UNTIL_REBOOT);
    RegDeleteKeyW(s_desinst_todos ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER, W(CLAVE_DESINST).c_str());
#else
    rmdir(s_dir_desinst.c_str());
#endif
    if (s_borrar_datos) borrar_arbol(carpeta_datos());
    SDL_AtomicSet(&s_estado, 2);
    return 0;
}

/* ---- pedir permiso de administrador ----------------------------------------------------- */

#ifdef _WIN32
static bool relanzar_como_admin(const std::string& args) {
    SHELLEXECUTEINFOW si;
    std::wstring exe = W(s_yo), a = W(args);
    memset(&si, 0, sizeof si);
    si.cbSize = sizeof si;
    si.lpVerb = L"runas";
    si.lpFile = exe.c_str();
    si.lpParameters = a.c_str();
    si.nShow = SW_SHOWNORMAL;
    return ShellExecuteExW(&si) != FALSE;
}
#endif

/* ---- interfaz ----------------------------------------------------------------------------- */

enum { E_BIENVENIDA, E_OPCIONES, E_TRABAJANDO, E_LISTO, E_ERROR, E_SIN_PAQUETE, E_DESINST, NE };
static int s_paso = E_BIENVENIDA;
static Uint32 s_t0;

/* fondo del instalador: arte/instalador.jpg dentro del paquete (si se armo con arte) */
static Imagen s_fondo;
static void cargar_fondo() {
    for (auto& e : s_ents) {
        if (e.nombre != "arte/instalador.jpg" || e.tam > (64u << 20)) continue;
        FILE* f = abrir(s_yo, "rb");
        if (!f) return;
        std::vector<uint8_t> c((size_t)e.comp), d((size_t)e.tam);
        buscar(f, (int64_t)e.pos, SEEK_SET);
        bool ok = fread(c.data(), 1, c.size(), f) == c.size();
        fclose(f);
        uLongf n = (uLongf)d.size();
        if (ok && e.comp == e.tam) s_fondo = imagen_de_memoria(c.data(), (int)c.size());
        else if (ok && uncompress(d.data(), &n, c.data(), (uLong)c.size()) == Z_OK) s_fondo = imagen_de_memoria(d.data(), (int)n);
        return;
    }
}

static void titulo_grande(ImDrawList* dl, float x, float y) {
    dibujar_nave(dl, ImVec2(x + 18, y + 24), 24);
    dl->AddText(s_f_grande, s_f_grande->LegacySize, ImVec2(x + 50, y - 4), IM_COL32(255, 255, 255, 255), "ESCUADRON2");
    dl->AddText(s_f_normal, s_f_normal->LegacySize, ImVec2(x + 52, y + 42), C_ACENTO, T("GAMECUBE  |  PORT A PC", "GAMECUBE  |  PC PORT"));
}

static std::string tam_txt(uint64_t b) {
    char s[32];
    if (b >= (1ull << 30)) snprintf(s, sizeof s, "%.1f GB", b / 1073741824.0);
    else snprintf(s, sizeof s, "%.0f MB", b / 1048576.0);
    return s;
}

static void empezar() {
    SDL_AtomicSet(&s_hecho, 0);
    SDL_AtomicSet(&s_estado, 1);
    s_t0 = SDL_GetTicks();
    s_paso = E_TRABAJANDO;
    SDL_DetachThread(SDL_CreateThread(s_desinstalar ? hilo_desinstalar : hilo_instalar, "trabajo", NULL));
}

static void texto_tenue(const char* t, float ancho) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ancho);
    ImGui::TextUnformatted(t);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

static bool s_salir;

static void interfaz(int w, int h) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float t = SDL_GetTicks() / 1000.0f;
    if (s_fondo.tex) {
        float z = 1.0f + 0.02f * SDL_sinf(t * 0.1f);
        dibujar_cubrir(dl, s_fondo, ImVec2(-w * (z - 1) / 2, -h * (z - 1) / 2), ImVec2(w * (z + 1) / 2, h * (z + 1) / 2));
        velo(dl, ImVec2(0, 0), ImVec2((float)w, (float)h));
        dl->AddRectFilled(ImVec2(0, 0), ImVec2((float)w, (float)h), IM_COL32(8, 7, 12, 70));
    } else fondo_estrellas(dl, ImVec2(0, 0), ImVec2((float)w, (float)h), t);
    float x0 = 56, ancho = (float)w - 112;
    titulo_grande(dl, x0, 48);
    ImGui::SetCursorPos(ImVec2(x0, 140));
    ImGui::BeginChild("contenido", ImVec2(ancho + 12, (float)h - 140 - 130), false);
    ancho -= 12;
    ImGui::PushTextWrapPos(ancho);
    if (s_paso == E_SIN_PAQUETE) {
        ImGui::PushFont(s_f_titulo); ImGui::TextUnformatted(T("Este instalador esta incompleto", "This installer is incomplete")); ImGui::PopFont();
        texto_tenue(T("No tiene los archivos del port pegados al final (se agregan con port/tools/empaquetar_instalador.py). Descargalo de nuevo.", "It does not carry the port files at the end (port/tools/empaquetar_instalador.py adds them). Download it again."), ancho);
    } else if (s_paso == E_BIENVENIDA) {
        ImGui::PushFont(s_f_titulo); ImGui::TextUnformatted(T("Bienvenido", "Welcome")); ImGui::PopFont();
        texto_tenue(T("Este programa instala el port a PC de Escuadron2: el juego, el launcher "
                    "y el desinstalador. Ocupa ", "This program installs the PC port of Escuadron2: the game, the launcher "
                    "and the uninstaller. It takes "), ancho);
        ImGui::SameLine(0, 0);
        ImGui::TextDisabled("%s.", tam_txt(tam_elegido()).c_str());
        ImGui::Dummy(ImVec2(0, 10));
        panel(T("Necesitas tu propia copia del juego", "You need your own copy of the game"), NULL, std::min(ancho, 760.0f));
        texto_tenue(T("El instalador no trae ningun archivo del juego. Para jugar vas a elegir, desde el launcher, la imagen "
                    "del juego de EE.UU. (GSWE64) que hayas sacado de tu propio disco con una consola (.iso o .gcm).",
                    "The installer does not include any game files. To play, choose in the launcher the disc image of the "
                    "US version of the game (GSWE64) that you dumped from your own disc with a console (.iso or .gcm)."), std::min(ancho, 760.0f) - 48);
        fin_panel(std::min(ancho, 760.0f));
    } else if (s_paso == E_OPCIONES) {
        float pw = std::min(ancho, 820.0f);
        ImGui::PushFont(s_f_titulo); ImGui::TextUnformatted(T("Opciones", "Options")); ImGui::PopFont();
        ImGui::Dummy(ImVec2(0, 4));
        panel(T("Donde instalar", "Where to install"), NULL, pw);
        {
            int m = s_todos ? 1 : 0;
            ImGui::RadioButton(T("Solo para mi", "Just for me"), &m, 0);
            ImGui::SameLine(0, 30);
            ImGui::RadioButton(T("Para todos los usuarios (pide permiso de administrador)", "For all users (asks for administrator permission)"), &m, 1);
            if ((m == 1) != s_todos) { s_todos = m == 1; if (!s_carpeta_tocada) s_carpeta = carpeta_defecto(s_todos); }
            ImGui::Dummy(ImVec2(0, 4));
            char b[1024];
            snprintf(b, sizeof b, "%s", s_carpeta.c_str());
            ImGui::SetNextItemWidth(pw - 48 - 140);
            if (ImGui::InputText("##dir", b, sizeof b)) { s_carpeta = b; s_carpeta_tocada = true; }
            ImGui::SameLine();
            if (ImGui::Button(T("Cambiar...", "Browse..."), ImVec2(128, 0))) { std::string c = elegir_carpeta(s_carpeta); if (!c.empty()) { s_carpeta = c; s_carpeta_tocada = true; } }
        }
        fin_panel(pw);
        if (tam_componente(C_TEXTURAS) || tam_componente(C_MUSICA)) {
            panel(T("Extras", "Extras"), T("Vienen en este instalador; se pueden dejar afuera.", "Included in this installer; you can leave them out."), pw);
            if (tam_componente(C_TEXTURAS)) {
                std::string t = std::string(T("Texturas en alta definicion", "High definition textures")) + "  (" + tam_txt(tam_componente(C_TEXTURAS)) + ")";
                renglon(t.c_str(), T("Paquete de texturas de la comunidad: el juego se ve mas nitido. Se pueden apagar despues en Opciones.",
                                     "Community texture pack: the game looks sharper. Can be turned off later in Options."), pw, 56);
                interruptor("tex", &s_con_texturas);
            }
            if (tam_componente(C_MUSICA)) {
                renglon(T("Musica del launcher", "Launcher music"), T("Suena en el launcher mientras elegis las opciones.", "Plays in the launcher while you set things up."), pw, 56);
                interruptor("mus", &s_con_musica);
            }
            fin_panel(pw);
        }
        panel(T("Accesos directos", "Shortcuts"), NULL, pw);
        ImGui::Checkbox(T("En el escritorio", "On the desktop"), &s_escritorio);
        ImGui::SameLine(0, 40);
        ImGui::Checkbox(T("En el menu Inicio", "In the Start menu"), &s_menu);
        fin_panel(pw);
    } else if (s_paso == E_TRABAJANDO || s_paso == E_LISTO) {
        bool des = s_desinstalar;
        float pw = std::min(ancho, 820.0f);
        ImGui::PushFont(s_f_titulo);
        ImGui::TextUnformatted(s_paso == E_LISTO ? (des ? T("Listo: se desinstalo", "Done: uninstalled") : T("Listo: ya esta instalado", "Done: installed")) : (des ? T("Desinstalando...", "Uninstalling...") : T("Instalando...", "Installing...")));
        ImGui::PopFont();
        float fr = 1;
        if (s_paso == E_TRABAJANDO) {
            fr = des ? (float)SDL_AtomicGet(&s_hecho) / std::max<size_t>(1, s_arch_desinst.size())
                     : (float)((double)SDL_AtomicGet(&s_hecho) * 1024.0 / std::max<uint64_t>(1, tam_elegido()));
        }
        ImVec2 p = ImGui::GetCursorScreenPos();
        p.y += 14;
        dl->AddRectFilled(p, ImVec2(p.x + pw, p.y + 14), IM_COL32(44, 42, 50, 255), 7);
        dl->AddRectFilled(p, ImVec2(p.x + std::max(14.0f, pw * std::min(fr, 1.0f)), p.y + 14), C_ACENTO, 7);
        ImGui::Dummy(ImVec2(pw, 42));
        if (s_paso == E_TRABAJANDO) {
            SDL_LockMutex(s_mx); std::string a = s_actual; SDL_UnlockMutex(s_mx);
            ImGui::TextDisabled("%.0f %%   %s", fr * 100, a.c_str());
        } else if (!des) {
            texto_tenue((T("Se instalo en ", "Installed in ") + s_carpeta + T(". Abri el launcher, elegi tu imagen del disco y a jugar.", ". Open the launcher, pick your disc image and play.")).c_str(), pw);
            ImGui::Dummy(ImVec2(0, 6));
            ImGui::Checkbox(T("Abrir Escuadron2 al cerrar", "Open Escuadron2 when closing"), &s_abrir);
        } else {
            texto_tenue(s_borrar_datos ? T("Se borraron tambien tus partidas y opciones.", "Your saves and options were deleted too.") :
                        T("Tus partidas guardadas y opciones quedaron en tu carpeta de datos, por si lo volves a instalar.",
                          "Your saves and options were kept in your data folder, in case you install it again."), pw);
        }
    } else if (s_paso == E_ERROR) {
        ImGui::PushFont(s_f_titulo); ImGui::TextUnformatted(T("No se pudo terminar", "Could not finish")); ImGui::PopFont();
        SDL_LockMutex(s_mx); std::string e = s_error; SDL_UnlockMutex(s_mx);
        ImGui::TextColored(ImVec4(1, 0.65f, 0.55f, 1), "%s", e.c_str());
    } else if (s_paso == E_DESINST) {
        float pw = std::min(ancho, 820.0f);
        ImGui::PushFont(s_f_titulo); ImGui::TextUnformatted(T("Desinstalar", "Uninstall")); ImGui::PopFont();
        texto_tenue((T("Se va a borrar el port de ", "This removes the port from ") + s_dir_desinst + T(", con sus accesos directos.", ", with its shortcuts.")).c_str(), pw);
        ImGui::Dummy(ImVec2(0, 8));
        panel(T("Tus datos", "Your data"), NULL, pw);
        renglon(T("Borrar tambien partidas guardadas, controles y opciones", "Also delete saves, controls and options"), carpeta_datos().c_str(), pw, 56);
        interruptor("dat", &s_borrar_datos);
        fin_panel(pw);
    }
    ImGui::PopTextWrapPos();
    ImGui::EndChild();

    /* idioma, arriba a la derecha (no mientras trabaja) */
    if (s_paso != E_TRABAJANDO) {
        const char* op[2] = { "Espa\xc3\xb1ol", "English" };
        float x = (float)w - 56;
        for (int i = 1; i >= 0; i--) {
            ImVec2 t = ImGui::CalcTextSize(op[i]);
            x -= t.x + 24;
            ImGui::SetCursorPos(ImVec2(x, 52));
            bool activo = s_en == i;
            ImGui::PushStyleColor(ImGuiCol_Button, activo ? ACENTO : ImVec4(0.12f, 0.115f, 0.14f, 0.85f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, activo ? 1.0f : 0.7f));
            ImGui::PushID(i);
            if (ImGui::Button(op[i], ImVec2(t.x + 20, 0))) s_en = i;
            ImGui::PopID();
            ImGui::PopStyleColor(2);
            x -= 4;
        }
        SDL_SetWindowTitle(SDL_GL_GetCurrentWindow(), s_desinstalar ? T("Desinstalar Escuadron2", "Uninstall Escuadron2") : T("Instalar Escuadron2", "Install Escuadron2"));
    }

    /* botones abajo a la derecha */
    ImGui::PushFont(s_f_medio);
    float bw = 170, bh = 52, by = (float)h - 56 - bh, bx = (float)w - 56 - bw;
    ImGui::SetCursorPos(ImVec2(bx, by));
    if (s_paso == E_BIENVENIDA) {
        if (boton_naranja(T("Siguiente", "Next"), ImVec2(bw, bh))) s_paso = E_OPCIONES;
        ImGui::SetCursorPos(ImVec2(bx - bw - 14, by));
        if (ImGui::Button(T("Cancelar", "Cancel"), ImVec2(bw, bh))) s_salir = true;
    } else if (s_paso == E_OPCIONES) {
        bool adm = s_todos && !soy_admin();
        ImGui::SetCursorPos(ImVec2((float)w - 56 - (adm ? bw + 40 : bw), by));
        if (boton_naranja(adm ? T("Instalar (admin)", "Install (admin)") : T("Instalar", "Install"), ImVec2(adm ? bw + 40 : bw, bh))) {
            if (s_todos && !soy_admin()) {
#ifdef _WIN32
                std::string a = std::string(s_en ? "--en" : "--es") + " --todos --carpeta \"" + s_carpeta + "\"" + (s_escritorio ? "" : " --sin-escritorio") + (s_menu ? "" : " --sin-menu") +
                                (s_con_texturas ? "" : " --sin-texturas") + (s_con_musica ? "" : " --sin-musica");
                if (relanzar_como_admin(a)) s_salir = true;
#endif
            } else empezar();
        }
        ImGui::SetCursorPos(ImVec2((float)w - 56 - (adm ? bw + 40 : bw) - bw - 14, by));
        if (ImGui::Button(T("Atras", "Back"), ImVec2(bw, bh))) s_paso = E_BIENVENIDA;
    } else if (s_paso == E_LISTO || s_paso == E_ERROR || s_paso == E_SIN_PAQUETE) {
        if (boton_naranja(s_paso == E_LISTO ? T("Terminar", "Finish") : T("Cerrar", "Close"), ImVec2(bw, bh))) s_salir = true;
    } else if (s_paso == E_DESINST) {
        bool adm = s_desinst_todos && !soy_admin();
        ImGui::SetCursorPos(ImVec2((float)w - 56 - (adm ? bw + 70 : bw + 20), by));
        if (boton_naranja(adm ? T("Desinstalar (admin)", "Uninstall (admin)") : T("Desinstalar", "Uninstall"), ImVec2(adm ? bw + 70 : bw + 20, bh))) {
            if (s_desinst_todos && !soy_admin()) {
#ifdef _WIN32
                if (relanzar_como_admin(std::string(s_en ? "--en" : "--es") + " --desinstalar --carpeta \"" + s_dir_desinst + "\"" + (s_borrar_datos ? " --borrar-datos" : ""))) s_salir = true;
#endif
            } else empezar();
        }
        ImGui::SetCursorPos(ImVec2((float)w - 56 - (adm ? bw + 70 : bw + 20) - bw - 14, by));
        if (ImGui::Button(T("Cancelar", "Cancel"), ImVec2(bw, bh))) s_salir = true;
    }
    ImGui::PopFont();
    if (s_paso != E_TRABAJANDO) {
        ImGui::SetCursorPos(ImVec2(x0, (float)h - 56 - 22));
        ImGui::TextDisabled(T("Version %s", "Version %s"), VERSION_PORT);
    }
    ImGui::Dummy(ImVec2(0, 0));
}

/* ---- armar un instalador con extras -----------------------------------------------------
 *   Instalar-Escuadron2.exe --agregar SALIDA.exe destino=origen [destino=origen ...]
 * Copia este instalador (con lo que ya trae) y le agrega archivos o carpetas enteras (origen
 * carpeta: se agrega todo lo de adentro bajo 'destino/'). Asi cada uno arma, en su PC, un
 * instalador con su paquete de texturas y su musica, que nunca van al repo.
 * Las imagenes y la musica (ya comprimidas) se guardan tal cual; lo demas con zlib. */
struct NuevoArch { std::string destino, origen; };
static void listar(const std::string& dir, const std::string& dest, std::vector<NuevoArch>& v) {
#ifdef _WIN32
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(W(unir(dir, "*")).c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        std::string n = U(fd.cFileName);
        if (n == "." || n == "..") continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) listar(unir(dir, n), dest + "/" + n, v);
        else v.push_back({ dest + "/" + n, unir(dir, n) });
    } while (FindNextFileW(h, &fd));
    FindClose(h);
#else
    std::string c = "find '" + dir + "' -type f";
    FILE* p = popen(c.c_str(), "r");
    char l[4096];
    if (!p) return;
    while (fgets(l, sizeof l, p)) {
        std::string f = l; while (!f.empty() && (f.back() == '\n')) f.pop_back();
        v.push_back({ dest + f.substr(dir.size()), f });
    }
    pclose(p);
#endif
}
static bool es_dir(const std::string& p) {
#ifdef _WIN32
    DWORD a = GetFileAttributesW(W(p).c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st; return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
#endif
}
static bool ya_comprimido(const std::string& n) {
    size_t d = n.find_last_of('.');
    std::string e = d == std::string::npos ? "" : n.substr(d + 1);
    for (auto& c : e) c = (char)tolower((unsigned char)c);
    return e == "png" || e == "jpg" || e == "jpeg" || e == "flac" || e == "ogg" || e == "mp3" || e == "zip";
}
static void pon64(std::vector<uint8_t>& b, uint64_t v) { for (int i = 0; i < 8; i++) b.push_back((uint8_t)(v >> (8 * i))); }

static int armar(int argc, char** argv, int desde) {
    std::string salida = argv[desde];
    std::vector<NuevoArch> nuevos;
    for (int i = desde + 1; i < argc; i++) {
        std::string a = argv[i];
        size_t eq = a.find('=');
        if (eq == std::string::npos) continue;
        std::string d = a.substr(0, eq), o = a.substr(eq + 1);
        while (!d.empty() && d.back() == '/') d.pop_back();
        if (es_dir(o)) listar(o, d, nuevos);
        else if (existe(o)) nuevos.push_back({ d, o });
    }
    FILE* in = abrir(s_yo, "rb");
    FILE* out = abrir(salida, "wb");
    if (!in || !out) { if (in) fclose(in); if (out) fclose(out); return 2; }
    /* el programa y lo que ya traia, tal cual (las posiciones no cambian) */
    uint64_t hasta = s_tam_programa;
    for (auto& e : s_ents) hasta = std::max<uint64_t>(hasta, e.pos + e.comp);
    std::vector<uint8_t> b(1 << 20), z(1 << 20);
    for (uint64_t q = hasta; q;) { size_t n = (size_t)std::min<uint64_t>(q, b.size()); if (fread(b.data(), 1, n, in) != n) break; fwrite(b.data(), 1, n, out); q -= n; }
    fclose(in);
    std::vector<Entrada> todas;
    uint64_t pos = hasta, total = 0;
    for (auto& e : s_ents) {   /* si se agrega de nuevo algo que ya estaba (otra musica), gana lo nuevo */
        bool tapado = false;
        for (auto& n : nuevos) if (n.destino == e.nombre) tapado = true;
        if (!tapado) { todas.push_back(e); total += e.tam; }
    }
    for (auto& n : nuevos) {
        FILE* f = abrir(n.origen, "rb");
        if (!f) continue;
        Entrada e; e.nombre = n.destino; e.pos = pos; e.tam = 0; e.comp = 0;
        if (ya_comprimido(n.destino)) {
            size_t k;
            while ((k = fread(b.data(), 1, b.size(), f)) > 0) { fwrite(b.data(), 1, k, out); e.tam += k; }
            e.comp = e.tam;
        } else {
            z_stream zs; memset(&zs, 0, sizeof zs);
            deflateInit(&zs, 6);
            int fin;
            do {
                size_t k = fread(b.data(), 1, b.size(), f);
                e.tam += k;
                fin = feof(f) ? Z_FINISH : Z_NO_FLUSH;
                zs.next_in = b.data(); zs.avail_in = (uInt)k;
                do {
                    zs.next_out = z.data(); zs.avail_out = (uInt)z.size();
                    deflate(&zs, fin);
                    size_t sal = z.size() - zs.avail_out;
                    fwrite(z.data(), 1, sal, out); e.comp += sal;
                } while (zs.avail_out == 0);
            } while (fin != Z_FINISH);
            deflateEnd(&zs);
            if (e.comp == e.tam) e.comp++;   /* comp == tam quiere decir "tal cual": no confundir */
        }
        fclose(f);
        pos += e.comp; total += e.tam;
        todas.push_back(e);
    }
    /* indice y remolque nuevos */
    std::vector<uint8_t> ix;
    uint32_t cant = (uint32_t)todas.size();
    for (int i = 0; i < 4; i++) ix.push_back((uint8_t)(cant >> (8 * i)));
    for (auto& e : todas) {
        ix.push_back((uint8_t)(e.nombre.size() & 255)); ix.push_back((uint8_t)(e.nombre.size() >> 8));
        ix.insert(ix.end(), e.nombre.begin(), e.nombre.end());
        pon64(ix, e.pos); pon64(ix, e.comp); pon64(ix, e.tam);
    }
    fwrite(ix.data(), 1, ix.size(), out);
    std::vector<uint8_t> rem = { 'R', 'L', 'P', 'A', 'Q', '0', '0', '1' };
    pon64(rem, pos); pon64(rem, s_tam_programa); pon64(rem, total);
    fwrite(rem.data(), 1, rem.size(), out);
    bool ok = fclose(out) == 0;
    char msj[512];
    snprintf(msj, sizeof msj, ok ? "Listo: %s\n%u archivos agregados (%s instalado)." : "No se pudo escribir %s", salida.c_str(), (unsigned)nuevos.size(), tam_txt(total).c_str());
    SDL_ShowSimpleMessageBox(ok ? SDL_MESSAGEBOX_INFORMATION : SDL_MESSAGEBOX_ERROR, "Escuadron2", msj, NULL);
    return ok ? 0 : 1;
}

/* el desinstalador no se puede borrar a si mismo mientras corre: se copia a TEMP y sigue desde ahi */
static bool desinstalar_desde_temp(const std::string& dir, int argc, char** argv) {
#ifdef _WIN32
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    std::string copia = U(tmp) + "Escuadron2_desinstalar.exe";
    if (!CopyFileW(W(s_yo).c_str(), W(copia).c_str(), FALSE)) return false;
    MoveFileExW(W(copia).c_str(), NULL, MOVEFILE_DELAY_UNTIL_REBOOT);
    std::string a = std::string(s_en ? "--en" : "--es") + " --desinstalar --carpeta \"" + dir + "\"";
    for (int i = 1; i < argc; i++) if (!strcmp(argv[i], "--borrar-datos")) a += " --borrar-datos";
    STARTUPINFOW si; PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof si); si.cb = sizeof si;
    std::wstring cmd = L"\"" + W(copia) + L"\" " + W(a);
    if (!CreateProcessW(NULL, &cmd[0], NULL, NULL, FALSE, 0, NULL, tmp, &si, &pi)) return false;
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return true;
#else
    (void)dir; (void)argc; (void)argv;
    return false;
#endif
}

int main(int argc, char** argv) {
    const char* capt = getenv("RL_INSTALADOR_CAPTURAS");
    const char* autom = getenv("RL_INSTALADOR_AUTO");
    std::string arg_carpeta;
    bool arg_desinst = false, arg_todos = false;
    idioma_del_sistema();
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--en")) s_en = 1;
        else if (!strcmp(argv[i], "--es")) s_en = 0;
        if (!strcmp(argv[i], "--carpeta") && i + 1 < argc) arg_carpeta = argv[++i];
        else if (!strcmp(argv[i], "--todos")) arg_todos = true;
        else if (!strcmp(argv[i], "--sin-escritorio")) s_escritorio = false;
        else if (!strcmp(argv[i], "--sin-menu")) s_menu = false;
        else if (!strcmp(argv[i], "--desinstalar")) arg_desinst = true;
        else if (!strcmp(argv[i], "--borrar-datos")) s_borrar_datos = true;
        else if (!strcmp(argv[i], "--sin-texturas")) s_con_texturas = false;
        else if (!strcmp(argv[i], "--sin-musica")) s_con_musica = false;
    }
#ifdef _WIN32
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    /* en Windows los argumentos llegan en la pagina de codigos local: tomarlos de nuevo en UTF-8 */
    {
        int n; LPWSTR* wa = CommandLineToArgvW(GetCommandLineW(), &n);
        for (int i = 1; wa && i + 1 < n; i++) if (!wcscmp(wa[i], L"--carpeta")) arg_carpeta = U(wa[i + 1]);
        if (wa) LocalFree(wa);
    }
#endif
    if (capt && !getenv("SDL_VIDEODRIVER")) SDL_setenv("SDL_VIDEODRIVER", "offscreen", 1);
    SDL_SetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON, "1");
    SDL_SetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON_SMALL, "1");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) return 1;
    s_mx = SDL_CreateMutex();
    s_yo = ruta_propia();
    bool hay = leer_paquete();
    for (int i = 1; i + 1 < argc; i++)
        if (!strcmp(argv[i], "--agregar")) {
#ifdef _WIN32
            /* rutas en UTF-8 */
            int n; LPWSTR* wa = CommandLineToArgvW(GetCommandLineW(), &n);
            std::vector<std::string> u; std::vector<char*> pv;
            for (int k = 0; wa && k < n; k++) u.push_back(U(wa[k]));
            for (auto& x : u) pv.push_back(&x[0]);
            if (wa) LocalFree(wa);
            int r = hay ? armar((int)pv.size(), pv.data(), i + 1) : 3;
#else
            int r = hay ? armar(argc, argv, i + 1) : 3;
#endif
            SDL_Quit();
            return r;
        }
    /* modo desinstalador: sin paquete y con instalados.txt al lado, o con --desinstalar */
    {
        std::string dir = arg_carpeta;
        if (dir.empty()) { size_t sl = s_yo.find_last_of("/\\"); dir = sl == std::string::npos ? "." : s_yo.substr(0, sl); }
        if (arg_desinst || (!hay && existe(unir(dir, "instalados.txt")))) {
            if (!arg_desinst) {   /* corriendo desde la carpeta instalada: pasar a TEMP */
                if (desinstalar_desde_temp(dir, argc, argv)) { SDL_Quit(); return 0; }
            }
            s_desinstalar = true;
            s_dir_desinst = dir;
            if (!leer_instalados(dir)) { SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Escuadron2", T("No se encontro la instalacion.", "The installation was not found."), NULL); return 1; }
            s_paso = E_DESINST;
        }
    }
    if (!s_desinstalar) {
        s_paso = hay ? E_BIENVENIDA : E_SIN_PAQUETE;
        s_todos = arg_todos;
        s_carpeta = arg_carpeta.empty() ? carpeta_defecto(s_todos) : arg_carpeta;
        if (autom) { s_carpeta = autom; s_escritorio = s_menu = false; }
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_Window* win = SDL_CreateWindow(s_desinstalar ? T("Desinstalar Escuadron2", "Uninstall Escuadron2") : T("Instalar Escuadron2", "Install Escuadron2"), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       960, 680, SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI | (capt ? SDL_WINDOW_HIDDEN : 0));
    if (!win) { SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Escuadron2", SDL_GetError(), NULL); return 1; }
    SDL_GLContext gl = SDL_GL_CreateContext(win);
    SDL_GL_MakeCurrent(win, gl);
    SDL_GL_SetSwapInterval(capt ? 0 : 1);
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
    cargar_fondo();
    if ((arg_todos && !arg_carpeta.empty() && soy_admin()) || autom) empezar();   /* relanzado como admin, o prueba */
    int cuadro = 0;
    static const int k_capt[] = { E_BIENVENIDA, E_OPCIONES, E_TRABAJANDO, E_LISTO, E_DESINST, E_SIN_PAQUETE };
    while (!s_salir) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL2_ProcessEvent(&e);
            if (e.type == SDL_QUIT && s_paso != E_TRABAJANDO) s_salir = true;
        }
        if (s_paso == E_TRABAJANDO) {
            int st = SDL_AtomicGet(&s_estado);
            if (st == 2) s_paso = E_LISTO;
            else if (st == 3) s_paso = E_ERROR;
        }
        if (autom && s_paso != E_TRABAJANDO) { s_abrir = false; s_salir = true; }
        if (capt) {   /* recorrer los pasos con datos de ejemplo */
            int i = cuadro / 3;
            if (i >= (int)(sizeof k_capt / sizeof k_capt[0])) break;
            s_paso = k_capt[i];
            s_desinstalar = s_paso == E_DESINST;
            if (s_paso == E_DESINST) s_dir_desinst = s_carpeta;
            if (s_paso == E_TRABAJANDO) { SDL_AtomicSet(&s_hecho, (int)((s_total ? s_total : 150u << 20) * 0.62 / 1024)); if (!s_total) s_total = 150u << 20; poner_actual("escuadron2.exe"); }
        }
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        int w, h;
        SDL_GetWindowSize(win, &w, &h);
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2((float)w, (float)h));
        ImGui::Begin("instalador", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
        interfaz(w, h);
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
            snprintf(p, sizeof p, "%s/instalador_%d.bmp", capt, cuadro / 3);
            guardar_bmp(p, fw, fh);
        }
        SDL_GL_SwapWindow(win);
        cuadro++;
    }
    if (s_paso == E_LISTO && !s_desinstalar && s_abrir && !capt) {
        std::string exe = unir(s_carpeta, "Escuadron2.exe");
#ifdef _WIN32
        /* si se instalo como admin, abrirlo igual con los permisos del explorador seria lo ideal; asi esta bien */
        ShellExecuteW(NULL, L"open", W(exe).c_str(), NULL, W(s_carpeta).c_str(), SW_SHOWNORMAL);
#else
        (void)exe;
#endif
    }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(gl);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
