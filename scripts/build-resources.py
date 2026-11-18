#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Build Woof's resource archive without invoking an external build system."""
import argparse
from pathlib import Path
import zipfile

parser = argparse.ArgumentParser()
parser.add_argument("root", type=Path)
parser.add_argument("output", type=Path)
parser.add_argument("files", nargs="+")
args = parser.parse_args()
with zipfile.ZipFile(args.output, "w", compression=zipfile.ZIP_DEFLATED) as archive:
    for filename in sorted(args.files):
        path = Path(filename).resolve()
        name = path.relative_to(args.root.resolve()).as_posix()
        entry = zipfile.ZipInfo(name, (1980, 1, 1, 0, 0, 0))
        entry.compress_type = zipfile.ZIP_DEFLATED
        entry.external_attr = 0o100644 << 16
        archive.writestr(entry, path.read_bytes())
