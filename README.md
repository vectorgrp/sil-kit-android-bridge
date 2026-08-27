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
`.venv/` using `uv`, and runs `conan install` during CMake configure. A Conan
client that happens to be on `PATH` is deliberately not used: corporate machines
often have a system-wide Conan **1.x**, which cannot read this repository's
Conan 2 `conanfile.py`. Conan 1 and Conan 2 share nothing (`~/.conan` plus
`CONAN_USER_HOME` versus `~/.conan2` plus `CONAN_HOME`), so an existing Conan 1
setup is left untouched.

Relevant cache variables (see [cmake/fetch_conan.cmake](cmake/fetch_conan.cmake)):

| Variable | Purpose |
|----------|---------|
| `GRPC_CONAN_REFERENCE` | gRPC reference, e.g. to use an internal Artifactory channel. Defaults to `grpc/1.69.0` (ConanCenter carries 1.54.3, 1.69.0 and 1.83.0 only) |
| `CONAN_PROFILE` | Conan 2 profile name for host and build. Empty uses `default`, created via `conan profile detect` |
| `CONAN_SETTINGS` | extra `--settings key=value` arguments |
| `CONAN_VENV_DIR` | location of the virtual environment (default `.venv`) |
| `CONAN_EXECUTABLE` | use an existing Conan 2 client instead of provisioning one |

ConanCenter publishes prebuilt Windows binaries for one configuration only:
msvc 194, cppstd 14, Release, dynamic runtime. Visual Studio 2026 detects as
msvc 195, for which no binaries exist, so the dependencies would be compiled
from source. MSVC keeps binary compatibility across toolset updates, so ask for
the published configuration instead:

```
cmake --preset relwithdebinfo -DCONAN_SETTINGS=compiler.version=194
```

ConanCenter has no Debug binaries for these packages either, so `--preset debug`
on Windows compiles the dependencies from source (roughly an hour, once - the
result stays in the Conan cache). Prefer `--preset relwithdebinfo` or
`--preset release` unless a debug build of the dependencies is actually needed.

## Build

```sh
cmake --preset debug
cmake --build --preset debug
```

On Windows, run this from a Visual Studio developer command prompt (or after
`vcvars64.bat`) so that the compiler and Ninja are on `PATH`.

Binaries are placed in `_build/<OS>/debug/` (e.g. `_build/Linux/debug/`,
`_build/Windows/debug/`).

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
./_build/Linux/debug/sil-kit-android-bridge --vhal-uri unix:///tmp/vsock_3_1002/vm.vsock_9300 --silkit-uri localhost:8501
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
ctest --preset debug --output-on-failure
```

`grpc-client-test` is a manual developer tool (not a CTest case) that requires a
running VHAL gRPC server:

```sh
./_build/Linux/debug/test/grpc-client-test /tmp/vsock_1_1000/vm.vsock
```

## Examples

[examples/vector-canoe-se/](examples/vector-canoe-se/) contains a CANoe
Server Edition test configuration that exercises the adapter from a CANoe test
bench.

## Packaging

```sh
cmake --preset debug 
cmake --build --preset debug --target package
```

This produces inside `_build/Linux/debug/`:
- `sil-kit-android-bridge-<version>-Linux-x86_64.zip` — portable archive (`bin/sil-kit-android-bridge`)
- `sil-kit-android-bridge-<version>-Linux-x86_64.deb` — Debian/Ubuntu package (`/usr/bin/sil-kit-android-bridge`)

Install the `.deb` with:

```sh
sudo dpkg -i sil-kit-android-bridge-*.deb
```

On Windows a ZIP is produced instead, inside `_build/Windows/debug/`:
`sil-kit-android-bridge-<version>-Windows-x86_64.zip`, containing
`bin/sil-kit-android-bridge.exe`, `bin/silkit-client.exe` and `bin/SilKit.dll`.
Unlike the Debian package, which resolves libsilkit through `dpkg-shlibdeps`,
the archive is self-contained.

## License

Code authored by Vector Informatik GmbH is licensed under the MIT License
(see [LICENSE.md](LICENSE.md) and the `SPDX-License-Identifier: MIT` headers in each source file).

This repository also bundles third-party code under other licenses -- notably the vendored
AOSP Vehicle HAL (Apache-2.0) and spdlog/fmt (MIT). The project is therefore an aggregate,
not a pure-MIT work; the bundled components retain their own licenses. See
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for the complete list and attributions.
