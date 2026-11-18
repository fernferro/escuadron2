/* Opciones del juego, compartidas por el launcher (Escuadron2) y el menu dentro del juego (F1).
 *
 * Se guardan en ajustes.ini (clave = valor) en la carpeta de datos: el juego lo lee al arrancar
 * (port/runtime/main.c, antes de los argumentos) y el menu del juego lo vuelve a escribir.
 * La pantalla es la "mezcla" de los dos estilos que gustaron: titulos y valores amarillos,
 * y un panel oscuro con borde dorado con la lista a la izquierda y la explicacion de
 * la opcion elegida a la derecha.
 *
 * Incluir despues de imgui.h y estilo.h (usa sus fuentes). */
#pragma once
#include <string>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "trucos.h"   /* port/runtime: la lista de trucos del juego */

struct Ajustes {
    /* pantalla */
    bool pantalla_completa = true;
    bool panoramico = false;
    int escala = 0;              /* 0 automatica (x2), 1..4 */
    bool vsync = false;
    int filtro = 0;              /* 0 suave, 1 nitido */
    int contador_fps = 0;        /* 0 no, 1 arriba a la izquierda, 2 arriba a la derecha */
    /* graficos */
    int msaa = 0;                /* 0, 2, 4, 8 */
    int aniso = 8;               /* 0 (apagado), 2, 4, 8, 16 */
    bool texturas_hd = true;
    bool shaders_al_arrancar = true;
    bool aviso_shaders = true;
    /* audio */
    int volumen = 100;           /* 0..100 */
    /* juego */
    bool camara_bomba = false;
    bool vibracion = true;
    /* trucos (los passcodes del propio juego: port/runtime/trucos.h) */
    bool trucos[NTRUCOS] = {};
};

static inline std::string aj_recortar(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

/* devuelve false si no existe el archivo (quedan los valores por defecto) */
static inline bool ajustes_cargar(Ajustes& a, const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    char linea[512];
    if (!f) return false;
    while (fgets(linea, sizeof linea, f)) {
        std::string l = linea;
        size_t eq = l.find('=');
        if (l.empty() || l[0] == '#' || eq == std::string::npos) continue;
        std::string k = aj_recortar(l.substr(0, eq)), v = aj_recortar(l.substr(eq + 1));
        int n = atoi(v.c_str());
        bool b = n != 0;
        if (k == "pantalla_completa") a.pantalla_completa = b;
        else if (k == "panoramico") a.panoramico = b;
        else if (k == "escala") a.escala = n;
        else if (k == "vsync") a.vsync = b;
        else if (k == "filtro") a.filtro = n;
        else if (k == "contador_fps") a.contador_fps = n;
        else if (k == "msaa") a.msaa = n;
        else if (k == "anisotropico") a.aniso = n;
        else if (k == "texturas_hd") a.texturas_hd = b;
        else if (k == "preparar_todo") a.shaders_al_arrancar = b;
        else if (k == "aviso_shaders") a.aviso_shaders = b;
        else if (k == "volumen") a.volumen = n;
        else if (k == "camara_bomba") a.camara_bomba = b;
        else if (k == "vibracion") a.vibracion = b;
        else if (k.rfind("truco_", 0) == 0) {
            for (int i = 0; i < NTRUCOS; i++) if (k.substr(6) == k_trucos[i].clave) a.trucos[i] = b;
        }
    }
    fclose(f);
    return true;
}

static inline bool ajustes_guardar(const Ajustes& a, const std::string& path) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    fprintf(f, "# Escuadron2: opciones (las escriben el launcher y el menu del juego, F1)\n");
    fprintf(f, "pantalla_completa = %d\npanoramico = %d\nescala = %d\nvsync = %d\nfiltro = %d\ncontador_fps = %d\n",
            a.pantalla_completa, a.panoramico, a.escala, a.vsync, a.filtro, a.contador_fps);
    fprintf(f, "msaa = %d\nanisotropico = %d\ntexturas_hd = %d\npreparar_todo = %d\naviso_shaders = %d\n",
            a.msaa, a.aniso, a.texturas_hd, a.shaders_al_arrancar, a.aviso_shaders);
    fprintf(f, "volumen = %d\ncamara_bomba = %d\nvibracion = %d\n", a.volumen, a.camara_bomba, a.vibracion);
    for (int i = 0; i < NTRUCOS; i++) fprintf(f, "truco_%s = %d\n", k_trucos[i].clave, a.trucos[i]);
    fclose(f);
    return true;
}

