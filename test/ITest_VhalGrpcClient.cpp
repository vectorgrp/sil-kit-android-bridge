// SPDX-FileCopyrightText: 2026 Vector Informatik GmbH
// SPDX-License-Identifier: MIT
//
// Integration tests for VhalGrpcClient against a programmable fake VHAL
// (test/FakeVehicleServer.hpp) over gRPC's in-process transport. This is the
// layer Test_VhalGrpcClient.cpp cannot reach: that file covers VhalGrpcConvert,
// the pure translation, while everything here needs a stub and a server.
//
// The write-path cases assert on what the server actually received rather than
// on the return value, because the bug this guards against - a BYTES value
// written into int32_values - returned success at every layer and only showed up
// as a VHAL error message on a real device.
//
// No sockets: the in-process transport means no port allocation, no bind race
// and nothing for a loaded CI runner to make flaky.
#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "FakeVehicleServer.hpp"
#include "VhalGrpcClient.hpp"
#include "VhalGrpcConvert.hpp"

namespace {

namespace vhal = android::hardware::automotive::vehicle::proto;

// One property per type the VhalSimple API models, resolved from the AOSP table
// so the declared type bits are real: the client type-checks against them.
const int32_t kInt32Id = VehicleProperty_GetIdByName("INFO_MODEL_YEAR");
const int32_t kInt64Id = VehicleProperty_GetIdByName("ANDROID_EPOCH_TIME");
const int32_t kFloatId = VehicleProperty_GetIdByName("EV_BATTERY_LEVEL");
const int32_t kStringId = VehicleProperty_GetIdByName("INFO_VIN");
const int32_t kBytesId = VehicleProperty_GetIdByName("STORAGE_ENCRYPTION_BINDING_SEED");

constexpr auto kInt32Name = "INFO_MODEL_YEAR";
constexpr auto kInt64Name = "ANDROID_EPOCH_TIME";
constexpr auto kFloatName = "EV_BATTERY_LEVEL";
constexpr auto kStringName = "INFO_VIN";
constexpr auto kBytesName = "STORAGE_ENCRYPTION_BINDING_SEED";

const std::vector<uint8_t> kSeed{0xDE, 0xAD, 0xBE, 0xEF};

class GrpcClientTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        fake_ = std::make_unique<test::FakeVhal>();
        client_ = std::make_unique<VhalGrpcClient>(fake_->Channel());

        // Distinctive values, so a wrong read looks wrong rather than looking
        // like a plausible zero.
        Server().SeedInt32(kInt32Id, 2026);
        Server().SeedInt64(kInt64Id, 1767225600000LL);
        Server().SeedFloat(kFloatId, 88.5f);
        Server().SeedString(kStringId, "WVWZZZ1JZXW000001");
        Server().SeedBytes(kBytesId, kSeed);
    }

    auto Server() -> test::FakeVehicleServer&
    {
        return fake_->Service();
    }

    //! The single VehiclePropValue the client sent in its only SetValues call.
    auto SentValue() -> vhal::VehiclePropValue
    {
        const auto requests = Server().ReceivedSetRequests();
        EXPECT_EQ(requests.size(), 1u);
        EXPECT_EQ(requests.at(0).requests_size(), 1);
        return requests.at(0).requests(0).value();
    }

    std::unique_ptr<test::FakeVhal> fake_;
    std::unique_ptr<VhalGrpcClient> client_;
};

// ---- Read path ----

TEST_F(GrpcClientTest, ReadsEachTypeTheApiModels)
{
    const auto i32 = client_->GetProperty(kInt32Name);
    ASSERT_TRUE(i32.has_value());
    EXPECT_EQ(i32->name, kInt32Name);
    EXPECT_EQ(i32->Get<int32_t>().value(), 2026);

    const auto i64 = client_->GetProperty(kInt64Name);
    ASSERT_TRUE(i64.has_value());
    EXPECT_EQ(i64->Get<int64_t>().value(), 1767225600000LL);

    const auto f32 = client_->GetProperty(kFloatName);
    ASSERT_TRUE(f32.has_value());
    EXPECT_FLOAT_EQ(f32->Get<float>().value(), 88.5f);

    const auto str = client_->GetProperty(kStringName);
    ASSERT_TRUE(str.has_value());
    EXPECT_EQ(str->Get<std::string>().value(), "WVWZZZ1JZXW000001");

    const auto bytes = client_->GetProperty(kBytesName);
    ASSERT_TRUE(bytes.has_value());
    EXPECT_EQ(bytes->Get<std::vector<uint8_t>>().value(), kSeed);
}

