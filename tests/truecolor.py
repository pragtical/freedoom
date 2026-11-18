#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check native-resolution RGB output without counting interpolated pixels."""
import ctypes as c
import os
from pathlib import Path
import sys
import tempfile
import time

library = Path(sys.argv[1]).resolve()
root = Path(__file__).resolve().parent.parent
iwad = root / "iwads/freedoom2.wad"
native = c.CDLL(str(library))
native.fd_start.argtypes = [c.c_char_p, c.c_char_p, c.c_char_p, c.c_int]
native.fd_start.restype = c.c_void_p
native.fd_frame_reader.argtypes = [c.c_void_p]
native.fd_frame_reader.restype = c.c_void_p
native.fd_status.argtypes = native.fd_close.argtypes = [c.c_void_p]
native.fd_set_truecolor.argtypes = [c.c_void_p, c.c_int]
native.fd_input.argtypes = [c.c_void_p, c.c_char_p, c.c_uint]
native.fd_copy_frame.argtypes = [c.c_void_p, c.c_void_p, c.c_uint, c.c_uint, c.c_void_p, c.c_int]
native.fd_read_log.argtypes = [c.c_void_p, c.c_void_p, c.c_uint]


def input_(session, data):
    assert native.fd_input(session, data, len(data)) == len(data)


def key(session, value):
    input_(session, bytes((ord("K"), 1, value, 0)))
    time.sleep(.06)
    input_(session, bytes((ord("K"), 0, value, 0)))
    time.sleep(.15)


def snapshot(session, width, height):
    pixels = (c.c_uint32 * (width * height))()
    serial = c.c_uint32()
    deadline = time.monotonic() + 3
    while time.monotonic() < deadline:
        result = native.fd_copy_frame(native.fd_frame_reader(session), pixels, width, height, c.byref(serial), 0)
        assert result >= 0
        if result:
            return pixels
        time.sleep(.002)
    raise AssertionError("No native frame became available")


def capture(pixels, width, height, name):
    if capture_dir := os.getenv("FREEDOOM_CAPTURE_DIR"):
        captures = Path(capture_dir)
        captures.mkdir(parents=True, exist_ok=True)
        data = bytes(pixels)
        (captures / f"{name}.ppm").write_bytes(f"P6\n{width} {height}\n255\n".encode()
            + b"".join(data[offset:offset+3] for offset in range(0, len(data), 4)))


def enter_level(session):
    deadline = time.monotonic() + 10
    while native.fd_status(session) == 0 and time.monotonic() < deadline:
        time.sleep(.01)
    log = c.create_string_buffer(8192)
    native.fd_read_log(session, log, len(log))
    assert native.fd_status(session) == 1, log.value
    time.sleep(1.5)

    for value in (27, 13, 13):
        key(session, value)
    time.sleep(1.4)


with tempfile.TemporaryDirectory(prefix="freedoom-color-") as directory:
    config = Path(directory) / "woof.cfg"
    config.write_text("screen_melt 2\n", encoding="ascii")
    args = (os.fsencode(library.parent), os.fsencode(directory), os.fsencode(iwad), 0)
    session = native.fd_start(*args)
    assert session
    try:
        enter_level(session)
        count = len(set(snapshot(session, 960, 600)))
        assert count > 256, ("true color is not on by default", count)

        # Escape > Options > General > Video > True Color (select, toggle).
        for value in (27, ord("o"), 13, ord("g"), 13, 13, 13):
            key(session, value)
        capture(snapshot(session, 960, 600), 960, 600, "video-options")
        key(session, 27)
        pixels = snapshot(session, 960, 600)
        count = len(set(pixels))
        assert 32 < count <= 256, ("menu did not disable true color", count)
        capture(pixels, 960, 600, "indexed")
        assert native.fd_set_truecolor(session, 1)

        frames = []
        for w, h in ((960, 720), (1280, 768)):
            input_(session, bytes((ord("R"), w & 255, (w >> 8) | ((h & 15) << 4), h >> 4)))
            time.sleep(.25)
            height = h * 5 // 6
            pixels = snapshot(session, w, height)
            count = len(set(pixels))
            frames.append(f"{w}x{height}: {count} colors")
            capture(pixels, w, height, f"truecolor-{w}")
            assert count > 256, ("native toggle did not enable true color", w, height, count)

        key(session, 9)   # Full-screen automap.
        assert len(set(snapshot(session, 1280, 640))) > 4
        key(session, 9)
        key(session, 27)  # Menu shading over the live view.
        assert len(set(snapshot(session, 1280, 640))) > 32
        for value in (27, 0xbc, 13, 13):  # Close menu, F2, select slot, save.
            key(session, value)
        assert list(Path(directory).rglob("*.dsg")), "No savegame was written"
        key(session, 0xbd)  # F3, render the indexed thumbnail, then load it.
        assert len(set(snapshot(session, 1280, 640))) > 32
        key(session, 13)
        time.sleep(1.4)
        assert len(set(snapshot(session, 1280, 640))) > 32
        assert native.fd_set_truecolor(session, 0)
        time.sleep(.25)
        count = len(set(snapshot(session, 1280, 640)))
        assert 32 < count <= 256, ("native toggle did not disable true color", count)
        assert native.fd_status(session) == 1
        print(", ".join(frames))
    finally:
        native.fd_close(session)
    assert ["truecolor", "0"] in [line.split() for line in config.read_text().splitlines()]

    session = native.fd_start(*args)
    assert session
    try:
        enter_level(session)
        count = len(set(snapshot(session, 960, 600)))
        assert 32 < count <= 256, ("saved Off setting was not respected", count)
    finally:
        native.fd_close(session)
