/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdatomic.h>
#include "engine_api.h"
#include "framebuffer.h"
#include "audio_transport.h"
#include "preview.h"
#include "loader.h"

struct fd_session {
    fd_odamex_options options;
    char *strings[6];
    fd_module module;
    SDL_Thread *thread;
    SDL_Mutex *mutex;
    fd_reader *reader;
    fd_audio *audio;
    _Atomic int status;
    _Atomic bool stopping, paused, online;
    _Atomic uint32_t size;
    unsigned char queue[16384];
    unsigned head, used, log_length;
    char log[8192];
    uint64_t paused_time, suspended_at;
};

static _Atomic bool occupied;
static fd_session *active;
fd_audio *fd_session_audio(fd_session *session) { return session ? session->audio : NULL; }
fd_audio *fd_current_audio(void) { return active->audio; }

static void log_message(void *data, const char *message)
{
    fd_session *session = data;
    unsigned length = (unsigned)strnlen(message, sizeof(session->log) - 1);
    fd_mutex_lock(session->mutex);
    unsigned keep = session->log_length;
    if (keep + length + 1 > sizeof(session->log)) {
        keep = sizeof(session->log) - length - 1;
        memmove(session->log, session->log + session->log_length - keep, keep);
    }
    memcpy(session->log + keep, message, length);
    session->log[keep + length] = '\n';
    session->log_length = keep + length + 1;
    fd_mutex_unlock(session->mutex);
}

static int stopping(void *data)
{
    fd_session *session = data;
    if (!session->options.endpoint && atomic_load(&session->paused)) {
        // Fetch release events once the game resumes; keep their original order.
        session->suspended_at = fd_clock_ns();
        while (!atomic_load(&session->stopping) && atomic_load(&session->paused))
            fd_sleep_ns(5000000);
        session->paused_time += fd_clock_ns() - session->suspended_at;
        session->suspended_at = 0;
    }
    return atomic_load(&session->stopping);
}

static uint64_t clock_now(void *data)
{
    fd_session *session = data;
    return (session->suspended_at ? session->suspended_at : fd_clock_ns()) - session->paused_time;
}

static uint32_t requested_size(void *data) { return atomic_load(&((fd_session *)data)->size); }

static int read_input(void *data, unsigned char packet[4])
{
    fd_session *session = data;
    fd_mutex_lock(session->mutex);
    bool available = session->used != 0;
    if (available) {
        memcpy(packet, session->queue + session->head, 4);
        session->head = (session->head + 4) % sizeof(session->queue);
        session->used -= 4;
    }
    fd_mutex_unlock(session->mutex);
    return available;
}

static void publish(void *data, const unsigned char *rgba, unsigned width, unsigned height, int online)
{
    fd_session *session = data;
    atomic_store(&session->online, online != 0);
    fd_reader *reader = session->reader;
    if (!width || width > FD_MAX_WIDTH || !height || height > FD_MAX_HEIGHT
        || !fd_mutex_trylock(reader->mutex)) return;
    memcpy(reader->frame->pixels, rgba, (size_t)width * height * 4);
    reader->frame->width = width;
    reader->frame->height = height;
    if (!++reader->frame->serial) ++reader->frame->serial;
    fd_mutex_unlock(reader->mutex);
    atomic_store(&session->status, FD_RUNNING);
}

static fd_module open_plugin_module(const char *directory, const char *name, char *error, size_t size)
{
    size_t length = strlen(directory) + strlen(name) + 2;
    char *path = malloc(length);
    if (!path) { snprintf(error, size, "Cannot allocate engine library path"); return NULL; }
    snprintf(path, length, "%s/%s", directory, name);
    fd_module module = fd_module_open(path, error, size);
    free(path);
    return module;
}

