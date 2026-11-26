# Testing

```sh
ctest --preset relwithdebinfo --output-on-failure
```

The suite is self-contained: it starts its own SIL Kit registry and its own fake
VHAL, so it needs no `sil-kit-registry` process and no Android device. CI runs
it on Linux and Windows; a test that passes on only one of them is broken.

## Rules

- `Test_*` are unit tests: no registry, no server, no network. `ITest_*` are
  integration tests: they run against an in-process registry or fake VHAL and
  use only the public SIL Kit API.
- **Assert on logic, never on timing.** CI runners are loaded and slow. Wait on
  a condition (an event, a state query, a barrier). A timeout is only a hang
  guard, far beyond any plausible delay. To prove that something does *not*
  happen, order it after a positive case that established the same
  precondition.
- **Create every SIL Kit controller before `StartLifecycle()`.** A controller
  created later can miss service discovery and never match its peer, which
  shows up as a load-dependent flake. `SkParticipant::AddRpcClient()` throws
  once `Start()` has run.
- **A new test must be able to fail.** Revert the fix and watch it go red.

## Targets

| Target | Covers |
|--------|--------|
| `Test_VhalMockClient` | the in-memory VHAL: seeding, per-type get/set, wrong-type rejection |
| `Test_VhalSimpleApi` | identity constants against the vCDL, and the byte-exact CANoe wire format |
| `Test_VhalGrpcClient` | `VhalGrpcConvert` only: URI validation, both conversion directions, `StatusCode` gating |
| `ITest_VhalSilKitAdapter` | faked CANoe consumer -> adapter -> `VhalMockClient`, over a live registry |
| `ITest_VhalGrpcClient` | `VhalGrpcClient` -> fake VHAL: result alignment, health, streams, transport failures, which protobuf field each write lands in |
| `ITest_VhalBridgeGrpc` | the whole chain, thin: consumer -> SIL Kit -> adapter -> `VhalGrpcClient` -> fake VHAL |
| `PackagedBinaryHasNoDynamicVendoredDeps` | Linux only: the binaries link gRPC and Protobuf statically |

`grpc-client-test` and `sil-kit-android-bridge-client` are manual tools. They
are built but not registered with CTest.

## Test helpers

All in `test/`, header-only:

- `TestHarness.hpp`: `test::LiveRegistry` starts an in-process registry on
  `silkit://localhost:0`; `Uri()` returns the bound address. Each test binary
  gets its own, so targets run in parallel.
- `SkParticipant.hpp`: a SIL Kit participant playing the CANoe consumer.
  Open a fixture with `AwaitServerReachable()` as the discovery barrier.
- `CanoeWire.hpp`: the wire format as CANoe emits it. It is hand-written on
  purpose: encoding with the bridge's own `VhalSimpleApi.hpp` would pass on any
  self-consistent format, including a wrong one.
- `FakeVehicleServer.hpp`: `test::FakeVhal`, a programmable VHAL gRPC server on
  gRPC's in-process transport (no socket, no port). It can inject per-property
  `StatusCode`s, transport failures, reordered or dropped results, an unhealthy
  `CheckHealth` and a truncated config stream. `ReceivedSetRequests()` returns
  what arrived on the wire.

`VhalMockClient` lives in `source/`, because it is also the production `--mock`
mode. Changing it changes shipped behavior.

## Adding a test

Pick the layer, then copy that target's block from `test/CMakeLists.txt`. Link
only what the layer needs:

| Layer | Links |
|-------|-------|
| pure logic | `GTest::gtest_main`, `spdlog::spdlog` |
| protobuf translation | + `proto_lib` |
| gRPC with a server | + `proto_lib`, and the `VhalGrpcClient.cpp` / `VhalGrpcConvert.cpp` sources |
| SIL Kit | + `SilKit::SilKit`, and the `VhalSimpleApi.cpp` source |

Every target needs `skab_copy_runtime_dlls(<target>)` and
`gtest_discover_tests(<target>)`.

Two pitfalls:

- `gtest_discover_tests` runs the binary at build time to list the cases, so
  nothing may start a server or bind a port at static-initialization time.
  Fixtures are fine.
- A file that includes both `<grpcpp/grpcpp.h>` and the VHAL protos needs
  `#undef TRY_AGAIN` and `#undef ACCESS_DENIED`: winsock2 defines both as
  macros, and both are `StatusCode` values.
