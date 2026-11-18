/* Servicios basicos del runtime: llamadas indirectas, errores, reloj. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "runtime.h"

typedef struct { uint32_t addr; PPCFunc fn; } FuncEntry;
extern const FuncEntry g_func_table[];
extern const unsigned g_func_count;
extern const char* const g_func_names[];

FILE* rt_log_file;                 /* copia del registro (en Windows: escuadron2_log.txt) */
void (*rt_fatal_hook)(const char* msg);   /* la ventana muestra el error en un cuadro */

void rt_log(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fflush(stderr);
    if (rt_log_file) {
        va_start(ap, fmt);
        vfprintf(rt_log_file, fmt, ap);
        va_end(ap);
        fflush(rt_log_file);
    }
}

void rt_fatal(const char* fmt, ...) {
    char msg[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    fprintf(stderr, "FATAL: %s", msg);
    fflush(stderr);
    if (rt_log_file) { fprintf(rt_log_file, "FATAL: %s", msg); fflush(rt_log_file); }
    if (rt_fatal_hook) rt_fatal_hook(msg);
    exit(1);
}

static int find(uint32_t addr) {
    unsigned lo = 0, hi = g_func_count;
    while (lo < hi) {
        unsigned mid = (lo + hi) / 2;
        if (g_func_table[mid].addr < addr) lo = mid + 1;
        else hi = mid;
    }
    return (lo < g_func_count && g_func_table[lo].addr == addr) ? (int)lo : -1;
}

PPCFunc ppc_lookup(uint32_t addr) {
    int i = find(addr);
    return i < 0 ? NULL : g_func_table[i].fn;
}

const char* rt_func_name(uint32_t addr, uint32_t* start) {
    unsigned lo = 0, hi = g_func_count;
    while (lo < hi) {
        unsigned mid = (lo + hi) / 2;
        if (g_func_table[mid].addr <= addr) lo = mid + 1;
        else hi = mid;
    }
    if (lo == 0) return "?";
    if (start) *start = g_func_table[lo - 1].addr;
    return g_func_names[lo - 1];
}

void call_indirect(PPCContext* ctx, uint32_t addr) {
    /* cache por hilo (las llamadas indirectas son muchas: tablas virtuales de C++) */
    static __thread struct { uint32_t addr; PPCFunc fn; } cache[1024];
    unsigned h = (addr >> 2) & 1023;
    PPCFunc fn;
    if (cache[h].addr == addr && cache[h].fn) { cache[h].fn(ctx); return; }
    fn = ppc_lookup(addr);
    if (fn) { cache[h].addr = addr; cache[h].fn = fn; }
    if (!fn) {
        /* Codigo que no esta recompilado (cargado del disco en tiempo de ejecucion). */
        if ((addr & 3) == 0 && mem_ptr(addr)) { interp_call(ctx, addr); return; }
        {
            uint32_t s = 0;
            const char* n = rt_func_name(addr, &s);
            rt_fatal("llamada indirecta a 0x%08X, que no es inicio de funcion (dentro de %s+0x%X), LR=0x%08X\n",
                     addr, n, addr - s, ctx->lr);
        }
    }
    fn(ctx);
}

void ppc_unimplemented(PPCContext* ctx, uint32_t pc, uint32_t insn) {
    (void)ctx;
    rt_fatal("instruccion no soportada 0x%08X en 0x%08X\n", insn, pc);
}

void ppc_syscall(PPCContext* ctx, uint32_t pc) {
    static int n;
    (void)ctx;
    if (n++ < 4) rt_log("sc en 0x%08X (ignorado)%s\n", pc, n == 4 ? " (no se avisa mas)" : "");
}

uint64_t ppc_timebase(void) {
    /* El timebase de la GameCube corre a 40.5 MHz (bus de 162 MHz / 4). La consola cuenta
     * la hora desde el 1/1/2000 (no desde 1970): asi las partidas guardadas tienen bien la fecha. */
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (uint64_t)(ts.tv_sec - 946684800) * 40500000ull + (uint64_t)ts.tv_nsec * 405ull / 10000ull;
}
const char* rt_exe_path;   /* argv[0] (main.c) */