// A refused read carries a value field anyway. Converting it would hand the
// caller a number indistinguishable from a real reading.
TEST_F(GrpcClientTest, RefusedReadYieldsNoValueEvenThoughOneWasAttached)
{
    Server().SetReadStatus(kInt32Id, vhal::StatusCode::NOT_AVAILABLE);
    EXPECT_FALSE(client_->GetProperty(kInt32Name).has_value());
}

TEST_F(GrpcClientTest, UnknownPropertyYieldsNoValue)
{
    Server().SetReadStatus(kInt32Id, vhal::StatusCode::INVALID_ARG);
    EXPECT_FALSE(client_->GetProperty(kInt32Name).has_value());
}

// A VHAL promises nothing about the order it answers in. Attributing result i to
// request i means GetProperty("A") can return B's value - and because the
// adapter checks only the type, a same-typed mix-up reaches CANoe looking
// correct. Silent wrong data is the worst failure this bridge has.
TEST_F(GrpcClientTest, ResultsAreMatchedToTheRequestedNameNotToTheirPosition)
{
    Server().SetResultOrder(test::ResultOrder::Reversed);

    const auto results = client_->GetProperties({kInt32Name, kInt64Name});
    ASSERT_EQ(results.size(), 2u);
    ASSERT_TRUE(results[0].has_value());
    ASSERT_TRUE(results[1].has_value());
    EXPECT_EQ(results[0]->name, kInt32Name);
    EXPECT_EQ(results[1]->name, kInt64Name);
    EXPECT_EQ(results[0]->Get<int32_t>().value(), 2026);
    EXPECT_EQ(results[1]->Get<int64_t>().value(), 1767225600000LL);
}

// A VHAL may answer fewer results than it was asked for. The gap has to stay in
// place, or every later entry is attributed to the wrong name.
TEST_F(GrpcClientTest, MissingResultLeavesItsOwnSlotEmpty)
{
    Server().SetResultOrder(test::ResultOrder::DropFirst);

    const auto results = client_->GetProperties({kInt32Name, kInt64Name});
    ASSERT_EQ(results.size(), 2u);
    EXPECT_FALSE(results[0].has_value());
    ASSERT_TRUE(results[1].has_value());
    EXPECT_EQ(results[1]->name, kInt64Name);
    EXPECT_EQ(results[1]->Get<int64_t>().value(), 1767225600000LL);
}

TEST_F(GrpcClientTest, SameNameTwiceIsAnsweredTwice)
{
    const auto results = client_->GetProperties({kInt32Name, kInt32Name});
    ASSERT_EQ(results.size(), 2u);
    ASSERT_TRUE(results[0].has_value());
    ASSERT_TRUE(results[1].has_value());
    EXPECT_EQ(results[0]->Get<int32_t>().value(), 2026);
    EXPECT_EQ(results[1]->Get<int32_t>().value(), 2026);
}

TEST_F(GrpcClientTest, UnknownPropertyNameThrows)
{
    EXPECT_THROW(client_->GetProperty("NOT_A_REAL_PROPERTY"), VhalClientError);
}

TEST_F(GrpcClientTest, ReadTransportFailureThrows)
{
    Server().SetTransportStatus(grpc::StatusCode::UNAVAILABLE);
    EXPECT_THROW(client_->GetProperty(kInt32Name), VhalClientError);
}

// ---- Write path: assert on what reached the wire ----

