/* Texturas en alta definicion (paquetes de Dolphin). Ver texturas_hd.c. */
#pragma once
#include <stdint.h>
#include <stddef.h>

enum { HD_NADA, HD_CARGANDO, HD_LISTA, HD_FALLO };
#define HD_MAX_NIVELES 14

typedef struct {
    int w, h, niveles;
    int comprimida;          /* 0: RGBA8; 1: bloques (formato_gl) */
    unsigned formato_gl;
    uint8_t* nivel[HD_MAX_NIVELES];
    uint32_t tam[HD_MAX_NIVELES];
} HdImagen;

typedef struct HdTex HdTex;

extern int hd_desactivado;       /* --sin-texturas-hd */
void hd_iniciar(void);           /* busca los paquetes (desde el hilo de OpenGL) */
int hd_activo(void);
uint64_t hd_xxh64(const void* p, size_t n);
HdTex* hd_buscar(const char* nombre);
int hd_estado(HdTex* t);         /* la primera vez la manda a leer en otro hilo */
const HdImagen* hd_imagen(HdTex* t);
void hd_soltar_imagen(HdTex* t); /* despues de subirla a la placa */
unsigned* hd_gl(HdTex* t);       /* textura de OpenGL ya subida (0 si todavia no) */
const char* hd_nombre(HdTex* t);
int hd_png_rgba(const char* ruta, const uint8_t* rgba, int w, int h);
