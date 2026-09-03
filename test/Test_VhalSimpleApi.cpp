// SPDX-FileCopyrightText: 2026 Vector Informatik GmbH
// SPDX-License-Identifier: MIT
//
// Unit tests for the CANoe-compatible binary wire format of the VhalSimple API
// (VhalSimpleApi.hpp). The "CANoe side" is emulated with raw SilKit SerDes calls
// that emit bare fields (no struct framing) - exactly what the
// sil-adapter-builder-generated TypeLib serializer produces. These tests are the
// regression guard for the wire-format compatibility that previously broke CANoe
// interop.
#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "silkit/util/Span.hpp"
#include "silkit/util/serdes/Deserializer.hpp"
#include "silkit/util/serdes/Serializer.hpp"

#include "VhalSimpleApi.hpp"

namespace {
namespace SerDes = SilKit::Util::SerDes;
namespace Api = VhalSimpleApi;
using SilKit::Util::ToSpan;

// Build a request the way the CANoe-generated client does: bare fields, no
// struct framing.
auto CanoeGetRequest(const std::string& name) -> std::vector<uint8_t>
{
    SerDes::Serializer s;
    s.Serialize(name);
    return s.ReleaseBuffer();
}

// ---- Contract identity ----

// These four values are the entire matching contract with VhalSimple.vcdl. If a
// rename lands here without landing there, CANoe stops matching and every call
// returns ServerNotReachable, so pin them.
TEST(VhalSimpleApiContract, IdentityMatchesTheVcdl)
{
    EXPECT_STREQ(Api::kNamespace, "VhalSimple");
    EXPECT_STREQ(Api::kInstance, "properties");
    EXPECT_STREQ(Api::kVirtualNetwork, "Default");
    EXPECT_EQ(Api::kApiVersion, 1);
}

TEST(VhalSimpleApiContract, MethodNamesAreBareAndTypeSuffixed)
{
    EXPECT_STREQ(Api::Method::kGetApiVersion, "GetApiVersion");
    EXPECT_STREQ(Api::Method::kGetInt32, "GetInt32");
    EXPECT_STREQ(Api::Method::kSetInt32, "SetInt32");
    EXPECT_STREQ(Api::Method::kGetInt64, "GetInt64");
    EXPECT_STREQ(Api::Method::kSetInt64, "SetInt64");
    EXPECT_STREQ(Api::Method::kGetFloat, "GetFloat");
    EXPECT_STREQ(Api::Method::kSetFloat, "SetFloat");
    EXPECT_STREQ(Api::Method::kGetString, "GetString");
    EXPECT_STREQ(Api::Method::kSetString, "SetString");
    EXPECT_STREQ(Api::Method::kGetBytes, "GetBytes");
    EXPECT_STREQ(Api::Method::kSetBytes, "SetBytes");
}

// ---- Request deserialization (adapter receives what CANoe sends) ----

TEST(VhalSimpleApiSerDes, DeserializeGetRequestFromBareCanoeBytes)
{
    const auto bytes = CanoeGetRequest("EV_BATTERY_LEVEL");
    EXPECT_EQ(Api::DeserializeGetRequest(ToSpan(bytes)), "EV_BATTERY_LEVEL");
}

TEST(VhalSimpleApiSerDes, DeserializeSetInt32Request)
{
    SerDes::Serializer s;
    s.Serialize(std::string{"RANGE_REMAINING"});
    s.Serialize(int32_t{123}, 32);
    const auto bytes = s.ReleaseBuffer();

    const auto [name, value] = Api::DeserializeSetRequest<int32_t>(ToSpan(bytes));
    EXPECT_EQ(name, "RANGE_REMAINING");
    EXPECT_EQ(value, 123);
}

TEST(VhalSimpleApiSerDes, DeserializeSetInt64Request)
{
    SerDes::Serializer s;
    s.Serialize(std::string{"ANDROID_EPOCH_TIME"});
    s.Serialize(int64_t{1767225600000}, 64);
    const auto bytes = s.ReleaseBuffer();

    const auto [name, value] = Api::DeserializeSetRequest<int64_t>(ToSpan(bytes));
    EXPECT_EQ(name, "ANDROID_EPOCH_TIME");
    EXPECT_EQ(value, int64_t{1767225600000});
}

TEST(VhalSimpleApiSerDes, DeserializeSetFloatRequest)
{
    SerDes::Serializer s;
    s.Serialize(std::string{"EV_BATTERY_LEVEL"});
    s.Serialize(21.5f);
    const auto bytes = s.ReleaseBuffer();

    const auto [name, value] = Api::DeserializeSetRequest<float>(ToSpan(bytes));
    EXPECT_EQ(name, "EV_BATTERY_LEVEL");
    EXPECT_FLOAT_EQ(value, 21.5f);
}

TEST(VhalSimpleApiSerDes, DeserializeSetStringRequest)
{
    SerDes::Serializer s;
    s.Serialize(std::string{"INFO_VIN"});
    s.Serialize(std::string{"WVWZZZ1JZXW000001"});
    const auto bytes = s.ReleaseBuffer();

    const auto [name, value] = Api::DeserializeSetRequest<std::string>(ToSpan(bytes));
    EXPECT_EQ(name, "INFO_VIN");
    EXPECT_EQ(value, "WVWZZZ1JZXW000001");
}

TEST(VhalSimpleApiSerDes, DeserializeSetBytesRequest)
{
    const std::vector<uint8_t> payload{0xDE, 0xAD, 0xBE, 0xEF};
    SerDes::Serializer s;
    s.Serialize(std::string{"STORAGE_ENCRYPTION_BINDING_SEED"});
    s.Serialize(payload);
    const auto bytes = s.ReleaseBuffer();

    const auto [name, value] = Api::DeserializeSetRequest<std::vector<uint8_t>>(ToSpan(bytes));
    EXPECT_EQ(name, "STORAGE_ENCRYPTION_BINDING_SEED");
    EXPECT_EQ(value, payload);
}

// ---- Response serialization (adapter sends what CANoe deserializes) ----

TEST(VhalSimpleApiSerDes, Int32ResponseRoundTrip)
{
    const auto bytes = Api::SerializeResponse(int32_t{42});
    SerDes::Deserializer d{bytes};
    EXPECT_EQ(d.Deserialize<int32_t>(32), 42); // bare read, as the CANoe client does
}

TEST(VhalSimpleApiSerDes, Int64ResponseRoundTrip)
{
    const auto bytes = Api::SerializeResponse(int64_t{-9007199254740993});
    SerDes::Deserializer d{bytes};
    EXPECT_EQ(d.Deserialize<int64_t>(64), int64_t{-9007199254740993});
}

TEST(VhalSimpleApiSerDes, FloatResponseRoundTrip)
{
    const auto bytes = Api::SerializeResponse(3.5f);
    SerDes::Deserializer d{bytes};
    EXPECT_FLOAT_EQ(d.Deserialize<float>(), 3.5f);
}

TEST(VhalSimpleApiSerDes, StringResponseRoundTrip)
{
    const auto bytes = Api::SerializeResponse(std::string{"PARKED"});
    SerDes::Deserializer d{bytes};
    EXPECT_EQ(d.Deserialize<std::string>(), "PARKED");
}

TEST(VhalSimpleApiSerDes, BytesResponseRoundTrip)
{
    const std::vector<uint8_t> payload{0x00, 0x01, 0xFF};
    const auto bytes = Api::SerializeResponse(payload);
    SerDes::Deserializer d{bytes};
    EXPECT_EQ(d.Deserialize<std::vector<uint8_t>>(), payload);
}

TEST(VhalSimpleApiSerDes, VoidResponseIsEmpty)
{
    EXPECT_TRUE(Api::SerializeVoidResponse().empty());
}

// Empty payloads are what a failed Get answers with, so they have to survive the
// round trip rather than throwing.
TEST(VhalSimpleApiSerDes, DefaultConstructedValuesRoundTrip)
{
    {
        const auto bytes = Api::SerializeResponse(std::string{});
        SerDes::Deserializer d{bytes};
        EXPECT_EQ(d.Deserialize<std::string>(), "");
    }
    {
        const auto bytes = Api::SerializeResponse(std::vector<uint8_t>{});
        SerDes::Deserializer d{bytes};
        EXPECT_TRUE(d.Deserialize<std::vector<uint8_t>>().empty());
    }
}

// ---- Byte-exact layout (what actually goes on the wire) ----

TEST(VhalSimpleApiSerDes, Int32ResponseIsFourBytesLittleEndian)
{
    const auto bytes = Api::SerializeResponse(int32_t{42});
    ASSERT_EQ(bytes.size(), 4u); // BeginStruct/EndStruct are zero-width
    EXPECT_EQ(bytes[0], 0x2A);
    EXPECT_EQ(bytes[1], 0x00);
    EXPECT_EQ(bytes[2], 0x00);
    EXPECT_EQ(bytes[3], 0x00);
}

TEST(VhalSimpleApiSerDes, Int64ResponseIsEightBytesLittleEndian)
{
    const auto bytes = Api::SerializeResponse(int64_t{42});
    ASSERT_EQ(bytes.size(), 8u);
    EXPECT_EQ(bytes[0], 0x2A);
    for (size_t i = 1; i < 8; ++i)
    {
        EXPECT_EQ(bytes[i], 0x00) << "byte " << i;
    }
}

TEST(VhalSimpleApiSerDes, StringIsUint32LengthPrefixedThenRawBytes)
{
    const auto bytes = CanoeGetRequest("AB");
    ASSERT_EQ(bytes.size(), 4u + 2u);
    EXPECT_EQ(bytes[0], 0x02); // length = 2, little-endian uint32
    EXPECT_EQ(bytes[1], 0x00);
    EXPECT_EQ(bytes[2], 0x00);
    EXPECT_EQ(bytes[3], 0x00);
    EXPECT_EQ(bytes[4], 'A');
    EXPECT_EQ(bytes[5], 'B');
}

// Bytes use the same framing as string, which is what makes GetBytes/SetBytes a
// mechanical addition rather than a new wire concept.
TEST(VhalSimpleApiSerDes, BytesUseTheSameFramingAsString)
{
    const auto asBytes = Api::SerializeResponse(std::vector<uint8_t>{'A', 'B'});
    const auto asString = Api::SerializeResponse(std::string{"AB"});
    EXPECT_EQ(asBytes, asString);
}

// The BeginStruct/EndStruct calls in VhalSimpleApi.hpp must be byte-equivalent
// no-ops for these byte-aligned fields, so a struct-framed encoding matches the
// bare encoding CANoe emits.
TEST(VhalSimpleApiSerDes, StructFramingIsZeroWidth)
{
    SerDes::Serializer framed;
    framed.BeginStruct();
    framed.Serialize(std::string{"EV_BATTERY_LEVEL"});
    framed.EndStruct();

    EXPECT_EQ(framed.ReleaseBuffer(), CanoeGetRequest("EV_BATTERY_LEVEL"));
}

} // namespace
