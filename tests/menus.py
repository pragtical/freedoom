#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise mouse sliders and adjacent tabs with unavailable saved gamepads."""
import ctypes as c
from pathlib import Path
import sys
import tempfile
import time

library = Path(sys.argv[1]).resolve()
root = Path(__file__).resolve().parent.parent
native = c.CDLL(str(library))
native.fd_start.argtypes = [c.c_char_p, c.c_char_p, c.c_char_p, c.c_int]
native.fd_start.restype = c.c_void_p
native.fd_status.argtypes = [c.c_void_p]
native.fd_close.argtypes = [c.c_void_p]
native.fd_input.argtypes = [c.c_void_p, c.c_char_p, c.c_uint]
native.fd_frame_reader.argtypes = [c.c_void_p]
native.fd_frame_reader.restype = c.c_void_p
native.fd_copy_frame.argtypes = [c.c_void_p, c.c_void_p, c.c_uint, c.c_uint, c.c_void_p, c.c_int]

def key(session, code):
    assert native.fd_input(session, bytes((ord("K"), 1, code, 0)), 4) == 4
    time.sleep(.07)
    assert native.fd_input(session, bytes((ord("K"), 0, code, 0)), 4) == 4
    time.sleep(.13)
    assert native.fd_status(session) == 1

def snapshot(session):
    pixels, serial = (c.c_uint32 * (320 * 240))(), c.c_uint32()
    deadline = time.monotonic() + 2
    while native.fd_copy_frame(native.fd_frame_reader(session), pixels, 320, 240, c.byref(serial), 0) == 0:
        assert time.monotonic() < deadline
        time.sleep(.01)
    assert serial.value and len(set(pixels)) > 16, "Menu frame was blank"

iwad = root / "iwads/freedoom2.wad"
sensitivities = ("mouse_sensitivity", "mouse_sensitivity_y_look",
    "mouse_sensitivity_y", "mouse_sensitivity_strafe")
for device in (1, 0, 255):
    with tempfile.TemporaryDirectory(prefix="freedoom-menus-") as state:
        config = Path(state) / "woof.cfg"
        config.write_text(f"joy_device {device}\n" + "".join(f"{name} 15\n" for name in sensitivities))
        for cycle in range(2):
            session = native.fd_start(str(library.parent).encode(), state.encode(), str(iwad).encode(), 0)
            assert session
            try:
                deadline = time.monotonic() + 10
                while native.fd_status(session) == 0 and time.monotonic() < deadline:
                    time.sleep(.01)
                assert native.fd_status(session) == 1
                time.sleep(1.2)
                # Main > Options > General. Right changes pages until Enter
                # selects a slider; this is also the route into Gamepad.
                for code in (27, ord("o"), 13, ord("g"), 13, 0xae, 0xae):
                    key(session, code)
                for _ in sensitivities:
                    for code in (13, 0xae, 0xae, 0xac, 13, 0xaf):
                        key(session, code)
                snapshot(session)
                print(f"device={device} cycle={cycle + 1}: mouse sliders changed", flush=True)
                key(session, 0xae)
                snapshot(session)
                print(f"device={device} cycle={cycle + 1}: gamepad tab rendered", flush=True)
                key(session, 0xac)
                snapshot(session)
                key(session, 27)
            finally:
                native.fd_close(session)
            saved = dict(line.split()[:2] for line in config.read_text().splitlines()
                if line and not line.startswith(";") and len(line.split()) >= 2)
            assert all(saved[name] == str(16 + cycle) for name in sensitivities), saved
            assert saved["joy_device"] == str(device), "Saved device preference changed"
print("PASS mouse sensitivity changes, adjacent tabs, unavailable devices and restarts")
