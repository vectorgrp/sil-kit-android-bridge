// SPDX-FileCopyrightText: 2026 Vector Informatik GmbH
// SPDX-License-Identifier: MIT
#include <algorithm>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "VhalMockClient.hpp"

namespace {
// VhalMockClient seeds a fixed set of properties in its constructor; the target
// string is unused by the mock so any placeholder works.
auto MakeClient() -> VhalMockClient
{
    return VhalMockClient{"mock://test"};
}

TEST(VhalMockClient, CheckHealthIsTrue)
{
    auto client = MakeClient();
    EXPECT_TRUE(client.CheckHealth());
}

// The seed must cover every VhalSimple type, or --mock cannot exercise the whole
// API surface from CANoe.
TEST(VhalMockClient, SeedCoversEveryApiType)
{
    auto client = MakeClient();
    const auto names = client.GetAllProperties();
    auto has = [&](const char* n) { return std::find(names.begin(), names.end(), n) != names.end(); };
    EXPECT_TRUE(has("INFO_MODEL_YEAR"));                 // int32
    EXPECT_TRUE(has("EV_CHARGE_PORT_CONNECTED"));        // int32, AOSP BOOLEAN
    EXPECT_TRUE(has("ANDROID_EPOCH_TIME"));              // int64
    EXPECT_TRUE(has("EV_BATTERY_LEVEL"));                // float
    EXPECT_TRUE(has("INFO_VIN"));                        // string
    EXPECT_TRUE(has("STORAGE_ENCRYPTION_BINDING_SEED")); // bytes
}

// Every seeded value carries the type AOSP declares for its id. The constructor
// enforces this, so a wrong entry throws rather than reaching a CANoe test as a
// silent zero.
TEST(VhalMockClient, SeededValuesUseTheAospDeclaredType)
{
    auto client = MakeClient();

    const auto asFloat = client.GetProperty("EV_BATTERY_LEVEL"); // AOSP: FLOAT
    ASSERT_TRUE(asFloat.has_value());
    ASSERT_TRUE(asFloat->Holds<float>()) << "EV_BATTERY_LEVEL must be float, not int32";
    EXPECT_FLOAT_EQ(asFloat->Get<float>().value(), 88.5f);

    const auto asInt64 = client.GetProperty("ANDROID_EPOCH_TIME"); // AOSP: INT64
    ASSERT_TRUE(asInt64.has_value());
    ASSERT_TRUE(asInt64->Holds<int64_t>());

    const auto asString = client.GetProperty("INFO_VIN"); // AOSP: STRING
    ASSERT_TRUE(asString.has_value());
    ASSERT_TRUE(asString->Holds<std::string>());
    EXPECT_EQ(asString->Get<std::string>().value(), "WVWZZZ1JZXW000001");

    const auto asBytes = client.GetProperty("STORAGE_ENCRYPTION_BINDING_SEED"); // AOSP: BYTES
    ASSERT_TRUE(asBytes.has_value());
    ASSERT_TRUE(asBytes->Holds<std::vector<uint8_t>>());
}

// Distinctive values matter: a wrong read has to look wrong, not like a plausible
// zero or a default-constructed fallback.
TEST(VhalMockClient, SeededValuesAreNonDefault)
{
    auto client = MakeClient();
    for (const auto& name : client.GetAllProperties())
    {
        const auto prop = client.GetProperty(name);
        ASSERT_TRUE(prop.has_value());
        EXPECT_FALSE(prop->IsEmpty()) << name;
    }
    // NIGHT_MODE is deliberately 0 - a boolean needs a false somewhere - so the
    // check above is about "not monostate", not "not zero".
    EXPECT_EQ(client.GetProperty("NIGHT_MODE")->Get<int32_t>().value_or(-1), 0);
}

TEST(VhalMockClient, GetPropertyMissingReturnsNullopt)
{
    auto client = MakeClient();
    EXPECT_FALSE(client.GetProperty("NOT_A_REAL_PROPERTY").has_value());
}

TEST(VhalMockClient, SetPropertyUpdatesStoredValue)
{
    auto client = MakeClient();
    EXPECT_TRUE(client.SetProperty(CarProperty{"EV_BATTERY_LEVEL", 42.5f}));

    const auto prop = client.GetProperty("EV_BATTERY_LEVEL");
    ASSERT_TRUE(prop.has_value());
    EXPECT_FLOAT_EQ(prop->Get<float>().value_or(-1.f), 42.5f);
}

TEST(VhalMockClient, SetPropertyInsertsNewProperty)
{
    auto client = MakeClient();
    // GEAR_SELECTION is seeded, so use an AOSP property that is not: IGNITION_STATE.
    EXPECT_TRUE(client.SetProperty(CarProperty{"IGNITION_STATE", int32_t(2)}));

    const auto prop = client.GetProperty("IGNITION_STATE");
    ASSERT_TRUE(prop.has_value());
    EXPECT_EQ(prop->Get<int32_t>().value_or(-1), 2);
}

// A write of the wrong type is refused the way VhalGrpcClient::SetValues refuses
// it, so --mock and a real device fail the same way instead of the mock silently
// changing the stored type and breaking every later read.
TEST(VhalMockClient, SetPropertyRejectsAWrongTypedWrite)
{
    auto client = MakeClient();
    EXPECT_THROW(client.SetProperty(CarProperty{"EV_BATTERY_LEVEL", int32_t(85)}), std::runtime_error);

    // ...and the stored value is untouched.
    EXPECT_FLOAT_EQ(client.GetProperty("EV_BATTERY_LEVEL")->Get<float>().value_or(-1.f), 88.5f);
}

// Vendor / ad-hoc names are not in the AOSP table, so there is nothing to
// validate against and they are stored as given.
TEST(VhalMockClient, SetPropertyAcceptsAnyTypeForANonAospName)
{
    auto client = MakeClient();
    EXPECT_TRUE(client.SetProperty(CarProperty{"VENDOR_SOMETHING", 1.5f}));
    EXPECT_TRUE(client.GetProperty("VENDOR_SOMETHING")->Holds<float>());

    EXPECT_TRUE(client.SetProperty(CarProperty{"VENDOR_SOMETHING", std::string{"now a string"}}));
    EXPECT_TRUE(client.GetProperty("VENDOR_SOMETHING")->Holds<std::string>());
}

TEST(VhalMockClient, GetPropertiesBatchMixesHitsAndMisses)
{
    auto client = MakeClient();
    const auto props = client.GetProperties({"EV_BATTERY_LEVEL", "EV_CHARGE_STATE", "NONEXISTENT_PROP"});

    ASSERT_EQ(props.size(), 3u);
    ASSERT_TRUE(props[0].has_value());
    EXPECT_EQ(props[0]->name, "EV_BATTERY_LEVEL");
    ASSERT_TRUE(props[1].has_value());
    EXPECT_EQ(props[1]->name, "EV_CHARGE_STATE");
    EXPECT_FALSE(props[2].has_value());
}

// ---- GetPropertyInfo, which backs GetPropertyStatus ----
//
// The mock has no access model: everything it stores it will also hand back and
// let you overwrite, so ReadWrite is the honest answer rather than the access
// AOSP declares. Reporting INFO_VIN as read-only while still accepting the write
// would make the introspection lie about the very backend it describes.

TEST(VhalMockClient, GetPropertyInfoDescribesASeededProperty)
{
    auto client = MakeClient();

    const auto info = client.GetPropertyInfo("EV_BATTERY_LEVEL");
    ASSERT_TRUE(info.has_value());
    EXPECT_NE(info->id, 0) << "EV_BATTERY_LEVEL is an AOSP name, so it must resolve";
    EXPECT_EQ(info->type, CarPropertyType::Float);
    EXPECT_TRUE(info->IsReadable());
    EXPECT_TRUE(info->IsWritable());
}

// The type comes from the AOSP table, not from the stored variant, so it is the
// same answer a real VHAL gives. This is what lets a consumer pick the right
// Get<Type> instead of guessing and reading a silent zero.
TEST(VhalMockClient, GetPropertyInfoReportsTheAospDeclaredType)
{
    auto client = MakeClient();

    auto typeOf = [&](const char* name) {
        const auto info = client.GetPropertyInfo(name);
        return info.has_value() ? info->type : CarPropertyType::None;
    };
    EXPECT_EQ(typeOf("INFO_MODEL_YEAR"), CarPropertyType::Int32);
    EXPECT_EQ(typeOf("EV_CHARGE_PORT_CONNECTED"), CarPropertyType::Int32) << "AOSP BOOLEAN travels as int32";
    EXPECT_EQ(typeOf("ANDROID_EPOCH_TIME"), CarPropertyType::Int64);
    EXPECT_EQ(typeOf("EV_BATTERY_LEVEL"), CarPropertyType::Float);
    EXPECT_EQ(typeOf("INFO_VIN"), CarPropertyType::String);
    EXPECT_EQ(typeOf("STORAGE_ENCRYPTION_BINDING_SEED"), CarPropertyType::Bytes);
}

// An AOSP name the mock does not seed. nullopt is "this backend does not have
// it"; the adapter still reports the name as known, which is what separates a
// typo from a property this vehicle does not implement.
TEST(VhalMockClient, GetPropertyInfoIsNulloptForAnUnseededAospName)
{
    auto client = MakeClient();
    EXPECT_FALSE(client.GetPropertyInfo("PERF_STEERING_ANGLE").has_value());
}

TEST(VhalMockClient, GetPropertyInfoIsNulloptForAnUnknownName)
{
    auto client = MakeClient();
    EXPECT_FALSE(client.GetPropertyInfo("NOT_A_REAL_PROPERTY").has_value());
}

// A value stored under a non-AOSP name is real to the mock but unresolvable in
// the AOSP table, so id is 0 and the type is unknown while the entry exists.
// This is the case that proves "exists" and "is a known name" are independent.
TEST(VhalMockClient, GetPropertyInfoForANonAospNameExistsWithNoId)
{
    auto client = MakeClient();
    ASSERT_TRUE(client.SetProperty(CarProperty{"CABIN_TEMP", 21.5f}));

    const auto info = client.GetPropertyInfo("CABIN_TEMP");
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->id, 0);
    EXPECT_EQ(info->type, CarPropertyType::None);
    EXPECT_TRUE(info->IsReadable());
    EXPECT_TRUE(info->IsWritable());
}

