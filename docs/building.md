# Building, packaging and releasing

The short version is in the [README](../README.md#build). This covers what has
to be installed first, why the Windows build provisions its own Conan, and the
maintainer procedures.

## Prerequisites

### Linux (Ubuntu)

| Dependency | How to install |
|------------|----------------|
| **SIL Kit** | `sudo dpkg -i libsilkit5_<version>-1ubuntu1_amd64.deb libsilkit-dev_<version>-1ubuntu1_amd64.deb silkit-utils_<version>-1ubuntu1_amd64.deb` from [GitHub Releases](https://github.com/vectorgrp/sil-kit/releases) |
| **gRPC + Protobuf** | `sudo apt install libgrpc++-dev libprotobuf-dev protobuf-compiler-grpc` |
| **GTest + GMock** | `sudo apt install libgtest-dev libgmock-dev` |
| **CMake** | >= 3.21 |

### Windows

| Dependency | How to install |
|------------|----------------|
| **SIL Kit** | `SilKit-<version>.msi`, or unpack `SilKit-<version>-Win-x86_64-VS2019.zip` from [GitHub Releases](https://github.com/vectorgrp/sil-kit/releases) and pass `-DSilKit_DIR=<...>/lib/cmake/SilKit` |
| **gRPC + Protobuf + GTest** | fetched automatically with Conan 2 (see below) |
| **Visual Studio** | 2019 or newer, with the C++ toolset and Ninja |
| **CMake** | >= 3.21 |
| **uv** | [astral.sh/uv](https://docs.astral.sh/uv/) |

Run the build from a Visual Studio developer command prompt (or after
`vcvars64.bat`) so that the compiler and Ninja are on `PATH`.

## Conan on Windows

The build provisions a Conan 2 client into a virtual environment at
`_build/conan-venv/` using `uv`, and runs `conan install` during CMake configure.
A Conan client that happens to be on `PATH` is deliberately not used: many
machines have a system-wide Conan **1.x**, which cannot read this repository's
Conan 2 `conanfile.py`. Conan 1 and Conan 2 share nothing (`~/.conan` plus
`CONAN_USER_HOME` versus `~/.conan2` plus `CONAN_HOME`), so an existing Conan 1
setup is left untouched.

Both the client and the Conan package cache live under `_build/`, so a build
creates no state anywhere else: nothing for two accounts on a shared machine to
collide over, and `rm -rf _build` is a complete reset. The cost is that each
worktree keeps its own package cache; point `CONAN_HOME_DIR` somewhere shared if
you would rather trade that back.

Relevant cache variables (see [cmake/fetch_conan.cmake](../cmake/fetch_conan.cmake)):

| Variable | Purpose |
|----------|---------|
| `GRPC_CONAN_REFERENCE` | gRPC reference, e.g. to use a different remote or channel. Defaults to `grpc/1.69.0` (ConanCenter carries 1.54.3, 1.69.0 and 1.83.0 only) |
| `CONAN_PROFILE` | Conan 2 profile name for host and build. Empty uses `default`, created via `conan profile detect` |
| `CONAN_SETTINGS` | extra `--settings key=value` arguments |
| `CONAN_VENV_DIR` | location of the virtual environment (default `_build/conan-venv`) |
| `CONAN_HOME_DIR` | Conan home for this project (default `_build/conan2`) |
| `CONAN_EXECUTABLE` | use an existing Conan 2 client instead of provisioning one |
| `SKAB_ALLOW_WINDOWS_DEBUG` | permit a Debug configure despite the source build it implies (see below) |

The Conan home is scoped to this project so that an existing Conan installation
is never touched, and so that the build can install its own compatibility plugin
into it ([cmake/conan/compatibility.py](../cmake/conan/compatibility.py)).

That plugin is what keeps builds fast. ConanCenter publishes prebuilt Windows
binaries for one configuration only - msvc 194, cppstd 14, Release, dynamic
runtime - while Visual Studio 2026 detects as msvc 195. A Conan `package_id` is
an exact-match cache key rather than a statement about ABI compatibility, so
without help nothing matches and every dependency is compiled from source. The
plugin declares the msvc `195 -> 194 -> 193` fallback, so the published binaries
are reused. No extra flags are needed.

Declaring `compiler.version=194` in the profile instead does **not** work: Conan
derives `vcvars_ver` from `compiler.version`, so any package that still has to be
built from source invokes `vcvarsall.bat -vcvars_ver=14.4` and fails on a machine
that only has the 14.5 toolset installed.

Do not pin `compiler.cppstd` either. abseil, protobuf, grpc and gtest all mark
cppstd as ABI-relevant, so pinning it discards every prebuilt binary. The
standard this project compiles with comes from `CMAKE_CXX_STANDARD` and has
nothing to do with that setting.

## No debug preset on Windows

ConanCenter publishes exactly one Windows binary for `grpc/1.69.0` - `msvc 194,
Release, dynamic` - and no Debug binary for any package in the graph. A Debug
configure therefore compiles gRPC, Protobuf, abseil, OpenSSL, re2, c-ares and
zlib from source, which takes about an hour and happens inside CMake's configure
step, where it looks like a hang.

So `debug` is a Linux-only preset. On Windows use `relwithdebinfo`: it carries
full debug symbols, is what CI builds, and is the configuration these
instructions assume. What it gives up against a true Debug build is `/Od` and
the debug CRT's heap and iterator diagnostics.

Configuring `Debug` without a preset - Visual Studio's CMake integration, or a
hand-written `-DCMAKE_BUILD_TYPE=Debug` - stops with an error explaining the
same thing. Pass `-DSKAB_ALLOW_WINDOWS_DEBUG=ON` if you genuinely need debug
builds of the dependencies and accept the wait.

## Packaging

```sh
cmake --preset relwithdebinfo
cmake --build --preset relwithdebinfo --target package
```

This produces inside `_build/Linux/relwithdebinfo/`:
- `sil-kit-android-bridge-<version>-Linux-x86_64.zip` - portable archive (`bin/sil-kit-android-bridge`)
- `sil-kit-android-bridge-<version>-Linux-x86_64.deb` - Debian/Ubuntu package (`/usr/bin/sil-kit-android-bridge`)

Install the `.deb` with:

```sh
sudo dpkg -i sil-kit-android-bridge-*.deb
```

On Windows a ZIP is produced instead, inside `_build/Windows/relwithdebinfo/`:
`sil-kit-android-bridge-<version>-Windows-x86_64.zip`, containing
`bin/sil-kit-android-bridge.exe`, `bin/silkit-client.exe` and `bin/SilKit.dll`.
Unlike the Debian package, which resolves libsilkit through `dpkg-shlibdeps`,
the archive is self-contained.

gRPC, Protobuf, abseil, OpenSSL, re2, c-ares and zlib are linked **statically**,
as is the vendored spdlog, so they need no DLLs beside the executable - `dumpbin
/dependents` on the packaged binary lists only `SilKit.dll` and Windows system
libraries. `SilKit.dll` is the one exception, because SIL Kit is distributed as
a shared library with no static variant. The Microsoft C++ runtime
(`MSVCP140.dll`, `VCRUNTIME140*.dll`) is expected from the system; the
redistributable is not bundled.

Because those libraries end up inside the executables, their license texts ship
with them, under `share/doc/sil-kit-android-bridge/licenses/` - one directory
per component, plus `NOTICES-conan.md` listing versions and SPDX identifiers for
the Conan-resolved ones. Those are collected at configure time by
[cmake/conan/licenses_deployer.py](../cmake/conan/licenses_deployer.py); the
vendored components (spdlog, fmt, the AOSP VHAL) and SIL Kit itself are installed
by [cmake/Notices.cmake](../cmake/Notices.cmake).

## Releasing

Maintainer procedure. The project version lives in
[cmake/Version.cmake](../cmake/Version.cmake) and nowhere else; CMake, Conan and
CI all read it from there.

1. Bump the triple in `cmake/Version.cmake` and merge to `main`.
2. Tag that commit and push the tag - this is what starts a release:
   ```sh
   git tag -a vX.Y.Z -m "vX.Y.Z"
   git push origin vX.Y.Z
   ```
3. CI checks the tag against `cmake/Version.cmake` (a mismatch fails in seconds,
   before either platform builds), builds and tests Linux and Windows, then
   creates a **draft** release with the archives attached.
4. Review the generated notes on the draft and press Publish.

Do not publish a release by hand before CI has attached the archives. If
[immutable releases](https://docs.github.com/en/code-security/concepts/supply-chain-security/immutable-releases)
are enabled, publishing seals the release: its assets can no longer be changed,
and its tag name is reserved permanently - deleting the release does not give the
name back, so the version has to be skipped.

Creating the release in the UI is still handled - the job uploads to an existing
draft or release rather than failing - but pushing the tag is the reliable
trigger, since a draft against a tag that does not exist yet creates no tag and
so starts no build.
