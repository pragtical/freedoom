/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <setjmp.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine_api.h"
#include "framebuffer.h"
#include "audio_transport.h"
#include "network.h"
#include "content.h"
#include "woof.h"
#include "d_main.h"
#include "d_event.h"
#include "doomkeys.h"
#include "doomstat.h"
#include "i_exit.h"
#include "m_argv.h"
#include "s_sound.h"
#include "truecolor.h"
#define FD_MEMORY_IMPLEMENTATION
#include "engine_memory.h"
void D_DoomMain(void);

struct fd_session {
    SDL_Thread *thread;
    SDL_Mutex *mutex;
    fd_reader *reader;
    fd_audio *audio;
    fd_network network;
    _Atomic int status;
    _Atomic int requested_truecolor;
    _Atomic bool stopping;
    unsigned char input[16384];
    unsigned input_read, input_length;
    bool requested_pause, requested_frame;
    uint32_t requested_size;
    char log[8192];
    unsigned log_length;
    char *directory, *state, *iwad;
    int sound;
};
static _Atomic int occupied;
static fd_session *active;
fd_audio *fd_session_audio(fd_session *session) { return session ? session->audio : NULL; }
fd_audio *fd_current_audio(void) { return active->audio; }
fd_network *fd_session_network(fd_session *session) { return session ? &session->network : NULL; }
fd_network *fd_current_network(void) { return &active->network; }
const char *fd_current_iwad(void) { return active->iwad; }
const char *fd_session_iwad(fd_session *session) { return session ? session->iwad : NULL; }
const char *fd_network_master(void) { return active->network.options.master; }
unsigned fd_network_options_size(void) { return sizeof(fd_net_options); }
unsigned fd_network_state_size(void) { return sizeof(fd_net_state); }
static jmp_buf unwind;
static bool suspended, frame_requested;
static uint64_t suspended_at, suspended_time, deadline;
static bool exiting;
unsigned fd_view_width = 960, fd_view_height = 720;

