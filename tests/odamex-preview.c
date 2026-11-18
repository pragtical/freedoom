/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#define getcwd _getcwd
#else
#include <fcntl.h>
#include <unistd.h>
#endif
#include "preview.h"
#include "loader.h"
#include "platform/SDL3/SDL.h"

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); exit(1); \
} } while (0)

struct session {
    __typeof__(&fd_odamex_run) run;
    const char *resources, *state, *iwad, *endpoint;
    unsigned frames, events;
    int result, invalid, menu_quit, resize;
};

static int stopping(void *data)
{
    struct session *session = data;
    return session->frames >= (session->menu_quit ? 60u : 12u);
}

static int input(void *data, unsigned char packet[4])
{
    struct session *session = data;
    unsigned limit = session->menu_quit ? 4 : 2;
    if (session->events == limit || session->frames < 3 + 6 * (session->events / 2)) return 0;
    unsigned key = session->menu_quit ? (session->events < 2 ? 0xc4 : 'y') : 27;
    memcpy(packet, (unsigned char[]){'K', session->events++ % 2 == 0, key, 0}, 4);
    return 1;
}

static uint32_t size(void *data)
{
    struct session *session = data;
    if (!session->resize || session->frames < 2) return 0;
    return session->frames < 6 ? 1280u | (720u << 16) : 1152u | (648u << 16);
}

static void frame(void *data, const unsigned char *rgba, unsigned width, unsigned height, int online_level)
{
    struct session *session = data;
    uint32_t requested = size(data);
    CHECK(width == (requested ? requested & 65535u : 640u));
    CHECK(height == (requested ? requested >> 16 : 480u));
    unsigned colorful = 0;
    for (unsigned y = 0; y < height; y += 5) {
        for (unsigned x = 0; x < width; x += 5) {
            const unsigned char *pixel = rgba + (width * y + x) * 4;
            CHECK(pixel[3] == 255);
            colorful += pixel[0] != pixel[1] || pixel[1] != pixel[2];
        }
    }
    if (colorful > 64 && (!session->endpoint || online_level)) session->frames++;
}

static void error(void *data, const char *message)
{
    if (!((struct session *)data)->invalid) fprintf(stderr, "Odamex: %s\n", message);
}

static int run(void *data)
{
    struct session *session = data;
    fd_odamex_host host = {.context = session, .stopping = stopping, .input = input,
        .frame = frame, .error = error, .size = size};
    fd_odamex_options options = {.resources = session->resources, .state = session->state,
        .iwad = session->iwad, .endpoint = session->endpoint};
    session->result = session->run(&host, &options);
    return 0;
}

int main(int argc, char **argv)
{
    CHECK(argc == 5 || argc == 6);
#ifndef _WIN32
    int stdin_flags = fcntl(STDIN_FILENO, F_GETFD);
#endif
    char *cwd = getcwd(NULL, 0);
    CHECK(cwd);
    char message[1024];
    for (unsigned pass = 0; pass < (argc == 6 ? 4u : 3u); pass++) {
        struct session session = {.resources = argv[2], .state = argv[3],
            .iwad = pass == 0 ? "nonexistent-freedoom2.wad" : argv[4], .invalid = pass == 0,
            .endpoint = pass == 3 ? argv[5] : NULL, .menu_quit = pass == 2, .resize = pass == 1};
        fd_module module = fd_module_open(argv[1], message, sizeof(message));
        if (!module) fprintf(stderr, "%s\n", message);
        CHECK(module);
        session.run = (__typeof__(session.run))fd_module_symbol(module, "fd_odamex_run");
        CHECK(session.run && !fd_module_symbol(module, "D_DoomMain"));
        SDL_Thread *thread = fd_thread_create(run, &session);
        CHECK(thread);
        fd_thread_join(thread, NULL);
        if (session.result != (session.invalid ? -1 : 0))
            fprintf(stderr, "Odamex session %u returned %d after %u frames\n",
                    pass + 1, session.result, session.frames);
        if (session.invalid) CHECK(session.result == -1);
        else {
            CHECK(session.result == 0);
            if (session.menu_quit) CHECK(session.frames < 60 && session.events == 4);
            else CHECK(session.frames >= 12 && session.events == 2);
        }
        CHECK(fd_module_close(module));
#ifndef _WIN32
        CHECK(fcntl(STDIN_FILENO, F_GETFD) == stdin_flags);
#endif
        char *after = getcwd(NULL, 0);
        CHECK(after && !strcmp(cwd, after));
        free(after);
    }
    free(cwd);
    puts("PASS Odamex startup error, native resizing, input, worker stop, menu Quit and reload");
    if (argc == 6) puts("PASS joined and rendered a level on stock Odamex");
    return 0;
}
