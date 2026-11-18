/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef FD_ODAMEX_LOADER_H
#define FD_ODAMEX_LOADER_H
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
typedef HMODULE fd_module;
#else
#include <dlfcn.h>
typedef void *fd_module;
#endif

static inline fd_module fd_module_open(const char *path, char *error, size_t size)
{
#ifdef _WIN32
    int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
    wchar_t *wide = length ? malloc((size_t)length * sizeof(*wide)) : NULL;
    fd_module module = NULL;
    if (wide && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, length)) {
        for (wchar_t *p = wide; *p; p++) if (*p == L'/') *p = L'\\';
        module = LoadLibraryExW(wide, NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!module) snprintf(error, size, "Cannot load %s (Windows error %lu)", path, GetLastError());
    } else snprintf(error, size, "Cannot convert library path %s to UTF-16", path);
    free(wide);
#else
    fd_module module = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!module) snprintf(error, size, "%s", dlerror());
#endif
    return module;
}

#ifdef _WIN32
#define fd_module_symbol GetProcAddress
#define fd_module_close FreeLibrary
#else
#define fd_module_symbol dlsym
static inline int fd_module_close(fd_module module) { return dlclose(module) == 0; }
#endif
#endif
