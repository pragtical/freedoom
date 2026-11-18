#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise pack selection and online IWAD switching with read-only owned assets."""
import multiprocessing as mp
import os
from pathlib import Path
import socket
import subprocess
import sys
import time
from network import Peer

def main():
    library, profile, iwads = (str(Path(value).resolve()) for value in sys.argv[1:4])
    root = Path(__file__).resolve().parent.parent
    assets = root / 'iwads'
    script = str(root / 'tests/editor-games.lua')
    env = dict(os.environ, PRAGTICAL_USERDIR=profile, FREEDOOM_TEST_IWAD_DIR=iwads, FREEDOOM_TEST_ROLE='picker')
    subprocess.run([os.getenv('PRAGTICAL_BIN', 'pragtical'), 'run', '-n', script], env=env, check=True, timeout=60)
    for filename in ('DOOM.WAD', 'DOOM2.WAD', 'TNT.WAD', 'PLUTONIA.WAD'):
        peer = Peer(mp.get_context('spawn'), library, str(assets))
        editor = None
        ready = Path(profile) / 'game-ready'
        ready.unlink(missing_ok=True)
        try:
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
                probe.bind(('127.0.0.1', 0)); port = probe.getsockname()[1]
            iwad = str(Path(iwads) / filename)
            assert peer.call('start', {'mode': 1, 'port': port, 'iwad': iwad, 'skill': 1})
            peer.wait(lambda s: s['phase'] == 2)
            env.update(FREEDOOM_TEST_ROLE='online', FREEDOOM_TEST_PORT=str(port), FREEDOOM_TEST_IWAD=iwad)
            editor = subprocess.Popen([os.getenv('PRAGTICAL_BIN', 'pragtical'), 'run', '-n', script], env=env)
            deadline = time.monotonic() + 35
            while not ready.is_file():
                assert editor.poll() is None, 'Editor exited before selecting the matching IWAD'
                assert time.monotonic() < deadline, peer.call('state')
                time.sleep(.03)
            assert peer.call('launch')
            peer.wait(lambda s: s['phase'] == 3 and s['tic'] > 80)
            assert editor.wait(timeout=30) == 0
        finally:
            if editor and editor.poll() is None:
                editor.terminate()
                try: editor.wait(timeout=5)
                except subprocess.TimeoutExpired: editor.kill(); editor.wait()
            peer.close()

if __name__ == '__main__': main()
