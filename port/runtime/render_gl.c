/*
 * Backend de OpenGL 3.3 para GX (se activa con --gl).
 *
 * Reparte el trabajo asi:
 *   - CPU (este hilo grafico): lee los vertices y los transforma con el mismo codigo
 *     que el rasterizador por software (decode_vertex), decodifica texturas a RGBA
 *     (con cache), arma triangulos y traduce el estado de GX a estado de OpenGL.
 *   - GPU: rasteriza, ejecuta el TEV con un "ubershader" (un solo programa que lee
 *     la configuracion de las etapas desde un bloque de uniforms), prueba de alfa,
 *     Z y mezcla.
 * El EFB es un framebuffer de 640x528. Las copias a la XFB van a texturas que se
 * muestran en la ventana; las copias a textura quedan como texturas de OpenGL
 * registradas en la direccion de destino, y se usan cuando el juego dibuja con esa
 * direccion.
 *
 * Coordenadas: la fila y del EFB es la fila y de OpenGL (sin invertir). Solo se
 * invierte al mostrar la imagen en la ventana.
 */
#ifdef RL_GL

#include <SDL.h>
#include <pthread.h>
#include <sys/stat.h>
#include <time.h>
#include <errno.h>
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#include <shlobj.h>
#else
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "runtime.h"
#include "texturas_hd.h"
#include "os.h"
#include "gx.h"
#include "input.h"
#include "render_int.h"
#include "vtx.h"

#define GL_GLEXT_PROTOTYPES 0
#include <GL/glcorearb.h>

/* ---- carga de funciones de OpenGL ---------------------------------------- */

#define GL_FUNCS(X) \
    X(PFNGLGETSTRINGPROC, glGetString) X(PFNGLVIEWPORTPROC, glViewport) X(PFNGLCLEARCOLORPROC, glClearColor) \
    X(PFNGLCLEARPROC, glClear) X(PFNGLCLEARDEPTHPROC, glClearDepth) X(PFNGLENABLEPROC, glEnable) \
    X(PFNGLDISABLEPROC, glDisable) X(PFNGLBLENDFUNCSEPARATEPROC, glBlendFuncSeparate) \
    X(PFNGLBLENDEQUATIONSEPARATEPROC, glBlendEquationSeparate) X(PFNGLBLENDCOLORPROC, glBlendColor) \
    X(PFNGLDEPTHFUNCPROC, glDepthFunc) X(PFNGLDEPTHMASKPROC, glDepthMask) X(PFNGLCOLORMASKPROC, glColorMask) \
    X(PFNGLCULLFACEPROC, glCullFace) X(PFNGLFRONTFACEPROC, glFrontFace) X(PFNGLSCISSORPROC, glScissor) \
    X(PFNGLLOGICOPPROC, glLogicOp) X(PFNGLGENTEXTURESPROC, glGenTextures) X(PFNGLDELETETEXTURESPROC, glDeleteTextures) \
    X(PFNGLBINDTEXTUREPROC, glBindTexture) X(PFNGLTEXIMAGE2DPROC, glTexImage2D) X(PFNGLTEXIMAGE3DPROC, glTexImage3D) X(PFNGLTEXSUBIMAGE3DPROC, glTexSubImage3D) X(PFNGLTEXSUBIMAGE2DPROC, glTexSubImage2D) X(PFNGLCOMPRESSEDTEXIMAGE3DPROC, glCompressedTexImage3D) X(PFNGLGENERATEMIPMAPPROC, glGenerateMipmap) X(PFNGLFRAMEBUFFERTEXTURELAYERPROC, glFramebufferTextureLayer) X(PFNGLTEXPARAMETERIPROC, glTexParameteri) \
    X(PFNGLACTIVETEXTUREPROC, glActiveTexture) X(PFNGLPIXELSTOREIPROC, glPixelStorei) X(PFNGLREADPIXELSPROC, glReadPixels) \
    X(PFNGLGENFRAMEBUFFERSPROC, glGenFramebuffers) X(PFNGLBINDFRAMEBUFFERPROC, glBindFramebuffer) \
    X(PFNGLFRAMEBUFFERTEXTURE2DPROC, glFramebufferTexture2D) X(PFNGLCHECKFRAMEBUFFERSTATUSPROC, glCheckFramebufferStatus) \
    X(PFNGLBLITFRAMEBUFFERPROC, glBlitFramebuffer) X(PFNGLDELETEFRAMEBUFFERSPROC, glDeleteFramebuffers) \
    X(PFNGLGENBUFFERSPROC, glGenBuffers) X(PFNGLBINDBUFFERPROC, glBindBuffer) X(PFNGLBUFFERDATAPROC, glBufferData) \
    X(PFNGLGENVERTEXARRAYSPROC, glGenVertexArrays) X(PFNGLBINDVERTEXARRAYPROC, glBindVertexArray) \
    X(PFNGLVERTEXATTRIBPOINTERPROC, glVertexAttribPointer) X(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray) \
    X(PFNGLDRAWARRAYSPROC, glDrawArrays) X(PFNGLCREATESHADERPROC, glCreateShader) X(PFNGLSHADERSOURCEPROC, glShaderSource) \
    X(PFNGLCOMPILESHADERPROC, glCompileShader) X(PFNGLGETSHADERIVPROC, glGetShaderiv) \
    X(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog) X(PFNGLCREATEPROGRAMPROC, glCreateProgram) \
    X(PFNGLATTACHSHADERPROC, glAttachShader) X(PFNGLLINKPROGRAMPROC, glLinkProgram) X(PFNGLGETPROGRAMIVPROC, glGetProgramiv) \
    X(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog) X(PFNGLUSEPROGRAMPROC, glUseProgram) \
    X(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation) X(PFNGLUNIFORM1IPROC, glUniform1i) \
    X(PFNGLUNIFORM4FPROC, glUniform4f) X(PFNGLUNIFORM4IPROC, glUniform4i) X(PFNGLGETERRORPROC, glGetError) \
    X(PFNGLBINDBUFFERBASEPROC, glBindBufferBase) X(PFNGLGETUNIFORMBLOCKINDEXPROC, glGetUniformBlockIndex) \
    X(PFNGLUNIFORMBLOCKBINDINGPROC, glUniformBlockBinding) X(PFNGLFINISHPROC, glFinish) \
    X(PFNGLBINDATTRIBLOCATIONPROC, glBindAttribLocation) X(PFNGLBINDFRAGDATALOCATIONPROC, glBindFragDataLocation) \
    X(PFNGLGENSAMPLERSPROC, glGenSamplers) X(PFNGLDRAWELEMENTSPROC, glDrawElements) X(PFNGLVERTEXATTRIB3FPROC, glVertexAttrib3f) X(PFNGLVERTEXATTRIB2FPROC, glVertexAttrib2f) X(PFNGLDISABLEVERTEXATTRIBARRAYPROC, glDisableVertexAttribArray) X(PFNGLSAMPLERPARAMETERIPROC, glSamplerParameteri) X(PFNGLSAMPLERPARAMETERFPROC, glSamplerParameterf) X(PFNGLBINDSAMPLERPROC, glBindSampler) \
    X(PFNGLVERTEXATTRIBIPOINTERPROC, glVertexAttribIPointer) X(PFNGLTEXBUFFERPROC, glTexBuffer) \
    X(PFNGLBUFFERSUBDATAPROC, glBufferSubData) X(PFNGLDRAWELEMENTSBASEVERTEXPROC, glDrawElementsBaseVertex) X(PFNGLBINDBUFFERRANGEPROC, glBindBufferRange) X(PFNGLMAPBUFFERRANGEPROC, glMapBufferRange) X(PFNGLGENQUERIESPROC, glGenQueries) X(PFNGLBEGINQUERYPROC, glBeginQuery) X(PFNGLENDQUERYPROC, glEndQuery) X(PFNGLGETQUERYOBJECTIVPROC, glGetQueryObjectiv) X(PFNGLGETQUERYOBJECTUI64VPROC, glGetQueryObjectui64v) X(PFNGLUNMAPBUFFERPROC, glUnmapBuffer) X(PFNGLGETINTEGERVPROC, glGetIntegerv) X(PFNGLGETFLOATVPROC, glGetFloatv) X(PFNGLDELETESHADERPROC, glDeleteShader) X(PFNGLDELETEPROGRAMPROC, glDeleteProgram) X(PFNGLFENCESYNCPROC, glFenceSync) X(PFNGLCLIENTWAITSYNCPROC, glClientWaitSync) X(PFNGLDELETESYNCPROC, glDeleteSync) \
    X(PFNGLGENRENDERBUFFERSPROC, glGenRenderbuffers) X(PFNGLBINDRENDERBUFFERPROC, glBindRenderbuffer) X(PFNGLRENDERBUFFERSTORAGEMULTISAMPLEPROC, glRenderbufferStorageMultisample) X(PFNGLFRAMEBUFFERRENDERBUFFERPROC, glFramebufferRenderbuffer)

#define DECL(t, n) static t p_##n;
GL_FUNCS(DECL)
#undef DECL

static void load_gl(void) {
#define LOAD(t, n) p_##n = (t)SDL_GL_GetProcAddress(#n); if (!p_##n) rt_fatal("OpenGL: falta %s\n", #n);
    GL_FUNCS(LOAD)
#undef LOAD
}

/* ---- estado ---------------------------------------------------------------- */

static SDL_Window* s_win;
static SDL_GLContext s_ctx;
static GLuint s_efb_fbo, s_efb_color, s_efb_depth;
/* Antialiasing (--msaa N): se dibuja en un EFB con N muestras por pixel (s_efb_fbo) y antes de
 * leerlo (copias a textura o a la XFB, lecturas del procesador) se resuelve a las texturas de
 * siempre (s_efb_res_fbo). Sin antialiasing s_efb_res_fbo == s_efb_fbo. */
static GLuint s_efb_res_fbo;
int gl_msaa;                     /* 0, 2, 4 u 8 */
int gl_aniso_nivel = -1;         /* filtrado anisotropico: -1 automatico (x8), 0 apagado, 2..16 */
int gl_filtro_salida;            /* 0 suave (bilineal), 1 nitido (pixeles) */
static GLuint s_prog, s_ubo, s_vao, s_vbo, s_ibo;
/* transformacion en la GPU */
static GLuint s_prog_z, s_xprog_z;
static GLuint s_xprog, s_xvao, s_xf_buf, s_xf_tex, s_xft_buf, s_xft_tex;
static int s_use_xf = 1;            /* RL_GL_XF=0 la apaga (se transforma en la CPU) */
/* Estado de XF en la GPU: el estado (832 palabras) se parte en 52 bloques de 16. Cada
 * estado es una tabla de 52 numeros de bloque (u_xft) y los bloques estan en u_xf; un
 * estado nuevo solo sube los bloques que cambiaron (casi siempre una matriz). Antes se
 * subian las 832 palabras enteras por cada cambio: ~16 MB por cuadro en el nivel 3. */
#define XF_BLOCKS 52
static uint32_t* s_snap;            /* tablas de los estados de este "turno" del buffer */
static uint32_t s_snap_n, s_snap_up, s_snap_max;   /* cuantos hay / cuantos ya se subieron / maximo */
static uint32_t* s_pool;            /* bloques */
static uint32_t s_pool_n, s_pool_up, s_pool_max;
static uint32_t s_cur_blk[XF_BLOCKS];
static int s_snap_last_valid;
static GLuint s_blit_prog, s_conv_prog, s_quad_vao;
static GLint s_blit_loc_rect, s_conv_loc_src, s_conv_loc_mode, s_conv_loc_mode2;
static GLuint s_white_tex;
static uint32_t s_frame;
int gl_vsync;
int gl_headless;                 /* sin ventana visible (pruebas) */
extern int gl_widescreen;        /* --panoramico (vtx.c) */
int gl_ws_aplastar(float p0, float p1, float p2);
int gl_fullscreen;               /* --pantalla-completa */
int gl_scale = 0;                /* resolucion interna: EFB de 640x528 por esto (--escala N); 0 = automatica */
static float s_aniso;            /* filtrado anisotropico (si la placa lo tiene) */
#define S_ (gl_scale)
static void flush_batch(void);
static void spec_init(void);
static void spec_prewarm(void);

static void gl_state_reset(void);
static void prewarm_inicial(void);
static void calentar_inicial(void);
static void prep_barra(float fr);
static void prep_eventos(void);
static void ring_init(void);
static int has_ext(const char* name);
static int s_applied_valid;

/* Bloque de uniforms (std140: todo en vec4/ivec4). */
typedef struct {
    float vp[4];        /* escala x, escala y, desplazamiento x, desplazamiento y (pixeles del EFB) */
    float vpz[4];       /* escala z, desplazamiento z */
    int32_t misc[4];    /* etapas, comparacion de alfa (BP 0xF3), -, - */
    int32_t stage[16][4];
    int32_t regs[4][4];
    int32_t konst[4][4];
    int32_t swap[4][4];
    float fogf[4];      /* a, c, -, - */
    int32_t fogi[4];    /* funcion (fsel), b (magnitud), proyeccion (0 perspectiva), b_shift */
    int32_t fogc[4];    /* color de la niebla */
    int32_t indc[4][4]; /* IND_CMD de cada etapa (texturas indirectas) */
    int32_t indr[4];    /* IREF, etapas indirectas, IND_SS0, IND_SS1 */
    int32_t indm[6][4]; /* matrices indirectas: fila s (ma, mc, me, corrimiento), fila t (mb, md, mf, corrimiento) */
    int32_t tdim[8][4]; /* ancho y alto de cada textura (solo con texturas indirectas) */
    float zs[4];        /* "zfreeze": plano de profundidad congelado (d/dx, d/dy, en el origen) en pixeles de la ventana */
} GxUniforms;

/* ---- shaders ----------------------------------------------------------------- */

static const char* VS_MAIN =
    "#version 330 core\n"
    "layout(std140) uniform Gx { vec4 vp; vec4 vpz; ivec4 misc; ivec4 stage[16]; ivec4 regs[4]; ivec4 konst[4]; ivec4 swp[4]; vec4 fogf; ivec4 fogi; ivec4 fogc; ivec4 indc[4]; ivec4 indr; ivec4 indm[6]; ivec4 tdim[8]; vec4 zs; };\n"
    "in vec4 a_pos; in vec4 a_c0; in vec4 a_c1;\n"
    "in vec3 a_t0; in vec3 a_t1; in vec3 a_t2; in vec3 a_t3; in vec3 a_t4; in vec3 a_t5; in vec3 a_t6; in vec3 a_t7;\n"
    "in uvec2 a_lay; flat out uvec2 v_lay;\n"
    "out vec4 v_c0; out vec4 v_c1;\n"
    "out vec3 v_t0; out vec3 v_t1; out vec3 v_t2; out vec3 v_t3; out vec3 v_t4; out vec3 v_t5; out vec3 v_t6; out vec3 v_t7;\n"
    "void main() {\n"
    "  float w = a_pos.w;\n"
    "  float sx = a_pos.x * vp.x + vp.z * w;\n"          /* pantalla * w */
    "  float sy = a_pos.y * vp.y + vp.w * w;\n"
    "  gl_Position = vec4(sx / 320.0 - w, sy / 264.0 - w, 2.0 * (a_pos.z * vpz.x + vpz.y * w) / 16777216.0 - w, w);\n"
    "  gl_ClipDistance[0] = a_pos.z + a_pos.w;\n"        /* plano cercano de GX: -w <= z */
    "  gl_ClipDistance[1] = -a_pos.z;\n"                 /* plano lejano de GX: z <= 0 (sin esto los vertices basura del juego tapan la pantalla) */
    "  v_c0 = a_c0 * 255.0; v_c1 = a_c1 * 255.0;\n"
    "  v_t0 = a_t0; v_t1 = a_t1; v_t2 = a_t2; v_t3 = a_t3; v_t4 = a_t4; v_t5 = a_t5; v_t6 = a_t6; v_t7 = a_t7;\n"
    "  v_lay = a_lay;\n"
    "}\n";

/* Transformacion en la GPU: lo mismo que vtx_decode (vtx.c) pero en el shader. El
 * estado de XF de cada dibujo (matrices, luces, registros) va en un "buffer de texturas"
 * (u_xf, en bloques; u_xft dice que bloques usa cada estado); cada vertice dice que estado usa. */
static const char* VS_XF =
    "#version 330 core\n"
    "layout(std140) uniform Gx { vec4 vp; vec4 vpz; ivec4 misc; ivec4 stage[16]; ivec4 regs[4]; ivec4 konst[4]; ivec4 swp[4]; vec4 fogf; ivec4 fogi; ivec4 fogc; ivec4 indc[4]; ivec4 indr; ivec4 indm[6]; ivec4 tdim[8]; vec4 zs; };\n"
    "uniform usamplerBuffer u_xf; uniform usamplerBuffer u_xft;\n"
    "in vec3 a_pos; in vec3 a_nrm; in vec4 a_c0; in vec4 a_c1; in uvec4 a_ia; in uvec4 a_ib;\n"
    "in vec2 a_r0; in vec2 a_r1; in vec2 a_r2; in vec2 a_r3; in vec2 a_r4; in vec2 a_r5; in vec2 a_r6; in vec2 a_r7;\n"
    "in uvec2 a_lay; flat out uvec2 v_lay;\n"
    "out vec4 v_c0; out vec4 v_c1;\n"
    "out vec3 v_t0; out vec3 v_t1; out vec3 v_t2; out vec3 v_t3; out vec3 v_t4; out vec3 v_t5; out vec3 v_t6; out vec3 v_t7;\n"
    "int sb;\n"
    "uvec4 T(int t) { uint b = texelFetch(u_xft, sb + (t >> 4))[(t >> 2) & 3]; return texelFetch(u_xf, int(b) * 4 + (t & 3)); }\n"
    "uint W(int w) { return T(w >> 2)[w & 3]; }\n"
    "float F(int w) { return uintBitsToFloat(W(w)); }\n"
    "vec4 ROW(int t) { return uintBitsToFloat(T(t)); }\n"
    "uint REG(int r) { return W(736 + r); }\n"
    "vec3 m34(int t, vec3 v) { vec4 r0 = ROW(t), r1 = ROW(t + 1), r2 = ROW(t + 2);\n"
    "  return vec3(r0.x * v.x + r0.y * v.y + r0.z * v.z + r0.w, r1.x * v.x + r1.y * v.y + r1.z * v.z + r1.w, r2.x * v.x + r2.y * v.y + r2.z * v.z + r2.w); }\n"
    "vec4 rgba(uint c) { return vec4(float((c >> 24) & 255u), float((c >> 16) & 255u), float((c >> 8) & 255u), float(c & 255u)); }\n"
    "vec2 raw(int t) { if (t == 0) return a_r0; if (t == 1) return a_r1; if (t == 2) return a_r2; if (t == 3) return a_r3;\n"
    "  if (t == 4) return a_r4; if (t == 5) return a_r5; if (t == 6) return a_r6; return a_r7; }\n"
    "uint tmi(int i) { uint w = i < 4 ? a_ia[2 + (i >> 1)] : a_ib[(i - 4) >> 1]; return (w >> (8u * uint(i & 1))) & 63u; }\n"
    "void main() {\n"
    "  v_lay = a_lay;\n"
    "  sb = int(a_ia.x) * 13;\n"
    "  int pmi = int(a_ia.y & 63u);\n"
    "  uint flags = a_ib.z;\n"
    "  bool has_n = (flags & 1u) != 0u;\n"
    "  vec3 ep = m34(pmi, a_pos);\n"
    "  vec4 o;\n"
    "  float p0 = F(736 + 0x20), p1 = F(736 + 0x21), p2 = F(736 + 0x22), p3 = F(736 + 0x23), p4 = F(736 + 0x24), p5 = F(736 + 0x25);\n"
    "  if (REG(0x26) == 0u) o = vec4(p0 * ep.x + p1 * ep.z, p2 * ep.y + p3 * ep.z, p4 * ep.z + p5, -ep.z);\n"
    "  else o = vec4(p0 * ep.x + p1, p2 * ep.y + p3, p4 * ep.z + p5, 1.0);\n"
    "  float w = o.w;\n"
    "  float sx = o.x * vp.x + vp.z * w;\n"
    "  float sy = o.y * vp.y + vp.w * w;\n"
    "  gl_Position = vec4(sx / 320.0 - w, sy / 264.0 - w, 2.0 * (o.z * vpz.x + vpz.y * w) / 16777216.0 - w, w);\n"
    "  gl_ClipDistance[0] = o.z + o.w;\n"
    "  gl_ClipDistance[1] = -o.z;\n"
    /* colores */
    "  vec3 n = vec3(0.0); bool have_n = false;\n"
    "  vec4 cols[2];\n"
    "  for (int i = 0; i < 2; i++) {\n"
    "    bool have_vc = (flags & (2u << uint(i))) != 0u;\n"
    "    vec4 vc = (i == 0 ? a_c0 : a_c1) * 255.0;\n"
    "    vec4 res = vec4(0.0);\n"
    "    for (int part = 0; part < 2; part++) {\n"
    "      uint ctrl = REG((part != 0 ? 0x10 : 0x0E) + i);\n"
    "      bool mat_vtx = (ctrl & 1u) != 0u, light = ((ctrl >> 1) & 1u) != 0u, amb_vtx = ((ctrl >> 6) & 1u) != 0u;\n"
    "      uint mask = ((ctrl >> 2) & 15u) | (((ctrl >> 11) & 15u) << 4);\n"
    "      uint diff = (ctrl >> 7) & 3u, atten = (ctrl >> 9) & 3u;\n"
    "      vec4 mat = (mat_vtx && have_vc) ? vc : rgba(REG(0x0C + i));\n"
    "      if (light && has_n) {\n"
    "        if (!have_n) {\n"
    "          int nb = 256 + (pmi & 31) * 3;\n"
    "          vec3 nn = vec3(F(nb) * a_nrm.x + F(nb + 1) * a_nrm.y + F(nb + 2) * a_nrm.z,\n"
    "                         F(nb + 3) * a_nrm.x + F(nb + 4) * a_nrm.y + F(nb + 5) * a_nrm.z,\n"
    "                         F(nb + 6) * a_nrm.x + F(nb + 7) * a_nrm.y + F(nb + 8) * a_nrm.z);\n"
    "          float len = dot(nn, nn);\n"
    "          n = len > 0.0 ? nn * inversesqrt(len) : nn;\n"
    "          have_n = true;\n"
    "        }\n"
    "        vec4 lit = (amb_vtx && have_vc) ? vc : rgba(REG(0x0A + i));\n"
    "        for (int li = 0; li < 8; li++) {\n"
    "          if ((mask & (1u << uint(li))) == 0u) continue;\n"
    "          int lt = 152 + li * 4;\n"
    "          vec4 L0 = ROW(lt + 1), L1 = ROW(lt + 2), L2 = ROW(lt + 3);\n"
    "          vec4 lcol = rgba(T(lt).w);\n"
    "          vec3 lpos = vec3(L1.z, L1.w, L2.x), ldir = L2.yzw;\n"
    "          vec3 lp = lpos - ep;\n"
    "          float dlen = sqrt(dot(lp, lp));\n"
    "          vec3 ld = dlen > 0.0 ? lp / dlen : vec3(0.0, 0.0, 1.0);\n"
    /* como Dolphin: funcion de atenuacion 1 = especular, 3 = foco, 0/2 = ninguna;
       difuso 0 = ninguno, 1 = con signo, 2 = recortado a 0 */
    "          float ndl = dot(n, ld), att = 1.0;\n"
    "          if (atten == 1u) {\n"
    "            float a = ndl >= 0.0 ? max(0.0, dot(n, ldir)) : 0.0;\n"
    "            vec3 da = vec3(L0.w, L1.x, L1.y); if (diff == 0u && dot(da, da) > 0.0) da = normalize(da);\n"
    "            float den = dot(da, vec3(1.0, a, a * a));\n"
    "            att = den != 0.0 ? max(0.0, L0.x + L0.y * a + L0.z * a * a) / den : 0.0;\n"
    "          } else if (atten == 3u) {\n"
    "            float a = max(0.0, dot(ld, ldir));\n"
    "            float den = L0.w + L1.x * dlen + L1.y * dlen * dlen;\n"
    "            att = den != 0.0 ? max(0.0, L0.x + L0.y * a + L0.z * a * a) / den : 0.0;\n"
    "          }\n"
    "          if (diff == 2u) ndl = max(ndl, 0.0); else if (diff == 0u) ndl = 1.0;\n"
    "          lit += lcol * (ndl * att);\n"
    "        }\n"
    "        mat = mat * clamp(lit, 0.0, 255.0) * (1.0 / 255.0);\n"
    "      }\n"
    "      if (part == 0) res.rgb = mat.rgb; else res.a = mat.a;\n"
    "    }\n"
    "    cols[i] = floor(clamp(res, 0.0, 255.0) + 0.5);\n"
    "  }\n"
    "  v_c0 = cols[0]; v_c1 = cols[1];\n"
    /* coordenadas de textura */
    "  vec3 tco[8];\n"
    "  int ntex = min(int(REG(0x3F) & 15u), 8);\n"
    "  bool dual = (REG(0x12) & 1u) != 0u;\n"
    "  for (int i = 0; i < 8; i++) {\n"
    "    tco[i] = vec3(0.0, 0.0, 1.0);\n"
    "    if (i >= ntex) continue;\n"
    "    uint tg = REG(0x40 + i);\n"
    "    uint type = (tg >> 4) & 7u, src = (tg >> 7) & 31u;\n"
    "    bool proj = ((tg >> 1) & 1u) != 0u;\n"
    "    vec3 inp = vec3(0.0, 0.0, 1.0), outp;\n"
    "    if (src == 0u) inp = a_pos; else if (src == 1u) inp = a_nrm;\n"
    "    else if (src >= 5u && src <= 12u) inp = vec3(raw(int(src) - 5), 1.0);\n"
    "    if (type == 0u) {\n"
    "      vec3 r = m34(int(tmi(i)), inp);\n"
    "      outp = vec3(r.xy, proj ? r.z : 1.0);\n"
    "    } else if (type == 1u) {\n"   /* relieve (emboss): la coordenada de otra etapa (sin tangentes el corrimiento es 0) */
    "      int es = int((tg >> 12) & 7u);\n"
    "      outp = es < i ? tco[es] : vec3(0.0, 0.0, 1.0);\n"
    "      tco[i] = outp; continue;\n"
    "    } else if (type == 2u) outp = vec3(cols[0].rg / 255.0, 1.0);\n"
    "    else if (type == 3u) outp = vec3(cols[1].rg / 255.0, 1.0);\n"
    "    else outp = vec3(inp.xy, 1.0);\n"
    "    if (dual && type == 0u) {\n"
    /* como la consola (y Dolphin): la matriz de post-transformacion va sobre (s,t,q) sin
       dividir; la division por q la hace despues el rasterizador */
    "      int pt = 88 + int(REG(0x50 + i) & 63u);\n"
    "      vec3 nn = proj ? outp : vec3(outp.xy, 1.0);\n"
    "      if ((REG(0x50 + i) & 256u) != 0u) nn = normalize(nn);\n"
    "      vec3 r = m34(pt, nn);\n"
    "      outp = vec3(r.xy, proj ? r.z : 1.0);\n"
    "    }\n"
    "    tco[i] = outp;\n"
    "  }\n"
    "  v_t0 = tco[0]; v_t1 = tco[1]; v_t2 = tco[2]; v_t3 = tco[3]; v_t4 = tco[4]; v_t5 = tco[5]; v_t6 = tco[6]; v_t7 = tco[7];\n"
    "}\n";

/* El TEV, igual que shade() en render.c pero en la GPU. */
/* El shader del TEV se arma con tres partes: lo de antes de las etapas, el cuerpo de una
 * etapa y lo de despues. El "ubershader" recorre las etapas leyendo su configuracion de los
 * uniforms; los shaders especializados repiten el cuerpo una vez por etapa con la
 * configuracion como constantes (el compilador elimina todo lo que no se usa: mucho mas
 * rapido en la placa, como hace Dolphin). */
static const char* FS_PRE =
    "layout(std140) uniform Gx { vec4 vp; vec4 vpz; ivec4 misc; ivec4 stage[16]; ivec4 regs[4]; ivec4 konst[4]; ivec4 swp[4]; vec4 fogf; ivec4 fogi; ivec4 fogc; ivec4 indc[4]; ivec4 indr; ivec4 indm[6]; ivec4 tdim[8]; vec4 zs; };\n"
    "uniform sampler2DArray u_tex0; uniform sampler2DArray u_tex1; uniform sampler2DArray u_tex2; uniform sampler2DArray u_tex3;\n"
    "uniform sampler2DArray u_tex4; uniform sampler2DArray u_tex5; uniform sampler2DArray u_tex6; uniform sampler2DArray u_tex7;\n"
    "flat in uvec2 v_lay;\n"
    "in vec4 v_c0; in vec4 v_c1;\n"
    "in vec3 v_t0; in vec3 v_t1; in vec3 v_t2; in vec3 v_t3; in vec3 v_t4; in vec3 v_t5; in vec3 v_t6; in vec3 v_t7;\n"
    "out vec4 o_color;\n"
    "vec3 tcoord(int i) {\n"
    "  if (i == 0) return v_t0; if (i == 1) return v_t1; if (i == 2) return v_t2; if (i == 3) return v_t3;\n"
    "  if (i == 4) return v_t4; if (i == 5) return v_t5; if (i == 6) return v_t6; return v_t7;\n"
    "}\n"
    "float tlay(int m) { uint w = m < 4 ? v_lay.x : v_lay.y; return float((w >> (8u * uint(m & 3))) & 255u); }\n"
    "vec4 tsample(int m, vec2 uv) {\n"
    "  vec3 c = vec3(uv, tlay(m));\n"
    "  if (m == 0) return texture(u_tex0, c); if (m == 1) return texture(u_tex1, c);\n"
    "  if (m == 2) return texture(u_tex2, c); if (m == 3) return texture(u_tex3, c);\n"
    "  if (m == 4) return texture(u_tex4, c); if (m == 5) return texture(u_tex5, c);\n"
    "  if (m == 6) return texture(u_tex6, c); return texture(u_tex7, c);\n"
    "}\n"
    "ivec4 swz(ivec4 c, int t) { ivec4 s = swp[t]; return ivec4(c[s.x], c[s.y], c[s.z], c[s.w]); }\n"
    "int kval(int sel, int comp) {\n"
    "  if (sel < 8) { int f[8] = int[8](255, 223, 191, 159, 128, 96, 64, 32); return f[sel]; }\n"
    "  if (sel >= 12 && sel <= 15) return konst[sel - 12][comp];\n"
    "  if (sel >= 16) return konst[(sel - 16) & 3][(sel - 16) >> 2];\n"
    "  return 255;\n"
    "}\n"
    "int half_div(int r) { return r < 0 ? -((-r) / 2) : r / 2; }\n"
    "int frag_z() {\n"
    "#ifdef ZFREEZE\n"
    "  return clamp(int(zs.z + zs.x * gl_FragCoord.x + zs.y * gl_FragCoord.y), 0, 16777215);\n"
    "#else\n"
    "  return min(int(gl_FragCoord.z * 16777215.0 + 0.5), 16777215);\n"
    "#endif\n"
    "}\n"
    "void main() {\n"
    "#ifdef ZFREEZE\n"
    "  gl_FragDepth = float(frag_z()) / 16777216.0;\n"
    "#endif\n"
    "  ivec4 R[4]; R[0] = regs[0]; R[1] = regs[1]; R[2] = regs[2]; R[3] = regs[3];\n"
    "  ivec4 outc = ivec4(0);\n"
    "#ifdef SPEC_NS\n  const int ns = SPEC_NS;\n#else\n  int ns = misc.x;\n#endif\n"
    /* texturas indirectas (como Dolphin): primero se leen las etapas indirectas */
    "  int nbmp = indr.y;\n"
    "  ivec3 indtex[4]; ivec2 tevc = ivec2(0);\n"
    "  for (int i = 0; i < 4; i++) {\n"
    "    indtex[i] = ivec3(0);\n"
    "    if (i < nbmp) {\n"
    "      int ref = (indr.x >> (6 * i)) & 63;\n"
    "      int sc = ((i < 2) ? indr.z : indr.w) >> (8 * (i & 1));\n"
    "      vec3 tc = tcoord((ref >> 3) & 7);\n"
    "      vec2 uv = (tc.z != 0.0 && tc.z != 1.0) ? tc.xy / tc.z : tc.xy;\n"
    "      uv *= vec2(1.0 / float(1 << (sc & 15)), 1.0 / float(1 << ((sc >> 4) & 15)));\n"
    "      indtex[i] = ivec3(tsample(ref & 7, uv).abg * 255.0 + 0.5);\n"
    "    }\n"
    "  }\n";
static const char* FS_BODY =
    "    int order = st.x, cenv = st.y, aenv = st.z;\n"
    "    int texmap = order & 7, texc_i = (order >> 3) & 7, texen = (order >> 6) & 1, chan = (order >> 7) & 7;\n"
    "    int kc = st.w & 31, ka = (st.w >> 8) & 31;\n"
    "    ivec4 texc = ivec4(255), rasc = ivec4(0);\n"
    "    int icmd = indc[s >> 2][s & 3];\n"
    "    bool ind = nbmp > 0 && (icmd & 3) < nbmp;\n"
    "    if (texen != 0 || ind) {\n"
    "      vec3 tc = tcoord(texc_i);\n"
    "      vec2 uv = (tc.z != 0.0 && tc.z != 1.0) ? tc.xy / tc.z : tc.xy;\n"
    "      if (nbmp > 0) {\n"
    "        vec2 dim = vec2(max(tdim[texmap].xy, ivec2(1))) * 128.0;\n"
    "        ivec2 fp = ivec2(floor(uv * dim));\n"
    "        if (ind) {\n"
    "          int fmt = (icmd >> 2) & 3, bias = (icmd >> 4) & 7, mid = (icmd >> 9) & 15;\n"
    "          int sw = (icmd >> 13) & 7, tw = (icmd >> 16) & 7, fb = (icmd >> 20) & 1;\n"
    "          ivec3 crd = indtex[icmd & 3] & ivec3(fmt == 0 ? 255 : fmt == 1 ? 31 : fmt == 2 ? 15 : 7);\n"
    "          int bv = fmt == 0 ? -128 : 1;\n"
    "          if ((bias & 1) != 0) crd.x += bv;\n"
    "          if ((bias & 2) != 0) crd.y += bv;\n"
    "          if ((bias & 4) != 0) crd.z += bv;\n"
    "          ivec2 tr = ivec2(0); int sh = 0;\n"
    "          if (mid >= 1 && mid <= 3) { ivec4 r0 = indm[2 * (mid - 1)], r1 = indm[2 * (mid - 1) + 1];\n"
    "            tr = ivec2(r0.x * crd.x + r0.y * crd.y + r0.z * crd.z, r1.x * crd.x + r1.y * crd.y + r1.z * crd.z) >> 3; sh = r0.w; }\n"
    "          else if (mid >= 5 && mid <= 7) { tr = (fp * crd.x) >> 8; sh = indm[2 * (mid - 5)].w; }\n"
    "          else if (mid >= 9 && mid <= 11) { tr = (fp * crd.y) >> 8; sh = indm[2 * (mid - 9)].w; }\n"
    "          tr = sh >= 0 ? (tr >> sh) : (tr << (-sh));\n"
    "          ivec2 wc = fp;\n"
    "          if (sw == 6) wc.x = 0; else if (sw != 0) wc.x &= (128 << (9 - sw)) - 1;\n"
    "          if (tw == 6) wc.y = 0; else if (tw != 0) wc.y &= (128 << (9 - tw)) - 1;\n"
    "          tevc = (fb != 0 ? tevc : ivec2(0)) + wc + tr;\n"
    "        } else tevc = fp;\n"
    "        uv = (vec2(tevc) + 0.5) / dim;\n"
    "      }\n"
    "      if (texen != 0) {\n"
    "        ivec4 t = ivec4(tsample(texmap, uv) * 255.0 + 0.5);\n"
    "        texc = swz(t, (aenv >> 2) & 3);\n"
    "      }\n"
    "    }\n"
    "    if (chan == 0) rasc = swz(ivec4(v_c0), aenv & 3);\n"
    "    else if (chan == 1) rasc = swz(ivec4(v_c1), aenv & 3);\n"
    "    ivec4 A, B, C, D;\n"
    "    for (int i = 0; i < 4; i++) {\n"
    "      int sel = (cenv >> (12 - 4 * i)) & 15;\n"
    "      ivec3 v;\n"
    "      if (sel < 8) v = ((sel & 1) == 0) ? R[sel >> 1].rgb : ivec3(R[sel >> 1].a);\n"
    "      else if (sel == 8) v = texc.rgb; else if (sel == 9) v = ivec3(texc.a);\n"
    "      else if (sel == 10) v = rasc.rgb; else if (sel == 11) v = ivec3(rasc.a);\n"
    "      else if (sel == 12) v = ivec3(255); else if (sel == 13) v = ivec3(128);\n"
    "      else if (sel == 14) v = ivec3(kval(kc, 0), kval(kc, 1), kval(kc, 2));\n"
    "      else v = ivec3(0);\n"
    "      int asel = (aenv >> (13 - 3 * i)) & 7, av;\n"
    "      if (asel < 4) av = R[asel].a; else if (asel == 4) av = texc.a; else if (asel == 5) av = rasc.a;\n"
    "      else if (asel == 6) av = kval(ka, 3); else av = 0;\n"
    "      if (i == 0) A = ivec4(v, av); else if (i == 1) B = ivec4(v, av); else if (i == 2) C = ivec4(v, av); else D = ivec4(v, av);\n"
    "    }\n"
    "    ivec4 res;\n"
    "    for (int k = 0; k < 4; k++) {\n"
    "      int env = k < 3 ? cenv : aenv;\n"
    "      int bias = (env >> 16) & 3, sub = (env >> 18) & 1, clampv = (env >> 19) & 1, scale = (env >> 20) & 3;\n"
    "      int va = A[k] & 255, vb = B[k] & 255, vc = C[k] & 255, vd = D[k];\n"
    "      int r;\n"
    "      if (bias == 3) {\n"
    "        int op = (scale << 1) | sub; bool cmp;\n"
    "        int a0 = A.r & 255, b0 = B.r & 255;\n"
    "        int a1 = a0 | ((A.g & 255) << 8), b1 = b0 | ((B.g & 255) << 8);\n"
    "        int a2 = a1 | ((A.b & 255) << 16), b2 = b1 | ((B.b & 255) << 16);\n"
    "        if (op == 0) cmp = a0 > b0; else if (op == 1) cmp = a0 == b0;\n"
    "        else if (op == 2) cmp = a1 > b1; else if (op == 3) cmp = a1 == b1;\n"
    "        else if (op == 4) cmp = a2 > b2; else if (op == 5) cmp = a2 == b2;\n"
    "        else if (op == 6) cmp = va > vb; else cmp = va == vb;\n"
    "        r = vd + (cmp ? vc : 0);\n"
    "      } else {\n"
    "        int c2 = vc + (vc >> 7);\n"
    "        int l = (va * (256 - c2) + vb * c2) >> 8;\n"
    "        r = sub != 0 ? vd - l : vd + l;\n"
    "        if (bias == 1) r += 128; else if (bias == 2) r -= 128;\n"
    "        if (scale == 1) r *= 2; else if (scale == 2) r *= 4; else if (scale == 3) r = half_div(r);\n"
    "      }\n"
    "      r = clampv != 0 ? clamp(r, 0, 255) : clamp(r, -1024, 1023);\n"
    "      res[k] = r;\n"
    "    }\n"
    "    int dc = (cenv >> 22) & 3, da = (aenv >> 22) & 3;\n"
    "    R[dc].rgb = res.rgb; R[da].a = res.a;\n"
    "    if (s == ns - 1) outc = ivec4(clamp(R[dc].rgb, 0, 255), clamp(R[da].a, 0, 255));\n";
