// SPDX-FileCopyrightText: 2026 Vector Informatik GmbH
// SPDX-License-Identifier: MIT
#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "VhalMockClient.hpp"

namespace
{
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

TEST(VhalMockClient, GetAllPropertiesReturnsSeededSet)
{
    auto client = MakeClient();
    const auto names = client.GetAllProperties();
    // Six properties are seeded in the constructor.
    EXPECT_EQ(names.size(), 6u);
    EXPECT_NE(std::find(names.begin(), names.end(), "EV_BATTERY_LEVEL"), names.end());
}

TEST(VhalMockClient, GetPropertyReturnsSeededValue)
{
    auto client = MakeClient();
    const auto prop = client.GetProperty("EV_BATTERY_LEVEL");
    ASSERT_TRUE(prop.has_value());
    ASSERT_TRUE(prop->Holds<int32_t>());
    EXPECT_EQ(prop->Get<int32_t>().value(), 75);
}

TEST(VhalMockClient, GetPropertyMissingReturnsNullopt)
{
    auto client = MakeClient();
    EXPECT_FALSE(client.GetProperty("NOT_A_REAL_PROPERTY").has_value());
}

TEST(VhalMockClient, SetPropertyUpdatesStoredValue)
{
    auto client = MakeClient();
    EXPECT_TRUE(client.SetProperty(CarProperty{"EV_BATTERY_LEVEL", int32_t(85)}));

    const auto prop = client.GetProperty("EV_BATTERY_LEVEL");
    ASSERT_TRUE(prop.has_value());
    EXPECT_EQ(prop->Get<int32_t>().value_or(-1), 85);
}

TEST(VhalMockClient, SetPropertyInsertsNewProperty)
{
    auto client = MakeClient();
    EXPECT_TRUE(client.SetProperty(CarProperty{"RANGE_REMAINING", int32_t(123)}));

    const auto prop = client.GetProperty("RANGE_REMAINING");
    ASSERT_TRUE(prop.has_value());
    EXPECT_EQ(prop->Get<int32_t>().value_or(-1), 123);
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

TEST(VhalMockClient, PropertyChangedHandlerIsInvokedOnSimulatedChange)
{
    auto client = MakeClient();

    CarProperty observed;
    int callCount = 0;
    const int id = client.AddPropertyChangedHandler("EV_BATTERY_LEVEL", [&](const CarProperty& p) {
        observed = p;
        ++callCount;
    });

    client.SimulatePropertyChange(CarProperty{"EV_BATTERY_LEVEL", int32_t(90)});

    EXPECT_EQ(callCount, 1);
    EXPECT_EQ(observed.name, "EV_BATTERY_LEVEL");
    EXPECT_EQ(observed.Get<int32_t>().value_or(-1), 90);

    // A change to an unrelated property must not invoke this handler.
    client.SimulatePropertyChange(CarProperty{"EV_CHARGE_STATE", int32_t(1)});
    EXPECT_EQ(callCount, 1);

    client.RemovePropertyChangedHandler(id);
}

TEST(VhalMockClient, RemovedHandlerIsNotInvoked)
{
    auto client = MakeClient();

    int callCount = 0;
    const int id =
        client.AddPropertyChangedHandler("EV_BATTERY_LEVEL", [&](const CarProperty&) { ++callCount; });
    client.RemovePropertyChangedHandler(id);

    client.SimulatePropertyChange(CarProperty{"EV_BATTERY_LEVEL", int32_t(90)});
    EXPECT_EQ(callCount, 0);
}

} // namespace
