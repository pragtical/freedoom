/* SPDX-License-Identifier: GPL-2.0-or-later */
#define FD_MEMORY_IMPLEMENTATION
#include "engine_memory.h"
#include <stdint.h>
#include "i_system.h"

/* Only the engine worker touches these records. Host buffers, synchronization
   objects and session handles have separate ownership and are never tracked. */
typedef struct allocation {
    void *memory;
    struct allocation *next;
} allocation;
static allocation *allocations[4096];
typedef struct file_record { FILE *file; struct file_record *next; } file_record;
static file_record *files;
typedef struct state_record {
    void *address;
    size_t size;
    struct state_record *next;
    unsigned char initial[];
} state_record;
static state_record *states;
static unsigned bucket(void *memory) { return ((uintptr_t)memory >> 4) % 4096; }
static void remember(void *memory) {
    if (!memory) return;
    allocation *record = malloc(sizeof(*record));
    if (!record) { free(memory); I_Error("Out of memory tracking engine allocation"); }
    unsigned key = bucket(memory);
    *record = (allocation){memory, allocations[key]}; allocations[key] = record;
}
static void forget(void *memory) {
    allocation **link = &allocations[bucket(memory)];
    while (*link && (*link)->memory != memory) link = &(*link)->next;
    if (*link) { allocation *record = *link; *link = record->next; free(record); }
}
void *fd_engine_malloc(size_t size) { void *result = malloc(size); remember(result); return result; }
void *fd_engine_calloc(size_t count, size_t size) { void *result = calloc(count, size); remember(result); return result; }
void *fd_engine_realloc(void *memory, size_t size) {
    if (!size) { fd_engine_free(memory); return NULL; }
    forget(memory);
    void *result = realloc(memory, size);
    if (result) remember(result);
    else remember(memory);
    return result;
}
void fd_engine_free(void *memory) { if (memory) { forget(memory); free(memory); } }
char *fd_engine_strdup(const char *text) {
    size_t size = strlen(text) + 1; char *result = fd_engine_malloc(size);
    if (result) memcpy(result, text, size);
    return result;
}
static FILE *remember_file(FILE *file) {
    if (!file) return NULL;
    file_record *record = malloc(sizeof(*record));
    if (!record) { fclose(file); I_Error("Out of memory tracking engine file"); }
    *record = (file_record){file, files}; files = record; return file;
}
FILE *fd_engine_fopen(const char *path, const char *mode) { return remember_file(fopen(path, mode)); }
#ifdef _WIN32
FILE *fd_engine_wfopen(const wchar_t *path, const wchar_t *mode) { return remember_file(_wfopen(path, mode)); }
#endif
int fd_engine_fclose(FILE *file) {
    file_record **link = &files;
    while (*link && (*link)->file != file) link = &(*link)->next;
    if (*link) { file_record *record = *link; *link = record->next; free(record); }
    return fclose(file);
}
void fd_state_register(void *address, size_t size) {
    state_record *record = malloc(sizeof(*record) + size);
    if (!record) I_Error("Out of memory recording initial engine state");
    record->address = address; record->size = size; record->next = states;
    memcpy(record->initial, address, size); states = record;
}
void fd_release_engine_state(void) {
    while (files) { file_record *record = files; files = record->next; fclose(record->file); free(record); }
    for (unsigned i = 0; i < 4096; i++) {
        while (allocations[i]) {
            allocation *record = allocations[i]; allocations[i] = record->next;
            free(record->memory); free(record);
        }
    }
    for (state_record *record = states; record; record = record->next)
        memcpy(record->address, record->initial, record->size);
}
