project('odamex', 'c', 'cpp', version: '12.3.0', license: 'GPL-2.0-or-later')
python = import('python').find_installation()
root = meson.current_source_dir()
protobuf_root = root / 'libraries/protobuf'
protobuf_inc = include_directories('libraries/protobuf/src')
protobuf_options = ['cpp_std=c++17']
protobuf_paths = run_command(python, files('protobuf-sources.py'),
  protobuf_root, 'runtime', check: true).stdout().strip().split('\n')
protobuf_sources = []
protobuf_args = []
arena = ''
if host_machine.system() == 'darwin'
  # dyld keeps libraries with Mach-O thread-local descriptors loaded.
  protobuf_args += '-DGOOGLE_PROTOBUF_NO_THREADLOCAL'
  arena = 'libraries/protobuf/src/google/protobuf/arena.cc'
  protobuf_sources += custom_target('protobuf-pthread-arena',
    input: arena, output: 'pthread-arena.cc',
    command: [python, files('protobuf-pthread-arena.py'), '@INPUT@', '@OUTPUT@'])
endif
foreach path : protobuf_paths
  if path != arena
    protobuf_sources += files(path)
  endif
endforeach
protobuf_compiler = files(run_command(python, files('protobuf-sources.py'),
  protobuf_root, 'compiler', check: true).stdout().strip().split('\n'))
protobuf_runtime = static_library('odamex-protobuf', protobuf_sources, pic: true,
  include_directories: protobuf_inc, dependencies: dependency('threads'),
  cpp_args: protobuf_args, gnu_symbol_visibility: 'hidden', override_options: protobuf_options)
protobuf = declare_dependency(link_with: protobuf_runtime, include_directories: protobuf_inc,
  compile_args: protobuf_args)
protobuf_native = protobuf_runtime
if meson.is_cross_build()
  protobuf_native = static_library('odamex-protobuf-native', protobuf_sources, native: true,
    include_directories: protobuf_inc, dependencies: dependency('threads', native: true),
    override_options: protobuf_options)
endif
protoc = executable('odamex-protoc', protobuf_compiler, 'protoc-main.cc', native: true,
  link_with: protobuf_native, include_directories: protobuf_inc,
  cpp_args: meson.is_cross_build() ? [] : protobuf_args,
  dependencies: dependency('threads', native: true), override_options: protobuf_options)
proto = custom_target('odamex-schemas',
  input: files('odaproto/common.proto', 'odaproto/server.proto'),
  output: ['common.pb.cc', 'common.pb.h', 'server.pb.cc', 'server.pb.h'],
  command: [protoc, '-I', root / 'odaproto',
    '--cpp_out=@OUTDIR@', root / 'odaproto/common.proto', root / 'odaproto/server.proto'])
schemas = declare_dependency(sources: proto, include_directories: include_directories('.'))

patched = ['client/sdl/i_input.cpp', 'client/sdl/i_video.cpp', 'client/src/v_video.cpp', 'client/src/cl_main.cpp',
  'client/src/d_main.cpp', 'client/src/m_menu.cpp', 'client/src/m_options.cpp',
  'client/src/cl_demo.cpp', 'client/src/c_console.cpp',
  'client/src/cl_parse.cpp', 'client/src/cl_download.cpp', 'client/src/otransfer.cpp',
  'common/d_main.cpp', 'common/i_net.cpp',
  'common/com_misc.cpp', 'common/g_gametype.cpp', 'common/g_spree.cpp',
  'common/p_horde.cpp', 'common/p_interaction.cpp', 'common/p_map.cpp', 'common/p_pspr.cpp',
  'common/p_switch.cpp', 'common/svc_map.cpp', 'common/svc_message.cpp', 'common/i_time.cpp',
  'common/m_resfile.cpp']
excluded = patched + ['common/i_crash_posix.cpp', 'common/i_crash_win32.cpp',
  'common/m_consolecommandstream.cpp', 'common/m_fileio_posix.cpp',
  'common/m_fileio_win32.cpp', 'common/m_stacktrace.cpp']
