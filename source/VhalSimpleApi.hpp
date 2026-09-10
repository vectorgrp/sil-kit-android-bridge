// SPDX-FileCopyrightText: 2026 Vector Informatik GmbH
// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "silkit/util/Span.hpp"
#include "silkit/util/serdes/Deserializer.hpp"
#include "silkit/util/serdes/Serialization.hpp"
#include "silkit/util/serdes/Serializer.hpp"

// The VhalSimple API: the CANoe-facing surface of the bridge.
//
// This file is the C++ half of a two-sided contract. The other half is
// examples/vector-canoe-se/VhalSimple.vcdl, and the two must agree on the
// namespace, the instance name, the method names and the parameter types.
// Changing one without the other does not fail loudly - SIL Kit simply stops
// matching the client to the server, and every call returns ServerNotReachable.
//
// Scope. The API exposes scalar get/set for every type CarProperty can hold:
// int32 (which also carries AOSP BOOLEAN as 0/1), int64, float, string and
// bytes. AOSP's INT32_VEC, INT64_VEC, FLOAT_VEC and MIXED properties are out of
// scope by design - CarProperty has no representation for them, so supporting
// them is a data-model change rather than a method addition.
//
// Versioning. kApiVersion is served over the wire by GetApiVersion so a CANoe
// configuration can check it speaks the same dialect instead of failing as a
// silent non-match. Adding a method keeps the version; changing or removing one
// bumps it.
//
// Wire format. The bytes match sil-adapter-builder output exactly. The generated
// TypeLib serializer emits parameters as *bare* fields with no struct markers;
// the BeginStruct/EndStruct calls below are kept for clarity and are
// byte-equivalent, because SIL Kit's SerDes encodes BeginStruct as Align() and
// EndStruct as a no-op - both zero-width for these already byte-aligned fields.
// The layouts are therefore:
//
//   Get<Type>(string name) -> Type
//     request : string(name)
//     response: <Type>
//
//   Set<Type>(string name, Type value) -> void
//     request : string(name) . <Type>
//     response: (empty)
//
//   GetApiVersion() -> int32
//     request : (empty)
//     response: int32(32)
//
//   GetPropertyStatus(string name) -> int32
//     request : string(name)
//     response: int32(32), a bitfield - see PropertyStatus below
//
// where <Type> is int32(32), int64(64), float, a uint32-length-prefixed string,
// or a uint32-length-prefixed byte array.

