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

// ------------------------------------------- CarProperty -> VehiclePropValue

// The regression guard for the bug found against live Cuttlefish: a BYTES value
// was dispatched through the integral-vector branch and rejected with "expects
// BYTES, not INT32_VEC", so no byte property could ever be written to a device.
TEST(VhalConvertWrite, BytesGoIntoByteValuesNotInt32Values)
{
    const std::vector<uint8_t> seed{0xDE, 0xAD, 0xBE, 0xEF};
    const auto v = VhalConvert::ToPropValue(IdOf(kBytesProp), CarProperty::Value{seed});

    EXPECT_EQ(v.prop(), IdOf(kBytesProp));
    EXPECT_EQ(v.int32_values_size(), 0) << "a BYTES value must not land in int32_values";
    ASSERT_EQ(v.byte_values().size(), seed.size());
    EXPECT_EQ(std::vector<uint8_t>(v.byte_values().begin(), v.byte_values().end()), seed);
}

// Every arm has to reach the protobuf field ToCarProperty reads back, or a write
// and the following read disagree.
TEST(VhalConvertWrite, EachTypeUsesTheFieldItsReaderExpects)
{
    {
        const auto v = VhalConvert::ToPropValue(IdOf(kInt32Prop), CarProperty::Value{int32_t{2026}});
        ASSERT_EQ(v.int32_values_size(), 1);
        EXPECT_EQ(v.int32_values(0), 2026);
    }
    {
        const auto v = VhalConvert::ToPropValue(IdOf(kInt64Prop), CarProperty::Value{int64_t{1767225600000}});
        ASSERT_EQ(v.int64_values_size(), 1);
        EXPECT_EQ(v.int64_values(0), int64_t{1767225600000});
    }
    {
        const auto v = VhalConvert::ToPropValue(IdOf(kFloatProp), CarProperty::Value{88.5f});
        ASSERT_EQ(v.float_values_size(), 1);
        EXPECT_FLOAT_EQ(v.float_values(0), 88.5f);
    }
    {
        const auto v = VhalConvert::ToPropValue(IdOf(kStringProp), CarProperty::Value{std::string{"VIN"}});
        EXPECT_EQ(v.string_value(), "VIN");
    }
    // AOSP BOOLEAN travels as int32.
    {
        const auto v = VhalConvert::ToPropValue(IdOf(kBooleanProp), CarProperty::Value{int32_t{1}});
        ASSERT_EQ(v.int32_values_size(), 1);
        EXPECT_EQ(v.int32_values(0), 1);
    }
}

// A write and a read of the same property must agree on the field, which is the
// invariant the original bug broke.
TEST(VhalConvertWrite, WriteThenReadRoundTripsEveryType)
{
    auto roundTrip = [](const char* name, const CarProperty::Value& value) {
        const auto prop = VhalConvert::ToCarProperty(VhalConvert::ToPropValue(IdOf(name), value));
        ASSERT_TRUE(prop.has_value()) << name;
        EXPECT_TRUE(prop->value == value) << name;
    };
    roundTrip(kInt32Prop, CarProperty::Value{int32_t{2026}});
    roundTrip(kInt64Prop, CarProperty::Value{int64_t{1767225600000}});
    roundTrip(kFloatProp, CarProperty::Value{88.5f});
    roundTrip(kStringProp, CarProperty::Value{std::string{"WVWZZZ1JZXW000001"}});
    roundTrip(kBytesProp, CarProperty::Value{std::vector<uint8_t>{0xDE, 0xAD, 0xBE, 0xEF}});
    roundTrip(kBooleanProp, CarProperty::Value{int32_t{1}});
}

