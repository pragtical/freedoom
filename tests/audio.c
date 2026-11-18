/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "audio_transport.h"
#define CHECK(test) do { if (!(test)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test); exit(1); } } while (0)
static fd_audio *current;
fd_audio *fd_current_audio(void) { return current; }
fd_audio *fd_session_audio(fd_session *session) { return (fd_audio *)session; }
int main(void) {
    current = fd_audio_create(); CHECK(current);
    fd_session *session = (fd_session *)current;
    unsigned char bytes[] = {128, 40, 210, 128}, copy[4] = {0};
    unsigned sample = fd_cache_sample(10, bytes, sizeof(bytes), 11025, 0, false);
    CHECK(sample == 1 && fd_find_sample(10) == sample && !fd_find_sample(11));
    unsigned rate, encoded;
    CHECK(fd_audio_sample(session, sample, NULL, 0, &rate, &encoded) == 4 && rate == 11025 && !encoded);
    CHECK(fd_audio_sample(session, sample, copy, 3, &rate, &encoded) == 4 && copy[0] == 0);
    CHECK(fd_audio_sample(session, sample, copy, 4, &rate, &encoded) == 4 && !memcmp(copy, bytes, 4));
    CHECK(!fd_audio_sample(session, 2, NULL, 0, NULL, NULL));
    fd_voice_state voice = {.generation = 1, .sample = sample, .playing = 1, .gain = .5f, .pan = -.5f, .rate = 1.2f};
    fd_set_voice(0, &voice);
    fd_voice_finished(session, 0, 0); CHECK(fd_get_voice(0).playing);
    fd_voice_progress(session, 0, 1, .25f); CHECK(fd_get_voice(0).offset == .25f);
    fd_set_voice(0, &voice); CHECK(fd_get_voice(0).offset == .25f);
    fd_voice_finished(session, 0, 1); CHECK(!fd_get_voice(0).playing);
    fd_set_voice(0, &voice); CHECK(!fd_get_voice(0).playing);
    voice.generation = 2; fd_set_voice(0, &voice);
    fd_voice_finished(session, 0, 1); CHECK(fd_get_voice(0).playing);
    fd_voice_progress(session, 0, 1, 9); CHECK(fd_get_voice(0).offset == 0);
    fd_audio_state state;
    CHECK(fd_audio_snapshot(session, &state) && state.voices[0].generation == 2);
    int16_t music[FD_MUSIC_CAPACITY * 2], output[FD_MUSIC_CAPACITY * 2];
    for (unsigned i = 0; i < FD_MUSIC_CAPACITY * 2; i++) music[i] = (int16_t)i;
    fd_set_music(false, .5f, true);
    CHECK(fd_audio_snapshot(session, &state));
    unsigned generation = state.music_generation;
    fd_push_music(music, FD_MUSIC_CAPACITY);
    CHECK(fd_music_read(session, output, FD_MUSIC_CAPACITY, generation - 1) == 0);
    CHECK(fd_music_read(session, output, FD_MUSIC_CAPACITY, generation) == FD_MUSIC_CAPACITY);
    CHECK(!memcmp(music, output, sizeof(music)));
    fd_push_music(music, FD_MUSIC_CAPACITY); fd_push_music(music, 512);
    CHECK(fd_audio_snapshot(session, &state) && state.music_generation != generation);
    CHECK(fd_music_read(session, output, FD_MUSIC_CAPACITY, state.music_generation) == 512);
    fd_set_music(true, .5f, true);
    CHECK(fd_audio_snapshot(session, &state) && state.music_paused);
    CHECK(!fd_music_read(session, output, FD_MUSIC_CAPACITY, state.music_generation));
    fd_audio_disable(session); CHECK(!fd_audio_enabled());
    voice.generation++; fd_set_voice(0, &voice); CHECK(!fd_get_voice(0).playing);
    fd_push_music(music, 512);
    CHECK(fd_audio_snapshot(session, &state) && state.music_paused);
    CHECK(!fd_music_read(session, output, FD_MUSIC_CAPACITY, state.music_generation));
    fd_audio_destroy(current); fd_audio_destroy(NULL);
    puts("PASS audio samples, generations, progress, bounded PCM and mute");
}
