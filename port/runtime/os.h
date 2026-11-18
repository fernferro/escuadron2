#ifndef OS_H
#define OS_H

#include "ppc.h"

/* Memoria reservada por el runtime al tope de la RAM (fuera del arena del juego). */
#define OS_ARENA_HI          0x817E0000u
#define OS_BI2_ADDR          0x817E0000u   /* copia de bi2.bin (8 KB), apuntada por 0x800000F4 */
#define OS_IRQ_STACK_TOP     0x817FC000u   /* pila de los manejadores de interrupcion */
#define OS_IRQ_CONTEXT       0x817FCC00u   /* OSContext falso para los manejadores */
#define OS_REPORT_BUF        0x817FD000u
#define OS_REPORT_BUF_SIZE   0x1000u
#define OS_FST_ADDR          0x817FE800u   /* tabla de archivos del disco (FST) */
#define OS_SPRINTF           0x80051498u

/* Interrupciones del SO (__OSInterrupt). */
enum {
    IRQ_DSP_AI = 5, IRQ_DSP_ARAM = 6, IRQ_DSP_DSP = 7, IRQ_AI = 8,
    IRQ_EXI0_EXI = 9, IRQ_EXI0_TC = 10, IRQ_EXI0_EXT = 11,
    IRQ_EXI1_EXI = 12, IRQ_EXI1_TC = 13, IRQ_EXI1_EXT = 14,
    IRQ_EXI2_EXI = 15, IRQ_EXI2_TC = 16,
    IRQ_CP = 17, IRQ_PE_TOKEN = 18, IRQ_PE_FINISH = 19, IRQ_SI = 20, IRQ_DI = 21,
    IRQ_RSW = 22, IRQ_PI_ERROR = 23, IRQ_VI = 24
};

void os_raise_interrupt(int irq);
void os_clear_interrupt(int irq);
int os_interrupt_pending(int irq);
uint64_t os_now_us(void);
void os_init_host(PPCContext* main_ctx);
PPCContext* os_main_ctx(void);
extern int os_trace_threads;

/* hw.c */
void hw_init(void);
void hw_update(void);            /* genera los eventos que vencieron */
uint64_t hw_next_event_us(void); /* cuando ocurre el proximo evento */

/* disc.c */
int disc_open(const char* sys_dir, const char* files_dir);
void disc_read(uint32_t offset, uint32_t len, uint8_t* dst);

/* gx.c */
void gx_fifo_write(uint32_t value, int size);
void gx_init(void);

/* video.c */
void video_retrace(void);
void video_game_frame(void);   /* el juego cambio la XFB que se muestra (un cuadro del juego) */

#endif
