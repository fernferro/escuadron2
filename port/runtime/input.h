#ifndef INPUT_H
#define INPUT_H
#include <stdint.h>

/* Estado de un control de GameCube, en el formato del bus SI. */
typedef struct {
    uint16_t buttons;   /* bits del protocolo SI (A = 0x0100, START = 0x1000, ...) */
    uint8_t stick_x, stick_y, c_x, c_y, trig_l, trig_r;
} PadState;

#define PAD_SI_LEFT  0x0001
#define PAD_SI_RIGHT 0x0002
#define PAD_SI_DOWN  0x0004
#define PAD_SI_UP    0x0008
#define PAD_SI_Z     0x0010
#define PAD_SI_R     0x0020
#define PAD_SI_L     0x0040
#define PAD_SI_A     0x0100
#define PAD_SI_B     0x0200
#define PAD_SI_X     0x0400
#define PAD_SI_Y     0x0800
#define PAD_SI_START 0x1000
#define PAD_SI_ORIGIN 0x0080  /* "use origin": siempre encendido en un control real */

/* Devuelve 0 si no hay control en ese puerto. */
int input_get_pad(int port, PadState* out);
/* Guion de entradas para pruebas sin teclado: "cuadro:botones,..." */
void input_set_script(const char* script);
void input_frame(void);
/* Estado del teclado / control de la PC (lo actualiza la ventana). */
void input_set_host(const PadState* p);

#endif
