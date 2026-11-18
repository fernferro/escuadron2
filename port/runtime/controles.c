/*
 * Controles configurables: teclado y joystick -> control de GameCube en el puerto 1.
 *
 * Se leen de controles.ini (carpeta actual o al lado del programa; el launcher lo escribe).
 * Sin el archivo quedan los controles de siempre. Formato:
 *
 *   [teclado]            nombres de tecla de SDL ("Space", "Left Shift", "Return", "W", "Up", "Keypad 8"...)
 *   A = Space, J         hasta 4 teclas por boton, separadas por coma
 *   STICK_ARRIBA = W
 *   [joystick]           nombres de SDL: a b x y back start leftshoulder rightshoulder leftstick
 *   A = a                rightstick dpup dpdown dpleft dpright, y los ejes lefttrigger righttrigger
 *   L = lefttrigger      (un eje en un boton cuenta como apretado pasado el punto del gatillo)
 *   STICK = izquierda    que palanca del joystick hace de palanca principal / palanca C
 *   C = derecha          (izquierda, derecha o ninguna)
 *   [ajustes]
 *   zona_muerta_stick = 10    en %
 *   zona_muerta_c = 10
 *   gatillo = 80              en %: cuanto hay que apretar L/R para que cuente como boton
 *   vibracion = 100           en %
 *
 * Botones del GameCube: A B X Y Z L R START, CRUZ_ARRIBA/ABAJO/IZQUIERDA/DERECHA,
 * STICK_ARRIBA/ABAJO/IZQUIERDA/DERECHA, C_ARRIBA/ABAJO/IZQUIERDA/DERECHA.
 */
#ifdef RL_GL
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "runtime.h"
#include "input.h"

enum { B_A, B_B, B_X, B_Y, B_Z, B_L, B_R, B_START, B_CU, B_CD, B_CL, B_CR,
       B_SU, B_SD, B_SL, B_SR, B_KU, B_KD, B_KL, B_KR, NB };
static const char* k_nombres[NB] = { "A", "B", "X", "Y", "Z", "L", "R", "START",
    "CRUZ_ARRIBA", "CRUZ_ABAJO", "CRUZ_IZQUIERDA", "CRUZ_DERECHA",
    "STICK_ARRIBA", "STICK_ABAJO", "STICK_IZQUIERDA", "STICK_DERECHA",
    "C_ARRIBA", "C_ABAJO", "C_IZQUIERDA", "C_DERECHA" };
static const uint16_t k_bits[NB] = { PAD_SI_A, PAD_SI_B, PAD_SI_X, PAD_SI_Y, PAD_SI_Z, PAD_SI_L, PAD_SI_R, PAD_SI_START,
    PAD_SI_UP, PAD_SI_DOWN, PAD_SI_LEFT, PAD_SI_RIGHT, 0, 0, 0, 0, 0, 0, 0, 0 };

#define MAXT 4
typedef struct { int n; int sc[MAXT]; } Teclas;
typedef struct { int n; int boton[MAXT]; int eje[MAXT]; } Joy;   /* boton o eje (-1 si no) */

static Teclas s_tec[NB];
static Joy s_joy[NB];
static int s_stick = 0, s_cstick = 1;          /* 0 izquierda, 1 derecha, -1 ninguna */
static int s_dz_stick = 10, s_dz_c = 10, s_gatillo = 80;
int controles_vibracion = 100;

static void def_tec(int b, const char* a, const char* c) {
    s_tec[b].n = 0;
    if (a) s_tec[b].sc[s_tec[b].n++] = SDL_GetScancodeFromName(a);
    if (c) s_tec[b].sc[s_tec[b].n++] = SDL_GetScancodeFromName(c);
}
static void def_joy(int b, int boton, int eje) { s_joy[b].n = 1; s_joy[b].boton[0] = boton; s_joy[b].eje[0] = eje; }

