/* SPDX-License-Identifier: GPL-2.0-or-later
   Headless adaptation of Woof 16.0.0 i_video.c. Original copyright:
   id Software et al. (1999), Fabian Greffrath (2020-2021). */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "woof.h"
#include "am_map.h"
#include "doomstat.h"
#include "i_gamma.h"
#include "i_system.h"
#include "i_timer.h"
#include "i_video.h"
#include "m_config.h"
#include "m_input.h"
#include "m_io.h"
#include "mn_menu.h"
#include "r_main.h"
#include "r_plane.h"
#include "r_draw.h"
#include "st_stuff.h"
#include "v_video.h"
#include "w_wad.h"
#include "z_zone.h"
#include "spng.h"

fixed_t fractionaltic;
boolean dynamic_resolution, uncapped = true, resetneeded, setrefreshneeded;
boolean toggle_fullscreen, toggle_exclusive_fullscreen;
boolean correct_aspect_ratio = true, screenvisible = true;
int current_video_height, fps, custom_fov = FOV_DEFAULT, gamma2 = 9;
static int default_height = 600, widescreen = 1, default_widescreen = 1;
static boolean default_uncapped = true;
static SDL_Color colors[256];
static unsigned view_width, view_height;
static uint64_t last_fps;
static int frame_count;

