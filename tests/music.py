#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check OPL loudness compensation, explicit gain and mute on real game music."""
import ctypes as c
import math
from pathlib import Path
import sys
import tempfile
import time

root = Path(__file__).resolve().parent.parent
native = c.CDLL(str(Path(sys.argv[1]).resolve()))
native.fd_start.argtypes = [c.c_char_p, c.c_char_p, c.c_char_p, c.c_int]
native.fd_start.restype = c.c_void_p
native.fd_status.argtypes = [c.c_void_p]
native.fd_close.argtypes = [c.c_void_p]
native.fd_input.argtypes = [c.c_void_p, c.c_char_p, c.c_uint]
native.fd_music_read.argtypes = [c.c_void_p, c.c_void_p, c.c_uint, c.c_uint]

class Voice(c.Structure):
    _fields_ = [(name, c.c_uint) for name in ("generation", "sample", "playing", "paused", "looping")] + [
        (name, c.c_float) for name in ("gain", "pan", "rate", "offset")]

class Audio(c.Structure):
    _fields_ = [("voices", Voice * 32), ("music_generation", c.c_uint),
        ("music_paused", c.c_uint), ("music_gain", c.c_float)]

native.fd_audio_snapshot.argtypes = [c.c_void_p, c.POINTER(Audio)]

def measure(phase, auto, gain=0, volume=8, gameplay=False):
    iwad = root / f"iwads/freedoom{phase}.wad"
    with tempfile.TemporaryDirectory(prefix="freedoom-music-") as state:
        (Path(state) / "woof.cfg").write_text(
            f"auto_gain {auto}\nopl_gain {gain}\nmusic_volume {volume}\n")
        session = native.fd_start(str(Path(sys.argv[2]).resolve()).encode(), state.encode(), str(iwad).encode(), 1)
        assert session
        try:
            deadline = time.monotonic() + 10
            while native.fd_status(session) == 0 and time.monotonic() < deadline:
                time.sleep(.01)
            assert native.fd_status(session) == 1
            if gameplay:
                time.sleep(1.2)
                for key in (27, 13, 13) + ((13,) if phase == 1 else ()):
                    assert native.fd_input(session, bytes((ord("K"), 1, key, 0)), 4) == 4
                    time.sleep(.07)
                    assert native.fd_input(session, bytes((ord("K"), 0, key, 0)), 4) == 4
                    time.sleep(.15)
                time.sleep(1)
            audio, pcm = Audio(), (c.c_int16 * 8192)()
            deadline = time.monotonic() + 3.5
            peak, total, count, effective = 0, 0, 0, 0
            while time.monotonic() < deadline:
                if native.fd_audio_snapshot(session, c.byref(audio)):
                    frames = native.fd_music_read(session, pcm, 4096, audio.music_generation)
                    samples = pcm[:frames * 2]
                    if samples:
                        peak = max(peak, max(abs(value) for value in samples) / 32768)
                        total += sum(value * value for value in samples)
                        count += len(samples)
                    effective = audio.music_gain
                time.sleep(.01)
            assert count and total, "OPL stream was silent"
            rms = math.sqrt(total / count) / 32768
            print(f"phase={phase} gameplay={gameplay} auto={auto} opl_gain={gain} volume={volume} "
                f"raw_rms={20 * math.log10(rms):.2f}dBFS peak={peak:.3f} gain={effective:.3f}", flush=True)
            return effective, peak
        finally:
            native.fd_close(session)

raw, _ = measure(2, 0)
assert math.isclose(raw, 8 / 15, rel_tol=1e-5), raw
manual, _ = measure(2, 0, gain=6)
assert math.isclose(manual, raw * 10 ** (6 / 20), rel_tol=1e-5), manual
for phase in (1, 2):
    normalized, peak = measure(phase, 1)
    assert not math.isclose(normalized, raw, rel_tol=.01), "OPL music was not normalized"
    assert math.isfinite(normalized) and normalized <= 10 * raw
    assert normalized * peak <= .95 * raw + .001, "Normalization lost peak headroom"
muted, _ = measure(2, 1, volume=0)
assert muted == 0, "Normalization bypassed the music-volume slider"
raw_level, _ = measure(2, 0, gameplay=True)
normalized, peak = measure(2, 1, gameplay=True)
assert math.isfinite(normalized) and normalized > 0
assert not math.isclose(normalized, raw_level, rel_tol=.01)
assert normalized * peak <= .95 * raw_level + .001
print("PASS music loudness, both IWADs, manual gain, peak headroom and mute")
