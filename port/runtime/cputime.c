/*
 * Tiempo de CPU por categoria de hilo (juego, grafico, DSP) para la linea [velocidad]:
 * dice cuantos milisegundos de procesador cuesta cada cuadro en cada parte.
 */
#include "runtime.h"
#include <pthread.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

#define MAXT 64
static struct { int cat; int used;
#ifdef _WIN32
    HANDLE h;
#else
    clockid_t cid;
#endif
} s_t[MAXT];
static pthread_mutex_t s_m = PTHREAD_MUTEX_INITIALIZER;

void cpu_register_thread(int cat) {
    int i;
    pthread_mutex_lock(&s_m);
    for (i = 0; i < MAXT && s_t[i].used; i++) {}
    if (i < MAXT) {
#ifdef _WIN32
        s_t[i].h = OpenThread(THREAD_QUERY_INFORMATION, FALSE, GetCurrentThreadId());
        if (s_t[i].h) { s_t[i].used = 1; s_t[i].cat = cat; }
#else
        if (pthread_getcpuclockid(pthread_self(), &s_t[i].cid) == 0) { s_t[i].used = 1; s_t[i].cat = cat; }
#endif
    }
    pthread_mutex_unlock(&s_m);
}

/* microsegundos de CPU acumulados de la categoria */
uint64_t cpu_category_us(int cat) {
    uint64_t sum = 0;
    int i;
    pthread_mutex_lock(&s_m);
    for (i = 0; i < MAXT; i++) {
        if (!s_t[i].used || s_t[i].cat != cat) continue;
#ifdef _WIN32
        {
            FILETIME a, b, k, u;
            if (GetThreadTimes(s_t[i].h, &a, &b, &k, &u)) {
                uint64_t kk = ((uint64_t)k.dwHighDateTime << 32) | k.dwLowDateTime;
                uint64_t uu = ((uint64_t)u.dwHighDateTime << 32) | u.dwLowDateTime;
                sum += (kk + uu) / 10;
            }
        }
#else
        {
            struct timespec ts;
            if (clock_gettime(s_t[i].cid, &ts) == 0) sum += (uint64_t)ts.tv_sec * 1000000ull + (uint64_t)ts.tv_nsec / 1000;
        }
#endif
    }
    pthread_mutex_unlock(&s_m);
    return sum;
}
