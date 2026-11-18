#include "vm_sections.h"
#include "game/game.h"
#include "gsys/gsys.h"

/* Ruta base de los recursos; gsysInit la completa (buffer de 0x200). */
static char sRootPath[0x200] = "main";

extern volatile s32 lbl_80095D30;
extern u8 lbl_7FE1C4A0[];

void fn_800073F0(void* ptr, s32 value, u32 size);
u32 fn_80007338(void);
s32 sprintf(char* buf, const char* fmt, ...);
u32 gsysRegisterResource(const char* name, void (*callback)(u32));
void fn_7FC03C90(void);
void gsysInit(char* rootPath, u32 size);
void fn_7FD589EC(void);
void fn_7FD436D4(s32 a, s32 b);
void fn_7FD443B8(s32 a);
void fn_7FD486EC(u16 width, u16 height);
void fn_7FD489B8(s32 a);
void fn_7FD489CC(f32 fov, f32 aspect, f32 near, f32 mid, f32 far);
void fn_7FD29758(void);
void fn_7FC07AF0(void);
void loadingWaitCounter(s32* a);
void fn_7FD5978C(void);
void memcardInit(void);
void audioLoadData(s32 a);
void fn_7FD599E0(s32 a);
void fn_7FC22E9C(void);
void textLoadLanguage(void);
void legalScreen(void);
void fn_7FD57550(void* a);
void resLoadPackage(const char* a, const char* b, s32 c, s32 d);
void fn_7FD5A27C(const char* a, const char* b, s32 c);
void fn_7FD69314(void);
void fn_7FC05350(void);
void loadingScreen(void);


/* ---- funciones del propio modulo (definidas mas abajo) ---- */
void gameStateLevel(void);
void gameStateHangar(void);
void gameStateIntro(void);
void gameStateBriefing(void);
void gameStateFrontend(void);
BOOL levelPauseCallback(u8 a, u8 abort);
void fn_7FC0099C(void);
void fn_7FC011FC(void);
void fn_7FC016A0(void);
void fn_7FC01C48(void);
void fn_7FC01F2C(void);
void frontendLoop(void);
void fn_7FC02260(s32 a);
void gameRegisterResourceCallback(u32 res);
void fn_7FC022C8(f32 b, s32 a);
void gameCheckMemoryLeaks(u32 freeMem);

/* ---- otros modulos ---- */
void levelLoad(void);
void levelUnload(void);
void levelNextStage(void);
void levelRestart(void);
void levelResetPlayerStart(void);
void staticGameLoad(void);
void staticGameUnload(void);
void gameResetTimers(void);
void gameSessionBegin(s32 arg);
void gameSessionEnd(void);
void fn_7FC0232C(void);
void fn_7FC02330(void);
void fn_7FC02408(void);
void gameFrameBegin(void);
void gameFrameUpdate(void);
void gameFrameSetTime(f32 t);
void gameFrameRender(void);
void gameFramePresent(void);
void gameFrameEnd(u8 flag);
void loadingScreen(void);
void resLoadPackage(const char* dir, const char* alias, s32 a, s32 b);
void fn_7FD47048(void);
void fn_7FD47034(void);
void fn_7FC4610C(void* a, s32 level);
void fn_7FD4A3C0(void);
u8 fn_7FD53070(BOOL (*callback)(u8, u8));
void fn_7FD462B4(s32 a);
u8 fn_7FD69300(void);
void fn_7FC84AA4(void* a);
void fn_7FC83E7C(void);
void fn_7FCDA51C(s32 a, s32 b);
s32 fn_7FC084A8(void);
void fn_7FD796B0(void);
void fn_7FD4C050(s32 a);
void fn_7FD52FD4(void);
void fn_7FD56D00(void);
s32* fn_7FC086BC(void);
void fn_7FCB83E0(s32 hangar, s32 a, s32 b, s32 c);
void fn_7FD5A4A0(s32 a);
void fn_7FD462A0(void);
void fn_7FCB8730(void);
s32 fn_7FCB7CD0(void);
void fn_7FD7F9AC(void);
void fn_7FD80300(void);
void fn_7FD69370(void);
void fn_7FD69240(void);
void fn_7FD692AC(void);
void fn_7FD6A0F0(void* a);
void fn_7FC03CD8(s32 a);
void fn_7FC03CE4(void);
void fn_7FD59A84(void);
void fn_7FC49440(void);
void fn_7FC498C0(f32 dt);
void fn_7FC48C38(void* a, f32 dt);
void fn_7FD6A290(void* a);
void fn_7FC0C8E8(void);
void fn_7FC0C98C(void* view);
void fn_7FC0C994(s32 a, f32 dt);
void fn_7FC0CA9C(f32 dt);
void fn_7FC1CDF8(f32 dt);
void fn_7FD47204(void);
void fn_7FD6B144(void* a, s32 b);
void fn_800074F8(s32 a, s32 b);

extern u8 lbl_80095D34;
extern u8 lbl_800962EC;
extern u8 lbl_800962ED;
extern u8 lbl_800962EE;
extern u8 lbl_800962EF;
extern void* lbl_800962E8;  /* objeto del frontend */
extern f32 lbl_80095D78;
extern u8 lbl_7FDCA04C[];
extern u8 lbl_7FDEFE14[];
extern u8 lbl_7FDF056C[];

