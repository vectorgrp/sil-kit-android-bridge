// SPDX-FileCopyrightText: 2026 Vector Informatik GmbH
// SPDX-License-Identifier: MIT
//
// Unit tests for the pure VHAL translation layer (VhalGrpcConvert.hpp): URI
// validation, VehiclePropValue -> CarProperty, and the StatusCode gating that
// keeps a refused read or write from looking like data. No gRPC server and no
// SIL Kit are involved; every input here is a protobuf message built in-process.
#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "VhalGrpcConvert.hpp"

namespace {
namespace proto = android::hardware::automotive::vehicle::proto;
using android::hardware::automotive::vehicle::VehicleProperty_GetIdByName;

// One representative AOSP property per value type. Looked up by name so the test
// keeps working if AOSP renumbers an id, and so a typo fails loudly here rather
// than silently converting a property of the wrong type.
constexpr auto kStringProp = "INFO_VIN";
constexpr auto kBooleanProp = "EV_CHARGE_PORT_CONNECTED";
constexpr auto kInt32Prop = "INFO_MODEL_YEAR";
constexpr auto kInt64Prop = "ANDROID_EPOCH_TIME";
constexpr auto kFloatProp = "EV_BATTERY_LEVEL";
constexpr auto kBytesProp = "STORAGE_ENCRYPTION_BINDING_SEED";

auto IdOf(const char* name) -> int32_t
{
    const auto id = VehicleProperty_GetIdByName(name);
    EXPECT_NE(id, 0) << "unknown property name in test fixture: " << name;
    return id;
}

auto ValueOf(const char* name) -> proto::VehiclePropValue
{
    proto::VehiclePropValue v;
    v.set_prop(IdOf(name));
    return v;
}

// ---------------------------------------------------------------- ValidateUri

TEST(VhalConvertUri, AbsolutePathBecomesUnixScheme)
{
    EXPECT_EQ(VhalConvert::ValidateUri("/tmp/vhal.sock"), "unix:///tmp/vhal.sock");
    EXPECT_EQ(VhalConvert::ValidateUri("/tmp/vsock_3_1002/vm.vsock_9300"), "unix:///tmp/vsock_3_1002/vm.vsock_9300");
}

TEST(VhalConvertUri, KnownSchemesPassThroughUnchanged)
{
    EXPECT_EQ(VhalConvert::ValidateUri("unix:///tmp/vhal.sock"), "unix:///tmp/vhal.sock");
    EXPECT_EQ(VhalConvert::ValidateUri("http://localhost:9300"), "http://localhost:9300");
    EXPECT_EQ(VhalConvert::ValidateUri("https://localhost:9300"), "https://localhost:9300");
}

TEST(VhalConvertUri, UnknownSchemeThrows)
{
    EXPECT_THROW(VhalConvert::ValidateUri("localhost:9300"), VhalClientError);
    EXPECT_THROW(VhalConvert::ValidateUri("tcp://localhost:9300"), VhalClientError);
    EXPECT_THROW(VhalConvert::ValidateUri(""), VhalClientError);
}

// -------------------------------------------------- VehiclePropValue mapping

TEST(VhalConvertValue, Int32PropertyYieldsInt32)
{
    auto v = ValueOf(kInt32Prop);
    v.mutable_int32_values()->Add(2026);

    const auto prop = VhalConvert::ToCarProperty(v);
    ASSERT_TRUE(prop.has_value());
    EXPECT_TRUE(prop->Holds<int32_t>());
    EXPECT_EQ(prop->Get<int32_t>().value(), 2026);
    EXPECT_EQ(prop->name, kInt32Prop);
}

// BOOLEAN is carried in int32_values and normalised to 0/1, so it lands in the
// same variant arm as INT32 rather than in a separate one.
TEST(VhalConvertValue, BooleanPropertyNormalisesToZeroOrOne)
{
    auto connected = ValueOf(kBooleanProp);
    connected.mutable_int32_values()->Add(7); // any non-zero
    const auto yes = VhalConvert::ToCarProperty(connected);
    ASSERT_TRUE(yes.has_value());
    EXPECT_TRUE(yes->Holds<int32_t>());
    EXPECT_EQ(yes->Get<int32_t>().value(), 1);

    auto disconnected = ValueOf(kBooleanProp);
    disconnected.mutable_int32_values()->Add(0);
    const auto no = VhalConvert::ToCarProperty(disconnected);
    ASSERT_TRUE(no.has_value());
    EXPECT_EQ(no->Get<int32_t>().value(), 0);
}

TEST(VhalConvertValue, Int64PropertyYieldsInt64)
{
    auto v = ValueOf(kInt64Prop);
    v.mutable_int64_values()->Add(int64_t{1767225600000});

    const auto prop = VhalConvert::ToCarProperty(v);
    ASSERT_TRUE(prop.has_value());
    EXPECT_TRUE(prop->Holds<int64_t>());
    EXPECT_EQ(prop->Get<int64_t>().value(), int64_t{1767225600000});
}

TEST(VhalConvertValue, FloatPropertyYieldsFloat)
{
    auto v = ValueOf(kFloatProp);
    v.mutable_float_values()->Add(21.5f);

    const auto prop = VhalConvert::ToCarProperty(v);
    ASSERT_TRUE(prop.has_value());
    EXPECT_TRUE(prop->Holds<float>());
    EXPECT_FLOAT_EQ(prop->Get<float>().value(), 21.5f);
}

TEST(VhalConvertValue, StringPropertyYieldsString)
{
    auto v = ValueOf(kStringProp);
    v.set_string_value("WVWZZZ1JZXW000001");

    const auto prop = VhalConvert::ToCarProperty(v);
    ASSERT_TRUE(prop.has_value());
    EXPECT_TRUE(prop->Holds<std::string>());
    EXPECT_EQ(prop->Get<std::string>().value(), "WVWZZZ1JZXW000001");
}

TEST(VhalConvertValue, BytesPropertyYieldsByteVector)
{
    auto v = ValueOf(kBytesProp);
    v.set_byte_values(std::string("\x01\x02\x03", 3));

    const auto prop = VhalConvert::ToCarProperty(v);
    ASSERT_TRUE(prop.has_value());
    ASSERT_TRUE(prop->Holds<std::vector<uint8_t>>());
    EXPECT_EQ(prop->Get<std::vector<uint8_t>>().value(), (std::vector<uint8_t>{1, 2, 3}));
}

// The regression guard for the out-of-bounds read: a VHAL that declines a read
// still returns a result, and the value it carries has no entries. Indexing it
// is a protobuf CHECK failure in a debug build and an out-of-bounds read in a
// release one.
TEST(VhalConvertValue, EmptyRepeatedFieldYieldsNullopt)
{
    for (const char* name : {kBooleanProp, kInt32Prop, kInt64Prop, kFloatProp})
    {
        // No *_values entries added at all.
        EXPECT_FALSE(VhalConvert::ToCarProperty(ValueOf(name)).has_value())
            << "expected nullopt for a valueless " << name;
    }
}

TEST(VhalConvertValue, UnknownPropertyTypeYieldsNullopt)
{
    proto::VehiclePropValue v;
    v.set_prop(0); // type bits decode to UNKNOWN
    EXPECT_FALSE(VhalConvert::ToCarProperty(v).has_value());
}

// ------------------------------------------------- GetValueResult status gate

TEST(VhalConvertReadStatus, OkResultConverts)
{
    proto::GetValueResult result;
    result.set_status(proto::StatusCode::OK);
    *result.mutable_value() = ValueOf(kFloatProp);
    result.mutable_value()->mutable_float_values()->Add(80.0f);

    const auto prop = VhalConvert::ToCarProperty(result);
    ASSERT_TRUE(prop.has_value());
    EXPECT_FLOAT_EQ(prop->Get<float>().value(), 80.0f);
}

// A non-OK result can still carry a value. Converting it anyway would hand the
// caller a number indistinguishable from a real reading, which is the bug.
TEST(VhalConvertReadStatus, NonOkResultYieldsNulloptEvenWithAValue)
{
    for (auto status :
         {proto::StatusCode::NOT_AVAILABLE, proto::StatusCode::ACCESS_DENIED, proto::StatusCode::INTERNAL_ERROR,
          proto::StatusCode::TRY_AGAIN, proto::StatusCode::NOT_AVAILABLE_SAFETY})
    {
        proto::GetValueResult result;
        result.set_status(status);
        *result.mutable_value() = ValueOf(kFloatProp);
        result.mutable_value()->mutable_float_values()->Add(80.0f);

        EXPECT_FALSE(VhalConvert::ToCarProperty(result).has_value())
            << "status " << proto::StatusCode_Name(status) << " must not yield a value";
    }
}

// ------------------------------------------------ SetValueResults status gate

TEST(VhalConvertWriteStatus, AllOkYieldsNoError)
{
    proto::SetValueResults results;
    for (int i = 0; i < 3; ++i)
    {
        auto* r = results.add_results();
        r->set_request_id(i);
        r->set_status(proto::StatusCode::OK);
    }
    EXPECT_FALSE(VhalConvert::FirstSetError(results).has_value());
}

TEST(VhalConvertWriteStatus, EmptyResultsYieldNoError)
{
    proto::SetValueResults results;
    EXPECT_FALSE(VhalConvert::FirstSetError(results).has_value());
}

TEST(VhalConvertWriteStatus, RefusedWriteIsReportedWithItsStatusName)
{
    proto::SetValueResults results;
    auto* ok = results.add_results();
    ok->set_request_id(1);
    ok->set_status(proto::StatusCode::OK);

    auto* bad = results.add_results();
    bad->set_request_id(7);
    bad->set_status(proto::StatusCode::ACCESS_DENIED);

    const auto err = VhalConvert::FirstSetError(results);
    ASSERT_TRUE(err.has_value());
    EXPECT_NE(err->find("ACCESS_DENIED"), std::string::npos) << *err;
    EXPECT_NE(err->find("7"), std::string::npos) << *err;
}

} // namespace
