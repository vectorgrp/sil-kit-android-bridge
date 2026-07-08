# sil-kit-android-bridge

`sil-kit-android-bridge` is a **SIL Kit Extension** that bridges an Android VHAL
(Vehicle Hardware Abstraction Layer) gRPC server to a
[SIL Kit](https://github.com/vectorgrp/sil-kit) simulation network, enabling
co-simulation between Android Automotive environments and CANoe/SIL Kit-based
test benches.

## Prerequisites

| Dependency | How to install |
|------------|----------------|
| **SIL Kit** | `sudo dpkg -i libsilkit5_<version>-1ubuntu1_amd64.deb libsilkit-dev_<version>-1ubuntu1_amd64.deb silkit-utils_<version>-1ubuntu1_amd64.deb` from [GitHub Releases](https://github.com/vectorgrp/sil-kit/releases) |
| **gRPC + Protobuf** | `sudo apt install libgrpc++-dev libprotobuf-dev protobuf-compiler-grpc` |
| **CMake** | >= 3.21 |

## Build

```sh
cmake --preset debug
cmake --build --preset debug
```

Binaries are placed in `_build/<OS>/debug/` (e.g. `_build/Linux/debug/`).

The supported target platform is Ubuntu; the build depends only on publicly available
packages.

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

### Quick start with mock VHAL

```sh
# Start a SIL Kit registry (from your SIL Kit installation)
sil-kit-registry &

# Run the adapter in mock mode
./_build/Linux/debug/sil-kit-android-bridge --mock
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

## License

Code authored by Vector Informatik GmbH is licensed under the MIT License
(see [LICENSE.md](LICENSE.md) and the `SPDX-License-Identifier: MIT` headers in each source file).

This repository also bundles third-party code under other licenses -- notably the vendored
AOSP Vehicle HAL (Apache-2.0) and spdlog/fmt (MIT). The project is therefore an aggregate,
not a pure-MIT work; the bundled components retain their own licenses. See
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for the complete list and attributions.
