# The VhalSimple API

The CANoe-facing surface of the bridge: a small request/response API for reading
and writing vehicle properties, forwarding each call to the VHAL.

## Methods

| Type | Read | Write |
|------|------|-------|
| `int32` | `int32 GetInt32(string name)` | `void SetInt32(string name, int32 value)` |
| `int64` | `int64 GetInt64(string name)` | `void SetInt64(string name, int64 value)` |
| `float` | `float GetFloat(string name)` | `void SetFloat(string name, float value)` |
| `string` | `string GetString(string name)` | `void SetString(string name, string value)` |
| `bytes` | `bytes GetBytes(string name)` | `void SetBytes(string name, bytes value)` |

Plus two methods that are not property accessors:

- `int32 GetApiVersion()` reports the contract version, so a CANoe configuration
  can check it speaks the same dialect. Bumped when a method changes or is
  removed, unchanged when one is added.
- `int32 GetPropertyStatus(string name)` reports what the bridge knows about a
  property without reading its value. See below.

AOSP `BOOLEAN` properties travel over the `int32` methods as 0/1.

Properties are addressed by their AOSP name (`EV_BATTERY_LEVEL`,
`RANGE_REMAINING`, ...); the name-to-id mapping is generated from AOSP's
`VehicleProperty.aidl` into [source/VhalProperty.hpp](../source/VhalProperty.hpp).

## Errors look like data

An unknown property, a read the VHAL refused, and a type mismatch all answer `0`
with `RpcCallStatus::Success`, because the VCDL methods carry no status
out-parameter. From a `Get<Type>` response alone a caller cannot tell "battery at
0%" from "no such property".

All three cases are logged by the bridge, in both directions, and a non-OK VHAL
`StatusCode` is never converted into a plausible-looking number.
`GetPropertyStatus` is how a caller asks instead of guessing.

## GetPropertyStatus

Returns a bitfield:

| Bits | Meaning |
|------|---------|
| `0x01` | the VHAL configures this property |
| `0x02` | readable |
| `0x04` | writable |
| `0x08` | a known AOSP property name, answered without asking the VHAL |
| bits 8-11 | which accessor to use: `0` none, `1` int32, `2` int64, `3` float, `4` string, `5` bytes |
| negative | the bridge could not answer; the lower bits still hold what was answerable offline |

`0` means the name is unknown to the bridge entirely. A real AOSP name that this
vehicle does not implement answers `0x08` plus its type - non-zero with `0x01`
clear - which is what separates a missing feature from a typo. So test
`status & 0x01`, never `status != 0`.

Bits 4-7 and 12-30 are reserved: mask what you need, and a later addition stays
additive.

Three limits worth knowing before relying on it:

- **It answers about the property, not about the area.** Access is the union
  across every area the VHAL declares, while `Get<Type>`/`Set<Type>` always
  address area `0` - the API has no area parameter. For a property whose areas
  exclude `0`, or whose areas differ in access, the status word is broader than
  what an accessor will actually manage.
- **It does not promise the read will succeed.** `0x02` means the VHAL declares
  the property readable, not that this particular read will return data; a VHAL
  may still refuse one with a non-OK `StatusCode`, which still arrives as `0`.
  The method answers existence and accessor choice.
- **`0x08` comes from the compiled-in AOSP table**, so it is available even with
  no VHAL at all, and it is *not* set for a vendor property the VHAL configures
  but AOSP has never heard of.

Against `--mock` every seeded property reports read/write, because that is what
the mock does; see
[the mock caveat](../examples/vector-canoe-se/README.md#running-against---mock).

## Scope

The API covers **every type the bridge models internally**. AOSP's `INT32_VEC`,
`INT64_VEC`, `FLOAT_VEC` and `MIXED` properties are out of scope by design:
`CarProperty` has no representation for them, so supporting them is a data-model
change rather than a method addition. That is the line, and it is why the surface
is called *simple* rather than *incomplete*.

For the record, the CANoe side would not be the obstacle if we did add them -
vCDL has `list<int32>` for a dynamic sequence and `array<uint8, N>` for a fixed
one (a `typedef` is the attested way to use a sequence in return position). The
blocker is `CarProperty::Value`. If you need vector properties, say so in an
issue and that is the conversation to have.

The API is also request/response only - there is no change notification.

## The contract

[source/VhalSimpleApi.hpp](../source/VhalSimpleApi.hpp) and
[examples/vector-canoe-se/VhalSimple.vcdl](../examples/vector-canoe-se/VhalSimple.vcdl)
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
