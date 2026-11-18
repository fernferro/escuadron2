/*
 * Perfilador por muestreo (solo Linux): RL_PROF=1 toma una muestra del contador de
 * programa cada 1 ms de CPU (de cualquier hilo) y al salir escribe build/prof.raw.
 * Analizar con: python3 port/tools/prof.py build/port/escuadron2_game build/prof.raw
 */
#if defined(__linux__) && defined(__x86_64__)
#define _GNU_SOURCE
#include "runtime.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <ucontext.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <execinfo.h>

#define MAXS (1 << 22)
static uint64_t* s_pc;
static uint64_t* s_caller;   /* RL_PROF_BT=1: quien llamo (de a 3 niveles) */
static int s_bt;
static uint32_t* s_tid;
static volatile int s_n;

static void on_prof(int sig, siginfo_t* si, void* uc_) {
    ucontext_t* uc = (ucontext_t*)uc_;
    int i = __atomic_fetch_add(&s_n, 1, __ATOMIC_RELAXED);
    (void)sig; (void)si;
    if (i >= MAXS) return;
    s_pc[i] = (uint64_t)uc->uc_mcontext.gregs[REG_RIP];
    s_tid[i] = (uint32_t)syscall(SYS_gettid);
    if (s_bt) {
        void* b[6];
        int n = backtrace(b, 6), k;
        for (k = 0; k < 3; k++) s_caller[i * 3 + k] = (n > k + 3) ? (uint64_t)(uintptr_t)b[k + 3] : 0;
    }
}

int main(int, char**);
static void prof_dump(void) {
    FILE* f = fopen("build/prof.raw", "w");
    int i, n = s_n < MAXS ? s_n : MAXS;
    if (!f) return;
    fprintf(f, "main %llx\n", (unsigned long long)(uintptr_t)&main);
    {   /* mapa de memoria, para atribuir muestras a bibliotecas */
        FILE* m = fopen("/proc/self/maps", "r");
        char line[512];
        while (m && fgets(line, sizeof line, m)) fprintf(f, "map %s", line);
        if (m) fclose(m);
    }
    for (i = 0; i < n; i++) {
        if (s_bt) fprintf(f, "%u %llx %llx %llx %llx\n", s_tid[i], (unsigned long long)s_pc[i], (unsigned long long)s_caller[i * 3],
                          (unsigned long long)s_caller[i * 3 + 1], (unsigned long long)s_caller[i * 3 + 2]);
        else fprintf(f, "%u %llx\n", s_tid[i], (unsigned long long)s_pc[i]);
    }
    fclose(f);
}

static int s_armed;
static void prof_timer_on(void) {
    struct itimerval it;
    it.it_interval.tv_sec = 0; it.it_interval.tv_usec = 1000;
    it.it_value = it.it_interval;
    setitimer(ITIMER_PROF, &it, NULL);
}
/* RL_PROF_DESDE=N: empezar a muestrear en el cuadro del juego N (lo llama video.c) */
void prof_frame(unsigned frame) {
    static long from = -2;
    if (!s_armed) return;
    if (from == -2) { const char* e = getenv("RL_PROF_DESDE"); from = e ? atol(e) : -1; }
    if (from >= 0 && (long)frame == from) { prof_timer_on(); rt_log("[prof] muestreo desde el cuadro %u\n", frame); }
}

void prof_start(void) {
    struct sigaction sa;
    struct itimerval it;
    if (!getenv("RL_PROF")) return;
    s_armed = 1;
    s_pc = (uint64_t*)malloc(sizeof(uint64_t) * MAXS);
    s_tid = (uint32_t*)malloc(sizeof(uint32_t) * MAXS);
    s_bt = getenv("RL_PROF_BT") != NULL;
    if (s_bt) { void* b[2]; s_caller = (uint64_t*)malloc(sizeof(uint64_t) * 3 * MAXS); backtrace(b, 2); }
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = on_prof;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigaction(SIGPROF, &sa, NULL);
    (void)it;
    if (!getenv("RL_PROF_DESDE")) prof_timer_on();
    atexit(prof_dump);
}
#else
#include "runtime.h"
void prof_start(void) {}
void prof_frame(unsigned frame) { (void)frame; }
#endif
