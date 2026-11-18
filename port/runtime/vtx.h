/* Decodificacion rapida de vertices (vtx.c). */
#ifndef VTX_H
#define VTX_H
#include <stddef.h>
#include "render_int.h"

typedef struct { int mat_vtx, light, amb_vtx, diff, atten; uint32_t mask; } ChanSetup;
typedef struct { float col[4], a[3], kk[3], pos[3], dir[3]; } LightSetup;

typedef struct {
    GXVtxLayout L;
    float pos_scale, tex_scale[8];
    uint32_t array_base[16], array_stride[16], array_span[16];
    const uint8_t* array_ptr[16];
    uint8_t zero[16];
    uint32_t def_pmi, def_tmi[8];
    int persp;
    float proj[6];
    float mat[2][4], amb[2][4];
    ChanSetup chan[2][2];
    uint32_t any_light;
    LightSetup lights[8];
    int ntex, dual;
    uint32_t tg_type[8], tg_src[8], tg_proj[8], tg_post[8];
    uint64_t pm_valid, tm_valid, ptm_valid;
    uint32_t nm_valid;
    /* desde aca no se limpia en cada dibujo */
    float pm[64][12], tm[64][12], ptm[64][12], nm[32][9];
    int fetch_valid;              /* vtx_setup_fetch: datos de abajo para no rehacerlo si no cambio nada */
    uint32_t fetch_matidx[2];
} VtxSetup;

void vtx_setup(const GXVtxLayout* L, VtxSetup* S);
void vtx_setup_fetch(const GXVtxLayout* L, VtxSetup* S);
#define XVTX_BASE 48
void vtx_fetch(VtxSetup* S, const uint8_t* v, uint8_t* out, int nraw, uint32_t snap);
extern volatile uint32_t vtx_fetch_gen;
/* vtx_fetch_n solo lee los campos de VtxSetup hasta 'persp' (se pueden copiar solo esos) */
#define VTX_FETCH_BYTES offsetof(VtxSetup, persp)
void vtx_fetch_n(VtxSetup* S, const uint8_t* data, int vsize, uint8_t* out, size_t st, int n, int nraw, uint32_t snap, const uint8_t lay[8]);
void vtx_decode(VtxSetup* S, const uint8_t* v, Vtx* o);

#endif
