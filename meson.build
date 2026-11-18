project('freedoom', 'c', version: '0.1.0', license: ['MIT', 'GPL-2.0-or-later'],
  meson_version: '>= 0.64', default_options: ['c_std=gnu11', 'buildtype=release', 'warning_level=1', 'b_ndebug=if-release'])
platform = host_machine.system()
if platform not in ['linux', 'darwin', 'windows']
  error('Freedoom supports Linux, macOS and Windows (MinGW).')
endif
cc = meson.get_compiler('c')
if cc.get_id() == 'msvc'
  error('Use MinGW for Windows builds of this plugin.')
endif
arch = get_option('arch_tuple')
if arch == ''
  arch = host_machine.cpu_family() + '-' + platform
endif
if arch != host_machine.cpu_family() + '-' + platform
  error('arch_tuple must match the target machine.')
endif
python = import('python').find_installation()
plugin_dir = get_option('data_dir') / 'plugins/freedoom'
if platform == 'windows'
  add_project_arguments('-DWIN32_LEAN_AND_MEAN=', '-D_WIN32_WINNT=0x0601', language: 'c')
  add_project_link_arguments('-static-libgcc', language: 'c')
else
  add_project_arguments('-D_DEFAULT_SOURCE', language: 'c')
endif
config = configuration_data()
config.set_quoted('PROJECT_NAME', 'Woof')
config.set_quoted('PROJECT_VERSION', '16.0.0')
config.set_quoted('PROJECT_STRING', 'Woof 16.0.0')
config.set_quoted('PROJECT_SHORTNAME', 'woof')
config.set_quoted('PROJECT_APPID', 'org.pragtical.freedoom')
config.set('HAVE_LIBM', 1)
if cc.compiles('typedef float vec __attribute__((ext_vector_type(3))); int main(void) { vec v = {0}; return v.x; }',
    args: ['-Werror=attributes'], name: 'extended vector types')
  config.set('HAVE_EXT_VECTOR_TYPE', 1)
endif
config.set10('HAVE_DECL_STRCASECMP', cc.has_function('strcasecmp', prefix: '#include <strings.h>'))
config.set10('HAVE_DECL_STRNCASECMP', cc.has_function('strncasecmp', prefix: '#include <strings.h>'))
if cc.has_function('getpwuid', prefix: '#include <pwd.h>')
  config.set('HAVE_GETPWUID', 1)
endif
configure_file(output: 'config.h', configuration: config)
woof = subproject('woof')
woof_root = woof.get_variable('woof_root')
resources = custom_target('woof-resources', output: 'woof.pk3',
  input: woof.get_variable('engine_resources'),
  command: [python, files('scripts/build-resources.py'), woof_root / 'base', '@OUTPUT@', '@INPUT@'],
  build_by_default: true)
deps = []
if platform != 'windows'
  deps += [dependency('threads'), cc.find_library('m')]
else
  deps += [cc.find_library('ws2_32'), cc.find_library('iphlpapi')]
endif
suffix = platform == 'windows' ? 'dll' : 'so'
reset_table = custom_target('engine-reset-table', output: 'engine-reset-table.c',
  input: 'src/woof/engine-state.json',
  command: [python, files('scripts/build-reset-table.py'), '@INPUT@', platform, '@OUTPUT@'])
network_content = custom_target('network-content', output: 'network-content.h',
  input: 'src/woof/network-content.json',
  command: [python, files('scripts/build-network-content.py'), '@INPUT@', '@OUTPUT@'])
embedded_sources = woof.get_variable('engine_sources') + files('src/woof/engine_exit.c',
  'src/woof/engine_input.c', 'src/woof/engine_video.c', 'src/woof/engine_sound.c', 'src/woof/engine_network.c')
prepared = []
fs = import('fs')
woof_types = custom_target('woof-color-types', output: 'woof-color-types.h',
  input: woof.get_variable('pixel_types'),
  command: [python, files('scripts/woof_truecolor.py'), '@INPUT@', '@OUTPUT@'])
foreach source : embedded_sources
  name = fs.stem(source)
  prepared += custom_target(name + '-embedded', output: name + '-embedded.c',
    input: [source, files('src/woof/engine-state.json')],
    depend_files: files('scripts/woof_truecolor.py'),
    command: [python, files('scripts/prepare-engine.py'), '@INPUT0@', '@OUTPUT@',
      '@INPUT1@', '--platform', platform])
endforeach
engine = shared_library('freedoom-bridge.' + arch,
  prepared, reset_table, network_content, woof_types, woof.get_variable('music_meter_sources'),
  'src/platform.c', 'src/woof/session.c', 'src/framebuffer.c', 'src/woof/engine_memory.c', 'src/audio_transport.c', 'src/woof/content.c',
  'src/woof/truecolor.c',
  include_directories: [include_directories('src/platform', 'src/woof', 'src', '.'), woof.get_variable('engine_includes')],
  c_args: ['-DFD_BUILD_SHARED', '-DWOOF_RELEASE', '-DRANGECHECK', '-DSPNG_USE_MINIZ', '-DSPNG_STATIC', '-DMINIZ_NO_TIME', '-DYYJSON_DISABLE_UTILS=1',
    '-include', woof_types.full_path()],
  dependencies: deps, name_prefix: '', name_suffix: suffix,
  gnu_symbol_visibility: 'hidden', install: false)
