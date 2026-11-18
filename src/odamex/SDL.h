/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

// The few SDL references in shared renderer sources only select SIMD paths.
#define SDL_VERSION_ATLEAST(x, y, z) 0
inline bool SDL_HasMMX() { return false; }
inline bool SDL_HasSSE2() { return false; }
inline bool SDL_HasAltiVec() { return false; }
