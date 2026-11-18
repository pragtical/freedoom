#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise the installed plugin through a stock server and a local WAD download."""
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import os
from pathlib import Path
import re
import socket
import struct
import subprocess
import sys
import tempfile
import threading

root = Path(__file__).resolve().parent.parent
plugin, reference = (Path(arg).resolve() for arg in sys.argv[1:])
assets = root / "iwads"
if capture_directory := os.getenv("FREEDOOM_CAPTURE_DIR"):
    Path(capture_directory).mkdir(parents=True, exist_ok=True)

with tempfile.TemporaryDirectory(prefix="freedoom-editor-odamex-") as directory:
    profile = Path(directory)
    (profile / "plugins").mkdir()
    (profile / "plugins/freedoom").symlink_to(plugin, target_is_directory=True)
    state = profile / "odamex-state/odamex/phase2"
    state.mkdir(parents=True)
    (state / "odamex.cfg").write_text('set "vid_fullscreen" "1"\n', encoding="ascii")
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
    master = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    master.bind(("127.0.0.1", 0))
    master.settimeout(.1)
    done = threading.Event()

    def directory_server():
        while not done.is_set():
            try:
                data, endpoint = master.recvfrom(128)
            except TimeoutError:
                continue
            if data == struct.pack("<I", 777123):
                master.sendto(struct.pack("<IH4BH", 777123, 1, 127, 0, 0, 1, port), endpoint)

    wad = profile / "fdqa.wad"
    wad_bytes = struct.pack("<4sIIcII8s", b"PWAD", 1, 13, b"X", 12, 1, b"FDQATEST")
    wad.write_bytes(wad_bytes)
    web = ThreadingHTTPServer(("127.0.0.1", 0), partial(SimpleHTTPRequestHandler, directory=profile))
    workers = [threading.Thread(target=directory_server), threading.Thread(target=web.serve_forever)]
    for thread in workers:
        thread.start()
    with (profile / "server.log").open("w+") as log:
        server = subprocess.Popen([reference, "-iwad", assets / "freedoom2.wad", "-file", wad,
            "-waddir", plugin, "-config", profile / "server.cfg", "-port", str(port),
            "+set", "sv_usemasters", "0", "+set", "sv_upnp", "0", "+set", "sv_hostname", "Freedoom QA",
            "+set", "sv_downloadsites", f"http://127.0.0.1:{web.server_port}/",
            "+set", "join_password", "test-password"], cwd=profile,
            env=dict(os.environ, HOME=directory), stdin=subprocess.PIPE, stdout=log, stderr=subprocess.STDOUT)
        try:
            env = dict(os.environ, PRAGTICAL_USERDIR=directory, FREEDOOM_ODAMEX_MASTER=f"127.0.0.1:{master.getsockname()[1]}")
            subprocess.run([os.getenv("PRAGTICAL_BIN", "pragtical"), "run", "-n",
                root / "tests/editor-odamex.lua"], env=env, check=True, timeout=55)
            downloads = list((profile / "odamex-state").rglob("fdqa.wad"))
            assert len(downloads) == 1 and downloads[0].read_bytes() == wad_bytes, downloads
            assert not list((profile / "odamex-state").rglob("*.part.*"))
            saved = (state / "odamex.cfg").read_text()
            assert f'set "cl_name" "{"W" * 14}w"' in saved, "Player Setup did not save the edited name"
            color = re.search(r'^set "cl_customcolor" "([0-9a-f]{2}) ([0-9a-f]{2}) [0-9a-f]{2}"',
                              saved, flags=re.MULTILINE | re.IGNORECASE)
            assert color, "Player Setup did not save the custom color"
            red, green = (int(channel, 16) for channel in color.groups())
            assert 65 < red < 140 and 130 < green < 206, (red, green)
        except (subprocess.SubprocessError, AssertionError):
            log.seek(0)
            print(log.read(), file=sys.stderr)
            for path in profile.glob("odamex-state/**/odamex.log"):
                print(path.read_text(errors="replace")[-12000:], file=sys.stderr)
            raise
        finally:
            server.terminate()
            try:
                server.wait(timeout=3)
            except subprocess.TimeoutExpired:
                server.kill()
                server.wait()
            done.set()
            web.shutdown()
            for thread in workers:
                thread.join()
            web.server_close()
            master.close()
