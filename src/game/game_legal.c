/*
 * Pantalla legal (licencias, logo Dolby) y bucles de espera
 * de carga. Va justo despues de game_level.c en el ejecutable.
 */
#include "vm_sections.h"
#include "game/game.h"
#include "dolphin/gx.h"

typedef struct LegalScreen {
    u8 unk00[0x8];
    f32 imageAlpha;      /* 0x08: opacidad de la imagen de carga */
    u8 unk0C[0x14];
    f32 delay;           /* 0x20: espera antes de empezar */
    f32 speed;           /* 0x24 */
    u8 loading;          /* 0x28: hay que cargar el paquete "legal" */
    u8 active;           /* 0x29: la pantalla legal se esta mostrando */
    u8 done;             /* 0x2A */
    u8 pad2B;
    void* font1;         /* 0x2C */
    void* font2;         /* 0x30 */
    void* text[8];       /* 0x34: textos de licencia */
    u8 dolby[0x5C];      /* 0x54: logo Dolby */
    f32 alpha;           /* 0xB0: fundido de la pantalla legal */
    f32 time;            /* 0xB4: tiempo restante en pantalla */
    f32 musicDelay;      /* 0xB8 */
} LegalScreen;

extern LegalScreen lbl_7FDE85A8;
#define gLegal lbl_7FDE85A8

extern void* lbl_80096200;
extern u8 lbl_7FE1C4A0[];

u8* fn_7FD59A84(void);
u8 fn_7FC03CE4(void);
void fn_7FC03CD8(s32 mode);
void fn_7FC0A630(void* cam);
void fn_7FC14C6C(void* view, void* cam, f32 aspect, f32 fov);
void fn_7FD462A0(void);
void fn_7FC2629C(void);
void fn_7FC09E30(LegalScreen* ls, s32 a, s32 b, f32 x, f32 y, f32 z);
u8 fn_7FD69300(void);
void fn_7FC09C7C(LegalScreen* ls, f32 dt);
void fn_7FD5762C(void* a);
void fn_7FD4CFDC(s32 ms);
void fn_7FD50AA8(f32 dt);
void fn_7FD5A600(void);
void fn_7FD47048(void);
void fn_7FD5A27C(const char* dir, const char* alias, s32 a);
void fn_7FD5A4A0(s32 a);
void fn_7FD33BDC(void* a);
void fn_7FCC9EA8(void* obj);
void fn_7FCCA630(void* obj, s32 a, f32 x, f32 y, f32 scale);
void fn_7FD3F324(f32 dt, s32 a, s32 b);
void fn_7FD43434(s32 alpha);
s32 fn_7FD5A458(const char* name);
void fn_7FD599E0(s32 a);
void* fn_800070C4(u32 size, s32 a);
void fn_80007270(void* heap);
void resLoadPackage(const char* dir, const char* alias, s32 a, s32 b);
void resUnloadPackage(const char* alias);
void* fn_7FD59280(const char* name, s32 size, s32 weight, s32 a, s32 b, s32 c);
void fn_7FD58DD8(const char* textId);
void* fn_7FD58E74(f32 x, f32 y);
void fn_7FD3EA1C(void* text, f32 a);
void fn_7FD3EA24(void* text, f32 a);
void fn_7FD3EA00(void* text, s32 idx, s32 r, s32 g, s32 b, s32 a);
void fn_7FD3E9F8(void* text, f32 a);
void fn_7FD3E9B8(void* text, s32 a);
void fn_7FD3E998(void* text, f32 x, f32 y);
void fn_7FCC9A64(void* obj, const char* name, s32 a);
void fn_7FD5A414(const char* name);
void fn_7FD594E8(void* font);
void fn_7FD3E1F8(void* text, s32 a);
void fn_7FCC9E20(void* obj);
void* fn_7FD471B4(s32* w, s32* h, s32* y, s32* x);
void fn_7FC26618(void);
f32 fn_7FD46244(void);
void fn_80009C70(void);
void fn_7FC23FA4(void);
void fn_7FC07C0C(f32 dt);
void fn_7FD43668(f32 a, f32 b, f32 c, f32 d);
void fn_7FD4536C(void);
void fn_7FD44ED4(void);
void fn_7FC08668(void);
void fn_80032D4C(void);
void fn_7FD44FB8(s32 a);
void fn_7FD48A44(void);
void fn_7FD45FF8(void);

void loadingFrameBegin(u8 loading);
void loadingFrameRender(u8 loading);
void loadingFrameEnd(u8 loading);
void legalDraw(void);
void legalLoad(void);
void legalUnload(void);
void loadingDrawImage(void);

