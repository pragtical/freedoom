#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run real UDP peers in separate processes with disposable engine profiles."""
import ctypes as c
import multiprocessing as mp
import os
from pathlib import Path
import signal
import socket
import sys
import tempfile
import time

class Options(c.Structure):
    _fields_ = [(key, c.c_int) for key in ("mode", "port", "deathmatch", "episode", "map", "skill")] + [
        ("address", c.c_char * 64), ("name", c.c_char * 32), ("master", c.c_char * 64)]

class Server(c.Structure):
    _fields_ = [("address", c.c_char * 64), ("description", c.c_char * 128), ("version", c.c_char * 32)] + [
        (key, c.c_uint) for key in ("players", "max_players", "state", "ping", "mission", "protocol", "compatible", "unassigned", "phase", "gamemode")]

class State(c.Structure):
    _fields_ = [(key, c.c_uint) for key in ("mode", "phase", "controller", "num_players", "max_players",
        "server_count", "revision", "tic", "player_mask", "limited")] + [
        (key, c.c_int) for key in ("consoleplayer", "deathmatch", "map", "episode", "skill")] + [
        ("message", c.c_char * 160), ("names", (c.c_char * 32) * 4),
        ("addresses", (c.c_char * 64) * 4), ("servers", Server * 32), ("wanted_iwad", c.c_ubyte * 20)]

def worker(connection, path, assets):
    signals = []
    if hasattr(signal, "SIGPIPE"):
        signal.signal(signal.SIGPIPE, lambda *_: signals.append(True))
    descriptors = []
    native = c.CDLL(path)
    native.fd_start_network.argtypes = [c.c_char_p, c.c_char_p, c.c_char_p, c.c_int, c.POINTER(Options)]
    native.fd_start_network.restype = c.c_void_p
    native.fd_status.argtypes = native.fd_stop.argtypes = native.fd_close.argtypes = [c.c_void_p]
    native.fd_network_snapshot.argtypes = [c.c_void_p, c.POINTER(State)]
    native.fd_network_launch.argtypes = native.fd_network_refresh.argtypes = [c.c_void_p]
    native.fd_network_configure.argtypes = [c.c_void_p, c.POINTER(Options)]
    native.fd_read_log.argtypes = [c.c_void_p, c.c_void_p, c.c_uint]
    native.fd_input.argtypes = [c.c_void_p, c.c_char_p, c.c_uint]
    native.fd_frame_reader.argtypes = [c.c_void_p]
    native.fd_frame_reader.restype = c.c_void_p
    native.fd_copy_frame.argtypes = [c.c_void_p, c.c_void_p, c.c_uint, c.c_uint, c.c_void_p, c.c_int]
    session = None
    log = ""
    probe_pixels, probe_serial = None, c.c_uint32()
    def stop():
        nonlocal session
        if session:
            native.fd_stop(session)
            deadline = time.monotonic() + 8
            while native.fd_status(session) < 2:
                assert time.monotonic() < deadline, "Native shutdown did not complete"
                time.sleep(.005)
            native.fd_close(session)
            session = None
            if hasattr(signal, "SIGPIPE"):
                count = len(signals)
                signal.raise_signal(signal.SIGPIPE)
                assert len(signals) == count + 1, "Engine changed the host SIGPIPE handler"
    try:
        with tempfile.TemporaryDirectory(prefix="freedoom-peer-") as directory:
            while True:
                command, value = connection.recv()
                if command == "start":
                    assert not session
                    probe_serial.value = 0
                    options = Options(mode=value.get("mode", 1), port=value["port"],
                        deathmatch=value.get("deathmatch", 0), episode=value.get("episode", 1), map=value.get("map", 1),
                        skill=value.get("skill", 3), master=value.get("master", "").encode(),
                        address=value.get("address", "127.0.0.1").encode(), name=value.get("name", "Player").encode())
                    session = native.fd_start_network(str(Path(path).parent).encode(), directory.encode(),
                        str(value.get("iwad", Path(assets) / f"freedoom{value.get('phase', 2)}.wad")).encode(), 0, c.byref(options))
                    log = ""
                    connection.send(bool(session))
                elif command == "state":
                    state = State()
                    deadline = time.monotonic() + 1
                    while not native.fd_network_snapshot(session, c.byref(state)):
                        assert time.monotonic() < deadline
                        time.sleep(.001)
                    output = c.create_string_buffer(8192)
                    count = native.fd_read_log(session, output, 8192)
                    log = (log + output.raw[:count].decode("utf-8", "replace"))[-16384:]
                    result = {key: getattr(state, key) for key in ("mode", "phase", "controller", "num_players",
                        "max_players", "server_count", "revision", "tic", "player_mask", "limited", "consoleplayer", "deathmatch", "map", "episode", "skill")}
                    result.update(status=native.fd_status(session), log=log,
                        names=[bytes(name).split(b"\0", 1)[0].decode("utf-8", "replace") for name in state.names],
                        message=state.message.decode("utf-8", "replace"),
                        servers=[{"address": item.address.decode(), "players": item.players, "state": item.state,
                            "max_players": item.max_players, "version": item.version.decode(), "protocol": item.protocol,
                            "compatible": item.compatible, "unassigned": item.unassigned, "phase": item.phase,
                            "mission": item.mission, "gamemode": item.gamemode}
                            for item in state.servers[:state.server_count]])
                    connection.send(result)
                elif command == "launch": connection.send(native.fd_network_launch(session))
                elif command == "refresh": connection.send(native.fd_network_refresh(session))
                elif command == "configure":
                    options = Options(deathmatch=value.get("deathmatch", 0), episode=value.get("episode", 1),
                        map=value.get("map", 1), skill=value.get("skill", 3))
                    connection.send(native.fd_network_configure(session, c.byref(options)))
                elif command == "input": connection.send(native.fd_input(session, value, len(value)))
                elif command == "colors":
                    pixels, serial = (c.c_uint32 * (320 * 240))(), c.c_uint32()
                    result = native.fd_copy_frame(native.fd_frame_reader(session), pixels, 320, 240, c.byref(serial), 0)
                    connection.send(len({pixel & 0xffffff for pixel in pixels[::37]}) if result == 1 else 0)
                elif command == "row":
                    if probe_pixels is None: probe_pixels = (c.c_uint32 * (1280 * 720))()
                    result = native.fd_copy_frame(native.fd_frame_reader(session), probe_pixels, 1280, 720,
                        c.byref(probe_serial), 0)
                    assert result >= 0
                    connection.send(c.string_at(c.addressof(probe_pixels) + 216 * 1280 * 4, 1280 * 4) if result else None)
                elif command == "high-fds":
                    import resource
                    if resource.getrlimit(resource.RLIMIT_NOFILE)[0] > 1100:
                        while not descriptors or descriptors[-1] < 1030:
                            descriptors.append(os.open(os.devnull, os.O_RDONLY))
                        connection.send(True)
                    else: connection.send(False)
                elif command == "stop":
                    start = time.monotonic(); stop(); connection.send(time.monotonic() - start)
                elif command == "quit": stop(); connection.send(True); return
    finally:
        stop()
        for descriptor in descriptors: os.close(descriptor)
        connection.close()

