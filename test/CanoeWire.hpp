// SPDX-FileCopyrightText: 2026 Vector Informatik GmbH
// SPDX-License-Identifier: MIT
#pragma once

// The CANoe side of the VhalSimple wire contract, as a test would emit it:
// bare fields with no struct framing, exactly what the sil-adapter-builder
// generated TypeLib serializer produces. Shared by every integration test that
// plays the part of a CANoe consumer.
//
// The encoding is deliberately hand-written here rather than taken from
// VhalSimpleApi.hpp. A test that encodes with the same code the bridge decodes
// with would pass on any self-consistent format, including a wrong one; these
// helpers are the independent second opinion that pins the format CANoe
// actually speaks. Test_VhalSimpleApi.cpp guards the byte layout itself.

#include <cstdint>
#include <string>
#include <vector>

#include "silkit/services/rpc/all.hpp"
#include "silkit/util/serdes/Deserializer.hpp"
#include "silkit/util/serdes/Serializer.hpp"

#include "VhalSimpleApi.hpp"

namespace test {

namespace Rpc = SilKit::Services::Rpc;
namespace SerDes = SilKit::Util::SerDes;

//! Build the RpcSpec a CANoe-generated consumer uses: the bare method name as
//! the function name (the matching key) plus the three Mandatory labels.
//! Defaults mirror VhalSilKitAdapter::MakeRpcSpec / sil-adapter-builder.
inline auto MakeClientSpec(const std::string& functionName, const std::string& ns = VhalSimpleApi::kNamespace,
                           const std::string& instance = VhalSimpleApi::kInstance,
                           const std::string& vnet = VhalSimpleApi::kVirtualNetwork) -> Rpc::RpcSpec
{
    Rpc::RpcSpec spec{functionName, SerDes::MediaTypeRpc()};
    spec.AddLabel("VirtualNetwork", vnet, SilKit::Services::MatchingLabel::Kind::Mandatory);
    spec.AddLabel("Namespace", ns, SilKit::Services::MatchingLabel::Kind::Mandatory);
    spec.AddLabel("Instance", instance, SilKit::Services::MatchingLabel::Kind::Mandatory);
    return spec;
}

// ---- Request encoders ----

inline auto GetReq(const std::string& name) -> std::vector<uint8_t>
{
    SerDes::Serializer s;
    s.Serialize(name);
    return s.ReleaseBuffer();
}

inline auto SetInt32Req(const std::string& name, int32_t value) -> std::vector<uint8_t>
{
    SerDes::Serializer s;
    s.Serialize(name);
    s.Serialize(value, 32);
    return s.ReleaseBuffer();
}

inline auto SetInt64Req(const std::string& name, int64_t value) -> std::vector<uint8_t>
{
    SerDes::Serializer s;
    s.Serialize(name);
    s.Serialize(value, 64);
    return s.ReleaseBuffer();
}

inline auto SetFloatReq(const std::string& name, float value) -> std::vector<uint8_t>
{
    SerDes::Serializer s;
    s.Serialize(name);
    s.Serialize(value);
    return s.ReleaseBuffer();
}

inline auto SetStringReq(const std::string& name, const std::string& value) -> std::vector<uint8_t>
{
    SerDes::Serializer s;
    s.Serialize(name);
    s.Serialize(value);
    return s.ReleaseBuffer();
}

inline auto SetBytesReq(const std::string& name, const std::vector<uint8_t>& value) -> std::vector<uint8_t>
{
    SerDes::Serializer s;
    s.Serialize(name);
    s.Serialize(value);
    return s.ReleaseBuffer();
}

inline auto EmptyReq() -> std::vector<uint8_t>
{
    return {};
}

// ---- Response parsers ----

inline auto ParseInt32(const std::vector<uint8_t>& bytes) -> int32_t
{
    SerDes::Deserializer d{bytes};
    return d.Deserialize<int32_t>(32);
}

inline auto ParseInt64(const std::vector<uint8_t>& bytes) -> int64_t
{
    SerDes::Deserializer d{bytes};
    return d.Deserialize<int64_t>(64);
}

inline auto ParseFloat(const std::vector<uint8_t>& bytes) -> float
{
    SerDes::Deserializer d{bytes};
    return d.Deserialize<float>();
}

inline auto ParseString(const std::vector<uint8_t>& bytes) -> std::string
{
    SerDes::Deserializer d{bytes};
    return d.Deserialize<std::string>();
}

inline auto ParseBytes(const std::vector<uint8_t>& bytes) -> std::vector<uint8_t>
{
    SerDes::Deserializer d{bytes};
    return d.Deserialize<std::vector<uint8_t>>();
}

} // namespace test
