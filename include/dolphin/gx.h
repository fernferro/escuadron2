#ifndef DOLPHIN_GX_H
#define DOLPHIN_GX_H

/*
 * Subconjunto minimo de la API grafica GX del SDK de GameCube, solo con lo
 * que usa el codigo ya pasado a C. Para el port a PC estas son las funciones
 * que hay que reimplementar sobre OpenGL/Vulkan.
 */
#include "types.h"

typedef f32 Mtx44[4][4];

typedef union {
    u8 u8;
    u16 u16;
    u32 u32;
    s8 s8;
    s16 s16;
    s32 s32;
    f32 f32;
} PPCWGPipe;

/* Escrituras directas a la FIFO de la GPU (0xCC008000) */
#define GXWGFifo (*(volatile PPCWGPipe*)0xCC008000)

static inline void GXPosition2f32(f32 x, f32 y) {
    GXWGFifo.f32 = x;
    GXWGFifo.f32 = y;
}

static inline void GXTexCoord2u8(u8 s, u8 t) {
    GXWGFifo.u8 = s;
    GXWGFifo.u8 = t;
}

static inline void GXEnd(void) {}

void C_MTXOrtho(Mtx44 m, f32 t, f32 b, f32 l, f32 r, f32 n, f32 f);
void GXSetProjection(Mtx44 m, s32 type);
void GXSetViewport(f32 left, f32 top, f32 wd, f32 ht, f32 nearz, f32 farz);
void GXSetScissor(u32 left, u32 top, u32 wd, u32 ht);
void GXSetNumChans(u8 n);
void GXLoadTexObj(void* obj, s32 id);
void GXSetNumTexGens(u8 n);
void GXSetTexCoordGen2(s32 dst, s32 func, s32 src, u32 mtx, u8 normalize, u32 postMtx);
void GXSetNumTevStages(u8 n);
void GXSetTevOrder(s32 stage, s32 coord, s32 map, s32 color);
void GXSetTevColorIn(s32 stage, s32 a, s32 b, s32 c, s32 d);
void GXSetTevColorOp(s32 stage, s32 op, s32 bias, s32 scale, u8 clamp, s32 out);
void GXSetTevAlphaIn(s32 stage, s32 a, s32 b, s32 c, s32 d);
void GXSetTevAlphaOp(s32 stage, s32 op, s32 bias, s32 scale, u8 clamp, s32 out);
void GXSetAlphaCompare(s32 comp0, u8 ref0, s32 op, s32 comp1, u8 ref1);
void GXSetBlendMode(s32 type, s32 src, s32 dst, s32 op);
void GXSetZMode(u8 enable, s32 func, u8 update);
void GXSetZCompLoc(u8 beforeTex);
void GXSetCullMode(s32 mode);
void GXSetCurrentMtx(u32 id);
void GXClearVtxDesc(void);
void GXSetVtxDesc(s32 attr, s32 type);
void GXSetVtxAttrFmt(s32 fmt, s32 attr, s32 cnt, s32 type, u8 frac);
void GXSetClipMode(s32 mode);
void GXBegin(s32 type, s32 fmt, u16 nverts);

#endif