static const char* FS_POST =
    "  if (fogi.x >= 2) {\n"                               /* niebla de GX (como Dolphin) */
    "    int zc = frag_z();\n"
    "    float ze;\n"
    "    if (fogi.z == 0) { float den = float(fogi.y - (zc >> fogi.w)); ze = den != 0.0 ? (fogf.x * 16777216.0) / den : 0.0; }\n"
    "    else ze = fogf.x * float(zc) / 16777216.0;\n"
    "    float fog = clamp(ze - fogf.y, 0.0, 1.0);\n"
    "    if (fogi.x == 4) fog = 1.0 - exp2(-8.0 * fog);\n"
    "    else if (fogi.x == 5) fog = 1.0 - exp2(-8.0 * fog * fog);\n"
    "    else if (fogi.x == 6) fog = exp2(-8.0 * (1.0 - fog));\n"
    "    else if (fogi.x == 7) { fog = 1.0 - fog; fog = exp2(-8.0 * fog * fog); }\n"
    "    int ifog = int(fog * 256.0 + 0.5);\n"
    "    outc.rgb = (outc.rgb * (256 - ifog) + fogc.rgb * ifog) >> 8;\n"
    "  }\n"
    "#ifndef SPEC_AC_SIEMPRE\n"     /* si la prueba de alfa siempre pasa, sin discard: la placa puede descartar por Z antes */
    "  int ac = misc.y;\n"
    "  int al = outc.a;\n"
    "  bool p[2];\n"
    "  for (int q = 0; q < 2; q++) {\n"
    "    int fv = (ac >> (16 + 3 * q)) & 7, rv = (ac >> (8 * q)) & 255;\n"
    "    p[q] = fv == 0 ? false : fv == 1 ? al < rv : fv == 2 ? al == rv : fv == 3 ? al <= rv : fv == 4 ? al > rv : fv == 5 ? al != rv : fv == 6 ? al >= rv : true;\n"
    "  }\n"
    "  int logic = (ac >> 22) & 3;\n"
    "  bool pass = logic == 0 ? (p[0] && p[1]) : logic == 1 ? (p[0] || p[1]) : logic == 2 ? (p[0] != p[1]) : (p[0] == p[1]);\n"
    "  if (!pass) discard;\n"
    "#endif\n"
    "  o_color = vec4(outc) / 255.0;\n"
    "}\n";

/* Arma el shader de fragmentos: ubershader (u == NULL) o especializado para las etapas de u. */
static char* build_fs(int zfreeze, const GxUniforms* u) {
    size_t cap = strlen(FS_PRE) + strlen(FS_POST) + (strlen(FS_BODY) + 160) * 17 + 256, n = 0;
    char* o = (char*)malloc(cap);
    int st;
    n += (size_t)snprintf(o + n, cap - n, "#version 330 core\n%s", zfreeze ? "#define ZFREEZE 1\n" : "");
    if (u) n += (size_t)snprintf(o + n, cap - n, "#define SPEC_NS %d\n%s", u->misc[0], (u->misc[2] & 1) ? "#define SPEC_AC_SIEMPRE 1\n" : "");
    n += (size_t)snprintf(o + n, cap - n, "%s", FS_PRE);
    if (!u) n += (size_t)snprintf(o + n, cap - n, "  for (int s = 0; s < ns; s++) {\n    ivec4 st = stage[s];\n%s  }\n", FS_BODY);
    else
        for (st = 0; st < u->misc[0] && st < 16; st++)
            /* la seleccion de constantes (konst) no va en el programa: sale del uniform. Asi
               hay ~300 programas en vez de ~2200 (con NVIDIA cada uno traba la primera vez) */
            n += (size_t)snprintf(o + n, cap - n, "  { const int s = %d; ivec4 st = ivec4(%d, %d, %d, stage[%d].w);\n%s  }\n", st,
                                  u->stage[st][0], u->stage[st][1], u->stage[st][2], st, FS_BODY);
    snprintf(o + n, cap - n, "%s", FS_POST);
    return o;
}

/* Rectangulo de pantalla completa: rect = (x0, y0, x1, y1) en coordenadas de textura. */
static const char* VS_QUAD =
    "#version 330 core\n"
    "uniform vec4 u_rect;\n"
    "out vec2 v_uv;\n"
    "void main() {\n"
    "  vec2 p = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));\n"
    "  v_uv = mix(u_rect.xy, u_rect.zw, p);\n"
    "  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";

static const char* FS_BLIT =
    "#version 330 core\n"
    "uniform sampler2D u_src;\n"
    "in vec2 v_uv; out vec4 o_color;\n"
    "void main() { o_color = vec4(texture(u_src, v_uv).rgb, 1.0); }\n";

/* Conversion de una copia del EFB al formato de textura pedido (ver efb_copy_to_texture en render.c). */
static const char* FS_CONV =
    "#version 330 core\n"
    "uniform sampler2D u_color; uniform sampler2D u_depth;\n"
    "uniform ivec4 u_mode;\n"      /* x = formato, y = intensidad, z = desde Z, w = reducir a la mitad */
    "uniform ivec4 u_mode2;\n"     /* x = el EFB tiene alfa */
    "uniform vec4 u_src;\n"        /* x0, y0, w, h en pixeles del EFB */
    "uniform int u_scale;\n"       /* resolucion interna */
    "in vec2 v_uv; out vec4 o_color;\n"
    "ivec4 fetch(vec2 p) {\n"
    "  ivec2 ip = clamp(ivec2(p), ivec2(0), ivec2(640 * u_scale - 1, 528 * u_scale - 1));\n"
    "  if (u_mode.z != 0) { uint z = min(uint(texelFetch(u_depth, ip, 0).r * 16777215.0 + 0.5), 16777215u);\n"
    "    return ivec4(int((z >> 16u) & 255u), int((z >> 8u) & 255u), int(z & 255u), 255); }\n"
    "  ivec4 c = ivec4(texelFetch(u_color, ip, 0) * 255.0 + 0.5);\n"
    "  if (u_mode2.x == 0) c.a = 255;\n"
    "  return c;\n"
    "}\n"
    "void main() {\n"
    "  vec2 p = (u_src.xy + v_uv * u_src.zw) * float(u_scale);\n"
    "  ivec4 c;\n"
    "  if (u_mode.w != 0) {\n"
    "    vec2 b = floor(p / 2.0) * 2.0;\n"   /* bloque 2x2 en el EFB */
    "    c = (fetch(b) + fetch(b + vec2(1, 0)) + fetch(b + vec2(0, 1)) + fetch(b + vec2(1, 1)) + 2) / 4;\n"
    "  } else c = fetch(p);\n"
    "  int Y = (u_mode.y != 0 && u_mode.z == 0) ? clamp(((66 * c.r + 129 * c.g + 25 * c.b + 128) >> 8) + 16, 0, 255) : c.r;\n"
    "  int f = u_mode.x;\n"
    "  ivec4 o;\n"
    "  if (f == 0) { int v = (Y >> 4) * 17; o = ivec4(v); }\n"                       /* R4 / I4 */
    "  else if (f == 1 || f == 8) o = ivec4(Y);\n"                                    /* R8 / I8 */
    "  else if (f == 7) o = ivec4(c.a);\n"                                            /* A8 */
    "  else if (f == 9) { int v = u_mode.y != 0 ? Y : c.g; o = ivec4(v); }\n"          /* G8 */
    "  else if (f == 10) { int v = u_mode.y != 0 ? Y : c.b; o = ivec4(v); }\n"         /* B8 */
    "  else if (f == 2) { int v = (Y >> 4) * 17; o = ivec4(v, v, v, (c.a >> 4) * 17); }\n"
    "  else if (f == 3) o = ivec4(Y, Y, Y, c.a);\n"
    "  else if (f == 11) o = ivec4(c.g, c.g, c.g, c.r);\n"
    "  else if (f == 12) o = ivec4(c.b, c.b, c.b, c.g);\n"
    "  else if (f == 4) o = ivec4((c.r >> 3) * 255 / 31, (c.g >> 2) * 255 / 63, (c.b >> 3) * 255 / 31, 255);\n"
    "  else if (f == 5) {\n"
    "    if (c.a >= 224) o = ivec4((c.r >> 3) * 255 / 31, (c.g >> 3) * 255 / 31, (c.b >> 3) * 255 / 31, 255);\n"
    "    else o = ivec4((c.r >> 4) * 17, (c.g >> 4) * 17, (c.b >> 4) * 17, (c.a >> 5) * 255 / 7);\n"
    "  }\n"
    "  else o = c;\n"
    "  o_color = vec4(o) / 255.0;\n"
    "}\n";

/* Copia del EFB leida como textura con paleta (C4/C8): el valor guardado es el indice y el
 * color sale de la TLUT, como en la consola (Dolphin: "palette conversion"). La usa la
 * computadora de punteria: la escena se copia en R8 y la paleta la tine (enemigos en lila). */
static const char* FS_PAL =
    "#version 330 core\n"
    "uniform sampler2DArray u_src; uniform sampler2D u_pal;\n"
    "uniform ivec4 u_mode;\n"     /* x = 1: indice de 4 bits (copia R4), y = cantidad de entradas */
    "in vec2 v_uv; out vec4 o_color;\n"
    "void main() {\n"
    "  ivec3 sz = textureSize(u_src, 0);\n"
    "  ivec2 ip = clamp(ivec2(v_uv * vec2(sz.xy)), ivec2(0), sz.xy - 1);\n"
    "  int v = int(texelFetch(u_src, ivec3(ip, 0), 0).r * 255.0 + 0.5);\n"
    "  int i = u_mode.x != 0 ? (v + 8) / 17 : v;\n"
    "  i = min(i, u_mode.y - 1);\n"
    "  o_color = texelFetch(u_pal, ivec2(i, 0), 0);\n"
    "}\n";
static GLuint s_pal_prog, s_pal_lut;
static GLint s_pal_loc_mode;

static GLuint compile(GLenum type, const char* src) {
    GLuint s = p_glCreateShader(type);
    GLint ok = 0;
    p_glShaderSource(s, 1, &src, NULL);
    p_glCompileShader(s);
    p_glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        p_glGetShaderInfoLog(s, sizeof log, NULL, log);
        rt_fatal("OpenGL: error compilando shader:\n%s\n", log);
    }
    return s;
}

static GLuint link_prog(const char* vs, const char* fs, int main_prog) {
    GLuint p = p_glCreateProgram();
    GLint ok = 0;
    p_glAttachShader(p, compile(GL_VERTEX_SHADER, vs));
    p_glAttachShader(p, compile(GL_FRAGMENT_SHADER, fs));
    if (main_prog == 2) {
        static const char* names[14] = { "a_pos", "a_nrm", "a_c0", "a_c1", "a_ia", "a_ib", "a_r0", "a_r1", "a_r2", "a_r3", "a_r4", "a_r5", "a_r6", "a_r7" };
        int i;
        for (i = 0; i < 14; i++) p_glBindAttribLocation(p, (GLuint)i, names[i]);
        p_glBindAttribLocation(p, 14, "a_lay");
    } else if (main_prog) {
        static const char* names[11] = { "a_pos", "a_c0", "a_c1", "a_t0", "a_t1", "a_t2", "a_t3", "a_t4", "a_t5", "a_t6", "a_t7" };
        int i;
        for (i = 0; i < 11; i++) p_glBindAttribLocation(p, (GLuint)i, names[i]);
        p_glBindAttribLocation(p, 14, "a_lay");
    }
    p_glBindFragDataLocation(p, 0, "o_color");
    p_glLinkProgram(p);
    p_glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096];
        p_glGetProgramInfoLog(p, sizeof log, NULL, log);
        rt_fatal("OpenGL: error enlazando shaders:\n%s\n", log);
    }
    return p;
}

/* RL_TEX_RUIDO=1 (pruebas): las texturas nuevas empiezan con basura, como en las placas de
 * verdad (llvmpipe las llena de ceros y esconde lecturas de memoria sin escribir). */
static const void* ruido(size_t n) {
    static int on = -1;
    static uint8_t* buf; static size_t cap;
    if (on < 0) on = getenv("RL_TEX_RUIDO") != NULL;
    if (!on) return NULL;
    if (n > cap) { size_t i; buf = (uint8_t*)realloc(buf, n); for (i = cap; i < n; i++) buf[i] = (uint8_t)(i * 2654435761u >> 13); cap = n; }
    return buf;
}
static GLuint new_tex(int w, int h, GLenum ifmt, GLenum fmt, GLenum type, const void* data) {
    GLuint t;
    p_glGenTextures(1, &t);
    p_glBindTexture(GL_TEXTURE_2D, t);
    if (!data && fmt == GL_RGBA) data = ruido((size_t)w * h * 4);
    p_glTexImage2D(GL_TEXTURE_2D, 0, (GLint)ifmt, w, h, 0, fmt, type, data);
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    return t;
}

static GLuint new_fbo(GLuint color, GLuint depth) {
    GLuint f;
    p_glGenFramebuffers(1, &f);
    p_glBindFramebuffer(GL_FRAMEBUFFER, f);
    p_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
    if (depth) p_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth, 0);
    if (p_glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) rt_fatal("OpenGL: framebuffer incompleto\n");
    return f;
}

/* con antialiasing: pasa el rectangulo (en pixeles del EFB) a las texturas que se leen */
static void efb_resolver(int x, int y, int w, int h, int con_z) {
    if (s_efb_fbo == s_efb_res_fbo) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (w <= 0 || h <= 0) return;
    p_glDisable(GL_SCISSOR_TEST);   /* el recorte del juego tambien recorta glBlitFramebuffer (la mira de la cabina con MSAA) */
    p_glBindFramebuffer(GL_READ_FRAMEBUFFER, s_efb_fbo);
    p_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, s_efb_res_fbo);
    p_glBlitFramebuffer(x * S_, y * S_, (x + w) * S_, (y + h) * S_, x * S_, y * S_, (x + w) * S_, (y + h) * S_,
                        GL_COLOR_BUFFER_BIT | (con_z ? GL_DEPTH_BUFFER_BIT : 0), GL_NEAREST);
    p_glBindFramebuffer(GL_FRAMEBUFFER, s_efb_fbo);
}

/* Las texturas que lee el TEV son todas "arreglos de texturas" (GL_TEXTURE_2D_ARRAY): las
 * del mismo tamano comparten un arreglo y cada vertice dice que capa usa cada unidad. Asi
 * los miles de dibujos chiquitos que solo cambian de textura (el suelo de el nivel 3: ~2500 por
 * cuadro) se juntan en pocas llamadas a la placa. Las copias del EFB tienen un arreglo de
 * una sola capa cada una. */
static GLuint new_tex_arr1(int w, int h, const void* data) {
    GLuint t;
    p_glGenTextures(1, &t);
    p_glBindTexture(GL_TEXTURE_2D_ARRAY, t);
    if (!data) data = ruido((size_t)w * h * 4);
    p_glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, w, h, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    p_glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    p_glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    p_glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL, 0);
    return t;
}
static GLuint new_fbo_layer(GLuint color) {
    GLuint f;
    p_glGenFramebuffers(1, &f);
    p_glBindFramebuffer(GL_FRAMEBUFFER, f);
    p_glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, color, 0, 0);
    if (p_glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) rt_fatal("OpenGL: framebuffer incompleto (capa)\n");
    return f;
}

SDL_Window* gl_ventana(void) { return s_win; }
SDL_GLContext gl_contexto(void) { return s_ctx; }

/* ---- F12: foto de lo que se ve, en capturas/Escuadron2_FECHA.png ---------------------- */
static volatile int s_foto_pedida;
typedef struct { uint8_t* px; int w, h; } Foto;
static int foto_guardar(void* x) {
    Foto* f = (Foto*)x;
    char p[640];
    time_t t = time(NULL);
    struct tm* tm = localtime(&t);
    int y, ok;
    int stbi_write_png(char const* filename, int w, int h, int comp, const void* data, int stride);
    void menu_aviso(const char* texto);
    for (y = 0; y < f->h / 2; y++) {   /* OpenGL lee de abajo para arriba */
        uint8_t* a = f->px + (size_t)y * f->w * 3; uint8_t* b = f->px + (size_t)(f->h - 1 - y) * f->w * 3;
        int i; for (i = 0; i < f->w * 3; i++) { uint8_t c = a[i]; a[i] = b[i]; b[i] = c; }
    }
    {   /* en Windows van a Imagenes\Escuadron2 (facil de encontrar); si no se puede, a capturas\ */
        char dir[512] = "capturas", nom[64];
        const char* donde = "the capturas folder";
#ifdef _WIN32
        char pics[MAX_PATH];
        if (SHGetFolderPathA(NULL, CSIDL_MYPICTURES, NULL, 0, pics) == 0) {
            snprintf(dir, sizeof dir, "%s\\Escuadron2", pics);
            donde = "Pictures\\Escuadron2";
        }
        if (_mkdir(dir) != 0 && errno != EEXIST) { strcpy(dir, "capturas"); donde = "the capturas folder"; _mkdir(dir); }
#else
        mkdir(dir, 0755);
#endif
        strftime(nom, sizeof nom, "Escuadron2_%Y-%m-%d_%H-%M-%S.png", tm);
        snprintf(p, sizeof p, "%s/%s", dir, nom);
        ok = stbi_write_png(p, f->w, f->h, 3, f->px, f->w * 3);
        rt_log("[gl] F12: %s %s\n", ok ? "foto guardada en" : "no se pudo guardar", p);
        {   char t[128]; snprintf(t, sizeof t, "Photo saved to %s", donde);
            menu_aviso(ok ? t : "Could not save the photo"); }
    }
    free(f->px); free(f);
    return 0;
}
static void foto_tomar(int w, int h) {
    Foto* f = (Foto*)malloc(sizeof *f);
    if (!f) return;
    f->w = w; f->h = h; f->px = (uint8_t*)malloc((size_t)w * h * 3);
    if (!f->px) { free(f); return; }
    p_glPixelStorei(GL_PACK_ALIGNMENT, 1);
    p_glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, f->px);
    p_glPixelStorei(GL_PACK_ALIGNMENT, 4);
    SDL_DetachThread(SDL_CreateThread(foto_guardar, "foto", f));   /* el PNG se arma aparte: sin tirones */
}

static void fatal_box(const char* msg) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Escuadron2: error", msg, s_win);
}

void gl_thread_init(void) {
    {   void controles_init(const char* exe_path); extern const char* rt_exe_path; controles_init(rt_exe_path); }
    hd_iniciar();
    static const uint8_t white[4] = { 255, 255, 255, 255 };
    extern void (*rt_fatal_hook)(const char* msg);
    int i;
    rt_fatal_hook = fatal_box;
    if (gl_headless && !getenv("SDL_VIDEODRIVER")) SDL_setenv("SDL_VIDEODRIVER", "offscreen", 1);
    /* controles de PS4/PS5: vibracion tambien por Bluetooth (SDL la apaga por defecto ahi) */
    SDL_SetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON, "1");         /* icono de la ventana: el del .exe */
    SDL_SetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON_SMALL, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS5_RUMBLE, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS4_RUMBLE, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK) != 0 &&
        SDL_InitSubSystem(SDL_INIT_VIDEO) != 0)
        rt_fatal("SDL: %s\n", SDL_GetError());
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    s_win = SDL_CreateWindow("Escuadron2", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                             gl_widescreen ? 1280 : 960, 720, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | (gl_headless ? SDL_WINDOW_HIDDEN : 0) |
                             (gl_fullscreen && !gl_headless ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0));
    if (!s_win) rt_fatal("SDL: no se pudo crear la ventana: %s\n", SDL_GetError());
    s_ctx = SDL_GL_CreateContext(s_win);
    if (!s_ctx) rt_fatal("SDL: no se pudo crear el contexto de OpenGL 3.3: %s\n", SDL_GetError());
    SDL_GL_SetSwapInterval(gl_vsync ? 1 : 0);
    load_gl();
    rt_log("[gl] %s / %s / %s\n", p_glGetString(GL_VENDOR), p_glGetString(GL_RENDERER), p_glGetString(GL_VERSION));

    if (gl_scale <= 0) gl_scale = gl_headless ? 1 : 2;   /* en la ventana, el doble de resolucion */
    if (gl_scale > 6) gl_scale = 6;
    rt_log("[gl] resolucion interna x%d (%dx%d)\n", gl_scale, EFB_W * gl_scale, EFB_H * gl_scale);
    {
        float a = 0;
        while (p_glGetError() != 0) {}
        p_glGetFloatv(0x84FF /* GL_MAX_TEXTURE_MAX_ANISOTROPY */, &a);
        if (p_glGetError() == 0 && a > 1.0f) {
            float pedido = gl_aniso_nivel < 0 ? 8.0f : (float)gl_aniso_nivel;
            s_aniso = a < pedido ? a : pedido;
        }
        if (gl_headless) s_aniso = 0;   /* las pruebas quedan iguales */
        if (s_aniso > 1.0f) rt_log("[gl] filtrado anisotropico x%.0f\n", s_aniso);
    }
    s_efb_color = new_tex(EFB_W * S_, EFB_H * S_, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    s_efb_depth = new_tex(EFB_W * S_, EFB_H * S_, GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL);
    s_efb_fbo = s_efb_res_fbo = new_fbo(s_efb_color, s_efb_depth);
    if (gl_msaa > 1 && !gl_headless) {
        GLint maxs = 0;
        GLuint rb[2], f;
        p_glGetIntegerv(0x8D57 /* GL_MAX_SAMPLES */, &maxs);
        if (gl_msaa > maxs) gl_msaa = maxs;
        if (gl_msaa > 1) {
            p_glGenRenderbuffers(2, rb);
            p_glBindRenderbuffer(GL_RENDERBUFFER, rb[0]);
            p_glRenderbufferStorageMultisample(GL_RENDERBUFFER, gl_msaa, GL_RGBA8, EFB_W * S_, EFB_H * S_);
            p_glBindRenderbuffer(GL_RENDERBUFFER, rb[1]);
            p_glRenderbufferStorageMultisample(GL_RENDERBUFFER, gl_msaa, GL_DEPTH_COMPONENT24, EFB_W * S_, EFB_H * S_);
            p_glGenFramebuffers(1, &f);
            p_glBindFramebuffer(GL_FRAMEBUFFER, f);
            p_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb[0]);
            p_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rb[1]);
            if (p_glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
                s_efb_fbo = f;
                rt_log("[gl] antialiasing MSAA x%d\n", gl_msaa);
            } else { rt_log("[gl] antialiasing x%d no disponible\n", gl_msaa); gl_msaa = 0; }
        } else gl_msaa = 0;
    } else gl_msaa = 0;
    s_white_tex = new_tex_arr1(1, 1, white);

    {   /* programas principales: normal y con "zfreeze" (profundidad congelada) */
        int v;
        for (v = 0; v < 2; v++) {
            char* fs = build_fs(v, NULL);
            GLuint pr = link_prog(VS_MAIN, fs, 1);
            free(fs);
            p_glUseProgram(pr);
            for (i = 0; i < 8; i++) {
                char n[16];
                snprintf(n, sizeof n, "u_tex%d", i);
                p_glUniform1i(p_glGetUniformLocation(pr, n), i);
            }
            p_glUniformBlockBinding(pr, p_glGetUniformBlockIndex(pr, "Gx"), 0);
            if (v) s_prog_z = pr; else s_prog = pr;
        }
    }
    p_glGenBuffers(1, &s_ubo);
    p_glBindBuffer(GL_UNIFORM_BUFFER, s_ubo);
    p_glBufferData(GL_UNIFORM_BUFFER, sizeof(GxUniforms), NULL, GL_DYNAMIC_DRAW);
    p_glBindBufferBase(GL_UNIFORM_BUFFER, 0, s_ubo);

    s_blit_prog = link_prog(VS_QUAD, FS_BLIT, 0);
    p_glUseProgram(s_blit_prog);
    p_glUniform1i(p_glGetUniformLocation(s_blit_prog, "u_src"), 0);
    s_blit_loc_rect = p_glGetUniformLocation(s_blit_prog, "u_rect");
    s_conv_prog = link_prog(VS_QUAD, FS_CONV, 0);
    p_glUseProgram(s_conv_prog);
    p_glUniform1i(p_glGetUniformLocation(s_conv_prog, "u_color"), 0);
    p_glUniform1i(p_glGetUniformLocation(s_conv_prog, "u_depth"), 1);
    s_conv_loc_src = p_glGetUniformLocation(s_conv_prog, "u_src");
    s_conv_loc_mode = p_glGetUniformLocation(s_conv_prog, "u_mode");
    s_conv_loc_mode2 = p_glGetUniformLocation(s_conv_prog, "u_mode2");
    p_glUniform4f(p_glGetUniformLocation(s_conv_prog, "u_rect"), 0, 0, 1, 1);
    p_glUniform1i(p_glGetUniformLocation(s_conv_prog, "u_scale"), gl_scale);

    s_pal_prog = link_prog(VS_QUAD, FS_PAL, 0);
    p_glUseProgram(s_pal_prog);
    p_glUniform1i(p_glGetUniformLocation(s_pal_prog, "u_src"), 0);
    p_glUniform1i(p_glGetUniformLocation(s_pal_prog, "u_pal"), 1);
    p_glUniform4f(p_glGetUniformLocation(s_pal_prog, "u_rect"), 0, 0, 1, 1);
    s_pal_loc_mode = p_glGetUniformLocation(s_pal_prog, "u_mode");
    p_glGenTextures(1, &s_pal_lut);
    p_glBindTexture(GL_TEXTURE_2D, s_pal_lut);
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    p_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 256, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

    p_glGenVertexArrays(1, &s_quad_vao);
    p_glGenVertexArrays(1, &s_vao);
    p_glBindVertexArray(s_vao);
    p_glGenBuffers(1, &s_vbo);
    p_glGenBuffers(1, &s_ibo);
    p_glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
    p_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s_ibo);   /* queda guardado en el VAO */
    for (i = 0; i < 3; i++) p_glEnableVertexAttribArray((GLuint)i);
    {   /* programa y buffers de la transformacion en la GPU */
        const char* e = getenv("RL_GL_XF");
        GLint maxt = 0;
        if (e && *e == '0') s_use_xf = 0;
        {
            int v;
            for (v = 0; v < 2; v++) {
                char* fs = build_fs(v, NULL);
                GLuint pr = link_prog(VS_XF, fs, 2);
                free(fs);
                p_glUseProgram(pr);
                for (i = 0; i < 8; i++) {
                    char n[16];
                    snprintf(n, sizeof n, "u_tex%d", i);
                    p_glUniform1i(p_glGetUniformLocation(pr, n), i);
                }
                p_glUniform1i(p_glGetUniformLocation(pr, "u_xf"), 8);
                p_glUniform1i(p_glGetUniformLocation(pr, "u_xft"), 9);
                p_glUniformBlockBinding(pr, p_glGetUniformBlockIndex(pr, "Gx"), 0);
                if (v) s_xprog_z = pr; else s_xprog = pr;
            }
        }
        spec_init();
        p_glGetIntegerv(GL_MAX_TEXTURE_BUFFER_SIZE, &maxt);
        s_snap_max = (uint32_t)maxt / (XF_BLOCKS / 4);
        if (s_snap_max > 16384) s_snap_max = 16384;
        s_pool_max = (uint32_t)maxt / 4;
        if (s_pool_max > 65536) s_pool_max = 65536;
        if (s_snap_max < 8 || s_pool_max < 8 * XF_BLOCKS) s_use_xf = 0;
        s_snap = (uint32_t*)malloc((size_t)s_snap_max * XF_BLOCKS * 4);
        s_pool = (uint32_t*)malloc((size_t)s_pool_max * 64);
        p_glGenBuffers(1, &s_xf_buf);
        p_glBindBuffer(GL_TEXTURE_BUFFER, s_xf_buf);
        p_glBufferData(GL_TEXTURE_BUFFER, (GLsizeiptr)s_pool_max * 64, NULL, GL_STREAM_DRAW);
        p_glGenBuffers(1, &s_xft_buf);
        p_glBindBuffer(GL_TEXTURE_BUFFER, s_xft_buf);
        p_glBufferData(GL_TEXTURE_BUFFER, (GLsizeiptr)s_snap_max * XF_BLOCKS * 4, NULL, GL_STREAM_DRAW);
        p_glGenTextures(1, &s_xf_tex);
        p_glActiveTexture(GL_TEXTURE8);
        p_glBindTexture(GL_TEXTURE_BUFFER, s_xf_tex);
        p_glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32UI, s_xf_buf);
        p_glGenTextures(1, &s_xft_tex);
        p_glActiveTexture(GL_TEXTURE9);
        p_glBindTexture(GL_TEXTURE_BUFFER, s_xft_tex);
        p_glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32UI, s_xft_buf);
        p_glActiveTexture(GL_TEXTURE0);
        p_glGenVertexArrays(1, &s_xvao);
        p_glBindVertexArray(s_xvao);
        p_glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
        p_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s_ibo);
        for (i = 0; i < 6; i++) p_glEnableVertexAttribArray((GLuint)i);
        for (i = 6; i < 14; i++) p_glVertexAttrib2f((GLuint)i, 0.0f, 0.0f);
        rt_log("[gl] transformacion de vertices en la %s (%u estados por buffer)\n", s_use_xf ? "GPU" : "CPU", s_snap_max);
    }
    ring_init();
    p_glBindFramebuffer(GL_FRAMEBUFFER, s_efb_fbo);
    p_glViewport(0, 0, EFB_W * S_, EFB_H * S_);
    p_glClearColor(0, 0, 0, 0);
    p_glClearDepth(1.0);
    p_glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    p_glEnable(GL_CLIP_DISTANCE0);
    if (!getenv("RL_SIN_LEJANO")) p_glEnable(GL_CLIP_DISTANCE0 + 1);
    p_glEnable(GL_DEPTH_CLAMP);
    p_glFrontFace(GL_CW);
    prewarm_inicial();   /* al final: con todo inicializado */
    calentar_inicial();
}

/* ---- ventana y entradas ------------------------------------------------------------ */

static SDL_GameController* s_pad;

/* abre un joystick (PS5, PS4, Xbox, ...): luz roja en los de PlayStation */
static void abrir_control(int indice) {
    SDL_GameController* c = SDL_GameControllerOpen(indice);
    if (!c) return;
    s_pad = c;
    rt_log("[pad] conectado: %s (%s)\n", SDL_GameControllerName(c),
           SDL_GameControllerGetType(c) == SDL_CONTROLLER_TYPE_PS5 ? "PS5" : SDL_GameControllerGetType(c) == SDL_CONTROLLER_TYPE_PS4 ? "PS4" : "otro");
    if (SDL_GameControllerHasLED(c)) SDL_GameControllerSetLED(c, 210, 30, 30);
}

static void poll_events(void) {
    SDL_Event e;
    int menu_evento(const SDL_Event* e);
    int menu_bloquea(int hay_botones);
    while (SDL_PollEvent(&e)) {
        if (menu_evento(&e)) continue;   /* F1 / Back: menu de opciones (menu.cpp) */
        if (e.type == SDL_QUIT) {
            /* salir de golpe: exit() puede quedar trabado esperando a otro hilo y el
               programa seguiria vivo y escondido, ocupando la placa y el procesador */
            extern FILE* rt_log_file;
            {   /* si lo cerraron porque se trabo, que el registro diga donde estaba cada hilo */
                extern volatile uint64_t gl_ult_imagen_us; extern volatile const char* gl_etapa; void os_volcar_hilos(const char* por);
                extern unsigned video_cuadro_actual(void);
                rt_log("[gl] ventana cerrada en el cuadro %u (ultima imagen hace %.1f s; hilo grafico: %s)\n", video_cuadro_actual(),
                       gl_ult_imagen_us ? (double)(os_now_us() - gl_ult_imagen_us) / 1e6 : 0.0, gl_etapa ? (const char*)gl_etapa : "?");
                os_volcar_hilos("cierre");
            }
            if (rt_log_file) fflush(rt_log_file);
            fflush(stdout);
            _Exit(0);
        }
        if (e.type == SDL_CONTROLLERDEVICEADDED && !s_pad) abrir_control(e.cdevice.which);
        if (e.type == SDL_CONTROLLERDEVICEREMOVED && s_pad &&
            e.cdevice.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(s_pad))) {
            int k;
            rt_log("[pad] se desconecto %s\n", SDL_GameControllerName(s_pad));
            SDL_GameControllerClose(s_pad); s_pad = NULL;
            for (k = 0; k < SDL_NumJoysticks() && !s_pad; k++) if (SDL_IsGameController(k)) abrir_control(k);
        }
        if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_F10) {
            extern uint32_t gx_xf_gen;
            gl_widescreen = !gl_widescreen;
            gx_xf_gen++;    /* que la proxima instantanea de XF tome la proyeccion nueva */
            rt_log("[gl] pantalla %s\n", gl_widescreen ? "panoramica 16:9" : "4:3");
        }
        if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_F12 && !e.key.repeat) {
            if (e.key.keysym.mod & KMOD_SHIFT) s_foto_pedida = 1;   /* Mayus+F12: foto comun (Imagenes\Escuadron2) */
            else {   /* F12, como siempre: el cuadro entero de GX (.png + .gxc en capturas), para depurar */
                extern volatile int video_capture_request;
                void menu_aviso(const char* texto);
                video_capture_request = 1;
                rt_log("[gl] F12: capturando este cuadro de GX (carpeta capturas)\n");
                menu_aviso("Capture saved to the capturas folder");
            }
        }
        if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_F11) {
            uint32_t fl = SDL_GetWindowFlags(s_win);
            SDL_SetWindowFullscreen(s_win, (fl & SDL_WINDOW_FULLSCREEN_DESKTOP) ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
        }
    }
    /* vibracion: el juego prende y apaga el motor; SDL necesita una duracion, asi que se
       renueva mientras siga prendido */
    {
        extern volatile int g_pad_rumble; extern int g_rumble_enabled;
        static int on; static uint32_t t_last;
        int want = g_pad_rumble && g_rumble_enabled;
        uint32_t now = SDL_GetTicks();
        if (s_pad && (want != on || (want && now - t_last > 400))) {
            extern int controles_vibracion;
            int f = controles_vibracion < 0 ? 0 : controles_vibracion > 100 ? 100 : controles_vibracion;
            SDL_GameControllerRumble(s_pad, want ? (Uint16)(0xA000 * f / 100) : 0, want ? (Uint16)(0xC000 * f / 100) : 0, want ? 600 : 0);
            on = want; t_last = now;
        }
    }
    /* teclado y control -> control de GameCube en el puerto 1 (controles.c, controles.ini) */
    {
        void controles_leer(const uint8_t* k, SDL_GameController* pad, PadState* p);
        PadState p;
        controles_leer(SDL_GetKeyboardState(NULL), s_pad, &p);
        if (menu_bloquea(p.buttons != 0 || p.trig_l > 40 || p.trig_r > 40)) {   /* control suelto */
            memset(&p, 0, sizeof p);
            p.stick_x = p.stick_y = p.c_x = p.c_y = 0x80;
        }
        input_set_host(&p);
    }
}

