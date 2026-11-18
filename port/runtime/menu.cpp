/* Menu dentro del juego (F1 o el boton Back/Select/Share del joystick), contador de FPS y aviso
 * de shaders. Se dibuja con Dear ImGui encima de la imagen del juego, en gl_present (hilo grafico).
 *
 * Las opciones son las mismas del launcher (port/launcher/ajustes.h) y se guardan en el mismo
 * ajustes.ini. Las que se pueden cambiar en vivo se aplican al toque; las demas (resolucion,
 * antialiasing, ...) se guardan y valen la proxima vez que se abre el juego.
 * Mientras el menu esta abierto el juego recibe el control suelto (no se mueve nada). */
#include <SDL.h>
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"
#include <SDL_opengl.h>
#include <cfloat>
#include <string>
#include <vector>
#define STB_IMAGE_STATIC   /* texturas_hd.c ya trae su copia de stb_image */
#include "estilo.h"
#include "ajustes.h"
#include "fuentes.inc"

extern "C" {
extern int gl_headless, gl_fullscreen, gl_widescreen, gl_vsync, gl_filtro_salida, gl_scale, gl_msaa, gl_aniso_nivel,
    gl_preparar_shaders, hd_desactivado, audio_volumen, g_camara_bomba, g_rumble_enabled;
extern uint32_t gx_xf_gen;
extern int g_contador_fps, g_aviso_shaders;
extern unsigned g_trucos_a, g_trucos_b;
extern char g_ajustes_ruta[1024];
int gl_shaders_pendientes(void);
SDL_Window* gl_ventana(void);
SDL_GLContext gl_contexto(void);
void rt_log(const char* fmt, ...);
uint64_t os_now_us(void);
}

static bool s_iniciado, s_abierto;
static Ajustes s_aj;
static AjustesUI s_ui;
static float s_k = -1;   /* escala con que se armaron las letras */

