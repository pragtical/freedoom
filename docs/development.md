# Development

## Build

Install a C compiler, Python 3, Meson, and Ninja. The default build includes
Odamex and also needs a C++17 compiler, Pillow, and Make. On Linux and macOS,
install pkg-config and OpenSSL development files, including static libraries.
Windows builds use MinGW and Schannel. CMake, SDL, and OpenAL are not required.
Use `-Dodamex=false` for a C-only Woof build without the Odamex dependencies.
Meson downloads pinned engine and dependency sources.

To build, test, and install into a Pragtical profile:

```sh
profile="$HOME/.config/pragtical"
meson setup build-meson --buildtype=release --prefix="$profile" -Ddata_dir=.
python3 scripts/fetch-assets.py
meson compile -C build-meson
meson test -C build-meson --num-processes 1 --print-errorlogs
meson install -C build-meson
python3 scripts/fetch-assets.py --directory "$profile/plugins/freedoom"
```

Set `profile` to the actual Pragtical user directory on your system, or use an
isolated test profile. Meson installs architecture-qualified libraries at the
plugin root and engine resources under `assets/`. The fetch script installs
verified IWADs under `iwads/`. Restart Pragtical after installing new Lua or
native code. On Windows, close Pragtical before replacing a loaded DLL.

Cross-compile for Windows from Linux with MinGW-w64:

```sh
meson setup build-windows --cross-file cross/x86_64-w64-mingw32.ini \
  --buildtype=release -Dstrip=true
meson compile -C build-windows
```

## Editor Tests

Install the plugin and its assets in a disposable profile, then run from the
repository root:

```sh
PRAGTICAL_USERDIR=/path/to/test-profile pragtical test tests/editor
```

The editor suite checks the game picker and default campaign. The Meson
suites cover native startup, rendering, audio, packaging, and local
multiplayer. Install LuaJIT to include the standalone Lua tests.

## Release Packages

Example for Linux x86_64:

```sh
meson setup build-release --buildtype=release -Dstrip=true
meson compile -C build-release
python3 scripts/package.py build-release --arch x86_64-linux --sources
python3 scripts/fetch-assets.py --bundle dist
python3 scripts/check-release.py dist --arch x86_64-linux --odamex
```

The platform archive contains native libraries, engine resources, and license
notices. PPM copies the Lua files from the repository and downloads that
archive and the shared IWAD bundle. The corresponding-source archive includes
the pinned dependency sources but excludes downloaded IWADs, saves, build
outputs, and local notes.

The [GitHub workflow](../.github/workflows/build.yml) builds and validates both
engines for five targets, with Ubuntu 22.04 as the Linux compatibility
baseline. The macOS jobs build static OpenSSL for the macOS 11 deployment
target instead of linking a newer Homebrew bottle.
Branch builds publish a `continuous` prerelease. A matching version
tag (currently `v0.1.0`) or manual release dispatch publishes the versioned
release and updates `latest`, which PPM uses. Pull requests do not publish.
