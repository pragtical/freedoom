#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Read-only IWAD metadata checks, including malformed files and optional owned games."""
import ctypes as c
import hashlib
import json
import os
from pathlib import Path
import sys
import tempfile

class Iwad(c.Structure):
    _fields_ = [('mission', c.c_uint), ('gamemode', c.c_uint), ('network', c.c_uint),
        ('title', c.c_char * 96), ('checksum', c.c_ubyte * 20)]

native = c.CDLL(str(Path(sys.argv[1]).resolve()))
native.fd_iwad_info.argtypes = [c.c_char_p, c.POINTER(Iwad)]
root = Path(__file__).resolve().parent.parent
assets = root / 'iwads'
content = json.loads((root / 'src/woof/network-content.json').read_text())
for record in content['iwads']:
    info = Iwad()
    assert native.fd_iwad_info(str(assets / record['file']).encode(), c.byref(info))
    assert info.mission == 8 and info.gamemode == (3 if record['file'] == 'freedoom1.wad' else 2)
    assert bytes(info.checksum).hex() == record['directory'] and info.network
with tempfile.TemporaryDirectory(prefix='freedoom-metadata-') as directory:
    path = Path(directory) / 'doom2.wad'
    for data in (b'', b'IWAD' + b'\xff' * 8, b'IWAD\1\0\0\0\x0c\0\0\0', b'PWAD' + b'\0' * 32):
        path.write_bytes(data)
        assert not native.fd_iwad_info(str(path).encode(), c.byref(Iwad()))
if os.getenv('FREEDOOM_TEST_IWAD_DIR'):
    directory = Path(os.environ['FREEDOOM_TEST_IWAD_DIR'])
    found = set()
    for path in directory.glob('*'):
        if path.suffix.lower() != '.wad': continue
        before = hashlib.sha256(path.read_bytes()).digest()
        info = Iwad()
        if native.fd_iwad_info(str(path).encode(), c.byref(info)):
            found.add(info.mission)
            print(path.name, info.title.decode(), info.mission, info.gamemode, bool(info.network))
        assert hashlib.sha256(path.read_bytes()).digest() == before
    assert {0, 1, 2, 3} <= found, found
print('PASS IWAD metadata, fingerprints, malformed inputs and read-only access')
