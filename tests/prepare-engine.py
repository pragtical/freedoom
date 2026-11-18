#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Verify UTF-8 source hashes and byte offsets under a non-UTF-8 locale."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
declaration = b'static int ticks;'
source = '// UTF-8: \u2013\nvoid tick(void) {\n    '.encode('utf-8') + declaration + b'\n    ticks++;\n}\n'
offset = source.index(declaration) + len(declaration)
record = {'sha256': hashlib.sha256(source).hexdigest(), 'locals': [(offset, ['ticks'])], 'globals': []}

with tempfile.TemporaryDirectory(prefix='freedoom-source-') as directory:
    directory = Path(directory)
    input_path = directory / 'sample.c'
    output_path = directory / 'sample-embedded.c'
    manifest = directory / 'state.json'
    manifest.write_text(json.dumps({'common': {input_path.name: record}, 'windows': {}, 'posix': {}}), encoding='ascii')
    env = dict(os.environ, LC_ALL='C', PYTHONUTF8='0', PYTHONCOERCECLOCALE='0')
    for newline in (b'\n', b'\r\n'):
        input_path.write_bytes(source.replace(b'\n', newline))
        subprocess.run([sys.executable, root / 'scripts/prepare-engine.py', input_path,
            output_path, manifest, '--platform', 'windows'], env=env, check=True)
        generated = output_path.read_text(encoding='utf-8')
        assert '// UTF-8: \u2013' in generated
        assert f'static int ticks; static int fd_recorded_{offset};' in generated
        assert 'fd_state_register(&ticks, sizeof(ticks)); }\n    ticks++;' in generated
print('PASS UTF-8 hashing and byte offsets with LF and CRLF')
