#!/usr/bin/env python3
"""Test asset downloads using a local cache and mocked responses, never the network."""
from contextlib import ExitStack
import importlib.util
import io
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import zipfile

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location('fetch_assets', ROOT / 'scripts/fetch-assets.py')
fetch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fetch)


class AssetsTest(unittest.TestCase):
    def prepare(self, root, extra_entry=None):
        cache = root / 'cache'
        cache.mkdir()
        checksums = {}
        for filename, assets in fetch.ASSETS.items():
            archive = cache / filename
            with zipfile.ZipFile(archive, 'w') as output:
                for name in assets:
                    output.writestr(f'{archive.stem}/{name}', name.encode())
                if extra_entry and extra_entry.startswith(archive.stem + '/'):
                    output.writestr(extra_entry, b'test')
            checksums[filename] = fetch.digest(archive)
        return cache, checksums

    def run_fetch(self, root, cache, checksums):
        with patch.object(fetch, 'SHA256', checksums), patch('sys.argv', ['fetch-assets',
                '--directory', str(root / 'plugin'), '--cache', str(cache)]):
            fetch.main()

    def test_verified_cache_installs_the_flat_layout(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            cache, checksums = self.prepare(root)
            with patch.object(fetch.urllib.request, 'urlopen', side_effect=AssertionError('network not expected')):
                self.run_fetch(root, cache, checksums)
            self.assertEqual((root / 'plugin/iwads/freedoom1.wad').read_bytes(), b'freedoom1.wad')
            self.assertEqual((root / 'plugin/iwads/freedoom2.wad').read_bytes(), b'freedoom2.wad')
            self.assertEqual((root / 'plugin/iwads/freedm.wad').read_bytes(), b'freedm.wad')
            self.assertEqual((root / 'plugin/licenses/Freedoom/COPYING.txt').read_bytes(), b'COPYING.txt')
            self.assertFalse((root / 'plugin/freedoom-0.13.0').exists())

    def test_reproducible_ppm_bundle(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            cache, _ = self.prepare(root)
            output = root / 'bundle.zip'
            with ExitStack() as archives:
                entries = {}
                for filename, assets in fetch.ASSETS.items():
                    source = cache / filename
                    archive = archives.enter_context(zipfile.ZipFile(source))
                    entries.update(fetch.asset_entries(archive, source.stem, assets))
                fetch.write_bundle(entries, output)
                fetch.write_bundle(entries, root / 'bundle2.zip', fetch.digest(output))
                with self.assertRaisesRegex(ValueError, 'PPM asset archive checksum mismatch'):
                    fetch.write_bundle(entries, output, 'bad checksum')
            with zipfile.ZipFile(output) as bundle:
                self.assertEqual(set(bundle.namelist()), {path for assets in fetch.ASSETS.values() for path in assets.values()})
                self.assertEqual(bundle.read('iwads/freedoom2.wad'), b'freedoom2.wad')
                self.assertEqual(bundle.read('iwads/freedm.wad'), b'freedm.wad')

    def test_bad_download_checksum_is_rejected_before_install(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            cache, checksums = self.prepare(root)
            (cache / 'freedm-0.13.0.zip').write_bytes(b'bad cache')
            with patch.object(fetch.urllib.request, 'urlopen', return_value=io.BytesIO(b'bad download')):
                with self.assertRaisesRegex(ValueError, 'checksum mismatch'):
                    self.run_fetch(root, cache, checksums)
            self.assertFalse((root / 'plugin').exists())
            self.assertFalse((cache / 'freedm-0.13.0.zip').exists())

    def test_unsafe_archive_paths_are_rejected(self):
        for entry in ('freedoom-0.13.0/../../escape', 'freedoom-0.13.0/..\\..\\escape',
                'freedm-0.13.0/file:stream'):
            with self.subTest(entry=entry), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                cache, checksums = self.prepare(root, entry)
                with self.assertRaisesRegex(ValueError, 'Unsafe asset archive'):
                    self.run_fetch(root, cache, checksums)
                self.assertFalse((root / 'escape').exists())


if __name__ == '__main__':
    unittest.main()