/*
 * Espera a que termine una carga en segundo plano. Si hay que redibujar,
 * arma una camara fija y sigue dibujando cuadros de carga hasta el final.
 */
void loadingWaitFlag(void) {
    Game* game = &gGame;
    u8* flag;
    u8 cam[0x48];

    do {
        flag = fn_7FD59A84();
        if (fn_7FC03CE4()) {
            Game* g;
            fn_7FC0A630(cam);
            g = &gGame;
            fn_7FC14C6C(g->view, cam, 1.3333334f, 55.0f);
            fn_7FD462A0();
            do {
                flag = fn_7FD59A84();
                loadingFrameBegin(1);
                loadingFrameRender(1);
                loadingFrameEnd(1);
            } while (!*flag || g->unk218);
            return;
        }
        fn_7FC2629C();
    } while (!*flag || game->unk218);
}

/* Igual que loadingWaitFlag, pero espera a que un contador llegue a cero. */
void loadingWaitCounter(s32* counter) {
    Game* game = &gGame;
    u8 cam[0x44];

    while (*counter > 0 || game->unk218) {
        if (fn_7FC03CE4()) {
            fn_7FC0A630(cam);
            game = &gGame;
            fn_7FC14C6C(game->view, cam, 1.3333334f, 55.0f);
            fn_7FD462A0();
            while (*counter > 0 || game->unk218) {
                loadingFrameBegin(1);
                loadingFrameRender(1);
                loadingFrameEnd(1);
            }
            return;
        }
        fn_7FC2629C();
    }
}

/* Bucle de la pantalla de carga / pantalla legal del arranque. */
void loadingScreen(void) {
    LegalScreen* ls = &gLegal;
    f32* delay;
    f32* speed;
    f32* musicDelay;
    u8* done;
    f32* dt;
    u8* active;
    u8* loading;
    Game* game;
    u8* music;
    u8* flag;
    u8 cam[0x48];

    fn_7FC0A630(cam);
    fn_7FC14C6C(gGame.view, cam, 1.3333334f, 55.0f);
    fn_7FC09E30(ls, 0, 0, 255.0f, 0.0f, 1.0f);
    fn_7FD462A0();
    fn_7FC03CD8(1);
    done = &ls->done;
    ls->done = 0;
    delay = &ls->delay;
    speed = &ls->speed;
    ls->delay = 0.0f;
    ls->speed = 0.4f;
    if (fn_7FD69300()) {
        *delay = 1.0f;
        *speed = 0.25f;
    }
    musicDelay = &ls->musicDelay;
    ls->musicDelay = 0.0f;
    if (gGame.state == 5 && gGame.unk258 == 0x16) {
        *musicDelay = 2.5f;
    }
    game = &gGame;
    dt = &game->deltaTime;
    music = lbl_7FE1C4A0;
    loading = &ls->loading;
    active = &ls->active;

    do {
        flag = fn_7FD59A84();
        if (*loading) {
            legalLoad();
        }
        loadingFrameBegin(0);
        if (*delay > 0.0f) {
            *delay -= game->deltaTime;
        } else {
            fn_7FC09C7C(ls, game->deltaTime * *speed);
        }
        if (*active && ls->time < 0.0f && 0.0f == ls->alpha) {
            legalUnload();
            fn_7FD5762C(music);
            *done = 1;
        }
        if (0.0f != *musicDelay) {
            *musicDelay -= *dt;
            if (*musicDelay <= 0.0f) {
                *musicDelay = 0.0f;
                fn_7FD4CFDC(2000);
            }
            fn_7FD50AA8(*dt);
        }
        loadingFrameRender(0);
        loadingDrawImage();
        if (*active) {
            legalDraw();
        }
        loadingFrameEnd(0);
    } while (!*flag || game->unk218 || *loading || *active);

    if (!*done) {
        fn_7FD5A600();
    }
    fn_7FD47048();
    fn_7FC03CD8(3);
}

/* Pide mostrar la pantalla legal: carga el paquete "legal" en segundo plano. */
void legalScreen(void) {
    fn_7FD5A27C("legal", "legal", 0);
    gLegal.loading = 1;
    gLegal.active = 0;
    gGame.unk258 = 0x16;
    fn_7FD5A4A0(3);
}

