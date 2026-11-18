/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "odamex.h"
#include "c_bind.h"
#include "c_cvars.h"
#include "d_main.h"
#include "doomkeys.h"
#include "doomstat.h"
#include "i_input.h"
#include "i_time.h"
#include "hooks.h"

namespace {
int repeat_key;
dtime_t next_repeat;
}

bool I_InitInput() { return true; }
void I_ShutdownInput() {}
void I_ForceUpdateGrab() {}
void I_FlushInput()
{
    repeat_key = 0;
    C_ReleaseKeys();
}
size_t I_GetJoystickCount() { return 0; }
std::string I_GetJoystickNameFromIndex(int) { return {}; }
bool I_OpenJoystick() { return false; }
void I_CloseJoystick() {}
CVAR_FUNC_IMPL(use_joystick) {}
CVAR_FUNC_IMPL(joy_active) {}
bool I_GetUIMousePosition(int &, int &) { return false; }
bool I_IsUIMouseButtonDown(int) { return false; }

static int keycode(unsigned char code)
{
    switch (code) {
        case 0xa3: return OKEY_LCTRL;
        case 0xb6: return OKEY_LSHIFT;
        case 0xa2: return OKEY_SPACE;
        case 0xad: return OKEY_UPARROW;
        case 0xaf: return OKEY_DOWNARROW;
        case 0xac: return OKEY_LEFTARROW;
        case 0xae: return OKEY_RIGHTARROW;
        case 0xd7: return OKEY_F11;
        case 127: return OKEY_BACKSPACE;
        default: return code >= 0xbb && code <= 0xc4 ? OKEY_F1 + code - 0xbb : code;
    }
}

void I_GetEvents(bool mouseOnly)
{
    if (mouseOnly) return;
    if (!menuactive) repeat_key = 0;
    unsigned char packet[4];
    int x = 0, y = 0;
    for (unsigned i = 0; i < 16 && fd_odamex_input(packet); ++i) {
        if (packet[0] == 'K') {
            int key = keycode(packet[2]);
            const int ascii = key >= 32 && key < 127 ? key : 0;
            event_t event(packet[1] ? ev_keydown : ev_keyup, key, ascii, ascii);
            D_PostEvent(&event);
            if (packet[1]) {
                repeat_key = 0;
                if (menuactive && (key == OKEY_LEFTARROW || key == OKEY_RIGHTARROW ||
                                   key == OKEY_UPARROW || key == OKEY_DOWNARROW)) {
                    repeat_key = key;
                    next_repeat = I_GetTime() + I_ConvertTimeFromMs(500);
                }
            } else if (key == repeat_key) {
                repeat_key = 0;
            }
        } else if (packet[0] == 'M') {
            x += static_cast<int8_t>(packet[1]);
            y += static_cast<int8_t>(packet[2]);
        }
    }
    if (x || y) {
        event_t event(ev_mouse, 0, x, y);
        D_PostEvent(&event);
    }
    if (repeat_key) {
        const auto now = I_GetTime();
        if (now >= next_repeat) {
            event_t event(ev_keydown, repeat_key, 0, 0);
            D_PostEvent(&event);
            // Follow Odamex's menu cadence without bursting after a stalled tic.
            const auto interval = I_ConvertTimeFromMs(30);
            next_repeat += interval;
            if (next_repeat <= now) next_repeat = now + interval;
        }
    }
}

void I_StartTic() { I_GetEvents(false); }