/* ---- usados por el bucle de mision ---- */
void fn_7FC1EC8C(void);
void fn_7FD79600(void);
void fn_7FD5310C(BOOL (*callback)(u8, u8));
u8 fn_7FD53234(BOOL (*callback)(u8, u8));
u8 fn_7FCD93D0(void);
u8 fn_7FCD9888(f32 dt);
void fn_7FC9A708(f32 dt);
void fn_7FC48AE8(void* a, u32 i);
void fn_7FC139CC(void);
void fn_7FC13220(void);
void fn_7FCDA190(void);
void fn_7FC11A04(void);
void fn_7FCBCC68(void* view);
void fn_7FCC15DC(void* view);
void fn_7FD541FC(void* view, f32 dt);
void fn_7FD534B8(void* view, f32 dt);
void fn_7FCE5330(f32 dt);
void gameUpdateTimers(void);
void fn_7FCC9490(void);
void fn_7FCC6C98(void);
void fn_7FCCB1FC(void* a, f32 dt);
void fn_7FC96B0C(void);
void fn_7FC22A48(f32 dt);
void fn_7FC45BD0(void* a, f32 dt);
void fn_7FC9BD04(f32 dt);
void fn_7FCA10D0(f32 dt);
void fn_7FCA2E98(s32* a, f32 dt);
void fn_7FC73E2C(f32 dt);
void fn_7FCE88C8(void* a);
void fn_7FC46544(void* a, f32 dt);
void fn_7FC44E4C(void* a);
void fn_7FC6EA40(f32 dt);
void fn_7FD796F4(void);
void fn_7FD52FC4(f32 a);
u8 fn_7FD52860(void);
void fn_7FD30A38(void);
u8 fn_7FD30AC0(void);
void fn_7FD211A8(void);
void fn_7FC0CC64(s32 pass, s32 mode);
void fn_7FC73EB8(void* view);
u8 fn_7FCD84E0(void);
void fn_7FCD771C(void* view);
void fn_7FCD7D28(s32 a);
void fn_7FC738E8(s32 a);
void fn_7FC73ED8(void* view);
u8 fn_7FCD8550(void);
void fn_7FCBCCC4(void* view);
void fn_7FCC163C(void* view);
void fn_7FCA5C8C(void* view);
void fn_7FC809A8(f32 dt);
void fn_7FD20118(void);
void fn_7FD539E8(s32 a, s32 b, void* view);
void fn_7FC9CBA4(void* view);
void fn_7FCA1578(void* view);
void fn_7FCBCDF8(void);
void fn_7FCC1704(void* view);
void fn_7FCA2EC4(s32* a, void* view);
void fn_7FC7970C(void);
void fn_7FC8B158(void);
void fn_7FC82774(void* view);
void fn_7FC863D8(void);
void fn_7FD326E0(void);
void fn_7FC08668(void);
void fn_7FC6ED24(void);
void fn_7FC085EC(f32 a);
void fn_7FC3906C(void);
void fn_7FCCB418(void* a);
void fn_7FC73FB8(void);
void fn_7FD43434(s32 a);
void fn_7FC94678(void* a, s32 b, void* c, void* d);
u8 fn_7FCB8844(void* view, f32 dt);
void fn_7FCB8B9C(void* view, s32 a);
void fn_7FD806C0(void* view, f32 dt);
void fn_7FD81AA0(void* view, s32 a);
extern s32 lbl_80095F70;
extern u8 lbl_7FDF6310[];
extern u8 lbl_7FDF04F0[];
extern u8 lbl_7FDF80B0[];
extern u8* lbl_80095DC8; /* jugador */

/* Jugador muerto (estado 0x11 con la marca 0xE04 puesta). */
static inline u8 playerIsDead(void) {
    u8* player = lbl_80095DC8;
    if (player[0xE04] && *(s32*)(player + 0xA7C) == 0x11) {
        return TRUE;
    }
    return FALSE;
}

/* Punto de entrada del juego: inicializa todo y corre la maquina de estados. */
void gameMain(void) {
    Game* game = &gGame;
    s32 i;
    s32* state;
    u32 freeMem;
    s32* mode;
    s32* unk25C;
    char name[20];

    fn_800073F0(game, 0, sizeof(Game));
    game->timeScale = 1.0f;
    game->unk27C = 16;
    game->unk27E = 14;

    fn_7FC03C90();
    gsysInit(sRootPath, sizeof(sRootPath));
    fn_7FD589EC();
    fn_7FD436D4(2, 0x280);
    fn_7FD443B8(0);
    fn_7FD486EC(game->screenWidth, game->screenHeight);
    fn_7FD489B8(0);
    fn_7FD489CC(1.5f, 0.75f, 400.0f, 3000.0f, 20000.0f);
    fn_7FD29758();
    fn_7FC07AF0();

    lbl_80095D30 = 8;
    game->mainRes = gsysRegisterResource("main", gameRegisterResourceCallback);
    for (i = 0; i < GAME_NUM_LEVEL_RES; i++) {
        sprintf(name, "l%02d", i);
        game->levelRes[i] = gsysRegisterResource(name, gameRegisterResourceCallback);
    }

    loadingWaitCounter(&lbl_80095D30);
    fn_7FD5978C();
    memcardInit();
    audioLoadData(0);
    fn_7FD599E0(1);
    fn_7FC22E9C();
    textLoadLanguage();
    legalScreen();
    audioLoadData(1);
    fn_7FD57550(lbl_7FE1C4A0);
    resLoadPackage("movieInfo", "movieInfo", 6, 0);
    fn_7FD5A27C("staticGame/us", "staticGame_us", 0);
    fn_7FD5A27C("pauseMenu", "pauseMenu", 0);
    fn_7FD69314();
    fn_7FC05350();
    fn_7FD599E0(0);
    loadingScreen();

    freeMem = fn_80007338();
    gsysDebugPrintf("--------- resident setup done %d bytes free ---------\n", freeMem);

    game->unk26C = -1;
    game->unk264 = 0x13;
    unk25C = &game->nextStage;
    *unk25C = 0;
    mode = &game->level;
    game->stage = 0;
    *mode = 0x19;
    state = &game->state;
    game->state = GAME_STATE_FRONTEND;
    if (*mode == 0x19) {
        *state = GAME_STATE_FRONTEND;
    }
    game->unk258 = *mode;
    *state = GAME_STATE_INTRO;
    game->unk258 = 0x16;
    *mode = game->unk258;
    *unk25C = 0;

    for (;;) {
        gameCheckMemoryLeaks(freeMem);
        switch (*state) {
        case GAME_STATE_FRONTEND:
        case GAME_STATE_FRONTEND2:
            gameStateFrontend();
            break;
        case GAME_STATE_INTRO:
            gameStateIntro();
            *state = GAME_STATE_FRONTEND;
            break;
        case GAME_STATE_BRIEFING:
            gameStateBriefing();
            if (*state == GAME_STATE_NONE) {
                *state = GAME_STATE_FRONTEND;
            }
            break;
        case GAME_STATE_HANGAR:
            gameStateHangar();
            if (*state == GAME_STATE_NONE) {
                *state = GAME_STATE_BRIEFING;
            }
            break;
        case GAME_STATE_LEVEL:
            gameStateLevel();
            if (*state == GAME_STATE_NONE) {
                *state = GAME_STATE_HANGAR;
            }
            break;
        case GAME_STATE_IDLE:
            break;
        }
    }
}

