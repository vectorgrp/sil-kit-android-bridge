# sil-kit-android-bridge

`sil-kit-android-bridge` is a **SIL Kit Extension** that bridges an Android VHAL
(Vehicle Hardware Abstraction Layer) gRPC server to a
[SIL Kit](https://github.com/vectorgrp/sil-kit) simulation network, enabling
co-simulation between Android Automotive environments and CANoe/SIL Kit-based
test benches.

The bridge joins the simulation as a SIL Kit participant and serves the
[VhalSimple API](docs/vhal-simple-api.md): get and set methods per value type
(`int32`, `int64`, `float`, `string`, `bytes`) that address vehicle properties
by AOSP name, such as `EV_BATTERY_LEVEL`, and forward each call to the VHAL.

> [!WARNING]
> **Public preview - we want your feedback.**
>
> The [VhalSimple API](docs/vhal-simple-api.md) is complete for every type the
> bridge models and covered by tests, so it is something you can build against.
> The project around it is early: not hardened and not security reviewed. While
> it is on `0.x` the CLI and the RPC surface may change without notice. Do not
> expose it to an untrusted network.
>
> Today it is **polling only** (no change notification), and the CANoe surface
> is **scalar get/set only** by design.
>
> **Please [open an issue](https://github.com/vectorgrp/sil-kit-android-bridge/issues)**:
> bug reports, "this does not build on my machine", missing property types, and
> use cases we have not thought of are all equally welcome.

## Quick start

Download a package from the
[releases](https://github.com/vectorgrp/sil-kit-android-bridge/releases): the
Linux `.deb` installs `sil-kit-android-bridge` and `silkit-client` to
`/usr/bin`, and the Linux and Windows zips have both in `bin/`. You also need
[SIL Kit 5](https://github.com/vectorgrp/sil-kit/releases) for the registry,
and on Linux for `libSilKit`. To build from source instead, see
[docs/building.md](docs/building.md); the binaries then land in
`_build/<OS>/relwithdebinfo/`.

The fastest way to see the bridge work needs no Android device at all: `--mock`
serves an in-memory VHAL seeded with at least one property per type. Each value
is distinctive, so a wrong read looks wrong rather than looking like a plausible
zero, and every property carries the type AOSP declares for it, so the mock
behaves like a device.

```sh
# 1. Start a SIL Kit registry (from your SIL Kit installation)
sil-kit-registry --listen-uri silkit://localhost:8500 &

# 2. Start the bridge against the built-in mock VHAL
sil-kit-android-bridge --mock

# 3. In another terminal, drive it with the bundled client tool
silkit-client
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
INFO_VIN=SILKITVHALBRIDGE1
SET  INFO_VIN = 'SILKITVHALBRIDGE1' (string) -> Success
GET  INFO_VIN = SILKITVHALBRIDGE1 (string)
```

The method family is chosen from the AOSP property-id type bits, so a plain
`NAME=value` line picks the right one. A bare `NAME` reads it, and `?NAME`
reports its status. Byte properties are written and shown as hex.

### Against a real Android VHAL

Replace step 2. Find the URI of the VHAL proxy process first -- here it is
serving on a unix domain socket:

```sh
$ pgrep -al vhal
1234 /var/tmp/cvd/.../artifacts/host_tools/bin/vhal_proxy_server /var/tmp/cvd/.../artifacts/host_tools/etc/automotive/vhalconfig ...:9300 unix:///tmp/vsock_3_1002/vm.vsock_9300
```

```sh
sil-kit-android-bridge --vhal-uri unix:///tmp/vsock_3_1002/vm.vsock_9300
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

## Examples

[examples/vector-canoe-se/](examples/vector-canoe-se/) contains a CANoe Server
Edition test configuration that exercises the bridge from a CANoe test bench,
with its own [README](examples/vector-canoe-se/README.md). It needs commercial
Vector tooling; everything else in this repository does not.

## Documentation

- [docs/vhal-simple-api.md](docs/vhal-simple-api.md) - the API in full: every
  method, the `GetPropertyStatus` struct, scope, and the wire contract
- [docs/building.md](docs/building.md) - prerequisites, Conan, packaging and
  releasing
- [docs/testing.md](docs/testing.md) - test rules and what each target covers
- [docs/](docs/) - index of the above

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for the DCO sign-off and the coding
conventions, and [SECURITY.md](SECURITY.md) for how to report a vulnerability
privately.

## License

Code authored by Vector Informatik GmbH is licensed under the MIT License
(see [LICENSE.md](LICENSE.md) and the `SPDX-License-Identifier: MIT` headers in each source file).

This repository also bundles third-party code under other licenses -- notably the vendored
AOSP Vehicle HAL (Apache-2.0) and spdlog/fmt (MIT). The project is therefore an aggregate,
not a pure-MIT work; the bundled components retain their own licenses. See
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for the complete list and attributions.

Built packages carry the corresponding license texts in `docs/licenses/`, so a
distributed archive is complete on its own.
