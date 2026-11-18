#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise Internet discovery without depending on any public service."""
import multiprocessing as mp
from pathlib import Path
import select
import socket
import struct
import subprocess
import sys
import threading
import time
from network import Peer

def packet(kind, content=b""):
    return struct.pack("!H", kind) + content

class Directory:
    def __init__(self):
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.socket.bind(("127.0.0.1", 0))
        self.address = f"127.0.0.1:{self.socket.getsockname()[1]}"
        self.rooms, self.entries, self.received = {}, [], []
        self.drop = False
        self.lock, self.stopping = threading.Lock(), threading.Event()
        self.thread = threading.Thread(target=self.run)
        self.thread.start()

    def room(self, players=0, mode=4, mission=8, state=0, protocol=True, malformed=False, version="Woof 16.0.0"):
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.bind(("127.0.0.1", 0))
        endpoint = f"127.0.0.1:{sock.getsockname()[1]}"
        protocols = b"\1CHOCOLATE_DOOM_0\0" if protocol else b"\0"
        data = version.encode() + b"\0" + bytes((state, players, 8, mode, mission)) + b"Fixture room\0" + protocols
        with self.lock:
            self.rooms[sock] = packet(14, b"" if malformed else data)
            self.entries.append(endpoint)
        return endpoint

    def run(self):
        while not self.stopping.is_set():
            with self.lock: rooms = dict(self.rooms)
            readable, _, _ = select.select([self.socket, *rooms], [], [], .02)
            for sock in readable:
                data, sender = sock.recvfrom(2048)
                if len(data) < 2: continue
                kind = struct.unpack("!H", data[:2])[0]
                if sock == self.socket:
                    with self.lock:
                        self.received.append(kind)
                        entries, drop = list(self.entries), self.drop
                    if kind == 2 and not drop:
                        payload = b""
                        for endpoint in entries:
                            encoded = endpoint.encode() + b"\0"
                            if len(payload) + len(encoded) > 1400:
                                sock.sendto(packet(3, payload), sender); payload = b""
                            payload += encoded
                        sock.sendto(packet(3, payload), sender)
                elif kind == 13:
                    sock.sendto(rooms[sock], sender)

    def close(self):
        self.stopping.set(); self.thread.join(3)
        assert not self.thread.is_alive()
        self.socket.close()
        for sock in self.rooms: sock.close()

class GuestRelay:
    """Deliver the guest's GAMESTART with the controller's first game tic."""
    def __init__(self, server_port):
        self.server = ("127.0.0.1", server_port)
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.socket.bind(("127.0.0.1", 0))
        self.socket.settimeout(.1)
        self.port = self.socket.getsockname()[1]
        self.stopping, self.delivered = threading.Event(), threading.Event()
        self.thread = threading.Thread(target=self.run)
        self.thread.start()

    def run(self):
        guest = start = tic = None
        while not self.stopping.is_set():
            try: data, sender = self.socket.recvfrom(2048)
            except socket.timeout: continue
            if sender != self.server:
                guest = sender
                self.socket.sendto(data, self.server)
                continue
            if not self.delivered.is_set():
                if data.startswith(packet(0x8005)): start = data  # Reliable GAMESTART
                elif data.startswith(packet(6)): tic = tic or data  # GAMEDATA
                else:
                    self.socket.sendto(data, guest)
                    continue
                if start and tic:
                    self.socket.sendto(start, guest)
                    self.socket.sendto(tic, guest)
                    self.delivered.set()
            else:
                self.socket.sendto(data, guest)

    def close(self):
        self.stopping.set()
        self.thread.join()
        self.socket.close()

def refresh(peer, directory, entries=None, drop=False):
    before = peer.call("state")["revision"]
    with directory.lock:
        if entries is not None: directory.entries = entries
        directory.drop = drop
    assert peer.call("refresh") == 1
    return peer.wait(lambda s: s["revision"] > before)

