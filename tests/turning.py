#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Constant mouse turning must not make the network camera jump backward."""
import multiprocessing as mp
from pathlib import Path
import socket
import statistics
import sys
import time
from network import Peer

def main():
    root = Path(__file__).resolve().parent.parent
    assets = root / 'iwads'
    peer = Peer(mp.get_context('spawn'), str(Path(sys.argv[1]).resolve()), str(assets))
    rows = []
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.bind(('127.0.0.1', 0)); port = sock.getsockname()[1]
        assert peer.call('start', {'mode': 1, 'port': port, 'skill': 1})
        peer.wait(lambda s: s['phase'] == 2)
        assert peer.call('launch')
        peer.wait(lambda s: s['phase'] == 3 and s['tic'] > 80)
        assert peer.call('input', bytes((ord('R'), 0, 5, 45))) == 4
        time.sleep(.1)
        for frame in range(150):
            assert peer.call('input', b'M\x04\0\0F\x01\0\0') == 8
            deadline = time.monotonic() + 2
            while True:
                row = peer.call('row')
                if row: break
                assert time.monotonic() < deadline, 'No new camera frame'
                time.sleep(.001)
            rows.append(row[::4])
            time.sleep(1 / 120)
    finally:
        peer.close()
    shifts = []
    # A static, textured ceiling strip measures actual rendered motion, not
    # merely changing serials or animated sprites. Ignore startup transients.
    for before, after in zip(rows[10:-1], rows[11:]):
        best = min((sum(abs(before[x] - after[x + shift]) for x in range(426, 853)),
            abs(shift), shift) for shift in range(-24, 25))
        shifts.append(best[2])
    direction = statistics.median(shifts)
    assert direction != 0, 'Mouse input did not turn the camera'
    reversed_frames = sum(shift * direction < 0 for shift in shifts)
    assert reversed_frames <= 2, f'Camera reversed {reversed_frames}/{len(shifts)} frames: {shifts}'
    print(f'PASS network mouse camera: {reversed_frames}/{len(shifts)} reversed frames')

if __name__ == '__main__': main()
