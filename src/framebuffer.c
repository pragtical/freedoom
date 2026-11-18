/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "framebuffer.h"

unsigned fd_abi_version(void) { return FD_ABI; }
unsigned fd_frame_size(void) { return sizeof(fd_frame); }

void fd_destroy(fd_reader *reader) {
    if (!reader) return;
    fd_mutex_destroy(reader->mutex);
    free(reader->frame);
    free(reader->rows);
    free(reader);
}

fd_reader *fd_create(void) {
    fd_reader *reader = calloc(1, sizeof(*reader));
    if (!reader) return NULL;
    reader->mutex = fd_mutex_create();
    reader->frame = calloc(1, sizeof(fd_frame));
    if (!reader->mutex || !reader->frame) { fd_destroy(reader); return NULL; }
    reader->frame->magic = FD_MAGIC;
    reader->frame->width = FD_WIDTH;
    reader->frame->height = FD_HEIGHT;
    return reader;
}

static uint32_t blend(uint32_t a, uint32_t b, unsigned weight) {
    unsigned other = 256 - weight;
    uint32_t rb = ((a & 0x00ff00ff) * other + (b & 0x00ff00ff) * weight) >> 8;
    uint32_t ag = ((a >> 8 & 0x00ff00ff) * other + (b >> 8 & 0x00ff00ff) * weight);
    return (rb & 0x00ff00ff) | (ag & 0xff00ff00);
}

int fd_copy_frame(fd_reader *reader, uint32_t *pixels, unsigned width,
                  unsigned height, uint32_t *serial, int smooth) {
    if (!reader || !pixels || !serial || width < 1 || width > FD_MAX_WIDTH || height < 1 || height > FD_MAX_HEIGHT)
        return -1;
    if (!fd_mutex_trylock(reader->mutex)) return 0;
    fd_frame *frame = reader->frame;
    if (frame->magic != FD_MAGIC || !frame->width || frame->width > FD_MAX_WIDTH
        || !frame->height || frame->height > FD_MAX_HEIGHT) {
        fd_mutex_unlock(reader->mutex);
        return -1;
    }
    if (!frame->serial || frame->serial == *serial) { fd_mutex_unlock(reader->mutex); return 0; }
    unsigned source_width = frame->width, source_height = frame->height;
    if (width == source_width && height == source_height) {
        memcpy(pixels, frame->pixels, width * height * sizeof(*pixels));
        *serial = frame->serial;
        fd_mutex_unlock(reader->mutex);
        return 1;
    }
    unsigned columns[FD_MAX_WIDTH];
    if (smooth) {
        if (reader->capacity < width * source_height) {
            uint32_t *rows = realloc(reader->rows, width * source_height * sizeof(uint32_t));
            if (!rows) { fd_mutex_unlock(reader->mutex); return -1; }
            reader->rows = rows;
            reader->capacity = width * source_height;
        }
        if (width == source_width) {
            memcpy(reader->rows, frame->pixels, width * source_height * sizeof(uint32_t));
        } else {
            unsigned weights[FD_MAX_WIDTH];
            for (unsigned x = 0; x < width; x++) {
                int position = (int)((2 * x + 1) * source_width * 128 / width) - 128;
                if (position < 0) position = 0;
                if (position > (int)(source_width - 1) * 256) position = (source_width - 1) * 256;
                columns[x] = position >> 8;
                weights[x] = position & 255;
            }
            for (unsigned y = 0; y < source_height; y++) {
                const uint32_t *row = (const uint32_t *)frame->pixels + y * source_width;
                for (unsigned x = 0; x < width; x++) {
                    unsigned right = columns[x] + (columns[x] + 1 < source_width);
                    reader->rows[y * width + x] = blend(row[columns[x]], row[right], weights[x]);
                }
            }
        }
        *serial = frame->serial;
        /* Release the producer before expanding the snapshot vertically. */
        fd_mutex_unlock(reader->mutex);
        for (unsigned y = 0; y < height; y++) {
            int position = (int)((2 * y + 1) * source_height * 128 / height) - 128;
            if (position < 0) position = 0;
            if (position > (int)(source_height - 1) * 256) position = (source_height - 1) * 256;
            unsigned top = position >> 8;
            unsigned bottom = top + (top + 1 < source_height);
            const uint8_t *top_row = (const uint8_t *)(reader->rows + top * width);
            const uint8_t *bottom_row = (const uint8_t *)(reader->rows + bottom * width);
            uint8_t *output = (uint8_t *)(pixels + y * width);
            unsigned weight = position & 255, other = 256 - weight;
            for (unsigned x = 0; x < width * sizeof(*pixels); x++)
                output[x] = (top_row[x] * other + bottom_row[x] * weight) >> 8;
        }
        return 1;
    }
    if (width == source_width) {
        for (unsigned y = 0; y < height; y++)
            memcpy(pixels + y * width, frame->pixels + (y * source_height / height) * width * sizeof(uint32_t),
                width * sizeof(uint32_t));
    } else {
        for (unsigned x = 0; x < width; x++) columns[x] = x * source_width / width;
        for (unsigned y = 0; y < height; y++) {
            const uint32_t *row = (const uint32_t *)frame->pixels + (y * source_height / height) * source_width;
            for (unsigned x = 0; x < width; x++) pixels[y * width + x] = row[columns[x]];
        }
    }
    *serial = frame->serial;
    fd_mutex_unlock(reader->mutex);
    return 1;
}
