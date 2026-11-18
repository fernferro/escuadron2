/* Estilo compartido del launcher y del instalador (Dear ImGui). Incluir despues de imgui.h y SDL_opengl.h. */
#pragma once

/* ---- estilo ------------------------------------------------------------------------ */

static ImFont *s_f_normal, *s_f_medio, *s_f_titulo, *s_f_grande, *s_f_chico;
static const ImVec4 ACENTO(1.00f, 0.83f, 0.15f, 1.00f);   /* acento amarillo (titulos) */
static const ImU32 C_ACENTO = IM_COL32(255, 212, 38, 255);
static const ImU32 C_PANEL = IM_COL32(10, 15, 32, 235);  /* paneles azul noche con borde dorado */
static const ImU32 C_BORDE = IM_COL32(201, 168, 76, 200);
static const ImU32 C_FILA = IM_COL32(6, 9, 20, 255);
static const ImU32 C_TENUE = IM_COL32(150, 156, 178, 255);

static void estilo() {
    ImGuiStyle& s = ImGui::GetStyle();
    ImVec4* c = s.Colors;
    ImGui::StyleColorsDark();
    s.WindowRounding = 0; s.FrameRounding = 8; s.ChildRounding = 12; s.GrabRounding = 8; s.PopupRounding = 8;
    s.ScrollbarRounding = 8; s.TabRounding = 8;
    s.FramePadding = ImVec2(14, 9); s.ItemSpacing = ImVec2(12, 10); s.WindowPadding = ImVec2(0, 0);
    s.ScrollbarSize = 10; s.GrabMinSize = 18;
    c[ImGuiCol_WindowBg] = ImVec4(0.02f, 0.03f, 0.07f, 1);
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = ImVec4(0.06f, 0.08f, 0.16f, 1);
    c[ImGuiCol_Text] = ImVec4(0.93f, 0.93f, 0.95f, 1);
    c[ImGuiCol_TextDisabled] = ImVec4(0.59f, 0.58f, 0.63f, 1);
    c[ImGuiCol_FrameBg] = ImVec4(0.03f, 0.045f, 0.10f, 1);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.14f, 0.13f, 0.16f, 1);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.16f, 0.15f, 0.18f, 1);
    c[ImGuiCol_Button] = ImVec4(0.10f, 0.15f, 0.30f, 1);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.15f, 0.22f, 0.42f, 1);
    c[ImGuiCol_ButtonActive] = ImVec4(0.19f, 0.27f, 0.50f, 1);
    c[ImGuiCol_Header] = ImVec4(0.10f, 0.15f, 0.30f, 1);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.15f, 0.22f, 0.42f, 1);
    c[ImGuiCol_HeaderActive] = ImVec4(0.28f, 0.27f, 0.31f, 1);
    c[ImGuiCol_SliderGrab] = ImVec4(0.95f, 0.95f, 0.97f, 1);
    c[ImGuiCol_SliderGrabActive] = ImVec4(1, 1, 1, 1);
    c[ImGuiCol_CheckMark] = ACENTO;
    c[ImGuiCol_Separator] = ImVec4(0.2f, 0.19f, 0.22f, 1);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(0.25f, 0.24f, 0.28f, 1);
    c[ImGuiCol_Border] = ImVec4(0.2f, 0.19f, 0.22f, 1);
}

/* interruptor tipo celular */
static bool interruptor(const char* id, bool* v) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float h = 30, w = 56, r = h * 0.5f;
    bool cambio = false;
    ImGui::InvisibleButton(id, ImVec2(w, h));
    if (ImGui::IsItemClicked()) { *v = !*v; cambio = true; }
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), *v ? C_ACENTO : IM_COL32(44, 52, 78, 255), r);
    dl->AddCircleFilled(ImVec2(*v ? p.x + w - r : p.x + r, p.y + r), r - 4, IM_COL32(20, 19, 23, 255));
    return cambio;
}

static bool boton_naranja(const char* t, ImVec2 tam = ImVec2(0, 0)) {
    ImGui::PushStyleColor(ImGuiCol_Button, ACENTO);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.90f, 0.42f, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.85f, 0.68f, 0.08f, 1));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.03f, 0.04f, 0.09f, 1));
    bool r = ImGui::Button(t, tam);
    ImGui::PopStyleColor(4);
    return r;
}

/* panel redondeado con titulo y subtitulo; hay que cerrarlo con fin_panel() */
static void panel(const char* titulo, const char* sub, float ancho) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->ChannelsSplit(2);
    dl->ChannelsSetCurrent(1);
    ImGui::BeginGroup();
    ImGui::Dummy(ImVec2(ancho, 8));
    ImGui::Indent(24);
    if (titulo) { ImGui::PushFont(s_f_medio); ImGui::PushStyleColor(ImGuiCol_Text, ACENTO); ImGui::TextUnformatted(titulo); ImGui::PopStyleColor(); ImGui::PopFont(); }
    if (sub) { ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]); ImGui::TextWrapped("%s", sub); ImGui::PopStyleColor(); }
    ImGui::Dummy(ImVec2(0, 4));
}
static void fin_panel(float ancho) {
    ImGui::Unindent(24);
    ImGui::Dummy(ImVec2(ancho, 8));
    ImGui::EndGroup();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->ChannelsSetCurrent(0);
    ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    dl->AddRectFilled(a, ImVec2(a.x + ancho, b.y), C_PANEL, 14);
    dl->AddRect(a, ImVec2(a.x + ancho, b.y), C_BORDE, 14, 0, 1.2f);
    dl->ChannelsMerge();
    ImGui::Dummy(ImVec2(0, 10));
}

