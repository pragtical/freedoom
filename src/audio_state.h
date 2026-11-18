/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef FD_AUDIO_STATE_H
#define FD_AUDIO_STATE_H

#define FD_AUDIO_CHANNELS 32
#define FD_MUSIC_RATE 49716
#define FD_MUSIC_CAPACITY 3968
typedef struct {
    unsigned generation, sample, playing, paused, looping;
    float gain, pan, rate, offset;
} fd_voice_state;

#endif