void gl_idle(void) {
    static uint32_t last;
    flush_batch();
    uint32_t now = SDL_GetTicks();
    if (now - last > 8) { poll_events(); last = now; }
}

/* ---- texturas ----------------------------------------------------------------- */

typedef struct TexEntry {
    uint32_t addr, w, h, fmt, tlut, tlut_fmt, nlev;
    uint64_t hash;
    uint32_t last_check, last_use;
    int from_efb;
    GLuint tex, fbo;        /* fbo: solo las copias del EFB (se reusa en cada copia) */
    struct TexArr* arr;     /* arreglo donde esta (texturas de la RAM; NULL: hay que volver a subirla) */
    int layer;
    HdTex* hd;              /* reemplazo en alta definicion (paquete de Dolphin) */
    int copia_tf;           /* copias del EFB: formato de la copia (como Dolphin EFBCopyFormat) */
    uint32_t copia_gen;     /* cuantas veces se copio (para rehacer la version con paleta) */
    GLuint pal_tex, pal_fbo;/* la copia leida como C4/C8 con una paleta (TLUT) */
    uint32_t pal_gen, pal_w, pal_h;
    uint64_t pal_hash;
    uint64_t hd_hash;       /* contenido para el que se busco el reemplazo */
    int hd_mirado;
    struct TexEntry* next;
} TexEntry;

typedef struct TexArr { GLuint tex; int w, h, nlev, cap; TexEntry** own; struct TexArr* next; } TexArr;
static TexArr* s_arrs;
static int s_arr_n;
/* capas por arreglo segun el tamano (1-2 MB por arreglo; como mucho 256: la capa va en 8 bits) */
static int arr_cap(int w, int h) { int a = w * h; return a <= 32 * 32 ? 256 : a <= 64 * 64 ? 128 : a <= 128 * 128 ? 32 : a <= 256 * 256 ? 8 : 1; }
/* busca una capa libre para la textura (o la de una que no se usa hace rato; si no, un arreglo nuevo) */
static void tex_slot(TexEntry* e) {
    int nlev = e->nlev ? (int)e->nlev : 1, i, bi = -1;
    TexArr* a, *best = NULL;
    uint32_t bl = 0;
    for (a = s_arrs; a; a = a->next) {
        if (a->w != (int)e->w || a->h != (int)e->h || a->nlev != nlev) continue;
        for (i = 0; i < a->cap; i++) {
            TexEntry* o = a->own[i];
            if (!o) { a->own[i] = e; e->arr = a; e->layer = i; e->tex = a->tex; return; }
            if (s_frame - o->last_use > 60 && (!best || s_frame - o->last_use > bl)) { best = a; bi = i; bl = s_frame - o->last_use; }
        }
    }
    if (best) {
        best->own[bi]->arr = NULL;
        best->own[bi] = e; e->arr = best; e->layer = bi; e->tex = best->tex;
        return;
    }
    a = (TexArr*)calloc(1, sizeof *a);
    a->w = (int)e->w; a->h = (int)e->h; a->nlev = nlev; a->cap = arr_cap(a->w, a->h);
    a->own = (TexEntry**)calloc((size_t)a->cap, sizeof *a->own);
    p_glGenTextures(1, &a->tex);
    p_glBindTexture(GL_TEXTURE_2D_ARRAY, a->tex);
    for (i = 0; i < nlev; i++) {
        int lw = a->w >> i, lh = a->h >> i;
        p_glTexImage3D(GL_TEXTURE_2D_ARRAY, i, GL_RGBA8, lw > 0 ? lw : 1, lh > 0 ? lh : 1, a->cap, 0, GL_RGBA, GL_UNSIGNED_BYTE, ruido((size_t)(lw > 0 ? lw : 1) * (lh > 0 ? lh : 1) * 4 * a->cap));
    }
    p_glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL, nlev - 1);
    a->next = s_arrs; s_arrs = a; s_arr_n++;
    a->own[0] = e; e->arr = a; e->layer = 0; e->tex = a->tex;
}

#define TEX_BUCKETS 4096
#define MAX_XFB_DBG 8
static uint32_t s_xfb_dbg[MAX_XFB_DBG];
static TexEntry* s_texcache[TEX_BUCKETS];

static uint32_t tex_bytes(int fmt, int w, int h) {
    static const int bw[16] = { 8, 8, 8, 4, 4, 4, 4, 0, 8, 8, 4, 0, 0, 0, 8, 0 };
    static const int bh[16] = { 8, 4, 4, 4, 4, 4, 4, 0, 8, 4, 4, 0, 0, 0, 8, 0 };
    if (!bw[fmt]) return 0;
    return (uint32_t)(((w + bw[fmt] - 1) / bw[fmt]) * ((h + bh[fmt] - 1) / bh[fmt]) * (fmt == 6 ? 64 : 32));
}

static uint64_t fnv(const uint8_t* p, uint32_t n, uint64_t h) {
    uint32_t i = 0;
    /* cuatro carriles independientes de a 8 bytes (el procesador los hace en paralelo) */
    if (n >= 32) {
        uint64_t a = h, b = h ^ 0x9E3779B97F4A7C15ull, c = h ^ 0xC2B2AE3D27D4EB4Full, d = h ^ 0x165667B19E3779F9ull;
        for (; i + 32 <= n; i += 32) {
            uint64_t v0, v1, v2, v3;
            memcpy(&v0, p + i, 8); memcpy(&v1, p + i + 8, 8); memcpy(&v2, p + i + 16, 8); memcpy(&v3, p + i + 24, 8);
            a = (a ^ v0) * 0x100000001B3ull; b = (b ^ v1) * 0x100000001B3ull;
            c = (c ^ v2) * 0x100000001B3ull; d = (d ^ v3) * 0x100000001B3ull;
            a ^= a >> 29; b ^= b >> 29; c ^= c >> 29; d ^= d >> 29;
        }
        h = a ^ (b * 3) ^ (c * 5) ^ (d * 7);
    }
    for (; i + 8 <= n; i += 8) { uint64_t v; memcpy(&v, p + i, 8); h = (h ^ v) * 0x100000001B3ull; h ^= h >> 29; }
    for (; i < n; i++) h = (h ^ p[i]) * 0x100000001B3ull;
    return h;
}

/* bytes de todos los niveles de mipmap (cada uno la mitad del anterior, en bloques) */
static uint32_t tex_bytes_levels(int fmt, int w, int h, int nlev) {
    uint32_t n = 0;
    int l;
    for (l = 0; l < nlev; l++) {
        n += tex_bytes(fmt, w, h);
        w = w > 1 ? w / 2 : 1; h = h > 1 ? h / 2 : 1;
    }
    return n;
}

static int getenv_tex_full(void) {   /* RL_TEX_HASH_FULL=1: hashear las texturas enteras */
    static int v = -1;
    if (v < 0) v = getenv("RL_TEX_HASH_FULL") != NULL;
    return v;
}

static uint64_t tex_hash(const TexInfo* t, int nlev) {
    uint64_t h = 0xCBF29CE484222325ull;
    uint32_t n = tex_bytes_levels(t->fmt, t->w, t->h, nlev);
    if (n > 65536 && !getenv_tex_full()) {
        /* texturas grandes: 512 muestras de 32 bytes repartidas (como el cache rapido de
           Dolphin); las que el juego cambia enteras (peliculas) se detectan igual */
        uint32_t k, step = (n / 512) & ~31u;
        for (k = 0; k < 512; k++) h = fnv(t->data + (size_t)k * step, 32, h);
        h = fnv(t->data + n - 32, 32, h ^ n);
    } else
        h = fnv(t->data, n, h);
    if (t->fmt == 8 || t->fmt == 9 || t->fmt == 10) {
        uint32_t entries = t->fmt == 8 ? 16 : t->fmt == 9 ? 256 : 16384;
        uint32_t off = t->tlut & (sizeof s_tmem - 1);
        uint32_t len = entries * 2;
        if (off + len > sizeof s_tmem) len = (uint32_t)sizeof s_tmem - off;
        h = fnv(s_tmem + off, len, h ^ 0x9E3779B97F4A7C15ull);
    }
    return h;
}

static void tex_upload_real(TexEntry* e, const TexInfo* t);
static void tex_upload(TexEntry* e, const TexInfo* t) {   /* con el tiempo, para el detalle de los cuadros lentos */
    extern uint64_t g_t_tex_us;
    uint64_t t0 = os_now_us();
    tex_upload_real(e, t);
    g_t_tex_us += os_now_us() - t0;
}
static void tex_upload_real(TexEntry* e, const TexInfo* t) {
    extern uint64_t gl_stat_tex_uploads;
    TexInfo lt = *t;
    int l, nlev = (int)e->nlev ? (int)e->nlev : 1;
    uint8_t* rgba = (uint8_t*)malloc((size_t)t->w * t->h * 4);
    gl_stat_tex_uploads++;
    if (!e->arr) tex_slot(e);
    p_glBindTexture(GL_TEXTURE_2D_ARRAY, e->arr->tex);
    p_glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (l = 0; l < nlev; l++) {
        texture_decode_rgba(&lt, rgba);
        {   /* RL_TEX_CHECK=1: compara la decodificacion por bloques con texel() */
            static int chk = -1, bad;
            if (chk < 0) chk = getenv("RL_TEX_CHECK") != NULL;
            if (chk && bad < 20) {
                int x, y;
                for (y = 0; y < lt.h && bad < 20; y++)
                    for (x = 0; x < lt.w; x++) {
                        uint8_t o[4];
                        texel(&lt, x, y, o);
                        if (memcmp(o, rgba + ((size_t)y * lt.w + x) * 4, 4)) {
                            rt_log("[tex] diferencia fmt %d %dx%d nivel %d en %d,%d: %02X%02X%02X%02X vs %02X%02X%02X%02X\n", lt.fmt, lt.w, lt.h, l, x, y,
                                   rgba[((size_t)y * lt.w + x) * 4], rgba[((size_t)y * lt.w + x) * 4 + 1], rgba[((size_t)y * lt.w + x) * 4 + 2], rgba[((size_t)y * lt.w + x) * 4 + 3], o[0], o[1], o[2], o[3]);
                            bad++;
                            break;
                        }
                    }
            }
        }
        {   /* RL_TEX_DUMP=1: guarda cada textura subida (nivel 0) en build/tex: color arriba, alfa abajo */
            static int dump = -1;
            if (dump < 0) dump = getenv("RL_TEX_DUMP") != NULL;
            if (dump) {
                int video_write_png(const char* path, const uint8_t* rgb, int w, int h);
                char path[128];
                uint8_t* rgb = (uint8_t*)malloc((size_t)lt.w * lt.h * 6);
                size_t i, n = (size_t)lt.w * lt.h;
                for (i = 0; i < n; i++) {
                    memcpy(rgb + 3 * i, rgba + 4 * i, 3);
                    memset(rgb + 3 * (n + i), rgba[4 * i + 3], 3);
                }
                snprintf(path, sizeof path, "build/tex/%08X_f%d_%dx%d_n%d.png", e->addr, lt.fmt, lt.w, lt.h, l);
                video_write_png(path, rgb, lt.w, lt.h * 2);
                free(rgb);
            }
        }
        p_glTexSubImage3D(GL_TEXTURE_2D_ARRAY, l, 0, 0, e->layer, lt.w, lt.h, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        lt.data += tex_bytes(lt.fmt, lt.w, lt.h);
        lt.w = lt.w > 1 ? lt.w / 2 : 1; lt.h = lt.h > 1 ? lt.h / 2 : 1;
    }
    free(rgba);
}

static TexEntry** bucket(uint32_t addr) { return &s_texcache[(addr >> 5) % TEX_BUCKETS]; }

/* Busca una copia del EFB en esa direccion con ese tamano. */
static TexEntry* find_efb_copy(uint32_t addr, int w, int h) {
    TexEntry* e;
    for (e = *bucket(addr); e; e = e->next)
        if (e->from_efb && e->addr == addr && (int)e->w == w && (int)e->h == h) return e;
    return NULL;
}

/* ---- texturas en alta definicion (paquetes de Dolphin, ver texturas_hd.c) ---- */

/* nombre de la textura como lo arma Dolphin (TextureInfo::CalculateTextureName) */
static void hd_nombres(const TexInfo* t, int con_mips, char* completo, char* sin_tlut, char* sin_tex) {
    uint32_t tam = tex_bytes(t->fmt, t->w, t->h), i, mn = 0xFFFF, mx = 0, tl = 0;
    const uint8_t* d = t->data;
    char base[48], tlut[24] = "";
    uint64_t th = hd_xxh64(d, tam);
    if (t->fmt == 8) { tl = 32; for (i = 0; i < tam; i++) { uint32_t a = d[i] & 15, b = d[i] >> 4; if (a < mn) mn = a; if (b < mn) mn = b; if (a > mx) mx = a; if (b > mx) mx = b; } }
    else if (t->fmt == 9) { tl = 512; for (i = 0; i < tam; i++) { if (d[i] < mn) mn = d[i]; if (d[i] > mx) mx = d[i]; } }
    else if (t->fmt == 10) {   /* igual que Dolphin (que lee un byte, no media palabra) */
        tl = 32768;
        for (i = 0; i < tam; i += 2) { uint32_t v = ((uint32_t)d[i] << 8) & 0x3FFF; if (v < mn) mn = v; if (v > mx) mx = v; }
    }
    if (tl) {
        uint32_t off = (t->tlut & (sizeof s_tmem - 1)) + 2 * mn, len = 2 * (mx + 1 - mn);
        if (off + len > sizeof s_tmem) len = (uint32_t)sizeof s_tmem - off;
        snprintf(tlut, sizeof tlut, "_%016llx", (unsigned long long)hd_xxh64(s_tmem + off, len));
    }
    snprintf(base, sizeof base, "tex1_%dx%d%s", t->w, t->h, con_mips ? "_m" : "");
    sprintf(completo, "%s_%016llx%s_%d", base, (unsigned long long)th, tlut, t->fmt);
    sprintf(sin_tlut, "%s_%016llx_$_%d", base, (unsigned long long)th, t->fmt);
    sprintf(sin_tex, "%s_$%s_%d", base, tlut, t->fmt);
}

/* sube la imagen HD a la placa (una sola vez; la comparten todas las copias de la textura) */
static GLuint hd_subir(HdTex* h, int con_mips) {
    const HdImagen* im = hd_imagen(h);
    GLuint tx;
    int l, nl;
    extern volatile const char* gl_etapa;
    if (!im) return 0;
    gl_etapa = "subiendo una textura HD";
    nl = im->niveles;
    p_glGenTextures(1, &tx);
    p_glBindTexture(GL_TEXTURE_2D_ARRAY, tx);
    p_glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (l = 0; l < nl; l++) {
        int w = im->w >> l > 0 ? im->w >> l : 1, hh = im->h >> l > 0 ? im->h >> l : 1;
        if (im->comprimida) p_glCompressedTexImage3D(GL_TEXTURE_2D_ARRAY, l, im->formato_gl, w, hh, 1, 0, (GLsizei)im->tam[l], im->nivel[l]);
        else p_glTexImage3D(GL_TEXTURE_2D_ARRAY, l, GL_RGBA8, w, hh, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, im->nivel[l]);
    }
    if (con_mips && nl == 1 && !im->comprimida) {
        int m = im->w > im->h ? im->w : im->h;
        for (nl = 1; (1 << (nl - 1)) < m; nl++) {}
        p_glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL, nl - 1);
        p_glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
    } else p_glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL, nl - 1);
    hd_soltar_imagen(h);
    *hd_gl(h) = tx;
    return tx;
}

/* RL_VOLCAR_TEXTURAS=1: guarda cada textura nueva con el nombre de Dolphin en texturas_volcadas/ */
static void hd_volcar(const TexInfo* t, int con_mips) {
    static int v = -1;
    char a[160], b[160], c[160], ruta[256];
    uint8_t* rgba;
    FILE* f;
    if (v < 0) {
        v = getenv("RL_VOLCAR_TEXTURAS") != NULL;
        if (v) {
#ifdef _WIN32
            _mkdir("texturas_volcadas");
#else
            mkdir("texturas_volcadas", 0755);
#endif
        }
    }
    if (!v) return;
    hd_nombres(t, con_mips, a, b, c);
    snprintf(ruta, sizeof ruta, "texturas_volcadas/%s.png", a);
    if ((f = fopen(ruta, "rb")) != NULL) { fclose(f); return; }
    rgba = (uint8_t*)malloc((size_t)t->w * t->h * 4);
    texture_decode_rgba(t, rgba);
    hd_png_rgba(ruta, rgba, t->w, t->h);
    free(rgba);
}

static void plain_state(void);
static void draw_quad(void);
/* la copia del EFB e leida como C4 (fmt 8) o C8 (fmt 9) con la TLUT de t */
static GLuint copia_con_paleta(TexEntry* e, const TexInfo* t) {
    int n = t->fmt == 8 ? 16 : 256, i, cuatro = t->fmt == 8;
    uint32_t off = t->tlut & (sizeof s_tmem - 1);
    uint8_t rgba[256 * 4];
    uint64_t h;
    if (t->fmt == 10) return 0;                                   /* C14X2: no */
    if (cuatro != (e->copia_tf == 0)) return 0;                   /* tamanos de indice distintos: no se sabe */
    if (off + 2u * (uint32_t)n > sizeof s_tmem) return 0;
    h = fnv(s_tmem + off, 2u * (uint32_t)n, 0xCBF29CE484222325ull ^ (uint64_t)t->tlut_fmt);
    if (e->pal_tex && e->pal_gen == e->copia_gen && e->pal_hash == h) return e->pal_tex;
    for (i = 0; i < n; i++) {   /* la paleta: entradas de 16 bits en big endian */
        uint16_t v = (uint16_t)((s_tmem[off + 2 * i] << 8) | s_tmem[off + 2 * i + 1]);
        uint8_t* o = rgba + 4 * i;
        if (t->tlut_fmt == 0) { o[0] = o[1] = o[2] = (uint8_t)(v & 255); o[3] = (uint8_t)(v >> 8); }        /* IA8 */
        else if (t->tlut_fmt == 1) { o[0] = (uint8_t)(((v >> 11) & 31) * 255 / 31); o[1] = (uint8_t)(((v >> 5) & 63) * 255 / 63); o[2] = (uint8_t)((v & 31) * 255 / 31); o[3] = 255; }   /* RGB565 */
        else if (v & 0x8000) { o[0] = (uint8_t)(((v >> 10) & 31) * 255 / 31); o[1] = (uint8_t)(((v >> 5) & 31) * 255 / 31); o[2] = (uint8_t)((v & 31) * 255 / 31); o[3] = 255; }   /* RGB5A3 */
        else { o[0] = (uint8_t)(((v >> 8) & 15) * 17); o[1] = (uint8_t)(((v >> 4) & 15) * 17); o[2] = (uint8_t)((v & 15) * 17); o[3] = (uint8_t)(((v >> 12) & 7) * 255 / 7); }
    }
    if (e->pal_tex && (e->pal_w != e->w || e->pal_h != e->h)) { p_glDeleteTextures(1, &e->pal_tex); p_glDeleteFramebuffers(1, &e->pal_fbo); e->pal_tex = e->pal_fbo = 0; }
    if (!e->pal_tex) { e->pal_tex = new_tex_arr1((int)e->w * S_, (int)e->h * S_, NULL); e->pal_fbo = new_fbo_layer(e->pal_tex); e->pal_w = e->w; e->pal_h = e->h; }
    flush_batch();
    gl_state_reset();
    p_glActiveTexture(GL_TEXTURE1);
    p_glBindTexture(GL_TEXTURE_2D, s_pal_lut);
    p_glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    p_glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, n, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    p_glActiveTexture(GL_TEXTURE0);
    p_glBindTexture(GL_TEXTURE_2D_ARRAY, e->tex);
    p_glBindFramebuffer(GL_FRAMEBUFFER, e->pal_fbo);
    plain_state();
    p_glViewport(0, 0, (int)e->w * S_, (int)e->h * S_);
    p_glUseProgram(s_pal_prog);
    p_glUniform4i(s_pal_loc_mode, cuatro, n, 0, 0);
    draw_quad();
    gl_state_reset();
    s_applied_valid = 0;
    e->pal_gen = e->copia_gen; e->pal_hash = h;
    if (getenv("RL_DUMP_COPIES")) {   /* tambien el resultado de leerla con paleta */
        int video_write_png(const char* path, const uint8_t* rgb, int w, int h);
        static int np;
        int W = (int)e->w * S_, H = (int)e->h * S_, q;
        uint8_t* rgba = (uint8_t*)malloc((size_t)W * H * 4);
        uint8_t* rgb = (uint8_t*)malloc((size_t)W * H * 3 * 2);
        char path[128];
        p_glBindFramebuffer(GL_FRAMEBUFFER, e->pal_fbo);
        p_glPixelStorei(GL_PACK_ALIGNMENT, 1);
        p_glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        for (q = 0; q < W * H; q++) { rgb[q * 3] = rgba[q * 4]; rgb[q * 3 + 1] = rgba[q * 4 + 1]; rgb[q * 3 + 2] = rgba[q * 4 + 2];
            rgb[(W * H + q) * 3] = rgb[(W * H + q) * 3 + 1] = rgb[(W * H + q) * 3 + 2] = rgba[q * 4 + 3]; }
        { extern uint64_t gx_draw_index_dbg(void); rt_log("[paleta] %d dibujo %llu e %p tex %u gen %u tf %d\n", np, (unsigned long long)gx_draw_index_dbg(), (void*)e, e->tex, e->copia_gen, e->copia_tf); }
        snprintf(path, sizeof path, "build/paleta_%03d_%08X.png", np++, e->addr);
        video_write_png(path, rgb, W, H * 2);
        free(rgba); free(rgb);
    }
    {   static int avisos; if (avisos++ < 5) rt_log("[gl] copia del EFB %08X %ux%u leida con paleta (fmt %d, tlut %d)\n", e->addr, e->w, e->h, t->fmt, t->tlut_fmt); }
    return e->pal_tex;
}

static GLuint xfb_como_textura(uint32_t addr, int w, int h, int fmt);
static GLuint texture_for(int map, int* linear_mag, int* linear_min, int* ws, int* wt, int* mip, int* bias, int* layer) {
    TexInfo t;
    *layer = 0;
    int m0 = map < 4 ? 0x80 + map : 0xA0 + map - 4, i3 = map < 4 ? 0x94 + map : 0xB4 + map - 4;
    int m1 = map < 4 ? 0x84 + map : 0xA4 + map - 4;
    uint32_t mode = gx.bp_regs[m0], mode1 = gx.bp_regs[m1];
    uint32_t addr = (gx.bp_regs[i3] & 0xFFFFFF) << 5;
    int nlev = 1;
    TexEntry* e;
    tex_info(map, &t);
    /* modo de textura 0: bits 5-6 mipmap (0 no, 1 el mas cercano, 2 lineal), bit 7 reduccion lineal,
       bits 9-16 sesgo del LOD (con signo, en 1/32); modo 1: LOD minimo y maximo (en 1/16) */
    *linear_mag = t.mag_linear;
    *linear_min = (mode >> 7) & 1;
    *mip = (int)((mode >> 5) & 3);
    *bias = (int)(int8_t)((mode >> 9) & 0xFF);
    *ws = t.wrap_s; *wt = t.wrap_t;
    if (*mip) {
        int maxl = (int)((mode1 >> 8) & 0xFF) / 16, sz = t.w > t.h ? t.w : t.h;
        while ((1 << (nlev - 1)) < sz && nlev - 1 < maxl) nlev++;
    }
    {   /* textura que empieza justo en una XFB: las transiciones de los menus dibujan la
           ultima imagen leyendo la memoria de la XFB. La imagen solo esta en la placa: se
           usa como textura (como hace Dolphin) en vez de la RAM, que tiene basura. */
        GLuint xt = xfb_como_textura(addr, t.w, t.h, t.fmt);
        if (xt) return xt;
    }
    {   /* RL_TEX_BUSCAR=direccion (hex): anota que dibujos usan esa textura y como (depuracion) */
        static long busca = -2;
        if (busca == -2) { const char* ev = getenv("RL_TEX_BUSCAR"); busca = ev ? strtol(ev, NULL, 16) : -1; }
        if (busca >= 0 && (uint32_t)busca == (addr & 0x1FFFFFFu)) {
            extern uint64_t gx_draw_index_dbg(void);
            rt_log("[buscar] dibujo %llu unidad %d: %08X %dx%d fmt %d tlut %d copia %s\n", (unsigned long long)gx_draw_index_dbg(), map, addr, t.w, t.h, t.fmt, t.tlut_fmt,
                   find_efb_copy(addr, t.w, t.h) ? "si" : "no");
        }
    }
    if ((e = find_efb_copy(addr, t.w, t.h)) != NULL) {
        e->last_use = s_frame;
        if (t.fmt == 8 || t.fmt == 9) { GLuint pt = copia_con_paleta(e, &t); if (pt) return pt; }
        return e->tex;
    }
    {
        TexEntry* q;
        static int warned;
        for (q = *bucket(addr); q; q = q->next)
            if (q->from_efb && q->addr == addr) {
                if (warned++ < 20) rt_log("[gl] copia del EFB en %08X de %ux%u usada como %dx%d fmt %d\n", addr, q->w, q->h, t.w, t.h, t.fmt);
                q->last_use = s_frame;
                return q->tex;
            }
    }
    {   /* textura que empieza dentro de una copia del EFB (no al principio): registrar.
           Solo con RL_LOG_COPIES: recorre toda la cache y en cada dibujo costaba mas de la
           mitad del tiempo del hilo grafico. */
        static int wsub, lg = -1;
        int b2;
        if (lg < 0) lg = getenv("RL_LOG_COPIES") != NULL;
        if (lg && wsub < 40)
            for (b2 = 0; b2 < TEX_BUCKETS && wsub < 40; b2++) {
                TexEntry* q;
                for (q = s_texcache[b2]; q; q = q->next)
                    if (q->from_efb && addr > q->addr && addr < q->addr + q->w * q->h * 4) {
                        rt_log("[gl] textura %08X %dx%d fmt %d dentro de la copia del EFB %08X %ux%u (desplazamiento %u)\n", addr, t.w, t.h, t.fmt, q->addr, q->w, q->h, addr - q->addr);
                        wsub++;
                        break;
                    }
            }
    }
    {
        static int warned2;
        int k;
        for (k = 0; k < MAX_XFB_DBG; k++)
            if (s_xfb_dbg[k] && addr >= s_xfb_dbg[k] && addr < s_xfb_dbg[k] + 1280 * 576 && warned2++ < 30)
                rt_log("[gl] textura dentro de una XFB: %08X (xfb %08X) %dx%d fmt %d tlut %X/%d\n", addr, s_xfb_dbg[k], t.w, t.h, t.fmt, t.tlut, t.tlut_fmt);
    }
    if (!t.data || !tex_bytes(t.fmt, t.w, t.h) || !mem_ptr((addr | 0x80000000u) + tex_bytes(t.fmt, t.w, t.h) - 1)) return s_white_tex;
    while (nlev > 1 && !mem_ptr((addr | 0x80000000u) + tex_bytes_levels(t.fmt, t.w, t.h, nlev) - 1)) nlev--;
    int xfb_nueva = 0;
    for (e = *bucket(addr); e; e = e->next)
        if (!e->from_efb && e->addr == addr && (int)e->w == t.w && (int)e->h == t.h && (int)e->fmt == t.fmt && (int)e->nlev == nlev &&
            ((t.fmt != 8 && t.fmt != 9 && t.fmt != 10) || (e->tlut == t.tlut && (int)e->tlut_fmt == t.tlut_fmt)))
            break;
    if (!e) {
        {
            static int lg = -1;
            if (lg < 0) lg = getenv("RL_LOG_COPIES") != NULL;
            if (lg && t.w * t.h >= 256 * 128) rt_log("[tex] cuadro %u: textura nueva desde la RAM %08X %dx%d fmt %d\n", s_frame, addr, t.w, t.h, t.fmt);
        }
        e = (TexEntry*)calloc(1, sizeof *e);
        e->addr = addr; e->w = (uint32_t)t.w; e->h = (uint32_t)t.h; e->fmt = (uint32_t)t.fmt;
        e->tlut = t.tlut; e->tlut_fmt = (uint32_t)t.tlut_fmt; e->nlev = (uint32_t)nlev;
        e->next = *bucket(addr); *bucket(addr) = e;
        e->hash = tex_hash(&t, nlev);
        e->last_check = s_frame;
        flush_batch();          /* tex_upload cambia la textura enlazada */
        tex_upload(e, &t);
        s_applied_valid = 0;
    } else if (!e->arr) {   /* su capa se la dieron a otra: subirla de nuevo */
        e->hash = tex_hash(&t, nlev);
        e->last_check = s_frame;
        flush_batch();
        tex_upload(e, &t);
        s_applied_valid = 0;
    } else if (e->last_check != s_frame || xfb_nueva) {
        uint64_t h = tex_hash(&t, nlev);
        e->last_check = s_frame;
        if (h != e->hash) { e->hash = h; flush_batch(); tex_upload(e, &t); s_applied_valid = 0; }
    }
    e->last_use = s_frame;
    if (!e->hd_mirado || e->hd_hash != e->hash) {   /* textura nueva o cambio su contenido */
        e->hd_mirado = 1;
        e->hd_hash = e->hash;
        e->hd = NULL;
        hd_volcar(&t, *mip != 0);
        if (hd_activo()) {
            char a[160], b[160], c[160];
            hd_nombres(&t, *mip != 0, a, b, c);
            e->hd = hd_buscar(a);
            if (!e->hd && (t.fmt == 8 || t.fmt == 9 || t.fmt == 10)) { e->hd = hd_buscar(b); if (!e->hd) e->hd = hd_buscar(c); }
        }
    }
    if (e->hd) {
        GLuint g = *hd_gl(e->hd);
        if (!g && hd_estado(e->hd) == HD_LISTA) { flush_batch(); g = hd_subir(e->hd, *mip != 0); s_applied_valid = 0; }
        if (g) { *layer = 0; return g; }
    }
    *layer = e->layer;
    return e->tex;
}

static void evict_textures(void) {
    int b;
    for (b = 0; b < TEX_BUCKETS; b++) {
        TexEntry** pp = &s_texcache[b];
        while (*pp) {
            TexEntry* e = *pp;
            if (s_frame - e->last_use > 600) {
                *pp = e->next;
                if (e->from_efb) p_glDeleteTextures(1, &e->tex);
                if (e->pal_tex) { p_glDeleteTextures(1, &e->pal_tex); p_glDeleteFramebuffers(1, &e->pal_fbo); }
                else if (e->arr) e->arr->own[e->layer] = NULL;
                if (e->fbo) p_glDeleteFramebuffers(1, &e->fbo);
                free(e);
            } else pp = &e->next;
        }
    }
}

/* ---- estado de GX -> OpenGL ------------------------------------------------------- */

static uint32_t bpr(int r) { return gx.bp_regs[r]; }
static float xf_rf(uint32_t reg) { return u32_f(gx.xf_regs[reg & 0xFF]); }

static void tev_pair(uint32_t ra, uint32_t bg, int32_t o[4]) {
    int r = (int)(ra & 0x7FF), a = (int)((ra >> 12) & 0x7FF), b = (int)(bg & 0x7FF), g = (int)((bg >> 12) & 0x7FF);
    if (r & 0x400) r -= 0x800;
    if (a & 0x400) a -= 0x800;
    if (b & 0x400) b -= 0x800;
    if (g & 0x400) g -= 0x800;
    o[0] = r; o[1] = g; o[2] = b; o[3] = a;
}

/* Todo el estado de OpenGL que necesita un dibujo. Si dos dibujos seguidos tienen el
 * mismo estado, se juntan en una sola llamada a la placa (la GameCube hace miles de
 * dibujos chiquitos por cuadro y cada llamada a OpenGL tiene un costo fijo). */
typedef struct {
    GxUniforms u;
    GLuint tex[8];
    uint8_t tpar[8][8];     /* filtro de aumento, de reduccion, envoltura s, t, mipmap, sesgo del LOD */
    uint32_t used;          /* texturas usadas por las etapas */
    uint32_t z, cm, cm1;
    int32_t sc[4];
    int32_t cull;
    int32_t ntex;
    int32_t xf;             /* 1: vertices sin transformar (VS_XF) */
    int32_t nraw;           /* coordenadas de textura crudas por vertice (VS_XF) */
    int32_t zf;             /* profundidad congelada (genMode bit 19) */
} DrawKey;

static DrawKey s_key_pending, s_key_applied;
static int s_k_igual;   /* la clave del ultimo dibujo es igual a s_key_pending (no hace falta compararlas) */
static uint32_t s_key_lay[2];   /* capa de cada unidad de textura (8 bits cada una) del dibujo actual */
/* Vertices compactos: posicion (4 floats), 2 colores RGBA8 y solo las coordenadas de
 * textura que usa el dibujo; los dibujos se arman con indices (sin repetir vertices). */
static size_t s_vn;                 /* vertices pendientes */
static uint8_t* s_vbuf;
static size_t s_vbytes, s_vcap;     /* bytes usados / reservados */
static uint32_t* s_ibuf;
static size_t s_in, s_icap;         /* indices pendientes */
static int s_vtex;                  /* coordenadas de textura por vertice del lote pendiente */
static int s_attr_tex = -1;         /* como estan configurados los atributos ahora */
static int s_vxf, s_vraw;
static int s_vzf;                   /* el lote pendiente usa "zfreeze" */          /* el lote pendiente es de VS_XF, con cuantas coordenadas crudas */
/* Lote diferido: al cambiar de material, el lote terminado no se manda enseguida (habria que
 * esperar a que los otros hilos terminen de leer sus vertices); queda aparte y se manda
 * recien al cerrar el lote siguiente. Mientras tanto el hilo grafico sigue leyendo comandos y
 * los otros hilos terminan los vertices. Cualquier otra cosa que toque OpenGL (copias,
 * texturas, mostrar) pasa por flush_batch, que manda los dos en orden. RL_DIFERIR=0: como antes. */
static volatile int s_cnt[2];                /* vertices sin leer de cada lote */
static volatile int* s_cnt_lote = &s_cnt[0]; /* el del lote que se esta armando */
typedef struct {
    int valido;
    DrawKey key;
    uint8_t* vbuf; size_t vbytes, vcap, vn;
    uint32_t* ibuf; size_t in, icap;
    int vtex, vxf, vraw, vzf;
    volatile int* cnt;
    volatile int* cntc;
    int sub; size_t voff, io; int vseg, iseg;
} LoteDif;
/* Lote ya subido al anillo de la placa por los hilos de vertices (en vez de que el hilo grafico
 * copie todo en flush_batch_real): offsets y copias pendientes. RL_SUBIR_EN_HILOS=0: como antes. */
static volatile int s_cntc[2];
static volatile int* s_cntc_lote = &s_cntc[0];
static int s_lote_sub, s_lote_vseg = -1, s_lote_iseg = -1;
static size_t s_lote_voff, s_lote_io;
static LoteDif s_dif = { .cnt = &s_cnt[1], .cntc = &s_cntc[1], .vseg = -1, .iseg = -1 };
static int s_diferir = -1;
int gx_en_fifo(const void* p);
static size_t vtx_stride(int ntex) { return (size_t)(24 + 12 * ntex + 8); }       /* + capas de textura */
static size_t xvtx_stride(int nraw) { return (size_t)(XVTX_BASE + 8 * nraw + 8); }
uint64_t gl_stat_swap_max_us;
uint64_t gl_stat_draw_calls, gl_stat_batched, gl_stat_tex_uploads;

static int s_gl_null = -1;          /* RL_GL_NULL=1: no dibujar nada (para medir el costo de CPU) */

/* Los vertices, indices y uniforms de todos los lotes van a buffers grandes "anillo":
 * cada lote se escribe a continuacion del anterior (glBufferSubData) y solo cuando se
 * llena se pide memoria nueva. Antes cada lote pedia buffers nuevos (glBufferData) y
 * eso, miles de veces por cuadro, era lo mas caro del hilo grafico en Windows.
 * Tambien se evita repetir llamadas de estado que no cambiaron. RL_GL_SIMPLE=1 vuelve
 * al camino viejo. */
#define VRING_BYTES (48u << 20)
#define IRING_BYTES (16u << 20)
#define URING_BYTES (4u << 20)
static int s_ring;
static size_t s_vring_off, s_iring_off, s_uring_off;
static GLint s_ubo_align = 256;
static uint32_t s_vring_epoch = 1;  /* cambia al pedir memoria nueva para los vertices */
static int s_draw_ready;            /* EFB y viewport puestos para dibujar */
static uint32_t s_ptr_key[2];       /* formato de vertice con que se armaron los punteros de cada VAO */

/* Con GL_ARB_buffer_storage los tres anillos quedan mapeados para siempre ("persistentes"):
 * escribir un lote es un memcpy, sin ninguna llamada a OpenGL. Para no pisar datos que la
 * placa todavia no uso, cada anillo se parte en 4 tramos y al salir de uno se pone una
 * "valla" (fence); antes de volver a escribir en un tramo se espera su valla.
 * RL_GL_PERSISTENTE=0 vuelve a mapear en cada lote. */
