#include "vm_sections.h"
#include "game/game.h"
#include "gsys/gsys.h"

extern s32 lbl_80095E30;
extern u8 lbl_7FDEFE14[]; /* posicion inicial del jugador */
extern u32 lbl_80095D38;  /* memoria libre tras cargar los datos compartidos */
extern u8 lbl_7FDF6310[];
extern u8 lbl_7FDE8294[];
extern u8 lbl_7FDE82A4[];
extern f32 lbl_7FDE84A4[64]; /* tiempo restante de cada temporizador */
extern u64 lbl_80095D20;  /* temporizadores activos (un bit por temporizador) */
extern u64 lbl_80095D28;  /* temporizadores vencidos */
extern u8 lbl_7FDF056C[];
extern s32 lbl_80095F70;
extern u8 lbl_7FDF04F0[];
extern u8 lbl_7FDF80B0[];

s32 sprintf(char* buf, const char* fmt, ...);
u32 fn_80007338(void);
void fn_80007450(void* ptr, u32 size);
void resLoadPackage(const char* dir, const char* alias, s32 a, s32 b);
void fn_7FD599E0(s32 a);
void fn_7FC73908(void);
void fn_7FC6D5D0(void);
void fn_7FC6FDCC(void);
void fn_7FC49440(void);
void fn_7FC4948C(void);
void fn_7FC2F528(s32* a, s32 b);
void fn_7FC1CC4C(void);
void fn_7FD4965C(void);
void fn_7FC38B3C(void);
void fn_7FD5A600(void);
void fn_7FC436B8(void* a);
void loadingScreen(void);
void fn_7FC1DED4(s32 level);
void fn_7FC1FFF8(void);
void fn_7FC7481C(void);
void fn_7FCCA824(void* a);
void fn_7FC96B78(void);
void fn_7FCEEA00(void);
void fn_7FD29870(s32 level, s32 stage);
void fn_7FCC9590(void);
void fn_7FD8955C(void);
void fn_7FD8A5D8(void);
void fn_7FC9A8BC(void);
void fn_7FC9FCB4(void);
void fn_7FCA2E08(s32* a);
void fn_7FCA58E0(void);
void fn_7FC43C28(void* a);
void fn_7FC464B0(void* a);
void fn_7FCE52E8(s32 a);
void fn_7FCE931C(void* a);
void fn_7FD8C8FC(void);
void fn_7FD52870(void);
void fn_7FCD9284(void);
u8* fn_7FC1E900(const char* name);
void resUnloadPackage(const char* alias);
void fn_7FC0DD0C(void);
void fn_7FC6FD54(void);
void fn_7FD52A2C(void);
void fn_7FCE5358(void);
void fn_7FCA60D0(void);
void fn_7FC9D508(void);
void fn_7FCA2C78(void);
void fn_7FCA2EF0(s32* a);
void fn_7FD8A908(void);
void fn_7FD89784(void);
void fn_7FCC967C(void);
void fn_7FCCABD4(void* a);
void fn_7FC2F880(s32* a);
void fn_7FC440BC(void* a);
void fn_7FC4653C(void* a);
void fn_7FD29CF0(void);
void fn_7FC1E6F4(void);
void fn_7FD4A46C(void);
void fn_7FC1CD48(void);
void fn_7FC6D5FC(void);
void fn_7FC73A40(void);
void fn_7FC45568(void* a);
void fn_7FC73D74(void);
void fn_7FC14844(void);
void fn_7FD4B144(void);
void fn_7FC43A24(void* a);
void fn_800074F8(s32 a, s32 b);
void fn_7FC73A1C(void);
void fn_7FC46540(void* a);
void fn_7FD443B8(s32 a);
void fn_7FD4B0F4(void);
void fn_7FC44200(void* a);
void fn_7FC9B38C(void);
void fn_7FC4610C(void* a, s32 level);
void fn_7FC265EC(void);
void fn_7FC26618(void);
f32 fn_7FD46244(void);
void fn_80009C70(void);
void fn_7FC23FA4(void);
void fn_7FC07C0C(f32 dt);
void fn_7FC1CED8(void* view);
void fn_7FD29D28(void* a, void* b);
void fn_7FD52054(void);
void fn_7FD50AA8(f32 t);
void fn_7FC153C0(void);
void fn_7FD4536C(void);
void fn_7FD44ED4(void);
void fn_7FD29D58(void* a, void* b);
void fn_7FC118F4(void* view);
void fn_7FC0C98C(void* view);
void fn_7FC08668(void);
void fn_7FC11964(void);
void fn_80032D4C(void);
void fn_7FD44FB8(s32 a);
void fn_7FC085EC(f32 a);
void fn_7FC119D4(void);
void fn_7FCDA380(void);
void fn_7FD45FF8(void);
void fn_8003C448(void);
extern f32 lbl_80095D7C;
void fn_7FC94678(void* a, s32 b, void* c, void* d);
void fn_7FC96AB8(void* a, s32 b, s32 c);

