#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
from pathlib import Path
import subprocess
import sys

subprocess.run([sys.executable, '-B', 'odawad.py', '-o', str(Path(sys.argv[2]).resolve()),
                'wadinfo.txt'], cwd=Path(sys.argv[1]) / 'wad', check=True)