/* lo que esta funcionando ahora (lo que se cambia en vivo) */
static void leer_vivos(Ajustes& a) {
    a.pantalla_completa = (SDL_GetWindowFlags(gl_ventana()) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
    a.panoramico = gl_widescreen != 0;
    a.vsync = gl_vsync != 0;
    a.filtro = gl_filtro_salida;
    a.contador_fps = g_contador_fps;
    a.aviso_shaders = g_aviso_shaders != 0;
    a.volumen = audio_volumen;
    a.camara_bomba = g_camara_bomba != 0;
    a.vibracion = g_rumble_enabled != 0;
    for (int i = 0; i < NTRUCOS; i++)
        a.trucos[i] = ((k_trucos[i].palabra == TRUCOS_A ? g_trucos_a : g_trucos_b) & k_trucos[i].mascara) != 0;
}

static void aplicar_vivos(const Ajustes& a) {
    SDL_Window* w = gl_ventana();
    bool fs = (SDL_GetWindowFlags(w) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
    if (fs != a.pantalla_completa) SDL_SetWindowFullscreen(w, a.pantalla_completa ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    gl_fullscreen = a.pantalla_completa;
    if ((gl_widescreen != 0) != a.panoramico) { gl_widescreen = a.panoramico; gx_xf_gen++; }
    if ((gl_vsync != 0) != a.vsync) { gl_vsync = a.vsync; SDL_GL_SetSwapInterval(gl_vsync ? 1 : 0); }
    gl_filtro_salida = a.filtro;
    g_contador_fps = a.contador_fps;
    g_aviso_shaders = a.aviso_shaders;
    audio_volumen = a.volumen;
    g_camara_bomba = a.camara_bomba;
    g_rumble_enabled = a.vibracion;
    g_trucos_a = g_trucos_b = 0;   /* video.c los mantiene prendidos en la partida */
    for (int i = 0; i < NTRUCOS; i++)
        if (a.trucos[i]) (k_trucos[i].palabra == TRUCOS_A ? g_trucos_a : g_trucos_b) |= k_trucos[i].mascara;
}

static void iniciar(void) {
    s_iniciado = true;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = NULL;
    io.LogFilename = NULL;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    ImFontConfig fc; fc.FontDataOwnedByAtlas = false;
    s_f_normal = io.Fonts->AddFontFromMemoryTTF((void*)k_poppins_regular, (int)sizeof k_poppins_regular, 17, &fc);
    s_f_chico = io.Fonts->AddFontFromMemoryTTF((void*)k_poppins_regular, (int)sizeof k_poppins_regular, 14, &fc);
    s_f_medio = io.Fonts->AddFontFromMemoryTTF((void*)k_poppins_medium, (int)sizeof k_poppins_medium, 19, &fc);
    s_f_titulo = io.Fonts->AddFontFromMemoryTTF((void*)k_poppins_medium, (int)sizeof k_poppins_medium, 28, &fc);
    s_f_grande = io.Fonts->AddFontFromMemoryTTF((void*)k_poppins_bold, (int)sizeof k_poppins_bold, 46, &fc);
    estilo();
    ImGui_ImplSDL2_InitForOpenGL(gl_ventana(), gl_contexto());
    ImGui_ImplOpenGL3_Init("#version 330 core");
    /* como estaban las opciones al arrancar: lo que dice ajustes.ini y lo que esta andando */
    ajustes_cargar(s_aj, g_ajustes_ruta);
    leer_vivos(s_aj);
    s_ui.en_juego = true;
    s_ui.al_abrir = s_aj;
}

static void abrir(bool si) {
    if (si == s_abierto) return;
    s_abierto = si;
    if (si) { leer_vivos(s_aj); s_ui.cerrar = false; }
    else if (!ajustes_guardar(s_aj, g_ajustes_ruta)) rt_log("[menu] no se pudo guardar %s\n", g_ajustes_ruta);
    else rt_log("[menu] opciones guardadas en %s\n", g_ajustes_ruta);
}

/* el juego no ve el control mientras el menu esta abierto, ni despues hasta que se
   suelten todos los botones (si no, la B con que se cerro el menu le llegaria al juego) */
extern "C" int menu_bloquea(int hay_botones) {
    static int soltar;
    if (s_abierto) { soltar = 1; return 1; }
    if (soltar && hay_botones) return 1;
    soltar = 0;
    return 0;
}

/* cada evento de la ventana pasa por aca primero; devuelve 1 si el juego no lo tiene que ver */
extern "C" int menu_evento(const SDL_Event* e) {
    if (gl_headless) return 0;
    if (!s_iniciado) {
        if (e->type == SDL_KEYDOWN && e->key.keysym.sym == SDLK_F1 && !e->key.repeat) { abrir(true); return 1; }
        if (e->type == SDL_CONTROLLERBUTTONDOWN && e->cbutton.button == SDL_CONTROLLER_BUTTON_BACK) { abrir(true); return 1; }
        return 0;
    }
    ImGui_ImplSDL2_ProcessEvent(e);
    if ((e->type == SDL_KEYDOWN && e->key.keysym.sym == SDLK_F1 && !e->key.repeat) ||
        (e->type == SDL_CONTROLLERBUTTONDOWN && e->cbutton.button == SDL_CONTROLLER_BUTTON_BACK)) {
        abrir(!s_abierto);
        return 1;
    }
    if (!s_abierto) return 0;
    return e->type == SDL_KEYDOWN || e->type == SDL_KEYUP || e->type == SDL_TEXTINPUT ||
           e->type == SDL_MOUSEBUTTONDOWN || e->type == SDL_MOUSEBUTTONUP || e->type == SDL_MOUSEWHEEL;
}

/* contador de FPS: promedio del ultimo medio segundo */
static float fps_medir(void) {
    static uint64_t t0; static int n; static float fps;
    uint64_t t = os_now_us();
    if (!t0) t0 = t;
    n++;
    if (t - t0 >= 500000) { fps = n * 1e6f / (float)(t - t0); n = 0; t0 = t; }
    return fps;
}

static void cartel(ImDrawList* dl, ImFont* f, float z, ImVec2 p, bool derecha, ImU32 col, const char* t) {
    ImVec2 ts = f->CalcTextSizeA(z, FLT_MAX, 0, t);
    if (derecha) p.x -= ts.x + 16;
    dl->AddRectFilled(p, ImVec2(p.x + ts.x + 16, p.y + ts.y + 8), IM_COL32(0, 0, 0, 150), 6);
    dl->AddText(f, z, ImVec2(p.x + 8, p.y + 4), col, t);
}

/* avisos cortos abajo al centro (p. ej. "Captura guardada"); se puede llamar desde otro hilo */
static char s_aviso[128];
static volatile uint32_t s_aviso_hasta;
extern "C" void menu_aviso(const char* texto) {
    snprintf(s_aviso, sizeof s_aviso, "%s", texto);
    s_aviso_hasta = SDL_GetTicks() + 2500;
}

extern "C" void menu_dibujar(int ww, int wh) {
    if (gl_headless) return;
    bool aviso = s_aviso_hasta && SDL_GetTicks() < s_aviso_hasta;
    float fps = fps_medir();
    {   /* pruebas: RL_MENU_ABRIR=N abre el menu en el cuadro N (RL_MENU_PESTANA elige la pestana) */
        static int cuadros; const char* e = getenv("RL_MENU_ABRIR");
        if (e && ++cuadros == atoi(e)) { abrir(true); if (getenv("RL_MENU_PESTANA")) s_ui.pest = atoi(getenv("RL_MENU_PESTANA")); }
    }
    int shaders = g_aviso_shaders ? gl_shaders_pendientes() : 0;
    if (!s_abierto && !g_contador_fps && !shaders && !aviso) {
        if (s_iniciado) {   /* que ImGui no junte eventos viejos mientras no se dibuja */
            ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplSDL2_NewFrame(); ImGui::NewFrame(); ImGui::EndFrame();
        }
        return;
    }
    if (!s_iniciado) iniciar();
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    ImGuiIO& io = ImGui::GetIO();
    float k = io.DisplaySize.y / 800.0f;
    if (k < 1.0f) k = 1.0f;
    if (k > 2.4f) k = 2.4f;
    s_k = k;
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    if (g_contador_fps && !s_abierto) {
        char t[32]; snprintf(t, sizeof t, "%.0f FPS", fps);
        float z = s_f_medio->LegacySize * k;
        ImU32 col = fps >= 57 ? IM_COL32(120, 255, 140, 255) : fps >= 45 ? IM_COL32(255, 212, 38, 255) : IM_COL32(255, 90, 80, 255);
        cartel(fg, s_f_medio, z, g_contador_fps == 2 ? ImVec2(io.DisplaySize.x - 12, 12) : ImVec2(12, 12), g_contador_fps == 2, col, t);
    }
    if (shaders && !s_abierto) {
        char t[64]; snprintf(t, sizeof t, "Compiling %d shader%s...", shaders, shaders == 1 ? "" : "s");
        float z = s_f_normal->LegacySize * k;
        cartel(fg, s_f_normal, z, ImVec2(12, io.DisplaySize.y - z - 24), false, IM_COL32(255, 212, 38, 255), t);
    }
    if (aviso && !s_abierto) {
        float z = s_f_medio->LegacySize * k;
        ImVec2 ts = s_f_medio->CalcTextSizeA(z, FLT_MAX, 0, s_aviso);
        cartel(fg, s_f_medio, z, ImVec2((io.DisplaySize.x - ts.x) * 0.5f - 8, io.DisplaySize.y - ts.y - 40 * k), false, IM_COL32(255, 212, 38, 255), s_aviso);
    }
    if (s_abierto) {
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.01f, 0.04f, 0.88f));
        ImGui::Begin("##menu", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse);
        if (ajustes_ui(s_aj, s_ui, ImVec2(0, 0), io.DisplaySize, k)) aplicar_vivos(s_aj);
        ImGui::End();
        ImGui::PopStyleColor();
        if (s_ui.cerrar) abrir(false);
    }
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    {   /* pruebas: RL_MENU_CAPTURA=archivo.bmp guarda la pantalla con el menu abierto */
        static int n;
        const char* c = getenv("RL_MENU_CAPTURA");
        if (c && s_abierto && ++n == 20) {   /* el juego no enlaza libGL: glReadPixels por SDL */
            typedef void (*LeerPx)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
            LeerPx leer = (LeerPx)SDL_GL_GetProcAddress("glReadPixels");
            int w = (int)io.DisplaySize.x, h = (int)io.DisplaySize.y;
            std::vector<uint8_t> px((size_t)w * h * 3);
            FILE* f = fopen(c, "wb");
            leer(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px.data());
            if (f) {   /* .ppm (de abajo hacia arriba en OpenGL: se da vuelta) */
                fprintf(f, "P6 %d %d 255\n", w, h);
                for (int y = h - 1; y >= 0; y--) fwrite(&px[(size_t)y * w * 3], 1, (size_t)w * 3, f);
                fclose(f);
            }
            rt_log("[menu] captura en %s\n", c);
        }
    }
    (void)ww; (void)wh;
}