/*
 * Estado 3: juega un nivel completo. Repite la etapa mientras se reintenta y
 * pasa a la siguiente etapa cuando corresponde.
 */
void gameStateLevel(void) {
    Game* game = &gGame;
    s32 i;
    s32 stageCount;
    s32 stage;

    game->level = game->unk258;
    game->stage = game->nextStage;
    game->unk270 = 0;
    game->unk271 = 1;
    game->unk274 = 0;
    game->unk21B = 0;
    game->unk21A = 0;
    game->quit = 0;
    game->unk21C = 0;
    fn_7FC02408();
    gameSessionBegin(0);
    fn_7FC0232C();
    levelLoad();
    fn_7FD47048();
    lbl_800962EE = 3;

    for (;;) {
        game->unk21A = 0;
        game->quit = 0;
        game->unk21C = 0;
        fn_7FC4610C(lbl_7FDEFE14, game->level);
        gameResetTimers();
        fn_7FD4A3C0();
        if (fn_7FD53070(levelPauseCallback)) {
            fn_7FC011FC();
            levelRestart();
            if (game->level == 0xE) {
                fn_7FD462B4(1);
                game->state = GAME_STATE_FRONTEND2;
                break;
            }
            if (fn_7FD69300()) {
                if (lbl_80095D34) {
                    game->state = GAME_STATE_FRONTEND;
                } else {
                    game->state = GAME_STATE_INTRO;
                    game->unk258 = 0x16;
                    game->nextStage = 0;
                }
                break;
            }
        }
        if (game->level == 5 || game->level == 9 || game->level == 0xB) {
            fn_7FC84AA4(lbl_7FDCA04C);
        }
        fn_7FC83E7C();
        for (i = 0; i < 5; i++) {
            fn_7FCDA51C(i, 1);
        }
        levelResetPlayerStart();
        fn_7FC0099C();
        fn_7FD462B4(1);
        if (!game->unk271 || game->unk270) {
            break;
        }
        if (game->state != GAME_STATE_IDLE && game->state != GAME_STATE_LEVEL) {
            break;
        }
        stageCount = fn_7FC084A8();
        stage = game->stage;
        if (stage + 1 >= stageCount) {
            break;
        }
        if (game->state != GAME_STATE_IDLE && (game->level == 0 || game->level == 7)) {
            break;
        }
        if (game->state != GAME_STATE_IDLE && game->level == 10 && stage == 1) {
            break;
        }
        game->state = GAME_STATE_LEVEL;
        game->nextStage = game->stage + 1;
        levelNextStage();
    }

    fn_7FD796B0();
    levelUnload();
    fn_7FC02330();
    gameSessionEnd();
    if (fn_7FD69300()) {
        lbl_800962EF = 4;
        fn_7FD4C050(0);
    }
    if (game->state == GAME_STATE_IDLE) {
        game->state = GAME_STATE_LEVEL;
        game->nextStage = 0;
        game->unk264 = 0x12;
    } else if (game->state == GAME_STATE_LEVEL) {
        game->state = GAME_STATE_FRONTEND2;
    }
}

/* Estado 1: hangar, eleccion de nave. */
void gameStateHangar(void) {
    Game* game = &gGame;
    s32* sel;
    s32* level;
    s32 hangar;

    sel = fn_7FC086BC();
    if (sel != NULL) {
        level = &game->level;
        *level = game->unk264;
        game->stage = 0;
        game->unk268 = sel[1];
        fn_7FC02408();
        gameSessionBegin(1);
        fn_7FC0232C();
        levelLoad();
        fn_7FD47048();
        levelResetPlayerStart();
        switch (*level) {
        case 0x12:
            hangar = 0;
            break;
        case 0x13:
            hangar = 1;
            break;
        case 0x14:
            hangar = 2;
            break;
        case 0x15:
            hangar = 3;
            break;
        }
        fn_7FCB83E0(hangar, sel[1], sel[2], 0);
        fn_7FD5A4A0(1);
        gameResetTimers();
        fn_7FD462A0();
        fn_7FC01C48();
        fn_7FD462B4(0);
        fn_7FCB8730();
        if ((game->unk26C = fn_7FCB7CD0()) == -1) {
            game->state = GAME_STATE_NONE;
        } else {
            game->state = GAME_STATE_LEVEL;
        }
        levelUnload();
        fn_7FC02330();
        gameSessionEnd();
        if (game->unk258 == 10) {
            game->nextStage = (fn_7FCB7CD0() == 5) ? 2 : 0;
        }
    }
}

/* Estado 5: intro al encender (nivel 0x16 = escena de presentacion). */
void gameStateIntro(void) {
    Game* game = &gGame;
    s32* level;

    level = &game->level;
    game->level = game->unk258;
    game->nextStage = 0;
    game->stage = 0;
    game->unk270 = 0;
    game->unk271 = 1;
    game->unk274 = 0;
    game->unk21B = 0;
    game->unk21A = 0;
    game->quit = 0;
    game->unk21C = 0;
    fn_7FC02408();
    gameSessionBegin(0);
    fn_7FC0232C();
    levelLoad();
    fn_7FC4610C(lbl_7FDEFE14, *level);
    gameResetTimers();
    fn_7FD4A3C0();
    if (*level == 0x16) {
        fn_7FC016A0();
    } else if (fn_7FD53070(levelPauseCallback)) {
        fn_7FC011FC();
    }
    fn_7FD462B4(1);
    levelUnload();
    fn_7FC02330();
    gameSessionEnd();
    game->state = GAME_STATE_FRONTEND;
}

/* Estado 2: briefing de la mision. */
void gameStateBriefing(void) {
    gGame.level = 0x18;
    fn_7FC02408();
    gameSessionBegin(2);
    fn_7FC0232C();
    fn_7FD4965C();
    resLoadPackage("briefing", "briefing", 3, 0);
    loadingScreen();
    staticGameLoad();
    gameResetTimers();
    fn_7FD7F9AC();
    fn_7FD47048();
    fn_7FC01F2C();
    fn_7FD462B4(0);
    fn_7FD80300();
    staticGameUnload();
    fn_7FC02330();
    gameSessionEnd();
}

