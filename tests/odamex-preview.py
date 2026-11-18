#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
from pathlib import Path
import os
import socket
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
iwad = root / 'iwads/freedoom2.wad'
test, library, wad = (Path(arg).resolve() for arg in sys.argv[1:])
with tempfile.TemporaryDirectory(prefix='freedoom-odamex-') as directory:
    state = Path(directory)
    client_state = state / 'client'
    client_state.mkdir()
    (client_state / 'odamex.cfg').write_text('set "vid_fullscreen" "1"\n', encoding='ascii')
    client = [test, library, wad.parent, client_state, iwad]
    reference = os.getenv('FREEDOOM_ODAMEX_TEST_SERVER')
    if not reference:
        subprocess.run(client, check=True)
    else:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
            probe.bind(('127.0.0.1', 0))
            port = probe.getsockname()[1]
        with (state / 'server.log').open('w+') as log:
            server = subprocess.Popen([Path(reference).resolve(), '-iwad', iwad, '-waddir',
                wad.parent, '-config', state / 'server.cfg', '-port', str(port),
                '+set', 'sv_usemasters', '0', '+set', 'sv_upnp', '0'],
                cwd=state, env=dict(os.environ, HOME=directory), stdin=subprocess.PIPE,
                stdout=log, stderr=subprocess.STDOUT)
            try:
                subprocess.run([*client, f'127.0.0.1:{port}'], check=True, timeout=24)
                assert server.poll() is None, 'Stock server exited'
            except (subprocess.SubprocessError, AssertionError):
                log.seek(0)
                print(log.read(), file=sys.stderr)
                raise
            finally:
                server.terminate()
                try:
                    server.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    server.kill()
                    server.wait()