namespace VhalSimpleApi {

// ---- Contract identity (mirrors VhalSimple.vcdl) ----

//! Bumped when a method changes or is removed; unchanged when one is added.
inline constexpr int32_t kApiVersion = 1;

inline constexpr auto kNamespace = "VhalSimple";
inline constexpr auto kInstance = "properties";
inline constexpr auto kVirtualNetwork = "Default";

//! The SIL Kit RPC function names. These are the matching keys: SIL Kit pairs a
//! client to a server on the *bare* method name plus the three mandatory labels,
//! never on the fully qualified controller name.
namespace Method {
inline constexpr auto kGetApiVersion = "GetApiVersion";

inline constexpr auto kGetInt32 = "GetInt32";
inline constexpr auto kSetInt32 = "SetInt32";
inline constexpr auto kGetInt64 = "GetInt64";
inline constexpr auto kSetInt64 = "SetInt64";
inline constexpr auto kGetFloat = "GetFloat";
inline constexpr auto kSetFloat = "SetFloat";
inline constexpr auto kGetString = "GetString";
inline constexpr auto kSetString = "SetString";
inline constexpr auto kGetBytes = "GetBytes";
inline constexpr auto kSetBytes = "SetBytes";

//! Introspection. Additive, so kApiVersion is unchanged.
inline constexpr auto kGetPropertyStatus = "GetPropertyStatus";
} // namespace Method

// ---- The GetPropertyStatus word ----
//
// Every Get<Type> answers a failure with a default value, so a caller cannot
// tell "battery at 0%" from "no such property". GetPropertyStatus is the way to
// ask instead of guessing, and it is one call rather than a status
// out-parameter on all ten accessors - which would have changed every existing
// signature and broken every CANoe configuration built against v1.
//
// The answer is a bitfield, not an enum. CANoe cannot include this header, so
// VhalUtils.cin restates these numbers by hand; Test_VhalSimpleApi pins them.
// A consumer must MASK the bits it needs and never compare the whole value, so
// that claiming a reserved bit later stays additive.

//! Which Get<Type>/Set<Type> pair addresses a property. Answered from the
//! compiled-in AOSP table, so it is available even for a property the vehicle
//! does not implement. This closes the silent-zero trap: GetInt32 on a float
//! property returns 0 and looks like data.
enum class TypeCode : int32_t
{
    //! Not representable in VhalSimple: INT32_VEC, FLOAT_VEC, MIXED and friends.
    //! See the Scope note above - these are a data-model gap, not a missing method.
    None = 0,
    Int32 = 1, //!< also carries AOSP BOOLEAN, as 0/1
    Int64 = 2,
    Float = 3,
    String = 4,
    Bytes = 5,
};

namespace PropertyStatus {

//! Nothing is known about this name: neither the VHAL's configuration table nor
//! the AOSP table has it. Note that the converse does not hold - see kKnownName.
inline constexpr int32_t kAbsent = 0;

inline constexpr int32_t kExists = 1 << 0;   //!< the VHAL configures this property
inline constexpr int32_t kReadable = 1 << 1; //!< its access is READ or READ_WRITE
inline constexpr int32_t kWritable = 1 << 2; //!< its access is WRITE or READ_WRITE

//! The name is in the AOSP VehicleProperty table. Independent of kExists: a name
//! this vehicle does not implement answers kKnownName with kExists clear, which
//! is what separates a typo from a missing feature. Callers test
//! `status & kExists`, never `status != kAbsent`.
inline constexpr int32_t kKnownName = 1 << 3;

// Bits 4-7 are reserved and always zero.

inline constexpr int32_t kTypeShift = 8;  //!< bits 8-11 hold a TypeCode
inline constexpr int32_t kTypeMask = 0xF; //!< mask after shifting, not before

// Bits 12-30 are reserved and always zero.

//! The bridge could not ask: the VHAL was unreachable or its configuration
//! stream broke. This is the sign bit, so a caller tests `status < 0`. The lower
//! bits still carry whatever was answerable offline (kKnownName and the type).
//!
//! Deliberately not kAbsent. Reporting an outage as "no such property" is the
//! same ambiguity this method exists to remove, one layer up.
inline constexpr int32_t kQueryFailed = INT32_MIN;

} // namespace PropertyStatus

// ---- Declarations ----

//! Copy a SIL Kit span into the buffer the Deserializer takes by value.
auto ToVector(SilKit::Util::Span<const uint8_t> span) -> std::vector<uint8_t>;

//! Request of every Get<Type> method: a single property name.
auto DeserializeGetRequest(SilKit::Util::Span<const uint8_t> data) -> std::string;

//! Response of every Set<Type> method, and the only empty payload in the API.
auto SerializeVoidResponse() -> std::vector<uint8_t>;

//! Request of Set<Type>: the property name followed by the value.
template <typename T>
auto DeserializeSetRequest(SilKit::Util::Span<const uint8_t> data) -> std::pair<std::string, T>;

//! Response of Get<Type>, and of GetApiVersion.
template <typename T>
auto SerializeResponse(const T& value) -> std::vector<uint8_t>;

//! Pack a GetPropertyStatus word. `flags` is an OR of the PropertyStatus
//! constants; `type` goes in the nibble. Kept separate from the flags so a
//! caller cannot shift a type into the wrong bits by hand.
auto MakeStatus(int32_t flags, TypeCode type) -> int32_t;

//! The TypeCode a status word carries. Works on a kQueryFailed word too, since
//! the type is answerable without the VHAL.
auto StatusTypeCode(int32_t status) -> TypeCode;

// ---- Implementation ----

namespace detail {

//! Single point of truth for how each API type sits on the wire. Every other
//! function here routes through this pair, so the encoding cannot drift between
//! the request and response directions.
template <typename T>
void SerializeValue(SilKit::Util::SerDes::Serializer& s, const T& value)
{
    if constexpr (std::is_same_v<T, int32_t>)
        s.Serialize(value, 32);
    else if constexpr (std::is_same_v<T, int64_t>)
        s.Serialize(value, 64);
    else if constexpr (std::is_same_v<T, float>)
        s.Serialize(value);
    else if constexpr (std::is_same_v<T, std::string>)
        s.Serialize(value);
    else if constexpr (std::is_same_v<T, std::vector<uint8_t>>)
        s.Serialize(value);
    else
        static_assert(!sizeof(T), "type is not part of the VhalSimple API");
}

template <typename T>
auto DeserializeValue(SilKit::Util::SerDes::Deserializer& d) -> T
{
    if constexpr (std::is_same_v<T, int32_t>)
        return d.Deserialize<int32_t>(32);
    else if constexpr (std::is_same_v<T, int64_t>)
        return d.Deserialize<int64_t>(64);
    else if constexpr (std::is_same_v<T, float>)
        return d.Deserialize<float>();
    else if constexpr (std::is_same_v<T, std::string>)
        return d.Deserialize<std::string>();
    else if constexpr (std::is_same_v<T, std::vector<uint8_t>>)
        return d.Deserialize<std::vector<uint8_t>>();
    else
        static_assert(!sizeof(T), "type is not part of the VhalSimple API");
}

} // namespace detail

template <typename T>
auto DeserializeSetRequest(SilKit::Util::Span<const uint8_t> data) -> std::pair<std::string, T>
{
    SilKit::Util::SerDes::Deserializer d{ToVector(data)};
    d.BeginStruct();
    auto name = d.Deserialize<std::string>();
    auto value = detail::DeserializeValue<T>(d);
    d.EndStruct();
    return {std::move(name), std::move(value)};
}

template <typename T>
auto SerializeResponse(const T& value) -> std::vector<uint8_t>
{
    SilKit::Util::SerDes::Serializer s;
    s.BeginStruct();
    detail::SerializeValue(s, value);
    s.EndStruct();
    return s.ReleaseBuffer();
}

inline auto MakeStatus(int32_t flags, TypeCode type) -> int32_t
{
    return flags | ((static_cast<int32_t>(type) & PropertyStatus::kTypeMask) << PropertyStatus::kTypeShift);
}

inline auto StatusTypeCode(int32_t status) -> TypeCode
{
    return static_cast<TypeCode>((status >> PropertyStatus::kTypeShift) & PropertyStatus::kTypeMask);
}

} // namespace VhalSimpleApi