// The regression test for the bug that shipped: a vector<uint8_t> fell into the
// integral-vector branch and the VHAL answered "expects BYTES, not INT32_VEC".
TEST_F(GrpcClientTest, BytesAreWrittenIntoByteValues)
{
    const std::vector<uint8_t> payload{0xCA, 0xFE, 0xBA, 0xBE};
    ASSERT_TRUE(client_->SetProperty(CarProperty{kBytesName, payload}));

    const auto sent = SentValue();
    EXPECT_EQ(sent.prop(), kBytesId);
    EXPECT_EQ(sent.byte_values(), std::string(payload.begin(), payload.end()));
    EXPECT_EQ(sent.int32_values_size(), 0);
}

TEST_F(GrpcClientTest, StringIsWrittenIntoStringValue)
{
    ASSERT_TRUE(client_->SetProperty(CarProperty{kStringName, std::string{"TESTVIN0000000001"}}));

    const auto sent = SentValue();
    EXPECT_EQ(sent.prop(), kStringId);
    EXPECT_EQ(sent.string_value(), "TESTVIN0000000001");
    EXPECT_TRUE(sent.byte_values().empty());
}

TEST_F(GrpcClientTest, Int32IsWrittenIntoInt32Values)
{
    ASSERT_TRUE(client_->SetProperty(CarProperty{kInt32Name, int32_t{2030}}));

    const auto sent = SentValue();
    ASSERT_EQ(sent.int32_values_size(), 1);
    EXPECT_EQ(sent.int32_values(0), 2030);
    EXPECT_EQ(sent.int64_values_size(), 0);
}

// Beyond 32 bits on purpose: a truncation in the field selection shows up here.
TEST_F(GrpcClientTest, Int64IsWrittenIntoInt64Values)
{
    ASSERT_TRUE(client_->SetProperty(CarProperty{kInt64Name, int64_t{1800000000000LL}}));

    const auto sent = SentValue();
    ASSERT_EQ(sent.int64_values_size(), 1);
    EXPECT_EQ(sent.int64_values(0), 1800000000000LL);
    EXPECT_EQ(sent.int32_values_size(), 0);
}

TEST_F(GrpcClientTest, FloatIsWrittenIntoFloatValues)
{
    ASSERT_TRUE(client_->SetProperty(CarProperty{kFloatName, 42.25f}));

    const auto sent = SentValue();
    ASSERT_EQ(sent.float_values_size(), 1);
    EXPECT_FLOAT_EQ(sent.float_values(0), 42.25f);
    EXPECT_EQ(sent.int32_values_size(), 0);
}

TEST_F(GrpcClientTest, WriteThenReadRoundTripsThroughTheServer)
{
    ASSERT_TRUE(client_->SetProperty(CarProperty{kFloatName, 42.25f}));

    const auto readBack = client_->GetProperty(kFloatName);
    ASSERT_TRUE(readBack.has_value());
    EXPECT_FLOAT_EQ(readBack->Get<float>().value(), 42.25f);
}

// The transport status only says the call arrived. A VHAL that accepts the call
// and refuses the write reports that per result.
TEST_F(GrpcClientTest, RefusedWriteThrowsEvenThoughTheCallSucceeded)
{
    Server().SetWriteStatus(kInt32Id, vhal::StatusCode::ACCESS_DENIED);
    EXPECT_THROW(client_->SetProperty(CarProperty{kInt32Name, int32_t{2030}}), VhalClientError);
}

TEST_F(GrpcClientTest, WriteTransportFailureThrows)
{
    Server().SetTransportStatus(grpc::StatusCode::UNAVAILABLE);
    EXPECT_THROW(client_->SetProperty(CarProperty{kInt32Name, int32_t{2030}}), VhalClientError);
}

TEST_F(GrpcClientTest, WritingAnUnknownPropertyReturnsFalseWithoutCallingTheServer)
{
    EXPECT_FALSE(client_->SetProperty(CarProperty{"NOT_A_REAL_PROPERTY", int32_t{1}}));
    EXPECT_TRUE(Server().ReceivedSetRequests().empty());
}

TEST_F(GrpcClientTest, WritingAnEmptyValueReturnsFalseWithoutCallingTheServer)
{
    CarProperty empty;
    empty.name = kInt32Name;
    EXPECT_FALSE(client_->SetProperty(empty));
    EXPECT_TRUE(Server().ReceivedSetRequests().empty());
}

