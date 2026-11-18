/* Partes del rasterizador que comparten el camino por software (render.c) y OpenGL (render_gl.c). */
#ifndef RENDER_INT_H
#define RENDER_INT_H
#include <stdint.h>
#include "gx.h"

#define EFB_W 640
#define EFB_H 528

typedef struct {
    float x, y, z, w;        /* espacio de recorte */
    float sx, sy, sz;        /* pantalla */
    float col[2][4];         /* colores de los canales (0..255) */
    float tex[8][3];         /* coordenadas de textura (s, t, q) */
} Vtx;

typedef struct { int w, h, fmt, wrap_s, wrap_t, mag_linear; const uint8_t* data; uint32_t tlut; int tlut_fmt; } TexInfo;
void texture_decode_rgba(const TexInfo* t, uint8_t* out);

extern uint8_t s_tmem[1 << 20];
extern int render_gl;

void decode_vertex(const GXVtxLayout* L, const uint8_t* v, Vtx* o);
void tex_info(int map, TexInfo* t);
void texel(const TexInfo* t, int x, int y, uint8_t o[4]);

/* render_gl.c */
void gl_thread_init(void);
void gl_idle(void);
void gl_present(uint32_t xfb_addr);
int gl_dump_xfb(const char* path, uint32_t xfb);
void gl_dump_efb(const char* path);
void gl_render_draw(int prim, int vat, int count, const uint8_t* data, uint32_t vsize);
void gl_render_copy(uint32_t cmd, int x0, int y0, int w, int h);
int video_write_png(const char* path, const uint8_t* rgb, int w, int h);

#endif