static void por_defecto(void) {
    int i;
    memset(s_tec, 0, sizeof s_tec); memset(s_joy, 0, sizeof s_joy);
    def_tec(B_A, "Space", "J"); def_tec(B_B, "Left Shift", "K"); def_tec(B_X, "U", NULL); def_tec(B_Y, "I", NULL);
    def_tec(B_Z, "Z", NULL); def_tec(B_L, "Q", NULL); def_tec(B_R, "E", NULL); def_tec(B_START, "Return", NULL);
    def_tec(B_CU, "Up", NULL); def_tec(B_CD, "Down", NULL); def_tec(B_CL, "Left", NULL); def_tec(B_CR, "Right", NULL);
    def_tec(B_SU, "W", NULL); def_tec(B_SD, "S", NULL); def_tec(B_SL, "A", NULL); def_tec(B_SR, "D", NULL);
    def_tec(B_KU, "Keypad 8", NULL); def_tec(B_KD, "Keypad 2", NULL); def_tec(B_KL, "Keypad 4", NULL); def_tec(B_KR, "Keypad 6", NULL);
    for (i = 0; i < NB; i++) { s_joy[i].n = 0; }
    def_joy(B_A, SDL_CONTROLLER_BUTTON_A, -1); def_joy(B_B, SDL_CONTROLLER_BUTTON_B, -1);
    def_joy(B_X, SDL_CONTROLLER_BUTTON_X, -1); def_joy(B_Y, SDL_CONTROLLER_BUTTON_Y, -1);
    def_joy(B_START, SDL_CONTROLLER_BUTTON_START, -1); def_joy(B_Z, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, -1);
    def_joy(B_L, -1, SDL_CONTROLLER_AXIS_TRIGGERLEFT); def_joy(B_R, -1, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
    def_joy(B_CU, SDL_CONTROLLER_BUTTON_DPAD_UP, -1); def_joy(B_CD, SDL_CONTROLLER_BUTTON_DPAD_DOWN, -1);
    def_joy(B_CL, SDL_CONTROLLER_BUTTON_DPAD_LEFT, -1); def_joy(B_CR, SDL_CONTROLLER_BUTTON_DPAD_RIGHT, -1);
    s_stick = 0; s_cstick = 1; s_dz_stick = 10; s_dz_c = 10; s_gatillo = 80; controles_vibracion = 100;
}

static char* recortar(char* s) {
    char* e;
    while (*s && isspace((unsigned char)*s)) s++;
    e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
    return s;
}

static int boton_de(const char* n) {
    int i;
    for (i = 0; i < NB; i++) if (!SDL_strcasecmp(n, k_nombres[i])) return i;
    return -1;
}

static int palanca(const char* v) {
    if (!SDL_strcasecmp(v, "derecha") || !SDL_strcasecmp(v, "right")) return 1;
    if (!SDL_strcasecmp(v, "ninguna") || !SDL_strcasecmp(v, "none")) return -1;
    return 0;
}

static int leer(const char* path) {
    FILE* f = fopen(path, "rb");
    char linea[512], seccion[32] = "";
    if (!f) return 0;
    while (fgets(linea, sizeof linea, f)) {
        char *s = recortar(linea), *eq, *clave, *valor;
        int b;
        if (!*s || *s == '#' || *s == ';') continue;
        if (*s == '[') { char* e = strchr(s, ']'); if (e) *e = 0; snprintf(seccion, sizeof seccion, "%s", s + 1); continue; }
        if (!(eq = strchr(s, '='))) continue;
        *eq = 0; clave = recortar(s); valor = recortar(eq + 1);
        if (!SDL_strcasecmp(seccion, "ajustes")) {
            int v = atoi(valor);
            if (!SDL_strcasecmp(clave, "zona_muerta_stick")) s_dz_stick = v;
            else if (!SDL_strcasecmp(clave, "zona_muerta_c")) s_dz_c = v;
            else if (!SDL_strcasecmp(clave, "gatillo")) s_gatillo = v;
            else if (!SDL_strcasecmp(clave, "vibracion")) controles_vibracion = v;
            continue;
        }
        if (!SDL_strcasecmp(seccion, "joystick") && !SDL_strcasecmp(clave, "STICK")) { s_stick = palanca(valor); continue; }
        if (!SDL_strcasecmp(seccion, "joystick") && !SDL_strcasecmp(clave, "C")) { s_cstick = palanca(valor); continue; }
        if ((b = boton_de(clave)) < 0) continue;
        {
            char* tok = strtok(valor, ",");
            if (!SDL_strcasecmp(seccion, "teclado")) s_tec[b].n = 0; else s_joy[b].n = 0;
            for (; tok; tok = strtok(NULL, ",")) {
                char* t = recortar(tok);
                if (!*t) continue;
                if (!SDL_strcasecmp(seccion, "teclado")) {
                    SDL_Scancode sc = SDL_GetScancodeFromName(t);
                    if (sc != SDL_SCANCODE_UNKNOWN && s_tec[b].n < MAXT) s_tec[b].sc[s_tec[b].n++] = sc;
                } else if (!SDL_strcasecmp(seccion, "joystick") && s_joy[b].n < MAXT) {
                    SDL_GameControllerButton jb = SDL_GameControllerGetButtonFromString(t);
                    SDL_GameControllerAxis ja = SDL_GameControllerGetAxisFromString(t);
                    if (jb != SDL_CONTROLLER_BUTTON_INVALID) { s_joy[b].boton[s_joy[b].n] = jb; s_joy[b].eje[s_joy[b].n] = -1; s_joy[b].n++; }
                    else if (ja != SDL_CONTROLLER_AXIS_INVALID) { s_joy[b].boton[s_joy[b].n] = -1; s_joy[b].eje[s_joy[b].n] = ja; s_joy[b].n++; }
                }
            }
        }
    }
    fclose(f);
    return 1;
}

void controles_init(const char* exe_path) {
    static int hecho;
    if (hecho) return;
    hecho = 1;
    por_defecto();
    if (leer("controles.ini")) { rt_log("[controles] controles.ini cargado\n"); return; }
    if (exe_path) {
        char p[1024];
        const char* sl = strrchr(exe_path, '/');
        const char* bs = strrchr(exe_path, '\\');
        if (bs > sl) sl = bs;
        if (sl && (size_t)(sl - exe_path) + 16 < sizeof p) {
            memcpy(p, exe_path, (size_t)(sl - exe_path + 1));
            strcpy(p + (sl - exe_path + 1), "controles.ini");
            if (leer(p)) { rt_log("[controles] %s cargado\n", p); return; }
        }
    }
}

static int eje_pct(SDL_GameController* pad, int eje) {   /* 0..100 */
    int v = SDL_GameControllerGetAxis(pad, (SDL_GameControllerAxis)eje);
    if (v < 0) v = -v;
    return v * 100 / 32767;
}
static int zona(int v, int dz) {   /* -32768..32767 -> -100..100 con zona muerta en % */
    int a = v < 0 ? -v : v, lim = dz * 32767 / 100;
    if (a <= lim) return 0;
    a = (a - lim) * 100 / (32767 - lim > 0 ? 32767 - lim : 1);
    if (a > 100) a = 100;
    return v < 0 ? -a : a;
}

void controles_leer(const uint8_t* k, SDL_GameController* pad, PadState* p) {
    int i, j, sx = 0, sy = 0, cx = 0, cy = 0, on[NB];
    int tl = 0, tr = 0;
    memset(p, 0, sizeof *p);
    for (i = 0; i < NB; i++) {
        on[i] = 0;
        for (j = 0; j < s_tec[i].n; j++) if (k[s_tec[i].sc[j]]) on[i] = 1;
        if (pad) for (j = 0; j < s_joy[i].n; j++) {
            if (s_joy[i].boton[j] >= 0 && SDL_GameControllerGetButton(pad, (SDL_GameControllerButton)s_joy[i].boton[j])) on[i] = 1;
            if (s_joy[i].eje[j] >= 0) {
                int v = eje_pct(pad, s_joy[i].eje[j]);
                if (i == B_L && v * 255 / 100 > tl) tl = v * 255 / 100;
                if (i == B_R && v * 255 / 100 > tr) tr = v * 255 / 100;
                if (v >= s_gatillo) on[i] = 1;
            }
        }
        if (on[i]) p->buttons |= k_bits[i];
    }
    if (on[B_L]) tl = 255;
    if (on[B_R]) tr = 255;
    p->trig_l = (uint8_t)tl; p->trig_r = (uint8_t)tr;
    if (on[B_SU]) sy += 100; if (on[B_SD]) sy -= 100; if (on[B_SL]) sx -= 100; if (on[B_SR]) sx += 100;
    if (on[B_KU]) cy += 100; if (on[B_KD]) cy -= 100; if (on[B_KL]) cx -= 100; if (on[B_KR]) cx += 100;
    if (pad) {
        if (s_stick >= 0) {
            sx += zona(SDL_GameControllerGetAxis(pad, s_stick ? SDL_CONTROLLER_AXIS_RIGHTX : SDL_CONTROLLER_AXIS_LEFTX), s_dz_stick);
            sy -= zona(SDL_GameControllerGetAxis(pad, s_stick ? SDL_CONTROLLER_AXIS_RIGHTY : SDL_CONTROLLER_AXIS_LEFTY), s_dz_stick);
        }
        if (s_cstick >= 0) {
            cx += zona(SDL_GameControllerGetAxis(pad, s_cstick ? SDL_CONTROLLER_AXIS_RIGHTX : SDL_CONTROLLER_AXIS_LEFTX), s_dz_c);
            cy -= zona(SDL_GameControllerGetAxis(pad, s_cstick ? SDL_CONTROLLER_AXIS_RIGHTY : SDL_CONTROLLER_AXIS_LEFTY), s_dz_c);
        }
    }
#define AX(v) (uint8_t)(128 + ((v) > 100 ? 100 : (v) < -100 ? -100 : (v)))
    p->stick_x = AX(sx); p->stick_y = AX(sy); p->c_x = AX(cx); p->c_y = AX(cy);
#undef AX
}
#endif
