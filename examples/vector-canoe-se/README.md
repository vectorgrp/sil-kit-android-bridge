<!-- SPDX-FileCopyrightText: 2026 Vector Informatik GmbH -->
<!-- SPDX-License-Identifier: MIT -->

# CANoe Server Edition example

A CANoe Server Edition (CANoe SE) test configuration that drives
`sil-kit-android-bridge` from a CANoe test bench, over the same SIL Kit RPC
surface the bridge exposes to any other participant.

This example needs commercial Vector tooling (CANoe SE and its command line
tools). It is tested with CANoe SE 20.2.89 on Ubuntu 24.04. Everything in the
repository outside this directory builds and runs without it -- use
`sil-kit-android-bridge --mock` and the `sil-kit-android-bridge-client` tool if
you do not have CANoe.

## What is here

| File | Role |
|------|------|
| `VhalSimple.vcdl` | The interface contract. Declares namespace `VhalSimple`, the `IVhalSimple` interface with the ten property methods plus `GetApiVersion` and `GetPropertyStatus`, and the object `properties`. |
| `VhalUtils.cin` | CAPL helpers wrapping each method in an async call plus `testWaitForAnswer`: `VhalGetInt32` / `VhalSetInt32` and the same pair for `Int64`, `Float`, `String` and `Bytes`, plus `VhalGetApiVersion`, `VhalGetPropertyStatus` and the `kVhalType*` constants for its `valueType`. |
| `VhalTest.can` | Three CAPL test cases. `CheckEvBatteryProperties` drives `EV_BATTERY_LEVEL` through a ramp. `CheckAllPropertyTypes` checks the API version, a read and a write round trip for each of the five types, and `GetPropertyStatus`; it asserts no property values, so it passes against `--mock` and against a real device. `CheckMockSeedValues` asserts the exact `--mock` seed values and is therefore not in the default test tree. |
| `venvironment.yaml` | Virtual environment: binds `VhalSimple.vcdl` and sets the SIL Kit participant name to `canoe-vhal-test`. |
| `VhalTest.vtestunit.yaml` | Test unit: `VhalTest.can` plus the test tree. |
| `VhalTest.vtesttree.yaml` | Test tree: `CheckEvBatteryProperties` and `CheckAllPropertyTypes`. |
| `run_canoe_se.sh` | Builds the environment and test unit, then runs CANoe SE against them. |
| `get_vhal_proxy_socket.sh` | Prints the unix socket of a running `vhal_proxy_server`, for use as `--vhal-uri`. |

### Test steps

The step IDs `CheckAllPropertyTypes` writes into the CANoe test report (the
first argument of `testStepPass` / `testStepFail` in `VhalTest.can`):

| Step | `CheckAllPropertyTypes` checks |
|------|--------------------------------|
| `0.1` | `GetApiVersion` answers with version 1 |
| `1.1` / `1.2` | `int32`: the read answers / a write round-trips (`EV_CHARGE_PORT_CONNECTED`) |
| `2.1` / `2.2` | `int64`, the same pair (`ANDROID_EPOCH_TIME`) |
| `3.1` / `3.2` | `float`, the same pair (`EV_BATTERY_LEVEL`) |
| `4.1` / `4.2` | `string`, the same pair (`INFO_VIN`) |
| `5.1` / `5.2` | `bytes`, the same pair (`STORAGE_ENCRYPTION_BINDING_SEED`) |
| `6.1` | an unknown property name answers with the `0` default |
| `7.1` / `7.2` | `GetPropertyStatus` for a real property / for a mistyped name |

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

`GetPropertyStatus` adds a fourth thing the two halves must agree on: the field
order of `struct VhalPropertyStatus`. A mismatch in a name, label or wire format
stops the call from matching at all, which is loud once you know to expect
`ServerNotReachable`. Field order is not a matching key: reorder it on one side
and the call succeeds and is misread.

| Field | Meaning |
|---|---|
| `exists` | the VHAL configures this property |
| `readable` / `writable` | the access the VHAL declares |
| `knownName` | the name is in the compiled-in AOSP table |
| `valueType` | which accessor to use: `0` none, `1` int32, `2` int64, `3` float, `4` string, `5` bytes |
| `queryFailed` | the bridge could not answer at all |

`knownName` is independent of `exists`, so a known name this vehicle does not
implement has `knownName` set and `exists` clear. To ask whether the vehicle
has a property, test `st.exists` alone: a non-zero `knownName` or `valueType`
does not imply it. Only the `valueType` codes are restated by hand in
`VhalUtils.cin`. The flags are struct fields, so they cannot drift.

## Running against a real device

`CheckAllPropertyTypes` passes against a live VHAL as well as `--mock`. It never
asserts what a property *contains*, only that each method family answers and
that a write is either reflected on read-back or leaves the value untouched. The
second case passes with a note, because a static or sensor-backed property being
read-only is a property of the vehicle, not a fault in the bridge.

Use `CheckMockSeedValues` (add it to `VhalTest.vtesttree.yaml`) only when the
bridge is running with `--mock`.

## What `--mock` serves

`--mock` needs no Android device. It seeds every VhalSimple type with a
distinctive value, so a wrong read looks wrong instead of looking like a
plausible zero. `CheckMockSeedValues` asserts one of them per type:

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
| `string` | `INFO_VIN` | `SILKITVHALBRIDGE1` |
| `string` | `INFO_MAKE` | `Vector` |
| `string` | `INFO_MODEL` | `SIL Kit Android Bridge` |
| `bytes` | `STORAGE_ENCRYPTION_BINDING_SEED` | `DE AD BE EF` |

The seed lives in [`source/VhalMockClient.hpp`](../../source/VhalMockClient.hpp);
add a property there and it is immediately reachable from CANoe.

### Running against --mock

Against `--mock`, `GetPropertyStatus` reports every seeded property above with
`exists`, `readable`, `writable` and `knownName` set plus its `valueType`,
because the mock hands back everything it stores and accepts a write to any of
it. A real VHAL reports whatever access its configuration table declares, so
`EV_BATTERY_LEVEL` is typically read-only there. A known AOSP name that is not
seeded answers only `knownName` and `valueType`, and a name outside the AOSP
table answers with every field clear.

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

Run the commands below from this directory.

1. Start a SIL Kit registry and the bridge (`--mock` works, and needs no Android
   device):

   ```sh
   sil-kit-registry --listen-uri silkit://localhost:8500 &
   ../../_build/Linux/relwithdebinfo/sil-kit-android-bridge --mock
   ```

   Against a real Android VHAL, get the socket with `./get_vhal_proxy_socket.sh`
   and pass it as `--vhal-uri`.

2. Run the test bench:

   ```sh
   ./run_canoe_se.sh
   ```

   This runs `environment-make` over `venvironment.yaml`, then `test-unit-make`
   over `VhalTest.vtestunit.yaml`, then `canoe-se` on the result, writing
   everything to `_OUT/`. All three must be on `PATH`. The script targets a
   Linux CANoe SE installation (`-A Linux64`).

Each CAPL helper waits up to `maxWait` (100 ms, in `VhalUtils.cin`) for an
answer, so a call the bridge does not answer fails its step instead of hanging.
