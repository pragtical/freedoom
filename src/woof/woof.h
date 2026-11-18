/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef FREEDOOM_WOOF_H
#define FREEDOOM_WOOF_H
#include <SDL3/SDL.h>
#include "doomtype.h"
#include "transport.h"

extern unsigned fd_view_width, fd_view_height;
void fd_wait_active(void);
void fd_poll_game_input(void);
bool fd_wait_frame(void);
bool fd_publish(const pixel_t *pixels, unsigned width, unsigned height,
                const SDL_Color *colors);
Uint64 fd_performance_counter(void);
#endif
