#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
from pathlib import Path
import subprocess
import sys
import tempfile

executable, root = sys.argv[1:]
with tempfile.TemporaryDirectory(prefix="freedoom-native-") as state:
    subprocess.run([executable, str(Path(executable).parent), root, state], check=True)
