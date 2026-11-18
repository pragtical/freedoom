/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <math.h>
#include <string.h>
#include "audio_transport.h"
#include "i_sound.h"
#include "i_oalsound.h"
#include "i_oalequalizer.h"
#include "i_oalstream.h"
#include "i_timer.h"
#include "m_config.h"
#include "sounds.h"
#include "w_wad.h"
#include "z_zone.h"
#include "opl.h"
#include "libebur128/ebur128.h"

boolean auto_gain, oal_use_doppler;
static float channel_gain[FD_AUDIO_CHANNELS], channel_volume[FD_AUDIO_CHANNELS];
static boolean music_ready, song_open, music_playing, music_paused;
static float music_gain = 1;
static int opl_gain;
static ebur128_state *music_meter;
static float music_auto_gain = 1;
static uint64_t music_frames;
static unsigned meter_frames;
static uint64_t next_music_sample;
static void noop(void) {}
static boolean unavailable(void) { return false; }
static const char **no_devices(void) { return NULL; }
static boolean no_music(int device) { (void)device; return false; }
static void *no_song(void *data, int size) { (void)data; (void)size; return NULL; }
static void bind_music(void) {
    BIND_BOOL_MUSIC(auto_gain, true, "Auto Gain");
    BIND_NUM_MUSIC(opl_gain, 0, -20, 20, "[OPL3 Emulation] Gain [dB]");
    stream_opl_module.BindVariables();
}
static const char **devices(void) { return stream_opl_module.I_DeviceList(); }
static const char *music_format(void) { return stream_opl_module.I_MusicFormat(); }
static midiplayertype_t player_type(void) { return midiplayer_opl; }
boolean I_OAL_InitStream(void) { return true; }
void I_OAL_ShutdownStream(void) {}
static void publish_music(bool clear) {
    float gain = auto_gain ? music_auto_gain : powf(10, opl_gain / 20.f);
    fd_set_music(music_paused || !music_playing, music_gain * gain, clear);
}

/* Use Woof's -23 LUFS target, not the raw (usually quiet) OPL signal. The
 * histogram keeps long tracks bounded; the cumulative peak retains headroom. */
