/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef FD_CONTENT_H
#define FD_CONTENT_H
#include "engine_api.h"
typedef struct {
    unsigned mission, gamemode, network;
    char title[96];
    unsigned char checksum[20];
} fd_iwad;
FD_EXPORT int fd_iwad_info(const char *path, fd_iwad *output);
#endif