/* Estados 6 y 7: menus principales. */
void gameStateFrontend(void) {
    Game* game = &gGame;
    s32 unused; /* el original reserva 8 bytes de pila que no usa */

    lbl_800962EC = game->level;
    lbl_800962ED = game->stage;
    game->level = 0x19;
    fn_7FC02408();
    gameSessionBegin(3);
    fn_7FC0232C();
    fn_7FD4965C();
    resLoadPackage("frontend", "feData", 3, 0);
    loadingScreen();
    staticGameLoad();
    if (game->state != GAME_STATE_FRONTEND2 || !game->unk271 || (s32)lbl_800962EC != 10) {
        fn_7FD47034();
    }
    game->quit = 0;
    fn_7FD69370();
    fn_7FD69240();
    frontendLoop();
    fn_7FD462B4(0);
    fn_7FD692AC();
    fn_7FD6A0F0(lbl_800962E8);
    staticGameUnload();
    fn_7FC02330();
    gameSessionEnd();
}

/*
 * Bucle de una mision: un cuadro por vuelta hasta que se pide salir.
 * Actualiza el mundo, maneja la pausa, el fin de mision (con fundido) y las
 * cinematicas, y dibuja la escena en pasadas numeradas.
 */
void fn_7FC0099C(void) {
    Game* game = &gGame;
    u8* unk21A;
    u8* quit;
    u8* unk21C;
    u8* unk218;
    u8 ending;
    u8 paused;
    f32 fade;
    BOOL (*callback)(u8, u8);
    void* view;
    u8* unk271;
    f32* dt;
    u8* unk1C8;
    u8* player;
    u32 i;
    u8* obj;
    s32 off;

    unk21A = &game->unk21A;
    quit = &game->quit;
    unk21C = &game->unk21C;
    unk218 = &game->unk218;
    ending = FALSE;
    paused = FALSE;
    fade = 0.0f;
    game->unk21A = 0;
    game->quit = 0;
    game->unk21C = 0;
    game->unk218 = 0;
    fn_7FC03CD8(0);
    fn_7FC1EC8C();
    if (fn_7FD69300()) {
        fn_7FD79600();
    }
    callback = levelPauseCallback;
    fn_7FD5310C(callback);
    view = game->view;
    unk271 = &game->unk271;
    dt = &game->deltaTime;
    unk1C8 = &game->unk1C8;

    while (!*quit) {
        gameFrameBegin();
        fn_7FC03CE4();
        gameFrameUpdate();
        fn_7FD59A84();

        if (fn_7FCD93D0()) {
            /* cinematica en curso */
            fn_7FC022C8(*dt, 1);
            if (fn_7FCD9888(*dt)) {
                continue;
            }
            fn_7FC9A708(*dt);
            for (i = 0; i < 4; i++) {
                fn_7FC48AE8(lbl_7FDF056C, i);
            }
            lbl_7FDF056C[1] = 1;
            gameFrameSetTime(*dt);
            gameFrameRender();
            fn_7FC139CC();
            fn_7FC13220();
            gameFramePresent();
            fn_7FCDA190();
            fn_7FC11A04();
            fn_7FD47204();
            fn_7FCDA190();
            fn_7FC11A04();
            gameFrameEnd(1);
            continue;
        }

        fn_7FCBCC68(view);
        fn_7FCC15DC(view);
        fn_7FC49440();
        fn_7FC498C0(*dt);
        fn_7FC48C38(lbl_7FDF056C, *dt);
        fn_7FC022C8(*dt, 1);
        if (!*unk218 && !game->unk1CE) {
            /* actualizacion completa del mundo */
            fn_7FD541FC(view, *dt);
            fn_7FD534B8(view, *dt);
            fn_7FC0C8E8();
            fn_7FC0C98C(view);
            fn_7FCE5330(*dt);
            fn_7FC0C994(0, *dt);
            gameUpdateTimers();
            fn_7FCC9490();
            fn_7FCC6C98();
            fn_7FCCB1FC(lbl_7FDF6310, *dt);
            fn_7FC96B0C();
            fn_7FC1EC8C();
            fn_7FC22A48(*dt);
            if (!game->unk1C8) {
                fn_7FC45BD0(lbl_7FDEFE14, *dt);
                fn_7FC0C994(2, *dt);
            }
            fn_7FC0C994(6, *dt);
            fn_7FC0C994(0x2F, *dt);
            fn_7FC0C994(8, *dt);
            fn_7FC0C994(0x24, *dt);
            fn_7FC0C994(0xA, *dt);
            fn_7FC0C994(0x16, *dt);
            fn_7FC0C994(0x15, *dt);
            fn_7FC0C994(3, *dt);
            fn_7FC0C994(0x36, *dt);
            fn_7FC9BD04(*dt);
            fn_7FCA10D0(*dt);
            fn_7FCA2E98(&lbl_80095F70, *dt);
            fn_7FC73E2C(*dt);
            fn_7FC0C994(7, *dt);
            fn_7FCE88C8(lbl_7FDF80B0);
            fn_7FC46544(lbl_7FDF04F0, *dt);
        } else {
            fn_7FC0C8E8();
            fn_7FC0C98C(view);
            fn_7FCC6C98();
        }

        if (*unk1C8 || !game->unk1CE) {
            fn_7FC1CDF8(*dt);
            if (!*unk1C8) {
                fn_7FC44E4C(lbl_7FDEFE14);
            }
        }
        fn_7FC6EA40((*unk218 || game->unk1CE) ? 0.0f : *dt);
        fn_7FC0CA9C(*dt);
        gameFrameSetTime(lbl_80095D78);
        fn_7FD796F4();

        if (!ending && *unk21A) {
            /* fin de mision: arranca el fundido */
            ending = TRUE;
            fade = 0.0f;
            if (!playerIsDead() && lbl_800962EF != 0x21) {
                paused = fn_7FD53234(callback);
                if (paused) {
                    *unk21C = 0;
                    fn_7FD52FC4(1.0f);
                }
            }
            if (!fn_7FD52860() || playerIsDead()) {
                if (game->unk270 || !*unk271 || game->stage + 1 >= fn_7FC084A8()) {
                    fn_7FD5A4A0(0);
                } else if (*unk271) {
                    fn_7FD5A4A0(4);
                }
            }
        } else if (ending && !*quit && !playerIsDead()) {
            if (!paused || *unk21C) {
                fade += 2.0f * *dt;
                if (fade >= 1.0f) {
                    fade = 1.0f;
                    fn_7FD30A38();
                    if (fn_7FD30AC0()) {
                        *quit = 1;
                    }
                }
            }
        }

        /* dibujo */
        fn_7FD211A8();
        gameFrameRender();
        obj = *(u8**)lbl_7FDEFE14;
        if (obj != NULL && !*unk1C8) {
            void** vt = *(void***)(obj + 0x80);
            ((void (*)(void*))vt[0x110 / 4])(obj);
        }
        fn_7FC0CC64(0x34, 0);
        fn_7FC73EB8(view);
        fn_7FC139CC();
        fn_7FC13220();
        if (fn_7FCD84E0()) {
            fn_7FCD771C(view);
            fn_7FC0CC64(0x31, 5);
            fn_7FCD7D28(0);
            fn_7FC0CC64(0x32, 5);
            fn_7FCD7D28(1);
            fn_7FC0CC64(0x33, 5);
            fn_7FC0CC64(0x15, 5);
            fn_7FC738E8(1);
            fn_7FC11A04();
            fn_7FC73ED8(view);
            fn_7FC11A04();
            fn_7FCD7D28(2);
        }
        gameFramePresent();
        if (fn_7FCD8550()) {
            fn_7FC0CC64(0x31, 1);
            fn_7FCD7D28(0);
            fn_7FC0CC64(0x32, 1);
            fn_7FCD7D28(1);
            fn_7FCBCCC4(view);
            fn_7FCC163C(view);
            fn_7FCA5C8C(view);
            fn_7FC809A8(*dt);
            fn_7FC0CC64(0x33, 1);
            fn_7FC0CC64(0x3A, 4);
            fn_7FD20118();
            fn_7FC0CC64(3, 1);
            fn_7FC0CC64(0x15, 1);
            fn_7FD539E8(1, 1, view);
        } else {
            fn_7FCBCCC4(view);
            fn_7FCC163C(view);
            fn_7FCA5C8C(view);
            fn_7FC809A8(*dt);
            fn_7FC0CC64(0x31, 1);
            fn_7FC0CC64(0x32, 1);
            fn_7FC0CC64(0x33, 1);
            fn_7FC0CC64(0x3A, 4);
            fn_7FD20118();
            fn_7FC0CC64(0x15, 1);
            fn_7FD539E8(1, 1, view);
            fn_7FC0CC64(3, 1);
        }
        fn_7FC9CBA4(view);
        fn_7FCA1578(view);
        fn_7FCBCDF8();
        fn_7FCC1704(view);
        fn_7FCA2EC4(&lbl_80095F70, view);
        fn_7FC11A04();
        fn_7FC7970C();
        fn_7FC8B158();
        fn_7FC82774(view);
        fn_7FC863D8();
        fn_7FC738E8(2);
        fn_7FC0CC64(0x37, 2);
        fn_7FD326E0();
        fn_7FC0CC64(1, 1);
        fn_7FC0CC64(2, 1);
        fn_7FC0CC64(0x38, 2);
        fn_7FC11A04();
        fn_7FC0CC64(0x36, 1);
        fn_7FC08668();
        fn_7FC738E8(1);
        fn_7FC6ED24();
        fn_7FC085EC(1.0f);
        fn_7FC73ED8(view);
        fn_7FC11A04();
        if (!*unk1C8) {
            fn_7FC3906C();
        }
        fn_7FCCB418(lbl_7FDF6310);
        fn_7FC73FB8();
        if (ending) {
            fn_7FD43434((s32)(255.0f * (1.0f - fade)));
        }
        gameFrameEnd(1);
    }

    ((BOOL (*)(u8, u8, f32))levelPauseCallback)(0, 0, 0.0f);
    obj = *(u8**)(lbl_7FDEFE14 + 0x10);
    if (obj != NULL && (off = *(s32*)(obj + 0x80)) != 0) {
        fn_7FC94678(obj + off, 1, obj, obj);
    }
    fn_7FC03CD8(3);
}

