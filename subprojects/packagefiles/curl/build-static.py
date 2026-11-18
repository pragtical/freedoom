"""Build the pinned cURL as a private archive; do not install a cURL runtime."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys

shell, make, configure_script, archive, *args = sys.argv[1:]
assert args.pop(0) == '--compiler'
split = args.index('--configure')
compiler, configure = args[:split], args[split + 1:]
archive = Path(archive).resolve()
build = archive.parent / 'curl-build'
build.mkdir(exist_ok=True)
env = dict(os.environ, CC=shlex.join(compiler))
script = Path(configure_script).resolve().as_posix()
command = [shell, script, *configure]
if os.name == 'nt':
    # Autoconf uses a colon-separated auxiliary-file search path in MSYS2.
    command = [shell, '-c', 'script=$(cygpath -u "$1") || exit; shift; exec sh "$script" "$@"',
               'configure-curl', script, *configure]
subprocess.run(command, cwd=build, env=env, check=True)
subprocess.run([make, '-C', 'lib', '-j', str(min(4, os.cpu_count() or 1)), 'libcurl.la'],
               cwd=build, env=env, check=True)
shutil.copyfile(build / 'lib/.libs/libcurl.a', archive)
