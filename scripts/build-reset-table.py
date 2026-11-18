#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
import json
from pathlib import Path
import sys

manifest, platform, output = sys.argv[1:]
records = json.loads(Path(manifest).read_text())
records = {**records["common"], **records["windows" if platform == "windows" else "posix"]}
names = sorted(Path(name).stem for name in records)
source = '#include "engine_memory.h"\n'
source += "\n".join(f"void fd_capture_{name}(void);" for name in names)
source += "\nvoid fd_capture_engine_state(void) {\n    static int captured;\n    if (captured) return;\n"
source += "\n".join(f"    fd_capture_{name}();" for name in names)
source += "\n    captured = 1;\n}\n"
output = Path(output)
if not output.exists() or output.read_text() != source:
    output.write_text(source)
