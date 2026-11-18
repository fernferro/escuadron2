/*
 * Entradas: por ahora un control en el puerto 1, en reposo, con un guion
 * opcional de botones por numero de cuadro (para probar menus sin teclado).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "input.h"

typedef struct { unsigned frame, len; uint16_t buttons; uint8_t sx, sy; } ScriptStep;
static ScriptStep s_script[256];
static int s_nscript;
static unsigned s_frame;

void input_set_script(const char* script) {
    /* formato: "120:START,300:A" = START en el cuadro 120, A en el 300 (6 cuadros cada uno).
       "400:S128/40*90" = palanca en x=128 y=40 (128 = centro, 0..255) durante 90 cuadros;
       "*N" sirve tambien para botones. */
    const char* p = script;
    while (p && *p && s_nscript < 256) {
        unsigned f = (unsigned)strtoul(p, (char**)&p, 10);
        uint16_t b = 0;
        unsigned len = 6, sx = 0, sy = 0;
        if (*p == ':') p++;
        while (*p && *p != ',') {
            if (!strncmp(p, "START", 5)) { b |= PAD_SI_START; p += 5; }
            else if (*p == 'A') { b |= PAD_SI_A; p++; }
            else if (*p == 'B') { b |= PAD_SI_B; p++; }
            else if (*p == 'X') { b |= PAD_SI_X; p++; }
            else if (*p == 'Y') { b |= PAD_SI_Y; p++; }
            else if (*p == 'U') { b |= PAD_SI_UP; p++; }
            else if (*p == 'D') { b |= PAD_SI_DOWN; p++; }
            else if (*p == 'L') { b |= PAD_SI_LEFT; p++; }
            else if (*p == 'R') { b |= PAD_SI_RIGHT; p++; }
            else if (*p == 'S') { sx = (unsigned)strtoul(p + 1, (char**)&p, 10); if (*p == '/') sy = (unsigned)strtoul(p + 1, (char**)&p, 10); if (!sx) sx = 1; if (!sy) sy = 1; }
            else if (*p == '*') { len = (unsigned)strtoul(p + 1, (char**)&p, 10); }
            else p++;
        }
        s_script[s_nscript].frame = f;
        s_script[s_nscript].len = len;
        s_script[s_nscript].buttons = b;
        s_script[s_nscript].sx = (uint8_t)sx; s_script[s_nscript].sy = (uint8_t)sy;
        s_nscript++;
        if (*p == ',') p++;
    }
}

void input_frame(void) { s_frame++; }

static PadState s_host;
static volatile int s_have_host;
void input_set_host(const PadState* p) { s_host = *p; s_have_host = 1; }

int input_get_pad(int port, PadState* out) {
    int i;
    if (port != 0) return 0;
    memset(out, 0, sizeof *out);
    out->buttons = PAD_SI_ORIGIN;
    out->stick_x = out->stick_y = out->c_x = out->c_y = 0x80;
    if (s_have_host) {
        PadState h = s_host;
        out->buttons |= h.buttons;
        out->stick_x = h.stick_x; out->stick_y = h.stick_y; out->c_x = h.c_x; out->c_y = h.c_y;
        out->trig_l = h.trig_l; out->trig_r = h.trig_r;
    }
    for (i = 0; i < s_nscript; i++)
        if (s_frame >= s_script[i].frame && s_frame < s_script[i].frame + s_script[i].len)
        {
            out->buttons |= s_script[i].buttons;
            if (s_script[i].sx) { out->stick_x = s_script[i].sx; out->stick_y = s_script[i].sy; }
        }
    return 1;
}

/* vibracion pedida por el juego para el control 1 (hw.c) y si el usuario la quiere */
volatile int g_pad_rumble;
int g_rumble_enabled = 1;
