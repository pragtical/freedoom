#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Encode verified fixed-IWAD network fingerprints, not engine runtime tables."""
import json
from pathlib import Path
import sys

source = json.loads(Path(sys.argv[1]).read_text())
assert len(source['references']) == 2 and source['iwads']
def digest(value):
    data = bytes.fromhex(value)
    assert len(data) == 20
    return '{' + ','.join(str(byte) for byte in data) + '}'
rows = []
for iwad in source['iwads']:
    assert len(iwad['dehacked']) == len(source['references'])
    rows.append('{' + digest(iwad['sha1']) + ',' + digest(iwad['directory']) + ',{'
        + ','.join(digest(value) for value in iwad['dehacked']) + '}}')
output = '/* Generated from network-content.json. */\n'
output += 'static const struct { sha1_digest_t file, directory, dehacked[2]; } fd_content[] = {\n'
output += ',\n'.join(rows) + '\n};\n'
assert len(source['vanilla_dehacked']) == len(source['references'])
output += 'static const sha1_digest_t fd_vanilla_dehacked[2] = {'
output += ','.join(digest(value) for value in source['vanilla_dehacked']) + '};\n'
path = Path(sys.argv[2])
if not path.exists() or path.read_text() != output:
    path.write_text(output)