typedef struct { GLuint buf; uint8_t* map; size_t size; int seg; GLsync fence[4]; } PRing;
static PRing s_pr_v, s_pr_i, s_pr_u;
static int s_persist;
uint64_t gl_stat_fence_waits;

static int pring_init(PRing* r, GLuint buf, GLenum target, size_t size) {
    typedef void (APIENTRYP PFN_BUFSTORAGE)(GLenum, GLsizeiptr, const void*, GLbitfield);
    static PFN_BUFSTORAGE bs;
    GLbitfield fl = GL_MAP_WRITE_BIT | 0x0040 /* PERSISTENT */ | 0x0080 /* COHERENT */;
    if (!bs) bs = (PFN_BUFSTORAGE)SDL_GL_GetProcAddress("glBufferStorage");
    if (!bs) return 0;
    while (p_glGetError() != 0) {}
    p_glBindBuffer(target, buf);
    bs(target, (GLsizeiptr)size, NULL, fl);
    r->map = (uint8_t*)p_glMapBufferRange(target, 0, (GLsizeiptr)size, fl);
    if (p_glGetError() != 0 || !r->map) return 0;
    r->buf = buf; r->size = size; r->seg = 0;
    memset(r->fence, 0, sizeof r->fence);
    return 1;
}

/* reserva len bytes (alineados) y devuelve el desplazamiento; espera a la placa si hace falta */
static size_t pring_alloc(PRing* r, size_t* cur, size_t len, size_t align) {
    size_t off = (*cur + align - 1) / align * align, q = r->size / 4;
    int s0, s1, k;
    if (off + len > r->size) off = 0;
    s0 = (int)(off / q); s1 = (int)((off + len - 1) / q);
    if (s1 > 3) s1 = 3;
    /* tramos nuevos que se empiezan a escribir: el anterior queda cerrado con una valla */
    for (k = s0; ; k = (k + 1) & 3) {
        if (k != r->seg) {
            if (r->fence[r->seg]) p_glDeleteSync(r->fence[r->seg]);
            r->fence[r->seg] = p_glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
            r->seg = k;
            if (r->fence[k]) {
                GLenum st = p_glClientWaitSync(r->fence[k], GL_SYNC_FLUSH_COMMANDS_BIT, 0);
                if (st == GL_TIMEOUT_EXPIRED) {
                    gl_stat_fence_waits++;
                    p_glClientWaitSync(r->fence[k], GL_SYNC_FLUSH_COMMANDS_BIT, 1000000000ull);
                }
                p_glDeleteSync(r->fence[k]);
                r->fence[k] = 0;
            }
        }
        if (k == s1) break;
    }
    *cur = off + len;
    return off;
}

static void ring_init(void) {
    s_ring = getenv("RL_GL_SIMPLE") == NULL;
    if (!s_ring) return;
    p_glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &s_ubo_align);
    if (s_ubo_align < 16) s_ubo_align = 16;
    {
        const char* e = getenv("RL_GL_PERSISTENTE");
        if (!(e && *e == '0') && has_ext("GL_ARB_buffer_storage")) {
            p_glBindVertexArray(s_vao);
            s_persist = pring_init(&s_pr_v, s_vbo, GL_ARRAY_BUFFER, VRING_BYTES) &&
                        pring_init(&s_pr_i, s_ibo, GL_ELEMENT_ARRAY_BUFFER, IRING_BYTES) &&
                        pring_init(&s_pr_u, s_ubo, GL_UNIFORM_BUFFER, URING_BYTES);
            if (s_persist) {
                p_glBindBufferRange(GL_UNIFORM_BUFFER, 0, s_ubo, 0, sizeof(GxUniforms));
                rt_log("[gl] buffers anillo persistentes (alineacion de uniforms %d)\n", s_ubo_align);
                return;
            }
            rt_log("[gl] no se pudieron mapear los buffers persistentes\n");
            /* los buffers ya no se pueden redimensionar: buffers nuevos */
            p_glGenBuffers(1, &s_vbo); p_glGenBuffers(1, &s_ibo); p_glGenBuffers(1, &s_ubo);
            p_glBindVertexArray(s_vao); p_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s_ibo);
            p_glBindVertexArray(s_xvao); p_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s_ibo);
            p_glBindBufferBase(GL_UNIFORM_BUFFER, 0, s_ubo);
        }
    }
    p_glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
    p_glBufferData(GL_ARRAY_BUFFER, VRING_BYTES, NULL, GL_STREAM_DRAW);
    p_glBindVertexArray(s_vao);
    p_glBufferData(GL_ELEMENT_ARRAY_BUFFER, IRING_BYTES, NULL, GL_STREAM_DRAW);
    p_glBindBuffer(GL_UNIFORM_BUFFER, s_ubo);
    p_glBufferData(GL_UNIFORM_BUFFER, URING_BYTES, NULL, GL_STREAM_DRAW);
    p_glBindBufferRange(GL_UNIFORM_BUFFER, 0, s_ubo, 0, sizeof(GxUniforms));
    rt_log("[gl] buffers anillo (alineacion de uniforms %d)\n", s_ubo_align);
}

/* sube el bloque de uniforms (lo llama apply_key) */
/* Escribe en un buffer anillo sin que el driver espere a la placa: la zona nunca la esta
 * usando un dibujo pendiente (se escribe siempre adelante y al dar la vuelta se pide memoria
 * nueva), asi que se puede mapear "sin sincronizar", como hace Dolphin. RL_GL_SUBDATA=1 usa
 * glBufferSubData. */
static void buf_write(GLenum target, size_t off, size_t len, const void* src) {
    static int sub = -1;
    void* p;
    if (sub < 0) sub = getenv("RL_GL_SUBDATA") != NULL;
    if (!sub) {
        p = p_glMapBufferRange(target, (GLintptr)off, (GLsizeiptr)len,
                               GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT | GL_MAP_INVALIDATE_RANGE_BIT);
        if (p) {
            memcpy(p, src, len);
            p_glUnmapBuffer(target);
            return;
        }
    }
    p_glBufferSubData(target, (GLintptr)off, (GLsizeiptr)len, src);
}

uint64_t gl_stat_xf_snaps, gl_stat_uniform_uploads;
static void upload_uniforms(const GxUniforms* u) {
    size_t sz = sizeof *u, off;
    gl_stat_uniform_uploads++;
    p_glBindBuffer(GL_UNIFORM_BUFFER, s_ubo);
    if (!s_ring) { p_glBufferData(GL_UNIFORM_BUFFER, (GLsizeiptr)sz, u, GL_STREAM_DRAW); return; }
    if (s_persist) {
        off = pring_alloc(&s_pr_u, &s_uring_off, sz, (size_t)s_ubo_align);
        memcpy(s_pr_u.map + off, u, sz);
        p_glBindBufferRange(GL_UNIFORM_BUFFER, 0, s_ubo, (GLintptr)off, (GLsizeiptr)sz);
        return;
    }
    off = (s_uring_off + (size_t)s_ubo_align - 1) / (size_t)s_ubo_align * (size_t)s_ubo_align;
    if (off + sz > URING_BYTES) {
        p_glBufferData(GL_UNIFORM_BUFFER, URING_BYTES, NULL, GL_STREAM_DRAW);
        off = 0;
    }
    buf_write(GL_UNIFORM_BUFFER, off, sz, u);
    p_glBindBufferRange(GL_UNIFORM_BUFFER, 0, s_ubo, (GLintptr)off, (GLsizeiptr)sz);
    s_uring_off = off + sz;
}

/* Sube los vertices e indices del lote; devuelve el vertice base y deja en *ioff el
 * desplazamiento de los indices. Los vertices quedan alineados al tamano del vertice. */
static GLint ring_upload(size_t stride, GLintptr* ioff) {
    size_t voff = (s_vring_off + stride - 1) / stride * stride, io;
    if (s_persist) {
        voff = pring_alloc(&s_pr_v, &s_vring_off, s_vbytes, stride);
        memcpy(s_pr_v.map + voff, s_vbuf, s_vbytes);
        io = pring_alloc(&s_pr_i, &s_iring_off, s_in * 4, 4);
        memcpy(s_pr_i.map + io, s_ibuf, s_in * 4);
        *ioff = (GLintptr)io;
        return (GLint)(voff / stride);
    }
    if (voff + s_vbytes > VRING_BYTES) {
        p_glBufferData(GL_ARRAY_BUFFER, VRING_BYTES, NULL, GL_STREAM_DRAW);
        voff = 0;
        s_vring_epoch++;
    }
    buf_write(GL_ARRAY_BUFFER, voff, s_vbytes, s_vbuf);
    s_vring_off = voff + s_vbytes;
    io = s_iring_off;
    if (io + s_in * 4 > IRING_BYTES) {
        p_glBufferData(GL_ELEMENT_ARRAY_BUFFER, IRING_BYTES, NULL, GL_STREAM_DRAW);
        io = 0;
    }
    buf_write(GL_ELEMENT_ARRAY_BUFFER, io, s_in * 4, s_ibuf);
    s_iring_off = io + s_in * 4;
    *ioff = (GLintptr)io;
    return (GLint)(voff / stride);
}

/* ---- shaders especializados ---- */
static void setup_main_prog(GLuint pr, int xf) {
    int i;
    p_glUseProgram(pr);
    for (i = 0; i < 8; i++) {
        char n[16];
        snprintf(n, sizeof n, "u_tex%d", i);
        p_glUniform1i(p_glGetUniformLocation(pr, n), i);
    }
    if (xf) {
        p_glUniform1i(p_glGetUniformLocation(pr, "u_xf"), 8);
        p_glUniform1i(p_glGetUniformLocation(pr, "u_xft"), 9);
    }
    p_glUniformBlockBinding(pr, p_glGetUniformBlockIndex(pr, "Gx"), 0);
}

typedef struct { int32_t xf, zf, ns, pad; int32_t st[16][4]; } SpecKey;
/* estado: 0 libre, 1 listo, 2 compilandose en otro hilo del driver (mientras tanto se usa
 * el ubershader, que da el mismo resultado pero es mas lento: asi no hay tirones) */
typedef struct { SpecKey k; GLuint prog; volatile int used; uint32_t visto; void* bin; GLint binlen; GLenum binfmt; uint8_t calentado, en_cola, usado; } SpecEnt;   /* used 4: listo en el hilo de shaders (falta guardarlo en la cache) */
#define SPEC_CAP 8192
static int s_sh_on;                 /* hay hilo de shaders */
static void sh_push(SpecEnt* e, int urgente);
static void sh_init(void);
static void sh_poll(void);
static SpecEnt* s_spec;
static int s_spec_n, s_uber = -1;
static GLuint s_cur_prog, s_cur_vao;
uint64_t gl_stat_shaders;
uint64_t gl_stat_gpu_ns, gl_stat_gpu_frames, gl_stat_swap_us, gl_stat_flush_us, gl_stat_prog_switch, gl_stat_tex_binds;

/* funciones opcionales: compilacion en paralelo y binarios de programas (cache en disco) */
typedef void (APIENTRYP PFN_MAXTHREADS)(GLuint);
static PFN_MAXTHREADS p_glMaxShaderCompilerThreads;
static PFNGLGETPROGRAMBINARYPROC p_glGetProgramBinary;
static PFNGLPROGRAMBINARYPROC p_glProgramBinary;
static PFNGLPROGRAMPARAMETERIPROC p_glProgramParameteri;
static int s_async, s_bin;          /* hay compilacion en paralelo / hay binarios */
static GLuint s_vs_main, s_vs_xf;   /* shaders de vertices ya compilados (se comparten) */
static uint32_t s_cache_tag;        /* placa + driver + version del codigo de los shaders */
static const char* SHADER_CACHE = "escuadron2_shaders.cache";
#define CACHE_MAGIC 0x31435352u     /* "RSC1" */

static uint32_t spec_fnv(uint32_t h, const void* p, size_t n) {
    size_t i;
    for (i = 0; i < n; i++) { h ^= ((const uint8_t*)p)[i]; h *= 16777619u; }
    return h;
}
static uint32_t spec_fnv_s(uint32_t h, const char* s) { return s ? spec_fnv(h, s, strlen(s)) : h; }

static int has_ext(const char* name) {
    typedef const GLubyte* (APIENTRYP PFN_GETSTRINGI)(GLenum, GLuint);
    PFN_GETSTRINGI gsi = (PFN_GETSTRINGI)SDL_GL_GetProcAddress("glGetStringi");
    GLint n = 0, i;
    if (!gsi) return 0;
    p_glGetIntegerv(GL_NUM_EXTENSIONS, &n);
    for (i = 0; i < n; i++) {
        const char* e = (const char*)gsi(GL_EXTENSIONS, (GLuint)i);
        if (e && strcmp(e, name) == 0) return 1;
    }
    return 0;
}

static char* spec_source(const SpecKey* k) {
    GxUniforms u;
    memset(&u, 0, sizeof u);
    u.misc[0] = k->ns;
    u.misc[2] = k->pad;
    memcpy(u.stage, k->st, sizeof(int32_t) * 4 * (size_t)k->ns);
    return build_fs(k->zf, &u);
}

/* empieza a enlazar el programa; con compilacion en paralelo vuelve enseguida */
static GLuint spec_start(const SpecKey* k) {
    static const char* n2[14] = { "a_pos", "a_nrm", "a_c0", "a_c1", "a_ia", "a_ib", "a_r0", "a_r1", "a_r2", "a_r3", "a_r4", "a_r5", "a_r6", "a_r7" };
    static const char* n1[11] = { "a_pos", "a_c0", "a_c1", "a_t0", "a_t1", "a_t2", "a_t3", "a_t4", "a_t5", "a_t6", "a_t7" };
    char* src = spec_source(k);
    GLuint p = p_glCreateProgram(), fs = p_glCreateShader(GL_FRAGMENT_SHADER);
    int i;
    const char* cs = src;
    p_glShaderSource(fs, 1, &cs, NULL);
    p_glCompileShader(fs);
    free(src);
    if (k->xf) { if (!s_vs_xf) s_vs_xf = compile(GL_VERTEX_SHADER, VS_XF); }
    else if (!s_vs_main) s_vs_main = compile(GL_VERTEX_SHADER, VS_MAIN);
    p_glAttachShader(p, k->xf ? s_vs_xf : s_vs_main);
    p_glAttachShader(p, fs);
    if (k->xf) for (i = 0; i < 14; i++) p_glBindAttribLocation(p, (GLuint)i, n2[i]);
    else for (i = 0; i < 11; i++) p_glBindAttribLocation(p, (GLuint)i, n1[i]);
    p_glBindAttribLocation(p, 14, "a_lay");
    p_glBindFragDataLocation(p, 0, "o_color");
    if (s_bin) p_glProgramParameteri(p, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);
    p_glLinkProgram(p);
    p_glDeleteShader(fs);   /* se borra cuando se borre el programa */
    return p;
}

static void cache_write(const SpecKey* k, GLenum fmt, GLint len, const void* buf);
static void cache_save(const SpecKey* k, GLuint p) {
    GLint len = 0;
    GLenum fmt = 0;
    void* buf;
    if (!s_bin || !s_cache_tag) return;
    p_glGetProgramiv(p, GL_PROGRAM_BINARY_LENGTH, &len);
    if (len <= 0 || len > (64 << 20)) return;
    buf = malloc((size_t)len);
    p_glGetProgramBinary(p, len, &len, &fmt, buf);
    cache_write(k, fmt, len, buf);
    free(buf);
}
static void cache_write(const SpecKey* k, GLenum fmt, GLint len, const void* buf) {
    FILE* f;
    long pos;
    if (!s_bin || !s_cache_tag || len <= 0) return;
    f = fopen(SHADER_CACHE, "ab");
    if (f) {
        uint32_t hdr[5];
        char* src = spec_source(k);
        fseek(f, 0, SEEK_END);
        pos = ftell(f);
        if (pos <= 0) {     /* archivo nuevo: cabecera */
            uint32_t h[2] = { CACHE_MAGIC, s_cache_tag };
            fwrite(h, sizeof h, 1, f);
        }
        hdr[0] = spec_fnv_s(s_cache_tag, src);
        hdr[1] = (uint32_t)fmt;
        hdr[2] = (uint32_t)len;
        hdr[3] = hdr[4] = 0;
        free(src);
        fwrite(k, sizeof *k, 1, f);
        fwrite(hdr, sizeof hdr, 1, f);
        fwrite(buf, (size_t)len, 1, f);
        fclose(f);
    }
}

/* el programa termino de enlazarse: comprobar y dejarlo listo para usar */
static int spec_finish(SpecEnt* e, int guardar) {
    GLint ok = 0;
    p_glGetProgramiv(e->prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096];
        p_glGetProgramInfoLog(e->prog, sizeof log, NULL, log);
        rt_fatal("OpenGL: error enlazando shaders:\n%s\n", log);
    }
    setup_main_prog(e->prog, e->k.xf);
    s_cur_prog = e->prog;
    e->used = 1;
    if (guardar) cache_save(&e->k, e->prog);
    return 1;
}

static SpecEnt* spec_slot(const SpecKey* k, int* nuevo) {
    uint32_t h = spec_fnv(2166136261u, k, 16 + 16 * (size_t)k->ns);
    SpecEnt* e;
    if (!s_spec) s_spec = (SpecEnt*)calloc(SPEC_CAP, sizeof *s_spec);
    for (e = &s_spec[h % SPEC_CAP];; e = (e == &s_spec[SPEC_CAP - 1]) ? s_spec : e + 1) {
        if (!e->used) { *nuevo = 1; return e; }
        if (memcmp(&e->k, k, sizeof *k) == 0) { *nuevo = 0; return e; }
    }
}

/* Carga los programas guardados en corridas anteriores (si son de esta placa, este
 * driver y esta version de los shaders): asi la primera vez que aparece un efecto
 * en el juego no hay que compilar nada. */
static void cache_load(void) {
    FILE* f;
    uint32_t h[2];
    int n = 0, malos = 0, viejos = 0;
    if (!s_bin) return;
    f = fopen(SHADER_CACHE, "rb");
    if (!f) return;
    if (fread(h, sizeof h, 1, f) != 1 || h[0] != CACHE_MAGIC || h[1] != s_cache_tag) {
        fclose(f);
        remove(SHADER_CACHE);   /* de otra placa, driver o version: se arma de nuevo */
        rt_log("[gl] cache de shaders vieja: se vuelve a armar\n");
        return;
    }
    for (;;) {
        SpecKey k;
        uint32_t hdr[5];
        void* buf;
        if (fread(&k, sizeof k, 1, f) != 1 || fread(hdr, sizeof hdr, 1, f) != 1) break;
        if (k.ns >= 0 && k.ns <= 16) { int s_; for (s_ = 0; s_ < k.ns; s_++) k.st[s_][3] = 0; }
        if (hdr[2] > (64u << 20) || k.ns < 0 || k.ns > 16) break;
        buf = malloc(hdr[2] ? hdr[2] : 1);
        if (fread(buf, hdr[2], 1, f) != 1) { free(buf); break; }
        if (s_spec_n < SPEC_CAP / 2) {
            char* src = spec_source(&k);
            int nuevo;
            SpecEnt* e = spec_slot(&k, &nuevo);
            if (nuevo && spec_fnv_s(s_cache_tag, src) != hdr[0]) viejos++;
            else if (nuevo) {
                GLint ok = 0;
                GLuint p = p_glCreateProgram();
                p_glProgramBinary(p, (GLenum)hdr[1], buf, (GLsizei)hdr[2]);
                p_glGetProgramiv(p, GL_LINK_STATUS, &ok);
                if (ok) {
                    e->k = k; e->prog = p;
                    spec_finish(e, 0);
                    s_spec_n++; n++;
                } else { p_glDeleteProgram(p); malos++; }
            }
            free(src);
        }
        free(buf);
    }
    fclose(f);
    while (p_glGetError() != 0) {}
    if (malos || viejos) {   /* se reescribe solo con los que sirven */
        int i;
        remove(SHADER_CACHE);
        for (i = 0; s_spec && i < SPEC_CAP; i++) if (s_spec[i].used == 1) cache_save(&s_spec[i].k, s_spec[i].prog);
    }
    rt_log("[gl] cache de shaders: %d programas cargados%s\n", n, malos ? " (habia binarios que el driver no acepto)" : "");
}

static void spec_init(void) {
    const char* e = getenv("RL_SHADER_CACHE");
    uint32_t t = 2166136261u;
    if (has_ext("GL_KHR_parallel_shader_compile"))
        p_glMaxShaderCompilerThreads = (PFN_MAXTHREADS)SDL_GL_GetProcAddress("glMaxShaderCompilerThreadsKHR");
    else if (has_ext("GL_ARB_parallel_shader_compile"))
        p_glMaxShaderCompilerThreads = (PFN_MAXTHREADS)SDL_GL_GetProcAddress("glMaxShaderCompilerThreadsARB");
    e = getenv("RL_SHADER_ASYNC");
    s_async = p_glMaxShaderCompilerThreads && (e ? *e != '0' : !gl_headless);
    if (s_async) p_glMaxShaderCompilerThreads(0xFFFFFFFFu);
    if (has_ext("GL_ARB_get_program_binary")) {
        p_glGetProgramBinary = (PFNGLGETPROGRAMBINARYPROC)SDL_GL_GetProcAddress("glGetProgramBinary");
        p_glProgramBinary = (PFNGLPROGRAMBINARYPROC)SDL_GL_GetProcAddress("glProgramBinary");
        p_glProgramParameteri = (PFNGLPROGRAMPARAMETERIPROC)SDL_GL_GetProcAddress("glProgramParameteri");
        if (p_glGetProgramBinary && p_glProgramBinary && p_glProgramParameteri) {
            GLint nf = 0;
            p_glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS, &nf);
            s_bin = nf > 0;
        }
    }
    e = getenv("RL_SHADER_CACHE");
    if (e && *e == '0') s_bin = 0;
    t = spec_fnv_s(t, (const char*)p_glGetString(GL_VENDOR));
    t = spec_fnv_s(t, (const char*)p_glGetString(GL_RENDERER));
    t = spec_fnv_s(t, (const char*)p_glGetString(GL_VERSION));
    t = spec_fnv_s(t, VS_MAIN);
    t = spec_fnv_s(t, VS_XF);
    s_cache_tag = t ? t : 1;
    rt_log("[gl] shaders: compilacion en paralelo %s, cache en disco %s\n", s_async ? "si" : "no", s_bin ? "si" : "no");
    rt_log("[gl] texturas sin enlazar (bindless): %s\n", has_ext("GL_ARB_bindless_texture") ? "si" : "no");
    cache_load();
    sh_init();
    if (s_async) {   /* los shaders de vertices se compilan ya (se comparten y compilarlos espera) */
        if (!s_vs_main) s_vs_main = compile(GL_VERTEX_SHADER, VS_MAIN);
        if (!s_vs_xf) s_vs_xf = compile(GL_VERTEX_SHADER, VS_XF);
    }
    spec_prewarm();
}

/* la prueba de alfa (BP 0xF3) pasa para cualquier alfa? */
static int alpha_always(int32_t ac) {
    int al, q;
    for (al = 0; al < 256; al++) {
        int p[2], logic = (ac >> 22) & 3, pass;
        for (q = 0; q < 2; q++) {
            int fv = (ac >> (16 + 3 * q)) & 7, rv = (ac >> (8 * q)) & 255;
            p[q] = fv == 0 ? 0 : fv == 1 ? al < rv : fv == 2 ? al == rv : fv == 3 ? al <= rv : fv == 4 ? al > rv : fv == 5 ? al != rv : fv == 6 ? al >= rv : 1;
        }
        pass = logic == 0 ? (p[0] && p[1]) : logic == 1 ? (p[0] || p[1]) : logic == 2 ? (p[0] != p[1]) : (p[0] == p[1]);
        if (!pass) return 0;
    }
    return 1;
}

/* Claves conocidas (port/tools/claves_shaders.py): con compilacion en paralelo se
 * compilan al arrancar, en los hilos del driver, mientras el juego muestra la intro. */
typedef struct { int xf, zf, ns, pads; uint32_t st[64]; } ClaveShader;   /* pads: bit 0 sin, bit 1 con prueba de alfa que siempre pasa */
static const ClaveShader s_claves[] = {
#include "shader_claves.inc"
};
static SpecEnt* s_pend[SPEC_CAP];
static int s_npend;

static size_t s_pw_next = 0;   /* proxima clave conocida para compilar (x2: con y sin prueba de alfa) */
static int s_pw_on, s_pw_jugando;

static void spec_prewarm(void) {
    const char* e = getenv("RL_SHADER_PRECALENTAR");
    if (s_sh_on && !(e && *e == '0')) {   /* con el hilo de shaders: se encolan todos de una */
        size_t i; int n = 0, p;
        for (i = 0; i < sizeof s_claves / sizeof s_claves[0]; i++)
            for (p = 0; p < 2; p++) {
                const ClaveShader* c = &s_claves[i];
                SpecKey k;
                SpecEnt* en;
                int nuevo;
                if (!((c->pads >> p) & 1) || s_spec_n >= SPEC_CAP / 2) continue;
                memset(&k, 0, sizeof k);
                k.xf = c->xf; k.zf = c->zf; k.ns = c->ns; k.pad = p;
                memcpy(k.st, c->st, sizeof(int32_t) * 4 * (size_t)k.ns);
                { int s_; for (s_ = 0; s_ < k.ns; s_++) k.st[s_][3] = 0; }   /* konst: va en el uniform */
                en = spec_slot(&k, &nuevo);
                if (!nuevo) continue;
                en->k = k; en->used = 2; s_spec_n++;
                sh_push(en, 0); n++;
            }
        rt_log("[gl] se precalientan %d shaders conocidos en el hilo de shaders\n", n);
        s_pw_on = 0;
        return;
    }
    s_pw_on = s_async && !(e && *e == '0');
    /* Durante el juego no: con NVIDIA el driver compila con varios hilos y le saca CPU al
       juego, al sonido y al hilo grafico (en el nivel 6 todo iba al doble de lento mientras
       compilaba la lista de 2000 shaders). Solo en las pantallas de carga (spec_prewarm_carga);
       RL_PRECALENTAR_JUGANDO=1 vuelve a lo de antes. */
    s_pw_jugando = getenv("RL_PRECALENTAR_JUGANDO") != NULL;
    if (s_pw_on) {
        size_t i; int n = 0;
        for (i = 0; i < sizeof s_claves / sizeof s_claves[0]; i++) n += (s_claves[i].pads & 1) + ((s_claves[i].pads >> 1) & 1);
        rt_log("[gl] se compilan en las pantallas de carga%s hasta %d shaders conocidos\n", s_pw_jugando ? " y durante el juego" : "", n);
    }
}

/* arranca unos pocos por cuadro; si el driver tarda en volver (compila en el momento
   aunque diga que lo hace en paralelo) se deja de precalentar para no trabar el juego */
static int s_pw_lentos;
static void spec_prewarm_step(void) {
    size_t total = 2 * (sizeof s_claves / sizeof s_claves[0]);
    uint64_t t0 = os_now_us();
    int started = 0;
    while (s_pw_next < total && started < 6 && s_npend < 24 && s_spec_n < SPEC_CAP / 2) {
        const ClaveShader* c = &s_claves[s_pw_next / 2];
        SpecKey k;
        SpecEnt* en;
        int nuevo;
        if (!((c->pads >> (s_pw_next & 1)) & 1)) { s_pw_next++; continue; }
        memset(&k, 0, sizeof k);
        k.xf = c->xf; k.zf = c->zf; k.ns = c->ns; k.pad = (int)(s_pw_next & 1);
        memcpy(k.st, c->st, sizeof(int32_t) * 4 * (size_t)k.ns);
        { int s_; for (s_ = 0; s_ < k.ns; s_++) k.st[s_][3] = 0; }   /* konst: va en el uniform */
        s_pw_next++;
        en = spec_slot(&k, &nuevo);
        if (!nuevo) continue;
        en->k = k;
        en->prog = spec_start(&k);
        en->used = 2; en->visto = 0;
        s_spec_n++;
        s_pend[s_npend++] = en;
        started++;
    }
    if (s_pw_next >= total) { s_pw_on = 0; rt_log("[gl] shaders conocidos: todos pedidos\n"); }
    else if (started && os_now_us() - t0 > (uint64_t)started * 15000) {
        if (++s_pw_lentos >= 3) {   /* tres veces mas de 15 ms por shader: el driver compila en el momento */
            s_pw_on = 0;
            rt_log("[gl] el driver compila los shaders en el momento: no se precalientan\n");
        }
    }
}

/* Mientras el juego carga un nivel (pantalla de carga) se compilan shaders conocidos de una,
 * hasta ~80 ms por imagen: ahi un tiron no se nota y despues no traban en el juego (con
 * NVIDIA el driver compila en el momento y el precalentado normal se apaga). */
volatile int gl_juego_cargando;
volatile int gl_dump_tras_carga;
int gl_preparar_shaders = 1;   /* compilar y calentar todos los shaders conocidos al arrancar (con barra); 0 en ajustes.ini: en las pantallas de carga */
static uint64_t s_pw_ult_us;   /* ultima vez que se compilaron conocidos (para el aviso en pantalla) */
/* shaders conocidos que faltan, si se estan compilando ahora (aviso "compilando N shaders") */
int gl_shaders_pendientes(void) {
    size_t total = 2 * (sizeof s_claves / sizeof s_claves[0]);
    if (!s_pw_ult_us || os_now_us() - s_pw_ult_us > 700000 || s_pw_next >= total) return 0;
    return (int)((total - s_pw_next) / 2);
}
static void spec_prewarm_carga(void) {
    size_t total = 2 * (sizeof s_claves / sizeof s_claves[0]);
    uint64_t t0 = os_now_us();
    static int avisado;
    int hechos = 0;
    while (s_pw_next < total && s_spec_n < SPEC_CAP / 2 && os_now_us() - t0 < 80000) {
        const ClaveShader* c = &s_claves[s_pw_next / 2];
        SpecKey k;
        SpecEnt* en;
        int nuevo;
        if (!((c->pads >> (s_pw_next & 1)) & 1)) { s_pw_next++; continue; }
        memset(&k, 0, sizeof k);
        k.xf = c->xf; k.zf = c->zf; k.ns = c->ns; k.pad = (int)(s_pw_next & 1);
        memcpy(k.st, c->st, sizeof(int32_t) * 4 * (size_t)k.ns);
        { int s_; for (s_ = 0; s_ < k.ns; s_++) k.st[s_][3] = 0; }   /* konst: va en el uniform */
        s_pw_next++;
        en = spec_slot(&k, &nuevo);
        if (!nuevo) continue;
        en->k = k;
        en->prog = spec_start(&k);
        s_spec_n++;
        spec_finish(en, 1);
        hechos++;
    }
    if (hechos) { s_cur_prog = 0; s_pw_ult_us = os_now_us(); }   /* spec_finish cambia el programa enlazado */
    if (hechos && !avisado++) rt_log("[gl] pantalla de carga: se compilan shaders conocidos\n");
    if (s_pw_next >= total && avisado < 1000) { avisado = 1000; rt_log("[gl] shaders conocidos: todos compilados\n"); }
}

/* Primera vez (o driver nuevo: la cache de shaders ya no sirve): compilar todos los shaders
 * conocidos ANTES de jugar, con una barra de progreso en la ventana. Sin esto se compilan
 * durante el juego y hay saltos de FPS hasta que estan todos (con NVIDIA el driver compila
 * en el momento). Tarda una sola vez: despues quedan en escuadron2_shaders.cache. */
/* hilos de la preparacion inicial: cada uno con su contexto compartido compila programas
 * (mientras se prepara nadie dibuja, asi que no traban al contexto principal) */
#define PREP_HILOS_MAX 8
typedef struct { SDL_Window* win; SDL_GLContext ctx; pthread_t t; int ok; } PrepHilo;
static SpecKey* s_prep_k;
static GLuint* s_prep_p;
/* el binario de cada programa, sacado en el mismo hilo que lo compilo: con NVIDIA el trabajo
 * de verdad (cientos de ms por programa) se hace recien al usarlo o al pedir el binario, y si
 * eso pasaba en el hilo principal la preparacion tardaba 2 minutos y medio en vez de segundos */
typedef struct { void* bin; GLint len; GLenum fmt; } PrepBin;
static PrepBin* s_prep_bin;
static void prep_terminar(int i) {
    GLint len = 0;
    setup_main_prog(s_prep_p[i], s_prep_k[i].xf);
    p_glUseProgram(0);
    if (!s_bin || !s_cache_tag) return;
    p_glGetProgramiv(s_prep_p[i], GL_PROGRAM_BINARY_LENGTH, &len);
    if (len <= 0 || len > (64 << 20)) return;
    s_prep_bin[i].bin = malloc((size_t)len);
    p_glGetProgramBinary(s_prep_p[i], len, &s_prep_bin[i].len, &s_prep_bin[i].fmt, s_prep_bin[i].bin);
}
static int s_prep_n;
static volatile int s_prep_sig, s_prep_hechos, s_prep_lim, s_prep_fin;
static void* prep_hilo(void* arg) {
    PrepHilo* h = (PrepHilo*)arg;
    if (SDL_GL_MakeCurrent(h->win, h->ctx) != 0) { h->ok = 0; return NULL; }
    h->ok = 1;
    while (!__atomic_load_n(&s_prep_fin, __ATOMIC_ACQUIRE)) {
        int i = __atomic_load_n(&s_prep_sig, __ATOMIC_ACQUIRE);
        GLint listo = 0;
        if (i >= __atomic_load_n(&s_prep_lim, __ATOMIC_ACQUIRE)) { SDL_Delay(1); continue; }
        if (!__atomic_compare_exchange_n(&s_prep_sig, &i, i + 1, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) continue;
        s_prep_p[i] = spec_start(&s_prep_k[i]);
        p_glGetProgramiv(s_prep_p[i], GL_LINK_STATUS, &listo);   /* espera a que termine */
        if (listo) prep_terminar(i);
        __atomic_fetch_add(&s_prep_hechos, 1, __ATOMIC_ACQ_REL);
    }
    p_glFinish();
    SDL_GL_MakeCurrent(h->win, NULL);
    return NULL;
}

/* tres maneras de compilar un tramo [a, b): se mide cual anda mejor con este driver */
static uint64_t s_prep_t_dib;
static void prep_tick(int n) {
    if (os_now_us() - s_prep_t_dib > 50000) { s_prep_t_dib = os_now_us(); prep_eventos(); prep_barra((float)s_prep_hechos / n * 0.9f); }
}
static void prep_secuencial(int a, int b, int n) {
    int j;
    for (j = a; j < b; j++) {
        GLint listo = 0;
        s_prep_p[j] = spec_start(&s_prep_k[j]);
        p_glGetProgramiv(s_prep_p[j], GL_LINK_STATUS, &listo);
        if (listo) prep_terminar(j);
        s_prep_hechos++;
        prep_tick(n);
    }
}
static void prep_paralelo(int a, int b, int n) {   /* KHR_parallel_shader_compile: todos a la vez */
    int j, faltan;
    for (j = a; j < b; j++) { s_prep_p[j] = spec_start(&s_prep_k[j]); prep_tick(n); }
    do {
        faltan = 0;
        for (j = a; j < b; j++) {
            GLint listo = 0;
            if (!s_prep_p[j]) continue;
            p_glGetProgramiv(s_prep_p[j], GL_COMPLETION_STATUS_KHR, &listo);
            if (!listo) faltan++;
        }
        s_prep_hechos = b - faltan - (a > 0 ? 0 : 0);
        prep_tick(n);
        if (faltan) SDL_Delay(2);
    } while (faltan);
    s_prep_hechos = b;
}
static void prep_con_hilos(int a, int b, int n) {
    __atomic_store_n(&s_prep_sig, a, __ATOMIC_RELEASE);
    __atomic_store_n(&s_prep_hechos, a, __ATOMIC_RELEASE);
    __atomic_store_n(&s_prep_lim, b, __ATOMIC_RELEASE);
    while (__atomic_load_n(&s_prep_hechos, __ATOMIC_ACQUIRE) < b) { prep_tick(n); SDL_Delay(5); }
}

static void prep_barra(float fr) {
    int w, h;
    char tit[128];
    SDL_GL_GetDrawableSize(s_win, &w, &h);
    p_glBindFramebuffer(GL_FRAMEBUFFER, 0);
    p_glDisable(GL_SCISSOR_TEST);
    p_glColorMask(1, 1, 1, 1);
    p_glViewport(0, 0, w, h);
    p_glClearColor(0.03f, 0.03f, 0.05f, 1); p_glClear(GL_COLOR_BUFFER_BIT);
    p_glEnable(GL_SCISSOR_TEST);
    p_glScissor(w / 6, h / 2 - h / 80, w * 2 / 3, h / 40);
    p_glClearColor(0.18f, 0.17f, 0.21f, 1); p_glClear(GL_COLOR_BUFFER_BIT);
    p_glScissor(w / 6, h / 2 - h / 80, (int)(w * 2 / 3 * fr) + 1, h / 40);
    p_glClearColor(0.82f, 0.15f, 0.17f, 1); p_glClear(GL_COLOR_BUFFER_BIT);
    p_glDisable(GL_SCISSOR_TEST);
    SDL_GL_SwapWindow(s_win);
    snprintf(tit, sizeof tit, "Escuadron2 - preparing shaders (first time only) %d%%", (int)(fr * 100));
    SDL_SetWindowTitle(s_win, tit);
}

static void prep_eventos(void) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev))
        if (ev.type == SDL_QUIT) { extern FILE* rt_log_file; if (rt_log_file) fflush(rt_log_file); _Exit(0); }
}

