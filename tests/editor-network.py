#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Pair one real editor with an isolated native peer, one run at a time."""
import multiprocessing as mp
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time
from network import Peer
from internet import Directory

root = Path(__file__).resolve().parent.parent
library = str(Path(sys.argv[1]).resolve())
profile = str(Path(sys.argv[2]).resolve())
assets = root / "iwads"

def main():
    for role in ("host", "join", "internet"):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
            probe.bind(("127.0.0.1", 0)); port = probe.getsockname()[1]
        peer = Peer(mp.get_context("spawn"), library, str(assets))
        editor, directory = None, None
        ready_dir = tempfile.TemporaryDirectory(prefix="freedoom-editor-ready-")
        ready = Path(ready_dir.name) / "host"
        try:
            if role != "host":
                assert peer.call("start", {"mode": 1, "port": port, "deathmatch": 1, "name": "Native", "phase": 1})
                peer.wait(lambda s: s["phase"] == 2)
            if role == "internet":
                directory = Directory()
                with directory.lock: directory.entries = [f"127.0.0.1:{port}"]
                directory.room(players=1, mode=2, mission=2)
                directory.room(players=1, mode=2, mission=7, version="Chocolate Hexen 3.1.1")
                for _ in range(8):
                    directory.room(players=1, mode=2, mission=7, version="Chocolate Hexen 3.1.1")
            environment = dict(os.environ, PRAGTICAL_USERDIR=profile,
                FREEDOOM_TEST_PORT=str(port), FREEDOOM_TEST_ROLE=role,
                FREEDOOM_TEST_READY=str(ready))
            if directory: environment["FREEDOOM_TEST_MASTER"] = directory.address
            editor = subprocess.Popen([os.getenv("PRAGTICAL_BIN", "pragtical"), "run", "-n",
                str(root / "tests/editor-network.lua")], env=environment)
            if role == "host":
                deadline = time.monotonic() + 25
                while not ready.exists():
                    assert editor.poll() is None, "Editor exited before hosting"
                    assert time.monotonic() < deadline, "Editor did not finish configuring the room"
                    time.sleep(.02)
                assert peer.call("start", {"mode": 2, "port": port, "name": "Native", "phase": 1})
            peer.wait(lambda s: s["phase"] == 2 and s["num_players"] == 2, timeout=25)
            if role != "host": assert peer.call("launch") == 1
            peer.wait(lambda s: s["phase"] == 3, timeout=20)
            assert editor.wait(timeout=40) == 0
        finally:
            if editor and editor.poll() is None:
                editor.terminate()
                try: editor.wait(timeout=10)
                except subprocess.TimeoutExpired: editor.kill(); editor.wait()
            peer.close()
            if directory: directory.close()
            ready_dir.cleanup()

if __name__ == "__main__": main()
