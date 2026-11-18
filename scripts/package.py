#!/usr/bin/env python3
"""Package native libraries and resources, with a separate corresponding-source archive."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import zipfile

ROOT = Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('build', type=Path)
parser.add_argument('--arch', required=True)
parser.add_argument('--output', type=Path, default=ROOT / 'dist')
parser.add_argument('--sources', action='store_true')
args = parser.parse_args()
build = args.build.resolve()
stage = build / 'package'
if stage.exists():
    shutil.rmtree(stage)
subprocess.run(['meson', 'install', '-C', str(build), '--no-rebuild', '--destdir', str(stage)], check=True)
plugins = list(stage.glob('**/plugins/freedoom'))
if len(plugins) != 1:
    parser.error('Expected exactly one installed plugins/freedoom directory')
plugin = plugins[0]
suffix = '.dll' if args.arch.endswith('-windows') else '.so'
bridge = plugin / ('freedoom-bridge.' + args.arch + suffix)
woof = plugin / 'assets/woof.pk3'
if not bridge.is_file():
    parser.error('Installed binary does not match --arch')
if not woof.is_file():
    parser.error('Release package is missing Woof engine resources')
notices = ('Woof.txt', 'Woof-notices.md', 'miniz.txt', 'spng.txt', 'yyjson.txt',
    'libebur128.txt', 'libebur128-queue.txt', 'netlib.txt')
for notice in notices:
    if not (plugin / 'licenses' / notice).is_file():
        parser.error('Release package is missing an engine dependency notice: ' + notice)
odamex = [plugin / ('freedoom-odamex' + part + '.' + args.arch + suffix)
    for part in ('', '-host')] + [plugin / 'assets/odamex.wad']
available_odamex = [path for path in odamex if path.is_file()]
if available_odamex and available_odamex != odamex:
    parser.error('Incomplete Odamex install: expected the resource, the client and the host library')
args.output.mkdir(parents=True, exist_ok=True)
extension = '.zip' if args.arch.endswith('-windows') else '.tar.gz'
archive = args.output / ('freedoom.' + args.arch + extension)
paths = sorted([bridge, woof, *available_odamex]
    + [path for path in (plugin / 'licenses').rglob('*') if path.is_file()])
if extension == '.zip':
    with zipfile.ZipFile(archive, 'w', compression=zipfile.ZIP_DEFLATED) as output:
        for path in paths:
            output.write(path, path.relative_to(plugin).as_posix())
else:
    with tarfile.open(archive, 'w:gz') as output:
        for path in paths:
            output.add(path, arcname=path.relative_to(plugin).as_posix(), recursive=False)
print(archive)

if args.sources:
    # Include the exact dependency sources, not just download links, for GPL redistribution.
    source_archive = args.output / 'freedoom-sources.tar.gz'
    with tarfile.open(source_archive, 'w:gz') as output:
        for directory, folders, filenames in os.walk(ROOT):
            relative = Path(directory).relative_to(ROOT)
            folders[:] = sorted(name for name in folders
                if name not in {'.git', '.agents', '.aws', '.codex', '__pycache__', '.venv', 'state',
                    'dist', 'package', 'packagecache', 'freedoom-0.13.0', 'pragtical-wine', 'soundfonts'}
                and relative / name != Path('plans')
                and not name.startswith(('build', 'SDL2-', 'SDL2_mixer-', 'SDL3-', 'openal-soft-'))
                and name != '__CMake_build' and not name.endswith('.framework'))
            if relative == Path('iwads'):
                folders[:] = []
                filenames = [name for name in filenames if name == 'README.md']
            for name in sorted(filenames):
                path = Path(directory) / name
                if name in {'AGENTS.md', '.wraplock'} or name.startswith(('freedoom-helper', 'freedoom-bridge')) or path.suffix.lower() in {'.pyc', '.o', '.a', '.so', '.dll', '.exe', '.dylib', '.lib', '.pdb', '.wad', '.sf2', '.pk3'}:
                    continue
                output.add(path, arcname=str(Path('freedoom') / relative / name), recursive=False)
    print(source_archive)
