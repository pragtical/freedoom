#!/usr/bin/env python3
"""Start both native clients from the published archive layout."""
import ctypes as ffi
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

def test_staged(root, arch):
    suffix = '.dll' if arch.endswith('-windows') else '.so'
    iwad = Path('iwads/freedoom2.wad').resolve()
    assert iwad.is_file()
    plugin = root / 'plugin'
    for engine in ('bridge', 'odamex-host'):
        library = ffi.CDLL(str(plugin / f'freedoom-{engine}.{arch}{suffix}'))
        if engine == 'bridge':
            start = library.fd_start
            start.argtypes = [ffi.c_char_p] * 3 + [ffi.c_int]
            resource_dir, extra = plugin / 'assets', (0,)
        else:
            start = library.fd_odamex_start
            start.argtypes = [ffi.c_char_p] * 6 + [ffi.c_int] * 2
            resource_dir, extra = plugin, (None, b'Player', None, 0, 0)
        start.restype = ffi.c_void_p
        library.fd_status.argtypes = [ffi.c_void_p]
        library.fd_close.argtypes = [ffi.c_void_p]
        library.fd_close.restype = None
        library.fd_read_log.argtypes = [ffi.c_void_p, ffi.c_char_p, ffi.c_uint]
        for turn in range(2):
            state = root / (engine + str(turn))
            state.mkdir()
            handle = start(str(resource_dir).encode(), str(state).encode(), str(iwad).encode(), *extra)
            assert handle, f'Could not create packaged {engine} session'
            try:
                deadline = time.monotonic() + 10
                status = library.fd_status(handle)
                while status == 0 and time.monotonic() < deadline:
                    time.sleep(.02)
                    status = library.fd_status(handle)
                if status != 1:
                    log = ffi.create_string_buffer(8192)
                    library.fd_read_log(handle, log, len(log))
                    raise AssertionError(f'{engine} did not render: {log.value.decode(errors="replace")}')
            finally:
                library.fd_close(handle)
        print(f'PASS packaged {engine}: startup and restart')


if __name__ == '__main__':
    if sys.argv[1] == '--staged':
        test_staged(Path(sys.argv[2]), sys.argv[3])
    else:
        archive, arch = sys.argv[1:]
        with tempfile.TemporaryDirectory(prefix='freedoom-package-') as temp:
            shutil.unpack_archive(archive, Path(temp) / 'plugin')
            subprocess.run([sys.executable, __file__, '--staged', temp, arch], check=True, timeout=60)