s64 fn_8002F200(void);
void fn_7FC24218(u32 seed);
void fn_7FC265B4(void);
void fn_7FC07AF0(void);
void fn_7FC0C2C8(s32 a, s32 b);
void fn_7FCA73F4(void);
s32 fn_7FC0824C(void);
s32 fn_7FC08198(void);
void fn_7FC0FA80(s32 a, s32 b);
void fn_7FC98C5C(void);
void fn_7FC487D0(void* a);
void fn_7FC48804(void* a);
void fn_7FC98C84(void);
void fn_7FC1032C(void);
void fn_7FC0FD18(void);
void fn_7FC0C894(void);
void fn_7FCA7474(void);
void fn_7FC07C08(void);
void fn_7FC265E4(void);
void fn_7FC0840C(void);
void fn_7FCC9560(void);
void fn_7FC0A630(void* a);

void fn_7FC0232C(void) {
}

void fn_7FC02330(void) {
}

/* Comienzo de una sesion de juego (al entrar a un nivel, hangar, etc.). */
void gameSessionBegin(s32 arg) {
    Game* game = &gGame;
    s32 count;
    s32 b;

    game->unk284 = arg;
    fn_7FC24218(fn_8002F200());
    fn_7FC265B4();
    fn_7FC07AF0();
    switch (game->level) {
    case 0x16:
        count = 1000;
        break;
    default:
        count = 512;
        break;
    }
    fn_7FC0C2C8(count, count * 10);
    fn_7FCA73F4();
    b = fn_7FC0824C();
    fn_7FC0FA80(fn_7FC08198(), b);
    fn_7FC98C5C();
    fn_7FC487D0(lbl_7FDF056C);
}

/* Fin de una sesion de juego. */
void gameSessionEnd(void) {
    fn_7FC48804(lbl_7FDF056C);
    fn_7FC98C84();
    fn_7FC1032C();
    fn_7FC0FD18();
    fn_7FC0C894();
    fn_7FCA7474();
    fn_7FC07C08();
    fn_7FC265E4();
}

void fn_7FC02408(void) {
    fn_7FC0840C();
    fn_7FCC9560();
    fn_7FC0A630(gGame.unk1D4);
}

/* Descuenta el tiempo del cuadro a los temporizadores activos y marca los vencidos. */
void gameUpdateTimers(void) {
    f32 dt = gGame.deltaTime;
    f32* timer;
    u64 mask;
    s32 i;

    timer = lbl_7FDE84A4;
    i = 0;
    mask = lbl_80095D20;
    for (; mask != 0; mask >>= 1, timer++, i++) {
        if (mask & 1) {
            *timer -= dt;
            if (*timer <= 0.0f) {
                lbl_80095D28 |= (u64)1 << i;
                lbl_80095D20 &= ~((u64)1 << i);
            }
        }
    }
}

