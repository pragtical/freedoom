/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef FD_PLATFORM_SDL_H
#define FD_PLATFORM_SDL_H
/* Source compatibility for Woof's portable utilities, not an SDL implementation.
   All symbols are private to the plugin; no editor SDL state is accessed. */
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#define SDL_arraysize(a) (sizeof(a) / sizeof(*(a)))
#define SDL_zero(a) memset(&(a), 0, sizeof(a))
#define SDL_memcpy memcpy
#define SDL_memset memset
#define SDL_free free
#define SDL_malloc malloc
#define SDL_calloc calloc
#define SDL_realloc realloc
#define SDL_strdup strdup
#define SDL_NS_PER_SECOND UINT64_C(1000000000)
#define SDL_NS_TO_SECONDS(ns) ((ns) / SDL_NS_PER_SECOND)
#define SDL_NS_PER_MS UINT64_C(1000000)
#define SDL_JOYSTICK_AXIS_MAX 32767
#define SDL_JOYSTICK_AXIS_MIN (-32768)
#define SDL_SCANCODE_1 30
#define SDL_SCANCODE_2 31
#define SDL_SCANCODE_3 32
#define SDL_SCANCODE_4 33
#define SDL_MS_TO_NS(ms) ((uint64_t)(ms) * SDL_NS_PER_MS)
typedef uint64_t Uint64;
typedef uint32_t Uint32;
typedef uint16_t Uint16;
typedef uint8_t Uint8;
typedef int32_t Sint32;
typedef struct { uint8_t r, g, b, a; } SDL_Color;
typedef struct SDL_Gamepad SDL_Gamepad;
typedef struct SDL_Event SDL_Event;
typedef void SDL_Process;
#define SDL_CreateProcess(args, wait) ((void)(args), (void)(wait), (SDL_Process *)0)
typedef int SDL_JoystickID;
typedef int SDL_GamepadAxis;
typedef struct { _Atomic int value; } SDL_AtomicInt;
typedef struct fd_mutex SDL_Mutex;
typedef struct fd_thread SDL_Thread;
typedef enum { SDL_ENUM_CONTINUE, SDL_ENUM_SUCCESS, SDL_ENUM_FAILURE } SDL_EnumerationResult;
typedef struct { int type; uint64_t size, create_time, modify_time, access_time; } SDL_PathInfo;
enum { SDL_PATHTYPE_NONE, SDL_PATHTYPE_FILE, SDL_PATHTYPE_DIRECTORY, SDL_PATHTYPE_OTHER };
enum { SDL_GAMEPAD_BUTTON_INVALID = -1, SDL_GAMEPAD_BUTTON_SOUTH,
    SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_NORTH,
    SDL_GAMEPAD_BUTTON_BACK, SDL_GAMEPAD_BUTTON_GUIDE, SDL_GAMEPAD_BUTTON_START,
    SDL_GAMEPAD_BUTTON_LEFT_STICK, SDL_GAMEPAD_BUTTON_RIGHT_STICK,
    SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
    SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_DOWN,
    SDL_GAMEPAD_BUTTON_DPAD_LEFT, SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
    SDL_GAMEPAD_BUTTON_MISC1, SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1,
    SDL_GAMEPAD_BUTTON_LEFT_PADDLE1, SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2,
    SDL_GAMEPAD_BUTTON_LEFT_PADDLE2, SDL_GAMEPAD_BUTTON_TOUCHPAD,
    SDL_GAMEPAD_BUTTON_MISC2, SDL_GAMEPAD_BUTTON_MISC3, SDL_GAMEPAD_BUTTON_MISC4,
    SDL_GAMEPAD_BUTTON_MISC5, SDL_GAMEPAD_BUTTON_MISC6, SDL_GAMEPAD_BUTTON_COUNT };
enum { SDL_GAMEPAD_AXIS_INVALID = -1, SDL_GAMEPAD_AXIS_LEFTX,
    SDL_GAMEPAD_AXIS_LEFTY, SDL_GAMEPAD_AXIS_RIGHTX, SDL_GAMEPAD_AXIS_RIGHTY,
    SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, SDL_GAMEPAD_AXIS_COUNT };
#define SDL_GetPerformanceCounter fd_clock_ns
#define SDL_GetPerformanceFrequency() SDL_NS_PER_SECOND
#define SDL_GetTicksNS fd_clock_ns
#define SDL_GetTicks() (fd_clock_ns() / SDL_NS_PER_MS)
#define SDL_Delay(ms) fd_sleep_ns(SDL_MS_TO_NS(ms))
#define SDL_DelayPrecise fd_sleep_ns
#define SDL_CreateMutex fd_mutex_create
#define SDL_DestroyMutex fd_mutex_destroy
#define SDL_LockMutex fd_mutex_lock
#define SDL_UnlockMutex fd_mutex_unlock
#define SDL_GetAtomicInt(a) atomic_load(&(a)->value)
#define SDL_SetAtomicInt(a,v) atomic_exchange(&(a)->value, (v))
#define SDL_CreateThread(fn,name,data) fd_thread_create(fn,data)
#define SDL_WaitThread fd_thread_join
#define SDL_GetPathInfo fd_path_info
#define SDL_EnumerateDirectory fd_enumerate
#define SDL_CreateDirectory fd_make_directory
#define SDL_RemovePath fd_remove_path
#define SDL_RenamePath fd_rename_path
#define SDL_CopyFile fd_copy_file
#define SDL_SaveFile fd_save_file
#define SDL_GetBasePath fd_base_path
#define SDL_GetPrefPath(org,app) fd_pref_path()
#define SDL_GetPlatform() "Pragtical"
#define SDL_SetAppMetadata(a,b,c) ((void)0)
#define SDL_ShowSimpleMessageBox(a,b,c,d) ((void)0)
#define SDL_MESSAGEBOX_ERROR 0
#define SDL_MESSAGEBOX_INFORMATION 0
#define SDL_GetError() "Freedoom platform operation failed"
uint64_t fd_clock_ns(void);
void fd_sleep_ns(uint64_t ns);
SDL_Mutex *fd_mutex_create(void);
void fd_mutex_destroy(SDL_Mutex *mutex);
void fd_mutex_lock(SDL_Mutex *mutex);
bool fd_mutex_trylock(SDL_Mutex *mutex);
void fd_mutex_unlock(SDL_Mutex *mutex);
SDL_Thread *fd_thread_create(int (*function)(void *), void *data);
void fd_thread_join(SDL_Thread *thread, int *result);
bool fd_path_info(const char *path, SDL_PathInfo *info);
bool fd_enumerate(const char *path, SDL_EnumerationResult (*callback)(void *, const char *, const char *), void *data);
bool fd_make_directory(const char *path);
bool fd_remove_path(const char *path);
bool fd_rename_path(const char *from, const char *to);
bool fd_copy_file(const char *from, const char *to);
FILE *fd_open_file(const char *path, const char *mode);
bool fd_save_file(const char *path, const void *data, size_t length);
const char *fd_base_path(void);
char *fd_pref_path(void);
#endif