/* Primera vez (o driver nuevo: la cache de shaders ya no sirve): compilar todos los shaders
 * conocidos ANTES de jugar, con una barra de progreso en la ventana. Sin esto se compilan
 * durante el juego y hay saltos de FPS hasta que estan todos (con NVIDIA el driver compila
 * en el momento). Se reparten en varios hilos, cada uno con un contexto compartido (de a uno
 * tardaba mas de 5 minutos). Tarda una sola vez: despues quedan en escuadron2_shaders.cache. */
static void prewarm_inicial(void) {
    size_t total = 2 * (sizeof s_claves / sizeof s_claves[0]), i;
    uint64_t t0 = os_now_us(), t_dib = 0;
    int nh = 0, j, n = 0;
    PrepHilo hilos[PREP_HILOS_MAX];
    if (getenv("RL_SIN_PREPARAR_SHADERS") || getenv("RL_UBERSHADER") || (!getenv("RL_PREPARAR_SHADERS") && !(gl_preparar_shaders && !gl_headless) && (gl_headless || !s_pw_on))) return;
    {   const char* mx = getenv("RL_PREPARAR_MAX"); if (mx && (size_t)atoi(mx) < total) total = (size_t)atoi(mx); }   /* pruebas */
    s_prep_k = (SpecKey*)calloc(total, sizeof *s_prep_k);
    s_prep_p = (GLuint*)calloc(total, sizeof *s_prep_p);
    s_prep_bin = (PrepBin*)calloc(total, sizeof *s_prep_bin);
    for (i = 0; i < total; i++) {   /* los conocidos que no vinieron en la cache */
        const ClaveShader* c = &s_claves[i / 2];
        SpecKey k;
        uint32_t hh;
        SpecEnt* e;
        int esta = 0;
        if (!((c->pads >> (i & 1)) & 1)) continue;
        memset(&k, 0, sizeof k);
        k.xf = c->xf; k.zf = c->zf; k.ns = c->ns; k.pad = (int)(i & 1);
        memcpy(k.st, c->st, sizeof(int32_t) * 4 * (size_t)k.ns);
        { int s_; for (s_ = 0; s_ < k.ns; s_++) k.st[s_][3] = 0; }   /* konst: va en el uniform */
        hh = spec_fnv(2166136261u, &k, 16 + 16 * (size_t)k.ns);
        if (s_spec) for (e = &s_spec[hh % SPEC_CAP]; e->used; e = (e == &s_spec[SPEC_CAP - 1]) ? s_spec : e + 1)
            if (!memcmp(&e->k, &k, sizeof k)) { esta = 1; break; }
        if (!esta) {   /* sin repetidos (sin la konst, varias claves conocidas son la misma) */
            int r;
            for (r = n - 1; r >= 0 && memcmp(&s_prep_k[r], &k, sizeof k); r--) {}
            if (r < 0) s_prep_k[n++] = k;
        }
    }
    s_pw_next = 2 * (sizeof s_claves / sizeof s_claves[0]);   /* las pantallas de carga ya no compilan nada */
    if (n < 100 || s_spec_n + n > SPEC_CAP / 2) {
        s_pw_next = 0;   /* pocos (o demasiados): como antes, en las pantallas de carga */
        free(s_prep_k); free(s_prep_p); free(s_prep_bin); s_prep_bin = NULL; return;
    }
    rt_log("[gl] preparando %d shaders antes de jugar (solo la primera vez o con un driver nuevo)\n", n);
    prep_barra(0);
    if (!s_vs_main) s_vs_main = compile(GL_VERTEX_SHADER, VS_MAIN);
    if (!s_vs_xf) s_vs_xf = compile(GL_VERTEX_SHADER, VS_XF);
    p_glFinish();
    s_prep_n = n; s_prep_sig = 0; s_prep_hechos = 0; s_prep_lim = 0; s_prep_fin = 0;
    {   /* hilos con contexto compartido (quietos hasta que se les da un tramo) */
        int cpus = SDL_GetCPUCount(), quiero = cpus - 1;
        const char* e = getenv("RL_PREPARAR_HILOS");
        if (e) quiero = atoi(e);
        if (quiero > PREP_HILOS_MAX) quiero = PREP_HILOS_MAX;
        for (j = 0; j < quiero; j++) {
            PrepHilo* h = &hilos[nh];
            h->win = SDL_CreateWindow("rl-prep", 0, 0, 16, 16, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
            if (!h->win) break;
            SDL_GL_MakeCurrent(s_win, s_ctx);
            SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);
            h->ctx = SDL_GL_CreateContext(h->win);
            SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 0);
            SDL_GL_MakeCurrent(s_win, s_ctx);
            if (!h->ctx) { SDL_DestroyWindow(h->win); break; }
            h->ok = 0;
            if (pthread_create(&h->t, NULL, prep_hilo, h) != 0) { SDL_GL_DeleteContext(h->ctx); SDL_DestroyWindow(h->win); break; }
            nh++;
        }
    }
    {   /* probar las tres maneras con un tramo cada una y seguir con la mas rapida */
        const char* forzar = getenv("RL_PREPARAR_MODO");   /* s, p o h (pruebas) */
        int pos = 0, modo = forzar ? *forzar : 0, prueba = n / 20 > 24 ? 24 : n / 20;
        double r_s = 1e9, r_p = 1e9, r_h = 1e9;
        if (!modo && prueba >= 8) {
            uint64_t t;
            t = os_now_us(); prep_secuencial(pos, pos + prueba, n); r_s = (double)(os_now_us() - t) / prueba; pos += prueba;
            if (s_async) { t = os_now_us(); prep_paralelo(pos, pos + 2 * prueba, n); r_p = (double)(os_now_us() - t) / (2 * prueba); pos += 2 * prueba; }
            if (nh) { t = os_now_us(); prep_con_hilos(pos, pos + 2 * prueba, n); r_h = (double)(os_now_us() - t) / (2 * prueba); pos += 2 * prueba; }
            modo = r_h <= r_p && r_h <= r_s ? 'h' : r_p <= r_s ? 'p' : 's';
            rt_log("[gl] ms por shader: de a uno %.1f, en paralelo %.1f, con %d hilos %.1f -> %s\n", r_s / 1000, r_p < 1e8 ? r_p / 1000 : -1.0,
                   nh, r_h < 1e8 ? r_h / 1000 : -1.0, modo == 'h' ? "hilos" : modo == 'p' ? "paralelo" : "de a uno");
        }
        if (modo == 'h' && !nh) modo = 's';
        if (modo == 'p' && !s_async) modo = 's';
        if (modo == 'h') prep_con_hilos(pos, n, n);
        else if (modo == 'p') { while (pos < n) { int f = pos + 64 < n ? pos + 64 : n; prep_paralelo(pos, f, n); pos = f; } }
        else prep_secuencial(pos, n, n);
        __atomic_store_n(&s_prep_fin, 1, __ATOMIC_RELEASE);
        for (j = 0; j < nh; j++) { pthread_join(hilos[j].t, NULL); SDL_GL_DeleteContext(hilos[j].ctx); SDL_DestroyWindow(hilos[j].win); }
        SDL_GL_MakeCurrent(s_win, s_ctx);
        t_dib = s_prep_t_dib;
    }
    /* en el contexto principal: dejarlos listos y guardarlos en la cache */
    {
        uint64_t t_fin = os_now_us();
        rt_log("[gl] compilados en %.1f s\n", (t_fin - t0) / 1e6);
        for (j = 0; j < n; j++) {
            int nuevo;
            SpecEnt* en = spec_slot(&s_prep_k[j], &nuevo);
            if (!nuevo) { p_glDeleteProgram(s_prep_p[j]); free(s_prep_bin[j].bin); continue; }
            en->k = s_prep_k[j];
            en->prog = s_prep_p[j];
            s_spec_n++;
            if (s_prep_bin[j].bin) {   /* el binario ya salio del hilo que lo compilo */
                spec_finish(en, 0);
                cache_write(&en->k, s_prep_bin[j].fmt, s_prep_bin[j].len, s_prep_bin[j].bin);
                free(s_prep_bin[j].bin);
            } else spec_finish(en, 1);
            if (os_now_us() - t_dib > 50000) { t_dib = os_now_us(); prep_eventos(); prep_barra(0.9f + 0.1f * j / n); }
        }
        rt_log("[gl] guardados en la cache en %.1f s\n", (os_now_us() - t_fin) / 1e6);
    }
    free(s_prep_k); free(s_prep_p); free(s_prep_bin); s_prep_k = NULL; s_prep_p = NULL; s_prep_bin = NULL;
    s_cur_prog = 0;
    SDL_SetWindowTitle(s_win, "Escuadron2");
    p_glBindFramebuffer(GL_FRAMEBUFFER, s_efb_fbo);
    p_glViewport(0, 0, EFB_W * S_, EFB_H * S_);
    p_glClearColor(0, 0, 0, 0);
    s_applied_valid = 0;
    gl_state_reset();
    rt_log("[gl] shaders preparados en %.1f s\n", (os_now_us() - t0) / 1e6);
}

/* al arrancar, despues de cargar/preparar: calentar los programas listos (ver calentar_prog) */
static GLuint calentado_o_uber(SpecEnt* e, GLuint uber);
static void calentar_prog(GLuint pr, int xf);
static void calentar_ent(SpecEnt* e);
static int cal_activo(void);
static uint64_t s_cal_us;
static unsigned s_cal_hechos, s_cal_lentos;
/* escuadron2_shaders.usados: los programas que el juego uso de verdad en partidas anteriores
 * (un numero de 32 bits por programa). Al arrancar se calientan esos primero. */
static const char* SHADER_USADOS = "escuadron2_shaders.usados";
static uint32_t s_uso_nuevos[4096];
static int s_uso_n;
static uint32_t spec_hash(const SpecKey* k) { return spec_fnv(2166136261u, k, 16 + 16 * (size_t)k->ns); }
static void uso_anotar(SpecEnt* e) {
    e->usado = 1;
    if (s_uso_n < 4096) s_uso_nuevos[s_uso_n++] = spec_hash(&e->k);
}
static void uso_guardar(void) {
    FILE* f;
    if (!s_uso_n) return;
    f = fopen(SHADER_USADOS, "ab");
    if (f) { fwrite(s_uso_nuevos, sizeof(uint32_t), (size_t)s_uso_n, f); fclose(f); }
    s_uso_n = 0;
}
static SpecEnt** s_calh_lista;
static int s_calh_n;
static volatile int s_calh_sig, s_calh_hechos, s_calh_fin;
static SpecEnt* s_calq[SPEC_CAP];
static int s_caln;
static uint8_t s_prog_cal[65536];   /* programas calentados (para el registro [lento]) */
/* hilo de calentar: su propio FBO chico y VAO vacio (no se comparten entre contextos) */
static void* calh_hilo(void* arg) {
    PrepHilo* h = (PrepHilo*)arg;
    GLuint fbo, vao, c, z;
    if (SDL_GL_MakeCurrent(h->win, h->ctx) != 0) { h->ok = 0; return NULL; }
    h->ok = 1;
    c = new_tex(4, 4, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    z = new_tex(4, 4, GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL);
    fbo = new_fbo(c, z);
    p_glGenVertexArrays(1, &vao);
    p_glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    p_glViewport(0, 0, 4, 4);
    p_glEnable(GL_SCISSOR_TEST);
    p_glScissor(0, 0, 1, 1);
    p_glBindVertexArray(vao);
    while (!__atomic_load_n(&s_calh_fin, __ATOMIC_ACQUIRE)) {
        int i = __atomic_fetch_add(&s_calh_sig, 1, __ATOMIC_ACQ_REL);
        SpecEnt* e;
        if (i >= s_calh_n) break;
        e = s_calh_lista[i];
        p_glUseProgram(e->prog);
        p_glDrawArrays(GL_TRIANGLES, 0, 3);
        p_glFinish();
        e->calentado = 1;
        __atomic_add_fetch(&s_calh_hechos, 1, __ATOMIC_ACQ_REL);
    }
    p_glUseProgram(0);
    p_glBindFramebuffer(GL_FRAMEBUFFER, 0);
    p_glDeleteFramebuffers(1, &fbo);
    p_glDeleteTextures(1, &c); p_glDeleteTextures(1, &z);
    p_glFinish();
    return NULL;
}
static int u32_cmp(const void* a, const void* b) { uint32_t x = *(const uint32_t*)a, y = *(const uint32_t*)b; return x < y ? -1 : x > y; }
static void calentar_inicial(void) {
    uint64_t t0 = os_now_us(), t_dib = 0, lim;
    int i, hechos = 0, total = 0;
    const char* e = getenv("RL_CALENTAR_MAX_S");
    if (!cal_activo() || getenv("RL_UBERSHADER")) return;
    uint32_t* us = NULL;
    size_t nus = 0;
    int conocidos = 0;
    /* "shaders al arrancar" (opciones): se calientan TODOS los listos, no solo los usados antes,
       y sin limite de tiempo: tarda mas al abrir pero ningun programa traba la primera vez */
    int todos = gl_preparar_shaders && !gl_headless;
    lim = (uint64_t)((e ? atof(e) : todos ? 900.0 : 12.0) * 1e6);
    atexit(uso_guardar);
    calentar_prog(s_prog, 0); calentar_prog(s_prog_z, 0);
    if (s_xprog) { calentar_prog(s_xprog, 1); calentar_prog(s_xprog_z, 1); }
    {   FILE* f = fopen(SHADER_USADOS, "rb");   /* los usados antes (ordenados, para buscar) */
        if (f) {
            long tam;
            fseek(f, 0, SEEK_END); tam = ftell(f); fseek(f, 0, SEEK_SET);
            if (tam > 0 && tam < (16 << 20)) {
                us = (uint32_t*)malloc((size_t)tam);
                nus = fread(us, sizeof(uint32_t), (size_t)tam / 4, f);
                qsort(us, nus, sizeof *us, u32_cmp);
            }
            fclose(f);
        }
    }
    if (todos) { free(us); us = NULL; nus = 0; }
    for (i = 0; s_spec && i < SPEC_CAP; i++) {
        SpecEnt* en = &s_spec[i];
        if ((en->used != 1 && en->used != 4) || en->calentado) continue;
        if (nus) { uint32_t h = spec_hash(&en->k); if (bsearch(&h, us, nus, sizeof *us, u32_cmp)) { en->usado = 1; conocidos++; } }
    }
    total = conocidos;
    if (!nus) for (i = 0; s_spec && i < SPEC_CAP; i++) if ((s_spec[i].used == 1 || s_spec[i].used == 4) && !s_spec[i].calentado) total++;
    free(us);
    if (total) rt_log("[gl] calentando %d programas%s (la primera vez que se dibuja con cada uno el driver traba)\n", total, nus ? " que el juego uso antes" : "");
    /* los usados antes; sin registro todavia (primera vez), todos los listos */
    s_calh_lista = (SpecEnt**)malloc(sizeof(SpecEnt*) * (size_t)(total + 1));
    s_calh_n = 0;
    for (i = 0; s_spec && i < SPEC_CAP && s_calh_n < total; i++) {
        SpecEnt* en = &s_spec[i];
        if ((en->used != 1 && en->used != 4) || en->calentado) continue;
        if (nus && !en->usado) continue;
        s_calh_lista[s_calh_n++] = en;
    }
    s_calh_sig = 0; s_calh_hechos = 0; s_calh_fin = 0;
    {   /* en varios hilos, cada uno con su contexto compartido (al arrancar nadie mas dibuja) */
        PrepHilo hs[PREP_HILOS_MAX];
        int nh = 0, j, quiero = SDL_GetCPUCount() - 1;
        const char* eh = getenv("RL_CALENTAR_HILOS");
        if (eh) quiero = atoi(eh);
        if (quiero > PREP_HILOS_MAX) quiero = PREP_HILOS_MAX;
        if (s_calh_n < 8) quiero = 0;
        for (j = 0; j < quiero; j++) {
            PrepHilo* h = &hs[nh];
            h->win = SDL_CreateWindow("rl-cal", 0, 0, 16, 16, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
            if (!h->win) break;
            SDL_GL_MakeCurrent(s_win, s_ctx);
            SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);
            h->ctx = SDL_GL_CreateContext(h->win);
            SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 0);
            SDL_GL_MakeCurrent(s_win, s_ctx);
            if (!h->ctx) { SDL_DestroyWindow(h->win); break; }
            h->ok = 0;
            if (pthread_create(&h->t, NULL, calh_hilo, h) != 0) { SDL_GL_DeleteContext(h->ctx); SDL_DestroyWindow(h->win); break; }
            nh++;
        }
        if (!nh) {   /* de a uno en el contexto principal */
            while (s_calh_sig < s_calh_n && os_now_us() - t0 < lim) {
                SpecEnt* en = s_calh_lista[s_calh_sig++];
                calentar_prog(en->prog, en->k.xf);
                en->calentado = 1; hechos++;
                if (os_now_us() - t_dib > 50000) { t_dib = os_now_us(); prep_eventos(); prep_barra((float)hechos / s_calh_n); SDL_SetWindowTitle(s_win, "Escuadron2 - preparing graphics"); }
            }
        } else {
            while (__atomic_load_n(&s_calh_hechos, __ATOMIC_ACQUIRE) < s_calh_n && os_now_us() - t0 < lim) {
                prep_eventos();
                prep_barra((float)__atomic_load_n(&s_calh_hechos, __ATOMIC_ACQUIRE) / s_calh_n);
                SDL_SetWindowTitle(s_win, "Escuadron2 - preparing graphics");
                SDL_Delay(30);
            }
            __atomic_store_n(&s_calh_fin, 1, __ATOMIC_RELEASE);
            for (j = 0; j < nh; j++) { pthread_join(hs[j].t, NULL); SDL_GL_DeleteContext(hs[j].ctx); SDL_DestroyWindow(hs[j].win); }
            SDL_GL_MakeCurrent(s_win, s_ctx);
            for (j = 0; j < s_calh_n; j++) if (s_calh_lista[j]->calentado) { hechos++; if (s_calh_lista[j]->prog < 65536) s_prog_cal[s_calh_lista[j]->prog] = 1; }
            rt_log("[gl] calentando con %d hilos\n", nh);
        }
        /* los que no llegaron: en las pantallas de carga */
        for (j = 0; j < s_calh_n; j++) {
            SpecEnt* en = s_calh_lista[j];
            if (!en->calentado && !en->en_cola && s_caln < SPEC_CAP) { en->en_cola = 1; s_calq[s_caln++] = en; }
        }
    }
    free(s_calh_lista); s_calh_lista = NULL;
    SDL_SetWindowTitle(s_win, "Escuadron2");
    p_glBindFramebuffer(GL_FRAMEBUFFER, s_efb_fbo);
    p_glViewport(0, 0, EFB_W * S_, EFB_H * S_);
    s_applied_valid = 0;
    gl_state_reset();
    if (total) rt_log("[gl] %d de %d programas calentados en %.1f s (%u tardaron mas de 8 ms); el resto se dibuja con el ubershader hasta una pantalla de carga\n",
                      hechos, total, (os_now_us() - t0) / 1e6, s_cal_lentos);
}

/* una vez por cuadro: los que terminaron de compilarse quedan listos (y en la cache) */
static void spec_poll(void) {
    int i, j = 0, checked = 0;
    for (i = 0; i < s_npend; i++) {
        SpecEnt* e = s_pend[i];
        if (e->used == 2 && checked < 64) {
            GLint listo = 0;
            checked++;
            p_glGetProgramiv(e->prog, GL_COMPLETION_STATUS_KHR, &listo);
            if (listo) spec_finish(e, 1);
        }
        if (e->used == 2) s_pend[j++] = e;
    }
    s_npend = j;
}


/* ---- hilo de shaders ---------------------------------------------------------------
 * Con NVIDIA el driver compila en el momento aunque diga que lo hace en paralelo: cada
 * shader nuevo trababa el hilo grafico decenas de ms y no se podia precalentar. Ahora los
 * shaders se compilan y enlazan en otro hilo, con su propio contexto de OpenGL que comparte
 * los objetos con el principal (y una ventana escondida). Mientras tanto se dibuja con el
 * ubershader (mismo resultado). Primero los que pide el juego, despues los conocidos
 * (shader_claves.inc), con prioridad baja. Va APAGADO: en una GTX 1660 Ti el driver traba
 * el contexto principal mientras el otro compila (cada dibujo tardaba cientos de ms).
 * RL_SHADER_HILO=1 lo prende para probar. */
static SDL_Window* s_sh_win;
static SDL_GLContext s_sh_ctx;
static pthread_mutex_t s_sh_m = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_sh_cv = PTHREAD_COND_INITIALIZER;
#define SHQ 16384
static SpecEnt* s_shq_u[SHQ]; static unsigned s_shq_uh, s_shq_ut;   /* los que pidio el juego */
static SpecEnt* s_shq_p[SHQ]; static unsigned s_shq_ph, s_shq_pt;   /* precalentar */
static SpecEnt* s_shdone[SHQ]; static unsigned s_shd_h, s_shd_t;
static volatile unsigned s_sh_hechos;
static void sh_push(SpecEnt* e, int urgente) {
    pthread_mutex_lock(&s_sh_m);
    if (urgente) { if (s_shq_ut - s_shq_uh < SHQ) s_shq_u[s_shq_ut++ % SHQ] = e; }
    else if (s_shq_pt - s_shq_ph < SHQ) s_shq_p[s_shq_pt++ % SHQ] = e;
    pthread_cond_signal(&s_sh_cv);
    pthread_mutex_unlock(&s_sh_m);
}
static void* sh_thread(void* arg) {
    static const char* n2[14] = { "a_pos", "a_nrm", "a_c0", "a_c1", "a_ia", "a_ib", "a_r0", "a_r1", "a_r2", "a_r3", "a_r4", "a_r5", "a_r6", "a_r7" };
    static const char* n1[11] = { "a_pos", "a_c0", "a_c1", "a_t0", "a_t1", "a_t2", "a_t3", "a_t4", "a_t5", "a_t6", "a_t7" };
    GLuint vs_main = 0, vs_xf = 0;
    (void)arg;
#ifdef _WIN32
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
#else
    setpriority(PRIO_PROCESS, (id_t)syscall(SYS_gettid), 10);
#endif
    if (SDL_GL_MakeCurrent(s_sh_win, s_sh_ctx) != 0) { rt_log("[gl] hilo de shaders: no se pudo usar su contexto: %s\n", SDL_GetError()); s_sh_on = 0; return NULL; }
    for (;;) {
        SpecEnt* e = NULL;
        GLuint p, fs;
        GLint ok = 0;
        char* src;
        const char* cs;
        int i;
        pthread_mutex_lock(&s_sh_m);
        while (s_shq_uh == s_shq_ut && s_shq_ph == s_shq_pt) pthread_cond_wait(&s_sh_cv, &s_sh_m);
        if (s_shq_uh != s_shq_ut) e = s_shq_u[s_shq_uh++ % SHQ];
        else e = s_shq_p[s_shq_ph++ % SHQ];
        pthread_mutex_unlock(&s_sh_m);
        if (e->used != 2) continue;
        src = spec_source(&e->k);
        cs = src;
        fs = p_glCreateShader(GL_FRAGMENT_SHADER);
        p_glShaderSource(fs, 1, &cs, NULL);
        p_glCompileShader(fs);
        free(src);
        if (e->k.xf) { if (!vs_xf) vs_xf = compile(GL_VERTEX_SHADER, VS_XF); }
        else if (!vs_main) vs_main = compile(GL_VERTEX_SHADER, VS_MAIN);
        p = p_glCreateProgram();
        p_glAttachShader(p, e->k.xf ? vs_xf : vs_main);
        p_glAttachShader(p, fs);
        if (e->k.xf) for (i = 0; i < 14; i++) p_glBindAttribLocation(p, (GLuint)i, n2[i]);
        else for (i = 0; i < 11; i++) p_glBindAttribLocation(p, (GLuint)i, n1[i]);
        p_glBindAttribLocation(p, 14, "a_lay");
        p_glBindFragDataLocation(p, 0, "o_color");
        if (s_bin) p_glProgramParameteri(p, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);
        p_glLinkProgram(p);
        p_glDeleteShader(fs);
        p_glGetProgramiv(p, GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[4096];
            p_glGetProgramInfoLog(p, sizeof log, NULL, log);
            rt_fatal("OpenGL: error enlazando shaders (hilo de shaders):\n%s\n", log);
        }
        setup_main_prog(p, e->k.xf);      /* uniforms de las texturas y el bloque: son del programa */
        p_glUseProgram(0);
        if (s_bin) {
            GLint len = 0;
            p_glGetProgramiv(p, GL_PROGRAM_BINARY_LENGTH, &len);
            if (len > 0 && len <= (64 << 20)) {
                e->bin = malloc((size_t)len);
                p_glGetProgramBinary(p, len, &e->binlen, &e->binfmt, e->bin);
            }
        }
        p_glFinish();                      /* que el otro contexto lo vea terminado */
        e->prog = p;
        __atomic_store_n(&e->used, 4, __ATOMIC_RELEASE);
        pthread_mutex_lock(&s_sh_m);
        if (s_shd_t - s_shd_h < SHQ) s_shdone[s_shd_t++ % SHQ] = e;
        pthread_mutex_unlock(&s_sh_m);
        __atomic_add_fetch(&s_sh_hechos, 1, __ATOMIC_RELAXED);
    }
    return NULL;
}
/* en el hilo grafico, una vez por imagen: guarda en la cache los que terminaron */
static void sh_poll(void) {
    for (;;) {
        SpecEnt* e = NULL;
        pthread_mutex_lock(&s_sh_m);
        if (s_shd_h != s_shd_t) e = s_shdone[s_shd_h++ % SHQ];
        pthread_mutex_unlock(&s_sh_m);
        if (!e) break;
        if (e->bin) { cache_write(&e->k, e->binfmt, e->binlen, e->bin); free(e->bin); e->bin = NULL; }
        e->used = 1;
    }
    {   static unsigned avisados;
        unsigned h = s_sh_hechos;
        if (h - avisados >= 200 || (h != avisados && s_shq_ph == s_shq_pt && s_shq_uh == s_shq_ut)) {
            rt_log("[gl] hilo de shaders: %u listos (%u por precalentar)\n", h, s_shq_pt - s_shq_ph);
            avisados = h;
        }
    }
}
/* crea el contexto compartido y arranca el hilo (en el hilo grafico, al iniciar) */
static void sh_init(void) {
    const char* e = getenv("RL_SHADER_HILO");
    pthread_t t;
    if (!e || *e == '0') return;   /* apagado salvo RL_SHADER_HILO=1: con NVIDIA el contexto compartido traba al principal mientras compila (version U: cada dibujo tardaba cientos de ms) */
    s_sh_win = SDL_CreateWindow("rl-shaders", 0, 0, 16, 16, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!s_sh_win) { rt_log("[gl] hilo de shaders: sin ventana (%s)\n", SDL_GetError()); return; }
    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);
    s_sh_ctx = SDL_GL_CreateContext(s_sh_win);
    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 0);
    SDL_GL_MakeCurrent(s_win, s_ctx);
    if (!s_sh_ctx) { rt_log("[gl] hilo de shaders: sin contexto compartido (%s)\n", SDL_GetError()); SDL_DestroyWindow(s_sh_win); s_sh_win = NULL; return; }
    s_sh_on = 1;
    if (pthread_create(&t, NULL, sh_thread, NULL) != 0) { s_sh_on = 0; return; }
    rt_log("[gl] shaders: se compilan en un hilo aparte\n");
}

/* ---- calentar programas ------------------------------------------------------------
 * Con NVIDIA cada programa traba 50-220 ms la PRIMERA vez que se dibuja con el (aunque venga
 * de la cache o se haya preparado antes): el driver termina de compilarlo en ese momento, y
 * pasa en cada corrida. Una sola vez alcanza (despues no vuelve a trabar). Entonces:
 *  - al arrancar se "calientan" los programas listos con un dibujo de prueba que no pinta
 *    nada, con la barra de progreso (hasta RL_CALENTAR_MAX_S segundos, 25 por defecto);
 *  - durante el juego, un programa que todavia no se dibujo NUNCA traba: se dibuja con el
 *    ubershader (mismo resultado, un poco mas de trabajo para la placa) y se anota;
 *  - los anotados se calientan en las pantallas de carga.
 * RL_SIN_CALENTAR=1 vuelve a lo de antes. */