install_args = ['--directory', plugin_dir,
  '--license', woof_root / 'COPYING', 'Woof.txt',
  '--license', woof_root / 'README.md', 'Woof-notices.md',
  '--data', resources.full_path(), 'assets/woof.pk3']
foreach name : ['miniz', 'spng', 'yyjson']
  install_args += ['--license', woof_root / 'third-party' / name / 'LICENSE', name + '.txt']
endforeach
install_args += ['--license', woof_root / 'third-party/libebur128/COPYING', 'libebur128.txt']
install_args += ['--license', woof_root / 'third-party/libebur128/queue/sys/queue.h', 'libebur128-queue.txt']
install_args += ['--license', woof_root / 'netlib/netlib.c', 'netlib.txt']
if get_option('strip')
  install_args += ['--strip', find_program('strip').full_path()]
endif
if platform == 'darwin'
  install_args += '--codesign'
endif
meson.add_install_script('scripts/install-runtime.py', install_args, engine.full_path())
install_data('init.lua', 'runtime.lua', 'multiplayer.lua', 'games.lua', 'odamex.lua', 'README.md', 'LICENSE', install_dir: plugin_dir)
install_data('iwads/README.md', install_dir: plugin_dir / 'iwads')
install_data('assets/README.md', install_dir: plugin_dir / 'assets')
frame_test = executable('test-framebuffer', 'tests/framebuffer.c', 'src/framebuffer.c', 'src/platform.c',
  dependencies: deps, include_directories: include_directories('src'))
test('framebuffer', frame_test)
audio_test = executable('test-audio', 'tests/audio.c', 'src/audio_transport.c', 'src/platform.c',
  dependencies: deps, include_directories: include_directories('src'))
test('audio', audio_test)
session_test = executable('test-session', 'tests/session.c', link_with: engine,
  c_args: ['-DFD_USE_SHARED'], include_directories: include_directories('src', 'src/woof'))
if not meson.is_cross_build()
  dedicated_test = executable('test-woof-server', woof.get_variable('dedicated_sources'),
    'tests/dedicated.c', 'src/platform.c',
    include_directories: [include_directories('src/platform', 'src', '.'), woof.get_variable('engine_includes')],
    dependencies: deps, c_args: ['-DWOOF_RELEASE'])
  luajit = find_program('luajit', required: false)
  if luajit.found()
    test('lua-runtime', luajit, args: [files('tests/runtime.lua'), engine.full_path(), meson.project_source_root()],
      depends: engine)
    test('multiplayer-ui', luajit, args: [files('tests/multiplayer.lua'), meson.project_source_root()])
    test('game-catalog', luajit, args: [files('tests/games.lua'), meson.project_source_root()])
  endif
  test('engine-lifecycle', python, args: [files('tests/run-native.py'), session_test.full_path(), meson.project_source_root()],
    depends: [session_test, engine, resources], timeout: 120)
  test('engine', python, args: [files('tests/engine.py'), engine.full_path(), '50'],
    depends: [engine, resources], timeout: 180)
  test('engine-audio', python, args: [files('tests/engine.py'), engine.full_path(), '3'],
    env: ['FREEDOOM_TEST_SOUND=1'], depends: [engine, resources], timeout: 60)
  test('music-levels', python, args: [files('tests/music.py'), engine.full_path(), meson.current_build_dir()],
    depends: [engine, resources], timeout: 60)
  test('menus', python, args: [files('tests/menus.py'), engine.full_path()],
    depends: [engine, resources], timeout: 120)
  test('network', python, args: [files('tests/network.py'), engine.full_path()],
    depends: [engine, resources], timeout: 180)
  test('turning', python, args: [files('tests/turning.py'), engine.full_path()],
    depends: [engine, resources], timeout: 60)
  test('truecolor', python, args: [files('tests/truecolor.py'), engine.full_path()],
    depends: [engine, resources], timeout: 45)
  test('internet', python, args: [files('tests/internet.py'), engine.full_path(), dedicated_test.full_path()],
    depends: [engine, resources, dedicated_test], timeout: 120)
  test('manifest', python, args: [files('scripts/check-release.py')])
  test('prepare-engine', python, args: [files('tests/prepare-engine.py')])
  test('network-content', python, args: [files('tests/content.py')])
  test('iwad-metadata', python, args: [files('tests/games.py'), engine.full_path()], depends: engine)
  test('installation', python, args: [files('tests/installation.py')])
  test('assets', python, args: [files('tests/assets.py')])
endif
if get_option('odamex')
  add_languages('cpp', native: false)
  odamex = subproject('odamex')
  curl = subproject('curl')
  subdir('src/odamex')
endif
summary({'Architecture': arch, 'Plugin directory': plugin_dir, 'Engine': 'Woof (in-process)',
  'Odamex': get_option('odamex')}, section: 'Freedoom')
