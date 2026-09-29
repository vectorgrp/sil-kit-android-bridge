# Building, packaging and releasing

## Prerequisites

### Linux (Ubuntu)

| Dependency | How to install |
|------------|----------------|
| **SIL Kit** | `sudo dpkg -i libsilkit5_<version>-1ubuntu1_amd64.deb libsilkit-dev_<version>-1ubuntu1_amd64.deb silkit-utils_<version>-1ubuntu1_amd64.deb` from [GitHub Releases](https://github.com/vectorgrp/sil-kit/releases) |
| **gRPC + Protobuf + GTest** | fetched automatically with Conan 2 (see below) |
| **CMake** | >= 3.21 |
| **uv** | [astral.sh/uv](https://docs.astral.sh/uv/) |

Do **not** install `protobuf-compiler` or `protobuf-compiler-grpc`. Conan's
build modules find them on `PATH` and would generate code for the wrong
version. `cmake/gRPC.cmake` works around it and logs when it does.

### Windows

| Dependency | How to install |
|------------|----------------|
| **SIL Kit** | `SilKit-<version>.msi`, or unpack `SilKit-<version>-Win-x86_64-VS2019.zip` from [GitHub Releases](https://github.com/vectorgrp/sil-kit/releases) and pass `-DSilKit_DIR=<...>/lib/cmake/SilKit` |
| **gRPC + Protobuf + GTest** | fetched automatically with Conan 2 (see below) |
| **Visual Studio** | 2019 or newer, with the C++ toolset and Ninja |
| **CMake** | >= 3.21 |
| **uv** | [astral.sh/uv](https://docs.astral.sh/uv/) |

Build from a Visual Studio developer command prompt (or after `vcvars64.bat`),
so the compiler and Ninja are on `PATH`.

## Build

```sh
cmake --preset relwithdebinfo
cmake --build --preset relwithdebinfo
```

Presets: `relwithdebinfo`, `release`, and `debug` (Linux only, see
[Debug builds](#debug-builds)). Output goes to `_build/<OS>/<preset>/`.

## Dependencies (Conan)

`SKAB_STATIC_DEPS=ON` (the default) resolves gRPC, Protobuf and GoogleTest with
Conan 2 and links them statically, so the binaries run on machines other than
the build host. A dynamically linked build does not: the 0.4.x Linux archive,
built on Ubuntu 22.04, failed to start on 24.04.

`-DSKAB_STATIC_DEPS=OFF` uses the distribution's packages instead, which link
dynamically only. Install them first:
`sudo apt install libgrpc++-dev libprotobuf-dev protobuf-compiler-grpc libgtest-dev libgmock-dev`.

How the build handles Conan:

- It installs its own Conan 2 client with `uv` into `_build/<OS>/conan-venv/`
  and runs `conan install` during configure. A Conan on `PATH` is ignored, since
  it is often Conan 1.x, which cannot read this `conanfile.py`. An existing
  Conan 1 setup is left alone.
- The Conan home is `_build/<OS>/conan2/`, so nothing is written outside
  `_build/`, and `rm -rf _build` is a full reset. Each worktree gets its own
  package cache; set `SKAB_CONAN_HOME_DIR` to share one.
- Both directories are per host OS, because the home stores the detected
  compiler profile. Without that, a WSL configure would pick up the Windows
  profile.
- ConanCenter publishes Windows binaries for msvc 194 (Release, dynamic runtime)
  only, while Visual Studio 2026 is msvc 195.
  [cmake/conan/compatibility.py](../cmake/conan/compatibility.py) lets 195 reuse
  the 194 and 193 binaries, so nothing is built from source.
- Do not pin `compiler.version` or `compiler.cppstd` in a profile instead.
  Pinning `compiler.version=194` makes source builds call
  `vcvarsall.bat -vcvars_ver=14.4`, which fails when only the 14.5 toolset is
  installed. Pinning `compiler.cppstd` discards every prebuilt binary. The C++
  standard this project uses comes from `CMAKE_CXX_STANDARD`.

Cache variables (see [cmake/fetch_conan.cmake](../cmake/fetch_conan.cmake)):

| Variable | Purpose |
|----------|---------|
| `SKAB_STATIC_DEPS` | resolve gRPC/Protobuf/GoogleTest with Conan and link them statically (default `ON`) |
| `SKAB_GRPC_CONAN_REFERENCE` | gRPC reference, e.g. for a different remote or channel. Default `grpc/1.69.0` (ConanCenter has 1.54.3, 1.69.0 and 1.83.0 only) |
| `SKAB_CONAN_PROFILE` | Conan 2 profile for host and build. Empty uses `default`, created with `conan profile detect` |
| `SKAB_CONAN_SETTINGS` | extra `--settings key=value` arguments |
| `SKAB_CONAN_VENV_DIR` | location of the Conan virtual environment (default `_build/<OS>/conan-venv`) |
| `SKAB_CONAN_HOME_DIR` | Conan home (default `_build/<OS>/conan2`) |
| `SKAB_CONAN_EXECUTABLE` | use this Conan 2 client instead of installing one |
| `SKAB_CONAN_BUILD_POLICY` | Conan `--build` policy. CI uses `never`, so a missing binary fails at once instead of starting an hour-long source build |
| `SKAB_ALLOW_WINDOWS_DEBUG` | allow a Debug configure on Windows (see below) |

## Debug builds

ConanCenter publishes no Debug binaries for this dependency graph, on either
platform (checked 2026-09-14). Building Debug dependencies compiles gRPC,
Protobuf, abseil, OpenSSL, re2, c-ares and zlib from source. That takes about
an hour, inside CMake's configure step, where it looks like a hang.

- **Windows:** a Debug configure is refused, because Debug links `/MDd` and
  cannot use the published `/MD` packages. Use `relwithdebinfo`: it has full
  debug symbols and is what CI builds. `-DSKAB_ALLOW_WINDOWS_DEBUG=ON` allows
  Debug if you accept the source build.
- **Linux:** `--preset debug` links the Release dependencies and compiles only
  this project as Debug. The two then disagree about `NDEBUG`; if that matters,
  use `-DSKAB_STATIC_DEPS=OFF`.

## Packaging

```sh
cmake --preset relwithdebinfo
cmake --build --preset relwithdebinfo --target package
```

| Platform | Output in `_build/<OS>/relwithdebinfo/` |
|----------|------------------------------------------|
| Linux | `sil-kit-android-bridge-<version>-Linux-x86_64.zip` (`bin/sil-kit-android-bridge`) and `.deb` (`/usr/bin/sil-kit-android-bridge`, install with `sudo dpkg -i`) |
| Windows | `sil-kit-android-bridge-<version>-Windows-x86_64.zip` with `bin/sil-kit-android-bridge.exe`, `bin/silkit-client.exe` and `bin/SilKit.dll` |

gRPC, Protobuf, abseil, OpenSSL, re2, c-ares, zlib and the vendored spdlog are
linked statically on both platforms. The only other runtime dependencies are:

- SIL Kit, which exists only as a shared library. The Windows archive ships
  `SilKit.dll`. The `.deb` declares `libsilkit` through `dpkg-shlibdeps`.
- The system C/C++ runtime. On Windows the Microsoft C++ redistributable
  (`MSVCP140.dll`, `VCRUNTIME140*.dll`) is expected to be installed.

This is enforced. `ci/assert-static-deps.sh` fails if a statically linked
library appears in `DT_NEEDED`. It runs as the
`PackagedBinaryHasNoDynamicVendoredDeps` CTest case and in CI. CI also starts
the packaged binary in a bare `ubuntu:24.04` container with only SIL Kit
mounted.

The Linux artifact is built on Ubuntu 24.04 and needs glibc 2.39 or newer. It
does not run on 22.04.

Because the dependencies are inside the executables, their license texts ship
with them, under `share/doc/sil-kit-android-bridge/licenses/`: one directory per
component, plus `NOTICES-conan.md` with versions and SPDX identifiers.
[cmake/conan/licenses_deployer.py](../cmake/conan/licenses_deployer.py) collects
the Conan ones and fails the configure if one has no license text.
[cmake/Notices.cmake](../cmake/Notices.cmake) installs them together with the
vendored ones. `ci/assert-notices.sh` fails CI if the package is missing any.

## Releasing

The version lives only in [cmake/Version.cmake](../cmake/Version.cmake); CMake,
Conan and CI all read it from there. Keep its `set()` lines plain, because
`conanfile.py` and CI parse them with a regular expression.

1. Bump the version in `cmake/Version.cmake` and merge to `main`.
2. Tag that commit and push the tag. The tag push starts the release:
   ```sh
   git tag -a vX.Y.Z -m "vX.Y.Z"
   git push origin vX.Y.Z
   ```
3. CI checks the tag against `cmake/Version.cmake`, builds and tests Linux and
   Windows, and creates a **draft** release with the archives attached.
4. Review the draft's notes and press Publish.

Do not publish a release before CI has attached the archives. With
[immutable releases](https://docs.github.com/en/code-security/concepts/supply-chain-security/immutable-releases)
enabled, publishing locks the assets and reserves the tag name permanently.
Deleting the release does not free the name, so that version has to be
skipped.

A release created in the web UI also works (CI uploads to an existing draft or
release), but pushing the tag is the reliable trigger. A draft for a tag that
does not exist yet creates no tag, so no build starts.