void I_SetPalette(byte *palette) {
    const byte *gamma = gammatable[CLAMP(gamma2, 0, 17)];
    for (unsigned i = 0; i < 256; i++)
        colors[i] = (SDL_Color){gamma[palette[i * 3]], gamma[palette[i * 3 + 1]], gamma[palette[i * 3 + 2]], 255};
}
byte I_GetNearestColor(byte *palette, int r, int g, int b) {
    int best = 0, distance = INT_MAX;
    for (int i = 0; i < 256; i++) {
        int dr = r - palette[i * 3], dg = g - palette[i * 3 + 1], db = b - palette[i * 3 + 2];
        int next = dr * dr + dg * dg + db * db;
        if (next < distance) { best = i; distance = next; }
    }
    return best;
}
void I_ReadScreen(pixel_t *destination) { V_GetBlock(0, 0, video.width, video.height, destination); }
void I_BeginRead(unsigned bytes) { (void)bytes; }
void I_EndRead(void) {}
void I_UpdateHudAnchoring(void) {
    int width = video.unscaledw, height = correct_aspect_ratio ? ACTUALHEIGHT : SCREENHEIGHT;
    if (hud_anchoring == HUD_ANCHORING_4_3) { width = 4; height = 3; }
    else if (hud_anchoring == HUD_ANCHORING_16_9) { width = 16; height = 9; }
    else if (hud_anchoring == HUD_ANCHORING_21_9) { width = 21; height = 9; }
    int base_height = correct_aspect_ratio ? ACTUALHEIGHT : SCREENHEIGHT;
    st_wide_shift = CLAMP((base_height * width / height - NONWIDEWIDTH) / 2, 0, video.deltaw);
    if (st_wide_shift > .9f * video.deltaw) st_wide_shift = video.deltaw;
    MN_UpdateHudAnchoringItem();
}
void I_ResetScreen(void) {
    resetneeded = false;
    widescreen = default_widescreen;
    current_video_height = correct_aspect_ratio ? fd_view_height * 5 / 6 : fd_view_height;
    current_video_height = CLAMP(current_video_height, SCREENHEIGHT, FD_MAX_HEIGHT);
    int actual = correct_aspect_ratio ? current_video_height * 6 / 5 : current_video_height;
    int width = fd_view_width, height = fd_view_height;
    if (!widescreen) { width = 4; height = 3; }
    else if (widescreen > 1) {
        static const int ratios[][2] = {{4,3},{4,3},{16,10},{16,9},{21,9},{32,9}};
        int index = CLAMP(widescreen, 0, 5); width = ratios[index][0]; height = ratios[index][1];
    }
    video.height = current_video_height;
    video.width = (actual * width / height) & ~1;
    if (video.width > FD_MAX_WIDTH) video.width = FD_MAX_WIDTH;
    int unscaled_height = correct_aspect_ratio ? ACTUALHEIGHT : SCREENHEIGHT;
    video.unscaledw = unscaled_height * width / height;
    video.deltaw = (video.unscaledw - NONWIDEWIDTH) / 2;
    Z_FreeTag(PU_VALLOC);
    V_Init(); R_InitVisplanesRes(); R_SetFuzzColumnMode(); setsizeneeded = true;
    AM_ResetScreenSize(); I_UpdateHudAnchoring();
    free(I_VideoBuffer);
    I_VideoBuffer = calloc((size_t)video.width * video.height, sizeof(pixel_t));
    if (!I_VideoBuffer) I_Error("Cannot allocate video buffer");
    V_RestoreBuffer(); Z_FreeTag(PU_RENDERER); R_InitAnyRes(); ST_InitRes();
}
void I_InitGraphics(void) {
    M_InputAddKey(input_use, 'e');
    dynamic_resolution = false; uncapped = true;
    default_widescreen = 1;
    view_width = fd_view_width; view_height = fd_view_height;
    I_ResetScreen();
    I_SetPalette(W_CacheLumpName("PLAYPAL", PU_CACHE));
}
void I_StartTic(void) {
    fd_wait_active();
    fd_poll_game_input();
    if (view_width != fd_view_width || view_height != fd_view_height) {
        view_width = fd_view_width; view_height = fd_view_height; resetneeded = true;
    }
}
void I_StartDisplay(void) { fd_wait_active(); fd_poll_game_input(); }
void I_StartFrame(void) {}
void I_FinishUpdate(void) {
    fd_wait_active();
    if (noblit) return;
    bool published = fd_publish(I_VideoBuffer, video.width, video.height, colors);
    uint64_t now = I_GetTimeUS(); frame_count++;
    if (now - last_fps >= 1000000) { fps = frame_count; frame_count = 0; last_fps = now; }
    setrefreshneeded = false;
    if (published) fd_wait_frame();
}
void I_ShutdownGraphics(void) {}
void I_QuitVideo(void) { free(I_VideoBuffer); I_VideoBuffer = NULL; }
void I_ToggleVsync(void) {}
void I_DynamicResolution(void) {}
void I_ResetDRS(void) {}
void *I_GetSDLWindow(void) { return NULL; }
void *I_GetSDLRenderer(void) { return NULL; }
void I_InitWindowIcon(void) {}
void I_ShowMouseCursor(boolean visible) { (void)visible; }
void I_ResetRelativeMouseState(void) {}
void I_UpdatePriority(boolean active) { (void)active; }
resolution_scaling_t I_GetResolutionScaling(void) {
    return (resolution_scaling_t){fd_view_height * 5 / 6, 50};
}
boolean I_WritePNGfile(char *filename) {
    size_t size = (size_t)video.width * video.height * 3;
    byte *rgb = malloc(size); if (!rgb) return false;
    for (int i = 0; i < video.width * video.height; i++) {
        SDL_Color color = colors[I_VideoBuffer[i]];
        rgb[i * 3] = color.r; rgb[i * 3 + 1] = color.g; rgb[i * 3 + 2] = color.b;
    }
    FILE *output = M_fopen(filename, "wb");
    spng_ctx *png = spng_ctx_new(SPNG_CTX_ENCODER);
    struct spng_ihdr header = {.width = video.width, .height = video.height,
        .bit_depth = 8, .color_type = SPNG_COLOR_TYPE_TRUECOLOR};
    boolean ok = output && png && !spng_set_png_file(png, output)
        && !spng_set_ihdr(png, &header) && !spng_encode_image(png, rgb, size, SPNG_FMT_PNG, SPNG_ENCODE_FINALIZE);
    if (output && fclose(output)) ok = false;
    spng_ctx_free(png); free(rgb); return ok;
}
void I_BindVideoVariables(void) {
    /* Woof's menu tables still resolve these keys. Their controls will be
       disabled because the editor, rather than the engine, owns the device. */
    static const struct { const char *name; int initial, min, max; } host_keys[] = {
        {"fullscreen", 1, 0, 1}, {"use_vsync", 1, 0, 1}, {"fpslimit", 0, 0, 500},
        {"smooth_scaling", 1, 0, 1}, {"vga_porch_flash", 0, 0, 1}, {"disk_icon", 0, 0, 1},
        {"video_display", 0, 0, UL}, {"max_video_width", 0, SCREENWIDTH, UL},
        {"max_video_height", 0, SCREENHEIGHT, UL}, {"window_width", 1065, 0, UL},
        {"window_height", 600, 0, UL}, {"grabmouse", 1, 0, 1}, {"mute_unfocused", 1, 0, 1},
        {"vanilla_keyboard_mapping", 0, 0, 1}, {"joy_rumble", 5, 0, 10},
        {"snd_hrtf", 0, 0, 1}, {"snd_resampler", 1, 0, UL},
        {"snd_absorption", 0, 0, 10}, {"snd_doppler", 0, 0, 10}, {"snd_equalizer", 0, 0, 4},
        {"snd_eq_preamp", 0, -24, 0}, {"snd_eq_low_gain", 0, -12, 12},
        {"snd_eq_low_cutoff", 200, 50, 800}, {"snd_eq_mid1_gain", 0, -12, 12},
        {"snd_eq_mid1_center", 500, 200, 3000}, {"snd_eq_mid1_width", 100, 1, 100},
        {"snd_eq_mid2_gain", 0, -12, 12}, {"snd_eq_mid2_center", 3000, 1000, 8000},
        {"snd_eq_mid2_width", 100, 1, 100}, {"snd_eq_high_gain", 0, -12, 12},
        {"snd_eq_high_cutoff", 6000, 4000, 16000}, {"fl_gain", 0, -20, 20}
    };
    static int host_values[sizeof(host_keys) / sizeof(*host_keys)];
    for (unsigned i = 0; i < sizeof(host_keys) / sizeof(*host_keys); i++)
        M_BindNum(host_keys[i].name, &host_values[i], NULL,
            host_keys[i].initial, host_keys[i].min, host_keys[i].max,
            ss_none, wad_no, "Managed by Pragtical");
    M_BindNum("current_video_height", &default_height, &current_video_height, 600, 200, UL, ss_none, wad_no, "Vertical resolution");
    BIND_BOOL_GENERAL(dynamic_resolution, false, "Dynamic resolution");
    BIND_BOOL(correct_aspect_ratio, true, "Aspect ratio correction");
    M_BindBool("uncapped", &default_uncapped, &uncapped, true, ss_gen, wad_no, "Uncapped rendering");
    M_BindNum("widescreen", &default_widescreen, &widescreen, 1, 0, 5, ss_gen, wad_no, "Widescreen ratio");
    M_BindNum("fov", &custom_fov, NULL, FOV_DEFAULT, FOV_MIN, FOV_MAX, ss_gen, wad_no, "Field of view");
    BIND_NUM_GENERAL(gamma2, 9, 0, 17, "Gamma level");
}