/*
 * Busca el objeto "player_StartPosition" y reinicia los componentes que
 * tenga enlazados (los campos 0x68..0x74 son desplazamientos desde el objeto).
 */
void levelResetPlayerStart(void) {
    u8* obj = fn_7FC1E900("player_StartPosition");

    if (*(u32*)(obj + 0x68) != 0) {
        fn_7FC94678(obj + *(u32*)(obj + 0x68), 0, obj, obj);
    }
    if (*(u32*)(obj + 0x6C) != 0) {
        fn_7FC96AB8(obj + *(u32*)(obj + 0x6C), 0, 0);
    }
    if (*(u32*)(obj + 0x70) != 0) {
        fn_7FC96AB8(obj + *(u32*)(obj + 0x70), 0, 0);
    }
    if (*(u32*)(obj + 0x74) != 0) {
        fn_7FC96AB8(obj + *(u32*)(obj + 0x74), 0, 0);
    }
}

/*
 * Carga un nivel: datos comunes, la parte compartida del nivel ("lNN/00"),
 * la parte propia de la etapa ("lNN/<etapa+1>") y despues inicializa
 * terreno, cielo, radar, dialogos y la posicion del jugador.
 */
void levelLoad(void) {
    s32* level;
    s32* stage;
    char dir[128];
    char alias[16];

    resLoadPackage("staticGame/us", "staticGame_us", 1, 0);
    fn_7FD599E0(1);
    fn_7FC73908();
    fn_7FC6D5D0();
    fn_7FC6FDCC();
    fn_7FC49440();
    fn_7FC4948C();
    fn_7FC2F528(&lbl_80095E30, 0xF);
    fn_7FC1CC4C();
    fn_7FD4965C();
    fn_7FC38B3C();

    /* datos compartidos por todas las etapas del nivel */
    level = &gGame.level;
    sprintf(dir, "l%02d/%02d", *level, 0);
    sprintf(alias, "l%02d.%02d", *level, 0);
    resLoadPackage(dir, alias, 3, 0);
    fn_7FD5A600();
    fn_7FC436B8(lbl_7FDEFE14);
    fn_7FD599E0(0);
    loadingScreen();
    fn_7FD599E0(1);
    gsysDebugPrintf("--------- shared data loaded %d bytes free ---------\n", fn_80007338());
    lbl_80095D38 = fn_80007338();

    /* datos propios de la etapa */
    stage = &gGame.stage;
    sprintf(dir, "l%02d/%02d", *level, *stage + 1);
    sprintf(alias, "l%02d.%02d", *level, *stage + 1);
    resLoadPackage(dir, alias, 3, 0);
    fn_7FD599E0(0);
    loadingScreen();

    fn_7FC1DED4(*level);
    fn_7FC1FFF8();
    fn_7FC7481C();
    fn_7FCCA824(lbl_7FDF6310);
    fn_7FC96B78();
    fn_7FCEEA00();
    fn_80007450(lbl_7FDE8294, 0x10);
    fn_80007450(lbl_7FDE82A4, 0x200);
    fn_80007450(lbl_7FDE84A4, 0x100);
    lbl_80095D28 = 0;
    lbl_80095D20 = 0;
    fn_7FD29870(*level, *stage);
    fn_7FCC9590();
    fn_7FD8955C();
    fn_7FD8A5D8();
    fn_7FC9A8BC();
    fn_7FC9FCB4();
    fn_7FCA2E08(&lbl_80095F70);
    fn_7FCA58E0();
    fn_7FC43C28(lbl_7FDEFE14);
    fn_7FC464B0(lbl_7FDF04F0);
    fn_7FCE52E8(0);
    fn_7FCE931C(lbl_7FDF80B0);
    fn_7FD8C8FC();
    fn_7FD52870();
    fn_7FCD9284();
}