static int run(void *data)
{
    fd_session *session = data;
    active = session;
    int result = -1;
    const char *directory = session->options.resources;
    char error[1024];
    session->module = open_plugin_module(directory, FD_ODAMEX_LIBRARY, error, sizeof(error));
    if (session->module) {
        __typeof__(&fd_odamex_run) start = (__typeof__(start))fd_module_symbol(session->module, "fd_odamex_run");
        if (start) {
            size_t length = strlen(directory) + sizeof("/assets");
            char *assets = malloc(length);
            if (assets) {
                snprintf(assets, length, "%s/assets", directory);
                fd_odamex_options options = session->options;
                options.resources = assets;
                fd_odamex_host host = {.context = session, .stopping = stopping, .input = read_input,
                    .frame = publish, .error = log_message, .log = log_message, .clock = clock_now,
                    .sleep = fd_sleep_ns,
                    .size = requested_size, .cache_sample = fd_cache_sample, .set_voice = fd_set_voice,
                    .get_voice = fd_get_voice, .set_music = fd_set_music, .push_music = fd_push_music};
                result = start(&host, &options);
                free(assets);
            } else log_message(session, "Cannot allocate Odamex resource path");
        } else log_message(session, "Odamex engine is missing its native entry point");
    } else log_message(session, error);
    active = NULL;
    atomic_store(&session->status, result ? FD_FAILED : FD_STOPPED);
    return 0;
}

FD_EXPORT fd_session *fd_odamex_start(const char *directory, const char *state, const char *iwad,
                                     const char *endpoint, const char *name, const char *password,
                                     int sound, int download)
{
    if (!directory || !state || !iwad || atomic_exchange(&occupied, true)) return NULL;
    fd_session *session = calloc(1, sizeof(*session));
    if (!session) { atomic_store(&occupied, false); return NULL; }
    const char *values[] = {directory, state, iwad, endpoint, name, password};
    for (unsigned i = 0; i < sizeof(values) / sizeof(*values); i++) {
        if (values[i] && !(session->strings[i] = strdup(values[i]))) { fd_close(session); return NULL; }
    }
    session->options = (fd_odamex_options){session->strings[0], session->strings[1], session->strings[2],
        session->strings[3], session->strings[4], session->strings[5], sound, download};
    session->mutex = fd_mutex_create();
    session->reader = fd_create();
    session->audio = fd_audio_create();
    if (!session->mutex || !session->reader || !session->audio
        || !(session->thread = fd_thread_create(run, session))) { fd_close(session); return NULL; }
    return session;
}

int fd_status(fd_session *session) { return session ? atomic_load(&session->status) : FD_STOPPED; }
FD_EXPORT int fd_odamex_is_online(fd_session *session) { return session && atomic_load(&session->online); }
fd_reader *fd_frame_reader(fd_session *session) { return session ? session->reader : NULL; }
void fd_stop(fd_session *session) { if (session) atomic_store(&session->stopping, true); }
void fd_close(fd_session *session)
{
    if (!session) return;
    fd_stop(session);
    fd_thread_join(session->thread, NULL);
    if (session->module) fd_module_close(session->module);
    fd_destroy(session->reader);
    fd_audio_destroy(session->audio);
    fd_mutex_destroy(session->mutex);
    for (unsigned i = 0; i < sizeof(session->strings) / sizeof(*session->strings); i++) free(session->strings[i]);
    free(session);
    atomic_store(&occupied, false);
}

unsigned fd_read_log(fd_session *session, char *output, unsigned capacity)
{
    if (!session || !output || !fd_mutex_trylock(session->mutex)) return 0;
    unsigned count = capacity < session->log_length ? capacity : session->log_length;
    memcpy(output, session->log, count);
    memmove(session->log, session->log + count, session->log_length - count);
    session->log_length -= count;
    fd_mutex_unlock(session->mutex);
    return count;
}

int fd_input(fd_session *session, const unsigned char *packets, unsigned length)
{
    if (!session || !packets || length % 4 || fd_status(session) >= FD_STOPPED) return 0;
    fd_mutex_lock(session->mutex);
    for (unsigned i = 0; i < length; i += 4) {
        const unsigned char *p = packets + i;
        if (p[0] == 'P') atomic_store(&session->paused, p[1] != 0);
        else if (p[0] == 'R') {
            unsigned w = p[1] | ((p[2] & 15) << 8), h = (p[2] >> 4) | (p[3] << 4);
            if (w >= 320 && w <= FD_MAX_WIDTH && h >= 240 && h <= FD_MAX_HEIGHT)
                atomic_store(&session->size, w | h << 16);
        }
    }
    unsigned i = 0;
    for (; i < length; i += 4) {
        const unsigned char *p = packets + i;
        if (p[0] != 'K' && p[0] != 'M') continue;
        if (session->used == sizeof(session->queue)) break;
        memcpy(session->queue + (session->head + session->used) % sizeof(session->queue), p, 4);
        session->used += 4;
    }
    fd_mutex_unlock(session->mutex);
    return i;
}
