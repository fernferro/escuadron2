/* Musica de fondo del launcher.
 *
 * Si en la carpeta arte/ (la de datos o la de al lado del programa) hay un archivo
 * musica.ogg, .mp3, .flac o .wav, el launcher lo pasa en bucle. El archivo lo pone cada
 * uno (nunca va en el repo ni en el instalador). Se decodifica entero en otro hilo al abrir
 * (stb_vorbis / dr_mp3, dominio publico) y se para mientras se juega: el juego tiene su sonido.
 * Incluir una sola vez (lleva las implementaciones). */
#pragma once
#include <SDL.h>
#include <string>
#include <vector>

#include "stb/stb_vorbis.c"
#undef L
#undef C
#undef R
#define DR_MP3_IMPLEMENTATION
#include "dr_libs/dr_mp3.h"
#define DR_FLAC_IMPLEMENTATION
#include "dr_libs/dr_flac.h"

struct Musica {
    std::string ruta;                 /* archivo encontrado ("" si no hay) */
    std::vector<int16_t> pcm;         /* estereo intercalado */
    int frec = 44100;
    SDL_AudioDeviceID dev = 0;
    SDL_atomic_t lista;               /* 1 cuando termino de decodificar */
    size_t pos = 0;                   /* en cuadros (pares de muestras) */
    float vol = 0.6f, vol_actual = 0; /* vol_actual sube y baja de a poco (fundido) */
    bool sonando = false;             /* lo que se quiere: el fundido va hacia vol o hacia 0 */
};
static Musica s_mus;

static void mus_cb(void*, Uint8* out, int n) {
    int16_t* o = (int16_t*)out;
    int cuadros = n / 4;
    size_t total = s_mus.pcm.size() / 2;
    float objetivo = s_mus.sonando ? s_mus.vol : 0.0f;
    float paso = 1.0f / (s_mus.frec * 1.2f);   /* fundido de ~1 s */
    for (int i = 0; i < cuadros; i++) {
        if (s_mus.vol_actual < objetivo) s_mus.vol_actual = SDL_min(objetivo, s_mus.vol_actual + paso);
        else if (s_mus.vol_actual > objetivo) s_mus.vol_actual = SDL_max(objetivo, s_mus.vol_actual - paso);
        float g = s_mus.vol_actual * s_mus.vol_actual;   /* el oido escucha mas parejo asi */
        if (!total) { o[2 * i] = o[2 * i + 1] = 0; continue; }
        o[2 * i] = (int16_t)(s_mus.pcm[2 * s_mus.pos] * g);
        o[2 * i + 1] = (int16_t)(s_mus.pcm[2 * s_mus.pos + 1] * g);
        if (++s_mus.pos >= total) s_mus.pos = 0;
    }
}

static bool mus_termina(const std::string& a, const char* ext) {
    size_t n = strlen(ext);
    if (a.size() < n) return false;
    for (size_t i = 0; i < n; i++) if (tolower((unsigned char)a[a.size() - n + i]) != ext[i]) return false;
    return true;
}

