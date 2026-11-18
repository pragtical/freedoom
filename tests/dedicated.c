/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Standalone fixture: unmodified Woof networking, no embedded bridge. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include "platform/SDL3/SDL.h"
#include "doomtype.h"
#include "i_exit.h"
#include "i_printf.h"
#include "i_system.h"
#include "m_argv.h"
#include "net_server.h"
void NET_DedicatedServer(void);
int I_GetTimeMS(void) { return fd_clock_ns() / SDL_NS_PER_MS; }
void I_Sleep(int ms) { fd_sleep_ns((uint64_t)ms * SDL_NS_PER_MS); }
void NET_CL_Run(void) {}
void I_AtExitPrio(atexit_func_t function, boolean error, const char *name, exit_priority_t priority) {
    (void)error; (void)name; (void)priority; atexit(function);
}
void I_Printf(verbosity_t priority, const char *format, ...) {
    (void)priority;
    va_list args; va_start(args, format); vfprintf(stderr, format, args); va_end(args);
    fputc('\n', stderr);
}
void I_ErrorInternal(const char *prefix, const char *format, ...) {
    (void)prefix;
    va_list args; va_start(args, format); vfprintf(stderr, format, args); va_end(args);
    exit(1);
}
void *I_Realloc(void *memory, size_t size) {
    void *result = realloc(memory, size);
    if (!result && size) I_Error("Out of memory");
    return result;
}
int I_ConsoleStdout(void) { return 0; }
const char *fd_base_path(void) { return "."; }
char *fd_pref_path(void) { return strdup("."); }
int main(int argc, char **argv) {
    myargc = argc; myargv = argv;
    NET_DedicatedServer();
    return 0;
}