TEST(VhalMockClient, PropertyChangedHandlerIsInvokedOnSimulatedChange)
{
    auto client = MakeClient();

    CarProperty observed;
    int callCount = 0;
    const int id = client.AddPropertyChangedHandler("EV_BATTERY_LEVEL", [&](const CarProperty& p) {
        observed = p;
        ++callCount;
    });

    client.SimulatePropertyChange(CarProperty{"EV_BATTERY_LEVEL", 90.0f});

    EXPECT_EQ(callCount, 1);
    EXPECT_EQ(observed.name, "EV_BATTERY_LEVEL");
    EXPECT_FLOAT_EQ(observed.Get<float>().value_or(-1.f), 90.0f);

    // A change to an unrelated property must not invoke this handler.
    client.SimulatePropertyChange(CarProperty{"EV_CHARGE_STATE", int32_t(1)});
    EXPECT_EQ(callCount, 1);

    client.RemovePropertyChangedHandler(id);
}

TEST(VhalMockClient, RemovedHandlerIsNotInvoked)
{
    auto client = MakeClient();

    int callCount = 0;
    const int id = client.AddPropertyChangedHandler("EV_BATTERY_LEVEL", [&](const CarProperty&) { ++callCount; });
    client.RemovePropertyChangedHandler(id);

    client.SimulatePropertyChange(CarProperty{"EV_BATTERY_LEVEL", 90.0f});
    EXPECT_EQ(callCount, 0);
}

} // namespace