/* ---- pantalla de opciones ------------------------------------------------------------- */

static const ImU32 AJ_AMARILLO = IM_COL32(255, 212, 38, 255);
static const ImU32 AJ_AMARILLO_OSC = IM_COL32(120, 88, 0, 255);
static const ImU32 AJ_DORADO = IM_COL32(201, 168, 76, 255);
static const ImU32 AJ_TEXTO = IM_COL32(236, 236, 242, 255);
static const ImU32 AJ_TENUE = IM_COL32(150, 156, 178, 255);
static const ImU32 AJ_PANEL = IM_COL32(8, 12, 28, 240);
static const ImU32 AJ_SELEC = IM_COL32(32, 54, 108, 175);

enum { AJ_P_PANTALLA, AJ_P_GRAFICOS, AJ_P_AUDIO, AJ_P_JUEGO, AJ_P_TRUCOS, AJ_NP };
static const char* k_aj_pest[AJ_NP] = { "DISPLAY", "GRAPHICS", "AUDIO", "GAME", "CHEATS" };

struct AjustesUI {
    int pest = 0, sel = 0, desp = 0;   /* desp: primera fila a la vista (listas largas) */
    bool en_juego = false;     /* menu del juego: avisa lo que necesita reiniciar */
    Ajustes al_abrir;          /* como estaban al arrancar el juego (para el aviso de reinicio) */
    bool cerrar = false;       /* Esc / B: el que llama decide */
};

/* una opcion de la lista */
struct AjFila {
    const char* t;             /* etiqueta */
    const char* desc;          /* explicacion (panel derecho) */
    int tipo;                  /* 0 lista de valores, 1 si/no, 2 barra 0..100 */
    int* v; bool* b;           /* valor */
    const char* const* ops; const int* vals; int n;   /* tipo 0: textos y valores */
    bool reinicio;             /* se aplica al volver a arrancar el juego */
    bool cambiado;             /* (en el juego) distinto de como arranco */
    const char* codigo;        /* trucos: el passcode original */
};

static inline int aj_idx(const AjFila& f) {
    for (int i = 0; i < f.n; i++) if (f.vals[i] == *f.v) return i;
    return 0;
}
static inline void aj_mover(AjFila& f, int d) {
    if (f.tipo == 1) *f.b = !*f.b;
    else if (f.tipo == 2) { *f.v += d * 10; if (*f.v < 0) *f.v = 0; if (*f.v > 100) *f.v = 100; }
    else { int i = aj_idx(f) + d; if (i < 0) i = f.n - 1; if (i >= f.n) i = 0; *f.v = f.vals[i]; }
}
static inline const char* aj_valor(const AjFila& f) {
    if (f.tipo == 1) return *f.b ? "ON" : "OFF";
    if (f.tipo == 2) return "";
    return f.ops[aj_idx(f)];
}

static void aj_triangulo(ImDrawList* dl, ImVec2 c, float r, bool der, ImU32 col) {
    if (der) dl->AddTriangleFilled(ImVec2(c.x - r * 0.6f, c.y - r), ImVec2(c.x - r * 0.6f, c.y + r), ImVec2(c.x + r * 0.8f, c.y), col);
    else dl->AddTriangleFilled(ImVec2(c.x + r * 0.6f, c.y - r), ImVec2(c.x + r * 0.6f, c.y + r), ImVec2(c.x - r * 0.8f, c.y), col);
}

static void aj_texto_sombra(ImDrawList* dl, ImFont* f, float z, ImVec2 p, ImU32 c, const char* t) {
    dl->AddText(f, z, ImVec2(p.x + 2, p.y + 3), IM_COL32(0, 0, 0, 170), t);
    dl->AddText(f, z, p, c, t);
}

static bool aj_tecla(ImGuiKey a, ImGuiKey b = ImGuiKey_None, ImGuiKey c = ImGuiKey_None) {
    return ImGui::IsKeyPressed(a, true) || (b != ImGuiKey_None && ImGui::IsKeyPressed(b, true)) ||
           (c != ImGuiKey_None && ImGui::IsKeyPressed(c, true));
}

/* Dibuja la pantalla de opciones en el rectangulo (org, tam) de la ventana actual; k agranda
 * letras y medidas (1 en el launcher; en el juego, segun el alto de la ventana).
 * Devuelve true si cambio algo (guardar y aplicar). */