/*
 * Bucle tras terminar/abortar una mision (sigue dibujando el mundo mientras
 * se muestra el resultado) hasta que se pide salir.
 */
void fn_7FC011FC(void) {
    Game* game = &gGame;
    u8* unk21A;
    u8* quit;
    u8* unk21C;
    u8* unk218;
    u8 ending;
    f32 fade;
    void* view;
    f32* dt;
    u8* unk1C8;
    u8* obj;

    unk21A = &game->unk21A;
    quit = &game->quit;
    unk21C = &game->unk21C;
    unk218 = &game->unk218;
    ending = FALSE;
    fade = 0.0f;
    game->unk21A = 0;
    game->quit = 0;
    game->unk21C = 0;
    game->unk218 = 0;
    fn_7FC03CD8(0);
    view = game->view;
    dt = &game->deltaTime;
    unk1C8 = &game->unk1C8;

    while (!*quit) {
        gameFrameBegin();
        fn_7FC03CE4();
        gameFrameUpdate();
        fn_7FCBCC68(view);
        fn_7FCC15DC(view);
        fn_7FC022C8(*dt, 1);
        if (!*unk218 && !game->unk1CE) {
            fn_7FD541FC(view, *dt);
            fn_7FD534B8(view, *dt);
            fn_7FC0C8E8();
            fn_7FC0C98C(view);
            fn_7FC0C994(0, *dt);
            gameUpdateTimers();
            fn_7FCC9490();
            fn_7FC96B0C();
            if (!game->unk1C8) {
                fn_7FC45BD0(lbl_7FDEFE14, *dt);
                fn_7FC0C994(2, *dt);
            }
            fn_7FC0C994(6, *dt);
            fn_7FC0C994(8, *dt);
            fn_7FC0C994(0x24, *dt);
            fn_7FC0C994(0xA, *dt);
            fn_7FC0C994(0x16, *dt);
            fn_7FC0C994(0x15, *dt);
            fn_7FC0C994(3, *dt);
            fn_7FC0C994(0x36, *dt);
            fn_7FC9BD04(*dt);
            fn_7FCA10D0(*dt);
            fn_7FCA2E98(&lbl_80095F70, *dt);
            fn_7FC73E2C(*dt);
            fn_7FC0C994(7, *dt);
        } else {
            fn_7FC0C8E8();
            fn_7FC0C98C(view);
        }
        if (*unk1C8 || !game->unk1CE) {
            fn_7FC1CDF8(*dt);
            if (!*unk1C8) {
                fn_7FC44E4C(lbl_7FDEFE14);
            }
        }
        fn_7FC6EA40((*unk218 || game->unk1CE) ? 0.0f : *dt);
        fn_7FC0CA9C(*dt);
        gameFrameSetTime(lbl_80095D78);
        if (fn_7FD69300()) {
            if (!ending && *unk21A) {
                fade = 0.0f;
                ending = TRUE;
            } else if (ending && !*quit) {
                fade += 2.0f * *dt;
                if (fade >= 1.0f) {
                    fade = 1.0f;
                    game->unk21B = 0;
                    fn_7FD52FD4();
                    fn_7FD56D00();
                    *unk21C = 1;
                }
            }
        }
        if (*unk21C) {
            fn_7FD30A38();
            if (fn_7FD30AC0()) {
                *quit = 1;
            }
            if (game->level == 0xE) {
                fade = 1.0f;
            }
        }

        fn_7FD211A8();
        gameFrameRender();
        obj = *(u8**)lbl_7FDEFE14;
        if (obj != NULL && !*unk1C8) {
            void** vt = *(void***)(obj + 0x80);
            ((void (*)(void*))vt[0x110 / 4])(obj);
        }
        fn_7FC0CC64(0x34, 0);
        fn_7FC73EB8(view);
        fn_7FC139CC();
        fn_7FC13220();
        gameFramePresent();
        fn_7FC0CC64(0x31, 1);
        fn_7FC0CC64(0x32, 1);
        fn_7FC0CC64(0x33, 1);
        fn_7FC0CC64(0x3A, 4);
        fn_7FD20118();
        fn_7FC0CC64(0x15, 1);
        fn_7FD539E8(1, 1, view);
        fn_7FC0CC64(3, 1);
        fn_7FC11A04();
        fn_7FC7970C();
        fn_7FC8B158();
        fn_7FC82774(view);
        fn_7FC863D8();
        fn_7FC738E8(2);
        fn_7FC0CC64(0x37, 2);
        fn_7FD326E0();
        fn_7FC0CC64(1, 1);
        fn_7FC0CC64(2, 1);
        fn_7FC0CC64(0x38, 2);
        fn_7FC11A04();
        fn_7FC0CC64(0x36, 1);
        fn_7FC08668();
        fn_7FC6ED24();
        fn_7FC738E8(1);
        fn_7FC085EC(1.0f);
        fn_7FC73ED8(view);
        fn_7FC11A04();
        fn_7FC73FB8();
        if (ending || (*unk21C && game->level == 0xE)) {
            fn_7FD43434((s32)(255.0f * (1.0f - fade)));
        }
        gameFrameEnd(0);
    }
    *unk21A = 0;
    *quit = 0;
    *unk21C = 0;
    fn_7FC03CD8(3);
}

