/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "audio_transport.h"
struct sample {
    int lump;
    unsigned char *bytes;
    unsigned size, rate, encoded;
};
struct fd_audio {
    SDL_Mutex *mutex;
    fd_audio_state state;
    struct sample samples[4096];
    unsigned sample_count;
    size_t sample_bytes;
    int16_t music[FD_MUSIC_CAPACITY * 2];
    unsigned read, count;
    _Atomic bool enabled;
};
fd_audio *fd_audio_create(void) {
    fd_audio *audio = calloc(1, sizeof(*audio)); if (!audio) return NULL;
    audio->mutex = fd_mutex_create();
    if (!audio->mutex) { free(audio); return NULL; }
    audio->state.music_gain = 1;
    audio->state.music_paused = true;
    atomic_store(&audio->enabled, true);
    return audio;
}
bool fd_audio_enabled(void) { return atomic_load(&fd_current_audio()->enabled); }
void fd_audio_disable(fd_session *session) {
    fd_audio *audio = fd_session_audio(session); if (!audio) return;
    atomic_store(&audio->enabled, false);
    fd_mutex_lock(audio->mutex);
    for (unsigned i = 0; i < FD_AUDIO_CHANNELS; i++) audio->state.voices[i].playing = false;
    audio->state.music_paused = true; ++audio->state.music_generation;
    audio->read = audio->count = 0;
    fd_mutex_unlock(audio->mutex);
}
void fd_audio_destroy(fd_audio *audio) {
    if (!audio) return;
    for (unsigned i = 0; i < audio->sample_count; i++) free(audio->samples[i].bytes);
    fd_mutex_destroy(audio->mutex); free(audio);
}
unsigned fd_find_sample(int lump) {
    fd_audio *audio = fd_current_audio();
    for (unsigned i = 0; i < audio->sample_count; i++)
        if (audio->samples[i].lump == lump) return i + 1;
    return 0;
}
float fd_sample_length(unsigned id) {
    fd_audio *audio = fd_current_audio();
    if (!id || id > audio->sample_count || !audio->samples[id - 1].rate) return 0;
    return (float)audio->samples[id - 1].size / audio->samples[id - 1].rate;
}
unsigned fd_cache_sample(int lump, const void *bytes, unsigned size, unsigned rate, unsigned encoded, bool looping) {
    fd_audio *audio = fd_current_audio();
    if (!size || size > 16 * 1024 * 1024 || audio->sample_count == 4096
        || audio->sample_bytes + size > 128 * 1024 * 1024) return 0;
    unsigned char *copy = malloc(size); if (!copy) return 0;
    memcpy(copy, bytes, size);
    /* Match Woof's 1 ms DMX edge fade without modifying the cached WAD lump. */
    unsigned fade = rate / 1000;
    if (!encoded && !looping && fade && fade <= size) {
        if (copy[0] != 128)
            for (unsigned i = 0; i < fade; i++) copy[i] = ((int)copy[i] - 128) * (int)i / (int)fade + 128;
        if (copy[size - 1] != 128)
            for (unsigned i = 0; i < fade; i++) copy[size - i - 1] = ((int)copy[size - i - 1] - 128) * (int)i / (int)fade + 128;
    }
    fd_mutex_lock(audio->mutex);
    unsigned index = audio->sample_count++;
    audio->samples[index] = (struct sample){lump, copy, size, rate, encoded};
    audio->sample_bytes += size;
    fd_mutex_unlock(audio->mutex); return index + 1;
}
unsigned fd_audio_sample(fd_session *session, unsigned id, void *output, unsigned capacity,
                          unsigned *rate, unsigned *encoded) {
    fd_audio *audio = fd_session_audio(session); if (!audio || !id) return 0;
    if (!fd_mutex_trylock(audio->mutex)) return 0;
    unsigned size = 0;
    if (id <= audio->sample_count) {
        struct sample *sample = &audio->samples[id - 1];
        size = sample->size;
        if (rate) *rate = sample->rate;
        if (encoded) *encoded = sample->encoded;
        if (output && capacity >= size) memcpy(output, sample->bytes, size);
    }
    fd_mutex_unlock(audio->mutex); return size;
}
void fd_set_voice(unsigned channel, const fd_voice_state *voice) {
    fd_audio *audio = fd_current_audio();
    if (channel >= FD_AUDIO_CHANNELS) return;
    fd_mutex_lock(audio->mutex);
    fd_voice_state next = *voice;
    if (!atomic_load(&audio->enabled)) next.playing = false;
    if (next.generation == audio->state.voices[channel].generation) {
        if (!audio->state.voices[channel].playing) next.playing = false;
        next.offset = audio->state.voices[channel].offset;
    }
    audio->state.voices[channel] = next;
    fd_mutex_unlock(audio->mutex);
}
fd_voice_state fd_get_voice(unsigned channel) {
    fd_audio *audio = fd_current_audio(); fd_voice_state voice = {0};
    if (channel < FD_AUDIO_CHANNELS) {
        fd_mutex_lock(audio->mutex); voice = audio->state.voices[channel]; fd_mutex_unlock(audio->mutex);
    }
    return voice;
}
void fd_voice_finished(fd_session *session, unsigned channel, unsigned generation) {
    fd_audio *audio = fd_session_audio(session); if (!audio || channel >= FD_AUDIO_CHANNELS) return;
    fd_mutex_lock(audio->mutex);
    fd_voice_state *voice = &audio->state.voices[channel];
    if (voice->generation == generation) voice->playing = false;
    fd_mutex_unlock(audio->mutex);
}
void fd_voice_progress(fd_session *session, unsigned channel, unsigned generation, float position) {
    fd_audio *audio = fd_session_audio(session);
    if (!audio || channel >= FD_AUDIO_CHANNELS || !isfinite(position) || position < 0) return;
    fd_mutex_lock(audio->mutex);
    if (audio->state.voices[channel].generation == generation) audio->state.voices[channel].offset = position;
    fd_mutex_unlock(audio->mutex);
}
int fd_audio_snapshot(fd_session *session, fd_audio_state *state) {
    fd_audio *audio = fd_session_audio(session); if (!audio || !state || !fd_mutex_trylock(audio->mutex)) return 0;
    *state = audio->state; fd_mutex_unlock(audio->mutex); return 1;
}
void fd_set_music(unsigned paused, float gain, bool clear) {
    fd_audio *audio = fd_current_audio();
    fd_mutex_lock(audio->mutex);
    audio->state.music_paused = paused; audio->state.music_gain = gain;
    if (clear) { audio->read = audio->count = 0; ++audio->state.music_generation; }
    fd_mutex_unlock(audio->mutex);
}
void fd_push_music(const int16_t *pcm, unsigned frames) {
    fd_audio *audio = fd_current_audio();
    if (!frames || frames > FD_MUSIC_CAPACITY || !atomic_load(&audio->enabled)) return;
    fd_mutex_lock(audio->mutex);
    if (audio->count + frames > FD_MUSIC_CAPACITY) {
        audio->read = audio->count = 0; ++audio->state.music_generation;
    }
    for (unsigned i = 0; i < frames; i++) {
        unsigned tail = (audio->read + audio->count) % FD_MUSIC_CAPACITY;
        memcpy(audio->music + tail * 2, pcm + i * 2, sizeof(int16_t) * 2); ++audio->count;
    }
    fd_mutex_unlock(audio->mutex);
}
unsigned fd_music_read(fd_session *session, int16_t *output, unsigned frames, unsigned generation) {
    fd_audio *audio = fd_session_audio(session); if (!audio || !output || !frames || !fd_mutex_trylock(audio->mutex)) return 0;
    if (generation != audio->state.music_generation) { fd_mutex_unlock(audio->mutex); return 0; }
    if (frames > audio->count) frames = audio->count;
    for (unsigned i = 0; i < frames; i++) {
        memcpy(output + i * 2, audio->music + audio->read * 2, sizeof(int16_t) * 2);
        audio->read = (audio->read + 1) % FD_MUSIC_CAPACITY;
    }
    audio->count -= frames; fd_mutex_unlock(audio->mutex); return frames;
}