TEST(VhalConvertWrite, WrongTypedWriteThrowsNamingBothTypes)
{
    // int32 into a FLOAT property
    EXPECT_THROW(VhalConvert::ToPropValue(IdOf(kFloatProp), CarProperty::Value{int32_t{1}}), VhalClientError);
    // bytes into an INT32 property
    EXPECT_THROW(VhalConvert::ToPropValue(IdOf(kInt32Prop), CarProperty::Value{std::vector<uint8_t>{1, 2}}),
                 VhalClientError);
    // string into a BYTES property
    EXPECT_THROW(VhalConvert::ToPropValue(IdOf(kBytesProp), CarProperty::Value{std::string{"x"}}), VhalClientError);
    // an empty variant is never writable
    EXPECT_THROW(VhalConvert::ToPropValue(IdOf(kInt32Prop), CarProperty::Value{}), VhalClientError);
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

// ------------------------------------------------------------ ToPropertyInfo
//
// The property configuration table is what GetPropertyStatus answers from, so
// this mapping decides whether CANoe is told a property is readable, writable
// or absent. Proto in, project struct out - no channel, no server.

namespace {
auto ConfigOf(const char* name, proto::VehiclePropertyAccess access) -> proto::VehiclePropConfig
{
    proto::VehiclePropConfig c;
    c.set_prop(IdOf(name));
    c.set_access(access);
    return c;
}
} // namespace

TEST(VhalConvertConfig, AccessMapsToReadableAndWritable)
{
    auto info = [](proto::VehiclePropertyAccess access) {
        return VhalConvert::ToPropertyInfo(ConfigOf(kFloatProp, access));
    };

    EXPECT_FALSE(info(proto::VehiclePropertyAccess::NONE).IsReadable());
    EXPECT_FALSE(info(proto::VehiclePropertyAccess::NONE).IsWritable());

    EXPECT_TRUE(info(proto::VehiclePropertyAccess::READ).IsReadable());
    EXPECT_FALSE(info(proto::VehiclePropertyAccess::READ).IsWritable());

    EXPECT_FALSE(info(proto::VehiclePropertyAccess::WRITE).IsReadable());
    EXPECT_TRUE(info(proto::VehiclePropertyAccess::WRITE).IsWritable());

    EXPECT_TRUE(info(proto::VehiclePropertyAccess::READ_WRITE).IsReadable());
    EXPECT_TRUE(info(proto::VehiclePropertyAccess::READ_WRITE).IsWritable());
}

// The project enum restates the AOSP numbers rather than including the proto
// into IVhalClient.hpp. VhalGrpcConvert.cpp static_asserts the correspondence,
// so this only has to prove the value actually survives the translation.
TEST(VhalConvertConfig, AccessValueIsCarriedThroughUntouched)
{
    const auto info = VhalConvert::ToPropertyInfo(ConfigOf(kInt32Prop, proto::VehiclePropertyAccess::READ_WRITE));
    EXPECT_EQ(info.access, PropertyAccess::ReadWrite);
    EXPECT_EQ(info.id, IdOf(kInt32Prop));
}

TEST(VhalConvertConfig, TypeComesFromTheAospTable)
{
    auto typeOf = [](const char* name) {
        return VhalConvert::ToPropertyInfo(ConfigOf(name, proto::VehiclePropertyAccess::READ)).type;
    };
    EXPECT_EQ(typeOf(kInt32Prop), CarPropertyType::Int32);
    EXPECT_EQ(typeOf(kBooleanProp), CarPropertyType::Int32) << "AOSP BOOLEAN travels as int32";
    EXPECT_EQ(typeOf(kInt64Prop), CarPropertyType::Int64);
    EXPECT_EQ(typeOf(kFloatProp), CarPropertyType::Float);
    EXPECT_EQ(typeOf(kStringProp), CarPropertyType::String);
    EXPECT_EQ(typeOf(kBytesProp), CarPropertyType::Bytes);
}

// A property whose type VhalSimple cannot carry is still a real property: it
// exists and has an access, it just has no Get<Type> to call. Reporting None
// rather than guessing a type is what stops a caller reading a vector property
// through GetInt32 and getting a plausible-looking first element.
TEST(VhalConvertConfig, UnrepresentableTypeYieldsNoTypeButKeepsTheRest)
{
    proto::VehiclePropConfig c;
    c.set_prop(IdOf("WHEEL_TICK")); // AOSP INT64_VEC
    c.set_access(proto::VehiclePropertyAccess::READ);

    const auto info = VhalConvert::ToPropertyInfo(c);
    EXPECT_EQ(info.type, CarPropertyType::None);
    EXPECT_EQ(info.id, IdOf("WHEEL_TICK"));
    EXPECT_TRUE(info.IsReadable());
}

// An unset config is proto3 default everything. It must not decode as readable:
// a default-constructed message reaching this function means the VHAL told us
// nothing, and inventing access from nothing is how a caller ends up calling a
// Get that will always fail.
TEST(VhalConvertConfig, DefaultConstructedConfigIsInaccessible)
{
    const auto info = VhalConvert::ToPropertyInfo(proto::VehiclePropConfig{});
    EXPECT_EQ(info.id, 0);
    EXPECT_EQ(info.access, PropertyAccess::None);
    EXPECT_EQ(info.type, CarPropertyType::None);
    EXPECT_FALSE(info.IsReadable());
    EXPECT_FALSE(info.IsWritable());
}

// AOSP V3+ lets a VHAL declare access per area and leave the top-level field at
// NONE. Reading only the top-level field would then report a perfectly usable
// property as inaccessible, so fall back to the union of the area accesses.
TEST(VhalConvertConfig, PerAreaAccessIsUsedWhenTheTopLevelAccessIsNone)
{
    proto::VehiclePropConfig c;
    c.set_prop(IdOf(kFloatProp));
    c.set_access(proto::VehiclePropertyAccess::NONE);
    c.add_area_configs()->set_access(proto::VehiclePropertyAccess::READ);
    c.add_area_configs()->set_access(proto::VehiclePropertyAccess::WRITE);

    const auto info = VhalConvert::ToPropertyInfo(c);
    EXPECT_TRUE(info.IsReadable()) << "one area is readable, so the property is";
    EXPECT_TRUE(info.IsWritable());
}

// Union means union in both directions. A restrictive area cannot narrow a
// property the VHAL declared globally...
TEST(VhalConvertConfig, ARestrictiveAreaDoesNotNarrowTheTopLevelAccess)
{
    proto::VehiclePropConfig c;
    c.set_prop(IdOf(kFloatProp));
    c.set_access(proto::VehiclePropertyAccess::READ_WRITE);
    c.add_area_configs()->set_access(proto::VehiclePropertyAccess::NONE);

    const auto info = VhalConvert::ToPropertyInfo(c);
    EXPECT_TRUE(info.IsReadable());
    EXPECT_TRUE(info.IsWritable());
}

// ...and a permissive area widens a top level that states less. Reading the
// areas only when the top level is NONE would report this property as
// not-writable, which is the intersection rule in disguise.
TEST(VhalConvertConfig, APermissiveAreaWidensAStatedTopLevelAccess)
{
    proto::VehiclePropConfig c;
    c.set_prop(IdOf(kFloatProp));
    c.set_access(proto::VehiclePropertyAccess::READ);
    c.add_area_configs()->set_access(proto::VehiclePropertyAccess::READ_WRITE);

    const auto info = VhalConvert::ToPropertyInfo(c);
    EXPECT_TRUE(info.IsReadable());
    EXPECT_TRUE(info.IsWritable()) << "an area declares the write, so the property supports it";
}

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