/*
 * Bucle de la escena de intro de mision (vuelo con camara de cine): igual que
 * el de mision pero con fundido de entrada y dos pasadas de dibujado por cuadro.
 */
void fn_7FC016A0(void) {
    Game* game = &gGame;
    u8* unk21A;
    u8* quit;
    u8* unk21C;
    u8* unk218;
    u8 ending;
    u8 paused;
    f32 fadeOut;
    f32 fadeIn;
    void* view2;
    void* view;
    u8* unk271;
    f32* dt;
    u8* unk1C8;
    u8* obj;
    s32 i;
    u8 drawFade;
    s32 alpha;
    BOOL (*callback)(u8, u8);

    unk21A = &game->unk21A;
    quit = &game->quit;
    unk21C = &game->unk21C;
    unk218 = &game->unk218;
    ending = FALSE;
    paused = FALSE;
    fadeOut = 0.0f;
    fadeIn = fadeOut;
    game->unk21A = 0;
    game->quit = 0;
    game->unk21C = 0;
    game->unk218 = 0;
    fn_7FD47034();
    levelResetPlayerStart();
    fn_7FC03CD8(0);
    fn_7FC1EC8C();
    view2 = game->view;
    view = view2;
    unk271 = &game->unk271;
    dt = &game->deltaTime;
    unk1C8 = &game->unk1C8;
    callback = levelPauseCallback;

    while (!*quit) {
        gameFrameBegin();
        fn_7FC03CE4();
        gameFrameUpdate();
        fn_7FD59A84();
        fn_7FCBCC68(view);
        fn_7FCC15DC(view);
        fn_7FC49440();
        fn_7FC498C0(*dt);
        fn_7FC48C38(lbl_7FDF056C, *dt);
        fn_7FC022C8(*dt, 1);
        if (!*unk218 && !game->unk1CE) {
            fn_7FD541FC(view, *dt);
            fn_7FD534B8(view, *dt);
            fn_7FC0C8E8();
            fn_7FC0C98C(view);
            fn_7FCE5330(*dt);
            fn_7FC0C994(0, *dt);
            gameUpdateTimers();
            fn_7FCC9490();
            fn_7FCC6C98();
            fn_7FC96B0C();
            fn_7FC1EC8C();
            fn_7FC22A48(*dt);
            if (!game->unk1C8) {
                fn_7FC45BD0(lbl_7FDEFE14, *dt);
                fn_7FC0C994(2, *dt);
            }
            fn_7FC0C994(6, *dt);
            fn_7FC0C994(0x2F, *dt);
            fn_7FC0C994(8, *dt);
            fn_7FC0C994(0x24, *dt);
            fn_7FC0C994(0xA, *dt);
            fn_7FC0C994(0x16, *dt);
            fn_7FC0C994(0x15, *dt);
            fn_7FC0C994(3, *dt);
            fn_7FC0C994(0x36, *dt);
            fn_7FC9BD04(*dt);
            fn_7FCA10D0(*dt);
            fn_7FCA2E98(&lbl_80095F70, *dt);
            fn_7FC73E2C(*dt);
            fn_7FC0C994(7, *dt);
            fn_7FCE88C8(lbl_7FDF80B0);
            if (fadeIn < 1.0f) {
                fadeIn += 3.0f * *dt;
                if (fadeIn > 1.0f) {
                    fadeIn = 1.0f;
                }
            }
        } else {
            fn_7FC0C8E8();
            fn_7FC0C98C(view);
        }
        if (*unk1C8 || !game->unk1CE) {
            fn_7FC1CDF8(*dt);
            if (!*unk1C8) {
                fn_7FC44E4C(lbl_7FDEFE14);
            }
        }
        fn_7FC6EA40((*unk218 || game->unk1CE) ? 0.0f : *dt);
        fn_7FC0CA9C(*dt);
        gameFrameSetTime(lbl_80095D78);
        if (!ending && *unk21A) {
            fadeOut = 0.0f;
            ending = TRUE;
            paused = fn_7FD53234(callback);
            if (paused) {
                *unk21C = 0;
                fn_7FD52FC4(2.0f);
            }
            if (!fn_7FD52860()) {
                if (!game->unk270 && *unk271 && game->stage + 1 < fn_7FC084A8()) {
                    if (*unk271) {
                        fn_7FD5A4A0(4);
                    }
                } else {
                    fn_7FD5A4A0(0);
                }
            }
        } else if (ending && !*quit) {
            if (!paused || (paused && *unk21C)) {
                fadeOut += 2.0f * *dt;
                if (fadeOut >= 1.0f) {
                    fadeOut = 1.0f;
                    fn_7FD30A38();
                    if (fn_7FD30AC0()) {
                        *quit = 1;
                    }
                }
            }
        }

        fn_7FD211A8();
        gameFrameRender();
        obj = *(u8**)lbl_7FDEFE14;
        if (obj != NULL && !*unk1C8) {
            void** vt = *(void***)(obj + 0x80);
            ((void (*)(void*))vt[0x110 / 4])(obj);
        }
        fn_7FC0CC64(0x34, 0);
        fn_7FC73EB8(view);
        fn_7FC139CC();
        fn_7FC13220();
        gameFramePresent();
        drawFade = ending;
        alpha = (s32)(255.0f * (fadeIn * (1.0f - fadeOut)));
        for (i = 0; i < 2; i++) {
            fn_7FCBCCC4(view);
            fn_7FC0CC64(0x31, 1);
            fn_7FC0CC64(0x32, 1);
            fn_7FC0CC64(0x33, 1);
            fn_7FC0CC64(0x3A, 4);
            fn_7FD20118();
            fn_7FD539E8(1, 1, view);
            fn_7FC0CC64(3, 1);
            fn_7FCBCDF8();
            fn_7FC11A04();
            fn_7FC7970C();
            fn_7FC8B158();
            fn_7FC82774(view2);
            fn_7FC863D8();
            fn_7FC738E8(2);
            fn_7FC0CC64(0x37, 2);
            fn_7FD326E0();
            fn_7FC0CC64(0x38, 2);
            fn_7FC08668();
            fn_7FC6ED24();
            fn_7FC738E8(1);
            fn_7FC085EC(1.0f);
            fn_7FC11A04();
            fn_7FC73ED8(view);
            fn_7FC11A04();
            fn_7FC73FB8();
            if (drawFade) {
                fn_7FD43434(alpha);
            }
            if (i == 0) {
                fn_7FD47204();
            }
        }
        gameFrameEnd(1);
    }
    levelPauseCallback(0, 0);
    *unk21A = 0;
    *quit = 0;
    *unk21C = 0;
    fn_7FC03CD8(3);
}

