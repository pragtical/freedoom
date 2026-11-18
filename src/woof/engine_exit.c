/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include "engine_api.h"
#include "network.h"
#include "i_exit.h"
#include "i_printf.h"
#include "i_system.h"
#include "d_ticcmd.h"

static struct {
    atexit_func_t function;
    boolean on_error;
    exit_priority_t priority;
} hooks[64];
static unsigned hook_count;

void I_AtExitPrio(atexit_func_t function, boolean on_error, const char *name, exit_priority_t priority) {
    (void)name;
    if (hook_count == sizeof(hooks) / sizeof(*hooks)) I_Error("Too many engine cleanup hooks");
    hooks[hook_count++] = (__typeof__(hooks[0])){function, on_error, priority};
}
void I_AtSignal(atexit_func_t function) { (void)function; }
void I_Signal(void) {}
void I_SafeExit(int code) {
    fd_begin_exit();
    for (int priority = 0; priority < exit_priority_max; priority++) {
        for (unsigned i = hook_count; i-- > 0;) {
            if (hooks[i].function && hooks[i].priority == priority) {
                atexit_func_t function = hooks[i].function;
                hooks[i].function = NULL;
                if (!code || hooks[i].on_error) function();
            }
        }
    }
    hook_count = 0;
    fd_engine_exit(code);
}
void I_InitPrintf(void) {}
int I_ConsoleStdout(void) { return 0; }
void I_Printf(verbosity_t priority, const char *format, ...) {
    if (priority > VB_INFO) return;
    char message[2048]; va_list args;
    va_start(args, format); vsnprintf(message, sizeof(message), format, args); va_end(args);
    fd_engine_log(message);
}
void I_PutChar(verbosity_t priority, int c) {
    if (priority <= VB_INFO) { char text[2] = {(char)c, 0}; fd_engine_log(text); }
}
void I_ErrorInternal(const char *prefix, const char *format, ...) {
    (void)prefix;
    char message[2048]; va_list args;
    va_start(args, format); vsnprintf(message, sizeof(message), format, args); va_end(args);
    fd_engine_log(message); I_SafeExit(-1);
}
void I_ErrorMsg(void) {}
void I_MessageBox(const char *format, ...) {
    char message[2048]; va_list args;
    va_start(args, format); vsnprintf(message, sizeof(message), format, args); va_end(args);
    fd_engine_log(message);
}
void *I_Realloc(void *memory, size_t size) {
    void *result = realloc(memory, size);
    if (!result && size) I_Error("Out of memory");
    return result;
}
ticcmd_t *I_BaseTiccmd(void) { static ticcmd_t empty; return &empty; }
const char *I_GetPlatform(void) { return "Pragtical"; }
void I_SetMetadata(const char *name, const char *version, const char *id) { (void)name; (void)version; (void)id; }
boolean I_GetMemoryValue(unsigned offset, void *value, int size) {
    static const unsigned char bytes[] = {0x9e, 0x0f, 0xc9, 0x00, 0x65, 0x04, 0x70, 0x00, 0x16, 0x00};
    if (size != 1 && size != 2 && size != 4) return false;
    if (offset > sizeof(bytes) || (unsigned)size > sizeof(bytes) - offset) return false;
    uint32_t result = 0;
    for (int i = 0; i < size; i++) result |= (uint32_t)bytes[offset + i] << (8 * i);
    if (size == 1) *(uint8_t *)value = result;
    else if (size == 2) *(uint16_t *)value = result;
    else *(uint32_t *)value = result;
    return true;
}