void fd_engine_exit(int code) { longjmp(unwind, code ? 2 : 1); }
void fd_begin_exit(void) { exiting = true; }
void fd_check_stop(void) {
    if (!exiting && atomic_load(&active->stopping)) I_SafeExit(0);
}
const char *fd_base_path(void) { return active->directory; }
char *fd_pref_path(void) { return fd_engine_strdup(active->state); }
void fd_engine_log(const char *message) {
    if (!active) return;
    size_t length = strlen(message);
    if (length > sizeof(active->log) - 1) { message += length - sizeof(active->log) + 1; length = sizeof(active->log) - 1; }
    fd_mutex_lock(active->mutex);
    size_t keep = active->log_length;
    if (keep + length + 1 > sizeof(active->log)) {
        keep = sizeof(active->log) - length - 1;
        memmove(active->log, active->log + active->log_length - keep, keep);
    }
    memcpy(active->log + keep, message, length);
    active->log[keep + length] = '\n'; active->log_length = keep + length + 1;
    fd_mutex_unlock(active->mutex);
}
unsigned fd_read_log(fd_session *session, char *buffer, unsigned capacity) {
    if (!session || !buffer || !capacity) return 0;
    fd_mutex_lock(session->mutex);
    unsigned count = session->log_length < capacity ? session->log_length : capacity;
    memcpy(buffer, session->log, count);
    memmove(session->log, session->log + count, session->log_length - count);
    session->log_length -= count;
    fd_mutex_unlock(session->mutex); return count;
}
Uint64 fd_performance_counter(void) {
    return (suspended ? suspended_at : fd_clock_ns()) - suspended_time;
}
static unsigned translate_key(unsigned raw) {
    switch (raw) {
        case 0xa3: return KEY_RCTRL;
        case 0xb6: return KEY_RSHIFT;
        case 0xa2: return ' ';
        case 0xd7: return KEY_F11;
        default: return raw >= 0xbb && raw <= 0xc4 ? KEY_F1 + raw - 0xbb : raw;
    }
}
static void pump_input(void) {
    fd_check_stop();
    fd_mutex_lock(active->mutex);
    bool pause = active->requested_pause;
    if (active->network.options.mode != FD_NET_OFFLINE) pause = false;
    frame_requested |= active->requested_frame;
    active->requested_frame = false;
    uint32_t size = active->requested_size;
    fd_mutex_unlock(active->mutex);
    if (size) { fd_view_width = size & 65535; fd_view_height = size >> 16; }
    if (pause != suspended) {
        if (pause) { suspended_at = fd_clock_ns(); S_PauseSound(); S_PauseMusic(); }
        else {
            suspended_time += fd_clock_ns() - suspended_at;
            if (!paused) S_ResumeSound();
            S_ResumeMusic();
        }
        suspended = pause;
    }
}
void fd_poll_game_input(void) {
    unsigned key_events = 0;
    int mouse_x = 0, mouse_y = 0;
    for (unsigned count = 0; count < 512 && key_events < 32; count++) {
        unsigned char packet[4];
        fd_mutex_lock(active->mutex);
        if (!active->input_length) { fd_mutex_unlock(active->mutex); break; }
        memcpy(packet, active->input + active->input_read, 4);
        active->input_read = (active->input_read + 4) % sizeof(active->input);
        active->input_length -= 4; fd_mutex_unlock(active->mutex);
        if (packet[0] == 'K') {
            event_t event = {0}; event.type = packet[1] ? ev_keydown : ev_keyup;
            event.data1.i = translate_key(packet[2]);
            event.data2.i = packet[1] && event.data1.i < 128 ? event.data1.i : 0;
            D_PostEvent(&event);
            key_events++;
            if (packet[1] && event.data1.i >= 32 && event.data1.i < 127) {
                event.type = ev_text; D_PostEvent(&event); key_events++;
            }
        } else if (packet[0] == 'M' && !menuactive) {
            mouse_x += (int8_t)packet[1]; mouse_y += (int8_t)packet[2];
        }
    }
    if (mouse_x || mouse_y) {
        event_t event = {0}; event.type = ev_mouse;
        event.data1.f = mouse_x; event.data2.f = mouse_y; D_PostEvent(&event);
    }
}
void fd_wait_active(void) {
    pump_input();
    fd_network_update();
    fd_pump_music();
    while (suspended) { fd_sleep_ns(5000000); pump_input(); }
}
bool fd_wait_frame(void) {
    if (!deadline) deadline = fd_clock_ns() + SDL_NS_PER_SECOND / TICRATE;
    for (;;) {
        fd_wait_active();
        uint64_t now = fd_clock_ns();
        if (frame_requested || now >= deadline) {
            frame_requested = false; deadline = now + SDL_NS_PER_SECOND / TICRATE; return true;
        }
        fd_sleep_ns(1000000);
    }
}
void fd_apply_truecolor(void) {
    if (atomic_load_explicit(&active->requested_truecolor, memory_order_relaxed) >= 0)
        fd_truecolor_setting = atomic_exchange(&active->requested_truecolor, -1) != 0;
    fd_truecolor = fd_truecolor_setting;
}
bool fd_publish(const pixel_t *pixels, unsigned width, unsigned height, const SDL_Color *colors) {
    if (!pixels || !colors || !width || width > FD_MAX_WIDTH || !height || height > FD_MAX_HEIGHT) I_SafeExit(-1);
    fd_reader *reader = active->reader;
    if (!fd_mutex_trylock(reader->mutex)) return false;
    fd_frame *frame = reader->frame;
    fd_copy_colors(frame->pixels, pixels, width * height, colors);
    frame->width = width; frame->height = height;
    if (++frame->serial == 0) ++frame->serial;
    fd_mutex_unlock(reader->mutex); atomic_store(&active->status, FD_RUNNING); return true;
}
static int run_engine(void *data) {
    active = data;
    exiting = false;
    suspended = frame_requested = false; suspended_at = suspended_time = deadline = 0;
    fd_view_width = 960; fd_view_height = 720;
    size_t length = strlen(active->state) + 16;
    char *config = malloc(length);
    if (!config) { atomic_store(&active->status, FD_FAILED); return 0; }
    snprintf(config, length, "%s/woof.cfg", active->state);
    char port[8], map[8], episode[8], skill[8], address[80], description[96];
    fd_net_options *options = &active->network.options;
    fd_iwad game;
    if (!fd_iwad_info(active->iwad, &game)) {
        fd_engine_log("Cannot identify the selected IWAD");
        free(config); atomic_store(&active->status, FD_FAILED); return 0;
    }
    snprintf(port, sizeof(port), "%d", options->port);
    snprintf(map, sizeof(map), "%d", options->map);
    snprintf(episode, sizeof(episode), "%d", options->episode);
    snprintf(skill, sizeof(skill), "%d", options->skill);
    snprintf(address, sizeof(address), "%s:%d", options->address, options->port);
    const char *modes[] = {"Co-op", "Deathmatch", "Deathmatch 2.0", "Deathmatch 3.0"};
    snprintf(description, sizeof(description), "%s - %s", options->name, modes[options->deathmatch]);
    char *arguments[32] = {"freedoom", "-iwad", active->iwad, "-config", config,
        "-save", active->state, "-noautoload"};
    myargc = 8; myargv = arguments;
    if (!active->sound) arguments[myargc++] = "-nosound";
    if (options->mode) {
        arguments[myargc++] = "-port"; arguments[myargc++] = port;
        if (options->mode == FD_NET_HOST) {
            arguments[myargc++] = "-privateserver";
            arguments[myargc++] = "-servername"; arguments[myargc++] = description;
        } else if (options->mode == FD_NET_JOIN || options->mode == FD_NET_INTERNET_JOIN) {
            arguments[myargc++] = "-connect"; arguments[myargc++] = address;
        }
        if (options->mode == FD_NET_HOST || options->mode == FD_NET_JOIN || options->mode == FD_NET_INTERNET_JOIN) {
            arguments[myargc++] = "-complevel"; arguments[myargc++] = "vanilla";
            arguments[myargc++] = "-skill"; arguments[myargc++] = skill;
            arguments[myargc++] = "-warp";
            if (game.gamemode != 2) arguments[myargc++] = episode;
            arguments[myargc++] = map;
            if (options->deathmatch) arguments[myargc++] = options->deathmatch == 1
                ? "-deathmatch" : options->deathmatch == 2 ? "-altdeath" : "-dm3";
        }
    }
    int result = setjmp(unwind);
    if (!result) { fd_capture_engine_state(); fd_network_initialize(); D_DoomMain(); result = 1; }
    fd_release_engine_state();
    free(config);
    atomic_store(&active->status, result == 1 ? FD_STOPPED : FD_FAILED);
    return 0;
}
fd_session *fd_start(const char *directory, const char *state, const char *iwad, int sound) {
    return fd_start_network(directory, state, iwad, sound, NULL);
}
bool fd_network_address(const char *text, unsigned default_port, char output[64]) {
    const char *part = text;
    unsigned octets[4];
    for (unsigned i = 0; i < 4; i++) {
        if (*part < '0' || *part > '9') return false;
        char *end;
        unsigned long value = strtoul(part, &end, 10);
        if (value > 255 || (i < 3 && *end != '.')) return false;
        octets[i] = value; part = end + (i < 3);
    }
    unsigned long port = default_port;
    if (*part == ':') {
        if (part[1] < '0' || part[1] > '9') return false;
        char *end; port = strtoul(part + 1, &end, 10); part = end;
    }
    if (*part || port < 1 || port > 65535) return false;
    snprintf(output, 64, "%u.%u.%u.%u:%lu", octets[0], octets[1], octets[2], octets[3], port);
    return true;
}
fd_session *fd_start_network(const char *directory, const char *state, const char *iwad,
                            int sound, const fd_net_options *options) {
    fd_net_options selected = {0};
    if (options) selected = *options;
    selected.address[sizeof(selected.address) - 1] = selected.name[sizeof(selected.name) - 1] = 0;
    selected.master[sizeof(selected.master) - 1] = 0;
    if (selected.mode < FD_NET_OFFLINE || selected.mode > FD_NET_INTERNET_JOIN
        || selected.deathmatch < 0 || selected.deathmatch > 3) return NULL;
    if (!selected.port) selected.port = 2342;
    if (!selected.episode) selected.episode = 1;
    if (!selected.map) selected.map = 1;
    if (!selected.skill) selected.skill = 3;
    if (selected.port < 1 || selected.port > 65535 || selected.map < 1 || selected.map > 32
        || selected.episode < 1 || selected.episode > 4 || selected.skill < 1 || selected.skill > 5) return NULL;
    if (!selected.name[0]) strcpy(selected.name, "Player");
    selected.name[29] = 0;
    if (selected.mode == FD_NET_INTERNET || selected.mode == FD_NET_INTERNET_JOIN) {
        char master[64];
        if (!fd_network_address(selected.master, 2342, master)) return NULL;
        strcpy(selected.master, master);
    } else selected.master[0] = 0;
    if (selected.mode == FD_NET_JOIN || selected.mode == FD_NET_INTERNET_JOIN) {
        if (!strcmp(selected.address, "localhost")) strcpy(selected.address, "127.0.0.1");
        char address[64];
        if (strchr(selected.address, ':') || !fd_network_address(selected.address, selected.port, address)) return NULL;
        *strchr(address, ':') = 0;
        strcpy(selected.address, address);
    }
    if (!directory || !state || !iwad || atomic_exchange(&occupied, 1)) return NULL;
    fd_session *session = calloc(1, sizeof(*session));
    if (!session) { atomic_store(&occupied, 0); return NULL; }
    atomic_init(&session->requested_truecolor, -1);
    session->directory = malloc(strlen(directory) + 2);
    if (session->directory) sprintf(session->directory, "%s/", directory);
    session->state = strdup(state); session->iwad = strdup(iwad); session->sound = sound;
    session->mutex = fd_mutex_create(); session->reader = fd_create(); session->audio = fd_audio_create();
    session->network.mutex = session->mutex;
    session->network.options = selected;
    session->network.state.mode = selected.mode;
    session->network.state.phase = selected.mode ? FD_NET_CONNECTING : FD_NET_NONE;
    if (!session->directory || !session->state || !session->iwad || !session->mutex || !session->reader || !session->audio) {
        fd_close(session); return NULL;
    }
    session->thread = fd_thread_create(run_engine, session);
    if (!session->thread) { fd_close(session); return NULL; }
    return session;
}
int fd_status(fd_session *session) { return session ? atomic_load(&session->status) : FD_STOPPED; }
int fd_set_truecolor(fd_session *session, int enabled) {
    if (!session || fd_status(session) >= FD_STOPPED) return 0;
    atomic_store(&session->requested_truecolor, enabled != 0);
    return 1;
}
int fd_input(fd_session *session, const unsigned char *packets, unsigned length) {
    if (!session || !packets || length % 4 || fd_status(session) >= FD_STOPPED) return 0;
    fd_mutex_lock(session->mutex);
    /* Controls coalesce separately, so a full key queue cannot prevent resume
       or shutdown while game input is waiting for the next simulation tick. */
    for (unsigned i = 0; i < length; i += 4) {
        const unsigned char *p = packets + i;
        if (p[0] == 'Q') atomic_store(&session->stopping, true);
        else if (p[0] == 'P') session->requested_pause = p[1] != 0;
        else if (p[0] == 'F' && p[1]) session->requested_frame = true;
        else if (p[0] == 'R') {
            unsigned width = p[1] | ((p[2] & 15) << 8), height = (p[2] >> 4) | (p[3] << 4);
            if (width >= 320 && width <= FD_MAX_WIDTH && height >= 240 && height <= FD_MAX_HEIGHT
                && width * 3 >= height * 4 && width * 10 <= height * 36)
                session->requested_size = width | height << 16;
        }
    }
    unsigned count = 0;
    for (; count < length; count += 4) {
        if (packets[count] != 'K' && packets[count] != 'M') continue;
        if (session->input_length == sizeof(session->input)) break;
        unsigned tail = (session->input_read + session->input_length) % sizeof(session->input);
        memcpy(session->input + tail, packets + count, 4); session->input_length += 4;
    }
    fd_mutex_unlock(session->mutex); return count;
}
void fd_stop(fd_session *session) { if (session) atomic_store(&session->stopping, true); }
void fd_close(fd_session *session) {
    if (!session) return;
    fd_stop(session); fd_thread_join(session->thread, NULL);
    fd_destroy(session->reader); fd_mutex_destroy(session->mutex);
    fd_audio_destroy(session->audio);
    free(session->directory); free(session->state); free(session->iwad); free(session);
    active = NULL; atomic_store(&occupied, 0);
}
FD_EXPORT fd_reader *fd_frame_reader(fd_session *session) { return session ? session->reader : NULL; }
