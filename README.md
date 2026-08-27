# sil-kit-android-bridge

`sil-kit-android-bridge` is a **SIL Kit Extension** that bridges an Android VHAL
(Vehicle Hardware Abstraction Layer) gRPC server to a
[SIL Kit](https://github.com/vectorgrp/sil-kit) simulation network, enabling
co-simulation between Android Automotive environments and CANoe/SIL Kit-based
test benches.

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

On Windows the build provisions a Conan 2 client into a virtual environment at
`_build/conan-venv/` using `uv`, and runs `conan install` during CMake configure.
A Conan client that happens to be on `PATH` is deliberately not used: corporate
machines often have a system-wide Conan **1.x**, which cannot read this
repository's Conan 2 `conanfile.py`. Conan 1 and Conan 2 share nothing
(`~/.conan` plus `CONAN_USER_HOME` versus `~/.conan2` plus `CONAN_HOME`), so an
existing Conan 1 setup is left untouched.

Both the client and the Conan package cache live under `_build/`, so a build
creates no state anywhere else: nothing for two accounts on a shared machine to
collide over, and `rm -rf _build` is a complete reset. The cost is that each
worktree keeps its own package cache; point `CONAN_HOME_DIR` somewhere shared if
you would rather trade that back.

Relevant cache variables (see [cmake/fetch_conan.cmake](cmake/fetch_conan.cmake)):

| Variable | Purpose |
|----------|---------|
| `GRPC_CONAN_REFERENCE` | gRPC reference, e.g. to use an internal Artifactory channel. Defaults to `grpc/1.69.0` (ConanCenter carries 1.54.3, 1.69.0 and 1.83.0 only) |
| `CONAN_PROFILE` | Conan 2 profile name for host and build. Empty uses `default`, created via `conan profile detect` |
| `CONAN_SETTINGS` | extra `--settings key=value` arguments |
| `CONAN_VENV_DIR` | location of the virtual environment (default `_build/conan-venv`) |
| `CONAN_HOME_DIR` | Conan home for this project (default `_build/conan2`) |
| `CONAN_EXECUTABLE` | use an existing Conan 2 client instead of provisioning one |
| `SK_ALLOW_WINDOWS_DEBUG` | permit a Debug configure despite the source build it implies (see below) |

The Conan home is scoped to this project so that an existing Conan installation
is never touched, and so that the build can install its own compatibility plugin
into it ([cmake/conan/compatibility.py](cmake/conan/compatibility.py)).

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

### No debug preset on Windows

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
same thing. Pass `-DSK_ALLOW_WINDOWS_DEBUG=ON` if you genuinely need debug
builds of the dependencies and accept the wait.

## Build

```sh
cmake --preset relwithdebinfo
cmake --build --preset relwithdebinfo
```

On Windows, run this from a Visual Studio developer command prompt (or after
`vcvars64.bat`) so that the compiler and Ninja are on `PATH`.

Binaries are placed in `_build/<OS>/<preset>/`, e.g.
`_build/Linux/relwithdebinfo/`. The presets are `debug`, `release` and
`relwithdebinfo`; `debug` is available on Linux only, for the reason above.

The primary target platform is Ubuntu; the build depends only on publicly
available packages. Windows is built and tested in CI as well.

## Usage

```
sil-kit-android-bridge [OPTIONS]

  -u, --silkit-uri <uri>       SIL Kit registry URI (default: silkit://localhost:8500)
  -V, --vhal-uri   <uri>       VHAL gRPC server URI (required unless --mock)
  -m, --mock                   Use built-in mock VHAL (no Android device needed)
  -n, --name       <name>      SIL Kit participant name (default: VhalBridge)
  -s, --synchronized           Enable coordinated lifecycle mode
  -l, --log        <level>     Log level: Trace|Debug|Info|Warn|Error|Off (default: Info)
  -c, --configuration <file>   Path to SIL Kit participant config YAML
  -v, --version                Print version information
  -h, --help                   Print this help
```

### Quick start

Ensure that a registry is running and/or reachable for the sil-kit-android-bridge.
Refer to the [SIL Kit documentation for details.](https://vectorgrp.github.io/sil-kit-docs/for-users/users.html#quick-start)
Determine the URI of the VHAL proxy process, e.g., here it is running with a unix domain socket at `unix:///tmp/vsock_3_1002/vm.vsock_9300`:
```sh
$ pgrep -al vhal
1234 /var/tmp/cvd/.../artifacts/host_tools/bin/vhal_proxy_server /var/tmp/cvd/.../artifacts/host_tools/etc/automotive/vhalconfig ...:9300 unix:///tmp/vsock_3_1002/vm.vsock_9300
```
```sh
# Start a SIL Kit registry (from your SIL Kit installation)
sil-kit-registry --listen-uri localhost:8501&

# Run the adapter against the VHAL proxy 
./_build/Linux/relwithdebinfo/sil-kit-android-bridge --vhal-uri unix:///tmp/vsock_3_1002/vm.vsock_9300 --silkit-uri localhost:8501
```
This will start a local registry running on port 8501  and the android bridge.
To send test data, use the test executable `silkit-client` to connect to the bridge:
```sh
$ ./silkit-client
Connecting participant 'silkit-client' to registry 'silkit://localhost:8500' (Namespace=VhalSilkit, Instance=vhalProperties, VirtualNetwork=Default)
No assignments on the command line; reading PROPERTY=value lines from stdin (Ctrl+D / Ctrl+Z to end).
RANGE_REMAINING=1235
SET  RANGE_REMAINING = 1235 (float) -> Success
GET  RANGE_REMAINING = 1235 (float)
```

## Testing

Tests live in [test/](test/) and use GoogleTest, registered with CTest. `Test_*`
are unit tests; `ITest_*` are integration tests that spin up an in-process SIL Kit
registry (no external `sil-kit-registry` needed). Run them all with:

```sh
ctest --preset relwithdebinfo --output-on-failure
```

`grpc-client-test` is a manual developer tool (not a CTest case) that requires a
running VHAL gRPC server:

```sh
./_build/Linux/relwithdebinfo/test/grpc-client-test /tmp/vsock_1_1000/vm.vsock
```

## Examples

[examples/vector-canoe-se/](examples/vector-canoe-se/) contains a CANoe
Server Edition test configuration that exercises the adapter from a CANoe test
bench.

## Packaging

```sh
cmake --preset relwithdebinfo
cmake --build --preset relwithdebinfo --target package
```

This produces inside `_build/Linux/relwithdebinfo/`:
- `sil-kit-android-bridge-<version>-Linux-x86_64.zip` — portable archive (`bin/sil-kit-android-bridge`)
- `sil-kit-android-bridge-<version>-Linux-x86_64.deb` — Debian/Ubuntu package (`/usr/bin/sil-kit-android-bridge`)

Install the `.deb` with:

```sh
sudo dpkg -i sil-kit-android-bridge-*.deb
```

On Windows a ZIP is produced instead, inside `_build/Windows/relwithdebinfo/`:
`sil-kit-android-bridge-<version>-Windows-x86_64.zip`, containing
`bin/sil-kit-android-bridge.exe`, `bin/silkit-client.exe` and `bin/SilKit.dll`.
Unlike the Debian package, which resolves libsilkit through `dpkg-shlibdeps`,
the archive is self-contained.

gRPC, Protobuf, abseil, OpenSSL, re2, c-ares, zlib and spdlog are linked
**statically**, so they need no DLLs beside the executable - `dumpbin
/dependents` on the packaged binary lists only `SilKit.dll` and Windows system
libraries. `SilKit.dll` is the one exception, because SIL Kit is distributed as
a shared library with no static variant. The Microsoft C++ runtime
(`MSVCP140.dll`, `VCRUNTIME140*.dll`) is expected from the system; the
redistributable is not bundled.

Because those libraries end up inside the executables, their license texts ship
with them, under `share/doc/sil-kit-android-bridge/licenses/` - one directory
per component, plus `NOTICES-conan.md` listing versions and SPDX identifiers.
The Conan ones are collected at configure time by
[cmake/conan/licenses_deployer.py](cmake/conan/licenses_deployer.py); see
[cmake/Notices.cmake](cmake/Notices.cmake) for the rest.

## License

Code authored by Vector Informatik GmbH is licensed under the MIT License
(see [LICENSE.md](LICENSE.md) and the `SPDX-License-Identifier: MIT` headers in each source file).

This repository also bundles third-party code under other licenses -- notably the vendored
AOSP Vehicle HAL (Apache-2.0) and spdlog/fmt (MIT). The project is therefore an aggregate,
not a pure-MIT work; the bundled components retain their own licenses. See
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for the complete list and attributions.

Built packages carry the corresponding license texts in
`share/doc/sil-kit-android-bridge/licenses/`, so a distributed archive is
complete on its own.
