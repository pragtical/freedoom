/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
static void sleep_ms(unsigned ms) { Sleep(ms); }
#else
#include <time.h>
static void sleep_ms(unsigned ms) {
    struct timespec delay = {ms / 1000, (ms % 1000) * 1000000}; nanosleep(&delay, NULL);
}
#endif
#include "engine_api.h"
#include "framebuffer.h"
#define CHECK(test) do { if (!(test)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test); exit(1); } } while (0)
static void wait_start(fd_session *session) {
    for (int i = 0; i < 1500 && fd_status(session) == FD_STARTING; i++) sleep_ms(10);
}
static void tap(fd_session *session, unsigned code) {
    unsigned char packet[] = {'K', 1, code, 0}; CHECK(fd_input(session, packet, 4) == 4); sleep_ms(70);
    packet[1] = 0; CHECK(fd_input(session, packet, 4) == 4); sleep_ms(120);
}
int main(int argc, char **argv) {
    CHECK(argc == 4);
    uint32_t *pixels = malloc(320 * 240 * sizeof(*pixels)); CHECK(pixels);
    for (unsigned i = 0; i < 50; i++) {
        char iwad[8192];
        snprintf(iwad, sizeof(iwad), "%s/iwads/freedoom%u.wad", argv[2], i % 2 + 1);
        fd_session *session = fd_start(argv[1], argv[3], iwad, 1); CHECK(session);
        wait_start(session);
        if (fd_status(session) != FD_RUNNING) {
            char log[8193]; unsigned size = fd_read_log(session, log, 8192); log[size] = 0; fputs(log, stderr);
        }
        CHECK(fd_status(session) == FD_RUNNING);
        if (i == 0 || i == 25) {
            sleep_ms(1300);
            tap(session, 27); tap(session, 13); tap(session, 13);
            if (i == 0) tap(session, 13);
            sleep_ms(1000); tap(session, 0xa3);
        }
        uint32_t serial = 0;
        int copied = 0;
        for (int attempt = 0; attempt < 100 && copied == 0; attempt++) {
            copied = fd_copy_frame(fd_frame_reader(session), pixels, 320, 240, &serial, 1);
            if (!copied) sleep_ms(1);
        }
        CHECK(copied == 1 && serial);
        fd_stop(session);
        for (int attempt = 0; attempt < 500 && fd_status(session) < FD_STOPPED; attempt++) sleep_ms(10);
        CHECK(fd_status(session) == FD_STOPPED); fd_close(session);
        if (i % 10 == 0) {
            session = fd_start(argv[1], argv[3], "missing-freedoom-test.wad", 0); CHECK(session);
            wait_start(session); CHECK(fd_status(session) == FD_FAILED); fd_close(session);
        }
    }
    free(pixels);
    puts("PASS 50 native lifecycles, both IWADs, gameplay, music and startup-error recovery");
}
