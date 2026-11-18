#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise the engine without loading it into an editor process."""
import ctypes as c
import os
from pathlib import Path
import sys
import tempfile
import time

library_path = Path(sys.argv[1]).resolve()
root = Path(__file__).resolve().parent.parent
native = c.CDLL(str(library_path))
native.fd_start.argtypes = [c.c_char_p, c.c_char_p, c.c_char_p, c.c_int]
native.fd_start.restype = c.c_void_p
native.fd_status.argtypes = [c.c_void_p]
native.fd_stop.argtypes = [c.c_void_p]
native.fd_close.argtypes = [c.c_void_p]
native.fd_read_log.argtypes = [c.c_void_p, c.c_void_p, c.c_uint]
native.fd_frame_reader.argtypes = [c.c_void_p]
native.fd_frame_reader.restype = c.c_void_p
native.fd_copy_frame.argtypes = [c.c_void_p, c.c_void_p, c.c_uint, c.c_uint, c.c_void_p, c.c_int]
native.fd_input.argtypes = [c.c_void_p, c.c_char_p, c.c_uint]
native.fd_music_read.argtypes = [c.c_void_p, c.c_void_p, c.c_uint, c.c_uint]

class Voice(c.Structure):
    _fields_ = [(name, c.c_uint) for name in ("generation", "sample", "playing", "paused", "looping")] + [
        (name, c.c_float) for name in ("gain", "pan", "rate", "offset")]

class Audio(c.Structure):
    _fields_ = [("voices", Voice * 32), ("music_generation", c.c_uint),
        ("music_paused", c.c_uint), ("music_gain", c.c_float)]

native.fd_audio_snapshot.argtypes = [c.c_void_p, c.POINTER(Audio)]
sound = os.getenv("FREEDOOM_TEST_SOUND") == "1"
working_directory = Path.cwd()
original_environment = dict(os.environ)

def wait_status(session, timeout=15):
    deadline = time.monotonic() + timeout
    while native.fd_status(session) == 0 and time.monotonic() < deadline:
        time.sleep(.01)
    return native.fd_status(session)

def input_(session, opcode, a=0, b=0, d=0):
    packet = bytes((ord(opcode), a, b, d))
    assert native.fd_input(session, packet, 4) == 4

def key(session, code):
    input_(session, "K", 1, code); time.sleep(.06)
    input_(session, "K", 0, code); time.sleep(.15)

def logs(session):
    output = c.create_string_buffer(8192)
    length = native.fd_read_log(session, output, len(output))
    return output.raw[:length].decode("utf-8", "replace")

def snapshot(reader, pixels, serial, smooth):
    # A producer holding the frame lock is a skipped read, not a failed frame.
    deadline = time.monotonic() + 2
    while True:
        result = native.fd_copy_frame(reader, pixels, 320, 240, c.byref(serial), smooth)
        assert result >= 0, "Invalid framebuffer"
        if result == 1:
            return
        assert time.monotonic() < deadline, "No completed frame became available"
        time.sleep(.01)

with tempfile.TemporaryDirectory(prefix="freedoom-engine-\u00e9-") as state:
    for iteration in range(int(sys.argv[2]) if len(sys.argv) > 2 else 1):
        phase = 2 if sound else iteration % 2 + 1
        iwad = root / f"iwads/freedoom{phase}.wad"
        assert iwad.is_file(), "Run scripts/fetch-assets.py first"
        session = native.fd_start(str(library_path.parent).encode(), state.encode(),
            str(iwad).encode(), int(sound))
        assert session, "Engine session unavailable"
        try:
            assert not native.fd_start(b".", b".", b"missing.wad", 0), "Accepted a second live engine"
            wait_status(session)
            output = logs(session)
            assert native.fd_status(session) == 1, output
            reader = native.fd_frame_reader(session)
            pixels = (c.c_uint32 * (320 * 240))()
            serial = c.c_uint32()
            snapshot(reader, pixels, serial, 1)
            if sound:
                time.sleep(1.2)
                audio = Audio()
                assert native.fd_audio_snapshot(session, c.byref(audio))
                pcm = (c.c_int16 * 8192)()
                frames = native.fd_music_read(session, pcm, 4096, audio.music_generation)
                assert frames > 0 and any(pcm[:frames * 2]), "No audible OPL PCM generated"
                key(session, 27); key(session, 13); key(session, 13)
                time.sleep(1.2)
                key(session, 0xa3)
                assert native.fd_audio_snapshot(session, c.byref(audio))
                assert any(voice.sample for voice in audio.voices), "No native sound effects"
                serial.value = 0
                snapshot(reader, pixels, serial, 1)
                assert len(set(pixels)) > 32, "Gameplay frame is blank"
            if iteration == 0:
                input_(session, "P", 1); time.sleep(.1)
                serial.value = 0
                snapshot(reader, pixels, serial, 0)
                time.sleep(.1)
                assert native.fd_copy_frame(reader, pixels, 320, 240, c.byref(serial), 0) == 0, "Pause advanced frames"
                queued = b"M\0\0\0" * 5000
                written = native.fd_input(session, queued, len(queued))
                assert 0 < written <= 16384 and written % 4 == 0
                assert native.fd_input(session, b"M\0\0\0", 4) == 0
                input_(session, "P", 0)
                input_(session, "F", 1)
                time.sleep(.1)
                snapshot(reader, pixels, serial, 0)
            native.fd_stop(session)
            deadline = time.monotonic() + 5
            while native.fd_status(session) < 2 and time.monotonic() < deadline:
                time.sleep(.01)
            assert native.fd_status(session) == 2, logs(session)
            print(f"Lifecycle {iteration + 1}: frame {serial.value}, clean shutdown", flush=True)
        finally:
            native.fd_close(session)
        if iteration % 10 == 0:
            failed = native.fd_start(str(library_path.parent).encode(), state.encode(), b"/missing/freedoom.wad", 0)
            assert failed and wait_status(failed) == 3, "Missing IWAD did not report a recoverable error"
            native.fd_close(failed)
        assert Path.cwd() == working_directory and dict(os.environ) == original_environment
    settings = {}
    expected = {"brightmaps": "1", "fuzzmode": "1", "stretchsky": "1",
        "linearsky": "1", "r_swirl": "1"}
    for line in (Path(state) / "woof.cfg").read_text().splitlines():
        fields = line.split()
        if len(fields) >= 2 and fields[0] in expected:
            settings[fields[0]] = fields[1]
    for name, value in expected.items():
        assert settings.get(name) == value, (name, settings.get(name))
