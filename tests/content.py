#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check fixed-IWAD fingerprints against their files and the legacy directory algorithm."""
import hashlib
import json
from pathlib import Path
import struct

root = Path(__file__).resolve().parent.parent
content = json.loads((root / 'src/woof/network-content.json').read_text())
assert len(content['vanilla_dehacked']) == len(content['references'])
assert all(len(bytes.fromhex(value)) == 20 for value in content['vanilla_dehacked'])
assets = root / 'iwads'
for iwad in content['iwads']:
    data = (assets / iwad['file']).read_bytes()
    assert hashlib.sha1(data).hexdigest() == iwad['sha1'], iwad['file']
    magic, count, offset = struct.unpack_from('<4sII', data)
    assert magic == b'IWAD' and offset + count * 16 <= len(data)
    directory = hashlib.sha1()
    for index in range(count):
        position, size, name = struct.unpack_from('<II8s', data, offset + index * 16)
        directory.update(name.split(b'\0')[0] + b'\0')
        directory.update(struct.pack('!III', 0, position, size))
    assert directory.hexdigest() == iwad['directory'], iwad['file']
    assert len(iwad['dehacked']) == len(content['references'])
    assert all(len(bytes.fromhex(value)) == 20 for value in iwad['dehacked'])
print('PASS verified IWAD files and standard network directory fingerprints')
