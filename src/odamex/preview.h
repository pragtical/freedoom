/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef FD_ODAMEX_PREVIEW_H
#define FD_ODAMEX_PREVIEW_H
#include "api.h"
#include <stdbool.h>
#include <stdint.h>
#include "audio_state.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void *context;
    int (*stopping)(void *context);
    int (*input)(void *context, unsigned char packet[4]);
    void (*frame)(void *context, const unsigned char *rgba, unsigned width, unsigned height,
                  int online_level);
    void (*error)(void *context, const char *message);
    void (*log)(void *context, const char *message);
    uint64_t (*clock)(void *context);
    void (*sleep)(uint64_t ns);
    uint32_t (*size)(void *context);
    unsigned (*cache_sample)(int lump, const void *data, unsigned size, unsigned rate,
                             unsigned encoded, bool looping);
    void (*set_voice)(unsigned channel, const fd_voice_state *voice);
    fd_voice_state (*get_voice)(unsigned channel);
    void (*set_music)(unsigned paused, float gain, bool clear);
    void (*push_music)(const int16_t *pcm, unsigned frames);
} fd_odamex_host;

typedef struct {
    const char *resources, *state, *iwad, *endpoint, *name, *password;
    int sound, download;
} fd_odamex_options;

/* One run per module load, on a native worker. Callback data is borrowed for
   the duration of the call; callbacks must not throw or call into Lua. */
FD_EXPORT int fd_odamex_run(const fd_odamex_host *host, const fd_odamex_options *options);

#ifdef __cplusplus
}
#endif
#endif
