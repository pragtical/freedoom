"""Read the source lists from the pinned Protobuf tree, not its build system."""
from pathlib import Path
import re
import sys

root = Path(sys.argv[1])
kind = sys.argv[2]

def sources(file, variable):
    text = (root / 'cmake' / file).read_text()
    block = re.search(r'set\(' + variable + r'\s+([^)]*)\)', text)[1]
    return [name.removeprefix('${protobuf_SOURCE_DIR}/') for name in block.split()]

if kind == 'runtime':
    names = sources('libprotobuf-lite.cmake', 'libprotobuf_lite_files')
    names += sources('libprotobuf.cmake', 'libprotobuf_files')
else:
    base = {'code_generator.cc', 'command_line_interface.cc', 'plugin.cc', 'plugin.pb.cc',
            'subprocess.cc', 'zip_writer.cc'}
    names = [name for name in sources('libprotoc.cmake', 'libprotoc_files')
             if '/compiler/cpp/' in name or Path(name).name in base]
assert names and all((root / name).is_file() for name in names)
print(*(f'libraries/protobuf/{name}' for name in names), sep='\n')
