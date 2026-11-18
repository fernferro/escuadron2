/* DSP nativo: interfaz entre los programas del DSP traducidos a C y el resto del port. */
#ifndef DSP_NATIVO_H
#define DSP_NATIVO_H
#include "dsp_core.h"

/* como termina una corrida de dsp_ejecutar_* */
enum {
    DSP_FIN_PRESUPUESTO = 0,   /* se acabo el presupuesto de instrucciones (g.pc = donde seguir) */
    DSP_FIN_HALT = 1,          /* instruccion HALT */
    DSP_FIN_ESPERA = 2,        /* el programa espera un mensaje (bucle de espera) */
    DSP_FIN_DESCONOCIDO = 3,   /* g.pc no esta en el programa traducido */
    DSP_FIN_CODIGO = 4,        /* cambio el programa en la memoria de instrucciones */
};

typedef int (*DspEjecutar)(int presupuesto);
typedef struct {
    const char* nombre;
    const u16* imagen;     /* palabras del programa (para reconocerlo en la memoria del DSP) */
    int largo;
    DspEjecutar ejecutar;
    DspEjecutar ejecutar_paso;   /* igual pero vuelve despues de cada instruccion (para verificar) */
} DspPrograma;

extern const DspPrograma g_dsp_programas[];
extern const DspPrograma g_dsp_programa_rom;
extern volatile int g_dsp_codigo_cambiado;
int dsp_es_fin_de_bucle(u16 a);
extern int g_dsp_quedan;    /* presupuesto que sobro en la ultima corrida */

#endif
