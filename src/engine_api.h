/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef FD_ENGINE_API_H
#define FD_ENGINE_API_H
#include <stddef.h>
#include <stdint.h>
#include "api.h"
typedef struct fd_session fd_session;
typedef struct fd_reader fd_reader;
enum { FD_STARTING, FD_RUNNING, FD_STOPPED, FD_FAILED };
FD_EXPORT fd_session *fd_start(const char *directory, const char *state, const char *iwad, int sound);
FD_EXPORT int fd_status(fd_session *session);
FD_EXPORT int fd_input(fd_session *session, const unsigned char *packets, unsigned length);
FD_EXPORT void fd_stop(fd_session *session);
FD_EXPORT void fd_close(fd_session *session);
FD_EXPORT unsigned fd_read_log(fd_session *session, char *buffer, unsigned capacity);
FD_EXPORT fd_reader *fd_frame_reader(fd_session *session);
/* Woof only. Queue a color mode change for the next display frame. */
FD_EXPORT int fd_set_truecolor(fd_session *session, int enabled);
void fd_engine_exit(int code) __attribute__((noreturn));
void fd_engine_log(const char *message);
#endif
