project('curl-static', 'c', version: '8.23.0', license: 'curl')
python = import('python').find_installation()
cc = meson.get_compiler('c')
curl_license = meson.current_source_dir() / 'COPYING'
windows = host_machine.system() == 'windows'
darwin = host_machine.system() == 'darwin'
configure = ['--enable-static', '--disable-shared', '--with-pic', '--enable-http',
  '--without-zlib', '--without-brotli', '--without-zstd', '--without-libpsl', '--without-libssh2',
  '--without-libssh', '--without-nghttp2', '--without-nghttp3', '--without-ngtcp2',
  '--without-libidn2', '--without-libgsasl', '--disable-ldap', '--disable-ldaps', '--disable-ftp',
  '--disable-dict', '--disable-telnet', '--disable-tftp', '--disable-gopher', '--disable-imap',
  '--disable-pop3', '--disable-smtp', '--disable-mqtt', '--disable-rtsp']
deps = [dependency('threads')]
ssl_license = ''
if windows
  configure += ['--host=' + host_machine.cpu_family() + '-w64-mingw32', '--with-schannel', '--without-openssl']
  foreach lib : ['ws2_32', 'crypt32', 'bcrypt', 'normaliz', 'iphlpapi', 'advapi32', 'secur32']
    deps += cc.find_library(lib)
  endforeach
else
  configure += ['--with-openssl', '--with-ca-fallback']
  ssl = dependency('openssl', static: true)
  deps += ssl
  if darwin
    configure += '--with-apple-sectrust'
    deps += dependency('appleframeworks', modules: ['Security', 'CoreFoundation', 'SystemConfiguration'])
  else
    deps += cc.find_library('dl')
  endif
  ssl_prefix = ssl.get_variable(pkgconfig: 'prefix', default_value: '/usr')
  fs = import('fs')
  foreach candidate : [ssl_prefix / 'LICENSE.txt', ssl_prefix / 'share/doc/libssl-dev/copyright',
      ssl_prefix / 'share/licenses/openssl/LICENSE.txt']
    if fs.is_file(candidate)
      ssl_license = candidate
      break
    endif
  endforeach
endif
libcurl = custom_target('curl-static', output: 'libfdcurl.a',
  input: 'configure',
  command: [python, files('build-static.py'), find_program('sh', native: true),
    find_program('make', native: true), '@INPUT@', '@OUTPUT@',
    '--compiler', cc.cmd_array(), '--configure', configure])
curl_dep = declare_dependency(link_with: libcurl, dependencies: deps,
  include_directories: include_directories('include'), compile_args: ['-DCURL_STATICLIB'])
