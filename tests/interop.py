#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Optional interoperability matrix using unmodified, separately installed source ports."""
import argparse
import multiprocessing as mp
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import time
from network import Peer

def port():
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.bind(('127.0.0.1', 0))
        return sock.getsockname()[1]

def stop(process):
    if process is not None and process.poll() is None:
        process.terminate()
        try: process.wait(timeout=5)
        except subprocess.TimeoutExpired: process.kill(); process.wait()

def run(args, server, client=None, stock_controller=False, phase=2, expected_error=False):
    root = Path(__file__).resolve().parent.parent
    assets = root / 'iwads'
    peers = [Peer(mp.get_context('spawn'), args.library, str(assets)) for _ in range(1 if client else 2)]
    relay = stock = None
    with tempfile.TemporaryDirectory(prefix='freedoom-interop-') as state:
        env = dict(os.environ, HOME=state, XDG_CONFIG_HOME=state, SDL_VIDEODRIVER='dummy',
            SDL_VIDEO_DRIVER='dummy', SDL_AUDIODRIVER='dummy', SDL_RENDER_DRIVER='software', ALSOFT_DRIVERS='null')
        with open(Path(state) / 'server.log', 'w+') as relay_log, open(Path(state) / 'client.log', 'w+') as stock_log:
            try:
                endpoint = port()
                command = [server, '-privateserver', '-port', str(endpoint)]
                if Path(server).name == 'woof': command.append('-dedicated')
                relay = subprocess.Popen(command, cwd=state, env=env, stdout=relay_log, stderr=subprocess.STDOUT)
                time.sleep(.15)
                assert relay.poll() is None, 'Dedicated server exited'
                def start_stock():
                    options = [client, '-iwad', args.iwad or str(assets / f'freedoom{phase}.wad'), '-nosound', '-nogui',
                        '-config', state + '/doom.cfg', '-extraconfig', state + '/extra.cfg',
                        '-connect', '127.0.0.1:' + str(endpoint), '-nodes', '2']
                    return subprocess.Popen(options, cwd=state, env=env, stdout=stock_log, stderr=subprocess.STDOUT)
                if stock_controller:
                    stock = start_stock(); time.sleep(.25)
                for index, peer in enumerate(peers):
                    value = {'mode': 2, 'port': endpoint, 'phase': phase, 'name': 'Embedded' + str(index),
                        'deathmatch': args.deathmatch}
                    if args.iwad: value['iwad'] = args.iwad
                    assert peer.call('start', value)
                    peer.wait(lambda s: s['phase'] == 2 or s['phase'] == 3)
                if client and not stock_controller: stock = start_stock()
                peers[0].wait(lambda s: s['num_players'] == 2)
                if not stock_controller:
                    assert peers[0].call('configure', {'deathmatch': args.deathmatch, 'map': 2, 'skill': 4})
                    assert peers[0].call('launch') == 1
                if expected_error:
                    peers[0].failed('Deathmatch 3.0')
                    print('PASS unsupported Deathmatch 3.0 rejected by', Path(server).name, flush=True)
                    return
                for peer in peers:
                    state = peer.wait(lambda s: s['phase'] == 3)
                    if not stock_controller:
                        assert state['map'] == 2 and state['skill'] == 4, state
                    peer.visible_frame()
                peers[0].call('input', b'M\x18\0\0' * 4 + b'K\x01\xad\0')
                for peer in peers:
                    peer.wait(lambda s: s['tic'] > 160, timeout=20)
                    peer.visible_frame()
                if stock:
                    assert stock.poll() is None, 'Stock client exited during gameplay'
                    stock_log.flush(); stock_log.seek(0)
                    assert 'consistency failure' not in stock_log.read().lower()
                print('PASS', Path(server).name, 'Phase', phase, 'stock-controller' if stock_controller else 'embedded-controller',
                    Path(client).name if client else 'two embedded clients', flush=True)
            except Exception:
                for log in (relay_log, stock_log):
                    log.flush(); log.seek(0); print(log.read(), flush=True)
                raise
            finally:
                for peer in reversed(peers): peer.close()
                stop(stock); stop(relay)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('library', type=lambda value: str(Path(value).resolve()))
    parser.add_argument('--server', action='append', required=True)
    parser.add_argument('--client', action='append', default=[])
    parser.add_argument('--iwad', type=lambda value: str(Path(value).resolve()))
    parser.add_argument('--deathmatch', type=int, default=0)
    args = parser.parse_args()
    for server in args.server:
        unsupported = args.deathmatch == 3 and Path(server).name in ('chocolate-server', 'crispy-server')
        for phase in ((2,) if args.iwad else (1, 2)): run(args, server, phase=phase, expected_error=unsupported)
    for client in args.client:
        server = str(Path(client).with_name(Path(client).name.replace('-doom', '-server')))
        for controller in (False, True): run(args, server, client, controller)

if __name__ == '__main__': main()
