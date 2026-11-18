#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Width-aware framebuffer and RGB rasterizer adapters for the pinned Woof."""
from pathlib import Path
import sys


def substitute(source, old, new, count=1):
    if source.count(old) != count:
        raise ValueError(f"Expected {count} occurrences of {old!r}")
    return source.replace(old, new)


def adapt(name, source):
    def replace(old, new, count=1):
        nonlocal source
        source = substitute(source, old, new, count)

    if name == "r_draw.c":
        for kind, count in (("dc", 4), ("ds", 1)):
            line = f"const lighttable_t *const *colormap = {kind}_colormap;"
            replace(line, line + """
    const pixel_t *const shades[2] = {fd_shademap(colormap[0]), fd_shademap(colormap[1])};""", count)
        for texel in ("src", "translation[src]"):
            replace(f"*dest = colormap[brightmap[src]][{texel}];",
                    f"*dest = fd_shade(colormap[brightmap[src]], shades[brightmap[src]], {texel});", 2)
        replace("*dest++ = colormap[brightmap[src]][src];",
                "*dest++ = fd_shade(colormap[brightmap[src]], shades[brightmap[src]], src);")
        for texel, count in (("src", 2), ("translation[src]", 1)):
            replace(f"tranmap[(*dest << 8) + colormap[brightmap[src]][{texel}]]",
                    f"fd_translucent(tranmap, *dest, fd_shade(colormap[brightmap[src]], shades[brightmap[src]], {texel}))", count)
        replace("const byte skycolor = dc_skycolor;",
                "const pixel_t *shades = fd_shademap(colormap);\n"
                "    const pixel_t skycolor = fd_shade(colormap, shades, dc_skycolor);\n"
                "    const pixel_t edge = fd_shade(colormap, shades, source[0]);")
        replace("*dest = colormap[skycolor];", "*dest = skycolor;")
        replace("main_tranmap\n"
                "                [(main_tranmap[(colormap[source[0]] << 8) + colormap[skycolor]]\n"
                "                  << 8)\n"
                "                 + colormap[skycolor]]",
                "fd_translucent(main_tranmap, fd_translucent(main_tranmap, edge, skycolor), skycolor)")
        replace("main_tranmap[(colormap[source[0]] << 8) + colormap[skycolor]]",
                "fd_translucent(main_tranmap, edge, skycolor)")
        for index in ("frac >> FRACBITS", "(frac >> FRACBITS) & heightmask"):
            replace(f"*dest = colormap[source[{index}]];", f"*dest = fd_shade(colormap, shades, source[{index}]);")
        for shade, offset, count in (
            ("6 * 256", "linesize * fuzzoffset[fuzzpos]", 2),
            ("6 * 256", "linesize * (fuzzoffset[fuzzpos] - FUZZOFF) / 2", 2),
            ("dark", "linesize * offset", 1),
            ("dark", "linesize * (offset - FUZZOFF) / 2", 1),
        ):
            old = f"[{shade} + dest[{offset}]]"
            if source.count("fullcolormap" + old) > 0:
                immediate_count = source.count("fullcolormap" + old)
                replace("fullcolormap" + old, f"fd_darken(fullcolormap, ({shade}) / 256, dest[{offset}])", immediate_count)
                count -= immediate_count
            if count:
                replace("fullcolormap\n            " + old,
                        f"fd_darken(fullcolormap, ({shade}) / 256, dest[{offset}])", count)
        replace("const byte fuzz =", "const pixel_t fuzz =", 4)
        for width in ("fuzzblockwidth", "fuzzblocksize"):
            replace(f"memset(dest, fuzz, {width});", "fd_fill_pixels(dest, fuzz, fuzzblockwidth);", 2)
        replace("fullcolormap[8 * 256 + *dest]", "fd_darken(fullcolormap, 8, *dest)")
    elif name == "v_video.c":
        for width, count in (("usew", 1), ("width", 2)):
            replace(f"memcpy(dest, src, {width});", f"memcpy(dest, src, {width} * sizeof(*dest));", count)
        replace("memset(dest, color, dstrect.sw);", "fd_fill_pixels(dest, color, dstrect.sw);")
        replace("    const byte *darkcolormap = &colormaps[0][20 * 256];\n\n", "")
        replace("darkcolormap[*col]", "fd_darken(colormaps[0], 20, *col)")
        for texel in ("source[frac >> FRACBITS]", "translation[source[frac >> FRACBITS]]"):
            replace(f"tranmap[(*dest << 8) + {texel}]", f"fd_translucent(tranmap, *dest, {texel})", 3)
    elif name == "am_map.c":
        replace("memset(I_VideoBuffer, color, f_h * f_w);", "fd_fill_pixels(I_VideoBuffer, color, f_h * f_w);")
        # Automap antialiasing keeps its tables and approximates RGB backgrounds.
        replace("bg2rgb[*dest]", "bg2rgb[fd_pixel_index(*dest)]")
    elif name == "f_wipe.c":
        start = "            unsigned int *fg2rgb = Col2RGB8[fade_tick];"
        replace(start, """            if (fd_truecolor)
            {
                dst[x] = fd_mix(sta[x], end[x], fade_tick * 4);
                continue;
            }
""" + start)
        replace("fg2rgb[end[x]]", "fg2rgb[fd_pixel_index(end[x])]")
        replace("bg2rgb[sta[x]]", "bg2rgb[fd_pixel_index(sta[x])]")
        replace("memcpy(dest, src, rect.sw);", "memcpy(dest, src, rect.sw * sizeof(*dest));")
    elif name == "mn_snapshot.c":
        # The snapshot in a Woof savegame must still be indexed on disk.
        replace("sizeof(pixel_t);", "sizeof(byte);")
        for name_ in ("snapshots[10]", "current_snapshot"):
            replace(f"static pixel_t *{name_}", f"static byte *{name_}")
        replace("pixel_t *p = current_snapshot;", "byte *p = current_snapshot;")
        replace("*p++ = s[line + V_ScaleX(x)];", "*p++ = fd_pixel_index(s[line + V_ScaleX(x)]);")
        replace("pixel_t *destline, *srcline;", "pixel_t *destline;\n    byte *srcline;")
    elif name == "r_voxel.c":
        replace("byte pix = spr->colormap[spr->brightmap[src]][src];",
                "const byte *map = spr->colormap[spr->brightmap[src]];\n"
                "\t\t\t\tpixel_t pix = fd_shade(map, fd_shademap(map), src);", 3)
    elif name == "engine_video.c":
        replace("void I_BindVideoVariables(void) {",
                'void I_BindVideoVariables(void) {\n'
                '    M_BindBool("truecolor", &fd_truecolor_setting, NULL, true, ss_gen, wad_no,\n'
                '               "32-bit lighting and blending");')
        replace("    for (unsigned i = 0; i < 256; i++)",
                "    fd_truecolor_palette(palette, gamma);\n    for (unsigned i = 0; i < 256; i++)")
        replace('    I_SetPalette(W_CacheLumpName("PLAYPAL", PU_CACHE));',
                '    const byte *palette = W_CacheLumpName("PLAYPAL", PU_CACHE);\n'
                '    fd_init_truecolor(palette);\n    I_SetPalette((byte *)palette);')
        replace("colors[I_VideoBuffer[i]]", "fd_display_color(I_VideoBuffer[i], colors)")
    elif name == "mn_setup.c":
        replace("static setup_menu_t gen_settings1[] = {",
                'static setup_menu_t gen_settings1[] = {\n'
                '    {"True Color", S_ONOFF, CNTR_X, M_SPC, {"truecolor"}},')
        return source
    elif name == "d_main.c":
        start = "  if (nodrawers)                    // for comparative timing / profiling\n    return;\n"
        replace(start, start + "\n  fd_apply_truecolor();\n")
    else:
        return source
    return '#include "truecolor.h"\n' + source


if __name__ == "__main__":
    header, output = map(Path, sys.argv[1:])
    output.write_text(substitute(header.read_text(), "typedef byte pixel_t;", "typedef uint32_t pixel_t;"))
