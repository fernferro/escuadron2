#ifndef GSYS_H
#define GSYS_H

#include "types.h"

/* Entrada del directorio de un archivo de recursos (64 bytes). */
typedef struct gsysDirEntry {
    u8 unk0[0x18];
    u8 flags;       /* 0x18: GSYS_ENTRY_DIR si es un directorio */
    u8 unk19[3];
    u32 size;       /* 0x1C: en directorios, bytes de entradas hijas (n * 0x40) */
    char name[0x20]; /* 0x20 */
} gsysDirEntry;

#define GSYS_ENTRY_DIR 0x80

/* Informacion de un recurso abierto. */
typedef struct gsysResourceInfo {
    u8 unk0[0xC];
    u32 size;        /* 0x0C: tamano descomprimido */
    s64 packedSize;  /* 0x10: tamano comprimido, -1 si no esta comprimido */
} gsysResourceInfo;

typedef struct gsysResource {
    u32 unk0;
    gsysResourceInfo* info; /* 0x04 */
    u8 unk8[4];
    void* buffer;           /* 0x0C */
} gsysResource;

/* Archivo de recursos registrado (0xA0 bytes). */
typedef struct gsysResourceFile {
    u8 unk0[0x1C];
    u8 open;        /* 0x1C */
    u8 unk1D[0xA0 - 0x1D];
} gsysResourceFile;

/* Iterador de directorio. */
typedef struct gsysDirIter {
    char* name;             /* 0x00 */
    u8 isDir;               /* 0x04 */
    gsysDirEntry* entry;    /* 0x08 */
    gsysResourceFile* file; /* 0x0C */
    s32 index;              /* 0x10 */
    u32 unk14;              /* 0x14 */
} gsysDirIter;

typedef void (*gsysCallback)(void);

/* Trabajo para el hilo de carga asincrona (0x98 bytes). */
typedef struct gsysJob {
    u32 arg0;         /* 0x00 */
    u32 arg1;         /* 0x04 */
    union {
        char name[0x80]; /* 0x08: LOAD */
        struct {
            s64 offset;  /* 0x08 */
            s32 length;  /* 0x10 */
        } get;
    } u;
    s32 type;          /* 0x88 */
    gsysCallback callback; /* 0x8C */
    struct gsysJob* msg;   /* 0x90: mensaje enviado a la cola (apunta al propio trabajo) */
    u32 unk94;
} gsysJob;

enum {
    GSYS_JOB_LOAD = 2,
    GSYS_JOB_UNLOAD = 3,
    GSYS_JOB_GETDATA = 4,
};

#define GSYS_MAX_FILES 16
#define GSYS_MAX_JOBS 16

/* Estado global del sistema de recursos (en .vmbss). */
typedef struct gsysState {
    u8 unk0[0x44];
    void* jobQueue;                          /* 0x44: cola de mensajes del hilo de carga */
    u8 unk48[4];
    gsysResourceFile files[GSYS_MAX_FILES];  /* 0x4C */
    u8 unkA4C[4];
    gsysJob jobs[GSYS_MAX_JOBS];             /* 0xA50 */
    s8 jobHead;                              /* 0x13D0 */
    s8 jobCount;                             /* 0x13D1 */
    u8 unk13D2[2];
    void* heap;                              /* 0x13D4 */
    void* lock;                              /* 0x13D8: cola usada como mutex */
} gsysState;

extern gsysState lbl_7FDEE7B8;
#define gsys lbl_7FDEE7B8

void gsysDebugPrintf(const char* fmt, ...);
u32 gsysGetResourceLength(gsysResource* res, u8 packed);

#endif