/* renglon de ajuste: texto a la izquierda, control a la derecha (devuelve x del control) */
static void renglon(const char* t, const char* sub, float ancho, float ancho_control, float alto_control = 30) {
    float y0 = ImGui::GetCursorPosY();
    ImGui::BeginGroup();
    ImGui::TextUnformatted(t);
    if (sub) { ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]); ImGui::TextUnformatted(sub); ImGui::PopStyleColor(); }
    ImGui::EndGroup();
    float alto = ImGui::GetItemRectSize().y;
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorStartPos().x + ancho - 24 - ancho_control);
    ImGui::SetCursorPosY(y0 + (alto - alto_control) * 0.5f);
}

/* ---- fondo: estrellas ---------------------------------------------------------------- */

static void fondo_estrellas(ImDrawList* dl, ImVec2 a, ImVec2 b, float t) {
    dl->AddRectFilledMultiColor(a, b, IM_COL32(8, 12, 30, 255), IM_COL32(12, 14, 34, 255), IM_COL32(3, 4, 12, 255), IM_COL32(4, 5, 14, 255));
    uint32_t s = 12345;
    for (int i = 0; i < 260; i++) {
        s = s * 1664525u + 1013904223u; float x = a.x + (s >> 8) % 10000 / 10000.0f * (b.x - a.x);
        s = s * 1664525u + 1013904223u; float y = a.y + (s >> 8) % 10000 / 10000.0f * (b.y - a.y);
        s = s * 1664525u + 1013904223u; float r = 0.6f + (s >> 8) % 100 / 100.0f * 1.4f;
        float br = 0.5f + 0.5f * SDL_sinf(t * 1.3f + (float)i);
        dl->AddCircleFilled(ImVec2(x, y), r, IM_COL32(220, 225, 255, (int)(60 + 140 * br)));
    }
    /* resplandor dorado abajo a la derecha */
    for (int k = 0; k < 48; k++) {
        float rr = 420.0f - k * 8.5f;
        dl->AddCircleFilled(ImVec2(b.x - 80, b.y + 60), rr, IM_COL32(230, 170, 40, 2), 96);
    }
}


/* ---- captura de pantalla para pruebas ---------------------------------------------- */

static void guardar_bmp(const char* path, int w, int h) {
    std::vector<uint8_t> px((size_t)w * h * 4);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    FILE* f = fopen(path, "wb");
    if (!f) return;
    int fila = w * 3, pad = (4 - fila % 4) % 4, tam = (fila + pad) * h;
    uint8_t hd[54] = { 'B', 'M' };
    *(uint32_t*)(hd + 2) = 54 + tam; *(uint32_t*)(hd + 10) = 54; *(uint32_t*)(hd + 14) = 40;
    *(int32_t*)(hd + 18) = w; *(int32_t*)(hd + 22) = h; *(uint16_t*)(hd + 26) = 1; *(uint16_t*)(hd + 28) = 24;
    *(uint32_t*)(hd + 34) = tam;
    fwrite(hd, 1, 54, f);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) { uint8_t* p = &px[((size_t)y * w + x) * 4]; uint8_t b[3] = { p[2], p[1], p[0] }; fwrite(b, 1, 3, f); }
        uint8_t z[3] = { 0, 0, 0 }; fwrite(z, 1, pad, f);
    }
    fclose(f);
}


/* ---- imagenes (arte opcional: carpeta arte/, nunca va en el repo) ---------------------- */

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#include "stb/stb_image.h"

struct Imagen { GLuint tex = 0; int w = 0, h = 0; };