class Peer:
    def __init__(self, context, library, assets):
        self.connection, child = context.Pipe()
        self.process = context.Process(target=worker, args=(child, library, assets))
        self.process.start(); child.close()
    def call(self, command, value=None):
        self.connection.send((command, value))
        assert self.connection.poll(12), f"Peer did not respond to {command}"
        return self.connection.recv()
    def wait(self, predicate, timeout=15):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            state = self.call("state")
            assert state["status"] != 3, state["log"]
            if predicate(state): return state
            time.sleep(.02)
        raise AssertionError(state)
    def failed(self, message, timeout=12):
        deadline = time.monotonic() + timeout
        state = self.call("state")
        while state["status"] < 2 and time.monotonic() < deadline:
            time.sleep(.02); state = self.call("state")
        assert state["status"] == 3 and message in state["log"], state
    def visible_frame(self):
        deadline = time.monotonic() + 8
        while self.call("colors") < 16:
            assert time.monotonic() < deadline, "Gameplay framebuffer stayed blank"
            time.sleep(.03)
    def close(self):
        if self.process.is_alive():
            try: self.call("quit")
            except (AssertionError, EOFError, BrokenPipeError): self.process.terminate()
        self.process.join(10)
        if self.process.is_alive(): self.process.kill(); self.process.join()
        self.connection.close()

