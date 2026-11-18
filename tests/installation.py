#!/usr/bin/env python3
"""Verify native installation staging without touching a profile."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent


class InstallationTest(unittest.TestCase):
    def test_destdir_prefix_and_atomic_replacement(self):
        with tempfile.TemporaryDirectory(prefix='freedoom-install-') as temporary:
            root = Path(temporary)
            source = root / 'freedoom-bridge.test.so'
            source.write_bytes(b'original library')
            resource = root / 'woof.pk3'
            resource.write_bytes(b'engine data')
            stage, prefix = root / 'stage', root / 'prefix'
            destination = stage / prefix.relative_to(prefix.anchor) / 'plugins/freedoom'
            env = {**os.environ, 'MESON_INSTALL_PREFIX': str(prefix), 'DESTDIR': str(stage)}
            command = [sys.executable, str(ROOT / 'scripts/install-runtime.py'),
                '--directory', 'plugins/freedoom', '--data', str(resource), 'assets/woof.pk3', str(source)]
            subprocess.run(command, env=env, check=True)
            installed = destination / source.name
            self.assertEqual(installed.read_bytes(), b'original library')
            self.assertEqual((destination / 'assets/woof.pk3').read_bytes(), b'engine data')
            self.assertFalse(prefix.exists(), 'DESTDIR escaped into the real prefix')
            old_inode = installed.stat().st_ino
            source.write_bytes(b'new library')
            if os.name == 'nt':
                subprocess.run(command, env=env, check=True)
            else:
                with installed.open('rb') as loaded:
                    subprocess.run(command, env=env, check=True)
                    self.assertEqual(loaded.read(), b'original library', 'Loaded inode was overwritten')
                self.assertNotEqual(installed.stat().st_ino, old_inode)
            self.assertEqual(installed.read_bytes(), b'new library')
            self.assertEqual({path.name for path in destination.iterdir()}, {source.name, 'assets'})

    def test_failed_copy_preserves_the_installed_file(self):
        with tempfile.TemporaryDirectory(prefix='freedoom-install-') as temporary:
            root = Path(temporary)
            installed = root / 'missing.so'
            installed.write_bytes(b'old library')
            env = {**os.environ, 'MESON_INSTALL_PREFIX': str(root), 'DESTDIR': ''}
            result = subprocess.run([sys.executable, str(ROOT / 'scripts/install-runtime.py'),
                '--directory', str(root), str(root / 'absent/missing.so')], env=env,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(installed.read_bytes(), b'old library')
            self.assertEqual({path.name for path in root.iterdir()}, {'missing.so'})


if __name__ == '__main__':
    unittest.main()