/* Dibuja la pantalla legal con su fundido de entrada y salida. */
void legalDraw(void) {
    LegalScreen* ls = &gLegal;
    f32* time;
    f32* dt;
    f32* alpha;
    f32 d;
    f32 v;
    f32 step;
    u16* width;
    u16* height;
    void* dolby;
    Mtx44 proj;

    time = &ls->time;
    dt = &gGame.deltaTime;
    d = gGame.deltaTime;
    ls->time -= d;
    if (ls->time > 0.0f) {
        alpha = &ls->alpha;
        if (ls->alpha < 1.0f) {
            v = ls->alpha + (step = 2.0f * d);
            *alpha = v;
            if (v > 1.0f) {
                *alpha = 1.0f;
            }
        }
    }
    if (*time < 0.0f) {
        alpha = &ls->alpha;
        if (ls->alpha > 0.0f) {
            v = ls->alpha - (step = 2.0f * d);
            *alpha = v;
            if (v < 0.0f) {
                *alpha = 0.0f;
            }
        }
    }
    if (lbl_80096200 != NULL) {
        fn_7FD33BDC(lbl_80096200);
    }
    height = &gGame.screenHeight;
    width = &gGame.screenWidth;
    C_MTXOrtho(proj, 0.0f, *height, 0.0f, *width, 0.0f, 1.0f);
    GXSetProjection(proj, 1);
    GXSetViewport(0.0f, 0.0f, *width, *height, 0.0f, 1.0f);
    GXSetScissor(0, 0, *width, *height);
    dolby = ls->dolby;
    fn_7FCC9EA8(dolby);
    fn_7FCCA630(dolby, 0, 273.0f, 310.0f, 1.0f);
    fn_7FD3F324(*dt, 0, 0);
    fn_7FD43434((s32)(255.0f * ls->alpha));
}

/* Carga el paquete "legal": fuentes, textos de licencia y el logo Dolby. */
void legalLoad(void) {
    LegalScreen* ls = &gLegal;
    void* heap;
    void** text;
    void* t;
    s32 i;

    if (fn_7FD5A458("legal") > 0) {
        fn_7FD599E0(1);
        heap = fn_800070C4(0x1000000, 0);
        resLoadPackage("legal", "legal", 0, 0);
        ls->font1 = fn_7FD59280("FranklinGothicNo2", 0x18, 400, 0, 0, 0);
        ls->font2 = fn_7FD59280("FranklinGotCdITC", 0x18, 400, 0, 0, 0);
        fn_7FD58DD8("FrntEnd/License/License1a");
        t = fn_7FD58E74(188.0f, 68.5f);
        text = &ls->text[0];
        ls->text[0] = t;
        fn_7FD3EA1C(ls->text[0], 0.57f);
        fn_7FD3EA24(*text, 1.3f);
        fn_7FD58DD8("FrntEnd/License/License1b");
        ls->text[1] = fn_7FD58E74(388.0f, 70.0f);
        fn_7FD58DD8("FrntEnd/License/License1c");
        ls->text[2] = fn_7FD58E74(320.0f, 94.0f);
        fn_7FD58DD8("FrntEnd/License/License2");
        ls->text[3] = fn_7FD58E74(320.0f, 160.0f);
        fn_7FD58DD8("FrntEnd/License/License3");
        ls->text[4] = fn_7FD58E74(320.0f, 225.0f);
        fn_7FD58DD8("FrntEnd/License/License4");
        ls->text[5] = fn_7FD58E74(320.0f, 290.0f);
        fn_7FD58DD8("FrntEnd/License/License5");
        t = fn_7FD58E74(320.0f, 380.0f);
        text = &ls->text[6];
        ls->text[6] = t;
        fn_7FD3EA00(ls->text[6], 1, 0xFF, 0xFF, 0xFF, 0xFF);
        fn_7FD3EA00(*text, 2, 0xFF, 0x36, 0, 0xFF);
        fn_7FD58DD8("FrntEnd_License_License6");
        ls->text[7] = fn_7FD58E74(320.0f, 420.0f);
        for (i = 0; i < 8; i++) {
            fn_7FD3E9F8(ls->text[i], 1.0f);
            fn_7FD3E9B8(ls->text[i], 1);
            fn_7FD3E998(ls->text[i], 0.0f, 0.0f);
            fn_7FD3EA00(ls->text[i], 0, 0xFF, 0xFF, 0xFF, 0xFF);
        }
        fn_7FCC9A64(ls->dolby, "dolby", 1);
        ls->alpha = 0.0f;
        ls->time = 5.0f;
        ls->loading = 0;
        ls->active = 1;
        fn_80007270(heap);
        fn_7FD5A414("legal");
        fn_7FD599E0(0);
    }
}