def main():
    library, dedicated = str(Path(sys.argv[1]).resolve()), str(Path(sys.argv[2]).resolve())
    root = Path(__file__).resolve().parent.parent
    assets = root / "iwads"
    directory = Directory()
    peers = [Peer(mp.get_context("spawn"), library, str(assets)) for _ in range(3)]
    finder, first, second = peers
    server = relay = None
    try:
        empty = directory.room()
        directory.room(players=1, mode=2)
        phase1 = directory.room(players=1, mode=3)
        directory.room(players=4, mode=2)
        doom2 = directory.room(players=1, mode=2, mission=1)
        strife = directory.room(players=1, mode=2, mission=8, version="Chocolate Strife 3.1.1")
        directory.room(protocol=False)
        directory.room(players=1, mode=2, state=2)
        directory.room(malformed=True)
        with directory.lock:
            directory.entries += ["attacker.invalid:2342", "999999999999999999999.0.0.1:2342", "127.0.0.1:999999"]
        assert finder.call("start", {"mode": 4, "port": 2342, "master": directory.address})
        found = finder.wait(lambda s: s["phase"] == 5 and s["server_count"] == 8)
        room = next(item for item in found["servers"] if item["address"] == empty)
        assert room["unassigned"] and room["compatible"] and room["protocol"], found
        room = next(item for item in found["servers"] if item["address"] == phase1)
        assert room["phase"] == 1 and room["compatible"], "Phase 2 browser rejected an installed Phase 1 game"
        room = next(item for item in found["servers"] if item["address"] == doom2)
        assert room["mission"] == 1 and room["gamemode"] == 2 and room["compatible"] and room["phase"] == 2, room
        room = next(item for item in found["servers"] if item["address"] == strife)
        assert room["mission"] == 8 and room["gamemode"] == 2 and not room["phase"] and not room["compatible"], room
        assert any(item["phase"] == 2 and item["compatible"] for item in found["servers"]), found
        assert any(not item["protocol"] for item in found["servers"]), found
        assert any(not item["compatible"] for item in found["servers"]), found
        assert any(item["state"] == 2 for item in found["servers"]), found
        finder.wait(lambda s: s["phase"] == 5)
        print("PASS master discovery, empty rooms, incompatible/full/running rooms and malformed data", flush=True)
        for _ in range(3):
            refresh(finder, directory, [])
            result = finder.wait(lambda s: s["phase"] == 5)
            assert not result["server_count"] and result["message"] == "0 Internet games found", result
        refresh(finder, directory, drop=True)
        result = finder.wait(lambda s: s["phase"] == 5)
        assert result["message"] == "Master server did not respond", result
        refresh(finder, directory, drop=False)
        finder.wait(lambda s: s["phase"] == 5)
        print("PASS refresh, empty results, master timeout and recovery", flush=True)
        for _ in range(133): directory.room()
        refresh(finder, directory)
        bounded = finder.wait(lambda s: s["phase"] == 5)
        assert bounded["server_count"] == 32 and bounded["limited"] == 1, bounded
        refresh(finder, directory, drop=True)
        assert finder.call("stop") < 2
        print("PASS native target/result bounds and cancellation", flush=True)

        # Only explicit Internet connections may ask the master to traverse NAT.
        target = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        target.bind(("127.0.0.1", 0))
        port = target.getsockname()[1]
        try:
            with directory.lock: directory.received.clear()
            assert first.call("start", {"mode": 5, "port": port, "master": directory.address})
            deadline = time.monotonic() + 4
            while 10 not in directory.received and time.monotonic() < deadline: time.sleep(.03)
            assert 10 in directory.received, directory.received
            assert first.call("stop") < 2
            with directory.lock: directory.received.clear()
            assert first.call("start", {"mode": 2, "port": port, "master": directory.address})
            time.sleep(2.3)
            assert not directory.received, "Private join contacted the master"
            first.call("stop")
        finally: target.close()
        print("PASS Internet-only NAT request and private-mode isolation", flush=True)

        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
            probe.bind(("127.0.0.1", 0)); port = probe.getsockname()[1]
        server = subprocess.Popen([dedicated, "-privateserver", "-port", str(port), "-servername", "Stock fixture"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        with directory.lock:
            directory.entries, directory.drop = [f"127.0.0.1:{port}"], False
        assert finder.call("start", {"mode": 4, "port": 2342, "master": directory.address})
        available = finder.wait(lambda s: s["server_count"] == 1)
        assert available["servers"][0]["unassigned"] and available["servers"][0]["compatible"], available
        finder.call("stop")
        assert server.poll() is None
        assert first.call("start", {"mode": 5, "port": port, "master": directory.address,
            "deathmatch": 3, "map": 2, "name": "Controller"})
        first.wait(lambda s: s["phase"] == 2 and s["controller"])
        relay = GuestRelay(port)
        assert second.call("start", {"mode": 2, "port": relay.port, "name": "Guest"})
        first.wait(lambda s: s["num_players"] == 2)
        second.wait(lambda s: s["num_players"] == 2)
        assert first.call("configure", {"deathmatch": 3, "map": 3, "skill": 4})
        first.wait(lambda s: s["map"] == 3 and s["skill"] == 4)
        assert not second.call("configure", {"map": 4})
        assert first.call("launch") == 1
        assert relay.delivered.wait(10), "Server did not send both GAMESTART and GAMEDATA"
        for peer in (first, second):
            state = peer.wait(lambda s: s["phase"] == 3 and s["tic"] > 40)
            assert state["deathmatch"] == 3 and state["map"] == 3 and state["skill"] == 4 and state["player_mask"] == 3, state
            peer.visible_frame()
        for peer in (second, first): peer.call("stop")
        assert 0 not in directory.received, "Browsing advertised a server"
        print("PASS unmodified Woof dedicated networking, controller settings and rendered multiplayer", flush=True)
    finally:
        for peer in reversed(peers): peer.close()
        if relay: relay.close()
        if server:
            server.kill(); server.wait(timeout=10)
        directory.close()

if __name__ == "__main__": main()
