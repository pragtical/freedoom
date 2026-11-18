/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <string.h>
#include "truecolor.h"
#include "m_argv.h"
#include "r_main.h"
#include "r_state.h"
#include "r_tranmap.h"
#include "w_wad.h"

uint32_t fd_base_rgb[256];
boolean fd_truecolor_setting, fd_truecolor;
static pixel_t shade_table[NUMCOLORMAPS][256];
static const lighttable_t *default_map;
static byte base_palette[256 * 3];
static const byte *display_gamma;
static bool tinted, blend_rgb;

void fd_init_truecolor(const byte *palette)
{
    fd_truecolor = fd_truecolor_setting;
    memcpy(base_palette, palette, sizeof(base_palette));
    int lump = W_GetNumForName("COLORMAP");
    default_map = W_IsIWADLump(lump) ? colormaps[0] : NULL;
    blend_rgb = W_CheckNumForName("TRANMAP") < 0 || M_CheckParm("-tranmap");
    for (unsigned i = 0; i < 256; i++)
        fd_base_rgb[i] = 0xff000000u | (uint32_t)palette[3 * i] << 16
            | (uint32_t)palette[3 * i + 1] << 8 | palette[3 * i + 2];

    // As in Odamex, build the light ramp in RGB without a nearest-color lookup.
    for (unsigned level = 0; level < NUMCOLORMAPS; level++)
        for (unsigned i = 0; i < 256; i++)
            shade_table[level][i] = fd_mix(fd_base_rgb[i], 0xff000000u, level * 256 / NUMCOLORMAPS);
}

void fd_truecolor_palette(const byte *palette, const byte *gamma)
{
    // Preserve the WAD's exact flash palettes for damage, bonuses, and suits.
    tinted = memcmp(palette, base_palette, sizeof(base_palette)) != 0;
    display_gamma = gamma;
}

const pixel_t *fd_shademap(const lighttable_t *map)
{
    uintptr_t offset = (uintptr_t)map - (uintptr_t)default_map;
    return fd_truecolor && default_map && offset < NUMCOLORMAPS * 256u && !(offset % 256)
        ? shade_table[offset / 256] : NULL;
}

SDL_Color fd_display_color(pixel_t pixel, const SDL_Color *colors)
{
    if (pixel <= 255 || !fd_truecolor || tinted) return colors[fd_pixel_index(pixel)];
    return (SDL_Color){
        display_gamma[(pixel >> 16) & 255], display_gamma[(pixel >> 8) & 255],
        display_gamma[pixel & 255], 255,
    };
}

void fd_copy_colors(byte *rgba, const pixel_t *pixels, unsigned count, const SDL_Color *colors)
{
    if (!fd_truecolor || tinted) {
        uint32_t palette[256];
        for (unsigned i = 0; i < 256; i++) {
            const byte color[4] = {colors[i].r, colors[i].g, colors[i].b, 255};
            memcpy(&palette[i], color, sizeof(color));
        }
        for (unsigned i = 0; i < count; i++) {
            uint32_t color = palette[fd_pixel_index(pixels[i])];
            memcpy(rgba + 4 * i, &color, sizeof(color));
        }
        return;
    }
    for (unsigned i = 0; i < count; i++) {
        SDL_Color color = fd_display_color(pixels[i], colors);
        const byte display[4] = {color.r, color.g, color.b, 255};
        memcpy(rgba + 4 * i, display, sizeof(display));
    }
}

pixel_t fd_translucent(const byte *map, pixel_t background, pixel_t foreground)
{
    if (fd_truecolor && blend_rgb && map == main_tranmap)
        return fd_mix(background, foreground, default_tranmap_alpha * 256 / 100);
    return map[(fd_pixel_index(background) << 8) + fd_pixel_index(foreground)];
}

pixel_t fd_darken(const lighttable_t *base, unsigned shade, pixel_t pixel)
{
    if (fd_truecolor && base == default_map)
        return shade ? fd_mix(pixel, 0xff000000u, shade * 256 / NUMCOLORMAPS) : pixel;
    return base[shade * 256 + fd_pixel_index(pixel)];
}