/* Libera todo lo que cargo legalLoad. */
void legalUnload(void) {
    LegalScreen* ls = &gLegal;
    void** font1;
    s32 i;

    fn_7FD594E8(ls->font2);
    font1 = &ls->font1;
    fn_7FD594E8(ls->font1);
    *font1 = NULL;
    for (i = 0; i < 8; i++) {
        fn_7FD3E1F8(ls->text[i], 1);
    }
    fn_7FCC9E20(ls->dolby);
    resUnloadPackage("legal");
    ls->loading = 0;
    ls->active = 0;
}

/* Dibuja la imagen de la pantalla de carga como un cuadrado texturizado. */
void loadingDrawImage(void) {
    f32* imageAlpha;
    void* tex;
    u16* width;
    u16* height;
    s32 w;
    s32 h;
    s32 y;
    s32 x;
    s32 x0;
    s32 y0;
    s32 x1;
    s32 y1;
    Mtx44 proj;

    imageAlpha = &gLegal.imageAlpha;
    if (gLegal.imageAlpha <= 0.0f) {
        return;
    }
    tex = fn_7FD471B4(&w, &h, &y, &x);
    if (tex == NULL) {
        return;
    }
    if (lbl_80096200 != NULL) {
        fn_7FD33BDC(lbl_80096200);
    }
    height = &gGame.screenHeight;
    width = &gGame.screenWidth;
    C_MTXOrtho(proj, 0.0f, *height, 0.0f, *width, 0.0f, 1.0f);
    GXSetProjection(proj, 1);
    GXSetViewport(0.0f, 0.0f, *width, *height, 0.0f, 1.0f);
    GXSetScissor(0, 0, *width, *height);
    GXSetNumChans(0);
    GXLoadTexObj(tex, 0);
    GXSetNumTexGens(1);
    GXSetTexCoordGen2(0, 1, 4, 0x3C, 0, 0x7D);
    GXSetNumTevStages(1);
    GXSetTevOrder(0, 0, 0, 0xFF);
    GXSetTevColorIn(0, 0xF, 0xC, 8, 0xF);
    GXSetTevColorOp(0, 0, 0, 0, 1, 0);
    GXSetTevAlphaIn(0, 7, 7, 7, 7);
    GXSetTevAlphaOp(0, 0, 0, 0, 1, 0);
    GXSetAlphaCompare(7, 0, 0, 7, 0);
    GXSetBlendMode(0, 1, 0, 0);
    GXSetZMode(0, 7, 0);
    GXSetZCompLoc(1);
    GXSetCullMode(0);
    GXSetCurrentMtx(0x3C);
    GXClearVtxDesc();
    GXSetVtxDesc(9, 1);
    GXSetVtxDesc(0xD, 1);
    GXSetVtxAttrFmt(0, 9, 0, 4, 0);
    GXSetVtxAttrFmt(0, 0xD, 1, 0, 0);
    GXSetClipMode(0);
    GXBegin(0x80, 0, 4);
    x0 = x;
    y0 = y;
    x1 = x0 + w;
    y1 = y0 + h;
    GXPosition2f32(x0, y0);
    GXTexCoord2u8(0, 0);
    GXPosition2f32(x1, y0);
    GXTexCoord2u8(1, 0);
    GXPosition2f32(x1, y1);
    GXTexCoord2u8(1, 1);
    GXPosition2f32(x0, y1);
    GXTexCoord2u8(0, 1);
    GXEnd();
    fn_7FD43434((s32)*imageAlpha);
}

/* Comienzo de un cuadro durante la carga: tiempo, entrada y audio. */
void loadingFrameBegin(u8 loading) {
    f32* dt;

    fn_7FC26618();
    gGame.deltaTime = fn_7FD46244();
    dt = &gGame.deltaTime;
    fn_80009C70();
    fn_7FC23FA4();
    fn_7FC07C0C(*dt);
    fn_7FC03CE4();
}

/* Prepara el dibujado de un cuadro de carga. */
void loadingFrameRender(u8 loading) {
    Game* game = &gGame;

    fn_7FD43668(*(f32*)(game->unk078 + 0xC0), *(f32*)(game->unk078 + 0xB8),
                *(f32*)(game->unk078 + 0xB0), *(f32*)(game->unk078 + 0xB4));
    fn_7FD4536C();
    fn_7FD44ED4();
    fn_7FC08668();
    fn_80032D4C();
    fn_7FD44FB8(0);
}

/* Cierra un cuadro de carga y lo muestra. */
void loadingFrameEnd(u8 loading) {
    if (!loading) {
        fn_7FD48A44();
    }
    fn_7FD45FF8();
}
