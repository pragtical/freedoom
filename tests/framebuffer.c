/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#endif
#include "framebuffer.h"
#define CHECK(test) do { if (!(test)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test); exit(1); } } while (0)

static uint32_t interpolate(uint32_t a, uint32_t b, unsigned weight) {
    uint32_t result = 0;
    for (unsigned shift = 0; shift < 32; shift += 8)
        result |= ((((a >> shift) & 255) * (256 - weight) + ((b >> shift) & 255) * weight) >> 8) << shift;
    return result;
}

int main(void) {
    CHECK(fd_abi_version() == FD_ABI && fd_frame_size() == sizeof(fd_frame));
    fd_reader *reader = fd_create();
    CHECK(reader);
    uint32_t serial = 0, pixel;
    CHECK(fd_copy_frame(reader, &pixel, 1, 1, &serial, 0) == 0);
    fd_mutex_lock(reader->mutex);
    CHECK(fd_copy_frame(reader, &pixel, 1, 1, &serial, 0) == 0);
    uint32_t *source = (uint32_t *)reader->frame->pixels;
    fd_mutex_unlock(reader->mutex);
    unsigned sources[][2] = {{320, 200}, {960, 600}, {1280, 800}, {1664, 785}, {2048, 1536}, {1, 1}};
    unsigned dimensions[][2] = {{1, 1}, {320, 200}, {640, 480}, {317, 123},
        {960, 720}, {1280, 960}, {1664, 942}, {2048, 1536}};
    for (unsigned input = 0; input < sizeof(sources) / sizeof(sources[0]); input++) {
        unsigned sw = sources[input][0], sh = sources[input][1];
        fd_mutex_lock(reader->mutex);
        reader->frame->width = sw;
        reader->frame->height = sh;
        for (unsigned i = 0; i < sw * sh; i++)
            source[i] = ((i * 7 & 255) << 24) | (i & 255) | ((i / sw & 255) << 8) | ((i * 3 & 255) << 16);
        reader->frame->serial = input + 1;
        fd_mutex_unlock(reader->mutex);
        for (unsigned size = 0; size < sizeof(dimensions) / sizeof(dimensions[0]); size++) {
            unsigned width = dimensions[size][0], height = dimensions[size][1];
            uint32_t *pixels = malloc(width * height * sizeof(*pixels));
            CHECK(pixels);
            for (int smooth = 0; smooth < 2; smooth++) {
                serial = 0;
                CHECK(fd_copy_frame(reader, pixels, width, height, &serial, smooth) == 1 && serial == input + 1);
                CHECK(fd_copy_frame(reader, pixels, width, height, &serial, smooth) == 0);
                for (unsigned y = 0; y < height; y++) for (unsigned x = 0; x < width; x++) {
                    uint32_t expected;
                    if (smooth) {
                        int sx = (int)((2 * x + 1) * sw * 128 / width) - 128;
                        int sy = (int)((2 * y + 1) * sh * 128 / height) - 128;
                        if (sx < 0) sx = 0;
                        if (sy < 0) sy = 0;
                        if (sx > (int)(sw - 1) * 256) sx = (sw - 1) * 256;
                        if (sy > (int)(sh - 1) * 256) sy = (sh - 1) * 256;
                        unsigned left = sx >> 8, right = left + (left + 1 < sw);
                        unsigned top = sy >> 8, bottom = top + (top + 1 < sh);
                        expected = interpolate(interpolate(source[top * sw + left], source[top * sw + right], sx & 255),
                            interpolate(source[bottom * sw + left], source[bottom * sw + right], sx & 255), sy & 255);
                    } else expected = source[(y * sh / height) * sw + x * sw / width];
                    CHECK(pixels[y * width + x] == expected);
                }
            }
            free(pixels);
        }
    }
    CHECK(fd_copy_frame(reader, &pixel, 0, 1, &serial, 0) == -1);
    CHECK(fd_copy_frame(reader, &pixel, FD_MAX_WIDTH + 1, 1, &serial, 0) == -1);
    reader->frame->width = 3840;
    reader->frame->height = 2160;
    memset(reader->frame->pixels, 127, 3840 * 2160 * 4);
    reader->frame->serial++;
    serial = 0;
    CHECK(fd_copy_frame(reader, &pixel, 1, 1, &serial, 1) == 1 && pixel == 0x7f7f7f7f);
    CHECK(fd_copy_frame(NULL, &pixel, 1, 1, &serial, 0) == -1);
    reader->frame->width = 0;
    CHECK(fd_copy_frame(reader, &pixel, 1, 1, &serial, 0) == -1);
    reader->frame->width = FD_MAX_WIDTH + 1;
    CHECK(fd_copy_frame(reader, &pixel, 1, 1, &serial, 0) == -1);
    reader->frame->width = FD_WIDTH;
    reader->frame->height = FD_MAX_HEIGHT + 1;
    CHECK(fd_copy_frame(reader, &pixel, 1, 1, &serial, 0) == -1);
    fd_destroy(reader);
    fd_destroy(NULL);
    puts("PASS framebuffer scaling, bounds, nonblocking snapshots and cleanup");
    return 0;
}
