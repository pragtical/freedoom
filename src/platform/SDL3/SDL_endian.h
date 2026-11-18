/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef FD_PLATFORM_ENDIAN_H
#define FD_PLATFORM_ENDIAN_H
#include <stdint.h>
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define SDL_Swap16LE(x) __builtin_bswap16((uint16_t)(x))
#define SDL_Swap32LE(x) __builtin_bswap32((uint32_t)(x))
#define SDL_Swap16BE(x) ((uint16_t)(x))
#define SDL_Swap32BE(x) ((uint32_t)(x))
#else
#define SDL_Swap16LE(x) ((uint16_t)(x))
#define SDL_Swap32LE(x) ((uint32_t)(x))
#define SDL_Swap16BE(x) __builtin_bswap16((uint16_t)(x))
#define SDL_Swap32BE(x) __builtin_bswap32((uint32_t)(x))
#endif
#endif
