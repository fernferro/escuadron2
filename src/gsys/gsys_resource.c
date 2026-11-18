#include "vm_sections.h"
#include "gsys/gsys.h"

/* Textos que todavia viven en la parte autogenerada de .vmdata */
extern char lbl_7FDBDBB0[]; /* "gsysGetResourceLength(): The specified resource ID is invalid!\n" */
extern char lbl_7FDBDBF0[]; /* gsysLoadResource(): nombre demasiado largo */
extern char lbl_7FDBDC34[]; /* "gsysLoadResource(): Could not allocate a job!\n" */
extern char lbl_7FDBDC64[]; /* gsysGetData(): longitud ilegal */
extern char lbl_7FDBDCA8[]; /* "gsysGetData(): Could not allocate a job!\n" */
extern char lbl_7FDBDCD4[]; /* "gsysUnloadResource(): Could not allocate a job!\n" */

gsysDirEntry* __gsysFindResource(gsysResourceFile* file, const char* name, s32 arg2);
s32 _gsysLoadResource(const char* name, u32 arg1, u32 arg2);
s32 _gsysGetData(gsysResource* res, s64 offset, s32 length, void* dest);
s32 gsysStrLen(const char* str);
void gsysStrNCpy(char* dst, const char* src, s32 n);
void gsysQueueSend(void* queue, void* msg, s32 flags);
void gsysQueueWait(void* queue);
void gsysQueuePost(void* queue);
void fn_80007270(void* ptr);
void fn_800073F0(void* ptr, s32 value, u32 size);
void fn_80007764(void* heap, void* ptr);

/*
 * Devuelve el tamano de un recurso. Con packed != 0 devuelve el tamano
 * comprimido si el recurso esta comprimido (packedSize != -1).
 */
u32 gsysGetResourceLength(gsysResource* res, u8 packed) {
    gsysResourceInfo* info;

    if (res != NULL) {
        info = res->info;
        if (!packed) {
            return info->size;
        } else if (info->packedSize == -1) {
            return info->size;
        } else {
            return info->packedSize;
        }
    }
    gsysDebugPrintf(lbl_7FDBDBB0);
    return 0;
}

/* Busca "name" en un archivo, o en todos los abiertos si file == -1. */
static inline gsysDirEntry* gsysFindEntry(const char* name, gsysResourceFile* file) {
    u32 i;
    gsysResourceFile* f;
    gsysDirEntry* entry;

    if (file == (gsysResourceFile*)-1) {
        for (i = 0; i < GSYS_MAX_FILES; i++) {
            f = &gsys.files[i];
            if (f->open) {
                entry = __gsysFindResource(f, name, 1);
                if (entry != NULL) {
                    return entry;
                }
            }
        }
        return NULL;
    }
    return __gsysFindResource(file, name, 1);
}

/*
 * Que es "name"? 0 = no existe, 1 = recurso, 2 = directorio.
 * file == (gsysResourceFile*)-1 busca en todos los archivos abiertos.
 */
s32 gsysGetResourceType(const char* name, gsysResourceFile* file) {
    gsysDirEntry* entry = gsysFindEntry(name, file);

    if (entry == NULL) {
        return 0;
    }
    if (entry->flags & GSYS_ENTRY_DIR) {
        return 2;
    }
    return 1;
}

/* Empieza a recorrer "name" (archivo o directorio). */
BOOL gsysFindFirst(gsysDirIter* iter, const char* name, gsysResourceFile* file) {
    gsysResourceFile* f;
    u32 i;
    gsysDirEntry* entry;

    if (name == NULL || iter == NULL) {
        return FALSE;
    }
    if (file == (gsysResourceFile*)-1) {
        for (i = 0; i < GSYS_MAX_FILES; i++) {
            f = &gsys.files[i];
            if (f->open) {
                entry = __gsysFindResource(f, name, 1);
                if (entry != NULL) {
                    file = f;
                    goto found;
                }
            }
        }
        entry = NULL;
    } else {
        entry = __gsysFindResource(file, name, 1);
    }
found:
    if (entry != NULL) {
        iter->entry = entry;
        iter->file = file;
        iter->index = 0;
        iter->unk14 = 0;
        if (entry->flags & GSYS_ENTRY_DIR) {
            if (entry->size == 0) {
                return FALSE;
            }
            iter->name = entry[1].name;
            iter->isDir = (entry[1].flags & GSYS_ENTRY_DIR) != 0;
            iter->index = 1;
        } else {
            iter->isDir = 0;
            iter->name = entry->name;
        }
        return TRUE;
    }
    return FALSE;
}