def main():
    library = str(Path(sys.argv[1]).resolve())
    root = Path(__file__).resolve().parent.parent
    assets = root / "iwads"
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
        probe.bind(("127.0.0.1", 0)); port = probe.getsockname()[1]
    peers = []
    try:
        for _ in range(4): peers.append(Peer(mp.get_context("spawn"), library, str(assets)))
        host, client, finder, fourth = peers
        if os.name != "nt" and host.call("high-fds"):
            print("Testing host UDP socket above FD_SETSIZE", flush=True)
        for mode in (0, 1, 2, 3):
            assert host.call("start", {"mode": 1, "port": port, "deathmatch": mode, "name": "Host"})
            host.wait(lambda s: s["phase"] == 2 and s["controller"])
            if mode == 0:
                assert finder.call("start", {"mode": 3, "port": port})
                found = finder.wait(lambda s: s["server_count"] > 0)
                assert found["servers"][0]["players"] == 1, found
                assert finder.call("refresh") == 1
                finder.wait(lambda s: s["server_count"] > 0 and s["revision"] > found["revision"])
                assert finder.call("stop") < 2
                print("PASS LAN broadcast discovery and refresh", flush=True)
            assert client.call("start", {"mode": 2, "port": port, "name": "Client",
                "address": "127.000.0.001"})
            host.wait(lambda s: s["num_players"] == 2)
            joined = client.wait(lambda s: s["phase"] == 2 and s["num_players"] == 2)
            assert set(joined["names"][:2]) == {"Host", "Client"}, joined
            assert not client.call("configure", {"map": 2}), "Non-controller changed room settings"
            assert not host.call("configure", {"map": 33}), "Invalid map was accepted"
            assert host.call("configure", {"deathmatch": mode, "map": 2, "skill": 2})
            host.wait(lambda s: s["map"] == 2)
            assert not client.call("launch") and host.call("launch") == 1
            a = host.wait(lambda s: s["phase"] == 3 and s["tic"] > 10)
            b = client.wait(lambda s: s["phase"] == 3 and s["tic"] > 10)
            assert a["deathmatch"] == b["deathmatch"] == mode, (a, b)
            assert a["map"] == b["map"] == 2, "Controller map change was not transmitted"
            assert a["skill"] == b["skill"] == 2, "Controller difficulty change was not transmitted"
            assert not host.call("configure", {"map": 3}), "In-game settings change was accepted"
            assert a["player_mask"] == b["player_mask"] == 3, (a, b)
            assert a["consoleplayer"] != b["consoleplayer"], (a, b)
            host.visible_frame(); client.visible_frame()
            assert client.call("input", b"K\1\xad\0K\1\xae\0") == 8
            assert host.call("input", b"P\1\0\0") == 4
            host.wait(lambda s: s["tic"] > a["tic"] + 40)
            client.wait(lambda s: s["tic"] > b["tic"] + 40)
            assert client.call("stop") < 2
            host.wait(lambda s: s["num_players"] == 1)
            assert host.call("stop") < 2
            print(f"PASS mode={mode}, movement, inactive host, disconnect and same-port restart", flush=True)
        assert host.call("start", {"mode": 1, "port": port})
        host.wait(lambda s: s["phase"] == 2)
        for index, peer in enumerate((client, finder, fourth)):
            assert peer.call("start", {"mode": 2, "port": port, "name": f"Player{index + 2}"})
        host.wait(lambda s: s["num_players"] == 4)
        for peer in peers[1:]: peer.wait(lambda s: s["num_players"] == 4)
        assert host.call("launch") == 1
        for peer in peers: peer.wait(lambda s: s["phase"] == 3 and s["player_mask"] == 15)
        for peer in reversed(peers): peer.call("stop")
        print("PASS four-player game and host signal-handler preservation", flush=True)
        assert host.call("start", {"mode": 1, "port": port})
        host.wait(lambda s: s["phase"] == 2 and s["controller"])
        assert host.call("launch") == 1
        host.wait(lambda s: s["phase"] == 3 and s["tic"] > 70)
        host.visible_frame(); host.call("stop")
        print("PASS solo host launch and nonblank gameplay", flush=True)
        freedm = str(assets / "freedm.wad")
        assert host.call("start", {"mode": 1, "port": port, "iwad": freedm, "deathmatch": 1})
        host.wait(lambda s: s["phase"] == 2)
        assert client.call("start", {"mode": 2, "port": port})
        client.failed("IWAD checksum")
        client.call("stop")
        assert client.call("start", {"mode": 2, "port": port, "iwad": freedm})
        host.wait(lambda s: s["num_players"] == 2)
        client.wait(lambda s: s["phase"] == 2 and s["num_players"] == 2)
        assert host.call("launch")
        for peer in (host, client):
            peer.wait(lambda s: s["phase"] == 3 and s["tic"] > 70 and s["deathmatch"] == 1)
            peer.visible_frame()
        client.call("stop"); host.call("stop")
        print("PASS FreeDM deathmatch and Phase 2 mismatch rejection", flush=True)
        with tempfile.TemporaryDirectory(prefix="freedoom-mismatch-") as directory:
            different = Path(directory) / "freedoom2.wad"
            different.write_bytes((assets / "freedoom2.wad").read_bytes() + b"different content")
            assert host.call("start", {"mode": 1, "port": port})
            host.wait(lambda s: s["phase"] == 2)
            assert client.call("start", {"mode": 2, "port": port, "iwad": str(different)})
            client.failed("differs")
            client.call("stop"); host.call("stop")
        print("PASS mismatched IWAD rejection", flush=True)
        assert client.call("start", {"mode": 2, "port": port})
        time.sleep(.1)
        assert client.call("stop") < 2, "Cancelling an unreachable server was slow"
        assert client.call("start", {"mode": 2, "port": port})
        client.failed("No response from server")
        client.call("stop")
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as occupied:
            occupied.bind(("0.0.0.0", port))
            assert host.call("start", {"mode": 1, "port": port})
            host.failed("bind")
            host.call("stop")
        for address in ("invalid.example", "256.0.0.1", "99999999999999999999999.0.0.1", "127.0.0.1:"):
            assert not client.call("start", {"mode": 2, "port": port, "address": address})
        assert host.call("start", {"mode": 1, "port": port})
        host.wait(lambda s: s["phase"] == 2)
        host.call("stop")
        print("PASS cancellation, connection timeout, occupied port, invalid address and recovery", flush=True)
    finally:
        for peer in reversed(peers): peer.close()

if __name__ == "__main__": main()