/* Bucle del hangar: dibuja el hangar y corre la eleccion de nave hasta terminar. */
void fn_7FC01C48(void) {
    Game* game = &gGame;
    u8* quit;
    u8* unk21C;
    u8* unk218;
    u8 done;
    void* view;
    f32* dt;
    u8* unk1C8;
    u8* obj;

    quit = &game->quit;
    unk21C = &game->unk21C;
    unk218 = &game->unk218;
    done = FALSE;
    game->quit = 0;
    game->unk21C = 0;
    game->unk218 = 0;
    fn_7FC03CD8(0);
    fn_7FD4A3C0();
    view = game->view;
    dt = &game->deltaTime;
    unk1C8 = &game->unk1C8;
    while (!done && !*quit) {
        gameFrameBegin();
        fn_7FC03CE4();
        gameFrameUpdate();
        fn_7FD59A84();
        fn_7FC49440();
        fn_7FC498C0(*dt);
        fn_7FC48C38(lbl_7FDF056C, *dt);
        fn_7FC022C8(*dt, 1);
        if (!*unk218 && !game->unk1CE) {
            fn_7FD541FC(view, *dt);
            fn_7FD534B8(view, *dt);
            done = fn_7FCB8844(view, *dt);
            fn_7FC0C8E8();
            fn_7FC0C98C(view);
            fn_7FCE5330(*dt);
            fn_7FC0C994(0, *dt);
            fn_7FC0C994(4, *dt);
            fn_7FC0C994(5, *dt);
            fn_7FC0C994(6, *dt);
            gameUpdateTimers();
            fn_7FC96B0C();
            fn_7FC1EC8C();
            fn_7FC22A48(*dt);
            fn_7FC0C994(8, *dt);
            fn_7FC0C994(3, *dt);
            fn_7FC73E2C(*dt);
            fn_7FC0C994(7, *dt);
            fn_7FC0CA9C(*dt);
            fn_7FCE88C8(lbl_7FDF80B0);
        } else {
            fn_7FC0C8E8();
            fn_7FC0C98C(view);
            fn_7FC0CA9C(*dt);
        }
        gameFrameSetTime(lbl_80095D78);
        if (*unk1C8 || !game->unk1CE) {
            fn_7FC1CDF8(*dt);
        }
        gameFrameRender();
        obj = *(u8**)lbl_7FDEFE14;
        if (obj != NULL && !*unk1C8) {
            void** vt = *(void***)(obj + 0x80);
            ((void (*)(void*))vt[0x110 / 4])(obj);
        }
        fn_7FC0CC64(0x34, 0);
        fn_7FC73EB8(view);
        fn_7FC139CC();
        fn_7FC13220();
        gameFramePresent();
        fn_7FCB8B9C(view, 1);
        fn_7FC0CC64(0, 1);
        fn_7FC0CC64(4, 1);
        fn_7FC0CC64(5, 1);
        fn_7FD539E8(1, 1, view);
        fn_7FC0CC64(6, 1);
        fn_7FC0CC64(3, 1);
        fn_7FC11A04();
        fn_7FC0CC64(0x37, 2);
        fn_7FC0CC64(0x38, 2);
        fn_7FC11A04();
        fn_7FC82774(view);
        fn_7FC863D8();
        fn_7FC73ED8(view);
        fn_7FC11A04();
        fn_7FCB8B9C(view, 0);
        gameFrameEnd(0);
    }
    fn_7FC03CD8(3);
    game->unk21A = 0;
    *quit = 0;
    *unk21C = 0;
}

