/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef FD_WOOF_TRUECOLOR_H
#define FD_WOOF_TRUECOLOR_H

#include <SDL3/SDL.h>
#include "doomtype.h"
#include "v_flextran.h"

extern uint32_t fd_base_rgb[256];
extern boolean fd_truecolor_setting, fd_truecolor;

void fd_init_truecolor(const byte *palette);
void fd_apply_truecolor(void);
void fd_truecolor_palette(const byte *palette, const byte *gamma);
const pixel_t *fd_shademap(const lighttable_t *map);
SDL_Color fd_display_color(pixel_t pixel, const SDL_Color *colors);
void fd_copy_colors(byte *rgba, const pixel_t *pixels, unsigned count, const SDL_Color *colors);

/* Source textures and special-effect palettes remain indexed. */
static inline byte fd_pixel_index(pixel_t pixel)
{
    if (pixel <= 255) return (byte)pixel;
    return RGB32k[(pixel >> 19) & 31][(pixel >> 11) & 31][(pixel >> 3) & 31];
}

static inline pixel_t fd_shade(const lighttable_t *map, const pixel_t *shades, byte index)
{
    return shades ? shades[index] : map[index];
}

static inline uint32_t fd_pixel_rgb(pixel_t pixel)
{
    return pixel <= 255 ? fd_base_rgb[pixel] : pixel;
}

static inline pixel_t fd_mix(pixel_t background, pixel_t foreground, unsigned alpha)
{
    uint32_t bg = fd_pixel_rgb(background);
    uint32_t fg = fd_pixel_rgb(foreground);
    unsigned inv = 256 - alpha;
    uint32_t rb = ((bg & 0xff00ffu) * inv + (fg & 0xff00ffu) * alpha) >> 8;
    uint32_t g = ((bg & 0x00ff00u) * inv + (fg & 0x00ff00u) * alpha) >> 8;
    return 0xff000000u | (rb & 0xff00ffu) | (g & 0x00ff00u);
}

pixel_t fd_translucent(const byte *map, pixel_t background, pixel_t foreground);
pixel_t fd_darken(const lighttable_t *base, unsigned shade, pixel_t pixel);

static inline void fd_fill_pixels(pixel_t *dest, pixel_t pixel, unsigned count)
{
    for (unsigned i = 0; i < count; i++) dest[i] = pixel;
}
#endif
