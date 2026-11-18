cpp = meson.get_compiler('cpp')
oda_sources = []
foreach path, source : odamex.get_variable('patch_inputs')
  name = fs.stem(path) + '-odamex.cpp'
  oda_sources += custom_target(name, input: source, output: name,
    command: [python, files('prepare.py'), '@INPUT@', '@OUTPUT@'])
endforeach
oda_libs = []
oda_flags = ['-DFD_BUILD_SHARED', '-DCLIENT_APP', '-DODAMEX_NO_GITVER',
  '-DINSTALL_BINDIR="bin"', '-DINSTALL_DATADIR="share"']
oda_link_args = []
oda_link_depends = []
oda_dependencies = [dependency('threads')]
if platform == 'windows'
  oda_flags += ['-DWIN32', '-DWIN32_LEAN_AND_MEAN=', '-DNOMINMAX', '-D_WIN32_WINNT=0x0601']
  oda_libs += cpp.find_library('ws2_32')
  oda_link_args += ['-static', '-static-libgcc', '-static-libstdc++']
else
  oda_flags += '-DUNIX'
  oda_dependencies += cc.find_library('dl', required: platform == 'linux')
endif
if platform == 'linux'
  oda_flags += cpp.get_supported_arguments('-fno-gnu-unique')
  oda_link_depends = files('exports.map')
  oda_link_args += '-Wl,--version-script=' + meson.current_source_dir() / 'exports.map'
elif platform == 'darwin'
  oda_flags += '-DOSX'
  oda_link_args += '-Wl,-exported_symbol,_fd_odamex_run'
endif
oda_libs += curl.get_variable('curl_dep')
oda_preview = shared_library('freedoom-odamex.' + arch, oda_sources,
  'session.cpp', 'video.cpp', 'input.cpp', 'sound.cpp',
  dependencies: [odamex.get_variable('core')] + oda_libs,
  include_directories: include_directories('.', '..'),
  cpp_args: oda_flags + odamex.get_variable('adl_flags'),
  link_args: oda_link_args, link_depends: oda_link_depends, override_options: ['cpp_std=c++17'],
  gnu_symbol_visibility: 'hidden', name_prefix: '', name_suffix: suffix,
  install: false)
oda_wad = custom_target('odamex-resources', output: 'odamex.wad',
  input: odamex.get_variable('resource_inputs'), build_by_default: true,
  command: [python, files('build-wad.py'), odamex.get_variable('root'), '@OUTPUT@'])
oda_host = shared_library('freedoom-odamex-host.' + arch, 'host.c',
  '../platform.c', '../framebuffer.c', '../audio_transport.c',
  include_directories: include_directories('.', '..'),
  c_args: ['-DFD_BUILD_SHARED', '-DFD_ODAMEX_LIBRARY="' + oda_preview.name() + '.' + suffix + '"'],
  dependencies: oda_dependencies,
  gnu_symbol_visibility: 'hidden', name_prefix: '', name_suffix: suffix, install: false)
oda_root = odamex.get_variable('root')
oda_install_args = ['--directory', plugin_dir, '--data', oda_wad.full_path(), 'assets/odamex.wad']
foreach notice, name : {
  'LICENSE': 'Odamex.txt', 'wad/doom.gpl': 'Odamex-resources.txt',
  'libraries/protobuf/LICENSE': 'Odamex-protobuf.txt',
  'libraries/libpng/LICENSE': 'Odamex-libpng.txt', 'libraries/zlib/LICENSE': 'Odamex-zlib.txt',
  'libraries/fmt/LICENSE': 'Odamex-fmt.txt', 'libraries/jsoncpp/LICENSE': 'Odamex-jsoncpp.txt',
  'libraries/minilzo/COPYING': 'Odamex-minilzo.txt', 'libraries/libadlmidi/LICENSE': 'libADLMIDI.txt',
  'libraries/libadlmidi/LICENSE.LGPL-2.1.txt': 'libADLMIDI-LGPL-2.1.txt',
  'libraries/libadlmidi/LICENSE.GPL-3.txt': 'libADLMIDI-GPL-3.txt',
  'libraries/libadlmidi/src/models/LICENSE.txt': 'libADLMIDI-models.txt',
  'libraries/libadlmidi/src/chips/opal/LICENSE.txt': 'Opal.txt',
}
  oda_install_args += ['--license', oda_root / notice, name]
endforeach
oda_install_args += ['--license', curl.get_variable('curl_license'), 'Odamex-curl.txt']
ssl_license = curl.get_variable('ssl_license')
if ssl_license != ''
  oda_install_args += ['--license', ssl_license, 'Odamex-openssl.txt']
endif
if get_option('strip')
  oda_install_args += ['--strip', find_program('strip').full_path()]
endif
if platform == 'darwin'
  oda_install_args += '--codesign'
endif
meson.add_install_script(python, files('../../scripts/install-runtime.py'), oda_install_args,
  oda_preview.full_path(), oda_host.full_path())
oda_test = executable('test-odamex-preview', '../../tests/odamex-preview.c', '../platform.c',
  include_directories: include_directories('.', '..'),
  dependencies: oda_dependencies, build_by_default: false)
test('odamex-preview', python, args: [files('../../tests/odamex-preview.py'),
  oda_test, oda_preview, oda_wad], timeout: 60)
