/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>
#include "platform/SDL3/SDL.h"
#ifdef _WIN32
#include <windows.h>
#include <process.h>
#include <wchar.h>
struct fd_mutex { SRWLOCK native; };
struct fd_thread { HANDLE native; int (*function)(void *); void *data; int result; };
static _Thread_local HANDLE sleep_timer;
static wchar_t *wide(const char *str) {
    int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, str, -1, NULL, 0);
    wchar_t *result = length ? malloc(length * sizeof(*result)) : NULL;
    if (result && !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, str, -1, result, length)) {
        free(result); return NULL;
    }
    return result;
}
uint64_t fd_clock_ns(void) {
    LARGE_INTEGER now, frequency;
    QueryPerformanceCounter(&now); QueryPerformanceFrequency(&frequency);
    return now.QuadPart / frequency.QuadPart * SDL_NS_PER_SECOND
        + now.QuadPart % frequency.QuadPart * SDL_NS_PER_SECOND / frequency.QuadPart;
}
void fd_sleep_ns(uint64_t ns) {
    if (!ns) { Sleep(0); return; }
    if (!sleep_timer) {
        /* Unlike timeBeginPeriod, a private high-resolution timer does not
           change the editor's or the rest of the system's timer settings. */
        sleep_timer = CreateWaitableTimerExW(NULL, NULL, 2, TIMER_ALL_ACCESS);
        if (!sleep_timer) sleep_timer = CreateWaitableTimerExW(NULL, NULL, 0, TIMER_ALL_ACCESS);
    }
    LARGE_INTEGER due; due.QuadPart = -(LONGLONG)((ns + 99) / 100);
    if (sleep_timer && SetWaitableTimer(sleep_timer, &due, 0, NULL, NULL, FALSE))
        WaitForSingleObject(sleep_timer, INFINITE);
    else Sleep((DWORD)((ns + 999999) / 1000000));
}
SDL_Mutex *fd_mutex_create(void) {
    SDL_Mutex *m = malloc(sizeof(*m)); if (m) InitializeSRWLock(&m->native); return m;
}
void fd_mutex_destroy(SDL_Mutex *m) { free(m); }
void fd_mutex_lock(SDL_Mutex *m) { AcquireSRWLockExclusive(&m->native); }
bool fd_mutex_trylock(SDL_Mutex *m) { return TryAcquireSRWLockExclusive(&m->native) != 0; }
void fd_mutex_unlock(SDL_Mutex *m) { ReleaseSRWLockExclusive(&m->native); }
static unsigned __stdcall thread_main(void *data) {
    SDL_Thread *t = data; t->result = t->function(t->data);
    if (sleep_timer) { CloseHandle(sleep_timer); sleep_timer = NULL; }
    return 0;
}
SDL_Thread *fd_thread_create(int (*function)(void *), void *data) {
    SDL_Thread *t = calloc(1, sizeof(*t)); if (!t) return NULL;
    t->function = function; t->data = data;
    t->native = (HANDLE)_beginthreadex(NULL, 0, thread_main, t, 0, NULL);
    if (!t->native) { free(t); return NULL; } return t;
}
void fd_thread_join(SDL_Thread *t, int *result) {
    if (!t) return;
    WaitForSingleObject(t->native, INFINITE); CloseHandle(t->native);
    if (result) *result = t->result;
    free(t);
}
bool fd_path_info(const char *path, SDL_PathInfo *info) {
    wchar_t *w = wide(path); WIN32_FILE_ATTRIBUTE_DATA data;
    bool ok = w && GetFileAttributesExW(w, GetFileExInfoStandard, &data); free(w);
    if (!ok) return false;
    memset(info, 0, sizeof(*info));
    info->type = data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ? SDL_PATHTYPE_DIRECTORY : SDL_PATHTYPE_FILE;
    info->size = (uint64_t)data.nFileSizeHigh << 32 | data.nFileSizeLow;
    info->modify_time = (((uint64_t)data.ftLastWriteTime.dwHighDateTime << 32 | data.ftLastWriteTime.dwLowDateTime)
        - UINT64_C(116444736000000000)) * 100;
    return true;
}
bool fd_make_directory(const char *path) {
    wchar_t *w = wide(path); bool ok = w && (CreateDirectoryW(w, NULL) || GetLastError() == ERROR_ALREADY_EXISTS);
    free(w); return ok;
}
bool fd_remove_path(const char *path) {
    wchar_t *w = wide(path); bool ok = w && (DeleteFileW(w) || RemoveDirectoryW(w)); free(w); return ok;
}
bool fd_rename_path(const char *from, const char *to) {
    wchar_t *a = wide(from), *b = wide(to);
    bool ok = a && b && MoveFileExW(a, b, MOVEFILE_REPLACE_EXISTING); free(a); free(b); return ok;
}
bool fd_copy_file(const char *from, const char *to) {
    wchar_t *a = wide(from), *b = wide(to);
    bool ok = a && b && CopyFileW(a, b, FALSE); free(a); free(b); return ok;
}
bool fd_enumerate(const char *path, SDL_EnumerationResult (*callback)(void *, const char *, const char *), void *data) {
    char *prefix = malloc(strlen(path) + 3); if (!prefix) return false;
    sprintf(prefix, "%s/", path);
    char *pattern = malloc(strlen(prefix) + 2); if (!pattern) { free(prefix); return false; }
    sprintf(pattern, "%s*", prefix); wchar_t *w = wide(pattern); free(pattern);
    WIN32_FIND_DATAW item; HANDLE search = w ? FindFirstFileW(w, &item) : INVALID_HANDLE_VALUE; free(w);
    if (search == INVALID_HANDLE_VALUE) { free(prefix); return false; }
    bool ok = true;
    do {
        char name[4096];
        if (!WideCharToMultiByte(CP_UTF8, 0, item.cFileName, -1, name, sizeof(name), NULL, NULL)) { ok = false; break; }
        if (!strcmp(name, ".") || !strcmp(name, "..")) continue;
        SDL_EnumerationResult result = callback(data, prefix, name);
        if (result != SDL_ENUM_CONTINUE) { ok = result == SDL_ENUM_SUCCESS; break; }
    } while (FindNextFileW(search, &item));
    FindClose(search); free(prefix); return ok;
}
#else
#include <dirent.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
struct fd_mutex { pthread_mutex_t native; };
struct fd_thread { pthread_t native; int (*function)(void *); void *data; int result; };
uint64_t fd_clock_ns(void) {
    struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * SDL_NS_PER_SECOND + now.tv_nsec;
}
void fd_sleep_ns(uint64_t ns) {
    struct timespec delay = {ns / SDL_NS_PER_SECOND, ns % SDL_NS_PER_SECOND};
    while (nanosleep(&delay, &delay) && errno == EINTR) {}
}
SDL_Mutex *fd_mutex_create(void) {
    SDL_Mutex *m = malloc(sizeof(*m));
    if (m && pthread_mutex_init(&m->native, NULL)) { free(m); return NULL; } return m;
}
void fd_mutex_destroy(SDL_Mutex *m) { if (m) { pthread_mutex_destroy(&m->native); free(m); } }
void fd_mutex_lock(SDL_Mutex *m) { pthread_mutex_lock(&m->native); }
bool fd_mutex_trylock(SDL_Mutex *m) { return pthread_mutex_trylock(&m->native) == 0; }
void fd_mutex_unlock(SDL_Mutex *m) { pthread_mutex_unlock(&m->native); }
static void *thread_main(void *data) {
    SDL_Thread *t = data; t->result = t->function(t->data); return NULL;
}
SDL_Thread *fd_thread_create(int (*function)(void *), void *data) {
    SDL_Thread *t = calloc(1, sizeof(*t)); if (!t) return NULL;
    t->function = function; t->data = data;
    if (pthread_create(&t->native, NULL, thread_main, t)) { free(t); return NULL; } return t;
}
void fd_thread_join(SDL_Thread *t, int *result) {
    if (!t) return;
    pthread_join(t->native, NULL);
    if (result) *result = t->result;
    free(t);
}
bool fd_path_info(const char *path, SDL_PathInfo *info) {
    struct stat st; if (stat(path, &st)) return false;
    memset(info, 0, sizeof(*info)); info->size = st.st_size;
    info->modify_time = (uint64_t)st.st_mtime * SDL_NS_PER_SECOND;
    info->type = S_ISREG(st.st_mode) ? SDL_PATHTYPE_FILE : S_ISDIR(st.st_mode) ? SDL_PATHTYPE_DIRECTORY : SDL_PATHTYPE_OTHER;
    return true;
}
bool fd_make_directory(const char *path) { return mkdir(path, 0700) == 0 || errno == EEXIST; }
bool fd_remove_path(const char *path) { return remove(path) == 0; }
bool fd_rename_path(const char *from, const char *to) { return rename(from, to) == 0; }
bool fd_copy_file(const char *from, const char *to) {
    FILE *input = fopen(from, "rb"); if (!input) return false;
    FILE *output = fopen(to, "wb"); if (!output) { fclose(input); return false; }
    char buffer[16384]; size_t count; bool ok = true;
    while ((count = fread(buffer, 1, sizeof(buffer), input)))
        if (fwrite(buffer, 1, count, output) != count) { ok = false; break; }
    ok = !ferror(input) && ok;
    fclose(input); if (fclose(output)) ok = false; return ok;
}
bool fd_enumerate(const char *path, SDL_EnumerationResult (*callback)(void *, const char *, const char *), void *data) {
    DIR *directory = opendir(path); if (!directory) return false;
    char *prefix = malloc(strlen(path) + 2); if (!prefix) { closedir(directory); return false; }
    sprintf(prefix, "%s/", path); bool ok = true; struct dirent *entry;
    while ((entry = readdir(directory))) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        SDL_EnumerationResult result = callback(data, prefix, entry->d_name);
        if (result != SDL_ENUM_CONTINUE) { ok = result == SDL_ENUM_SUCCESS; break; }
    }
    free(prefix); closedir(directory); return ok;
}
#endif
FILE *fd_open_file(const char *path, const char *mode) {
#ifdef _WIN32
    wchar_t *w = wide(path), *m = wide(mode);
    FILE *file = w && m ? _wfopen(w, m) : NULL;
    free(w); free(m); return file;
#else
    return fopen(path, mode);
#endif
}
bool fd_save_file(const char *path, const void *data, size_t length) {
    FILE *file = fd_open_file(path, "wb");
    if (!file) return false;
    bool ok = fwrite(data, 1, length, file) == length;
    return fclose(file) == 0 && ok;
}