static int s_cal = -1;
static GLuint s_cal_fbo, s_cal_vao;
static int cal_activo(void) {
    if (s_cal < 0) s_cal = !getenv("RL_SIN_CALENTAR") && !gl_headless;
    return s_cal;
}
static GLuint calentado_o_uber(SpecEnt* e, GLuint uber) {
    if (!e->usado) uso_anotar(e);
    if (e->calentado || !cal_activo()) return e->prog;
    if (!e->en_cola && s_caln < SPEC_CAP) { e->en_cola = 1; s_calq[s_caln++] = e; }
    return uber;
}
/* un dibujo que no pinta nada (triangulo degenerado, 1 pixel de tijera, otro FBO) */
static void calentar_prog(GLuint pr, int xf) {
    uint64_t t0 = os_now_us(), d;
    if (!s_cal_fbo) {
        GLuint c = new_tex(4, 4, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        GLuint z = new_tex(4, 4, GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL);
        s_cal_fbo = new_fbo(c, z);
        p_glGenVertexArrays(1, &s_cal_vao);
    }
    (void)xf;
    p_glBindFramebuffer(GL_FRAMEBUFFER, s_cal_fbo);
    p_glViewport(0, 0, 4, 4);
    p_glEnable(GL_SCISSOR_TEST);
    p_glScissor(0, 0, 1, 1);
    p_glUseProgram(pr);
    p_glBindVertexArray(s_cal_vao);
    p_glDrawArrays(GL_TRIANGLES, 0, 3);
    p_glFinish();   /* el driver puede dejarlo para despues: esperar asi se mide y no se acumula */
    d = os_now_us() - t0;
    s_cal_us += d; s_cal_hechos++;
    if (d > 8000) s_cal_lentos++;
    if (pr < 65536) s_prog_cal[pr] = 1;
    p_glDisable(GL_SCISSOR_TEST);
    s_cur_prog = 0; s_cur_vao = 0; s_draw_ready = 0; s_applied_valid = 0;
}
static void calentar_ent(SpecEnt* e) {
    if (e->calentado) return;
    calentar_prog(e->prog, e->k.xf);
    e->calentado = 1;
}
/* en las pantallas de carga: los que pidio el juego, hasta 'presup' microsegundos */
static void calentar_cola(uint64_t presup) {
    uint64_t t0 = os_now_us();
    int i = 0, j;
    static int avisado;
    if (!s_caln) return;
    gl_state_reset();
    while (i < s_caln && os_now_us() - t0 < presup) {
        SpecEnt* e = s_calq[i++];
        int us = e->used;
        if (us == 5) {   /* anotado en pleno juego: compilarlo ahora */
            e->prog = spec_start(&e->k);
            spec_finish(e, 1);
            calentar_ent(e);
        } else if (us == 1 || us == 4) calentar_ent(e);
        else { s_calq[--i] = e; break; }
    }
    for (j = 0; i < s_caln; i++) s_calq[j++] = s_calq[i];
    if (s_caln != j && avisado < 50) { avisado++; rt_log("[gl] pantalla de carga: %d programas calentados (quedan %d)\n", s_caln - j, j); }
    s_caln = j;
    gl_state_reset();
}

static GLuint prog_for(int xf, int zf, const GxUniforms* u) {
    SpecKey k;
    SpecEnt* e;
    int nuevo;
    GLuint uber;
    if (s_uber < 0) s_uber = getenv("RL_UBERSHADER") != NULL;
    uber = xf ? (zf ? s_xprog_z : s_xprog) : (zf ? s_prog_z : s_prog);
    if (s_uber) return uber;
    memset(&k, 0, sizeof k);
    k.xf = xf; k.zf = zf; k.ns = u->misc[0] > 16 ? 16 : u->misc[0];
    { static int noac = -1; if (noac < 0) noac = getenv("RL_NO_AC_SPEC") != NULL; k.pad = noac ? 0 : alpha_always(u->misc[1]); }
    memcpy(k.st, u->stage, sizeof(int32_t) * 4 * (size_t)k.ns);
    { int s_; for (s_ = 0; s_ < k.ns; s_++) k.st[s_][3] = 0; }   /* konst: va en el uniform */
    e = spec_slot(&k, &nuevo);
    if (!nuevo) {
        int us = __atomic_load_n(&e->used, __ATOMIC_ACQUIRE);
        if (us == 1 || us == 4) return calentado_o_uber(e, uber);
        if (us == 5) return uber;   /* anotado: se compila en la proxima pantalla de carga */
        if (s_sh_on) return uber;
        /* compilandose: preguntar al driver una vez por cuadro como mucho */
        if (e->visto != s_frame) {
            GLint listo = 0;
            e->visto = s_frame;
            p_glGetProgramiv(e->prog, GL_COMPLETION_STATUS_KHR, &listo);
            if (listo) { spec_finish(e, 1); return calentado_o_uber(e, uber); }
        }
        return uber;
    }
    if (s_spec_n >= SPEC_CAP * 3 / 4) return uber;
    e->k = k;
    s_spec_n++;
    gl_stat_shaders++;
    /* Un efecto nuevo en pleno juego: con NVIDIA compilarlo traba cientos de ms (en el nivel 8
       hubo imagenes de 1 y 1,8 s). Se dibuja con el ubershader y se compila en la proxima
       pantalla de carga (calentar_cola). */
    if (cal_activo() && !gl_juego_cargando && !getenv("RL_COMPILAR_JUGANDO")) {
        e->used = 5;
        if (!e->en_cola && s_caln < SPEC_CAP) { e->en_cola = 1; s_calq[s_caln++] = e; }
        return uber;
    }
    if (s_sh_on) { e->used = 2; sh_push(e, 1); return uber; }
    e->prog = spec_start(&k);
    if (s_async) { e->used = 2; e->visto = s_frame; if (s_npend < SPEC_CAP) s_pend[s_npend++] = e; return uber; }
    spec_finish(e, 1);
    return calentado_o_uber(e, uber);
}

static void use_prog_vao(GLuint pr, GLuint vao) {
    if (pr != s_cur_prog || !s_ring) { p_glUseProgram(pr); s_cur_prog = pr; gl_stat_prog_switch++; }
    if (vao != s_cur_vao || !s_ring) { p_glBindVertexArray(vao); s_cur_vao = vao; }
}


/* RL_PERFIL=1 (pruebas): cuenta cuantos pixeles pinta cada programa (consultas de OpenGL
 * esperando el resultado: lento) y al mostrar cada imagen escribe los que mas pintan. */
static int s_perfil = -1;
static GLuint s_perfil_q;
typedef struct { GLuint prog; int ns, uber; uint64_t frags, draws, tris; } PerfilEnt;
static PerfilEnt s_perfil_t[512];
static int s_perfil_n;
static uint64_t s_perfil_frags, s_perfil_draws, s_perfil_uber_frags, s_perfil_uber_draws;
static void perfil_begin(void) {
    if (s_perfil < 0) s_perfil = getenv("RL_PERFIL") != NULL;
    if (!s_perfil) return;
    if (!s_perfil_q) p_glGenQueries(1, &s_perfil_q);
    p_glBeginQuery(GL_SAMPLES_PASSED, s_perfil_q);
}
static void perfil_end(GLuint prog, int ns, int uber, uint32_t idx) {
    GLuint64 n = 0;
    int i;
    if (!s_perfil) return;
    p_glEndQuery(GL_SAMPLES_PASSED);
    p_glGetQueryObjectui64v(s_perfil_q, GL_QUERY_RESULT, &n);
    s_perfil_frags += n; s_perfil_draws++;
    if (uber) { s_perfil_uber_frags += n; s_perfil_uber_draws++; }
    for (i = 0; i < s_perfil_n && s_perfil_t[i].prog != prog; i++) {}
    if (i == s_perfil_n && s_perfil_n < 512) { s_perfil_t[i].prog = prog; s_perfil_t[i].ns = ns; s_perfil_t[i].uber = uber; s_perfil_n++; }
    if (i < 512) { s_perfil_t[i].frags += n; s_perfil_t[i].draws++; s_perfil_t[i].tris += idx / 3; }
}
static int perfil_cmp(const void* a, const void* b) {
    uint64_t x = ((const PerfilEnt*)a)->frags, y = ((const PerfilEnt*)b)->frags;
    return x < y ? 1 : x > y ? -1 : 0;
}
static void perfil_frame(void) {
    int i;
    if (s_perfil <= 0 || !s_perfil_draws) return;
    qsort(s_perfil_t, (size_t)s_perfil_n, sizeof *s_perfil_t, perfil_cmp);
    rt_log("[perfil] imagen: %llu llamadas, %.2f Mpx (%.1f pantallas de %dx%d); ubershader: %llu llamadas, %.2f Mpx; %d programas\n",
           (unsigned long long)s_perfil_draws, s_perfil_frags / 1e6, (double)s_perfil_frags / (EFB_W * S_ * EFB_H * S_), EFB_W * S_, EFB_H * S_,
           (unsigned long long)s_perfil_uber_draws, s_perfil_uber_frags / 1e6, s_perfil_n);
    for (i = 0; i < s_perfil_n && i < 12; i++)
        rt_log("[perfil]   prog %u etapas %d%s: %.2f Mpx, %llu llamadas, %llu triangulos\n", s_perfil_t[i].prog, s_perfil_t[i].ns, s_perfil_t[i].uber ? " UBER" : "",
               s_perfil_t[i].frags / 1e6, (unsigned long long)s_perfil_t[i].draws, (unsigned long long)s_perfil_t[i].tris);
    s_perfil_n = 0; s_perfil_frags = s_perfil_draws = s_perfil_uber_frags = s_perfil_uber_draws = 0;
    memset(s_perfil_t, 0, sizeof s_perfil_t);
}
void gl_perfil_informe(uint32_t a) { (void)a; flush_batch(); perfil_frame(); }
static void flush_batch_real(void);
void gl_vtx_esperar(void);
static void vj_esperar_cnt(volatile int* c);
static void vj_encolar_copia(const uint8_t* src, uint8_t* dst, size_t len, volatile int* esp, volatile int* cnt);
static int s_vj_hilos;
int gl_subir_en_hilos;   /* --subir-en-hilos (en prueba: en una PC con Windows el juego se quedo quieto al arrancar) */
static void apply_key(const DrawKey* k);
/* intercambia el lote que se esta armando con el diferido */
static void dif_cambiar(void) {
    LoteDif* d = &s_dif;
    DrawKey kt;
#define DIF_SW(T, a, b) do { T t_ = (a); (a) = (b); (b) = t_; } while (0)
    DIF_SW(uint8_t*, s_vbuf, d->vbuf); DIF_SW(size_t, s_vbytes, d->vbytes); DIF_SW(size_t, s_vcap, d->vcap); DIF_SW(size_t, s_vn, d->vn);
    DIF_SW(uint32_t*, s_ibuf, d->ibuf); DIF_SW(size_t, s_in, d->in); DIF_SW(size_t, s_icap, d->icap);
    DIF_SW(int, s_vtex, d->vtex); DIF_SW(int, s_vxf, d->vxf); DIF_SW(int, s_vraw, d->vraw); DIF_SW(int, s_vzf, d->vzf);
    DIF_SW(volatile int*, s_cnt_lote, d->cnt); DIF_SW(volatile int*, s_cntc_lote, d->cntc);
    DIF_SW(int, s_lote_sub, d->sub); DIF_SW(size_t, s_lote_voff, d->voff); DIF_SW(size_t, s_lote_io, d->io);
    DIF_SW(int, s_lote_vseg, d->vseg); DIF_SW(int, s_lote_iseg, d->iseg);
#undef DIF_SW
    kt = s_key_pending; s_key_pending = d->key; d->key = kt;
}
/* manda el lote diferido (si hay) */
static void dif_mandar(void) {
    uint64_t t0;
    if (!s_dif.valido) return;
    s_dif.valido = 0;
    t0 = os_now_us();
    dif_cambiar();
    flush_batch_real();
    dif_cambiar();
    gl_stat_flush_us += os_now_us() - t0;
}
static void flush_batch(void) {
    uint64_t t0;
    dif_mandar();
    if (!s_in) { vj_esperar_cnt(s_cnt_lote); s_vn = 0; s_vbytes = 0; return; }
    t0 = os_now_us();
    flush_batch_real();
    gl_stat_flush_us += os_now_us() - t0;
}
/* cierra el lote que se esta armando: el diferido anterior se manda y este queda diferido */
static void dif_cerrar(void) {
    if (!s_in) { flush_batch(); return; }
    dif_mandar();
    dif_cambiar();          /* el armado pasa a diferido; el armado queda con los buffers vacios */
    s_dif.valido = 1;
    s_vn = 0; s_vbytes = 0; s_in = 0;
    {   /* que los hilos de vertices lo suban al anillo de la placa apenas terminen de leerlo */
        static int en_hilos = -1;
        LoteDif* d = &s_dif;
        if (en_hilos < 0) { const char* e = getenv("RL_SUBIR_EN_HILOS"); en_hilos = (e && *e == '1') || gl_subir_en_hilos; }
        if (s_gl_null < 0) s_gl_null = getenv("RL_GL_NULL") != NULL;
        d->sub = 0;
        if (en_hilos && s_persist && s_vj_hilos > 0 && d->vxf && d->in && !s_gl_null) {
            size_t stride = xvtx_stride(d->vraw), pedazo = 256u << 10, o;
            int sv = s_pr_v.seg, si = s_pr_i.seg;
            d->voff = pring_alloc(&s_pr_v, &s_vring_off, d->vbytes, stride);
            d->io = pring_alloc(&s_pr_i, &s_iring_off, d->in * 4, 4);
            /* si al reservar se cerro un tramo con su valla, esa valla no cubre este dibujo
               (todavia no se mando): se rehace despues de mandarlo */
            d->vseg = s_pr_v.seg != sv ? sv : -1;
            d->iseg = s_pr_i.seg != si ? si : -1;
            for (o = 0; o < d->vbytes; o += pedazo)
                vj_encolar_copia(d->vbuf + o, s_pr_v.map + d->voff + o, d->vbytes - o < pedazo ? d->vbytes - o : pedazo, d->cnt, d->cntc);
            vj_encolar_copia((const uint8_t*)d->ibuf, s_pr_i.map + d->io, d->in * 4, d->cnt, d->cntc);
            d->sub = 1;
        }
    }
}
/* Diagnostico de tirones: llamadas de dibujo que tardan mucho en la CPU (el driver compila o
 * recompila el programa la primera vez que se usa con un estado nuevo). Se anotan las primeras. */
static uint8_t s_prog_visto[65536];
static unsigned s_lentos_avisos, s_prog_nuevos_cuadro;
static uint64_t s_lento_t0;
static void lento_antes(void) { s_lento_t0 = os_now_us(); }
static void lento_despues(int n) {
    uint64_t d = os_now_us() - s_lento_t0;
    GLuint pr = s_cur_prog;
    int nuevo = 0;
    if (pr < 65536 && !s_prog_visto[pr]) { extern uint64_t g_prog_nuevos_total; s_prog_visto[pr] = 1; nuevo = 1; s_prog_nuevos_cuadro++; g_prog_nuevos_total++; }
    if (d > 8000 && s_lentos_avisos < 200) {
        s_lentos_avisos++;
        rt_log("[lento] dibujo de %.1f ms (cuadro %u, programa %u%s, %d etapas, %d indices)\n", d / 1000.0, s_frame, pr,
               nuevo ? (pr < 65536 && s_prog_cal[pr] ? ", primera vez, YA CALENTADO" : ", primera vez") : "", s_key_pending.u.misc[0], n);
    }
}

static void flush_batch_real(void) {
    vj_esperar_cnt(s_cnt_lote);   /* los vertices del lote tienen que estar escritos */
    vj_esperar_cnt(s_cntc_lote);  /* y subidos, si los suben los hilos */
    if (s_gl_null < 0) s_gl_null = getenv("RL_GL_NULL") != NULL;
    if (!s_in) { s_vn = 0; s_vbytes = 0; return; }
    /* el estado del lote (con el lote diferido, todavia no esta puesto) */
    if (!s_applied_valid || memcmp(&s_key_pending, &s_key_applied, sizeof s_key_pending)) apply_key(&s_key_pending);
    if (s_gl_null) { s_vn = 0; s_vbytes = 0; s_in = 0; return; }
    if (!s_draw_ready || !s_ring) {
        p_glBindFramebuffer(GL_FRAMEBUFFER, s_efb_fbo);
        p_glViewport(0, 0, EFB_W * S_, EFB_H * S_);
        s_draw_ready = 1;
    }
    if (s_vxf) {
        GLsizei stride = (GLsizei)xvtx_stride(s_vraw);
        int i;
        GLint basev = 0;
        GLintptr ioff = 0;
        if (s_pool_up < s_pool_n) {     /* bloques de XF nuevos */
            p_glBindBuffer(GL_TEXTURE_BUFFER, s_xf_buf);
            buf_write(GL_TEXTURE_BUFFER, (size_t)s_pool_up * 64, (size_t)(s_pool_n - s_pool_up) * 64, s_pool + (size_t)s_pool_up * 16);
            s_pool_up = s_pool_n;
        }
        if (s_snap_up < s_snap_n) {     /* estados de XF nuevos */
            p_glBindBuffer(GL_TEXTURE_BUFFER, s_xft_buf);
            buf_write(GL_TEXTURE_BUFFER, (size_t)s_snap_up * XF_BLOCKS * 4, (size_t)(s_snap_n - s_snap_up) * XF_BLOCKS * 4, s_snap + (size_t)s_snap_up * XF_BLOCKS);
            s_snap_up = s_snap_n;
        }
        use_prog_vao(prog_for(1, s_vzf, &s_key_pending.u), s_xvao);
        p_glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
        if (s_lote_sub) { basev = (GLint)(s_lote_voff / (size_t)stride); ioff = (GLintptr)s_lote_io; }
        else if (s_ring) basev = ring_upload((size_t)stride, &ioff);
        else p_glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)s_vbytes, s_vbuf, GL_STREAM_DRAW);
        if (!s_ring || s_ptr_key[1] != (s_vring_epoch << 8 | (uint32_t)s_vraw)) {
            static int enabled = 0;     /* atributos de coordenadas crudas habilitados en s_xvao */
            p_glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
            p_glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)12);
            p_glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (void*)24);
            p_glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (void*)28);
            p_glVertexAttribIPointer(4, 4, GL_UNSIGNED_SHORT, stride, (void*)32);
            p_glVertexAttribIPointer(5, 4, GL_UNSIGNED_SHORT, stride, (void*)40);
            for (i = 0; i < 8; i++) {
                if (i < s_vraw) {
                    p_glVertexAttribPointer((GLuint)(6 + i), 2, GL_FLOAT, GL_FALSE, stride, (void*)(uintptr_t)(XVTX_BASE + 8 * i));
                    if (i >= enabled) p_glEnableVertexAttribArray((GLuint)(6 + i));
                } else if (i < enabled) {
                    p_glDisableVertexAttribArray((GLuint)(6 + i));
                    p_glVertexAttrib2f((GLuint)(6 + i), 0.0f, 0.0f);
                }
            }
            enabled = s_vraw;
            p_glVertexAttribIPointer(14, 2, GL_UNSIGNED_INT, stride, (void*)(uintptr_t)(XVTX_BASE + 8 * s_vraw));
            p_glEnableVertexAttribArray(14);
            s_ptr_key[1] = s_vring_epoch << 8 | (uint32_t)s_vraw;
        }
        perfil_begin();
        lento_antes();
        if (s_ring) p_glDrawElementsBaseVertex(GL_TRIANGLES, (GLsizei)s_in, GL_UNSIGNED_INT, (void*)ioff, basev);
        else {
            p_glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(s_in * 4), s_ibuf, GL_STREAM_DRAW);
            p_glDrawElements(GL_TRIANGLES, (GLsizei)s_in, GL_UNSIGNED_INT, (void*)0);
        }
        lento_despues((int)s_in);
        perfil_end(s_cur_prog, s_key_pending.u.misc[0], s_cur_prog == s_prog || s_cur_prog == s_prog_z || s_cur_prog == s_xprog || s_cur_prog == s_xprog_z, s_in);
        gl_stat_draw_calls++;
        if (s_lote_sub) {   /* las vallas cerradas al reservar este lote, ahora que ya se mando */
            if (s_lote_vseg >= 0 && s_pr_v.fence[s_lote_vseg]) { p_glDeleteSync(s_pr_v.fence[s_lote_vseg]); s_pr_v.fence[s_lote_vseg] = p_glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0); }
            if (s_lote_iseg >= 0 && s_pr_i.fence[s_lote_iseg]) { p_glDeleteSync(s_pr_i.fence[s_lote_iseg]); s_pr_i.fence[s_lote_iseg] = p_glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0); }
            s_lote_sub = 0; s_lote_vseg = s_lote_iseg = -1;
        }
        s_vn = 0; s_vbytes = 0; s_in = 0;
        return;
    }
    {
        GLsizei stride = (GLsizei)vtx_stride(s_vtex);
        GLint basev = 0;
        GLintptr ioff = 0;
        int i;
        use_prog_vao(prog_for(0, s_vzf, &s_key_pending.u), s_vao);
        p_glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
        if (s_ring) basev = ring_upload((size_t)stride, &ioff);
        else p_glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)s_vbytes, s_vbuf, GL_STREAM_DRAW);
        if (!s_ring || s_ptr_key[0] != (s_vring_epoch << 8 | (uint32_t)s_vtex)) {
            p_glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, stride, (void*)0);
            p_glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (void*)16);
            p_glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (void*)20);
            for (i = 0; i < 8; i++) {
                if (i < s_vtex) {
                    p_glVertexAttribPointer((GLuint)(3 + i), 3, GL_FLOAT, GL_FALSE, stride, (void*)(uintptr_t)(24 + 12 * i));
                    if (s_attr_tex < 0 || i >= s_attr_tex) p_glEnableVertexAttribArray((GLuint)(3 + i));
                } else if (s_attr_tex < 0 || i < s_attr_tex) {
                    p_glDisableVertexAttribArray((GLuint)(3 + i));
                    p_glVertexAttrib3f((GLuint)(3 + i), 0.0f, 0.0f, 1.0f);
                }
            }
            s_attr_tex = s_vtex;
            p_glVertexAttribIPointer(14, 2, GL_UNSIGNED_INT, stride, (void*)(uintptr_t)(24 + 12 * s_vtex));
            p_glEnableVertexAttribArray(14);
            s_ptr_key[0] = s_vring_epoch << 8 | (uint32_t)s_vtex;
        }
        perfil_begin();
        lento_antes();
        if (s_ring) p_glDrawElementsBaseVertex(GL_TRIANGLES, (GLsizei)s_in, GL_UNSIGNED_INT, (void*)ioff, basev);
        else {
            p_glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(s_in * 4), s_ibuf, GL_STREAM_DRAW);
            p_glDrawElements(GL_TRIANGLES, (GLsizei)s_in, GL_UNSIGNED_INT, (void*)0);
        }
        lento_despues((int)s_in);
        perfil_end(s_cur_prog, s_key_pending.u.misc[0], s_cur_prog == s_prog || s_cur_prog == s_prog_z || s_cur_prog == s_xprog || s_cur_prog == s_xprog_z, s_in);
        gl_stat_draw_calls++;
        s_vn = 0; s_vbytes = 0; s_in = 0;
    }
}

/* Lo llaman las funciones que tocan el estado de OpenGL por su cuenta. */
static int read_png(const char* path, GLuint fbo, int x, int y, int w, int h);
static void gl_state_reset(void) {
    int i;
    flush_batch();
    for (i = 0; i < 8; i++) p_glBindSampler((GLuint)i, 0);
    s_applied_valid = 0;
    s_draw_ready = 0; s_cur_prog = 0; s_cur_vao = 0;
}

/* Plano de profundidad para "zfreeze" (como Dolphin, VertexManagerBase::CalculateZSlope):
 * sale de los 3 ultimos vertices de cada dibujo de triangulos sin zfreeze. */
static float s_zslope[4];
static int s_zslope_ok;
static uint32_t s_zslope_gen;
static int s_zfreeze_off = -1;      /* RL_NO_ZFREEZE=1: ignorarlo (como antes) */

static void zslope_update(const float c[3][4], const GxUniforms* u) {
    float X[3], Y[3], Z[3], a, b, cc, dx, dy;
    int i;
    for (i = 0; i < 3; i++) {
        float w = c[i][3];
        if (w == 0.0f) return;
        X[i] = c[i][0] / w * u->vp[0] + u->vp[2];
        Y[i] = c[i][1] / w * u->vp[1] + u->vp[3];
        Z[i] = c[i][2] / w * u->vpz[0] + u->vpz[1];
    }
    {
        float ax = X[1] - X[0], ay = Y[1] - Y[0], az = Z[1] - Z[0];
        float bx = X[2] - X[0], by = Y[2] - Y[0], bz = Z[2] - Z[0];
        a = ay * bz - az * by;
        b = az * bx - ax * bz;
        cc = ax * by - ay * bx;
    }
    if (cc == 0.0f) return;
    dx = -a / cc; dy = -b / cc;
    /* en pixeles de la ventana (el EFB va escalado por S_) */
    s_zslope[0] = dx / (float)S_;
    s_zslope[1] = dy / (float)S_;
    s_zslope[2] = Z[0] - (X[0] * dx + Y[0] * dy);
    s_zslope[3] = 0.0f;
    s_zslope_ok = 1;
    s_zslope_gen++;
}

static void build_key(DrawKey* k, int prim_is_tri) {
    GxUniforms* u = &k->u;
    uint32_t so = bpr(0x59);
    float offx = (float)((int)((so & 0x3FF) * 2) - 342), offy = (float)((int)(((so >> 10) & 0x3FF) * 2) - 342);
    int ns = (int)((bpr(0x00) >> 10) & 15) + 1, s, i;
    memset(k, 0, sizeof *k);
    u->vp[0] = xf_rf(0x1A); u->vp[1] = xf_rf(0x1B);
    u->vp[2] = xf_rf(0x1D) - 342.0f - offx; u->vp[3] = xf_rf(0x1E) - 342.0f - offy;
    u->vpz[0] = xf_rf(0x1C); u->vpz[1] = xf_rf(0x1F);
    u->misc[0] = ns; u->misc[1] = (int32_t)bpr(0xF3);
    {   /* niebla: BP 0xEE (a), 0xEF (b), 0xF0 (b_shift), 0xF1 (c, proyeccion, funcion), 0xF2 (color) */
        uint32_t p0 = bpr(0xEE), p3 = bpr(0xF1), col = bpr(0xF2);
        uint32_t fa = ((p0 >> 19) & 1) << 31 | ((p0 >> 11) & 0xFF) << 23 | (p0 & 0x7FF) << 12;
        uint32_t fc = ((p3 >> 19) & 1) << 31 | ((p3 >> 11) & 0xFF) << 23 | (p3 & 0x7FF) << 12;
        u->fogi[0] = (int32_t)((p3 >> 21) & 7);
        { static int nf = -1; if (nf < 0) nf = getenv("RL_NO_FOG") != NULL; if (nf) u->fogi[0] = 0; }
        {
            static int tr = -1; static uint32_t last3 = 0xFFFFFFFF;
            if (tr < 0) tr = getenv("RL_FOG_TRACE") != NULL;
            if (tr && p3 != last3) { last3 = p3; rt_log("[niebla] cuadro %u: fsel %d a %08X b %06X shift %u c %08X color %06X\n", s_frame, u->fogi[0], p0, bpr(0xEF), bpr(0xF0) & 31, p3, col); }
        }
        if (u->fogi[0] >= 2) {
            u->fogf[0] = u32_f(fa); u->fogf[1] = u32_f(fc);
            u->fogi[1] = (int32_t)(bpr(0xEF) & 0xFFFFFF);
            u->fogi[2] = (int32_t)((p3 >> 20) & 1);
            u->fogi[3] = (int32_t)(bpr(0xF0) & 0x1F);
            u->fogc[0] = (int32_t)((col >> 16) & 255); u->fogc[1] = (int32_t)((col >> 8) & 255); u->fogc[2] = (int32_t)(col & 255);
        }
    }
    for (s = 0; s < ns; s++) {
        uint32_t order = (bpr(0x28 + s / 2) >> ((s & 1) * 12)) & 0xFFF;
        uint32_t ksel = bpr(0xF6 + s / 2);
        int kc = (int)((ksel >> ((s & 1) ? 14 : 4)) & 31), ka = (int)((ksel >> ((s & 1) ? 19 : 9)) & 31);
        u->stage[s][0] = (int32_t)order;
        u->stage[s][1] = (int32_t)bpr(0xC0 + 2 * s);
        u->stage[s][2] = (int32_t)bpr(0xC1 + 2 * s);
        u->stage[s][3] = kc | (ka << 8);
        if ((order >> 6) & 1) k->used |= 1u << (order & 7);
    }
    {   /* texturas indirectas: BP 0x10.. (IND_CMD por etapa), 0x27 (IREF), 0x25/0x26 (escalas), 0x06.. (matrices) */
        int nind = (int)((bpr(0x00) >> 16) & 7), m;
        if (nind > 4) nind = 4;
        if (nind) {
            static int dicho;
            if (!dicho++) rt_log("[gl] el juego usa texturas indirectas (cuadro %u)\n", s_frame);
            for (s = 0; s < ns; s++) u->indc[s >> 2][s & 3] = (int32_t)(bpr(0x10 + s) & 0x1FFFFF);
            u->indr[0] = (int32_t)(bpr(0x27) & 0xFFFFFF); u->indr[1] = nind;
            u->indr[2] = (int32_t)(bpr(0x25) & 0xFFFF); u->indr[3] = (int32_t)(bpr(0x26) & 0xFFFF);
            for (m = 0; m < 3; m++) {
                uint32_t A = bpr(0x06 + 3 * m), B = bpr(0x07 + 3 * m), C = bpr(0x08 + 3 * m);
                int sc = (int)(((A >> 22) & 3) | (((B >> 22) & 3) << 2) | (((C >> 22) & 3) << 4));
#define SX11(v) ((int32_t)((v) & 0x7FF) - (((v) & 0x400) ? 0x800 : 0))
                u->indm[2 * m][0] = SX11(A); u->indm[2 * m][1] = SX11(B); u->indm[2 * m][2] = SX11(C); u->indm[2 * m][3] = 17 - sc;
                u->indm[2 * m + 1][0] = SX11(A >> 11); u->indm[2 * m + 1][1] = SX11(B >> 11); u->indm[2 * m + 1][2] = SX11(C >> 11); u->indm[2 * m + 1][3] = 17 - sc;
#undef SX11
            }
            for (m = 0; m < nind; m++) k->used |= 1u << ((u->indr[0] >> (6 * m)) & 7);
        }
    }
    for (i = 0; i < 4; i++) {
        uint32_t a = bpr(0xF6 + 2 * i), b = bpr(0xF7 + 2 * i);
        tev_pair(gx.tev_reg[i][0], gx.tev_reg[i][1], u->regs[i]);
        tev_pair(gx.tev_konst[i][0], gx.tev_konst[i][1], u->konst[i]);
        u->swap[i][0] = (int32_t)(a & 3); u->swap[i][1] = (int32_t)((a >> 2) & 3);
        u->swap[i][2] = (int32_t)(b & 3); u->swap[i][3] = (int32_t)((b >> 2) & 3);
    }
    s_key_lay[0] = s_key_lay[1] = 0;
    for (i = 0; i < 8; i++) {
        int lmag, lmin, ws, wt, mip, bias, lay;
        if (!(k->used & (1u << i))) continue;
        k->tex[i] = texture_for(i, &lmag, &lmin, &ws, &wt, &mip, &bias, &lay);
        s_key_lay[i >> 2] |= (uint32_t)(lay & 255) << (8 * (i & 3));
        if (u->indr[1]) { TexInfo ti; tex_info(i, &ti); u->tdim[i][0] = ti.w; u->tdim[i][1] = ti.h; }
        k->tpar[i][0] = (uint8_t)lmag; k->tpar[i][1] = (uint8_t)lmin;
        k->tpar[i][2] = (uint8_t)ws; k->tpar[i][3] = (uint8_t)wt;
        k->tpar[i][4] = (uint8_t)mip; k->tpar[i][5] = (uint8_t)(int8_t)bias;
    }
    k->z = bpr(0x40);
    k->cm = bpr(0x41) & 0xFFFFFF;
    if ((bpr(0x43) & 7) != 1) k->cm |= 1u << 31;   /* el EFB no guarda alfa (RGB8 o RGB565): el alfa de destino vale 1 */
    k->cm1 = bpr(0x42) & 0x1FF;
    {
        uint32_t sc0 = bpr(0x20), sc1 = bpr(0x21);
        int ox = (int)((so & 0x3FF) * 2) - 342, oy = (int)(((so >> 10) & 0x3FF) * 2) - 342;
        int y0 = (int)(sc0 & 0x7FF) - 342 - oy, x0 = (int)((sc0 >> 12) & 0x7FF) - 342 - ox;
        int y1 = (int)(sc1 & 0x7FF) - 342 - oy, x1 = (int)((sc1 >> 12) & 0x7FF) - 342 - ox;
        k->sc[0] = x0; k->sc[1] = y0;
        k->sc[2] = x1 - x0 + 1 > 0 ? x1 - x0 + 1 : 0; k->sc[3] = y1 - y0 + 1 > 0 ? y1 - y0 + 1 : 0;
    }
    {
        int cull = (int)((bpr(0x00) >> 14) & 3);
        k->cull = prim_is_tri ? cull : 0;
    }
    k->ntex = (int32_t)(gx.xf_regs[0x3F] & 15);
    if (k->ntex > 8) k->ntex = 8;
    if (((bpr(0x00) >> 19) & 1) && (k->z & 1) && s_zslope_ok && !s_zfreeze_off) {
        /* "zfreeze": la profundidad no sale de los vertices sino del plano del ultimo
           triangulo dibujado sin zfreeze (asi el juego manda el cielo al fondo) */
        k->zf = 1;
        memcpy(u->zs, s_zslope, sizeof s_zslope);
    }
}

static GLuint sampler_for(const uint8_t p[8]) {
    static const GLint wraps[4] = { GL_CLAMP_TO_EDGE, GL_REPEAT, GL_MIRRORED_REPEAT, GL_REPEAT };
    static struct { uint32_t key; GLuint smp; } cache[512];
    static int n;
    uint32_t key = (uint32_t)(p[0] & 1) | (uint32_t)(p[1] & 1) << 1 | (uint32_t)(p[2] & 3) << 2 | (uint32_t)(p[3] & 3) << 4 |
                   (uint32_t)(p[4] & 3) << 6 | (uint32_t)p[5] << 8 | 0x80000000u;
    int i;
    for (i = 0; i < n; i++) if (cache[i].key == key) return cache[i].smp;
    if (n == 512) n = 0;   /* no deberia pasar */
    {
        GLuint sp;
        GLint minf;
        int mip = p[4] & 3, lin = p[1] & 1;
        p_glGenSamplers(1, &sp);
        if (mip == 0) minf = lin ? GL_LINEAR : GL_NEAREST;
        else if (mip == 1) minf = lin ? GL_LINEAR_MIPMAP_NEAREST : GL_NEAREST_MIPMAP_NEAREST;
        else minf = lin ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_LINEAR;
        p_glSamplerParameteri(sp, GL_TEXTURE_MAG_FILTER, p[0] ? GL_LINEAR : GL_NEAREST);
        p_glSamplerParameteri(sp, GL_TEXTURE_MIN_FILTER, minf);
        p_glSamplerParameteri(sp, GL_TEXTURE_WRAP_S, wraps[p[2] & 3]);
        p_glSamplerParameteri(sp, GL_TEXTURE_WRAP_T, wraps[p[3] & 3]);
        if (mip) p_glSamplerParameterf(sp, GL_TEXTURE_LOD_BIAS, (float)(int8_t)p[5] / 32.0f);
        if (mip && lin && s_aniso > 1.0f) p_glSamplerParameterf(sp, 0x84FE /* GL_TEXTURE_MAX_ANISOTROPY */, s_aniso);
        cache[n].key = key; cache[n].smp = sp; n++;
        return sp;
    }
}

static void apply_key(const DrawKey* k) {
    const DrawKey* o = s_applied_valid ? &s_key_applied : NULL;
    int i;
    if (!o || memcmp(&o->u, &k->u, sizeof k->u)) upload_uniforms(&k->u);
    for (i = 0; i < 8; i++) {
        if (!(k->used & (1u << i))) continue;
        if (!o || !(o->used & (1u << i)) || o->tex[i] != k->tex[i]) {
            p_glActiveTexture(GL_TEXTURE0 + (GLenum)i);
            p_glBindTexture(GL_TEXTURE_2D_ARRAY, k->tex[i]);
            gl_stat_tex_binds++;
        }
        /* el filtro y la envoltura van en un "sampler" por unidad: son de cada dibujo, no de la textura */
        if (!o || !(o->used & (1u << i)) || memcmp(o->tpar[i], k->tpar[i], 8))
            p_glBindSampler((GLuint)i, sampler_for(k->tpar[i]));
    }
    p_glActiveTexture(GL_TEXTURE0);
    if (!o || o->z != k->z) {
        uint32_t z = k->z;
        if (z & 1) {
            p_glEnable(GL_DEPTH_TEST);
            p_glDepthFunc(GL_NEVER + ((z >> 1) & 7));
            p_glDepthMask((z & 0x10) ? GL_TRUE : GL_FALSE);
        } else {
            p_glDisable(GL_DEPTH_TEST);
        }
    }
    if (!o || o->cm != k->cm || o->cm1 != k->cm1) {
        uint32_t cm = k->cm, cm1 = k->cm1;
        static const GLenum src_f[8] = { GL_ZERO, GL_ONE, GL_DST_COLOR, GL_ONE_MINUS_DST_COLOR, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA };
        static const GLenum dst_f[8] = { GL_ZERO, GL_ONE, GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA };
        int blend = cm & 1, logic = (cm >> 1) & 1, sub = (cm >> 11) & 1;
        int const_alpha = (cm1 >> 8) & 1;
        GLenum cs = GL_ONE, cd = GL_ZERO, as, ad, eq = GL_FUNC_ADD;
        if (blend) {
            if (sub) { cs = GL_ONE; cd = GL_ONE; eq = GL_FUNC_REVERSE_SUBTRACT; }
            else { cs = src_f[(cm >> 8) & 7]; cd = dst_f[(cm >> 5) & 7]; }
            if (cm >> 31) {   /* sin alfa en el EFB: como Dolphin, el alfa de destino es 1 */
                static int avisado;
                if ((cs == GL_DST_ALPHA || cs == GL_ONE_MINUS_DST_ALPHA || cd == GL_DST_ALPHA || cd == GL_ONE_MINUS_DST_ALPHA) && !avisado++)
                    rt_log("[gl] mezcla con el alfa de destino en un EFB sin alfa (se toma 1)\n");
                if (cs == GL_DST_ALPHA) cs = GL_ONE; else if (cs == GL_ONE_MINUS_DST_ALPHA) cs = GL_ZERO;
                if (cd == GL_DST_ALPHA) cd = GL_ONE; else if (cd == GL_ONE_MINUS_DST_ALPHA) cd = GL_ZERO;
            }
        }
        as = cs; ad = cd;
        if (const_alpha) {
            as = GL_CONSTANT_ALPHA; ad = GL_ZERO;
            p_glBlendColor(0, 0, 0, (float)(cm1 & 255) / 255.0f);
        }
        if (blend || const_alpha) {
            p_glEnable(GL_BLEND);
            p_glBlendFuncSeparate(cs, cd, as, ad);
            p_glBlendEquationSeparate(eq, const_alpha ? GL_FUNC_ADD : eq);
        } else {
            p_glDisable(GL_BLEND);
        }
        if (!blend && logic) {
            p_glEnable(GL_COLOR_LOGIC_OP);
            p_glLogicOp(GL_CLEAR + ((cm >> 12) & 15));
        } else {
            p_glDisable(GL_COLOR_LOGIC_OP);
        }
        p_glColorMask((cm & 8) ? GL_TRUE : GL_FALSE, (cm & 8) ? GL_TRUE : GL_FALSE, (cm & 8) ? GL_TRUE : GL_FALSE, (cm & 0x10) ? GL_TRUE : GL_FALSE);
    }
    if (!o || memcmp(o->sc, k->sc, sizeof k->sc)) {
        p_glEnable(GL_SCISSOR_TEST);
        p_glScissor(k->sc[0] * S_, k->sc[1] * S_, k->sc[2] * S_, k->sc[3] * S_);
    }
    if (!o || o->cull != k->cull) {
        if (!k->cull || getenv("RL_SIN_CULL")) p_glDisable(GL_CULL_FACE);
        else {
            p_glEnable(GL_CULL_FACE);
            p_glCullFace(k->cull == 1 ? GL_FRONT : k->cull == 2 ? GL_BACK : GL_FRONT_AND_BACK);
        }
    }
    s_key_applied = *k;
    s_applied_valid = 1;
}

/* ---- dibujo ----------------------------------------------------------------------- */

static uint8_t col8(float c) { return (uint8_t)(c <= 0 ? 0 : c >= 255.0f ? 255 : (int)(c + 0.5f)); }

/* Agrega un vertice al lote y devuelve su indice. */
static uint32_t put_vtx(const Vtx* v) {
    size_t st = vtx_stride(s_vtex);
    uint8_t* o;
    float* f;
    int i;
    if (s_vbytes + st > s_vcap) {
        gl_vtx_esperar();
        s_vcap = s_vcap ? s_vcap * 2 : (1u << 20);
        s_vbuf = (uint8_t*)realloc(s_vbuf, s_vcap);
    }
    o = s_vbuf + s_vbytes;
    f = (float*)o;
    f[0] = v->x; f[1] = v->y; f[2] = v->z; f[3] = v->w;
    o[16] = col8(v->col[0][0]); o[17] = col8(v->col[0][1]); o[18] = col8(v->col[0][2]); o[19] = col8(v->col[0][3]);
    o[20] = col8(v->col[1][0]); o[21] = col8(v->col[1][1]); o[22] = col8(v->col[1][2]); o[23] = col8(v->col[1][3]);
    f = (float*)(o + 24);
    for (i = 0; i < s_vtex; i++) { f[3 * i] = v->tex[i][0]; f[3 * i + 1] = v->tex[i][1]; f[3 * i + 2] = v->tex[i][2]; }
    memcpy(o + 24 + 12 * s_vtex, s_key_lay, 8);
    s_vbytes += st;
    return (uint32_t)(s_vn++);
}

static void put_tri(uint32_t a, uint32_t b, uint32_t c) {
    if (s_in + 3 > s_icap) {
        s_icap = s_icap ? s_icap * 2 : (1u << 18);
        s_ibuf = (uint32_t*)realloc(s_ibuf, s_icap * 4);
    }
    s_ibuf[s_in++] = a; s_ibuf[s_in++] = b; s_ibuf[s_in++] = c;
}
/* para los dibujos: se reserva una vez lugar para todos los indices y despues se escriben
   sin comprobar (put_tri comprobaba en cada triangulo) */
static void reservar_ind(size_t n) {
    if (s_in + n > s_icap) {
        while (s_in + n > s_icap) s_icap = s_icap ? s_icap * 2 : (1u << 18);
        s_ibuf = (uint32_t*)realloc(s_ibuf, s_icap * 4);
    }
}
static inline void put_tri_nc(uint32_t a, uint32_t b, uint32_t c) {
    uint32_t* o = s_ibuf + s_in;
    o[0] = a; o[1] = b; o[2] = c;
    s_in += 3;
}

/* Lineas y puntos: rectangulos de ancho fijo en pixeles (se calculan en el espacio de recorte). */
/* Corrimiento de las coordenadas de textura a lo ancho de lineas y puntos (BP 0x22 bits
 * 16-18 y 19-21; cada coordenada lo activa con el bit 18 o 19 de su registro SU_SSIZE, BP
 * 0x30 + 2i). Asi el juego dibuja, por ejemplo, el cable del snowspeeder: una linea ancha
 * con una textura que va de un borde al otro. Como Dolphin. */
static const float k_lpt_off[8] = { 0.f, 0.0625f, 0.125f, 0.25f, 0.5f, 1.f, 1.f, 1.f };
static void tex_offset(Vtx* v, int point, float ds, float dt) {
    int i;
    float o = k_lpt_off[(bpr(0x22) >> (point ? 19 : 16)) & 7];
    if (o == 0.f) return;
    for (i = 0; i < 8; i++)
        if ((bpr(0x30 + 2 * i) >> (point ? 19 : 18)) & 1) {
            float q = v->tex[i][2] != 0.f ? v->tex[i][2] : 1.f;
            v->tex[i][0] += ds * o * q;
            v->tex[i][1] += dt * o * q;
        }
}

/* point: 0 linea, 1 punto (para el corrimiento de textura) */
static void put_quad_px2(const Vtx* a, const Vtx* b, float dx, float dy, int point) {
    float xs = xf_rf(0x1A), ys = xf_rf(0x1B);
    Vtx q[4];
    float ax = xs != 0 ? dx * a->w / xs : 0, ay = ys != 0 ? dy * a->w / ys : 0;
    float bx = xs != 0 ? dx * b->w / xs : 0, by = ys != 0 ? dy * b->w / ys : 0;
    q[0] = *a; q[1] = *a; q[2] = *b; q[3] = *b;
    q[0].x -= ax; q[0].y -= ay; q[1].x += ax; q[1].y += ay;
    q[2].x += bx; q[2].y += by; q[3].x -= bx; q[3].y -= by;
    if (point) {           /* q1 arriba izq, q2 arriba der, q0 abajo izq, q3 abajo der */
        tex_offset(&q[2], 1, 1, 0); tex_offset(&q[0], 1, 0, 1); tex_offset(&q[3], 1, 1, 1);
    } else if (dx != 0) {  /* linea mas alta que ancha: el lado de +x */
        tex_offset(&q[1], 0, 1, 0); tex_offset(&q[2], 0, 1, 0);
    } else {               /* linea mas ancha que alta: el lado de abajo */
        tex_offset(&q[0], 0, 1, 0); tex_offset(&q[3], 0, 1, 0);
    }
    {
        uint32_t i0 = put_vtx(&q[0]), i1 = put_vtx(&q[1]), i2 = put_vtx(&q[2]), i3 = put_vtx(&q[3]);
        put_tri(i0, i1, i2); put_tri(i0, i2, i3);
    }
}