source_names = run_command(python, '-c',
  'from pathlib import Path; import sys; root = Path(sys.argv[1]); print(*sorted(p.relative_to(root).as_posix() for d in ("client/src", "common") for p in (root / d).glob("*.cpp")), sep=chr(10))',
  root, check: true).stdout().strip().split('\n')
sources = []
foreach path : source_names
  if path not in excluded
    sources += files(path)
  endif
endforeach
sources += files('libraries/fmt/src/format.cc', 'libraries/minilzo/minilzo.c',
  'libraries/jsoncpp/src/lib_json/json_reader.cpp', 'libraries/jsoncpp/src/lib_json/json_value.cpp',
  'libraries/jsoncpp/src/lib_json/json_writer.cpp')
zlib_sources = ['adler32', 'compress', 'crc32', 'deflate', 'gzclose', 'gzlib', 'gzread',
  'gzwrite', 'infback', 'inffast', 'inflate', 'inftrees', 'trees', 'uncompr', 'zutil']
zlib_files = []
foreach name : zlib_sources
  zlib_files += files('libraries/zlib' / (name + '.c'))
endforeach
zlib_inc = include_directories('libraries/zlib')
zlib_args = host_machine.system() == 'windows' ? [] : ['-DHAVE_UNISTD_H']
zlib = static_library('odamex-zlib', zlib_files, c_args: zlib_args, pic: true,
  include_directories: zlib_inc, gnu_symbol_visibility: 'hidden')
configure_file(input: 'libraries/libpng/scripts/pnglibconf.h.prebuilt', output: 'pnglibconf.h', copy: true)
png_sources = []
foreach name : ['png', 'pngerror', 'pngget', 'pngmem', 'pngpread', 'pngread', 'pngrio',
    'pngrtran', 'pngrutil', 'pngset', 'pngtrans', 'pngwio', 'pngwrite', 'pngwtran', 'pngwutil']
  png_sources += files('libraries/libpng' / (name + '.c'))
endforeach
png_inc = include_directories('.', 'libraries/libpng')
png = static_library('odamex-png', png_sources, pic: true,
  c_args: ['-DPNG_ARM_NEON_OPT=0'], include_directories: [png_inc, zlib_inc],
  gnu_symbol_visibility: 'hidden')
images = declare_dependency(link_with: [png, zlib], include_directories: [png_inc, zlib_inc])
adl = 'libraries/libadlmidi'
adl_flags = ['-DENABLE_END_SILENCE_SKIPPING', '-DBWMIDI_DISABLE_XMI_SUPPORT']
foreach emulator : ['NUKED', 'DOSBOX', 'JAVA', 'ESFMU', 'MAME_OPL2', 'YMFM']
  adl_flags += '-DADLMIDI_DISABLE_' + emulator + '_EMULATOR'
endforeach
foreach name : ['adlmidi.cpp', 'adlmidi_load.cpp', 'adlmidi_midiplay.cpp', 'adlmidi_opl3.cpp',
    'adlmidi_private.cpp', 'adlmidi_sequencer.cpp', 'inst_db.cpp', 'wopl/wopl_file.c',
    'chips/opal_opl3.cpp', 'chips/opal/opal.c', 'models/model_generic.c', 'models/model_ail.c',
    'models/model_apogee.c', 'models/model_dmx.c', 'models/model_hmi_sos.c', 'models/model_msadlib.c',
    'models/model_oconnell.c', 'models/model_win9x.c']
  sources += files(adl / 'src' / name)
endforeach
core = declare_dependency(sources: sources,
  include_directories: include_directories('.', 'common', 'client/src', 'client/sdl',
    'client/gui', 'libraries/fmt/include', 'libraries/minilzo', 'libraries/jsoncpp/include',
    'libraries/tinylibs/include', 'libraries', adl / 'include'),
  dependencies: [dependency('threads'), protobuf, images, schemas])
patch_inputs = {}
foreach path : patched
  patch_inputs += {path.replace('/', '-'): files(path)}
endforeach
resource_names = run_command(python, '-c',
  'from pathlib import Path; import sys; root = Path(sys.argv[1]); print(*sorted(p.relative_to(root).as_posix() for p in (root / "wad").rglob("*") if p.is_file()), sep=chr(10))',
  root, check: true).stdout().strip().split('\n')
resource_inputs = files(resource_names)
