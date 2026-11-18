#ifndef MEMCARD_H
#define MEMCARD_H
#include <stdint.h>
void mc_init(const char* path);
int mc_present(void);
void mc_select(int cs);
void mc_imm(uint8_t* buf, int len, int rw);   /* rw: 0 leer, 1 escribir, 2 ambos */
void mc_dma(uint32_t ram_addr, uint32_t len, int rw);
int mc_take_interrupt(void);
extern int mc_trace;
#endif