static void meter_music(const int16_t *pcm, unsigned frames) {
    if (!music_meter || ebur128_add_frames_short(music_meter, pcm, frames) != EBUR128_SUCCESS) return;
    music_frames += frames;
    meter_frames += frames;
    if (music_frames >= 2 * FD_MUSIC_RATE / 5 && meter_frames >= FD_MUSIC_RATE / 10) {
        meter_frames = 0;
        double momentary, shortterm, global, relative;
        if (ebur128_loudness_momentary(music_meter, &momentary) == EBUR128_SUCCESS
            && isfinite(momentary) && momentary > -70) {
            double loudness = momentary;
            bool adjust = true;
            if (music_frames >= 3 * FD_MUSIC_RATE) {
                if (ebur128_loudness_shortterm(music_meter, &shortterm) == EBUR128_SUCCESS
                    && ebur128_loudness_global(music_meter, &global) == EBUR128_SUCCESS
                    && ebur128_relative_threshold(music_meter, &relative) == EBUR128_SUCCESS
                    && isfinite(shortterm) && isfinite(global) && momentary > relative)
                    loudness = (.1 * momentary + shortterm + global) / 2.1;
                else adjust = false;
            }
            if (adjust) music_auto_gain = fminf(10, powf(10, (-23 - loudness) / 20));
        }
    }
    double left = 0, right = 0;
    if (ebur128_sample_peak(music_meter, 0, &left) == EBUR128_SUCCESS
        && ebur128_sample_peak(music_meter, 1, &right) == EBUR128_SUCCESS) {
        double peak = fmax(left, right);
        if (peak > 0) music_auto_gain = fminf(music_auto_gain, .95 / peak);
    }
    publish_music(false);
}
static void music_stop(void *handle) {
    (void)handle;
    music_playing = false;
    publish_music(true);
}
static void unregister_song(void *handle) {
    music_stop(handle);
    if (song_open) stream_opl_module.I_CloseStream();
    if (music_meter) ebur128_destroy(&music_meter);
    song_open = false;
}
static void shutdown_music(void) {
    unregister_song(NULL);
    if (music_ready) stream_opl_module.I_ShutdownStream();
    music_ready = false;
}
static boolean music_init(int device) {
    shutdown_music();
    music_ready = stream_opl_module.I_InitStream(device);
    return music_ready;
}
static void volume_music(int volume) {
    music_gain = (float)CLAMP(volume, 0, 15) / 15;
    publish_music(false);
}
static void pause_music(void *handle) {
    (void)handle; music_paused = true; publish_music(true);
}
static void resume_music(void *handle) {
    (void)handle; music_paused = false; next_music_sample = fd_clock_ns();
    publish_music(true);
}
static void *register_song(void *data, int size) {
    if (!music_ready || (!IsMid(data, size) && !IsMus(data, size))) return NULL;
    unregister_song(NULL);
    ALenum format; ALsizei frequency, frame_size;
    song_open = stream_opl_module.I_OpenStream(data, size, &format, &frequency, &frame_size);
    if (!song_open || format != AL_FORMAT_STEREO16 || frequency != FD_MUSIC_RATE || frame_size != 4) {
        if (song_open) stream_opl_module.I_CloseStream();
        song_open = false; return NULL;
    }
    return &song_open;
}
static void play_music(void *handle, boolean loop) {
    if (!handle || !song_open) return;
    if (music_meter) ebur128_destroy(&music_meter);
    music_meter = ebur128_init(2, FD_MUSIC_RATE,
        EBUR128_MODE_S | EBUR128_MODE_I | EBUR128_MODE_SAMPLE_PEAK | EBUR128_MODE_HISTOGRAM);
    music_auto_gain = 1; music_frames = meter_frames = 0;
    stream_opl_module.I_PlayStream(loop);
    music_playing = true; music_paused = false; next_music_sample = fd_clock_ns();
    publish_music(true);
}
void fd_pump_music(void) {
    if (!fd_audio_enabled()) { music_playing = false; return; }
    if (!music_ready || !song_open || !music_playing || music_paused) return;
    uint64_t now = fd_clock_ns();
    int16_t pcm[512 * 2];
    for (unsigned slice = 0; slice < 16 && next_music_sample < now + UINT64_C(70000000); slice++) {
        int frames = stream_opl_module.I_FillStream(pcm, 512);
        if (frames <= 0) { music_stop(NULL); return; }
        meter_music(pcm, frames);
        fd_push_music(pcm, frames);
        next_music_sample += (uint64_t)frames * SDL_NS_PER_SECOND / FD_MUSIC_RATE;
    }
}
music_module_t music_oal_module = {music_init, shutdown_music, volume_music,
    pause_music, resume_music, register_song, play_music, music_stop, unregister_song,
    devices, bind_music, player_type, music_format};
music_module_t music_mid_module = {.I_InitMusic = no_music, .I_ShutdownMusic = noop,
    .I_RegisterSong = no_song, .I_DeviceList = no_devices, .I_BindVariables = noop};
const sound_module_t sound_3d_module = {.InitSound = unavailable};