/* Copia el estado de XF que usa VS_XF al buffer de estados y devuelve su numero
 * (si es igual al anterior, se reusa). */
static uint32_t xf_snapshot(void) {
    extern uint32_t gx_xf_gen;
    extern uint64_t gx_xf_dirty;
    static uint32_t gen_last;
    static int ws_last = -1;
    uint64_t dirty;
    int b;
    if (ws_last != gl_widescreen) { ws_last = gl_widescreen; gx_xf_dirty = ~0ull; gen_last = gx_xf_gen - 1; }
    if (s_snap_n > 0 && gx_xf_gen == gen_last) return s_snap_n - 1;
    if (s_snap_n == s_snap_max || s_pool_n + XF_BLOCKS > s_pool_max) {
        /* buffers llenos: dibujar lo pendiente y empezar de nuevo (hay que volver a subir todo) */
        flush_batch();
        p_glBindBuffer(GL_TEXTURE_BUFFER, s_xf_buf);
        p_glBufferData(GL_TEXTURE_BUFFER, (GLsizeiptr)s_pool_max * 64, NULL, GL_STREAM_DRAW);
        p_glBindBuffer(GL_TEXTURE_BUFFER, s_xft_buf);
        p_glBufferData(GL_TEXTURE_BUFFER, (GLsizeiptr)s_snap_max * XF_BLOCKS * 4, NULL, GL_STREAM_DRAW);
        s_snap_n = s_snap_up = 0;
        s_pool_n = s_pool_up = 0;
        gx_xf_dirty = ~0ull;
    }
    gl_stat_xf_snaps++;
    dirty = gx_xf_dirty;
    gx_xf_dirty = 0;
    for (b = 0; b < XF_BLOCKS; b++) {
        const uint32_t* src;
        uint32_t* d;
        int w = b * 16;
        if (!(dirty >> b & 1)) continue;
        if (w < 256) src = &gx.xf_mem[w];
        else if (w < 352) src = &gx.xf_mem[0x400 + w - 256];
        else if (w < 608) src = &gx.xf_mem[0x500 + w - 352];
        else if (w < 736) src = &gx.xf_mem[0x600 + w - 608];
        else src = &gx.xf_regs[w - 736];
        d = s_pool + (size_t)s_pool_n * 16;
        memcpy(d, src, 64);
        if (w == 736 + 0x20 && getenv("RL_TRAZA_PROY")) {   /* depuracion: proyecciones distintas */
            static uint32_t vistas[64][7]; static int nv;
            int k, j;
            for (k = 0; k < nv; k++) { for (j = 0; j < 7 && vistas[k][j] == d[j]; j++) {} if (j == 7) break; }
            if (k == nv && nv < 64) {
                memcpy(vistas[nv++], d, 28);
                rt_log("[proy] %s p0 %.4f p1 %.4f p2 %.4f p3 %.4f p4 %.4f p5 %.4f | vp %.1f %.1f %.1f %.1f\n", d[6] ? "orto " : "persp",
                       u32_f(d[0]), u32_f(d[1]), u32_f(d[2]), u32_f(d[3]), u32_f(d[4]), u32_f(d[5]),
                       u32_f(gx.xf_regs[0x1A]), u32_f(gx.xf_regs[0x1B]), u32_f(gx.xf_regs[0x1D]), u32_f(gx.xf_regs[0x1E]));
            }
        }
        if (w == 736 + 0x20 && d[0x26 - 0x20] == 0 && gl_ws_aplastar(u32_f(d[0]), u32_f(d[1]), u32_f(d[2]))) {
            d[0] = f_u32(u32_f(d[0]) * 0.75f);
            d[1] = f_u32(u32_f(d[1]) * 0.75f);
        }
        s_cur_blk[b] = s_pool_n++;
    }
    memcpy(s_snap + (size_t)s_snap_n * XF_BLOCKS, s_cur_blk, sizeof s_cur_blk);
    gen_last = gx_xf_gen;
    return s_snap_n++;
}

static int key_stats_on(void) { static int v = -1; if (v < 0) v = getenv("RL_KEY_STATS") != NULL; return v; }

/* el dibujo actual tiene zfreeze prendido (aunque no haya plano todavia) */
static int k_zf_now(void) { return (int)((gx.bp_regs[0x00] >> 19) & 1); }
/* La pendiente de Z del ultimo dibujo solo la usan los dibujos con zfreeze (BP 0x00 bit 19),
   que son pocos y no en todos los niveles; calcularla en cada dibujo era ~10% del hilo
   grafico. Se calcula mientras haya habido zfreeze en los ultimos 600 cuadros (el primer
   cuadro con zfreeze despues de mucho tiempo puede usar una pendiente vieja).
   RL_ZSLOPE_SIEMPRE=1: como antes. */
static uint32_t s_zf_cuadro = 0x80000000u;
static int zf_reciente(void) {
    static int siempre = -1;
    if (siempre < 0) siempre = getenv("RL_ZSLOPE_SIEMPRE") != NULL || (gl_headless && !getenv("RL_ZSLOPE_NO"));   /* las pruebas, exactas */
    return siempre || (uint32_t)(s_frame - s_zf_cuadro) < 600u;
}

/* proyeccion de GX (XF 0x20-0x26) de un punto en el espacio de la camara */
static void proj_clip(const float e[3], float o[4]) {
    float p0 = xf_rf(0x20), p1 = xf_rf(0x21), p2 = xf_rf(0x22), p3 = xf_rf(0x23), p4 = xf_rf(0x24), p5 = xf_rf(0x25);
    if (gx.xf_regs[0x26] == 0 && gl_ws_aplastar(p0, p1, p2)) { p0 *= 0.75f; p1 *= 0.75f; }
    if (gx.xf_regs[0x26] == 0) { o[0] = p0 * e[0] + p1 * e[2]; o[1] = p2 * e[1] + p3 * e[2]; o[2] = p4 * e[2] + p5; o[3] = -e[2]; }
    else { o[0] = p0 * e[0] + p1; o[1] = p2 * e[1] + p3; o[2] = p4 * e[2] + p5; o[3] = 1.0f; }
}

/* ---- lectura de vertices en otros hilos ------------------------------------------------
 * En las escenas pesadas (el nivel 6, el nivel 7: ~26 mil dibujos por cuadro) el hilo grafico
 * no llega, y casi la mitad de su trabajo es leer y convertir los vertices de cada dibujo
 * (vtx_fetch_n). Eso no depende de nada mas: se encola y lo hacen otros hilos mientras el
 * grafico sigue leyendo comandos. Antes de usar los vertices (mandar el lote a la placa,
 * agrandar el buffer, o cuando los datos del dibujo se van a mover) se espera a que esten.
 * RL_VTX_HILOS=N (0 = todo en el hilo grafico, como antes). */
typedef struct { const VtxSetup* S; const uint8_t* data; uint8_t* dst; volatile int* cnt; size_t st; int vsize, n, nraw, fifo; uint32_t snap; uint8_t lay[8];
                 const uint8_t* csrc; size_t clen; volatile int* cesp; } VJob;   /* csrc: trabajo de copia (al anillo de la placa) */
