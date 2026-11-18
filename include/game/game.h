#ifndef GAME_H
#define GAME_H

#include "types.h"

/* Estados de la maquina principal del juego (gGame.state). */
enum {
    GAME_STATE_NONE = 0,
    GAME_STATE_HANGAR = 1,    /* eleccion de nave */
    GAME_STATE_BRIEFING = 2,  /* briefing de la mision */
    GAME_STATE_LEVEL = 3,     /* carga, juego y descarga de un nivel */
    GAME_STATE_IDLE = 4,
    GAME_STATE_INTRO = 5,     /* intro al encender */
    GAME_STATE_FRONTEND = 6,  /* menus */
    GAME_STATE_FRONTEND2 = 7
};

#define GAME_NUM_LEVEL_RES 7

/* Estado global del juego (0x294 bytes, al inicio de .vmbss). */
typedef struct Game {
    u32 mainRes;                          /* 0x000: recurso "main" */
    u8 view[0x24];                        /* 0x004: vista/camara principal */
    u8 unk028[0x50];                      /* 0x028 */
    u8 unk078[0x150];                     /* 0x078 */
    u8 unk1C8;                            /* 0x1C8 */
    u8 unk1C9;                            /* 0x1C9 */
    u8 unk1CA;                            /* 0x1CA */
    u8 unk1CB;
    u8 unk1CC;                            /* 0x1CC */
    u8 unk1CD;
    u8 unk1CE;                            /* 0x1CE */
    u8 unk1CF;                            /* 0x1CF */
    u8 unk1D0;                            /* 0x1D0 */
    u8 unk1D1;
    u8 unk1D2;                            /* 0x1D2 */
    u8 unk1D3;
    u8 unk1D4[0x218 - 0x1D4];             /* 0x1D4 */
    u8 unk218;                            /* 0x218 */
    u8 quit;                              /* 0x219: salir del bucle del estado actual */
    u8 unk21A;                            /* 0x21A */
    u8 unk21B;                            /* 0x21B */
    u8 unk21C;                            /* 0x21C */
    u8 unk21D[0x224 - 0x21D];
    f32 totalTime;                        /* 0x224: segundos acumulados */
    s32 frameCount;                       /* 0x228 */
    s32 level;                            /* 0x22C: nivel actual */
    s32 stage;                            /* 0x230: etapa dentro del nivel */
    u32 levelRes[GAME_NUM_LEVEL_RES];     /* 0x234: recursos "l00".."l06" */
    u32 unk250;                           /* 0x250 */
    s32 state;                            /* 0x254 */
    s32 unk258;                           /* 0x258 */
    s32 nextStage;                        /* 0x25C: etapa a cargar en levelNextStage */
    u8 unk260[4];
    s32 unk264;                           /* 0x264: nivel a cargar desde el hangar */
    s32 unk268;                           /* 0x268 */
    s32 unk26C;                           /* 0x26C: resultado de la eleccion de nave (-1 = cancelar) */
    u8 unk270;                            /* 0x270 */
    u8 unk271;                            /* 0x271 */
    u8 unk272[2];
    s32 unk274;                           /* 0x274 */
    u16 screenWidth;                      /* 0x278 */
    u16 screenHeight;                     /* 0x27A */
    u16 unk27C;                           /* 0x27C */
    u16 unk27E;                           /* 0x27E */
    u8 unk280[4];
    s32 unk284;                           /* 0x284 */
    f32 deltaTime;                        /* 0x288: segundos del ultimo cuadro */
    f32 timeScale;                        /* 0x28C */
    u8 unk290[4];
} Game;

extern Game lbl_7FDE8000;
#define gGame lbl_7FDE8000

void gameMain(void);
void levelLoad(void);
void levelUnload(void);
void levelNextStage(void);

#endif
