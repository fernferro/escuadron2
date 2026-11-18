#ifndef RUNTIME_H
#define RUNTIME_H

#include "ppc.h"

void rt_log(const char* fmt, ...);
void gx_fifo_flush(void);   /* gx.c */
enum { CPU_JUEGO = 0, CPU_GRAFICO = 1, CPU_DSP = 2 };
void cpu_register_thread(int cat);          /* cputime.c */
uint64_t cpu_category_us(int cat);
void rt_fatal(const char* fmt, ...);
void mem_init(void);

/* Registros de hardware (hw.c). */
uint32_t hw_read(uint32_t addr, int size);
void hw_write(uint32_t addr, uint32_t value, int size);

/* Nombre de la funcion que contiene una direccion (para mensajes). */
const char* rt_func_name(uint32_t addr, uint32_t* start);

#endif