boolean I_OAL_InitSound(int module) {
    (void)module;
    for (unsigned i = 0; i < FD_AUDIO_CHANNELS; i++) channel_gain[i] = 1;
    return true;
}
boolean I_OAL_ReinitSound(int module) { return I_OAL_InitSound(module); }
boolean I_OAL_AllowReinitSound(void) { return false; }
boolean I_OAL_CacheSound(sfxinfo_t *sfx) {
    if (!fd_audio_enabled()) return false;
    int lump = I_GetSfxLumpNum(sfx); if (lump < 0) return false;
    unsigned id = fd_find_sample(lump);
    if (!id) {
        int length = W_LumpLength(lump); if (length <= 8) return false;
        const byte *bytes = W_CacheLumpNum(lump, PU_CACHE);
        if (bytes[0] == 3 && bytes[1] == 0) {
            unsigned rate = bytes[2] | (unsigned)bytes[3] << 8;
            uint32_t size = bytes[4] | (uint32_t)bytes[5] << 8 | (uint32_t)bytes[6] << 16 | (uint32_t)bytes[7] << 24;
            if (!rate || size > (unsigned)length - 8 || size <= 32) return false;
            id = fd_cache_sample(lump, bytes + 24, size - 32, rate, 0, sfx->looping);
            sfx->length = (float)(size - 32) / rate;
        } else {
            id = fd_cache_sample(lump, bytes, length, 0, 1, sfx->looping);
        }
    }
    sfx->length = fd_sample_length(id);
    sfx->buffer = id; sfx->cached = id != 0; return sfx->cached;
}
boolean I_OAL_StartSound(int channel, sfxinfo_t *sfx, const sfxparams_t *params) {
    if (channel < 0 || channel >= FD_AUDIO_CHANNELS || !sfx->buffer) return false;
    fd_voice_state voice = fd_get_voice(channel);
    ++voice.generation; if (!voice.generation) ++voice.generation;
    voice.sample = sfx->buffer; voice.playing = true; voice.paused = false;
    voice.looping = sfx->looping; voice.rate = params->pitch;
    voice.offset = params->offset;
    channel_gain[channel] = 1;
    channel_volume[channel] = (float)params->volume / 127;
    voice.gain = channel_volume[channel];
    voice.pan = CLAMP((params->separation - 128) / 127.f, -1, 1);
    fd_set_voice(channel, &voice); return true;
}
void I_OAL_StopSound(int channel) {
    fd_voice_state voice = fd_get_voice(channel); voice.playing = false; fd_set_voice(channel, &voice);
}
void I_OAL_PauseSound(int channel) {
    fd_voice_state voice = fd_get_voice(channel); voice.paused = true; fd_set_voice(channel, &voice);
}
void I_OAL_ResumeSound(int channel) {
    fd_voice_state voice = fd_get_voice(channel); voice.paused = false; fd_set_voice(channel, &voice);
}
boolean I_OAL_SoundIsPlaying(int channel) { return fd_get_voice(channel).playing; }
boolean I_OAL_SoundIsPaused(int channel) { return fd_get_voice(channel).paused; }
void I_OAL_SetGain(int channel, float gain) {
    if (channel < 0 || channel >= FD_AUDIO_CHANNELS) return;
    channel_gain[channel] = gain;
    fd_voice_state voice = fd_get_voice(channel); voice.gain = gain * channel_volume[channel]; fd_set_voice(channel, &voice);
}
void I_OAL_SetVolume(int channel, int volume) {
    if (channel < 0 || channel >= FD_AUDIO_CHANNELS) return;
    channel_volume[channel] = volume / 127.f; I_OAL_SetGain(channel, channel_gain[channel]);
}
void I_OAL_SetPan(int channel, int separation) {
    fd_voice_state voice = fd_get_voice(channel);
    voice.pan = CLAMP((separation - 128) / 127.f, -1, 1); fd_set_voice(channel, &voice);
}
float I_OAL_GetOffset(int channel) {
    return fd_get_voice(channel).offset;
}
void I_OAL_ShutdownSound(void) { for (unsigned i = 0; i < FD_AUDIO_CHANNELS; i++) I_OAL_StopSound(i); }
void I_OAL_ShutdownModule(void) {}
void I_OAL_DeferUpdates(void) {}
void I_OAL_ProcessUpdates(void) {}
void I_OAL_SetResampler(void) {}
void I_OAL_MuteSound(void) { for (unsigned i = 0; i < FD_AUDIO_CHANNELS; i++) I_OAL_PauseSound(i); }
void I_OAL_UnmuteSound(void) { for (unsigned i = 0; i < FD_AUDIO_CHANNELS; i++) I_OAL_ResumeSound(i); }
void I_OAL_BindSoundVariables(void) {}
const char **I_OAL_GetResamplerStrings(void) { return NULL; }
boolean I_OAL_EqualizerInitialized(void) { return false; }
boolean I_OAL_CustomEqualizer(void) { return false; }
void I_OAL_ShutdownEqualizer(void) {}
void I_OAL_InitEqualizer(void) {}
void I_OAL_SetEqualizer(void) {}
void I_OAL_EqualizerPreset(void) {}
void I_BindEqualizerVariables(void) {}
