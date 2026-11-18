#!/usr/bin/env python3
"""Install native files without truncating running executables or libraries."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--directory', required=True, type=Path)
parser.add_argument('--strip')
parser.add_argument('--codesign', action='store_true')
parser.add_argument('--license', nargs=2, action='append', default=[])
parser.add_argument('--data', nargs=2, action='append', default=[])
parser.add_argument('binaries', nargs='+', type=Path)
args = parser.parse_args()
destination = args.directory
if not destination.is_absolute():
    destination = Path(os.environ['MESON_INSTALL_PREFIX']) / destination
destdir = os.environ.get('DESTDIR')
if destdir:
    destination = Path(destdir) / destination.relative_to(destination.anchor)
destination.mkdir(parents=True, exist_ok=True)
for source, name in args.license:
    (destination / 'licenses').mkdir(exist_ok=True)
    shutil.copy2(source, destination / 'licenses' / name)
for binary, name in [(path, path.name) for path in args.binaries] + [(Path(path), name) for path, name in args.data]:
    target = destination / name
    target.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=target.name + '.', dir=target.parent)
    os.close(fd)
    try:
        shutil.copy2(binary, temporary)
        if args.strip and binary in args.binaries:
            # Mach-O must retain exported FFI symbols and dynamic symbol references.
            subprocess.run([args.strip] + (['-x'] if args.codesign else []) + [temporary], check=True)
        if args.codesign and binary in args.binaries:
            subprocess.run(['codesign', '--force', '--sign', '-', temporary], check=True)
        os.replace(temporary, target)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)
