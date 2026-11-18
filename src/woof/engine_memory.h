/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef FD_ENGINE_MEMORY_H
#define FD_ENGINE_MEMORY_H
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <wchar.h>
#endif
void *fd_engine_malloc(size_t size);
void *fd_engine_calloc(size_t count, size_t size);
void *fd_engine_realloc(void *memory, size_t size);
void fd_engine_free(void *memory);
char *fd_engine_strdup(const char *text);
FILE *fd_engine_fopen(const char *path, const char *mode);
int fd_engine_fclose(FILE *file);
#ifdef _WIN32
FILE *fd_engine_wfopen(const wchar_t *path, const wchar_t *mode);
#endif
void fd_state_register(void *address, size_t size);
void fd_capture_engine_state(void);
void fd_release_engine_state(void);
#ifndef FD_MEMORY_IMPLEMENTATION
#define malloc fd_engine_malloc
#define calloc fd_engine_calloc
#define realloc fd_engine_realloc
#define free fd_engine_free
#define strdup fd_engine_strdup
#define fopen fd_engine_fopen
#define fclose fd_engine_fclose
#ifdef _WIN32
#define _wfopen fd_engine_wfopen
#endif
#endif
#endif
