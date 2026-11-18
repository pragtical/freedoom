#!/usr/bin/env python3
"""Install the verified upstream IWADs or build PPM's flat asset archive."""
import argparse
from contextlib import ExitStack
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import tempfile
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parent.parent
UPSTREAM = 'https://github.com/freedoom/freedoom/releases/download/v0.13.0/'
SHA256 = {
    'freedoom-0.13.0.zip': '3f9b264f3e3ce503b4fb7f6bdcb1f419d93c7b546f4df3e874dd878db9688f59',
    'freedm-0.13.0.zip': 'b420f13508ef745d7b38e83d15e55e0fc0b09d9a503c96741cddd9773d43f7c9',
}
ASSETS = {
    'freedoom-0.13.0.zip': {
        'freedoom1.wad': 'iwads/freedoom1.wad',
        'freedoom2.wad': 'iwads/freedoom2.wad',
        'COPYING.txt': 'licenses/Freedoom/COPYING.txt',
        'CREDITS.txt': 'licenses/Freedoom/CREDITS.txt',
        'CREDITS-MUSIC.txt': 'licenses/Freedoom/CREDITS-MUSIC.txt',
    },
    # The upstream FreeDM archive has identical license and credit notices.
    'freedm-0.13.0.zip': {'freedm.wad': 'iwads/freedm.wad'},
}


def digest(path):
    value = hashlib.sha256()
    with path.open('rb') as file:
        for chunk in iter(lambda: file.read(1024 * 1024), b''):
            value.update(chunk)
    return value.hexdigest()


def atomic_copy(source, destination):
    destination.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=destination.name + '.', dir=destination.parent)
    try:
        with os.fdopen(fd, 'wb') as output:
            shutil.copyfileobj(source, output)
        os.replace(temporary, destination)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def asset_entries(archive, root, assets):
    entries = {}
    for entry in archive.infolist():
        path = PurePosixPath(entry.filename)
        if (path.is_absolute() or '..' in path.parts or '\\' in entry.filename
                or any(':' in part for part in path.parts)
                or not path.parts or path.parts[0] != root):
            raise ValueError('Unsafe asset archive entry: ' + entry.filename)
        if len(path.parts) == 2 and path.name in assets and not entry.is_dir():
            if path.name in entries:
                raise ValueError('Duplicate asset archive entry: ' + entry.filename)
            entries[path.name] = entry
    if entries.keys() != assets.keys():
        raise ValueError('Asset archive is missing required IWADs or notices')
    return {assets[name]: (archive, entry) for name, entry in entries.items()}


def write_bundle(entries, destination, checksum=None):
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=destination.parent) as directory:
        temporary = Path(directory) / destination.name
        with zipfile.ZipFile(temporary, 'w') as output:
            for filename, (archive, entry) in sorted(entries.items()):
                # Fixed metadata and no compression make the checksum reproducible
                # across Python versions, platforms and zlib builds.
                info = zipfile.ZipInfo(filename)
                info.create_system = 3
                info.external_attr = 0o100644 << 16
                info.file_size = entry.file_size
                with archive.open(entry) as source, output.open(info, 'w') as target:
                    shutil.copyfileobj(source, target)
        if checksum is not None and digest(temporary) != checksum:
            raise ValueError('PPM asset archive checksum mismatch: ' + destination.name)
        os.replace(temporary, destination)


def verified_archive(cache, name):
    cached = cache / name
    if not cached.is_file() or digest(cached) != SHA256[name]:
        with urllib.request.urlopen(UPSTREAM + name, timeout=60) as response:
            atomic_copy(response, cached)
        if digest(cached) != SHA256[name]:
            cached.unlink()
            raise ValueError('Asset checksum mismatch: ' + name)
    return cached


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path, default=ROOT, help='Plugin root; defaults to the source checkout')
    parser.add_argument('--cache', type=Path, default=ROOT / 'build-assets-cache')
    parser.add_argument('--bundle', type=Path, help='Write the PPM asset archive to this directory instead of installing')
    args = parser.parse_args()
    args.cache.mkdir(parents=True, exist_ok=True)
    with ExitStack() as archives:
        entries = {}
        for name, assets in ASSETS.items():
            cached = verified_archive(args.cache, name)
            archive = archives.enter_context(zipfile.ZipFile(cached))
            entries.update(asset_entries(archive, cached.stem, assets))
        if args.bundle is not None:
            addon = json.loads((ROOT / 'manifest.json').read_text())['addons'][0]
            (asset,) = (item for item in addon['files'] if not item.get('arch'))
            bundle = args.bundle / asset['url'].rsplit('/', 1)[1]
            write_bundle(entries, bundle, asset['checksum'])
            print(bundle)
        else:
            for filename, (archive, entry) in entries.items():
                with archive.open(entry) as source:
                    atomic_copy(source, args.directory / filename)
            print('Verified and installed the Freedoom and FreeDM IWADs into ' + str(args.directory / 'iwads'))


if __name__ == '__main__':
    main()