/* Descarga el nivel completo: etapa actual, datos compartidos y datos comunes. */
void levelUnload(void) {
    s32* level;
    char alias[16];

    fn_7FC0DD0C();
    fn_7FC6FD54();
    fn_7FD52A2C();
    fn_7FCE5358();
    fn_7FCA60D0();
    fn_7FC9D508();
    fn_7FCA2C78();
    fn_7FCA2EF0(&lbl_80095F70);
    fn_7FD8A908();
    fn_7FD89784();
    fn_7FCC967C();
    fn_7FCCABD4(lbl_7FDF6310);
    fn_7FC2F880(&lbl_80095E30);
    fn_7FC440BC(lbl_7FDEFE14);
    fn_7FC4653C(lbl_7FDF04F0);
    fn_7FD29CF0();
    fn_7FC1E6F4();
    level = &gGame.level;
    sprintf(alias, "l%02d.%02d", gGame.level, gGame.stage + 1);
    resUnloadPackage(alias);
    sprintf(alias, "l%02d.%02d", *level, 0);
    resUnloadPackage(alias);
    fn_7FD4A46C();
    fn_7FC1CD48();
    fn_7FC6D5FC();
    fn_7FC73A40();
    resUnloadPackage("staticGame_us");
}

/*
 * Pasa a otra etapa del mismo nivel: libera los datos de la etapa actual,
 * verifica que no haya fugas de memoria y carga gGame.nextStage.
 */
void levelNextStage(void) {
    s32* level;
    s32* stage;
    char dir[128];
    char alias[16];

    fn_7FC45568(lbl_7FDEFE14);
    fn_7FC0DD0C();
    fn_7FC6FD54();
    fn_7FC73D74();
    fn_7FD52A2C();
    fn_7FCA60D0();
    fn_7FC9D508();
    fn_7FCA2C78();
    fn_7FCA2EF0(&lbl_80095F70);
    fn_7FD8A908();
    fn_7FD89784();
    fn_7FC14844();
    fn_7FCC967C();
    fn_7FCCABD4(lbl_7FDF6310);
    fn_7FD4B144();
    fn_7FC43A24(lbl_7FDEFE14);
    fn_7FD29CF0();
    fn_7FC1E6F4();

    stage = &gGame.stage;
    level = &gGame.level;
    sprintf(alias, "l%02d.%02d", gGame.level, gGame.stage + 1);
    resUnloadPackage(alias);
    fn_7FCE5358();
    gsysDebugPrintf("--------- unique data unloaded %d bytes free ---------\n", fn_80007338());
    if (lbl_80095D38 != fn_80007338()) {
        gsysDebugPrintf("--------- memory leak detected - dumping memory map ---------\n");
        fn_800074F8(0, 3);
        gsysDebugPrintf("--------- memory leak detected - dumping memory map ---------\n");
    }

    *stage = gGame.nextStage;
    fn_7FCCA824(lbl_7FDF6310);
    fn_7FC96B78();
    fn_80007450(lbl_7FDE8294, 0x10);
    fn_80007450(lbl_7FDE82A4, 0x200);
    fn_80007450(lbl_7FDE84A4, 0x100);
    lbl_80095D28 = 0;
    lbl_80095D20 = 0;
    fn_7FCEEA00();
    fn_7FC6FDCC();
    sprintf(dir, "l%02d/%02d", *level, *stage + 1);
    sprintf(alias, "l%02d.%02d", *level, *stage + 1);
    resLoadPackage(dir, alias, 3, 0);
    loadingScreen();
    fn_7FC1DED4(*level);
    fn_7FC7481C();
    fn_7FD29870(*level, *stage);
    fn_7FCC9590();
    fn_7FD8955C();
    fn_7FD8A5D8();
    fn_7FC9A8BC();
    fn_7FC9FCB4();
    fn_7FCA2EF0(&lbl_80095F70);
    fn_7FCA58E0();
    fn_7FC73A1C();
    fn_7FC43C28(lbl_7FDEFE14);
    fn_7FC46540(lbl_7FDF04F0);
    fn_7FCE52E8(0);
    fn_7FD8C8FC();
    fn_7FD52870();
    gGame.unk1C8 = 0;
    gGame.unk1C9 = 0;
    gGame.unk1CA = 0;
    gGame.unk1CE = 0;
    gGame.unk1D0 = 0;
    gGame.unk1D2 = 0;
    gGame.unk1CC = 0;
    fn_7FD443B8(0);
    fn_7FD4B0F4();
}

