<!-- SPDX-FileCopyrightText: 2026 Vector Informatik GmbH -->
<!-- SPDX-License-Identifier: MIT -->

# CANoe Server Edition example

A CANoe Server Edition (CANoe SE) test configuration that drives
`sil-kit-android-bridge` from a CANoe test bench, over the same SIL Kit RPC
surface the bridge exposes to any other participant.

This example needs commercial Vector tooling (CANoe SE and its command line
tools). Everything in the repository outside this directory builds and runs
without it -- use `sil-kit-android-bridge --mock` and the `silkit-client` tool if
you do not have CANoe.

## What is here

| File | Role |
|------|------|
| `VhalProperties.vcdl` | The interface contract. Declares namespace `VhalSilkit`, the `IVhalToSilkit` interface with the four consumed methods, and the object `vhalProperties`. |
| `VhalUtils.cin` | CAPL helpers wrapping each method in an async call plus `testWaitForAnswer` (`VhalSetProperty`, `VhalGetProperty`, `VhalSetPropertyFloat`, `VhalGetPropertyFloat`). |
| `VhalTest.can` | The CAPL test case `CheckEvBatteryProperties`: seeds a few EV properties, drives `EV_BATTERY_LEVEL` through a ramp, and reads values back. |
| `venvironment.yaml` | Virtual environment: binds `VhalProperties.vcdl` and sets the SIL Kit participant name to `canoe-vhal-test`. |
| `VhalTest.vtestunit.yaml` | Test unit: `VhalTest.can` plus the test tree. |
| `VhalTest.vtesttree.yaml` | Test tree: the single CAPL test case to execute. |
| `run_canoe_se.sh` | Builds the environment and test unit, then runs CANoe SE against them. |
| `get_vhal_proxy_socket.sh` | Prints the unix socket of a running `vhal_proxy_server`, for use as `--vhal-uri`. |

## How it matches the bridge

`VhalProperties.vcdl` and `source/VhalSilKitAdapter.hpp` describe the same four
RPC endpoints, and they have to agree on three things or nothing matches:

- **Function name** -- the bare method name (`GetProperty`, not
  `VhalSilkit::vhalProperties.GetProperty`) is the SIL Kit matching key.
- **Labels** -- `VirtualNetwork=Default`, `Namespace=VhalSilkit`,
  `Instance=vhalProperties`, all `Mandatory`.
- **Wire format** -- the SIL Kit SerDes encoding, guarded by
  `test/Test_VhalSilKitSerdes.cpp`.

The adapter defaults to exactly the values `sil-adapter-builder` generates from
this VCDL, so a CANoe participant built against it matches with no extra
configuration. `sil-adapter-builder` is the Vector tool that turns a `.vcdl`
binding into the SIL Kit participant configuration; it ships with CANoe and is
not part of this repository.

`test/ITest_VhalSilKitAdapter.cpp` stands in for this whole directory in CI: it
fakes the CANoe side with a plain SIL Kit participant using the same function
names and labels, so the contract is regression-tested without CANoe.

## Running it

1. Start a SIL Kit registry and the bridge (`--mock` works, and needs no Android
   device):

   ```sh
   sil-kit-registry --listen-uri silkit://localhost:8500 &
   ./_build/Linux/relwithdebinfo/sil-kit-android-bridge --mock
   ```

   Against a real Android VHAL, get the socket with `./get_vhal_proxy_socket.sh`
   and pass it as `--vhal-uri`.

2. Run the test bench:

   ```sh
   ./run_canoe_se.sh
   ```

   This runs `environment-make` over `venvironment.yaml`, then `test-unit-make`
   over `VhalTest.vtestunit.yaml`, then `canoe-se` on the result, writing
   everything to `_OUT/`. All three must be on `PATH`.

## Caveats

- The minimum CANoe SE version is not pinned here. The configuration files
  declare their own schema versions (`venvironment.yaml` 4.0.0, the test unit and
  test tree 2.0.0), which is the only version information this directory carries.
  We have not verified which CANoe SE releases accept them -- if yours does not,
  please open an issue and tell us the version.
- `run_canoe_se.sh` assumes a Linux CANoe SE installation and `-A Linux64`.
- The bridge must be running and connected to the registry before step 2; the
  CAPL helpers use a 100 ms `testWaitForAnswer` (`VhalUtils.cin`), so an
  unmatched RPC surfaces as a failed step rather than a hang.
- `CheckEvBatteryProperties` drives `EV_BATTERY_LEVEL` with raw magnitudes
  (10000 to 100000) rather than AOSP's documented unit for that property. It is a
  connectivity demonstration, not a model of realistic vehicle values.
