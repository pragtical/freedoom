/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef FD_AUDIO_TRANSPORT_H
#define FD_AUDIO_TRANSPORT_H
#include "engine_api.h"
#include "platform/SDL3/SDL.h"
#include "audio_state.h"
typedef struct {
    fd_voice_state voices[FD_AUDIO_CHANNELS];
    unsigned music_generation, music_paused;
    float music_gain;
} fd_audio_state;
typedef struct fd_audio fd_audio;
fd_audio *fd_audio_create(void);
void fd_audio_destroy(fd_audio *audio);
fd_audio *fd_session_audio(fd_session *session);
fd_audio *fd_current_audio(void);
bool fd_audio_enabled(void);
unsigned fd_cache_sample(int lump, const void *bytes, unsigned size, unsigned rate, unsigned encoded, bool looping);
float fd_sample_length(unsigned id);
unsigned fd_find_sample(int lump);
void fd_set_voice(unsigned channel, const fd_voice_state *voice);
fd_voice_state fd_get_voice(unsigned channel);
void fd_set_music(unsigned paused, float gain, bool clear);
void fd_push_music(const int16_t *pcm, unsigned frames);
void fd_pump_music(void);
FD_EXPORT int fd_audio_snapshot(fd_session *session, fd_audio_state *state);
FD_EXPORT unsigned fd_audio_sample(fd_session *session, unsigned id, void *output, unsigned capacity,
                                    unsigned *rate, unsigned *encoded);
FD_EXPORT void fd_voice_finished(fd_session *session, unsigned channel, unsigned generation);
FD_EXPORT void fd_voice_progress(fd_session *session, unsigned channel, unsigned generation, float position);
FD_EXPORT unsigned fd_music_read(fd_session *session, int16_t *output, unsigned frames, unsigned generation);
FD_EXPORT void fd_audio_disable(fd_session *session);
#endif
