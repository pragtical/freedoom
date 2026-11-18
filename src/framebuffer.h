/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef FREEDOOM_FRAMEBUFFER_H
#define FREEDOOM_FRAMEBUFFER_H
#include <stdint.h>
#include "platform/SDL3/SDL.h"
#include "transport.h"
#include "api.h"
#define FD_API FD_EXPORT
typedef struct fd_reader {
    SDL_Mutex *mutex;
    fd_frame *frame;
    uint32_t *rows;
    unsigned capacity;
} fd_reader;
FD_API unsigned fd_abi_version(void);
FD_API unsigned fd_frame_size(void);
FD_API fd_reader *fd_create(void);
FD_API int fd_copy_frame(fd_reader *reader, uint32_t *pixels, unsigned width,
                         unsigned height, uint32_t *serial, int smooth);
FD_API void fd_destroy(fd_reader *reader);
#endif