static bool ajustes_ui(Ajustes& a, AjustesUI& st, ImVec2 org, ImVec2 tam, float k = 1.0f) {
    static const char* const esc_t[] = { "AUTOMATIC (x2)", "x1  (640×528)", "x2  (1280×1056)", "x3  (1920×1584)", "x4  (2560×2112)" };
    static const int esc_v[] = { 0, 1, 2, 3, 4 };
    static const char* const asp_t[] = { "4:3  ORIGINAL", "16:9  WIDESCREEN" };
    static const int asp_v[] = { 0, 1 };
    static const char* const fil_t[] = { "SMOOTH", "SHARP" };
    static const int fil_v[] = { 0, 1 };
    static const char* const fps_t[] = { "OFF", "TOP LEFT", "TOP RIGHT" };
    static const int fps_v[] = { 0, 1, 2 };
    static const char* const msaa_t[] = { "OFF", "MSAA 2X", "MSAA 4X", "MSAA 8X" };
    static const int msaa_v[] = { 0, 2, 4, 8 };
    static const char* const ani_t[] = { "OFF", "2X", "4X", "8X", "16X" };
    static const int ani_v[] = { 0, 2, 4, 8, 16 };
    int pan = a.panoramico ? 1 : 0;
    AjFila filas[40];
    int nf = 0;
    bool cambio = false;
    const Ajustes& o = st.al_abrir;
#define FILA_L(T, D, V, OPS, VALS, R, CAMB) filas[nf++] = AjFila{ T, D, 0, V, NULL, OPS, VALS, (int)(sizeof(VALS) / sizeof(VALS[0])), R, CAMB }
#define FILA_B(T, D, B, R, CAMB) filas[nf++] = AjFila{ T, D, 1, NULL, B, NULL, NULL, 2, R, CAMB }
    switch (st.pest) {
    case AJ_P_PANTALLA:
        FILA_B("FULLSCREEN", "Fill the whole monitor. While playing, F11 toggles it.", &a.pantalla_completa, false, false);
        FILA_L("ASPECT RATIO", "4:3 is how it looked on a GameCube TV. 16:9 widens the view for widescreen monitors (you see more to the sides). F10 switches it while playing.", &pan, asp_t, asp_v, false, false);
        FILA_L("INTERNAL RESOLUTION", "How many pixels the game is rendered at before it is shown. Higher looks sharper and asks more of the graphics card. The GameCube rendered at x1.", &a.escala, esc_t, esc_v, true, a.escala != o.escala);
        FILA_B("VSYNC", "Waits for the monitor before showing each frame: avoids tearing, but can lower the FPS in heavy scenes.", &a.vsync, false, false);
        FILA_L("OUTPUT SCALING", "How the image is enlarged to the window size. Smooth keeps it even; sharp keeps hard edges.", &a.filtro, fil_t, fil_v, false, false);
        FILA_L("FPS COUNTER", "Shows in a corner how many frames per second the game runs at (60 is the console speed).", &a.contador_fps, fps_t, fps_v, false, false);
        break;
    case AJ_P_GRAFICOS:
        FILA_L("ANTIALIASING", "Smooths the jagged edges of ships and terrain. 4X looks great and costs little on a modern graphics card.", &a.msaa, msaa_t, msaa_v, true, a.msaa != o.msaa);
        FILA_L("ANISOTROPIC FILTERING", "Keeps ground textures sharp when seen at an angle (the floor in the distance).", &a.aniso, ani_t, ani_v, true, a.aniso != o.aniso);
        FILA_B("HD TEXTURES", "Uses Dolphin texture packs copied into the texturas_hd folder (if any).", &a.texturas_hd, true, a.texturas_hd != o.texturas_hd);
        FILA_B("SHADERS AT STARTUP", "Prepares and warms up every effect in the game before playing, with a progress bar. The first time (or after a driver update) it takes a few minutes, and every launch a few seconds more; in exchange there is no stutter the first time an effect appears. When off, they are prepared on loading screens.", &a.shaders_al_arrancar, true, a.shaders_al_arrancar != o.shaders_al_arrancar);
        FILA_B("SHADER NOTICE", "Shows in the bottom left how many shaders are being compiled.", &a.aviso_shaders, false, false);
        break;
    case AJ_P_AUDIO:
        filas[nf++] = AjFila{ "MASTER VOLUME", "Volume of the whole game. Music and sound effects are set separately in the game's own options.", 2, &a.volumen, NULL, NULL, NULL, 0, false, false };
        break;
    case AJ_P_JUEGO:
        FILA_B("BOMB CAMERA", "Like the original: when you drop a bomb or a torpedo in cockpit view, the view from the projectile is shown on the canopy. When off, you keep looking forward.", &a.camara_bomba, false, false);
        FILA_B("RUMBLE", "Lets the controller vibrate when the game asks for it (crashes, shots, explosions).", &a.vibracion, false, false);
        break;
    case AJ_P_TRUCOS:
        for (int i = 0; i < NTRUCOS; i++)
            filas[nf++] = AjFila{ k_trucos[i].nombre, k_trucos[i].desc, 1, NULL, &a.trucos[i], NULL, NULL, 2, false, false, k_trucos[i].codigo };
        break;
    }
#undef FILA_L
#undef FILA_B
    if (st.sel >= nf) st.sel = nf - 1;
    if (st.sel < 0) st.sel = 0;

    /* teclado y joystick */
    if (aj_tecla(ImGuiKey_Q, ImGuiKey_GamepadL1)) { st.pest = (st.pest + AJ_NP - 1) % AJ_NP; st.sel = 0; st.desp = 0; }
    if (aj_tecla(ImGuiKey_E, ImGuiKey_GamepadR1)) { st.pest = (st.pest + 1) % AJ_NP; st.sel = 0; st.desp = 0; }
    if (aj_tecla(ImGuiKey_UpArrow, ImGuiKey_GamepadDpadUp, ImGuiKey_GamepadLStickUp)) st.sel = (st.sel + nf - 1) % nf;
    if (aj_tecla(ImGuiKey_DownArrow, ImGuiKey_GamepadDpadDown, ImGuiKey_GamepadLStickDown)) st.sel = (st.sel + 1) % nf;
    if (aj_tecla(ImGuiKey_LeftArrow, ImGuiKey_GamepadDpadLeft, ImGuiKey_GamepadLStickLeft)) { aj_mover(filas[st.sel], -1); cambio = true; }
    if (aj_tecla(ImGuiKey_RightArrow, ImGuiKey_GamepadDpadRight, ImGuiKey_GamepadLStickRight) || aj_tecla(ImGuiKey_Enter, ImGuiKey_GamepadFaceDown)) { aj_mover(filas[st.sel], +1); cambio = true; }
    if (aj_tecla(ImGuiKey_Escape, ImGuiKey_GamepadFaceRight)) st.cerrar = true;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    float esc = k;
#define Z(f) ((f)->LegacySize * k)
    ImFont* f_tit = s_f_grande; ImFont* f_pes = s_f_titulo; ImFont* f_fila = s_f_medio; ImFont* f_txt = s_f_normal; ImFont* f_chico = s_f_chico;
    float cx = org.x + tam.x * 0.5f;
    float y = org.y + 26 * esc;

    /* titulo */
    {
        const char* t = "OPTIONS";
        ImVec2 ts = f_tit->CalcTextSizeA(Z(f_tit), FLT_MAX, 0, t);
        aj_texto_sombra(dl, f_tit, Z(f_tit), ImVec2(cx - ts.x * 0.5f, y), AJ_AMARILLO, t);
        y += ts.y + 18 * esc;
    }
    /* pestanas */
    {
        float total = 0, sep = 46 * esc;
        for (int i = 0; i < AJ_NP; i++) total += f_pes->CalcTextSizeA(Z(f_pes), FLT_MAX, 0, k_aj_pest[i]).x + (i ? sep : 0);
        float x = cx - total * 0.5f, h = Z(f_pes);
        dl->AddRectFilled(ImVec2(x - 60, y - 10), ImVec2(x + total + 60, y + h + 14), IM_COL32(20, 34, 72, 150), 6);
        aj_triangulo(dl, ImVec2(x - 36, y + h * 0.5f + 2), 8, false, AJ_AMARILLO);
        aj_triangulo(dl, ImVec2(x + total + 36, y + h * 0.5f + 2), 8, true, AJ_AMARILLO);
        for (int i = 0; i < AJ_NP; i++) {
            ImVec2 ts = f_pes->CalcTextSizeA(Z(f_pes), FLT_MAX, 0, k_aj_pest[i]);
            ImGui::SetCursorScreenPos(ImVec2(x, y - 4));
            ImGui::PushID(100 + i);
            if (ImGui::InvisibleButton("pest", ImVec2(ts.x, ts.y + 10))) { st.pest = i; st.sel = 0; st.desp = 0; }
            bool sobre = ImGui::IsItemHovered();
            ImGui::PopID();
            dl->AddText(f_pes, Z(f_pes), ImVec2(x, y), i == st.pest ? AJ_AMARILLO : sobre ? AJ_TEXTO : AJ_TENUE, k_aj_pest[i]);
            if (i == st.pest) dl->AddRectFilled(ImVec2(x, y + ts.y + 4), ImVec2(x + ts.x, y + ts.y + 7), AJ_AMARILLO, 1);
            x += ts.x + sep;
        }
        y += h + 34 * esc;
    }
    /* panel */
    float pw = tam.x - 80 < 1180 * esc ? tam.x - 80 : 1180 * esc;
    float pie = 46 * esc;
    float fh = 62 * esc, cu = 22 * esc;
    float alto = 36 + 6 * fh;   /* siempre del alto de 6 opciones: no salta al cambiar de pestana */
    if (alto > org.y + tam.y - pie - 10 - y) alto = org.y + tam.y - pie - 10 - y;
    ImVec2 p0(cx - pw * 0.5f, y), p1(cx + pw * 0.5f, y + alto);
    dl->AddRectFilled(p0, p1, AJ_PANEL, 14);
    dl->AddRect(p0, p1, AJ_DORADO, 14, 0, 1.5f);
    float lw = (p1.x - p0.x) * 0.58f;
    dl->AddLine(ImVec2(p0.x + lw, p0.y + 1), ImVec2(p0.x + lw, p1.y - 1), IM_COL32(201, 168, 76, 150), 1.0f);
    float ly = p0.y + 18;
    int vis = (int)((alto - 36) / fh);   /* filas que entran; las demas se ven moviendo la lista */
    if (vis < 1) vis = 1;
    if (ImGui::IsMouseHoveringRect(p0, ImVec2(p0.x + lw, p1.y)) && ImGui::GetIO().MouseWheel != 0)
        st.desp -= ImGui::GetIO().MouseWheel > 0 ? 1 : -1;
    else {
        if (st.sel < st.desp) st.desp = st.sel;
        if (st.sel >= st.desp + vis) st.desp = st.sel - vis + 1;
    }
    if (st.desp > nf - vis) st.desp = nf - vis;
    if (st.desp < 0) st.desp = 0;
    if (nf > vis) {   /* barrita de desplazamiento */
        float bx = p0.x + lw - 8, by0 = p0.y + 18, bh = alto - 36;
        dl->AddRectFilled(ImVec2(bx, by0), ImVec2(bx + 3, by0 + bh), IM_COL32(40, 50, 80, 255), 2);
        dl->AddRectFilled(ImVec2(bx, by0 + bh * st.desp / nf), ImVec2(bx + 3, by0 + bh * (st.desp + vis) / nf), AJ_DORADO, 2);
    }
    for (int i = st.desp; i < nf && i < st.desp + vis; i++) {
        AjFila& f = filas[i];
        ImVec2 r0(p0.x + 14, ly), r1(p0.x + lw - 14, ly + fh - 8);
        ImGui::SetCursorScreenPos(r0);
        ImGui::PushID(i);
        ImGui::InvisibleButton("fila", ImVec2(r1.x - r0.x, r1.y - r0.y));
        bool sobre = ImGui::IsItemHovered();
        if (sobre && (ImGui::GetIO().MouseDelta.x != 0 || ImGui::GetIO().MouseDelta.y != 0)) st.sel = i;
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            st.sel = i;
            if (f.tipo == 2) {   /* la barra: se elige el valor con el mouse */
                float bx0 = r1.x - 20 - 10 * cu, mx = ImGui::GetIO().MousePos.x;
                if (mx >= bx0 - 10) { int v = (int)((mx - bx0) / cu + 1) * 10; *f.v = v < 0 ? 0 : v > 100 ? 100 : v; cambio = true; }
            } else { aj_mover(f, +1); cambio = true; }
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) { st.sel = i; aj_mover(f, -1); cambio = true; }
        ImGui::PopID();
        bool s = i == st.sel;
        if (s) {
            dl->AddRectFilled(r0, r1, AJ_SELEC, 10);
            dl->AddRect(r0, r1, AJ_AMARILLO, 10, 0, 1.5f);
        }
        float ty = (r0.y + r1.y) * 0.5f - Z(f_fila) * 0.55f;
        dl->AddText(f_fila, Z(f_fila), ImVec2(r0.x + 18, ty), s ? AJ_AMARILLO : AJ_TEXTO, f.t);
        if (f.tipo == 2) {   /* barra de 10 cuadraditos, como en las opciones de la trilogia */
            float bx = r1.x - 20 - 10 * cu, by = (r0.y + r1.y) * 0.5f - cu * 0.41f;
            int llenos = *f.v / 10;
            for (int q = 0; q < 10; q++)
                dl->AddRectFilled(ImVec2(bx + q * cu, by), ImVec2(bx + q * cu + cu * 0.77f, by + cu * 0.82f),
                                  q < llenos ? IM_COL32(120 + q * 8, 190 + q * 4, 255, 255) : IM_COL32(40, 46, 66, 255), 2);
            char pc[16]; snprintf(pc, sizeof pc, "%d%%", *f.v);
            ImVec2 ts = f_chico->CalcTextSizeA(Z(f_chico), FLT_MAX, 0, pc);
            dl->AddText(f_chico, Z(f_chico), ImVec2(bx - ts.x - 14, by), AJ_TENUE, pc);
        } else {
            const char* v = aj_valor(f);
            ImVec2 ts = f_fila->CalcTextSizeA(Z(f_fila), FLT_MAX, 0, v);
            float vx = r1.x - 34 * esc - ts.x;
            dl->AddText(f_fila, Z(f_fila), ImVec2(vx, ty), f.tipo == 1 && !*f.b ? IM_COL32(200, 170, 80, 255) : AJ_AMARILLO, v);
            if (s) {
                aj_triangulo(dl, ImVec2(vx - 16 * esc, ty + Z(f_fila) * 0.55f), 6 * esc, false, AJ_AMARILLO);
                aj_triangulo(dl, ImVec2(r1.x - 18 * esc, ty + Z(f_fila) * 0.55f), 6 * esc, true, AJ_AMARILLO);
            }
        }
        if (st.en_juego && f.reinicio && f.cambiado)   /* punto naranja: se aplica al reiniciar */
            dl->AddCircleFilled(ImVec2(r0.x + 8, (r0.y + r1.y) * 0.5f), 4 * esc, IM_COL32(255, 150, 40, 255));
        ly += fh;
    }
    /* explicacion de la opcion elegida */
    if (nf > 0) {
        const AjFila& f = filas[st.sel];
        float dx = p0.x + lw + 28, dw = p1.x - dx - 28, dy = p0.y + 26;
        dl->AddText(f_fila, Z(f_fila), ImVec2(dx, dy), AJ_AMARILLO, f.t);
        dy += Z(f_fila) + 14;
        dl->AddText(f_txt, Z(f_txt), ImVec2(dx, dy), AJ_TEXTO, f.desc, NULL, dw);
        dy += f_txt->CalcTextSizeA(Z(f_txt), FLT_MAX, dw, f.desc).y + 18;
        if (f.codigo) {
            char cod[96]; snprintf(cod, sizeof cod, "Original code: %s", f.codigo);
            dl->AddText(f_txt, Z(f_txt), ImVec2(dx, dy), AJ_TENUE, cod, NULL);
            dy += Z(f_txt) + 18;
            const char* n = st.en_juego ? "Applies right away. Turning it off removes nothing: your save stays as it is."
                                        : "Turning it off removes nothing: your save stays as it is.";
            dl->AddText(f_txt, Z(f_txt), ImVec2(dx, dy), AJ_TENUE, n, NULL, dw);
        }
        if (f.reinicio) {
            const char* r = st.en_juego ? (f.cambiado ? "Changes the next time you open the game." : "This change applies when you reopen the game.")
                                        : "Applies when the game starts.";
            dl->AddText(f_txt, Z(f_txt), ImVec2(dx, dy), st.en_juego && f.cambiado ? IM_COL32(255, 160, 60, 255) : AJ_TENUE, r, NULL, dw);
        }
    }
    /* ayuda abajo */
    {
        const char* ayuda = st.en_juego ? "UP / DOWN  SELECT   ·   LEFT / RIGHT  CHANGE   ·   Q / E  or  L1 / R1  TAB   ·   ESC  or  B  BACK TO GAME"
                                        : "UP / DOWN  SELECT   ·   LEFT / RIGHT  CHANGE   ·   Q / E  or  L1 / R1  TAB";
        ImVec2 ts = f_chico->CalcTextSizeA(Z(f_chico), FLT_MAX, 0, ayuda);
        dl->AddText(f_chico, Z(f_chico), ImVec2(cx - ts.x * 0.5f, p1.y + 22 * esc), AJ_TENUE, ayuda);
    }
    a.panoramico = pan == 1;
#undef Z
    return cambio;
}