// ---- The SetValues<T> templates ----
// These live in the header and carry their own copy of the type checking that
// ToPropValue now owns. SetProperty does not route through them, so without
// these cases that logic is reachable from no test at all.

TEST_F(GrpcClientTest, SetValuesTemplatePutsBytesIntoByteValues)
{
    const std::vector<uint8_t> payload{0x01, 0x02, 0x03};
    client_->SetValues(kBytesId, payload);

    const auto sent = SentValue();
    EXPECT_EQ(sent.byte_values(), std::string(payload.begin(), payload.end()));
    EXPECT_EQ(sent.int32_values_size(), 0);
}

TEST_F(GrpcClientTest, SetValuesTemplatePutsInt32IntoInt32Values)
{
    client_->SetValues(kInt32Id, int32_t{2031});

    const auto sent = SentValue();
    ASSERT_EQ(sent.int32_values_size(), 1);
    EXPECT_EQ(sent.int32_values(0), 2031);
}

TEST_F(GrpcClientTest, SetValuesTemplateRejectsAWrongTypeBeforeCallingTheServer)
{
    EXPECT_THROW(client_->SetValues(kFloatId, int32_t{1}), VhalClientError);
    EXPECT_TRUE(Server().ReceivedSetRequests().empty());
}

TEST_F(GrpcClientTest, SetValuesTemplateReportsARefusedWrite)
{
    Server().SetWriteStatus(kInt32Id, vhal::StatusCode::ACCESS_DENIED);
    EXPECT_THROW(client_->SetValues(kInt32Id, int32_t{2031}), VhalClientError);
}

// ---- Health ----

TEST_F(GrpcClientTest, HealthyServerReportsHealthy)
{
    EXPECT_TRUE(client_->CheckHealth());
}

// A VHAL that is up but not usable answers the call successfully and says so in
// the payload. VhalSilKitAdapter::Init refuses to start when CheckHealth is
// false, so this is the bridge's only startup gate on the VHAL being usable.
TEST_F(GrpcClientTest, UnhealthyStatusIsReportedAsUnhealthy)
{
    Server().SetHealthy(false);
    EXPECT_FALSE(client_->CheckHealth());
}

TEST_F(GrpcClientTest, HealthCheckFailsWhenTheTransportFails)
{
    Server().SetTransportStatus(grpc::StatusCode::UNAVAILABLE);
    EXPECT_FALSE(client_->CheckHealth());
}

// ---- Property configuration stream ----

TEST_F(GrpcClientTest, ListsTheNamesOfEveryConfiguredProperty)
{
    const auto names = client_->GetAllProperties();
    EXPECT_EQ(names.size(), 5u);
    EXPECT_NE(std::find(names.begin(), names.end(), kInt32Name), names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), kBytesName), names.end());
}

// A stream that dies partway leaves a short list that is indistinguishable from
// a complete one unless the final status is checked.
TEST_F(GrpcClientTest, TruncatedConfigStreamThrowsRatherThanReturningAShortList)
{
    Server().SetConfigStreamFailsAfter(2);
    EXPECT_THROW(client_->GetAllProperties(), VhalClientError);
}

TEST_F(GrpcClientTest, ConfigStreamTransportFailureThrows)
{
    Server().SetTransportStatus(grpc::StatusCode::UNAVAILABLE);
    EXPECT_THROW(client_->GetAllProperties(), VhalClientError);
}

// ---- Property configuration lookup, which backs GetPropertyStatus ----

TEST_F(GrpcClientTest, GetPropertyInfoReportsTheConfiguredAccess)
{
    Server().SeedConfig(kFloatId, vhal::VehiclePropertyAccess::READ_WRITE);

    const auto info = client_->GetPropertyInfo(kFloatName);
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->id, kFloatId);
    EXPECT_EQ(info->type, CarPropertyType::Float);
    EXPECT_TRUE(info->IsReadable());
    EXPECT_TRUE(info->IsWritable());
}

