#ifndef GX_H
#define GX_H
#include <stdint.h>

typedef struct {
    int size;
    int pnmtx, texmtx[8];
    int pos_mode, pos_elems, pos_fmt, pos_shift, pos_off;
    int nrm_mode, nrm_elems, nrm_fmt, nrm_index3, nrm_off;
    int col_mode[2], col_fmt[2], col_off[2];
    int tex_mode[8], tex_elems[8], tex_fmt[8], tex_shift[8], tex_off[8];
} GXVtxLayout;

typedef struct {
    /* procesador de comandos */
    uint32_t cp_vcd_lo, cp_vcd_hi;
    uint32_t cp_vat_a[8], cp_vat_b[8], cp_vat_c[8];
    uint32_t cp_array_base[16], cp_array_stride[16];
    uint32_t cp_matidx_a, cp_matidx_b;
    /* transformacion (XF): matrices y registros */
    uint32_t xf_mem[0x800];
    uint32_t xf_regs[0x100];
    /* rasterizacion y pixeles (BP) */
    uint32_t bp_regs[0x100];
    uint32_t bp_mask;
    uint32_t tev_reg[4][2], tev_konst[4][2];   /* (RA, BG) de los registros de color y constantes */
    struct { uint64_t draws, vertices, display_lists, copies, tex_copies; } stats;
} GXState;

extern GXState gx;
void gx_vertex_layout(int vat, GXVtxLayout* L);

/* Hilo grafico */
typedef void (*GXHostFn)(uint32_t arg);
void gx_host_command(GXHostFn fn, uint32_t arg);   /* ejecutar en orden en el hilo grafico */
void gx_sync(void);
void gx_fifo_flush(void);                          /* mandar al hilo grafico lo escrito en el tubo */                                /* esperar a que termine todo lo encolado */
extern int gx_sync_mode;
/* backend (render.c / render_gl.c), llamados desde el hilo grafico */
void gx_backend_thread_init(void);
void gx_backend_idle(void);
void render_present(uint32_t xfb_addr);                /* mostrar la XFB en la ventana */
int render_dump_xfb(const char* path, uint32_t xfb);   /* 0 = la XFB esta en la RAM (software) */
void render_dump_efb(const char* path);

#endif
