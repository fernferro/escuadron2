/*
 * DSP falso: reproduce el protocolo de arranque de tareas del DSP para que la
 * inicializacion del audio avance.
 *   1. Al salir de reset, la ROM del DSP anuncia 0x8071FEED.
 *   2. La CPU manda la tarea: 0x80F3A001 dir, 0x80F3C002 largo, 0x80F3A002 destino,
 *      0x80F3B002 dram, 0x80F3D001 vector de inicio.
 *   3. La tarea arranca y avisa "lista" con 0xDCD10000.
 * (El audio real del juego, MusyX, se emulara mas adelante.)
 */
#include "runtime.h"

int dsp_trace;
int dsp_boot_pending;
static uint32_t s_prev;

void dsp_hle_reset(void) { dsp_boot_pending = 1; }

void dsp_hle_mail(uint32_t mail, uint32_t* reply, int* have_reply) {
    static int count;
    if (dsp_trace && count++ < 400) rt_log("[dsp] CPU -> DSP 0x%08X\n", mail);
    *have_reply = 0;
    if (s_prev == 0x80F3D001u) {        /* llego el vector: la tarea arranca */
        *reply = 0xDCD10000u;
        *have_reply = 1;
    } else if ((s_prev >> 16) == 0xBABEu) {
        /* MusyX: 0xBABE0000|largo y despues la direccion de la lista de comandos.
           El microcodigo procesa la lista y avisa "reanudado" (DSP_RESUME), que
           llama al callback de la tarea y marca salDspIsDone. Sin esto el
           sintetizador nunca avanza y los streams de voz nunca terminan.
           (Todavia no mezclamos audio: la salida queda en silencio.) */
        *reply = 0xDCD10001u;
        *have_reply = 1;
        mail = 0;
    }
    s_prev = mail;
}
