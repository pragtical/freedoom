/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "i_input.h"
#include "i_gamepad.h"
#include "d_event.h"
#include "i_rumble.h"
#include "i_endoom.h"
#include "i_system.h"
#include "m_array.h"
static const char **gamepad_names;
int I_GetAxisState(SDL_GamepadAxis axis) { (void)axis; return 0; }
boolean I_UseGamepad(void) { return false; }
boolean I_GyroSupported(void) { return false; }
void I_FlushGamepadSensorEvents(void) {}
void I_FlushGamepadEvents(void) {}
void I_SetSensorEventState(boolean enabled) { (void)enabled; }
void I_SetSensorsEnabled(boolean enabled) { (void)enabled; }
void I_InitGamepad(void) {}
void I_OpenGamepad(int index) { (void)index; }
void I_CloseGamepad(SDL_JoystickID id) { (void)id; }
void I_UpdateGamepadDevice(boolean input) { (void)input; }
const char **I_GamepadDeviceList(void) {
    /* Match Woof's no-device path: menus index the saved preference and
       inspect the growable array's length, not a NULL terminator. */
    joy_device = CLAMP(joy_device, 0, 255);
    array_clear(gamepad_names);
    for (int i = 0; i <= joy_device; i++) array_push(gamepad_names, "None");
    return gamepad_names;
}
boolean I_GamepadDevices(void) { return false; }
void I_ReadMouse(void) {}
void I_ReadGyro(void) {}
void I_UpdateGamepad(enum evtype_e type, boolean buttons) { (void)type; (void)buttons; }
void I_DelayEvent(void) {}
void I_HandleSensorEvent(SDL_Event *event) { (void)event; }
void I_HandleGamepadEvent(SDL_Event *event, boolean menu) { (void)event; (void)menu; }
void I_HandleKeyboardEvent(SDL_Event *event) { (void)event; }
void I_HandleMouseEvent(SDL_Event *event) { (void)event; }
void I_InitKeyboard(void) {}
void I_StartTextInput(void) {}
void I_StopTextInput(void) {}
void I_BindKeyboardVariables(void) {}
void I_ShutdownRumble(void) {}
void I_InitRumble(void) {}
void I_CacheRumble(struct sfxinfo_s *sfx, int format, const byte *data, int size, int rate) {
    (void)sfx; (void)format; (void)data; (void)size; (void)rate;
}
boolean I_RumbleEnabled(void) { return false; }
boolean I_RumbleSupported(void) { return false; }
void I_RumbleMenuFeedback(void) {}
void I_UpdateRumbleEnabled(void) {}
void I_SetRumbleSupported(SDL_Gamepad *gamepad) { (void)gamepad; }
void I_ResetRumbleChannel(int handle) { (void)handle; }
void I_ResetAllRumbleChannels(void) {}
void I_UpdateRumble(void) {}
void I_UpdateRumbleParams(const struct mobj_s *listener, const struct mobj_s *origin, int handle) {
    (void)listener; (void)origin; (void)handle;
}
void I_StartRumble(const struct mobj_s *listener, const struct mobj_s *origin,
                  const struct sfxinfo_s *sfx, int handle, rumble_type_t type) {
    (void)listener; (void)origin; (void)sfx; (void)handle; (void)type;
}
void I_DisableRumble(void) {}
void I_BindRumbleVariables(void) {}
void I_Endoom(byte *data) { (void)data; }