static Imagen imagen_de_memoria(const unsigned char* d, int n) {
    Imagen im;
    int c;
    unsigned char* px = stbi_load_from_memory(d, n, &im.w, &im.h, &c, 4);
    if (!px) return Imagen();
    glGenTextures(1, &im.tex);
    glBindTexture(GL_TEXTURE_2D, im.tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, im.w, im.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    {   /* mipmaps: una foto de 1920 achicada a la ventana sin ellos se ve serruchada ("pixelada") */
        typedef void (APIENTRY * GenMip)(GLenum);
        GenMip gen = (GenMip)SDL_GL_GetProcAddress("glGenerateMipmap");
        if (gen) gen(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gen ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    }
    stbi_image_free(px);
    return im;
}

static Imagen imagen_de_archivo(const std::string& ruta) {
    FILE* f = fopen(ruta.c_str(), "rb");
    std::vector<unsigned char> d;
    long n;
    if (!f) return Imagen();
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n > 0) { d.resize((size_t)n); if (fread(d.data(), 1, d.size(), f) != d.size()) d.clear(); }
    fclose(f);
    return d.empty() ? Imagen() : imagen_de_memoria(d.data(), (int)d.size());
}

/* dibuja la imagen llenando el rectangulo (recorta lo que sobra, como "cover" en CSS) */
static void dibujar_cubrir(ImDrawList* dl, const Imagen& im, ImVec2 a, ImVec2 b, int alfa = 255, float foco_x = 0.5f, float foco_y = 0.5f) {
    if (!im.tex) return;
    float rw = b.x - a.x, rh = b.y - a.y, ia = (float)im.w / im.h, ra = rw / rh;
    ImVec2 u0(0, 0), u1(1, 1);
    if (ia > ra) { float f = ra / ia; u0.x = (1 - f) * foco_x; u1.x = u0.x + f; }
    else { float f = ia / ra; u0.y = (1 - f) * foco_y; u1.y = u0.y + f; }
    dl->AddImage((ImTextureID)(intptr_t)im.tex, a, b, u0, u1, IM_COL32(255, 255, 255, alfa));
}

/* oscurece para que se lea el texto encima de una imagen */
static void velo(ImDrawList* dl, ImVec2 a, ImVec2 b) {
    float w = b.x - a.x, h = b.y - a.y;
    dl->AddRectFilledMultiColor(a, ImVec2(a.x + w * 0.7f, b.y), IM_COL32(8, 7, 12, 215), IM_COL32(8, 7, 12, 40), IM_COL32(8, 7, 12, 40), IM_COL32(8, 7, 12, 215));
    dl->AddRectFilled(ImVec2(a.x + w * 0.7f, a.y), b, IM_COL32(8, 7, 12, 40));
    dl->AddRectFilledMultiColor(ImVec2(a.x, a.y + h * 0.45f), b, IM_COL32(8, 7, 12, 0), IM_COL32(8, 7, 12, 0), IM_COL32(8, 7, 12, 235), IM_COL32(8, 7, 12, 235));
    dl->AddRectFilledMultiColor(a, ImVec2(b.x, a.y + 110), IM_COL32(8, 7, 12, 190), IM_COL32(8, 7, 12, 190), IM_COL32(8, 7, 12, 0), IM_COL32(8, 7, 12, 0));
}

/* nave propia del proyecto (misma forma que port/tools/nave.py), centro c y radio r */
static void dibujar_nave(ImDrawList* dl, ImVec2 c, float r) {
    static const float cuerpo[][2] = { {0,-1},{0.11f,-0.62f},{0.15f,0.55f},{0.09f,0.86f},{-0.09f,0.86f},{-0.15f,0.55f},{-0.11f,-0.62f} };
    static const float ala[][2] = { {0.13f,-0.02f},{0.92f,0.42f},{0.98f,0.62f},{0.14f,0.46f} };
    static const float aleta[][2] = { {0.82f,0.30f},{0.98f,0.08f},{1.0f,0.66f},{0.9f,0.62f} };
    static const float canard[][2] = { {0.1f,-0.42f},{0.36f,-0.2f},{0.34f,-0.12f},{0.11f,-0.24f} };
    static const float cabina[][2] = { {0,-0.66f},{0.06f,-0.42f},{0,-0.3f},{-0.06f,-0.42f} };
    const ImU32 blanco = IM_COL32(238, 238, 232, 255), rojo = C_ACENTO;
    auto poly = [&](const float (*p)[2], int n, float sx, ImU32 col) {
        ImVec2 q[8];
        for (int i = 0; i < n; i++) q[i] = ImVec2(c.x + sx * p[i][0] * r, c.y + p[i][1] * r);
        if (sx < 0) for (int i = 0; i < n / 2; i++) std::swap(q[i], q[n - 1 - i]);
        dl->AddConvexPolyFilled(q, n, col);
    };
    for (int k = 0; k < 3; k++) dl->AddCircleFilled(ImVec2(c.x, c.y + 0.95f * r), r * (0.22f - 0.06f * k), IM_COL32(255, 60, 40, 60 + 50 * k));
    for (float sx : { 1.0f, -1.0f }) { poly(ala, 4, sx, blanco); poly(canard, 4, sx, blanco); poly(aleta, 4, sx, rojo); }
    {   /* el cuerpo no es convexo del todo: dos partes */
        ImVec2 q[7];
        for (int i = 0; i < 7; i++) q[i] = ImVec2(c.x + cuerpo[i][0] * r, c.y + cuerpo[i][1] * r);
        dl->AddConvexPolyFilled(q, 7, blanco);
    }
    poly(cabina, 4, 1, IM_COL32(30, 40, 60, 255));
    dl->AddLine(ImVec2(c.x, c.y - 0.15f * r), ImVec2(c.x, c.y + 0.7f * r), rojo, std::max(1.0f, r * 0.05f));
}