/* Reinicia el nivel actual (por ejemplo al reintentar una mision). */
void levelRestart(void) {
    fn_7FC44200(lbl_7FDEFE14);
    fn_7FC73D74();
    fn_7FC0DD0C();
    fn_7FC6FD54();
    fn_7FCEEA00();
    fn_7FC9B38C();
    fn_7FC4610C(lbl_7FDEFE14, gGame.level);
    fn_7FD8C8FC();
}

/* Datos fijos usados fuera de los niveles (menus, briefing). */
void staticGameLoad(void) {
    resLoadPackage("staticGame/us", "staticGame_us", 1, 0);
    fn_7FC73908();
    fn_7FC6D5D0();
    fn_7FC6FDCC();
    fn_7FD29870(gGame.level, gGame.stage);
    fn_7FC1CC4C();
}

void staticGameUnload(void) {
    fn_7FC0DD0C();
    fn_7FD4A46C();
    fn_7FD29CF0();
    fn_7FC1CD48();
    fn_7FC6D5FC();
    fn_7FC73A40();
    resUnloadPackage("staticGame_us");
}

void gameResetTimers(void) {
    fn_7FC265EC();
    gGame.totalTime = 0.0f;
    gGame.frameCount = 10;
    gGame.timeScale = 1.0f;
    gGame.unk1CF = 0;
    lbl_80095D7C = 0.016683351f; /* 1/59.94: un cuadro NTSC */
}

/* Inicio de cuadro: mide el tiempo transcurrido. */
void gameFrameBegin(void) {
    fn_7FC26618();
    gGame.deltaTime = fn_7FD46244();
    fn_80009C70();
}

/* Actualiza el mundo y la camara. */
void gameFrameUpdate(void) {
    Game* game;

    fn_7FC23FA4();
    game = &gGame;
    fn_7FC07C0C(game->deltaTime);
    fn_7FC1CED8(game->view);
    fn_7FD29D28(game->unk078, game->unk028);
}

void gameFrameSetTime(f32 t) {
    fn_7FD52054();
    fn_7FD50AA8(t);
}

/* Dibuja la escena. */
void gameFrameRender(void) {
    Game* game;
    void* view;

    fn_7FC153C0();
    game = &gGame;
    view = game->view;
    fn_7FC1CED8(view);
    fn_7FD4536C();
    fn_7FD44ED4();
    fn_7FD29D58(game->unk078, game->unk028);
    fn_7FC118F4(view);
    fn_7FC0C98C(view);
    fn_7FC08668();
    fn_7FC11964();
}

/* Presenta la imagen en pantalla. */
void gameFramePresent(void) {
    fn_80032D4C();
    fn_7FD44FB8(1);
    fn_7FC085EC(1.0f);
}

/* Fin de cuadro: acumula tiempo y cuenta cuadros. */
void gameFrameEnd(u8 flag) {
    Game* game;

    fn_7FC119D4();
    if (flag) {
        fn_7FCDA380();
    }
    fn_7FD45FF8();
    game = &gGame;
    game->totalTime = game->totalTime + game->deltaTime;
    lbl_80095D7C = game->deltaTime;
    game->frameCount++;
}

BOOL gameWaitWhile(s32 busy) {
    while (busy != 0) {
        fn_8003C448();
    }
    return TRUE;
}
