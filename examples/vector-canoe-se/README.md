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
| `VhalSimple.vcdl` | The interface contract. Declares namespace `VhalSimple`, the `IVhalSimple` interface with the ten property methods plus `GetApiVersion`, and the object `properties`. |
| `VhalUtils.cin` | CAPL helpers wrapping each method in an async call plus `testWaitForAnswer`: `VhalGetInt32` / `VhalSetInt32` and the same pair for `Int64`, `Float`, `String` and `Bytes`, plus `VhalGetApiVersion`. |
| `VhalTest.can` | Two CAPL test cases. `CheckEvBatteryProperties` drives `EV_BATTERY_LEVEL` through a ramp; `CheckAllPropertyTypes` checks the API version and then, for each of the five types, reads the seeded value (step `x.1`) and does a write/read-back (step `x.2`), so one run shows exactly which method family fails. |
| `venvironment.yaml` | Virtual environment: binds `VhalSimple.vcdl` and sets the SIL Kit participant name to `canoe-vhal-test`. |
| `VhalTest.vtestunit.yaml` | Test unit: `VhalTest.can` plus the test tree. |
| `VhalTest.vtesttree.yaml` | Test tree: the single CAPL test case to execute. |
| `run_canoe_se.sh` | Builds the environment and test unit, then runs CANoe SE against them. |
| `get_vhal_proxy_socket.sh` | Prints the unix socket of a running `vhal_proxy_server`, for use as `--vhal-uri`. |

## How it matches the bridge

`VhalSimple.vcdl` and [`source/VhalSimpleApi.hpp`](../../source/VhalSimpleApi.hpp)
are the two halves of one contract, and they have to agree on three things or
nothing matches:

- **Function name** -- the bare method name (`GetInt32`, not
  `VhalSimple::properties.GetInt32`) is the SIL Kit matching key.
- **Labels** -- `VirtualNetwork=Default`, `Namespace=VhalSimple`,
  `Instance=properties`, all `Mandatory`.
- **Wire format** -- the SIL Kit SerDes encoding, guarded by
  `test/Test_VhalSimpleApi.cpp`.

A mismatch never fails loudly: SIL Kit simply does not pair the client with the
server, and every call returns `ServerNotReachable`. That is what
`GetApiVersion` is for, and why `CheckEvBatteryProperties` calls it first.

The adapter defaults to exactly the values `sil-adapter-builder` generates from
this VCDL, so a CANoe participant built against it matches with no extra
configuration. `sil-adapter-builder` is the Vector tool that turns a `.vcdl`
binding into the SIL Kit participant configuration; it ships with CANoe and is
not part of this repository.

`test/ITest_VhalSilKitAdapter.cpp` stands in for this whole directory in CI: it
fakes the CANoe side with a plain SIL Kit participant using the same function
names and labels, so the contract is regression-tested without CANoe. The
`silkit-client` tool does the same thing interactively.

## What `--mock` serves

`--mock` needs no Android device. It seeds every VhalSimple type with a
distinctive value, so a wrong read looks wrong instead of looking like a
plausible zero. `CheckAllPropertyTypes` asserts against exactly these:

| Type | Property | Seeded value |
|------|----------|--------------|
| `int32` | `INFO_MODEL_YEAR` | `2026` |
| `int32` | `EV_CHARGE_TIME_REMAINING` | `1234` |
| `int32` | `EV_CHARGE_STATE` | `3` |
| `int32` | `GEAR_SELECTION` | `4` |
| `int32` (AOSP BOOLEAN) | `EV_CHARGE_PORT_CONNECTED` | `1` |
| `int32` (AOSP BOOLEAN) | `NIGHT_MODE` | `0` |
| `int64` | `ANDROID_EPOCH_TIME` | `1767225600000` |
| `float` | `EV_BATTERY_LEVEL` | `88.5` |
| `float` | `RANGE_REMAINING` | `412.5` |
| `float` | `EV_CURRENT_BATTERY_CAPACITY` | `64000.0` |
| `float` | `INSTANTANEOUS_EV_EFFICIENCY` | `6.25` |
| `float` | `PERF_VEHICLE_SPEED` | `27.75` |
| `float` | `PERF_ODOMETER` | `123456.75` |
| `string` | `INFO_VIN` | `WVWZZZ1JZXW000001` |
| `string` | `INFO_MAKE` | `Vector` |
| `string` | `INFO_MODEL` | `SIL Kit Android Bridge` |
| `bytes` | `STORAGE_ENCRYPTION_BINDING_SEED` | `DE AD BE EF` |

The seed lives in [`source/VhalMockClient.hpp`](../../source/VhalMockClient.hpp);
add a property there and it is immediately reachable from CANoe.

**Use the method family that matches the AOSP type.** The mock declares each
value with the type AOSP declares for that property id, exactly as a device would,
so `GetFloat("EV_BATTERY_LEVEL")` works and `GetInt32` on it returns `0` and logs
a type mismatch. Two consequences worth knowing:

- A property the mock does not seed answers with `0` / `""` / empty, not an error -
  the VCDL methods have no status out-parameter. The bridge log says which it was.
- A write through the wrong family is *refused* by the VHAL, but `Set<Type>` is a
  `void` method, so CANoe still sees `Success`. The stored value is unchanged and
  the bridge logs `expects FLOAT, not int32`. Check the bridge log, not the call
  status.

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

- **`GetBytes` is confirmed; `SetBytes` is not.** Reading
  `STORAGE_ENCRYPTION_BINDING_SEED` from CANoe against `--mock` renders the seeded
  `DE AD BE EF` correctly in the trace window. That settles the part that was
  hardest to reason about: `sil-adapter-builder` and SIL Kit SerDes agree on how
  the vCDL `bytes` type goes on the wire - a `uint32` length prefix followed by
  raw bytes, byte-identical to a `string`. (For the record, `bytes` is a primitive
  sequence type; there is no `[]` suffix form in the language, so neither
  `uint8[]` nor `char[]` parses.) What is still unexercised is the write
  direction - whether CAPL marshals a char array into a `bytes` parameter the way
  `CallAsync` expects. That is step `6.2` of `CheckAllPropertyTypes`. If it fails,
  delete the two `Bytes` methods from the vCDL and the two wrappers from
  `VhalUtils.cin`: the bridge serves methods no vCDL declares without complaining,
  so the other nine are unaffected.
- The CAPL helpers for `string` and `bytes` copy the result into a caller-supplied
  buffer with `strncpy`; size the buffer for the properties you read. For `bytes`
  that also means a payload containing `0x00` is cut short - the helpers carry
  bytes in a char array, which is NUL-terminated. `CheckAllPropertyTypes`
  therefore round-trips a printable payload (`CAFE`). A binary-safe helper would
  need an explicit length alongside the buffer; the bridge itself has no such
  limit, it is purely the CAPL side.
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
