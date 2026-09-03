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
} // namespace Method

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

} // namespace VhalSimpleApi