/* Bucle del briefing. */
void fn_7FC01F2C(void) {
    Game* game = &gGame;
    u8* quit;
    u8* unk218;
    void* view;
    f32* dt;

    quit = &game->quit;
    unk218 = &game->unk218;
    game->quit = 0;
    game->unk218 = 0;
    fn_7FC03CD8(0);
    fn_7FD4A3C0();
    view = game->view;
    dt = &game->deltaTime;
    while (!*quit) {
        gameFrameBegin();
        fn_7FC03CE4();
        gameFrameUpdate();
        fn_7FD59A84();
        fn_7FC49440();
        fn_7FC498C0(*dt);
        fn_7FC48C38(lbl_7FDF056C, *dt);
        fn_7FC022C8(*dt, 1);
        if (!*unk218 && !game->unk1CE) {
            fn_7FC0C8E8();
            fn_7FC0C98C(view);
            fn_7FD806C0(view, *dt);
            fn_7FC0C994(4, *dt);
            fn_7FC0C994(5, *dt);
            fn_7FC73E2C(*dt);
            fn_7FC0C994(7, *dt);
            fn_7FC0CA9C(*dt);
        } else {
            fn_7FC0C8E8();
            fn_7FC0C98C(view);
            fn_7FC0CA9C(*dt);
        }
        gameFrameSetTime(lbl_80095D78);
        if (game->unk1C8 || !game->unk1CE) {
            fn_7FC1CDF8(*dt);
        }
        gameFrameRender();
        fn_7FC0CC64(0x34, 0);
        fn_7FC73EB8(view);
        fn_7FC139CC();
        fn_7FC13220();
        gameFramePresent();
        fn_7FD81AA0(view, 1);
        fn_7FC0CC64(0, 1);
        fn_7FC0CC64(4, 1);
        fn_7FC0CC64(5, 1);
        fn_7FC11A04();
        fn_7FC0CC64(0x37, 2);
        fn_7FC0CC64(0x38, 2);
        fn_7FC11A04();
        fn_7FC82774(view);
        fn_7FC863D8();
        fn_7FC73ED8(view);
        fn_7FC11A04();
        fn_7FC0CC64(5, 2);
        fn_7FD81AA0(view, 0);
        gameFrameEnd(0);
    }
    fn_7FC03CD8(3);
}

/* Callback de la pausa/fin de mision: decide si se reintenta la etapa. */
BOOL levelPauseCallback(u8 a, u8 abort) {
    Game* game;

    lbl_80095D34 = a;
    game = &gGame;
    if (abort) {
        game->unk21C = 1;
        return FALSE;
    }
    if (!fn_7FD69300() || (game->level != 1 && game->level != 12)) {
        game->unk21B = 0;
        fn_7FD52FD4();
        fn_7FD56D00();
        game->unk21C = 1;
    } else {
        game->unk21A = 1;
        return FALSE;
    }
    return TRUE;
}

/* Bucle de los menus: corre cuadros hasta que se pide salir. */
void frontendLoop(void) {
    Game* game = &gGame;
    u8* unk218;
    f32* dt;
    void* view;
    s32 unused;

    unk218 = &game->unk218;
    game->unk218 = 0;
    fn_7FC03CD8(0);
    fn_7FD4A3C0();
    view = game->view;
    dt = &game->deltaTime;
    while (!game->quit) {
        gameFrameBegin();
        fn_7FC03CE4();
        gameFrameUpdate();
        fn_7FD59A84();
        fn_7FC49440();
        fn_7FC498C0(*dt);
        fn_7FC48C38(lbl_7FDF056C, *dt);
        fn_7FC022C8(*dt, 1);
        if (!*unk218) {
            fn_7FD6A290(lbl_800962E8);
        }
        fn_7FC0C8E8();
        fn_7FC0C98C(view);
        fn_7FC0C994(0, *dt);
        fn_7FC0C994(0x20, *dt);
        fn_7FC0C994(0x22, *dt);
        fn_7FC0CA9C(*dt);
        gameFrameSetTime(lbl_80095D78);
        if (game->unk1C8 || !game->unk1CE) {
            fn_7FC1CDF8(*dt);
        }
        gameFrameRender();
        gameFramePresent();
        fn_7FC02260(0);
        fn_7FD47204();
        fn_7FC02260(1);
        gameFrameEnd(0);
    }
    fn_7FC03CD8(3);
}

void fn_7FC02260(s32 a) {
    fn_7FD6B144(lbl_800962E8, a);
}

/* Callback de gsysRegisterResource: guarda el handle (primero los niveles, al final "main"). */
void gameRegisterResourceCallback(u32 res) {
    if (lbl_80095D30 == 8) {
        gGame.mainRes = res;
    } else {
        gGame.levelRes[lbl_80095D30] = res;
    }
    lbl_80095D30--;
}

void fn_7FC022C8(f32 b, s32 a) {
}

/* Si queda menos memoria libre que al arrancar, avisa y vuelca el mapa de memoria. */
void gameCheckMemoryLeaks(u32 freeMem) {
    if (freeMem > fn_80007338()) {
        gsysDebugPrintf("--------- memory leak detected - dumping memory map ---------\n");
        fn_800074F8(0, 1);
        gsysDebugPrintf("--------- memory leak detected - dumping memory map ---------\n");
    }
}