/* Siguiente entrada del directorio (saltea el contenido de subdirectorios). */
BOOL gsysFindNext(gsysDirIter* iter) {
    gsysDirEntry* dir;
    s32 index;

    if (iter == NULL) {
        return FALSE;
    }
    dir = iter->entry;
    if (!(dir->flags & GSYS_ENTRY_DIR)) {
        return FALSE;
    }
    index = iter->index;
    if (dir[index].flags & GSYS_ENTRY_DIR) {
        index += dir[index].size >> 6;
    } else {
        index++;
    }
    if (index < (s32)(dir->size >> 6)) {
        iter->name = dir[index].name;
        iter->isDir = (dir[index].flags & GSYS_ENTRY_DIR) != 0;
        iter->index = index;
        return TRUE;
    }
    return FALSE;
}

/* Reserva un trabajo de la cola circular; -1 si esta llena. */
typedef struct { s8 head; s8 count; } gsysRing;
static inline s8 gsysAllocJob(gsysRing* ring) {
    s8 index;
    if (ring->count < GSYS_MAX_JOBS) {
        index = ring->head;
        ring->head = (index + 1) % GSYS_MAX_JOBS;
        ring->count++;
    } else {
        index = -1;
    }
    return index;
}

/*
 * Carga un recurso por nombre. Sin callback es sincronico; con callback se
 * encola para el hilo de carga y devuelve -1.
 */
s32 gsysLoadResource(const char* name, gsysCallback callback, u32 arg2, u32 arg3) {
    gsysState* st = &gsys;
    void** lock;
    s32 result = 0;
    s8 index;
    gsysJob* job;

    if (gsysStrLen(name) > 0x80) {
        gsysDebugPrintf(lbl_7FDBDBF0, name);
        return -1;
    }
    lock = &st->lock;
    gsysQueueWait(*lock);
    if (callback != NULL) {
        index = gsysAllocJob((gsysRing*)&st->jobHead);
        if (index != -1) {
            job = &st->jobs[index];
            job->type = GSYS_JOB_LOAD;
            gsysStrNCpy(job->u.name, name, 0x80);
            job->arg1 = arg2;
            job->arg0 = arg3;
            job->callback = callback;
            job->msg = job;
            gsysQueueSend(st->jobQueue, &job->msg, 0);
            result = -1;
        } else {
            gsysDebugPrintf(lbl_7FDBDC34);
        }
    } else {
        result = _gsysLoadResource(name, arg2, arg3);
    }
    gsysQueuePost(*lock);
    return result;
}

/* Lee "length" bytes desde "offset" de un recurso abierto (sincronico o encolado). */
s32 gsysGetData(gsysResource* res, s64 offset, s32 length, void* dest, gsysCallback callback) {
    gsysState* st = &gsys;
    void** lock;
    s32 result = 0;
    s8 index;
    gsysJob* job;

    if (length <= 0 || length > 0x1700000) {
        gsysDebugPrintf(lbl_7FDBDC64);
        return 0;
    }
    lock = &st->lock;
    gsysQueueWait(*lock);
    if (callback != NULL) {
        index = gsysAllocJob((gsysRing*)&st->jobHead);
        if (index != -1) {
            job = &st->jobs[index];
            job->type = GSYS_JOB_GETDATA;
            job->arg0 = (u32)res;
            job->u.get.offset = offset;
            job->u.get.length = length;
            job->arg1 = (u32)dest;
            job->callback = callback;
            job->msg = job;
            gsysQueueSend(st->jobQueue, &job->msg, 0);
            result = -1;
        } else {
            gsysDebugPrintf(lbl_7FDBDCA8);
        }
    } else {
        result = _gsysGetData(res, offset, length, dest);
    }
    gsysQueuePost(*lock);
    return result;
}

/* Libera un recurso (sincronico o encolado). */
BOOL gsysUnloadResource(gsysResource* res, gsysCallback callback) {
    gsysState* st = &gsys;
    void** lock = &st->lock;
    BOOL result = FALSE;
    s8 index;
    gsysJob* job;

    gsysQueueWait(*lock);
    if (callback != NULL) {
        index = gsysAllocJob((gsysRing*)&st->jobHead);
        if (index != -1) {
            job = &st->jobs[index];
            job->type = GSYS_JOB_UNLOAD;
            job->arg0 = (u32)res;
            job->callback = callback;
            job->msg = job;
            gsysQueueSend(st->jobQueue, &job->msg, 0);
            result = TRUE;
        } else {
            gsysDebugPrintf(lbl_7FDBDCD4);
        }
    } else {
        if (res->buffer != NULL) {
            fn_80007270(res->buffer);
        }
        ((u32*)res->unk0)[6]--;
        fn_800073F0(res, 0, 0x10);
        fn_80007764(st->heap, res);
        result = TRUE;
    }
    gsysQueuePost(*lock);
    return result;
}

/* strlen */
s32 gsysStrLen(const char* str) {
    s32 len = 0;
    while (*str++ != 0) {
        len++;
    }
    return len;
}
