# sil-kit-android-bridge

`sil-kit-android-bridge` is a **SIL Kit Extension** that bridges an Android VHAL
(Vehicle Hardware Abstraction Layer) gRPC server to a
[SIL Kit](https://github.com/vectorgrp/sil-kit) simulation network, enabling
co-simulation between Android Automotive environments and CANoe/SIL Kit-based
test benches.

> [!WARNING]
> **This is a public preview. We want your feedback.**
>
> The [VhalSimple API](#the-vhalsimple-api) is complete for every type the bridge
> models, versioned, and covered by tests, so it is something you can build
> against. The project around it is early: it has not been hardened, security
> reviewed, or run in production. While it is on `0.x`, the CLI, the property
> model and the RPC surface may change without notice or a deprecation period. Do
> not expose it to an untrusted network.
>
> We are publishing it early on purpose, because the useful next step is finding
> out what people actually need from it. **Please
> [open an issue](https://github.com/vectorgrp/sil-kit-android-bridge/issues)**:
> bug reports, "this does not build on my machine", missing property types, and
> use cases we have not thought of are all equally welcome. Tell us what you are
> trying to do and we will tell you honestly whether it fits.
>
> Known limitations today, so you can judge the fit before investing time:
>
> - **Polling only.** The VHAL to SIL Kit *push* direction is not implemented.
>   `AddPropertyChangedHandler` returns `0` and does nothing, so a CANoe test has
>   to read values rather than being notified of changes.
> - **The CANoe surface is deliberately small.** Scalar get/set only. That is a
>   scope decision, not a gap - see [Scope](#scope) for what is out and why.
> - **Errors look like data on the wire.** An unknown property, a read the VHAL
>   refused, and a type mismatch all answer `0` with `RpcCallStatus::Success`,
>   because the VCDL methods carry no status out-parameter. From a `Get<Type>`
>   response alone a caller still cannot tell "battery at 0%" from "no such
>   property" - but it no longer has to guess: `GetPropertyStatus` answers
>   whether the property exists, whether it is readable or writable, and which
>   accessor it needs. What the wire cannot say, the bridge log also says: all
>   three cases are logged, in both the read and the write direction, and a
>   non-OK VHAL `StatusCode` is no longer converted into a plausible-looking
>   number.
> - **No automated test drives a real device.** CI covers the gRPC path against a
>   fake VHAL, including the error, health and result-handling paths and which
>   protobuf field each write lands in, and exercises the whole chain (SIL Kit ->
>   bridge -> gRPC -> VHAL) end to end. What it cannot do is run an Android image:
>   that part is validated by hand against Cuttlefish, most recently for the 0.4
>   release. Expect rough edges against real hardware, and please report them.

## The VhalSimple API

The bridge joins a SIL Kit simulation as a participant (default name
`VhalBridge`) and serves the **VhalSimple API**: a small, versioned,
request/response surface for reading and writing vehicle properties, forwarding
each call to the VHAL.

| Type | Read | Write |
|------|------|-------|
| `int32` | `int32 GetInt32(string name)` | `void SetInt32(string name, int32 value)` |
| `int64` | `int64 GetInt64(string name)` | `void SetInt64(string name, int64 value)` |
| `float` | `float GetFloat(string name)` | `void SetFloat(string name, float value)` |
| `string` | `string GetString(string name)` | `void SetString(string name, string value)` |
| `bytes` | `bytes GetBytes(string name)` | `void SetBytes(string name, bytes value)` |

plus `int32 GetApiVersion()`, which reports the contract version (currently `1`)
so a CANoe configuration can check it speaks the same dialect, and
`int32 GetPropertyStatus(string name)`, which answers what the bridge knows
about a property without reading its value. AOSP `BOOLEAN` properties travel
over the `int32` methods as 0/1.

`GetPropertyStatus` returns a bitfield:

| Bits | Meaning |
|------|---------|
| `0x01` | the VHAL configures this property |
| `0x02` | readable |
| `0x04` | writable |
| `0x08` | a known AOSP property name, answered without asking the VHAL |
| bits 8-11 | which accessor to use: `0` none, `1` int32, `2` int64, `3` float, `4` string, `5` bytes |
| negative | the bridge could not reach the VHAL; the lower bits still hold what was answerable offline |

`0` means the name is unknown to the bridge entirely. A real AOSP name that this
vehicle does not implement answers `0x08` plus its type - non-zero with `0x01`
clear - which is what separates a missing feature from a typo. Test
`status & 0x01`, never `status != 0`. Bits 4-7 and 12-30 are reserved, so mask
what you need and a later addition stays additive.

Properties are addressed by their AOSP name (`EV_BATTERY_LEVEL`,
`RANGE_REMAINING`, ...); the name-to-id mapping is generated from AOSP's
`VehicleProperty.aidl` into `source/VhalProperty.hpp`.

### Scope

The API covers **every type the bridge models internally**. AOSP's `INT32_VEC`,
`INT64_VEC`, `FLOAT_VEC` and `MIXED` properties are out of scope by design:
`CarProperty` has no representation for them, so supporting them is a data-model
change rather than a method addition. That is the line, and it is why the surface
is called *simple* rather than *incomplete*.

For the record, the CANoe side would not be the obstacle if we did add them - vCDL
has `list<int32>` for a dynamic sequence and `array<uint8, N>` for a fixed one (a
`typedef` is the attested way to use a sequence in return position). The blocker
is `CarProperty::Value`. If you need vector properties, say so in an issue and
that is the conversation to have.

The API is also request/response only - there is no change notification. See the
first bullet of the warning above.

### The contract

[source/VhalSimpleApi.hpp](source/VhalSimpleApi.hpp) and
[examples/vector-canoe-se/VhalSimple.vcdl](examples/vector-canoe-se/VhalSimple.vcdl)
are the two halves of one contract; the C++ header holds the identity constants,
the method names and the wire format in one place, and everything else in the
repository reads them from there.

Each method is matched by SIL Kit on the **bare method name** plus three
mandatory labels: `VirtualNetwork=Default`, `Namespace=VhalSimple`,
`Instance=properties`. Those are the values `sil-adapter-builder` generates from
the VCDL, so a CANoe participant built against it matches with no extra
configuration. Override them with `--namespace`, `--instance` and
`--virtual-network` to serve a different interface definition.

A mismatch never fails loudly - SIL Kit simply does not pair client to server and
every call returns `ServerNotReachable` - which is what `GetApiVersion` is for.

## Build

```sh
cmake --preset relwithdebinfo
cmake --build --preset relwithdebinfo
```

On Windows, run this from a Visual Studio developer command prompt (or after
`vcvars64.bat`) so that the compiler and Ninja are on `PATH`. See
[Prerequisites](#prerequisites) for what has to be installed first.

Binaries are placed in `_build/<OS>/<preset>/`, e.g.
`_build/Linux/relwithdebinfo/`. The presets are `debug`, `release` and
`relwithdebinfo`; `debug` is Linux-only, for the reason given under
[No debug preset on Windows](#no-debug-preset-on-windows).

The primary target platform is Ubuntu; the build depends only on publicly
available packages. Windows is built and tested in CI as well.

## Quick start

The fastest way to see the bridge work needs no Android device at all: `--mock`
serves an in-memory VHAL seeded with one property per type - `INFO_MODEL_YEAR`
(int32), `ANDROID_EPOCH_TIME` (int64), `EV_BATTERY_LEVEL` (float), `INFO_VIN`
(string), `STORAGE_ENCRYPTION_BINDING_SEED` (bytes) and a dozen more. Each value
is distinctive, so a wrong read looks wrong rather than looking like a plausible
zero. Every property carries the type AOSP declares for it, so the mock behaves
like a device: use the method family that matches. The full seed is listed in
[examples/vector-canoe-se/README.md](examples/vector-canoe-se/README.md).

```sh
# 1. Start a SIL Kit registry (from your SIL Kit installation)
sil-kit-registry --listen-uri silkit://localhost:8500 &

# 2. Start the bridge against the built-in mock VHAL
./_build/Linux/relwithdebinfo/sil-kit-android-bridge --mock

# 3. In another terminal, drive it with the bundled client tool
./_build/Linux/relwithdebinfo/silkit-client
```

`silkit-client` reads `PROPERTY=value` lines from stdin and prints each
round-trip:

```
Connecting participant 'silkit-client' to registry 'silkit://localhost:8500' (Namespace=VhalSimple, Instance=properties, VirtualNetwork=Default)
Connected to the VhalSimple API v1.
No assignments on the command line; reading PROPERTY=value lines from stdin (Ctrl+D / Ctrl+Z to end).
EV_BATTERY_LEVEL=88.5
SET  EV_BATTERY_LEVEL = 88.5 (float) -> Success
GET  EV_BATTERY_LEVEL = 88.5 (float)
ANDROID_EPOCH_TIME=1767225600000
SET  ANDROID_EPOCH_TIME = 1767225600000 (int64) -> Success
GET  ANDROID_EPOCH_TIME = 1767225600000 (int64)
INFO_VIN=WVWZZZ1JZXW000001
SET  INFO_VIN = 'WVWZZZ1JZXW000001' (string) -> Success
GET  INFO_VIN = WVWZZZ1JZXW000001 (string)
```

The method family is chosen from the AOSP property-id type bits, so a plain
`NAME=value` line picks the right one. Byte properties are written and shown as
hex.

### Against a real Android VHAL

Replace step 2. Find the URI of the VHAL proxy process first -- here it is
serving on a unix domain socket:

```sh
$ pgrep -al vhal
1234 /var/tmp/cvd/.../artifacts/host_tools/bin/vhal_proxy_server /var/tmp/cvd/.../artifacts/host_tools/etc/automotive/vhalconfig ...:9300 unix:///tmp/vsock_3_1002/vm.vsock_9300
```

```sh
./_build/Linux/relwithdebinfo/sil-kit-android-bridge \
    --vhal-uri unix:///tmp/vsock_3_1002/vm.vsock_9300
```

Refer to the
[SIL Kit documentation](https://vectorgrp.github.io/sil-kit-docs/for-users/users.html#quick-start)
for registry setup beyond the single-host case above.

## Usage

```
sil-kit-android-bridge [OPTIONS]

  -u, --silkit-uri <uri>       SIL Kit registry URI (default: silkit://localhost:8500)
  -V, --vhal-uri   <uri>       VHAL gRPC server URI (required unless --mock)
  -m, --mock                   Use built-in mock VHAL (no Android device needed)
  -n, --name       <name>      SIL Kit participant name (default: VhalBridge)
  -s, --synchronized           Enable coordinated lifecycle mode
  -l, --log        <level>     Log level: Trace|Debug|Info|Warn|Error|Critical|Off (default: Info)
  -c, --configuration <file>   Path to SIL Kit participant config YAML
  -N, --namespace  <name>      VCDL namespace to serve (default: VhalSimple)
  -i, --instance   <name>      VCDL interface object to serve (default: properties)
      --virtual-network <name> VirtualNetwork matching label (default: Default)
  -v, --version                Print version information
  -h, --help                   Print this help
```

`--namespace`, `--instance` and `--virtual-network` are the three labels SIL Kit
matches on; the defaults pair with the stock `VhalSimple.vcdl`. `silkit-client`
takes the same three options, so both sides can be pointed at a different
interface definition together. (`--virtual-network` has no short form on the
bridge because `-V` is already `--vhal-uri`.)

`--log` and `--configuration` are mutually exclusive: the configuration file
carries its own logging setup.

## Testing

```sh
ctest --preset relwithdebinfo --output-on-failure
```

The suite is self-contained: it starts its own SIL Kit registry and its own fake
VHAL, so it needs neither a running `sil-kit-registry` nor an Android device.
What each target covers, how the test scaffolding fits together and what is
knowingly left uncovered are documented for contributors in
[AGENTS.md](AGENTS.md#testing).

## Examples

[examples/vector-canoe-se/](examples/vector-canoe-se/) contains a CANoe Server
Edition test configuration that exercises the bridge from a CANoe test bench,
with its own [README](examples/vector-canoe-se/README.md). It needs commercial
Vector tooling; everything else in this repository does not.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for the DCO sign-off requirement and the
coding conventions, [AGENTS.md](AGENTS.md) for the engineering rules the codebase
follows, and [SECURITY.md](SECURITY.md) for how to report a vulnerability
privately. 

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

Relevant cache variables (see [cmake/fetch_conan.cmake](cmake/fetch_conan.cmake)):

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
[cmake/conan/licenses_deployer.py](cmake/conan/licenses_deployer.py); the
vendored components (spdlog, fmt, the AOSP VHAL) and SIL Kit itself are installed
by [cmake/Notices.cmake](cmake/Notices.cmake).

## Releasing

Maintainer procedure. The project version lives in
[cmake/Version.cmake](cmake/Version.cmake) and nowhere else; CMake, Conan and CI
all read it from there.

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