static int mus_decodificar(void*) {
    const std::string& r = s_mus.ruta;
    int canales = 0, frec = 0;
    std::vector<int16_t> v;
    if (mus_termina(r, ".ogg")) {
        short* d = NULL;
        int n = stb_vorbis_decode_filename(r.c_str(), &canales, &frec, &d);
        if (n > 0 && d) v.assign(d, d + (size_t)n * canales);
        free(d);
    } else if (mus_termina(r, ".mp3")) {
        drmp3_config c; drmp3_uint64 n = 0;
        drmp3_int16* d = drmp3_open_file_and_read_pcm_frames_s16(r.c_str(), &c, &n, NULL);
        if (d) { canales = (int)c.channels; frec = (int)c.sampleRate; v.assign(d, d + (size_t)n * canales); drmp3_free(d, NULL); }
    } else if (mus_termina(r, ".flac")) {
        unsigned int ch = 0, fr = 0; drflac_uint64 n = 0;
        drflac_int16* d = drflac_open_file_and_read_pcm_frames_s16(r.c_str(), &ch, &fr, &n, NULL);
        if (d) { canales = (int)ch; frec = (int)fr; v.assign(d, d + (size_t)n * canales); drflac_free(d, NULL); }
    } else {
        SDL_AudioSpec s; Uint8* d = NULL; Uint32 n = 0;
        if (SDL_LoadWAV(r.c_str(), &s, &d, &n)) {
            SDL_AudioCVT cvt;   /* lo que sea -> 16 bits */
            if (SDL_BuildAudioCVT(&cvt, s.format, s.channels, s.freq, AUDIO_S16SYS, s.channels, s.freq) >= 0) {
                std::vector<Uint8> b((size_t)n * (cvt.len_mult > 0 ? cvt.len_mult : 1));
                memcpy(b.data(), d, n);
                cvt.buf = b.data(); cvt.len = (int)n;
                if (cvt.needed) SDL_ConvertAudio(&cvt);
                int16_t* p = (int16_t*)b.data();
                v.assign(p, p + (cvt.needed ? cvt.len_cvt : (int)n) / 2);
                canales = s.channels; frec = s.freq;
            }
            SDL_FreeWAV(d);
        }
    }
    if (canales == 1) {   /* mono -> estereo */
        std::vector<int16_t> e(v.size() * 2);
        for (size_t i = 0; i < v.size(); i++) e[2 * i] = e[2 * i + 1] = v[i];
        v.swap(e);
    } else if (canales > 2) {   /* se quedan los dos primeros */
        std::vector<int16_t> e(v.size() / canales * 2);
        for (size_t i = 0; i < e.size() / 2; i++) { e[2 * i] = v[i * canales]; e[2 * i + 1] = v[i * canales + 1]; }
        v.swap(e);
    }
    if (v.empty() || frec <= 0) return 0;
    s_mus.pcm.swap(v);
    s_mus.frec = frec;
    SDL_AtomicSet(&s_mus.lista, 1);
    return 0;
}

/* busca arte/musica.* y la empieza a decodificar */
static void mus_iniciar(const std::vector<std::string>& carpetas) {
    static const char* nombres[] = { "musica.ogg", "musica.mp3", "musica.flac", "musica.wav" };
    for (auto& d : carpetas)
        for (const char* n : nombres) {
            std::string p = d + "/" + n;
            FILE* f = fopen(p.c_str(), "rb");
            if (f) { fclose(f); s_mus.ruta = p; break; }
        }
    if (s_mus.ruta.empty()) return;
    SDL_Thread* t = SDL_CreateThread(mus_decodificar, "musica", NULL);
    if (t) SDL_DetachThread(t);
}

/* en cada cuadro: abre el audio cuando esta lista y la prende o la apaga */
static void mus_actualizar(bool quiere, float vol) {
    if (!SDL_AtomicGet(&s_mus.lista)) return;
    if (!s_mus.dev) {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) { SDL_AtomicSet(&s_mus.lista, 0); return; }
        SDL_AudioSpec q, obt;
        SDL_zero(q);
        q.freq = s_mus.frec; q.format = AUDIO_S16SYS; q.channels = 2; q.samples = 2048; q.callback = mus_cb;
        s_mus.dev = SDL_OpenAudioDevice(NULL, 0, &q, &obt, 0);
        if (!s_mus.dev) { SDL_AtomicSet(&s_mus.lista, 0); return; }
        SDL_PauseAudioDevice(s_mus.dev, 0);
    }
    SDL_LockAudioDevice(s_mus.dev);
    s_mus.sonando = quiere;
    s_mus.vol = vol;
    bool callada = !quiere && s_mus.vol_actual <= 0.0f;
    SDL_UnlockAudioDevice(s_mus.dev);
    /* callada del todo (jugando o apagada): el dispositivo en pausa, no gasta nada */
    if (callada != (SDL_GetAudioDeviceStatus(s_mus.dev) == SDL_AUDIO_PAUSED)) SDL_PauseAudioDevice(s_mus.dev, callada ? 1 : 0);
}

static bool mus_hay() { return !s_mus.ruta.empty(); }
