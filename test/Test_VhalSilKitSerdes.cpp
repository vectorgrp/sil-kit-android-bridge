// SPDX-FileCopyrightText: 2026 Vector Informatik GmbH
// SPDX-License-Identifier: MIT
//
// Unit tests for the CANoe-compatible binary wire format in VhalSilKitSerdes.hpp.
// The "CANoe side" is emulated with raw SilKit SerDes calls that emit bare fields
// (no struct framing) - exactly what the sil-adapter-builder-generated TypeLib
// serializer produces. These tests are the regression guard for the wire-format
// compatibility that previously broke CANoe interop.
#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "silkit/util/Span.hpp"
#include "silkit/util/serdes/Deserializer.hpp"
#include "silkit/util/serdes/Serializer.hpp"

#include "VhalSilKitSerdes.hpp"

namespace
{
namespace SerDes = SilKit::Util::SerDes;
using SilKit::Util::ToSpan;

// Build a request the way the CANoe-generated client does: bare fields, no
// struct framing.
auto CanoeGetRequest(const std::string& name) -> std::vector<uint8_t>
{
    SerDes::Serializer s;
    s.Serialize(name);
    return s.ReleaseBuffer();
}

// ---- Request deserialization (adapter receives what CANoe sends) ----

TEST(VhalSerDes, DeserializeGetRequestFromBareCanoeBytes)
{
    const auto bytes = CanoeGetRequest("EV_BATTERY_LEVEL");
    EXPECT_EQ(VhalSerDes::DeserializeGetRequest(ToSpan(bytes)), "EV_BATTERY_LEVEL");
}

TEST(VhalSerDes, DeserializeSetInt32Request)
{
    SerDes::Serializer s;
    s.Serialize(std::string{"RANGE_REMAINING"});
    s.Serialize(int32_t{123}, 32);
    const auto bytes = s.ReleaseBuffer();

    const auto [name, value] = VhalSerDes::DeserializeSetInt32Request(ToSpan(bytes));
    EXPECT_EQ(name, "RANGE_REMAINING");
    EXPECT_EQ(value, 123);
}

TEST(VhalSerDes, DeserializeSetFloatRequest)
{
    SerDes::Serializer s;
    s.Serialize(std::string{"CABIN_TEMP"});
    s.Serialize(21.5f);
    const auto bytes = s.ReleaseBuffer();

    const auto [name, value] = VhalSerDes::DeserializeSetFloatRequest(ToSpan(bytes));
    EXPECT_EQ(name, "CABIN_TEMP");
    EXPECT_FLOAT_EQ(value, 21.5f);
}

// ---- Response serialization (adapter sends what CANoe deserializes) ----

TEST(VhalSerDes, SerializeInt32ResponseRoundTrip)
{
    const auto bytes = VhalSerDes::SerializeInt32Response(42);
    SerDes::Deserializer d{bytes};
    EXPECT_EQ(d.Deserialize<int32_t>(32), 42); // bare read, as the CANoe client does
}

TEST(VhalSerDes, SerializeFloatResponseRoundTrip)
{
    const auto bytes = VhalSerDes::SerializeFloatResponse(3.5f);
    SerDes::Deserializer d{bytes};
    EXPECT_FLOAT_EQ(d.Deserialize<float>(), 3.5f);
}

TEST(VhalSerDes, VoidResponseIsEmpty)
{
    EXPECT_TRUE(VhalSerDes::SerializeVoidResponse().empty());
}

// ---- Byte-exact layout (what actually goes on the wire) ----

TEST(VhalSerDes, Int32ResponseIsFourBytesLittleEndian)
{
    const auto bytes = VhalSerDes::SerializeInt32Response(42);
    ASSERT_EQ(bytes.size(), 4u); // BeginStruct/EndStruct are zero-width
    EXPECT_EQ(bytes[0], 0x2A);
    EXPECT_EQ(bytes[1], 0x00);
    EXPECT_EQ(bytes[2], 0x00);
    EXPECT_EQ(bytes[3], 0x00);
}

TEST(VhalSerDes, StringIsUint32LengthPrefixedThenRawBytes)
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

// The BeginStruct/EndStruct calls in VhalSilKitSerdes.hpp must be byte-equivalent
// no-ops for these byte-aligned fields, so a struct-framed encoding matches the
// bare encoding CANoe emits.
TEST(VhalSerDes, StructFramingIsZeroWidth)
{
    SerDes::Serializer framed;
    framed.BeginStruct();
    framed.Serialize(std::string{"EV_BATTERY_LEVEL"});
    framed.EndStruct();

    EXPECT_EQ(framed.ReleaseBuffer(), CanoeGetRequest("EV_BATTERY_LEVEL"));
}

} // namespace