// The access has to survive the whole stream, not just the conversion: a
// read-only property reported as writable would send a caller into a SetFloat
// that the VHAL will always refuse.
TEST_F(GrpcClientTest, GetPropertyInfoReportsAReadOnlyPropertyAsNotWritable)
{
    Server().SeedConfig(kFloatId, vhal::VehiclePropertyAccess::READ);

    const auto info = client_->GetPropertyInfo(kFloatName);
    ASSERT_TRUE(info.has_value());
    EXPECT_TRUE(info->IsReadable());
    EXPECT_FALSE(info->IsWritable());
}

// A real VHAL configures properties it holds no current value for. The config
// table, not the value table, is what decides whether a property exists.
TEST_F(GrpcClientTest, GetPropertyInfoFindsAConfiguredPropertyWithNoValue)
{
    const auto id = VehicleProperty_GetIdByName("PERF_STEERING_ANGLE");
    Server().SeedConfig(id, vhal::VehiclePropertyAccess::READ);

    const auto info = client_->GetPropertyInfo("PERF_STEERING_ANGLE");
    ASSERT_TRUE(info.has_value());
    EXPECT_TRUE(info->IsReadable());
}

TEST_F(GrpcClientTest, GetPropertyInfoIsNulloptForAnUnconfiguredProperty)
{
    EXPECT_FALSE(client_->GetPropertyInfo("PERF_STEERING_ANGLE").has_value());
}

// An AOSP V3+ VHAL may state access per area and leave the top-level field at
// NONE. End to end, because the fallback is only useful if the area configs
// actually survive the stream.
TEST_F(GrpcClientTest, GetPropertyInfoUsesPerAreaAccessWhenThatIsAllTheVhalStates)
{
    Server().SeedAreaConfig(kFloatId, 0, vhal::VehiclePropertyAccess::READ);

    const auto info = client_->GetPropertyInfo(kFloatName);
    ASSERT_TRUE(info.has_value());
    EXPECT_TRUE(info->IsReadable());
}

// The table is streamed once. Proved by ordering rather than by timing: the
// first call establishes the cache, and only then is the transport broken. A
// second call that still answers can only be answering from memory.
TEST_F(GrpcClientTest, ThePropertyConfigTableIsStreamedOnlyOnce)
{
    Server().SeedConfig(kFloatId, vhal::VehiclePropertyAccess::READ_WRITE);
    ASSERT_TRUE(client_->GetPropertyInfo(kFloatName).has_value());

    Server().SetTransportStatus(grpc::StatusCode::UNAVAILABLE);

    const auto info = client_->GetPropertyInfo(kFloatName);
    ASSERT_TRUE(info.has_value()) << "a cached table must not need the server again";
    EXPECT_TRUE(info->IsWritable());
}

// A failure is not an absence. If the failed read were cached as an empty
// table, every later lookup would answer "no such property" - which is exactly
// the ambiguity GetPropertyStatus exists to remove, reintroduced one layer down.
TEST_F(GrpcClientTest, AFailedConfigReadIsRetriedRatherThanCachedAsAbsent)
{
    Server().SeedConfig(kFloatId, vhal::VehiclePropertyAccess::READ_WRITE);
    Server().SetTransportStatus(grpc::StatusCode::UNAVAILABLE);

    EXPECT_THROW(client_->GetPropertyInfo(kFloatName), VhalClientError);

    Server().SetTransportStatus(grpc::StatusCode::OK);

    const auto info = client_->GetPropertyInfo(kFloatName);
    ASSERT_TRUE(info.has_value()) << "the failure must not have been remembered";
    EXPECT_TRUE(info->IsReadable());
}

// Same rule for a stream that dies partway: a short table would report the
// properties past the truncation point as absent forever.
TEST_F(GrpcClientTest, ATruncatedConfigStreamIsNotCachedAsAShortTable)
{
    Server().SeedConfig(kFloatId, vhal::VehiclePropertyAccess::READ_WRITE);
    Server().SetConfigStreamFailsAfter(1);

    EXPECT_THROW(client_->GetPropertyInfo(kFloatName), VhalClientError);

    Server().SetConfigStreamFailsAfter(-1);

    EXPECT_TRUE(client_->GetPropertyInfo(kFloatName).has_value());
    EXPECT_EQ(client_->GetAllProperties().size(), 5u) << "the whole table, not the truncated prefix";
}

} // namespace
