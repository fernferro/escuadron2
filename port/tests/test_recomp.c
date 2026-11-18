/*
 * Pruebas del codigo recompilado: corre funciones del juego (de la biblioteca
 * de C de CodeWarrior incluida en el ejecutable) y compara con el resultado esperado.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "runtime.h"
#include "funcs.h"

int dol_load(const char* path, uint32_t* entry);

static PPCContext ctx;
static int fails, total;
#define SCRATCH 0x81500000u

static void put_str(uint32_t a, const char* s) { memcpy(mem_ptr(a), s, strlen(s) + 1); }
static const char* get_str(uint32_t a) { return (const char*)mem_ptr(a); }
static void check(int ok, const char* what) {
    total++;
    if (!ok) fails++;
    printf("%s %s\n", ok ? "OK   " : "FALLA", what);
}

int main(int argc, char** argv) {
    uint32_t entry;
    const char* dol = argc > 1 ? argv[1] : "orig/GSWE64/combined.dol";
    mem_init();
    if (!dol_load(dol, &entry)) { printf("no pude cargar %s\n", dol); return 1; }
    memset(&ctx, 0, sizeof ctx);
    f_800031F0(&ctx); /* __init_registers: r1, r2 (SDA2) y r13 (SDA) */
    printf("r1=%08X r2=%08X r13=%08X\n", ctx.r[1], ctx.r[2], ctx.r[13]);

    /* strlen */
    put_str(SCRATCH, "Hola, mundo!");
    ctx.r[3] = SCRATCH;
    f_800531C8(&ctx);
    check(ctx.r[3] == 12, "strlen(\"Hola, mundo!\") == 12");

    /* memset + memcpy */
    ctx.r[3] = SCRATCH + 0x100; ctx.r[4] = 0x5A; ctx.r[5] = 37;
    f_80005298(&ctx);
    ctx.r[3] = SCRATCH + 0x200; ctx.r[4] = SCRATCH + 0x100; ctx.r[5] = 37;
    f_8000538C(&ctx);
    {
        int ok = 1, i;
        for (i = 0; i < 37; i++) ok &= R8(SCRATCH + 0x200 + i) == 0x5A;
        ok &= R8(SCRATCH + 0x200 + 37) == 0;
        check(ok, "memset(37) + memcpy(37)");
    }

    /* sprintf con enteros, textos y hexadecimal */
    put_str(SCRATCH + 0x300, "l%02d/%02d [%s] %x %u %-5d|");
    put_str(SCRATCH + 0x380, "abc");
    ctx.r[3] = SCRATCH + 0x400; ctx.r[4] = SCRATCH + 0x300;
    ctx.r[5] = 3; ctx.r[6] = 12; ctx.r[7] = SCRATCH + 0x380; ctx.r[8] = 0xBEEF; ctx.r[9] = 4000000000u; ctx.r[10] = (uint32_t)-42;
    cr_setbit(&ctx, 6, 0); /* sin argumentos de punto flotante */
    f_80051498(&ctx);
    printf("      -> \"%s\" (devolvio %d)\n", get_str(SCRATCH + 0x400), (int)ctx.r[3]);
    check(strcmp(get_str(SCRATCH + 0x400), "l03/12 [abc] beef 4000000000 -42  |") == 0, "sprintf enteros y textos");

    /* sprintf con punto flotante */
    put_str(SCRATCH + 0x300, "%5.2f %e %g");
    ctx.r[3] = SCRATCH + 0x400; ctx.r[4] = SCRATCH + 0x300;
    ctx.f[1] = 3.14159; ctx.f[2] = -1234.5; ctx.f[3] = 0.000125;
    cr_setbit(&ctx, 6, 1);
    f_80051498(&ctx);
    printf("      -> \"%s\"\n", get_str(SCRATCH + 0x400));
    check(strcmp(get_str(SCRATCH + 0x400), " 3.14 -1.234500e+03 0.000125") == 0, "sprintf punto flotante");

    /* division y modulo de 64 bits (__div2u / __mod2u) */
    {
        uint64_t a = 0x123456789ABCDEFull, b = 0x12345ull;
        ctx.r[3] = (uint32_t)(a >> 32); ctx.r[4] = (uint32_t)a; ctx.r[5] = (uint32_t)(b >> 32); ctx.r[6] = (uint32_t)b;
        f_8004F638(&ctx);
        check((((uint64_t)ctx.r[3] << 32) | ctx.r[4]) == a / b, "__div2u 64 bits");
        ctx.r[3] = (uint32_t)(a >> 32); ctx.r[4] = (uint32_t)a; ctx.r[5] = (uint32_t)(b >> 32); ctx.r[6] = (uint32_t)b;
        f_8004F85C(&ctx);
        check((((uint64_t)ctx.r[3] << 32) | ctx.r[4]) == a % b, "__mod2u 64 bits");
    }

    printf("\n%d/%d pruebas pasaron\n", total - fails, total);
    return fails != 0;
}
