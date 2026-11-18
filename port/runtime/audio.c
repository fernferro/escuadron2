/*
 * Salida de audio: cada bloque que el DMA de audio (AI) "reproduce" se manda a SDL.
 * Las muestras de la GameCube son de 16 bits big-endian, estereo, 32 kHz, con el
 * canal derecho primero. Con --wav ARCHIVO tambien se graban (para pruebas).
 */
#include "runtime.h"
#include <stdio.h>
#include <string.h>
#ifdef RL_GL
#include <SDL.h>
static SDL_AudioDeviceID s_dev;
#endif

const char* audio_wav_path;
int audio_disabled;
static FILE* s_wav;
static uint32_t s_wav_bytes;
static int s_inited;
static int s_started;
unsigned audio_cortes;

#define RATE 32000

static void wav_header(FILE* f, uint32_t data) {
    uint8_t h[44];
    uint32_t v;
    memcpy(h, "RIFF", 4); v = 36 + data; memcpy(h + 4, &v, 4);
    memcpy(h + 8, "WAVEfmt ", 8); v = 16; memcpy(h + 16, &v, 4);
    h[20] = 1; h[21] = 0; h[22] = 2; h[23] = 0;
    v = RATE; memcpy(h + 24, &v, 4); v = RATE * 4; memcpy(h + 28, &v, 4);
    h[32] = 4; h[33] = 0; h[34] = 16; h[35] = 0;
    memcpy(h + 36, "data", 4); memcpy(h + 40, &data, 4);
    fseek(f, 0, SEEK_SET); fwrite(h, 1, 44, f); fseek(f, 0, SEEK_END);
}

static void audio_init(void) {
    s_inited = 1;
    if (audio_wav_path) {
        s_wav = fopen(audio_wav_path, "wb");
        if (s_wav) wav_header(s_wav, 0);
    }
#ifdef RL_GL
    extern int gl_headless;
    if (!audio_disabled && !gl_headless && SDL_InitSubSystem(SDL_INIT_AUDIO) == 0) {
        SDL_AudioSpec want, have;
        memset(&want, 0, sizeof want);
        want.freq = RATE; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 512;
        s_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
        if (s_dev) SDL_PauseAudioDevice(s_dev, 0);
        else rt_log("[audio] no se pudo abrir la salida de audio: %s\n", SDL_GetError());
    }
#endif
}

/* Milisegundos de audio encolados en la placa de sonido (-1 si no hay salida). */
int audio_queued_ms(void) {
#ifdef RL_GL
    if (s_dev) return (int)(SDL_GetQueuedAudioSize(s_dev) / (RATE * 4 / 1000));
#endif
    return -1;
}

int audio_volumen = 100;   /* 0..100 */
void audio_push(const uint8_t* be, uint32_t bytes) {
    int16_t buf[4096];
    uint32_t n = bytes / 4, i;
    if (!s_inited) audio_init();
    if (!be || n == 0) return;
    if (n > 2048) n = 2048;
    for (i = 0; i < n; i++) {
        buf[2 * i + 1] = (int16_t)(be[4 * i] << 8 | be[4 * i + 1]);       /* derecho */
        buf[2 * i]     = (int16_t)(be[4 * i + 2] << 8 | be[4 * i + 3]);   /* izquierdo */
    }
    if (s_wav) {
        fwrite(buf, 4, n, s_wav);
        s_wav_bytes += n * 4;
        if ((s_wav_bytes & 0xFFFF) < n * 4) { wav_header(s_wav, s_wav_bytes); fflush(s_wav); }
    }
#ifdef RL_GL
    if (audio_volumen < 100) {   /* volumen general (opciones): no toca el .wav de pruebas */
        int v = audio_volumen < 0 ? 0 : audio_volumen;
        for (i = 0; i < 2 * n; i++) buf[i] = (int16_t)((int32_t)buf[i] * v / 100);
    }
    if (s_dev) {
        int q = audio_queued_ms();
        if (q == 0 && s_started) audio_cortes++;   /* la placa de sonido se quedo sin nada: se escucha un corte */
        if (q < 250) SDL_QueueAudio(s_dev, buf, n * 4);
        if (audio_queued_ms() > 50) s_started = 1;
    }
#endif
}
