/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef FD_API_H
#define FD_API_H
#ifdef _WIN32
#if defined(FD_BUILD_SHARED)
#define FD_EXPORT __declspec(dllexport)
#elif defined(FD_USE_SHARED)
#define FD_EXPORT __declspec(dllimport)
#else
#define FD_EXPORT
#endif
#else
#define FD_EXPORT __attribute__((visibility("default")))
#endif
#endif
