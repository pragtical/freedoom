/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "odamex.h"
#include "i_music.h"
#include "i_sound.h"
#include "w_wad.h"
#include "z_zone.h"
#include "adlmidi.h"
#include "hooks.h"

namespace {
ADL_MIDIPlayer *song;
bool music_paused = true;
float sfx_gain = 1, music_gain = 1;
unsigned next_voice;
unsigned sample_fraction;
char loaded_sample;
}

std::string currentmusic;
EXTERN_CVAR(snd_crossover)
EXTERN_CVAR(snd_nomusic)
EXTERN_CVAR(snd_oplbank)
EXTERN_CVAR(snd_oplchips)
CVAR_FUNC_IMPL(snd_samplerate) {}
CVAR_FUNC_IMPL(snd_musicsystem) {}
CVAR_FUNC_IMPL(snd_nomusic) { if (var) I_StopSong(); }
CVAR_FUNC_IMPL(snd_oplcore) {}
CVAR_FUNC_IMPL(snd_oplpan) {}
CVAR_FUNC_IMPL(snd_oplchips) {}
CVAR_FUNC_IMPL(snd_oplbank) {}

void I_InitSound() {}
void I_ShutdownSound() { I_StopSong(); }
void I_SetChannels(int) {}
void I_SetSfxVolume(float gain) { sfx_gain = gain; }

void I_LoadSound(sfxinfo_struct *sfx)
{
    const auto *audio = fd_odamex_audio();
    if (!audio || !audio->cache_sample || sfx->data || sfx->lumpnum < 0) return;
    unsigned size = W_LumpLength(sfx->lumpnum);
    if (size < 8 || size > 16 * 1024 * 1024) return;
    const byte *data = static_cast<const byte *>(W_CacheLumpNum(sfx->lumpnum, PU_CACHE));
    unsigned rate = 0, encoded = 1;
    if (data[0] == 3 && data[1] == 0) {
        rate = static_cast<unsigned>(data[2]) | static_cast<unsigned>(data[3]) << 8;
        unsigned count = static_cast<unsigned>(data[4]) | static_cast<unsigned>(data[5]) << 8
            | static_cast<unsigned>(data[6]) << 16 | static_cast<unsigned>(data[7]) << 24;
        if (rate < 1000 || !count || count > size - 8) return;
        data += 8;
        size = count;
        if (size > 32u) { data += 16; size -= 32; }
        encoded = 0;
    }
    sfx->normal = audio->cache_sample(sfx->lumpnum, data, size, rate, encoded, false);
    if (sfx->normal) sfx->data = &loaded_sample;
}

static fd_voice_state voice(int handle)
{
    const auto *audio = fd_odamex_audio();
    if (!audio || !audio->get_voice || handle < 0 || handle >= FD_AUDIO_CHANNELS) return {};
    return audio->get_voice(handle);
}

static void levels(fd_voice_state &state, float volume, int separation)
{
    state.gain = std::clamp(sfx_gain * volume, 0.0f, 1.0f);
    state.pan = (std::clamp(separation, 0, 255) / 127.5f - 1.0f)
        * (snd_crossover ? -1.0f : 1.0f);
}

int I_StartSound(int id, float volume, int separation, int, bool loop)
{
    const auto *audio = fd_odamex_audio();
    if (!audio || !audio->set_voice || id < 0 || static_cast<size_t>(id) >= S_sfx.size()
        || !S_sfx[id].normal) return -1;
    for (unsigned i = 0; i < FD_AUDIO_CHANNELS; ++i) {
        unsigned channel = next_voice++ % FD_AUDIO_CHANNELS;
        fd_voice_state state = audio->get_voice(channel);
        if (state.playing) continue;
        ++state.generation;
        state.sample = S_sfx[id].normal;
        state.playing = true;
        state.paused = false;
        state.looping = loop;
        state.rate = 1;
        state.offset = 0;
        levels(state, volume, separation);
        audio->set_voice(channel, &state);
        return channel;
    }
    return -1;
}
void I_UpdateSoundParams(int handle, float volume, int separation, int)
{
    auto state = voice(handle);
    if (!state.playing) return;
    levels(state, volume, separation);
    fd_odamex_audio()->set_voice(handle, &state);
}
void I_StopSound(int handle)
{
    auto state = voice(handle);
    if (state.playing) {
        state.playing = false;
        fd_odamex_audio()->set_voice(handle, &state);
    }
}
static void pause_voice(int handle, bool paused)
{
    auto state = voice(handle);
    if (state.playing) {
        state.paused = paused;
        fd_odamex_audio()->set_voice(handle, &state);
    }
}
void I_PauseSound(int handle) { pause_voice(handle, true); }
void I_ResumeSound(int handle) { pause_voice(handle, false); }
int I_SoundIsPaused(int handle) { return voice(handle).paused; }
int I_SoundIsPlaying(int handle) { return voice(handle).playing; }

static void music_state(bool clear)
{
    const auto *audio = fd_odamex_audio();
    if (audio && audio->set_music) audio->set_music(music_paused, music_gain, clear);
}
void I_InitMusic(MusicSystemType) {}
void I_ShutdownMusic() { I_StopSong(); }
void I_SetMusicVolume(float gain) { music_gain = gain; music_state(false); }
void I_PauseSong() { music_paused = true; music_state(false); }
void I_ResumeSong() { music_paused = !song; music_state(false); }
void I_StopSong()
{
    if (song) adl_close(song);
    song = nullptr;
    music_paused = true;
    music_state(true);
}
void I_PlaySong(OByteSpan data, bool loop, int)
{
    I_StopSong();
    const auto *audio = fd_odamex_audio();
    if (!audio || !audio->push_music || snd_nomusic || data.size() < 4) return;
    if (memcmp(data.data(), "MUS\x1a", 4) && memcmp(data.data(), "MThd", 4)) return;
    song = adl_init(FD_MUSIC_RATE);
    if (!song) return;
    adl_setBank(song, std::clamp(snd_oplbank.asInt(), 0, static_cast<int>(adl_getBanksCount()) - 1));
    adl_setNumChips(song, std::clamp(snd_oplchips.asInt(), 1, 8));
    adl_setLoopEnabled(song, loop);
    if (adl_openData(song, data.data(), data.size()) < 0) { I_StopSong(); return; }
    music_paused = false;
    sample_fraction = 0;
    music_state(true);
}
void I_UpdateMusic()
{
    if (!song || music_paused) return;
    constexpr unsigned per_tick = FD_MUSIC_RATE / TICRATE;
    int16_t pcm[(per_tick + 1) * 2];
    sample_fraction += FD_MUSIC_RATE % TICRATE;
    unsigned frames = per_tick + sample_fraction / TICRATE;
    sample_fraction %= TICRATE;
    int count = adl_play(song, frames * 2, pcm);
    if (count > 0) fd_odamex_audio()->push_music(pcm, count / 2);
}
void I_ResetMidiVolume() {}
