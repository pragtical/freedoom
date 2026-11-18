#!/usr/bin/env python3
"""Check manifest/download names and reject incomplete or wrong-platform packages."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import tarfile
import zipfile

ROOT = Path(__file__).resolve().parent.parent
ARCHES = ('x86_64-linux', 'aarch64-linux', 'x86_64-darwin', 'aarch64-darwin', 'x86_64-windows')


def binary(data, arch):
    cpu, system = arch.split('-', 1)
    if system == 'linux':
        return len(data) > 64 and data[:6] == b'\x7fELF\x02\x01' and struct.unpack_from('<H', data, 18)[0] == (62 if cpu == 'x86_64' else 183)
    if system == 'darwin':
        return len(data) > 32 and data[:4] == b'\xcf\xfa\xed\xfe' and struct.unpack_from('<I', data, 4)[0] == (0x01000007 if cpu == 'x86_64' else 0x0100000C)
    offset = struct.unpack_from('<I', data, 60)[0] if len(data) >= 64 else len(data)
    return data[:2] == b'MZ' and offset + 6 <= len(data) and data[offset:offset + 4] == b'PE\0\0' and struct.unpack_from('<H', data, offset + 4)[0] == 0x8664


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', nargs='?', type=Path)
    parser.add_argument('--arch', choices=ARCHES)
    parser.add_argument('--checksums', type=Path)
    parser.add_argument('--odamex', action='store_true', help='Require a packaged Odamex client')
    args = parser.parse_args()
    (addon,) = json.loads((ROOT / 'manifest.json').read_text())['addons']
    version = re.search(r"version:\s*'([^']+)'", (ROOT / 'meson.build').read_text())[1]
    assert addon['id'] == 'freedoom' and addon['version'] == version and addon['mod_version'] == '3.14.0'
    assert addon.get('path') == '.' and 'arch' not in addon, 'Install Lua from the repo, and reject unsupported architectures'
    expected = {'freedoom.' + arch + ('.zip' if arch.endswith('-windows') else '.tar.gz'): arch for arch in ARCHES}
    assert len(addon['files']) == len(expected) + 1
    entries = {entry['arch']: entry for entry in addon['files'] if entry.get('arch')}
    assets = [entry for entry in addon['files'] if not entry.get('arch')]
    bundle_name = 'freedoom-iwads-0.13.0.zip'
    (asset,) = assets
    assert asset['url'] == 'https://github.com/pragtical/freedoom/releases/download/latest/' + bundle_name
    assert re.fullmatch('[a-f0-9]{64}', asset['checksum'])
    for name, arch in expected.items():
        entry = entries[arch]
        assert entry['url'] == 'https://github.com/pragtical/freedoom/releases/download/latest/' + name
        assert entry['checksum'] == 'SKIP' and not entry.get('optional')
        assert 'extra' not in entry, 'No helper executable is distributed'
    if args.directory:
        bundle = args.directory / bundle_name
        if bundle.exists() or not args.arch:
            assert hashlib.sha256(bundle.read_bytes()).hexdigest() == asset['checksum'], 'Wrong IWAD asset archive'
            with zipfile.ZipFile(bundle) as archive:
                assert set(archive.namelist()) == {'iwads/freedoom1.wad', 'iwads/freedoom2.wad', 'iwads/freedm.wad',
                    'licenses/Freedoom/COPYING.txt', 'licenses/Freedoom/CREDITS.txt',
                    'licenses/Freedoom/CREDITS-MUSIC.txt'}, 'Wrong IWAD asset archive layout'
        for name, arch in expected.items():
            if args.arch and args.arch != arch:
                continue
            path = args.directory / name
            if name.endswith('.zip'):
                with zipfile.ZipFile(path) as archive:
                    data = {name: archive.read(name) for name in archive.namelist()}
            else:
                with tarfile.open(path) as archive:
                    members = archive.getmembers()
                    assert all(member.isfile() for member in members), 'Unexpected archive entry'
                    data = {member.name: archive.extractfile(member).read() for member in members}
            bridge = 'freedoom-bridge.' + arch + ('.dll' if arch.endswith('-windows') else '.so')
            required = {bridge, 'assets/woof.pk3', 'licenses/Woof.txt', 'licenses/Woof-notices.md',
                'licenses/miniz.txt', 'licenses/spng.txt', 'licenses/yyjson.txt',
                'licenses/libebur128.txt', 'licenses/libebur128-queue.txt', 'licenses/netlib.txt'}
            suffix = '.dll' if arch.endswith('-windows') else '.so'
            modules = {'freedoom-odamex' + part + '.' + arch + suffix for part in ('', '-host')}
            if args.odamex or not args.arch or modules & data.keys():
                required |= modules | {'assets/odamex.wad', 'licenses/Odamex.txt', 'licenses/Odamex-resources.txt',
                    'licenses/Odamex-fmt.txt', 'licenses/Odamex-jsoncpp.txt', 'licenses/Odamex-minilzo.txt',
                    'licenses/Odamex-protobuf.txt', 'licenses/Odamex-libpng.txt', 'licenses/Odamex-zlib.txt',
                    'licenses/Odamex-curl.txt',
                    'licenses/libADLMIDI.txt', 'licenses/libADLMIDI-LGPL-2.1.txt', 'licenses/libADLMIDI-GPL-3.txt',
                    'licenses/libADLMIDI-models.txt', 'licenses/Opal.txt'}
                if 'licenses/Odamex-openssl.txt' in data or not arch.endswith('-windows'):
                    required.add('licenses/Odamex-openssl.txt')
                assert data.get('assets/odamex.wad', b'')[:4] in {b'IWAD', b'PWAD'}, 'Invalid Odamex resource WAD'
                assert all(binary(data.get(name, b''), arch) for name in modules), 'Wrong Odamex native architecture'
            assert set(data) == required, f'Unexpected or missing files in {path}: {set(data) ^ required}'
            assert data['assets/woof.pk3'].startswith(b'PK\x03\x04'), 'Invalid Woof resource archive'
            assert all(data.values()), 'Empty package file'
            assert binary(data[bridge], arch), 'Wrong native architecture'
        if args.checksums:
            paths = sorted(path for path in args.directory.iterdir() if path.is_file() and path != args.checksums)
            args.checksums.write_text(''.join(hashlib.sha256(path.read_bytes()).hexdigest() + '  ' + path.name + '\n' for path in paths))
    print('Verified Freedoom manifest' + (' and release packages' if args.directory else ''))


if __name__ == '__main__':
    main()
