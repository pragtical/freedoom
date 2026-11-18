/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef FREEDOOM_TRANSPORT_H
#define FREEDOOM_TRANSPORT_H
#include <stdint.h>
#define FD_ABI 1
#define FD_MAGIC 0x46443031u
#define FD_WIDTH 320
#define FD_HEIGHT 200
#define FD_MAX_WIDTH 4096
#define FD_MAX_HEIGHT 3072
typedef struct {
    uint32_t magic, width, height, serial;
    uint8_t pixels[FD_MAX_WIDTH * FD_MAX_HEIGHT * 4];
} fd_frame;
#endif