#define VJ_CAP 16384
#define VJ_SCAP 1024
static VJob s_vj[VJ_CAP];
static uint8_t* s_vj_setups;               /* copias de la parte de VtxSetup que usa vtx_fetch_n */
static uint64_t s_vj_pub, s_vj_sig, s_vj_hechos;   /* publicados / tomados / terminados */
static volatile int s_vj_fifo;
#define VJ_ARENA (8u << 20)
static uint8_t* s_vj_arena;                 /* copias de los vertices que venian en la cola de comandos */
static size_t s_vj_arena_n;              /* trabajos sin terminar que leen de la cola de comandos (s_fifo de gx.c) */
static int s_vj_hilos = -1, s_vj_dormidos;
static unsigned s_vj_ncopias;              /* copias desde la ultima espera */
static uint32_t s_vj_gen_copia = ~0u;
static const VtxSetup* s_vj_copia;
static pthread_mutex_t s_vj_m = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_vj_cv = PTHREAD_COND_INITIALIZER;
static int vj_hacer_uno(void) {
    uint64_t i = __atomic_load_n(&s_vj_sig, __ATOMIC_ACQUIRE);
    VJob* j;
    if (i >= __atomic_load_n(&s_vj_pub, __ATOMIC_ACQUIRE)) return 0;
    if (!__atomic_compare_exchange_n(&s_vj_sig, &i, i + 1, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return 1;
    j = &s_vj[i % VJ_CAP];
    if (j->csrc) {
        /* copia de un lote ya leido al anillo de la placa: primero tienen que terminar sus vertices
           (los trabajos de lectura se tomaron antes que este, asi que ya estan en marcha) */
        const uint8_t* src = j->csrc; uint8_t* dst = j->dst; size_t len = j->clen;
        volatile int* cnt = j->cnt; volatile int* esp = j->cesp;
        {   uint64_t t0 = 0; unsigned k = 0;
            while (__atomic_load_n(esp, __ATOMIC_ACQUIRE) > 0) {
#if defined(__x86_64__) || defined(__i386__)
                __builtin_ia32_pause();
#endif
                if ((++k & 4095) == 0) {   /* no deberia pasar: avisar y no quedarse para siempre */
                    uint64_t now = os_now_us();
                    if (!t0) t0 = now;
                    else if (now - t0 > 200000) { static int av; if (av++ < 5) rt_log("[vtx] copia esperando sus vertices hace 200 ms (faltan %d)\n", *esp); break; }
                }
            }
        }
        memcpy(dst, src, len);
        __atomic_sub_fetch(cnt, 1, __ATOMIC_ACQ_REL);
        __atomic_add_fetch(&s_vj_hechos, 1, __ATOMIC_ACQ_REL);
        return 1;
    }
    {
        volatile int* cnt = j->cnt;
        int fifo = j->fifo;
        vtx_fetch_n((VtxSetup*)j->S, j->data, j->vsize, j->dst, j->st, j->n, j->nraw, j->snap, j->lay);
        __atomic_sub_fetch(cnt, 1, __ATOMIC_ACQ_REL);
        if (fifo) __atomic_sub_fetch(&s_vj_fifo, 1, __ATOMIC_ACQ_REL);
    }
    __atomic_add_fetch(&s_vj_hechos, 1, __ATOMIC_ACQ_REL);
    return 1;
}
static void* vj_hilo(void* arg) {
    (void)arg;
    for (;;) {
        uint64_t t0 = 0;
        int k = 0;
        while (!vj_hacer_uno()) {
            if ((++k & 255) == 0) {   /* sin trabajo un rato: a dormir */
                uint64_t now = os_now_us();
                if (!t0) t0 = now;
                else if (now - t0 > 300) {
                    pthread_mutex_lock(&s_vj_m);
                    s_vj_dormidos++;
                    while (__atomic_load_n(&s_vj_sig, __ATOMIC_ACQUIRE) >= __atomic_load_n(&s_vj_pub, __ATOMIC_ACQUIRE))
                        pthread_cond_wait(&s_vj_cv, &s_vj_m);
                    s_vj_dormidos--;
                    pthread_mutex_unlock(&s_vj_m);
                    t0 = 0;
                }
            }
#if defined(__x86_64__) || defined(__i386__)
            __builtin_ia32_pause();
#endif
        }
    }
    return NULL;
}
static void vj_iniciar(void) {
    const char* e = getenv("RL_VTX_HILOS");
    int n = SDL_GetCPUCount() - 3, i;
    if (n > 3) n = 3;
    if (e) n = atoi(e);
    if (n < 0) n = 0;
    if (n > 8) n = 8;
    s_vj_hilos = 0;
    if (!n) return;
    s_vj_setups = (uint8_t*)malloc((size_t)VJ_SCAP * ((VTX_FETCH_BYTES + 63) & ~(size_t)63));
    for (i = 0; i < n; i++) {
        pthread_t t;
        if (pthread_create(&t, NULL, vj_hilo, NULL) == 0) s_vj_hilos++;
    }
    rt_log("[gl] lectura de vertices en %d hilos mas\n", s_vj_hilos);
}
/* esperar a que todos los vertices encolados esten escritos (ayudando mientras tanto) */
void gl_vtx_esperar(void) {
    if (s_vj_hilos <= 0) return;
    while (__atomic_load_n(&s_vj_hechos, __ATOMIC_ACQUIRE) < s_vj_pub) {
        if (!vj_hacer_uno()) {
#if defined(__x86_64__) || defined(__i386__)
            __builtin_ia32_pause();
#endif
        }
    }
    s_vj_ncopias = 0;
    s_vj_arena_n = 0;
    s_vj_gen_copia = ~0u;   /* la proxima copia puede volver a usar el lugar 0 */
}
/* encola la copia de len bytes de src a dst cuando *esp llegue a 0; *cnt cuenta las copias pendientes */
static void vj_encolar_copia(const uint8_t* src, uint8_t* dst, size_t len, volatile int* esp, volatile int* cnt) {
    VJob* j;
    while (s_vj_pub - __atomic_load_n(&s_vj_hechos, __ATOMIC_ACQUIRE) >= VJ_CAP) vj_hacer_uno();
    j = &s_vj[s_vj_pub % VJ_CAP];
    j->csrc = src; j->dst = dst; j->clen = len; j->cesp = esp; j->cnt = cnt;
    __atomic_add_fetch(cnt, 1, __ATOMIC_ACQ_REL);
    __atomic_store_n(&s_vj_pub, s_vj_pub + 1, __ATOMIC_RELEASE);
    if (s_vj_dormidos) { pthread_mutex_lock(&s_vj_m); pthread_cond_broadcast(&s_vj_cv); pthread_mutex_unlock(&s_vj_m); }
}
/* esperar solo los vertices de un lote (o los que leen de la cola de comandos) */
uint64_t gl_vj_esperas, gl_vj_trabajos, gl_vj_fifo_trab;   /* estadisticas (RL_VJ_STATS) */
static void vj_esperar_cnt(volatile int* c) {
    if (s_vj_hilos <= 0) return;
    if (__atomic_load_n(c, __ATOMIC_ACQUIRE) > 0) gl_vj_esperas++;
    while (__atomic_load_n(c, __ATOMIC_ACQUIRE) > 0) {
        if (!vj_hacer_uno()) {
#if defined(__x86_64__) || defined(__i386__)
            __builtin_ia32_pause();
#endif
        }
    }
}
void gl_vtx_esperar_fifo(void) { vj_esperar_cnt(&s_vj_fifo); }
/* para el volcado de cuando el juego se queda quieto */
void gl_vj_estado(void) {
    rt_log("[vtx] trabajos publicados %llu tomados %llu terminados %llu, lotes %d/%d copias %d/%d, cola %d, dormidos %d, diferido %d subido %d\n",
           (unsigned long long)s_vj_pub, (unsigned long long)s_vj_sig, (unsigned long long)s_vj_hechos, s_cnt[0], s_cnt[1], s_cntc[0], s_cntc[1],
           s_vj_fifo, s_vj_dormidos, s_dif.valido, s_dif.sub);
}
/* encola la lectura (o la hace ya si no hay hilos) */
static void vj_encolar(VtxSetup* S, const uint8_t* data, int vsize, size_t off, size_t st, int n, int nraw, uint32_t snap, const uint8_t lay[8]) {
    VJob* j;
    int fifo;
    if (s_vj_hilos < 0) vj_iniciar();
    if (s_vj_hilos <= 0) { vtx_fetch_n(S, data, vsize, s_vbuf + off, st, n, nraw, snap, lay); return; }
    /* las esperas (que reinician las copias de S y de los datos) van antes de copiar nada */
    if ((s_vj_gen_copia != vtx_fetch_gen || !s_vj_copia) && s_vj_ncopias >= VJ_SCAP) gl_vtx_esperar();
    fifo = gx_en_fifo(data);
    if (fifo) {
        /* los datos estan en la cola de comandos, que se mueve al seguir leyendo: se copian
           aparte (son pocos bytes) y asi no hay que esperar a este trabajo para seguir */
        size_t tam = (size_t)n * (size_t)vsize;
        if (!s_vj_arena) s_vj_arena = (uint8_t*)malloc(VJ_ARENA);
        if (tam <= VJ_ARENA / 4) {
            if (s_vj_arena_n + tam > VJ_ARENA) gl_vtx_esperar();   /* lleno: esperar a todos y volver a empezar */
            memcpy(s_vj_arena + s_vj_arena_n, data, tam);
            data = s_vj_arena + s_vj_arena_n;
            s_vj_arena_n += (tam + 15) & ~(size_t)15;
            fifo = 0;
        }
    }
    if (s_vj_gen_copia != vtx_fetch_gen || !s_vj_copia) {
        size_t tam = (VTX_FETCH_BYTES + 63) & ~(size_t)63;
        s_vj_copia = (const VtxSetup*)(s_vj_setups + tam * s_vj_ncopias++);
        memcpy((void*)s_vj_copia, S, VTX_FETCH_BYTES);
        s_vj_gen_copia = vtx_fetch_gen;
    }
    while (s_vj_pub - __atomic_load_n(&s_vj_hechos, __ATOMIC_ACQUIRE) >= VJ_CAP) vj_hacer_uno();
    j = &s_vj[s_vj_pub % VJ_CAP];
    j->S = s_vj_copia; j->data = data; j->vsize = vsize; j->dst = s_vbuf + off; j->st = st; j->n = n; j->nraw = nraw; j->snap = snap;
    j->cnt = s_cnt_lote; j->fifo = fifo; j->csrc = NULL;
    memcpy(j->lay, lay, 8);
    __atomic_add_fetch(s_cnt_lote, 1, __ATOMIC_ACQ_REL);
    if (fifo) { __atomic_add_fetch(&s_vj_fifo, 1, __ATOMIC_ACQ_REL); gl_vj_fifo_trab++; }
    gl_vj_trabajos++;
    __atomic_store_n(&s_vj_pub, s_vj_pub + 1, __ATOMIC_RELEASE);
    if (s_vj_dormidos) { pthread_mutex_lock(&s_vj_m); pthread_cond_broadcast(&s_vj_cv); pthread_mutex_unlock(&s_vj_m); }
}

void gl_render_draw(int prim, int vat, int count, const uint8_t* data, uint32_t vsize) {
    /* formato de vertices: solo se rehace si el juego escribio algo en CP o cambio de formato
       (lo comun son miles de dibujos seguidos con el mismo) */
    extern uint32_t gx_cp_gen;
    static GXVtxLayout L;
    static int l_vat = -1, l_nraw;
    static uint32_t l_gen;
    int cp_nuevo = 0;
    Vtx* v;
    int i;
    int use_xf = s_use_xf && prim <= 4;
    if (k_zf_now()) s_zf_cuadro = s_frame;
    if (vat != l_vat || gx_cp_gen != l_gen) {
        gx_vertex_layout(vat, &L);
        l_vat = vat; l_gen = gx_cp_gen; cp_nuevo = 1;
        for (i = 7; i >= 0 && !L.tex_mode[i]; i--) {}
        l_nraw = i + 1;
    }
    {   /* RL_PROBE_PX=x,y: anota cada dibujo que cambia el color o la Z de ese pixel del EFB */
        static int px = -2, py;
        static uint32_t lc, lz;
        if (px == -2) { const char* e = getenv("RL_PROBE_PX"); px = -1; if (e) sscanf(e, "%d,%d", &px, &py); }
        if (px >= 0) {
            uint8_t c[4]; uint32_t z = 0, cc;
            flush_batch();
            p_glBindFramebuffer(GL_FRAMEBUFFER, s_efb_fbo);
            p_glPixelStorei(GL_PACK_ALIGNMENT, 1);
            p_glReadPixels(px * S_, py * S_, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, c);
            { float zf = 0; p_glReadPixels(px * S_, py * S_, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &zf); z = (uint32_t)(zf * 16777215.0f + 0.5f); }
            cc = (uint32_t)c[0] << 16 | (uint32_t)c[1] << 8 | c[2];
            if (cc != lc || z != lz) { extern uint64_t gx_draw_index_dbg(void); rt_log("[sonda] antes del dibujo %llu (n %llu): color %06X z %06X\n", (unsigned long long)gx.stats.draws, (unsigned long long)gx_draw_index_dbg(), cc, z); }
            lc = cc; lz = z;
        }
    }
    {
        extern int gx_trace_draw;
        if (gx_trace_draw) {   /* RL_DRAW_TRACE: registros de BP y XF que deciden como se ve el dibujo */
            static const uint8_t regs[] = { 0x00, 0x28, 0x29, 0x40, 0x41, 0x42, 0x43, 0xF3, 0xF4, 0xF6, 0xF7, 0xF8, 0xF9,
                0x80, 0x84, 0x88, 0x94, 0x98, 0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xEE, 0xEF, 0xF0, 0xF1, 0xF2 };
            int r;
            rt_log("[traza] prim %d vat %d n %d xf %d\n[traza] BP:", prim, vat, count, use_xf);
            for (r = 0; r < (int)sizeof regs; r++) rt_log(" %02X=%06X", regs[r], gx.bp_regs[regs[r]] & 0xFFFFFF);
            rt_log("\n[traza] TEV:");
            for (r = 0; r < 2 * (int)(((gx.bp_regs[0] >> 10) & 15) + 1); r++) rt_log(" %02X=%06X", 0xC0 + r, gx.bp_regs[0xC0 + r] & 0xFFFFFF);
            rt_log("\n[traza] TEX:");
            for (r = 0; r < 8; r++) {
                int i0 = r < 4 ? 0x88 + r : 0xA8 + r - 4, i3 = r < 4 ? 0x94 + r : 0xB4 + r - 4, m0 = r < 4 ? 0x80 + r : 0xA0 + r - 4, tl = r < 4 ? 0x98 + r : 0xB8 + r - 4;
                rt_log(" %d:%06X@%06X m%06X t%06X", r, gx.bp_regs[i0] & 0xFFFFFF, (gx.bp_regs[i3] & 0xFFFFFF) << 5, gx.bp_regs[m0] & 0xFFFFFF, gx.bp_regs[tl] & 0xFFFFFF);
            }
            rt_log("\n[traza] BPALL:");
            for (r = 0; r < 256; r++) rt_log(" %06X", gx.bp_regs[r] & 0xFFFFFF);
            rt_log("\n[traza] TEVREG:");
            for (r = 0; r < 4; r++) rt_log(" %X/%X k%X/%X", gx.tev_reg[r][0], gx.tev_reg[r][1], gx.tev_konst[r][0], gx.tev_konst[r][1]);
            rt_log("\n[traza] TG:");
            for (r = 0; r < 8; r++) rt_log(" %08X/%08X", gx.xf_regs[0x40 + r], gx.xf_regs[0x50 + r]);
            rt_log("\n[traza] XF: amb %08X %08X mat %08X %08X chan %08X %08X %08X %08X nchan %u ntex %u\n",
                   gx.xf_regs[0x0A], gx.xf_regs[0x0B], gx.xf_regs[0x0C], gx.xf_regs[0x0D],
                   gx.xf_regs[0x0E], gx.xf_regs[0x0F], gx.xf_regs[0x10], gx.xf_regs[0x11], gx.xf_regs[0x09], gx.xf_regs[0x3F]);
            rt_log("[traza] viewport %g %g %g %g %g %g proj %g %g %g %g %g %g tipo %u matidx %08X\n", xf_rf(0x1A), xf_rf(0x1B), xf_rf(0x1C), xf_rf(0x1D), xf_rf(0x1E), xf_rf(0x1F),
                   xf_rf(0x20), xf_rf(0x21), xf_rf(0x22), xf_rf(0x23), xf_rf(0x24), xf_rf(0x25), gx.xf_regs[0x26], gx.xf_regs[0x18]);
            { extern uint32_t gx_m0_src, gx_m0_kind; rt_log("[traza] matriz0 cargada por %X desde %08X\n", gx_m0_kind, gx_m0_src); }
            {   int pm = gx.xf_regs[0x18] & 63, q; rt_log("[traza] matriz %d:", pm); for (q = 0; q < 12; q++) rt_log(" %g", u32_f(gx.xf_mem[pm * 4 + q])); rt_log("\n"); }
            rt_log("[traza] scissor BP20 %06X BP21 %06X BP59 %06X\n", gx.bp_regs[0x20] & 0xFFFFFF, gx.bp_regs[0x21] & 0xFFFFFF, gx.bp_regs[0x59] & 0xFFFFFF);
            for (r = 0; r < 8; r++) {   /* luces: color, atenuacion, posicion, direccion */
                int q; rt_log("[traza] luz %d: color %08X", r, gx.xf_mem[0x600 + r * 16 + 3]);
                for (q = 4; q < 16; q++) rt_log(" %g", u32_f(gx.xf_mem[0x600 + r * 16 + q]));
                rt_log("\n");
            }
        }
    }
    {
        static DrawKey k;
        /* armar el estado cuesta: si no cambio ningun registro de BP ni de XF desde el
           dibujo anterior (lo comun: miles de dibujos seguidos con el mismo material), se reusa */
        extern uint32_t gx_bp_gen, gx_xfreg_gen;
        static uint32_t kb_bp, kb_xr, kb_frame, kb_zs; static int kb_tri = -1, kb_off = -1;
        if (kb_off < 0) kb_off = getenv("RL_NO_KEY_CACHE") != NULL;
        if (s_zfreeze_off < 0) s_zfreeze_off = getenv("RL_NO_ZFREEZE") != NULL;
        if (kb_off || kb_tri != (prim <= 4) || kb_bp != gx_bp_gen || kb_xr != gx_xfreg_gen || kb_frame != s_frame ||
            (k.zf && kb_zs != s_zslope_gen)) {
            build_key(&k, prim <= 4);
            kb_tri = prim <= 4; kb_bp = gx_bp_gen; kb_xr = gx_xfreg_gen; kb_frame = s_frame; kb_zs = s_zslope_gen;
            k.xf = 0; k.nraw = 0; s_k_igual = 0;
        }
        {   /* RL_TRACE_NOZ=1: el dibujo de RL_DRAW_TRACE sin prueba de Z (depuracion) */
            extern int gx_trace_draw;
            static int noz = -1;
            if (noz < 0) noz = getenv("RL_TRACE_NOZ") != NULL;
            if (noz) { if (gx_trace_draw) { k.z &= ~1u; kb_frame = ~0u; s_k_igual = 0; } }
        }
        {
            int32_t nxf = use_xf ? 1 : 0, nnr = use_xf ? l_nraw : 0;
            if (k.xf != nxf || k.nraw != nnr) { k.xf = nxf; k.nraw = nnr; s_k_igual = 0; }
        }
        static int nobatch = -1;
        if (nobatch < 0) nobatch = getenv("RL_NO_BATCH") != NULL;
        if (!nobatch && s_vn && (s_k_igual || memcmp(&k, &s_key_pending, sizeof k) == 0)) { gl_stat_batched++; s_k_igual = 1; }
        else {
         if (s_vn && key_stats_on()) {   /* RL_KEY_STATS=1: por que se corta el lote (depuracion) */
            static unsigned c_u, c_tex, c_st, c_prog, c_other, n;
            const DrawKey* o = &s_key_pending;
            int same_tex = !memcmp(o->tex, k.tex, sizeof k.tex) && !memcmp(o->tpar, k.tpar, sizeof k.tpar) && o->used == k.used;
            int same_st = o->z == k.z && o->cm == k.cm && o->cm1 == k.cm1 && !memcmp(o->sc, k.sc, sizeof k.sc) && o->cull == k.cull && o->ntex == k.ntex && o->xf == k.xf && o->nraw == k.nraw && o->zf == k.zf;
            int same_prog = o->u.misc[0] == k.u.misc[0] && !memcmp(o->u.stage, k.u.stage, sizeof k.u.stage);
            if (same_tex && same_st && same_prog) c_u++;
            else if (!same_tex && same_st && same_prog) {
                c_tex++;
                if (c_tex < 40) {
                    int t; char b[256]; int bl = 0;
                    for (t = 0; t < 8; t++) if (o->tex[t] != k.tex[t] || memcmp(o->tpar[t], k.tpar[t], 8)) {
                        int i0 = t < 4 ? 0x88 + t : 0xA8 + t - 4, m0 = t < 4 ? 0x80 + t : 0xA0 + t - 4;
                        bl += snprintf(b + bl, sizeof b - (size_t)bl, " u%d:%u->%u img%06X m%06X", t, o->tex[t], k.tex[t], gx.bp_regs[i0] & 0xFFFFFF, gx.bp_regs[m0] & 0xFFFFFF);
                    }
                    rt_log("[lotes] textura: ns %d usadas %X%s\n", k.u.misc[0], k.used, b);
                }
            }
            else if (same_tex && !same_st && same_prog) c_st++;
            else if (same_tex && same_st && !same_prog) c_prog++;
            else c_other++;
            if (++n % 500 == 0) rt_log("[lotes] cortes: solo uniforms %u, solo texturas %u, solo estado %u, solo programa %u, varios %u\n", c_u, c_tex, c_st, c_prog, c_other);
         }
         {
            if (s_diferir < 0) { const char* e = getenv("RL_DIFERIR"); s_diferir = !(e && *e == '0'); }
            if (s_diferir) dif_cerrar();    /* el estado se pone al mandar el lote */
            else {
                flush_batch();
                if (!s_applied_valid || memcmp(&k, &s_key_applied, sizeof k)) apply_key(&k);
            }
            s_key_pending = k; s_k_igual = 1;
            s_vtex = k.ntex;
            s_vxf = k.xf; s_vraw = k.nraw; s_vzf = k.zf;
         }
        }
    }
    if (use_xf) {
        /* vertices sin transformar: los transforma VS_XF */
        static VtxSetup S;
        uint32_t snap = xf_snapshot();
        size_t st = xvtx_stride(s_vraw);
        uint32_t base;
        if (s_vbytes + st * (size_t)count > s_vcap) {
            gl_vtx_esperar();
            while (s_vbytes + st * (size_t)count > s_vcap) s_vcap = s_vcap ? s_vcap * 2 : (1u << 20);
            s_vbuf = (uint8_t*)realloc(s_vbuf, s_vcap);
        }
        if (cp_nuevo || !S.fetch_valid) vtx_setup_fetch(&L, &S);
        {   /* RL_VTX_VIEJO=1: vertice por vertice como antes (para comparar) */
            static int viejo = -1;
            if (viejo < 0) viejo = getenv("RL_VTX_VIEJO") != NULL;
            if (viejo)
                for (i = 0; i < count; i++) {
                    vtx_fetch(&S, data + (size_t)i * vsize, s_vbuf + s_vbytes + st * (size_t)i, s_vraw, snap);
                    memcpy(s_vbuf + s_vbytes + st * (size_t)i + XVTX_BASE + 8 * s_vraw, s_key_lay, 8);
                }
            else vj_encolar(&S, data, (int)vsize, s_vbytes, st, count, s_vraw, snap, (const uint8_t*)s_key_lay);
        }
        if (!k_zf_now() && count >= 3 && zf_reciente()) {
            /* plano para zfreeze: transformar en la CPU los 3 ultimos vertices */
            float c3[3][4];
            int j;
            uint8_t t3[3 * (XVTX_BASE + 8 * 8 + 8)];   /* los 3 ultimos leidos aca (los demas los lee otro hilo) */
            vtx_fetch_n(&S, data + (size_t)(count - 3) * vsize, (int)vsize, t3, st, 3, s_vraw, snap, (const uint8_t*)s_key_lay);
            for (j = 0; j < 3; j++) {
                const uint8_t* vv = t3 + st * (size_t)j;
                const float* pp = (const float*)vv;
                uint32_t pmi = ((const uint16_t*)(vv + 32))[1] & 63;
                const uint32_t* m = &gx.xf_mem[pmi * 4];
                float e[3];
                int r;
                for (r = 0; r < 3; r++) e[r] = u32_f(m[4 * r]) * pp[0] + u32_f(m[4 * r + 1]) * pp[1] + u32_f(m[4 * r + 2]) * pp[2] + u32_f(m[4 * r + 3]);
                proj_clip(e, c3[j]);
            }
            zslope_update(c3, &s_key_pending.u); s_k_igual = 0;
        }
        base = (uint32_t)s_vn;
        s_vn += (size_t)count; s_vbytes += st * (size_t)count;
        reservar_ind((size_t)count * 3);
        switch (prim) {
        case 0: case 1:
            for (i = 0; i + 3 < count; i += 4) { put_tri_nc(base + i, base + i + 1, base + i + 2); put_tri_nc(base + i, base + i + 2, base + i + 3); }
            break;
        case 2:
            for (i = 0; i + 2 < count; i += 3) put_tri_nc(base + i, base + i + 1, base + i + 2);
            break;
        case 3:
            for (i = 0; i + 2 < count; i++) {
                if (i & 1) put_tri_nc(base + i + 1, base + i, base + i + 2);
                else put_tri_nc(base + i, base + i + 1, base + i + 2);
            }
            break;
        case 4:
            for (i = 1; i + 1 < count; i++) put_tri_nc(base, base + i, base + i + 1);
            break;
        }
        if (s_vn > 200000 || s_in > 600000) flush_batch();
        return;
    }
    v = (Vtx*)malloc(sizeof(Vtx) * (size_t)count);
    {
        static VtxSetup S;
        static int check = -1;
        if (check < 0) check = getenv("RL_VTX_CHECK") != NULL;
        vtx_setup(&L, &S);
        {
            static int stats = -1;
            if (stats < 0) stats = getenv("RL_VTX_STATS") != NULL;
            if (stats) {
                char key[256];
                int t;
                int n = snprintf(key, sizeof key, "pos m%d f%d e%d | nrm m%d f%d | col m%d/%d f%d/%d | luz %02X ch %d%d%d%d | tex %d dual %d:",
                    L.pos_mode, L.pos_fmt, L.pos_elems, L.nrm_mode, L.nrm_fmt, L.col_mode[0], L.col_mode[1], L.col_fmt[0], L.col_fmt[1],
                    S.any_light, S.chan[0][0].light, S.chan[0][1].light, S.chan[1][0].light, S.chan[1][1].light, S.ntex, S.dual);
                for (t = 0; t < S.ntex && n < 240; t++) n += snprintf(key + n, sizeof key - n, " %u/%u/%u", S.tg_type[t], S.tg_src[t], S.tg_proj[t]);
                rt_log("[vtxst] %d %s\n", count, key);
            }
        }
        for (i = 0; i < count; i++) vtx_decode(&S, data + (size_t)i * vsize, &v[i]);
        if (check) {
            static int bad;
            for (i = 0; i < count; i++) {
                Vtx r;
                const float* a = (const float*)&v[i];
                const float* b = (const float*)&r;
                size_t k, nf = sizeof(Vtx) / sizeof(float);
                decode_vertex(&L, data + (size_t)i * vsize, &r);
                for (k = 0; k < nf; k++) {
                    if (k >= 4 && k < 7) continue;     /* sx, sy, sz no los calcula ninguno de los dos aca */
                    if (k >= 15 && k - 15 >= (size_t)(3 * (gx.xf_regs[0x3F] & 15))) continue;
                    if (fabsf(a[k] - b[k]) > 1e-3f * (1.0f + fabsf(b[k])) && bad++ < 20)
                        rt_log("[vtx] diferencia campo %u: %g vs %g (prim %d)\n", (unsigned)k, a[k], b[k], prim);
                }
            }
        }
    }
    {
        extern int gx_trace_draw;
        if (gx_trace_draw) {
            rt_log("[traza] xf18 %08X xf19 %08X prim %d vat %d n %d vsize %u pnmtx_off %d pos_mode %d fmt %d elems %d shift %d matidx %08X proj %08X\n", gx.xf_regs[0x18], gx.xf_regs[0x19],
                   prim, vat, count, vsize, L.pnmtx, L.pos_mode, L.pos_fmt, L.pos_elems, L.pos_shift, gx.cp_matidx_a, gx.xf_regs[0x26]);
            rt_log("[traza] proy %f %f %f %f %f %f  vp %f %f %f %f %f %f\n", xf_rf(0x20), xf_rf(0x21), xf_rf(0x22), xf_rf(0x23), xf_rf(0x24), xf_rf(0x25),
                   xf_rf(0x1A), xf_rf(0x1B), xf_rf(0x1C), xf_rf(0x1D), xf_rf(0x1E), xf_rf(0x1F));
            { extern uint32_t gx_m0_src, gx_m0_kind; int k;
              rt_log("[traza] matriz0 cargada por %X desde %08X base12 %08X stride %u:", gx_m0_kind, gx_m0_src, gx.cp_array_base[12], gx.cp_array_stride[12]);
              for (k = 0; k < 12; k++) rt_log(" %g", u32_f(gx.xf_mem[k]));
              rt_log("\n"); }
            for (i = 0; i < count && (i < 32 || getenv("RL_TRACE_ALLV")); i++)
                rt_log("[traza] v%d: %f %f %f %f  tex0 %f %f %f tex1 %f %f %f col %f %f %f %f  raw %02X %02X %02X %02X\n", i, v[i].x, v[i].y, v[i].z, v[i].w,
                       v[i].tex[0][0], v[i].tex[0][1], v[i].tex[0][2], v[i].tex[1][0], v[i].tex[1][1], v[i].tex[1][2],
                       v[i].col[0][0], v[i].col[0][1], v[i].col[0][2], v[i].col[0][3],
                       data[i * vsize], vsize > 1 ? data[i * vsize + 1] : 0, vsize > 2 ? data[i * vsize + 2] : 0, vsize > 3 ? data[i * vsize + 3] : 0);
        }
    }
    {   /* RL_DRAW_SUMMARY=C0: resumen (caja en pantalla, vertices detras de la camara) de los dibujos con ese TEV C0 */
        static long want = -2;
        if (want == -2) { const char* e = getenv("RL_DRAW_SUMMARY"); want = e ? strtol(e, NULL, 16) : -1; }
        if (want >= 0 && (want == 0xFFFFFFF || (long)(gx.bp_regs[0xC0] & 0xFFFFFF) == want)) {
            extern uint64_t gx_draw_index_dbg(void);
            float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
            int behind = 0;
            for (i = 0; i < count; i++) {
                if (v[i].w <= 0) { behind++; continue; }
                {
                    float sx = v[i].x / v[i].w * xf_rf(0x1A) + xf_rf(0x1D) - 342, sy = v[i].y / v[i].w * xf_rf(0x1B) + xf_rf(0x1E) - 342;
                    if (sx < x0) x0 = sx; if (sx > x1) x1 = sx; if (sy < y0) y0 = sy; if (sy > y1) y1 = sy;
                }
            }
            rt_log("[resumen] dibujo %llu prim %d n %d detras %d caja %.0f,%.0f - %.0f,%.0f z %06X cull %d\n", (unsigned long long)gx_draw_index_dbg(), prim, count, behind, x0, y0, x1, y1,
                   gx.bp_regs[0x40] & 0xFFFFFF, (int)((gx.bp_regs[0] >> 14) & 3));
        }
    }
    if (prim <= 4 && count >= 3 && !k_zf_now() && zf_reciente()) {
        float c3[3][4];
        int j;
        for (j = 0; j < 3; j++) { c3[j][0] = v[count - 3 + j].x; c3[j][1] = v[count - 3 + j].y; c3[j][2] = v[count - 3 + j].z; c3[j][3] = v[count - 3 + j].w; }
        zslope_update(c3, &s_key_pending.u); s_k_igual = 0;
    }
    {
        /* los vertices se agregan una vez y los triangulos se arman con indices */
        uint32_t base = 0;
        int need_verts = prim <= 4;
        if (need_verts) {
            base = (uint32_t)s_vn;
            for (i = 0; i < count; i++) put_vtx(&v[i]);
        }
        reservar_ind((size_t)count * 3);
        switch (prim) {
        case 0: case 1:
            for (i = 0; i + 3 < count; i += 4) { put_tri_nc(base + i, base + i + 1, base + i + 2); put_tri_nc(base + i, base + i + 2, base + i + 3); }
            break;
        case 2:
            for (i = 0; i + 2 < count; i += 3) put_tri_nc(base + i, base + i + 1, base + i + 2);
            break;
        case 3:
            for (i = 0; i + 2 < count; i++) {
                if (i & 1) put_tri_nc(base + i + 1, base + i, base + i + 2);
                else put_tri_nc(base + i, base + i + 1, base + i + 2);
            }
            break;
        case 4:
            for (i = 1; i + 1 < count; i++) put_tri_nc(base, base + i, base + i + 1);
            break;
        case 5: case 6: {
            float h = (float)(bpr(0x22) & 0xFF) / 12.0f;
            int step = prim == 5 ? 2 : 1;
            if (h < 0.5f) h = 0.5f;
            for (i = 0; i + 1 < count; i += step) {
                const Vtx* a = &v[i];
                const Vtx* b = &v[i + 1];
                float ddx, ddy;
                if (a->w <= 0 || b->w <= 0) continue;
                ddx = fabsf(b->x / b->w * xf_rf(0x1A) - a->x / a->w * xf_rf(0x1A));
                ddy = fabsf(b->y / b->w * xf_rf(0x1B) - a->y / a->w * xf_rf(0x1B));
                if (ddx > ddy) put_quad_px2(a, b, 0, h, 0);
                else put_quad_px2(a, b, h, 0, 0);
            }
            break;
        }
        case 7: {
            float h = (float)((bpr(0x22) >> 8) & 0xFF) / 12.0f;
            if (h < 0.5f) h = 0.5f;
            for (i = 0; i < count; i++) {
                Vtx l = v[i], r = v[i];
                float xs = xf_rf(0x1A);
                if (v[i].w <= 0 || xs == 0) continue;
                l.x -= h * l.w / xs; r.x += h * r.w / xs;
                put_quad_px2(&l, &r, 0, h, 1);
            }
            break;
        }
        default:
            break;
        }
    }
    free(v);
    if (s_vn > 200000 || s_in > 600000) flush_batch();
}

/* ---- copias del EFB -------------------------------------------------------------- */

typedef struct { uint32_t base; GLuint tex, fbo; uint32_t last; int sucia; uint32_t lineas; } Xfb;
#define MAX_XFB 8
static Xfb s_xfb[MAX_XFB];
#define XFB_LINES 576
/* Lineas que se consideran parte de la misma XFB (NTSC: hasta 480; las dos XFB del juego estan a 500 lineas). */
#define XFB_RANGE_LINES 480

/* nlines: cuantas lineas se van a escribir/leer desde dst. Dos XFB pueden estar pegadas en
 * la memoria (con imagenes de 304 lineas el juego pone una justo despues de la otra): una
 * direccion solo cae dentro de otra XFB si entra entera en su textura. */
static Xfb* xfb_for(uint32_t dst, uint32_t stride, uint32_t* line, int create, uint32_t nlines) {
    int i, free_i = -1, oldest = 0;
    for (i = 0; i < MAX_XFB; i++)          /* primero, una que empiece justo ahi */
        if (s_xfb[i].tex && s_xfb[i].base == dst) { *line = 0; return &s_xfb[i]; }
    for (i = 0; i < MAX_XFB; i++) {
        if (!s_xfb[i].tex) { if (free_i < 0) free_i = i; continue; }
        if (dst > s_xfb[i].base && dst < s_xfb[i].base + stride * XFB_RANGE_LINES) {
            uint32_t l = stride ? (dst - s_xfb[i].base) / stride : 0;
            if (l + nlines <= XFB_LINES) { *line = l; return &s_xfb[i]; }
        }
        if (s_xfb[i].last < s_xfb[oldest].last) oldest = i;
    }
    if (!create) return NULL;
    if (free_i < 0) free_i = oldest;
    if (!s_xfb[free_i].tex) {
        s_xfb[free_i].tex = new_tex(EFB_W * S_, XFB_LINES * S_, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        s_xfb[free_i].fbo = new_fbo(s_xfb[free_i].tex, 0);
        p_glClearColor(0, 0, 0, 1);
        p_glDisable(GL_SCISSOR_TEST);
        p_glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        p_glClear(GL_COLOR_BUFFER_BIT);
    }
    s_xfb[free_i].base = dst;
    s_xfb[free_i].lineas = 0; s_xfb[free_i].sucia = 0;
    s_xfb_dbg[free_i] = dst;
    *line = 0;
    return &s_xfb[free_i];
}

static void draw_quad(void) {
    p_glBindVertexArray(s_quad_vao);
    p_glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

/* Textura que empieza en una XFB (ver texture_for): se convierte la imagen de la XFB al
 * formato pedido con el mismo shader de las copias del EFB y queda en la cache como una copia. */
static GLuint xfb_como_textura(uint32_t addr, int w, int h, int fmt) {
    int i;
    uint32_t a = addr & 0x01FFFFFFu;
    for (i = 0; i < MAX_XFB; i++) {
        Xfb* x = &s_xfb[i];
        TexEntry* e;
        if (!x->tex || (x->base & 0x01FFFFFFu) != a || !x->lineas) continue;
        if (w > EFB_W || h > XFB_LINES || w < 64 || h < 64) return 0;
        e = find_efb_copy(addr, (uint32_t)w, (uint32_t)h);
        if (!e) {
            e = (TexEntry*)calloc(1, sizeof *e);
            e->addr = addr; e->w = (uint32_t)w; e->h = (uint32_t)h; e->from_efb = 1;
            e->tex = new_tex_arr1(w * S_, h * S_, NULL);
            e->next = *bucket(addr); *bucket(addr) = e;
            x->sucia = 1;
        }
        e->last_use = s_frame;
        if (x->sucia) {
            int tf = fmt == 6 ? 6 : fmt == 5 ? 5 : 4;   /* RGBA8 / RGB5A3 / RGB565 (lo demas, como RGB565) */
            if (!e->fbo) e->fbo = new_fbo_layer(e->tex);
            flush_batch();
            gl_state_reset();
            p_glBindFramebuffer(GL_FRAMEBUFFER, e->fbo);
            plain_state();
            p_glViewport(0, 0, w * S_, h * S_);
            p_glUseProgram(s_conv_prog);
            p_glUniform4f(s_conv_loc_src, 0, 0, (float)w, (float)h);
            p_glUniform4i(s_conv_loc_mode, tf, 0, 0, 0);
            p_glUniform4i(s_conv_loc_mode2, 0, 0, 0, 0);
            p_glActiveTexture(GL_TEXTURE0);
            p_glBindTexture(GL_TEXTURE_2D, x->tex);
            draw_quad();
            gl_state_reset();
            s_applied_valid = 0; s_cur_prog = 0;
            x->sucia = 0;
            {   static int n; if (n++ < 10) rt_log("[gl] la XFB %08X se usa como textura %dx%d fmt %d (transicion)\n", x->base, w, h, fmt); }
        }
        return e->tex;
    }
    return 0;
}

static void plain_state(void) {
    p_glDisable(GL_DEPTH_TEST);
    p_glDisable(GL_BLEND);
    p_glDisable(GL_CULL_FACE);
    p_glDisable(GL_SCISSOR_TEST);
    p_glDisable(GL_COLOR_LOGIC_OP);
    p_glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
}

uint64_t gx_replay_v0d;
void gl_render_copy(uint32_t cmd, int x0, int y0, int w, int h) {
    gl_state_reset();
    uint32_t dst = gx.bp_regs[0x4B] << 5;
    uint32_t stride = (gx.bp_regs[0x4D] & 0x3FF) << 5;
    if (cmd & (1u << 14)) {                        /* a la XFB */
        uint32_t line;
        Xfb* x = xfb_for(dst, stride, &line, 1, (uint32_t)h);
        x->last = s_frame;
        x->sucia = 1;
        if (line + (uint32_t)h > x->lineas) x->lineas = line + (uint32_t)h;
        plain_state();
        efb_resolver(x0, y0, w, h, 0);
        p_glBindFramebuffer(GL_READ_FRAMEBUFFER, s_efb_res_fbo);
        p_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, x->fbo);
        p_glBlitFramebuffer(x0 * S_, y0 * S_, (x0 + w) * S_, (y0 + h) * S_, 0, (GLint)line * S_, w * S_, ((GLint)line + h) * S_, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        {   /* RL_DUMP_XFB_COPIAS=1 (pruebas): guarda cada copia a la XFB (build/xfbc_N.png) */
            static int on = -1, n;
            if (on < 0) on = getenv("RL_DUMP_XFB_COPIAS") != NULL;
            if (on && n < 300) {
                char p[96];
                snprintf(p, sizeof p, "build/xfbc_%03d_d%llu_%08X.png", n++, (unsigned long long)(gx.stats.draws - gx_replay_v0d), dst);
                read_png(p, x->fbo, 0, (int)line, EFB_W, h);
            }
        }
    } else {                                       /* a una textura */
        int fmt = (int)((cmd >> 3) & 0xF), tf = (fmt >> 1) | ((fmt & 1) << 3);
        int half = (cmd >> 9) & 1;
        int ow = half ? w / 2 : w, oh = half ? h / 2 : h;
        int zsrc = (gx.bp_regs[0x43] & 7) == 3;
        TexEntry* e;
        GLuint fbo;
        if (ow <= 0 || oh <= 0) goto clear;
        gx.stats.tex_copies++;
        {   /* RL_LOG_COPIES=1: registra las copias del EFB a textura (la primera vez de cada destino/tamano) */
            static int lg = -1;
            if (lg < 0) lg = getenv("RL_LOG_COPIES") != NULL;
            if (lg && (!find_efb_copy(dst, ow, oh) || getenv("RL_LOG_COPIES")[0] == '2'))
                { extern uint64_t gx_draw_index_dbg(void); rt_log("[copia] dibujo %llu ", (unsigned long long)gx_draw_index_dbg()); }
            if (lg && (!find_efb_copy(dst, ow, oh) || getenv("RL_LOG_COPIES")[0] == '2'))
                rt_log("[copia] cuadro %u: EFB %d,%d %dx%d -> %08X %dx%d fmt %d paso %u%s\n", s_frame, x0, y0, w, h, dst, ow, oh, tf, stride, zsrc ? " (Z)" : "");
        }
        e = find_efb_copy(dst, ow, oh);
        if (!e) {
            e = (TexEntry*)calloc(1, sizeof *e);
            e->addr = dst; e->w = (uint32_t)ow; e->h = (uint32_t)oh; e->from_efb = 1;
            e->tex = new_tex_arr1(ow * S_, oh * S_, NULL);
            e->next = *bucket(dst); *bucket(dst) = e;
        }
        e->last_use = s_frame;
        e->copia_tf = tf; e->copia_gen++;
        if (getenv("RL_DUMP_COPIES")) {   /* el EFB entero (color) en el momento de la copia */
            int video_write_png(const char* path, const uint8_t* rgb, int w, int h);
            static int ne;
            int W = EFB_W * S_, H = EFB_H * S_, q;
            uint8_t* rgba = (uint8_t*)malloc((size_t)W * H * 4);
            uint8_t* rgb = (uint8_t*)malloc((size_t)W * H * 3);
            char path[96];
            p_glBindFramebuffer(GL_FRAMEBUFFER, s_efb_fbo);
            p_glPixelStorei(GL_PACK_ALIGNMENT, 1);
            p_glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
            for (q = 0; q < W * H; q++) { int y = q / W, x = q % W; uint8_t* o = rgb + ((size_t)(H - 1 - y) * W + x) * 3; o[0] = rgba[q * 4]; o[1] = rgba[q * 4 + 1]; o[2] = rgba[q * 4 + 2]; }
            snprintf(path, sizeof path, "build/efb_%03d_%08X.png", ne++, dst);
            video_write_png(path, rgb, W, H);
            free(rgba); free(rgb);
        }
        if (getenv("RL_DUMP_COPIES")) { extern uint64_t gx_draw_index_dbg(void); rt_log("[copiae] dibujo %llu %08X e %p tex %u gen %u tf %d\n", (unsigned long long)gx_draw_index_dbg(), dst, (void*)e, e->tex, e->copia_gen, tf); }
        efb_resolver(x0, y0, w, h, 1);
        if (!e->fbo) e->fbo = new_fbo_layer(e->tex);
        fbo = e->fbo;
        p_glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        plain_state();
        p_glViewport(0, 0, ow * S_, oh * S_);
        p_glUseProgram(s_conv_prog);
        p_glUniform4f(s_conv_loc_src, (float)x0, (float)y0, (float)w, (float)h);
        p_glUniform4i(s_conv_loc_mode, tf, (int)((cmd >> 15) & 1), zsrc, half);
        p_glUniform4i(s_conv_loc_mode2, (gx.bp_regs[0x43] & 7) == 1, 0, 0, 0);
        p_glActiveTexture(GL_TEXTURE1);
        p_glBindTexture(GL_TEXTURE_2D, s_efb_depth);
        p_glActiveTexture(GL_TEXTURE0);
        p_glBindTexture(GL_TEXTURE_2D, s_efb_color);
        draw_quad();
        {   /* RL_DUMP_COPIES=1: guarda cada copia del EFB a textura (color y alfa) en build/ */
            static int dump = -1, n;
            if (dump < 0) dump = getenv("RL_DUMP_COPIES") != NULL;
            if (dump && n < 200) {
                int W = ow * S_, H = oh * S_, i;
                uint8_t* rgba = (uint8_t*)malloc((size_t)W * H * 4);
                uint8_t* rgb = (uint8_t*)malloc((size_t)W * H * 3 * 2);
                char path[128];
                p_glPixelStorei(GL_PACK_ALIGNMENT, 1);
                p_glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
                for (i = 0; i < W * H; i++) {
                    rgb[i * 3] = rgba[i * 4]; rgb[i * 3 + 1] = rgba[i * 4 + 1]; rgb[i * 3 + 2] = rgba[i * 4 + 2];
                    rgb[(W * H + i) * 3] = rgb[(W * H + i) * 3 + 1] = rgb[(W * H + i) * 3 + 2] = rgba[i * 4 + 3];
                }
                snprintf(path, sizeof path, "build/copia_%03d_d%llu_%08X_f%d_pe%X_cmd%X_z%X_zc%X.png", n++, (unsigned long long)(gx.stats.draws - gx_replay_v0d), dst, tf, gx.bp_regs[0x43] & 0xFFFF, cmd & 0xFFFFFF, gx.bp_regs[0x40] & 0xFF, gx.bp_regs[0x51] & 0xFFFFFF);
                video_write_png(path, rgb, W, H * 2);
                free(rgba); free(rgb);
            }
        }
        p_glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
clear:
    if (cmd & (1u << 11)) {                        /* limpiar el EFB */
        uint32_t ar = gx.bp_regs[0x4F], gb = gx.bp_regs[0x50];
        uint32_t cm = gx.bp_regs[0x41], z = gx.bp_regs[0x40];
        GLbitfield bits = 0;
        p_glBindFramebuffer(GL_FRAMEBUFFER, s_efb_fbo);
        p_glDisable(GL_COLOR_LOGIC_OP);
        p_glEnable(GL_SCISSOR_TEST);
        p_glScissor(x0 * S_, y0 * S_, w * S_, h * S_);
        p_glColorMask((cm & 8) ? GL_TRUE : GL_FALSE, (cm & 8) ? GL_TRUE : GL_FALSE, (cm & 8) ? GL_TRUE : GL_FALSE, (cm & 0x10) ? GL_TRUE : GL_FALSE);
        p_glClearColor((float)(ar & 255) / 255.0f, (float)((gb >> 8) & 255) / 255.0f, (float)(gb & 255) / 255.0f, (float)((ar >> 8) & 255) / 255.0f);
        if (cm & 0x18) bits |= GL_COLOR_BUFFER_BIT;
        if (z & 0x10) {
            p_glDepthMask(GL_TRUE);
            p_glClearDepth((double)(gx.bp_regs[0x51] & 0xFFFFFF) / 16777215.0);
            bits |= GL_DEPTH_BUFFER_BIT;
        }
        if (bits) p_glClear(bits);
    }
    p_glViewport(0, 0, EFB_W * S_, EFB_H * S_);
}

/* ---- presentacion ------------------------------------------------------------------ */

uint32_t vi_xfb_height(void);

/* ---- vigia: si no se muestra ninguna imagen por varios segundos, el registro dice en que
 * estaba el hilo grafico y como estan los hilos del juego (para los cuelgues que no se
 * pueden repetir aca) ---------------------------------------------------------------------- */
volatile const char* gl_etapa = "arrancando";
volatile uint64_t gl_ult_imagen_us;
#define s_ult_imagen_us gl_ult_imagen_us
static int vigia(void* x) {
    int avisado = 0;
    (void)x;
    for (;;) {
        uint64_t ahora = os_now_us(), u = s_ult_imagen_us;
        SDL_Delay(1000);
        if (!u) continue;
        if (ahora - u > 5000000u && !avisado) {
            avisado = 1;
            rt_log("[vigia] hace %.1f s que no se muestra ninguna imagen; hilo grafico: %s\n", (ahora - u) / 1e6, (const char*)gl_etapa);
            { void os_volcar_hilos(const char* por); os_volcar_hilos("vigia"); }
        } else if (ahora - u < 1000000u) avisado = 0;
    }
    return 0;
}

void gl_present(uint32_t xfb_addr) {
    uint32_t line = 0;
    gl_etapa = "mostrando la imagen";
    if (!s_ult_imagen_us && !gl_headless) SDL_DetachThread(SDL_CreateThread(vigia, "vigia", NULL));
    s_ult_imagen_us = os_now_us();
    {   /* RL_SLOW_PRESENT_MS=N: simula una placa/vsync lenta (pruebas) */
        static int slow = -1;
        if (slow < 0) slow = getenv("RL_SLOW_PRESENT_MS") ? atoi(getenv("RL_SLOW_PRESENT_MS")) : 0;
        if (slow) SDL_Delay((Uint32)slow);
    }
    gl_state_reset();
    if (s_npend) spec_poll();
    if (s_sh_on) sh_poll();
    if (gl_juego_cargando && s_ring) { gl_state_reset(); spec_prewarm_carga(); }
    if (gl_juego_cargando && s_caln) calentar_cola(60000);
    if (s_uso_n && (gl_juego_cargando || s_uso_n > 64 || (s_frame % 300) == 0)) uso_guardar();
    else if (s_pw_on && s_pw_jugando) spec_prewarm_step();
    Xfb* x = xfb_for(xfb_addr & 0x01FFFFFFu, 1280, &line, 0, vi_xfb_height());
    {   /* RL_DUMP_PRESENTES=A-B (pruebas): guarda lo que se muestra en cada retrazado A..B en build/pres */
        static int a = -2, b;
        if (a == -2) { const char* e = getenv("RL_DUMP_PRESENTES"); a = -1; if (e) sscanf(e, "%d-%d", &a, &b); }
        {   /* RL_DUMP_TRAS_CARGA=N (pruebas): las N imagenes que siguen a cada carga de nivel */
            extern volatile int gl_dump_tras_carga;
            static int n = -1, quedan;
            if (n < 0) n = getenv("RL_DUMP_TRAS_CARGA") ? atoi(getenv("RL_DUMP_TRAS_CARGA")) : 0;
            if (n && gl_dump_tras_carga) { gl_dump_tras_carga = 0; quedan = n; }
            if (quedan > 0) {
                char p[96];
                quedan--;
#ifdef _WIN32
                _mkdir("capturas");
#else
                mkdir("capturas", 0755);
#endif
                snprintf(p, sizeof p, "capturas/carga_%06u_%08X%s.png", s_frame, xfb_addr, x ? "" : "_nada");
                rt_log("[pres] %u: XFB %08X alto %u linea %u%s\n", s_frame, xfb_addr, vi_xfb_height(), line, x ? "" : " (sin imagen)");
                if (x) read_png(p, x->fbo, 0, (int)line, EFB_W, (int)vi_xfb_height());
                else rt_log("[pres] %u: XFB %08X sin imagen\n", s_frame, xfb_addr);
            }
        }
        if (a >= 0 && (int)s_frame >= a && (int)s_frame <= b) {
            char p[96];
            snprintf(p, sizeof p, "build/pres/p_%05u_%08X%s.png", s_frame, xfb_addr, x ? "" : "_nada");
            if (x) read_png(p, x->fbo, 0, (int)line, EFB_W, (int)vi_xfb_height());
            else rt_log("[pres] %u: XFB %08X sin imagen\n", s_frame, xfb_addr);
            { uint32_t video_game_frames(void); rt_log("[pres] %u: cuadro del juego %u XFB %08X alto %u\n", s_frame, video_game_frames(), xfb_addr, vi_xfb_height()); }
        }
    }
    int ww, wh, vw, vh, vx, vy;
    uint32_t h = vi_xfb_height();
    {   /* imagenes que tardaron mucho: que paso en ese tramo (diagnostico de tirones) */
        static uint64_t t_ant, tex_ant, dib_ant;
        extern uint64_t gl_stat_tex_uploads, gl_stat_draw_calls;
        uint64_t ahora = os_now_us();
        if (t_ant && ahora - t_ant > 100000 && s_lentos_avisos < 200) {
            s_lentos_avisos++;
            rt_log("[lento] imagen de %.0f ms (cuadro %u): %u programas usados por primera vez, %llu texturas subidas, %llu llamadas de dibujo\n",
                   (ahora - t_ant) / 1000.0, s_frame, s_prog_nuevos_cuadro, (unsigned long long)(gl_stat_tex_uploads - tex_ant), (unsigned long long)(gl_stat_draw_calls - dib_ant));
        }
        t_ant = ahora; tex_ant = gl_stat_tex_uploads; dib_ant = gl_stat_draw_calls; s_prog_nuevos_cuadro = 0;
    }
    {   /* el juego no cambio de imagen por mucho tiempo (la ventana sigue, pero la imagen queda quieta) */
        uint32_t video_game_frames(void);
        static uint32_t ult; static uint64_t t_ult; static unsigned avisos; static int volcado;
        uint32_t gf = video_game_frames();
        uint64_t ahora = os_now_us();
        if (gf != ult) {
            if (t_ult && ahora - t_ult > 250000 && avisos++ < 200)
                rt_log("[lento] el juego no cambio de imagen por %.0f ms (cuadro del juego %u)\n", (ahora - t_ult) / 1000.0, gf);
            ult = gf; t_ult = ahora; volcado = 0;
        } else if (t_ult && ahora - t_ult > 1000000 && !volcado && !gl_juego_cargando && avisos < 200) {
            void os_volcar_hilos(const char* por);
            volcado = 1;
            os_volcar_hilos("el juego lleva mas de 1 s sin cambiar de imagen");
        }
    }
    perfil_frame();
    s_frame++;
    poll_events();
    SDL_GL_GetDrawableSize(s_win, &ww, &wh);
    plain_state();
    p_glBindFramebuffer(GL_FRAMEBUFFER, 0);
    p_glViewport(0, 0, ww, wh);
    p_glClearColor(0, 0, 0, 1);
    p_glClear(GL_COLOR_BUFFER_BIT);
    /* 4:3 (o 16:9 con --panoramico) con bandas negras */
    {
        int an = gl_widescreen ? 16 : 4, al = gl_widescreen ? 9 : 3;
        vw = ww; vh = ww * al / an;
        if (vh > wh) { vh = wh; vw = wh * an / al; }
    }
    vx = (ww - vw) / 2; vy = (wh - vh) / 2;
    if (x) {
        float y0 = (float)line / XFB_LINES, y1 = (float)(line + h) / XFB_LINES;
        /* el televisor muestra 480 lineas (NTSC); si la imagen tiene menos (este juego usa
           450), queda centrada con bordes negros como en la consola */
        int full = h > 480 ? 576 : 480;
        int ih = (int)((long)vh * (long)h / full);
        p_glViewport(vx, vy + (vh - ih) / 2, vw, ih);
        p_glUseProgram(s_blit_prog);
        p_glUniform4f(s_blit_loc_rect, 0, y1, 1, y0);   /* invertir: la fila 0 de la XFB va arriba */
        p_glActiveTexture(GL_TEXTURE0);
        p_glBindTexture(GL_TEXTURE_2D, x->tex);
        p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filtro_salida ? GL_NEAREST : GL_LINEAR);
        p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        draw_quad();
    }
    { static int n, cuando = -2; if (cuando == -2) { const char* e = getenv("RL_FOTO_CUADRO"); cuando = e ? atoi(e) : -1; }
      if (++n == cuando) s_foto_pedida = 1; }   /* para probar F12 sin teclado */
    if (s_foto_pedida) { s_foto_pedida = 0; foto_tomar(ww, wh); }   /* antes del menu y el contador */
    { void menu_dibujar(int ww, int wh); menu_dibujar(ww, wh); }   /* contador de FPS, aviso de shaders y menu (F1) */
    {   /* tiempo de la placa por cuadro (consultas de tiempo de OpenGL, sin esperar) */
        extern uint64_t gl_stat_gpu_ns, gl_stat_gpu_frames, gl_stat_swap_us;
        static GLuint q[8]; static int qn, qhead, qtail, active;
        if (!qn) { p_glGenQueries(8, q); qn = 8; }
        if (active) { p_glEndQuery(GL_TIME_ELAPSED); qtail++; active = 0; }
        while (qhead < qtail) {
            GLint ok = 0; GLuint64 ns = 0;
            p_glGetQueryObjectiv(q[qhead & 7], GL_QUERY_RESULT_AVAILABLE, &ok);
            if (!ok) break;
            p_glGetQueryObjectui64v(q[qhead & 7], GL_QUERY_RESULT, &ns);
            gl_stat_gpu_ns += ns; gl_stat_gpu_frames++;
            qhead++;
        }
        {
            /* minimizada: no se muestra (Windows haria esperar y frenaria al juego).
               (Se probo saltear imagenes cuando mostrar tardaba: se activaba de mas en escenas
               pesadas y se veia a 30 aunque el titulo dijera 60. No volver a hacerlo asi.) */
            uint64_t t0 = os_now_us(), dt;
            if (!(SDL_GetWindowFlags(s_win) & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN))) {
                gl_etapa = "SwapWindow (el driver mostrando la imagen)";
                SDL_GL_SwapWindow(s_win);
                gl_etapa = "despues de mostrar";
                dt = os_now_us() - t0;
                gl_stat_swap_us += dt;
                { extern uint64_t gl_stat_swap_max_us; if (dt > gl_stat_swap_max_us) gl_stat_swap_max_us = dt; }
            }
        }
        if (qtail - qhead < 8) { p_glBeginQuery(GL_TIME_ELAPSED, q[qtail & 7]); active = 1; }
    }
    {   /* cuadros por segundo en el titulo de la ventana (cada cuadro del juego cambia la XFB) */
        static uint32_t t0, n, lastxfb;
        uint32_t now = SDL_GetTicks();
        if (xfb_addr != lastxfb) { n++; lastxfb = xfb_addr; }
        if (!t0) t0 = now;
        if (now - t0 >= 1000) {
            char title[128];
            snprintf(title, sizeof title, "Escuadron2  -  %u FPS", (unsigned)(n * 1000 / (now - t0)));
            SDL_SetWindowTitle(s_win, title);
            t0 = now; n = 0;
        }
    }
    if ((s_frame & 63) == 0) evict_textures();
}

static int read_png(const char* path, GLuint fbo, int x, int y, int w, int h) {
    /* con resolucion interna mayor se lee todo y se reduce a 640 de ancho (promedio) */
    int sc = S_, W = w * sc, H = h * sc;
    uint8_t* rgba = (uint8_t*)malloc((size_t)W * H * 4);
    uint8_t* rgb = (uint8_t*)malloc((size_t)w * h * 3);
    int i, j;
    gl_state_reset();
    p_glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    p_glPixelStorei(GL_PACK_ALIGNMENT, 1);
    p_glReadPixels(x * sc, y * sc, W, H, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++) {
            int a, b, k, acc[3] = { 0, 0, 0 };
            for (b = 0; b < sc; b++) for (a = 0; a < sc; a++) {
                const uint8_t* q = rgba + ((size_t)(j * sc + b) * W + (i * sc + a)) * 4;
                for (k = 0; k < 3; k++) acc[k] += q[k];
            }
            for (k = 0; k < 3; k++) rgb[((size_t)j * w + i) * 3 + k] = (uint8_t)(acc[k] / (sc * sc));
        }
    video_write_png(path, rgb, w, h);
    free(rgba); free(rgb);
    return 1;
}

int gl_dump_xfb(const char* path, uint32_t xfb) {
    uint32_t line = 0;
    Xfb* x = xfb_for(xfb & 0x01FFFFFFu, 1280, &line, 0, vi_xfb_height());
    if (!x) return 1;
    if (getenv("RL_DUMP_ALL_XFB")) {
        int i;
        for (i = 0; i < MAX_XFB; i++) if (s_xfb[i].tex) {
            char p[600];
            snprintf(p, sizeof p, "%s.xfb%d_%08X.png", path, i, s_xfb[i].base);
            read_png(p, s_xfb[i].fbo, 0, 0, EFB_W, XFB_LINES);
        }
    }
    return read_png(path, x->fbo, 0, (int)line, EFB_W, (int)vi_xfb_height());
}

void gl_dump_efb(const char* path) { efb_resolver(0, 0, EFB_W, EFB_H, 0); read_png(path, s_efb_res_fbo, 0, 0, EFB_W, 480); }

/* ---- copias del EFB dentro de las capturas (F12 / --capturar) ----------------------
 * Las copias del EFB a textura viven solo en la placa: una captura sin ellas se ve distinta
 * al reproducirla (sombras, efectos de pantalla). Se guardan al empezar el cuadro. */
uint8_t* gl_save_efb_copies(size_t* out_n) {
    size_t cap = 1 << 20, n = 8;
    uint8_t* o = (uint8_t*)malloc(cap);
    uint32_t cnt = 0;
    int b;
    memcpy(o, "EFBC", 4);
    p_glPixelStorei(GL_PACK_ALIGNMENT, 1);
    for (b = 0; b < TEX_BUCKETS; b++) {
        TexEntry* e;
        for (e = s_texcache[b]; e; e = e->next) {
            uint32_t h[5];
            size_t px;
            if (!e->from_efb || !e->fbo) continue;
            h[0] = e->addr; h[1] = e->w; h[2] = e->h; h[3] = e->w * (uint32_t)S_; h[4] = e->h * (uint32_t)S_;
            px = (size_t)h[3] * h[4] * 4;
            while (n + sizeof h + px > cap) { cap *= 2; o = (uint8_t*)realloc(o, cap); }
            memcpy(o + n, h, sizeof h); n += sizeof h;
            p_glBindFramebuffer(GL_READ_FRAMEBUFFER, e->fbo);
            p_glReadPixels(0, 0, (GLsizei)h[3], (GLsizei)h[4], GL_RGBA, GL_UNSIGNED_BYTE, o + n);
            n += px;
            cnt++;
        }
    }
    p_glBindFramebuffer(GL_FRAMEBUFFER, 0);
    memcpy(o + 4, &cnt, 4);
    *out_n = n;
    return o;
}

void gl_load_efb_copies(const uint8_t* d, size_t n) {
    uint32_t cnt, i;
    size_t o = 8;
    if (n < 8 || memcmp(d, "EFBC", 4)) return;
    memcpy(&cnt, d + 4, 4);
    for (i = 0; i < cnt && o + 20 <= n; i++) {
        uint32_t h[5];
        TexEntry* e;
        size_t px;
        memcpy(h, d + o, sizeof h); o += sizeof h;
        px = (size_t)h[3] * h[4] * 4;
        if (o + px > n) break;
        e = (TexEntry*)calloc(1, sizeof *e);
        e->addr = h[0]; e->w = h[1]; e->h = h[2]; e->from_efb = 1;
        e->tex = new_tex_arr1((int)h[3], (int)h[4], d + o);
        e->fbo = new_fbo_layer(e->tex);
        if (getenv("RL_LOG_COPIES")) rt_log("[gl] captura: copia %08X %ux%u guardada a %ux%u\n", h[0], h[1], h[2], h[3], h[4]);
        e->next = *bucket(e->addr); *bucket(e->addr) = e;
        o += px;
    }
    p_glBindFramebuffer(GL_FRAMEBUFFER, 0);
    rt_log("[gl] captura: %u copias del EFB restauradas\n", cnt);
}

#else  /* sin OpenGL */
volatile const char* gl_etapa = "";
uint8_t* gl_save_efb_copies(size_t* out_n) { *out_n = 0; return NULL; }
void gl_load_efb_copies(const uint8_t* d, size_t n) { (void)d; (void)n; }
uint64_t gl_stat_swap_max_us;
uint64_t gl_stat_draw_calls, gl_stat_batched, gl_stat_tex_uploads, gl_stat_xf_snaps, gl_stat_uniform_uploads, gl_stat_shaders, gl_stat_gpu_ns, gl_stat_gpu_frames, gl_stat_swap_us, gl_stat_flush_us, gl_stat_prog_switch, gl_stat_tex_binds;

#include "runtime.h"
#include "render_int.h"
void gl_thread_init(void) { rt_fatal("este binario se compilo sin OpenGL (falta SDL2)\n"); }
void gl_idle(void) {}
void gl_present(uint32_t xfb_addr) { (void)xfb_addr; }
int gl_dump_xfb(const char* path, uint32_t xfb) { (void)path; (void)xfb; return 0; }
void gl_dump_efb(const char* path) { (void)path; }
void gl_vtx_esperar(void) {}
void gl_vtx_esperar_fifo(void) {}
void gl_vj_estado(void) {}
int gl_subir_en_hilos;
int gl_msaa, gl_aniso_nivel = -1, gl_filtro_salida, gl_preparar_shaders;
uint64_t gl_vj_esperas, gl_vj_trabajos, gl_vj_fifo_trab;
void gl_render_draw(int prim, int vat, int count, const uint8_t* data, uint32_t vsize) { (void)prim; (void)vat; (void)count; (void)data; (void)vsize; }
volatile int gl_juego_cargando;
volatile int gl_dump_tras_carga;
void gl_perfil_informe(uint32_t a) { (void)a; }
void gl_render_copy(uint32_t cmd, int x0, int y0, int w, int h) { (void)cmd; (void)x0; (void)y0; (void)w; (void)h; }

#endif
