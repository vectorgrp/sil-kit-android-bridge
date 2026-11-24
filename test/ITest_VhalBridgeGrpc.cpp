// SPDX-FileCopyrightText: 2026 Vector Informatik GmbH
// SPDX-License-Identifier: MIT
//
// The whole chain a CANoe test bench exercises, minus CANoe and minus the
// device: a faked consumer over SIL Kit -> the real VhalSilKitAdapter -> the
// real VhalGrpcClient -> a fake VHAL over gRPC.
//
// ITest_VhalSilKitAdapter covers the same SIL Kit surface against VhalMockClient
// and goes deeper into the RPC contract; this file exists to prove the two
// halves join up, so it stays thin. Depth on the gRPC side belongs in
// ITest_VhalGrpcClient.
//
// Until this existed, everything from VhalGrpcClient outwards was covered only
// by running the bridge against real hardware by hand. That is what made two
// wire-level bugs reach a device before anyone noticed.
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "silkit/util/Span.hpp"

#include "CanoeWire.hpp"
#include "FakeVehicleServer.hpp"
#include "SkParticipant.hpp"
#include "TestHarness.hpp"
#include "VhalGrpcClient.hpp"
#include "VhalSilKitAdapter.hpp"
#include "VhalSimpleApi.hpp"

namespace {

namespace Rpc = SilKit::Services::Rpc;
namespace vhal = android::hardware::automotive::vehicle::proto;
namespace Api = VhalSimpleApi;
using SilKit::Util::ToSpan;
using test::SkParticipant;

constexpr auto kInt32Name = "INFO_MODEL_YEAR";
constexpr auto kInt64Name = "ANDROID_EPOCH_TIME";
constexpr auto kFloatName = "EV_BATTERY_LEVEL";
constexpr auto kStringName = "INFO_VIN";
constexpr auto kBytesName = "STORAGE_ENCRYPTION_BINDING_SEED";
// Declared write-only by the fake below. A real VHAL has such properties, and
// nothing else in the suite drives WRITE-only access end to end.
constexpr auto kWriteOnlyName = "HVAC_TEMPERATURE_SET";
// A real AOSP float property that the fake never configures.
constexpr auto kUnconfiguredName = "TIRE_PRESSURE";
// VENDOR group | INT32 | GLOBAL: a property a VHAL may configure that has no
// entry in the compiled-in AOSP table.
constexpr int32_t kVendorId = 0x21400001;

const std::vector<uint8_t> kSeed{0xDE, 0xAD, 0xBE, 0xEF};

class BridgeGrpcTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Seed before Init: VhalSilKitAdapter::Init calls CheckHealth and
        // refuses to start if the VHAL is not usable.
        fake_ = std::make_unique<test::FakeVhal>();
        Server().SeedInt32(VehicleProperty_GetIdByName(kInt32Name), 2026);
        Server().SeedInt64(VehicleProperty_GetIdByName(kInt64Name), 1767225600000LL);
        Server().SeedFloat(VehicleProperty_GetIdByName(kFloatName), 88.5f);
        Server().SeedString(VehicleProperty_GetIdByName(kStringName), "SILKITVHALBRIDGE1");
        Server().SeedBytes(VehicleProperty_GetIdByName(kBytesName), kSeed);

        // Access has to be declared before Init: the adapter preloads the
        // property configuration table at startup, so a config seeded after
        // that point is intentionally not visible. That is the same ordering a
        // real VHAL imposes, where the table is fixed by the time it accepts
        // connections.
        Server().SeedConfig(VehicleProperty_GetIdByName(kFloatName), vhal::VehiclePropertyAccess::READ);
        Server().SeedConfig(VehicleProperty_GetIdByName(kInt32Name), vhal::VehiclePropertyAccess::READ_WRITE);
        Server().SeedConfig(VehicleProperty_GetIdByName(kWriteOnlyName), vhal::VehiclePropertyAccess::WRITE);
        Server().SeedConfig(kVendorId, vhal::VehiclePropertyAccess::READ_WRITE);

        adapter_.Init(registry_.Uri(), "VhalBridge", /*config*/ "", /*log*/ "Warn",
                      /*useSynchronized*/ false, std::make_unique<VhalGrpcClient>(fake_->Channel()),
                      Api::kVirtualNetwork, Api::kNamespace, Api::kInstance);
        adapter_.Start();

        canoe_ = std::make_unique<SkParticipant>("canoe-grpc-test", registry_.Uri());

        // Every controller before Start(): a controller created after the
        // lifecycle begins can miss SIL Kit's service discovery round.
        getInt32_ = canoe_->AddRpcClient("GetInt32", test::MakeClientSpec(Api::Method::kGetInt32));
        setInt32_ = canoe_->AddRpcClient("SetInt32", test::MakeClientSpec(Api::Method::kSetInt32));
        getInt64_ = canoe_->AddRpcClient("GetInt64", test::MakeClientSpec(Api::Method::kGetInt64));
        setInt64_ = canoe_->AddRpcClient("SetInt64", test::MakeClientSpec(Api::Method::kSetInt64));
        getFloat_ = canoe_->AddRpcClient("GetFloat", test::MakeClientSpec(Api::Method::kGetFloat));
        setFloat_ = canoe_->AddRpcClient("SetFloat", test::MakeClientSpec(Api::Method::kSetFloat));
        getString_ = canoe_->AddRpcClient("GetString", test::MakeClientSpec(Api::Method::kGetString));
        setString_ = canoe_->AddRpcClient("SetString", test::MakeClientSpec(Api::Method::kSetString));
        getBytes_ = canoe_->AddRpcClient("GetBytes", test::MakeClientSpec(Api::Method::kGetBytes));
        setBytes_ = canoe_->AddRpcClient("SetBytes", test::MakeClientSpec(Api::Method::kSetBytes));
        propertyStatus_ =
            canoe_->AddRpcClient("GetPropertyStatus", test::MakeClientSpec(Api::Method::kGetPropertyStatus));

        canoe_->Start();
        ASSERT_TRUE(canoe_->WaitUntilConnected("VhalBridge"));

        // A discovery barrier, so every case below runs against a settled system
        // and asserts on logic alone.
        const auto probe = canoe_->AwaitServerReachable(getFloat_, ToSpan(test::GetReq(kFloatName)));
        ASSERT_EQ(probe.status, Rpc::RpcCallStatus::Success)
            << (probe.harnessTimeout ? "the probe call never completed at all (harness watchdog)"
                                     : "the adapter's RPC servers were never matched");
    }

    void TearDown() override
    {
        canoe_.reset();
        adapter_.Stop();
        adapter_.Shutdown();
    }

    auto Server() -> test::FakeVehicleServer&
    {
        return fake_->Service();
    }

    test::LiveRegistry registry_;
    std::unique_ptr<test::FakeVhal> fake_;
    VhalSilKitAdapter adapter_;
    std::unique_ptr<SkParticipant> canoe_;

    Rpc::IRpcClient* getInt32_{};
    Rpc::IRpcClient* setInt32_{};
    Rpc::IRpcClient* getInt64_{};
    Rpc::IRpcClient* setInt64_{};
    Rpc::IRpcClient* getFloat_{};
    Rpc::IRpcClient* setFloat_{};
    Rpc::IRpcClient* getString_{};
    Rpc::IRpcClient* setString_{};
    Rpc::IRpcClient* getBytes_{};
    Rpc::IRpcClient* setBytes_{};
    Rpc::IRpcClient* propertyStatus_{};
};

TEST_F(BridgeGrpcTest, ReadsEachTypeThroughTheWholeChain)
{
    const auto i32 = canoe_->Call(getInt32_, ToSpan(test::GetReq(kInt32Name)));
    ASSERT_EQ(i32.status, Rpc::RpcCallStatus::Success);
    EXPECT_EQ(test::ParseInt32(i32.data), 2026);

    const auto i64 = canoe_->Call(getInt64_, ToSpan(test::GetReq(kInt64Name)));
    ASSERT_EQ(i64.status, Rpc::RpcCallStatus::Success);
    EXPECT_EQ(test::ParseInt64(i64.data), 1767225600000LL);

    const auto f32 = canoe_->Call(getFloat_, ToSpan(test::GetReq(kFloatName)));
    ASSERT_EQ(f32.status, Rpc::RpcCallStatus::Success);
    EXPECT_FLOAT_EQ(test::ParseFloat(f32.data), 88.5f);

    const auto str = canoe_->Call(getString_, ToSpan(test::GetReq(kStringName)));
    ASSERT_EQ(str.status, Rpc::RpcCallStatus::Success);
    EXPECT_EQ(test::ParseString(str.data), "SILKITVHALBRIDGE1");

    const auto bytes = canoe_->Call(getBytes_, ToSpan(test::GetReq(kBytesName)));
    ASSERT_EQ(bytes.status, Rpc::RpcCallStatus::Success);
    EXPECT_EQ(test::ParseBytes(bytes.data), kSeed);
}

TEST_F(BridgeGrpcTest, WritesEachTypeThroughTheWholeChain)
{
    const auto setI32 = canoe_->Call(setInt32_, ToSpan(test::SetInt32Req(kInt32Name, 2030)));
    ASSERT_EQ(setI32.status, Rpc::RpcCallStatus::Success);
    const auto i32 = canoe_->Call(getInt32_, ToSpan(test::GetReq(kInt32Name)));
    ASSERT_EQ(i32.status, Rpc::RpcCallStatus::Success);
    EXPECT_EQ(test::ParseInt32(i32.data), 2030);

    // Beyond 32 bits on purpose: a truncation anywhere in the chain shows here.
    const auto setI64 = canoe_->Call(setInt64_, ToSpan(test::SetInt64Req(kInt64Name, 1800000000000LL)));
    ASSERT_EQ(setI64.status, Rpc::RpcCallStatus::Success);
    const auto i64 = canoe_->Call(getInt64_, ToSpan(test::GetReq(kInt64Name)));
    ASSERT_EQ(i64.status, Rpc::RpcCallStatus::Success);
    EXPECT_EQ(test::ParseInt64(i64.data), 1800000000000LL);

    const auto setF32 = canoe_->Call(setFloat_, ToSpan(test::SetFloatReq(kFloatName, 42.25f)));
    ASSERT_EQ(setF32.status, Rpc::RpcCallStatus::Success);
    const auto f32 = canoe_->Call(getFloat_, ToSpan(test::GetReq(kFloatName)));
    ASSERT_EQ(f32.status, Rpc::RpcCallStatus::Success);
    EXPECT_FLOAT_EQ(test::ParseFloat(f32.data), 42.25f);

    const auto setStr = canoe_->Call(setString_, ToSpan(test::SetStringReq(kStringName, "TESTVIN0000000001")));
    ASSERT_EQ(setStr.status, Rpc::RpcCallStatus::Success);
    const auto str = canoe_->Call(getString_, ToSpan(test::GetReq(kStringName)));
    ASSERT_EQ(str.status, Rpc::RpcCallStatus::Success);
    EXPECT_EQ(test::ParseString(str.data), "TESTVIN0000000001");
}

// The end-to-end form of the bug that shipped: written into the wrong protobuf
// field, this came back from a real VHAL as
// "expects BYTES, not INT32_VEC" and never reached the device.
TEST_F(BridgeGrpcTest, WritesBytesIntoTheRightProtobufFieldAndReadsThemBack)
{
    const std::vector<uint8_t> payload{0xCA, 0xFE, 0xBA, 0xBE};

    const auto setResult = canoe_->Call(setBytes_, ToSpan(test::SetBytesReq(kBytesName, payload)));
    ASSERT_EQ(setResult.status, Rpc::RpcCallStatus::Success);

    const auto requests = Server().ReceivedSetRequests();
    ASSERT_EQ(requests.size(), 1u);
    ASSERT_EQ(requests.at(0).requests_size(), 1);
    const auto sent = requests.at(0).requests(0).value();
    EXPECT_EQ(sent.byte_values(), std::string(payload.begin(), payload.end()));
    EXPECT_EQ(sent.int32_values_size(), 0);

    const auto readBack = canoe_->Call(getBytes_, ToSpan(test::GetReq(kBytesName)));
    ASSERT_EQ(readBack.status, Rpc::RpcCallStatus::Success);
    EXPECT_EQ(test::ParseBytes(readBack.data), payload);
}

// The VCDL methods carry no status out-parameter, so a refused read can only be
// answered with a default-constructed value. What the wire cannot say, the
// bridge log does - this pins the documented behaviour, not an accident.
TEST_F(BridgeGrpcTest, RefusedReadIsAnsweredWithTheDocumentedDefault)
{
    Server().SetReadStatus(VehicleProperty_GetIdByName(kInt32Name), vhal::StatusCode::NOT_AVAILABLE);

    const auto result = canoe_->Call(getInt32_, ToSpan(test::GetReq(kInt32Name)));
    ASSERT_EQ(result.status, Rpc::RpcCallStatus::Success);
    EXPECT_EQ(test::ParseInt32(result.data), 0);
}

TEST_F(BridgeGrpcTest, UnknownPropertyIsAnsweredWithTheDocumentedDefault)
{
    const auto result = canoe_->Call(getInt32_, ToSpan(test::GetReq("NOT_A_REAL_PROPERTY")));
    ASSERT_EQ(result.status, Rpc::RpcCallStatus::Success);
    EXPECT_EQ(test::ParseInt32(result.data), 0);
}

// Deliberately not tested here: a VHAL answering out of order. The adapter reads
// one property per RPC, so a single-element result list cannot be reordered and
// any such test would pass against the broken code too. The misattribution is
// guarded where it can actually happen, over the multi-name GetProperties, in
// ITest_VhalGrpcClient.

// A VHAL that goes away mid-session must not take the bridge down with it: the
// adapter catches, logs, and keeps serving.
TEST_F(BridgeGrpcTest, VhalTransportFailureIsReportedAsTheDefaultAndTheBridgeSurvives)
{
    Server().SetTransportStatus(grpc::StatusCode::UNAVAILABLE);

    const auto failed = canoe_->Call(getInt32_, ToSpan(test::GetReq(kInt32Name)));
    ASSERT_EQ(failed.status, Rpc::RpcCallStatus::Success);
    EXPECT_EQ(test::ParseInt32(failed.data), 0);

    // Still serving once the VHAL recovers.
    Server().SetTransportStatus(grpc::StatusCode::OK);
    const auto recovered = canoe_->Call(getInt32_, ToSpan(test::GetReq(kInt32Name)));
    ASSERT_EQ(recovered.status, Rpc::RpcCallStatus::Success);
    EXPECT_EQ(test::ParseInt32(recovered.data), 2026);
}

// ---- GetPropertyStatus, end to end ----
//
// The only place the VHAL's declared access travels the full chain: fake VHAL
// config table -> gRPC stream -> VhalGrpcClient cache -> adapter -> SIL Kit ->
// consumer. Every layer below has its own coverage; what is only testable here
// is that the access survives all of them.
TEST_F(BridgeGrpcTest, PropertyStatusCarriesTheVhalDeclaredAccessToTheConsumer)
{
    const auto result = canoe_->Call(propertyStatus_, ToSpan(test::GetReq(kFloatName)));
    ASSERT_EQ(result.status, Rpc::RpcCallStatus::Success);

    const auto view = test::ParsePropertyStatus(result.data);
    EXPECT_TRUE(view.exists);
    EXPECT_TRUE(view.readable);
    EXPECT_FALSE(view.writable) << "the VHAL declared this property read-only";
    EXPECT_TRUE(view.knownName);
    EXPECT_EQ(view.valueType, static_cast<int8_t>(Api::TypeCode::Float));
}

// Everywhere else in the suite a property is either read/write or read-only,
// and VhalMockClient cannot express this at all, so without this case the
// writable field is never seen on its own.
TEST_F(BridgeGrpcTest, PropertyStatusCarriesWriteOnlyAccess)
{
    const auto result = canoe_->Call(propertyStatus_, ToSpan(test::GetReq(kWriteOnlyName)));
    ASSERT_EQ(result.status, Rpc::RpcCallStatus::Success);

    const auto view = test::ParsePropertyStatus(result.data);
    EXPECT_TRUE(view.exists);
    EXPECT_TRUE(view.writable);
    EXPECT_FALSE(view.readable) << "the VHAL declared this property write-only";
}

// knownName means "the compiled-in AOSP table has this name", and a vendor
// property is the only input where that differs from "the backend gave it an
// id". VhalGrpcClient reports the VHAL's own id, which is non-zero here.
TEST_F(BridgeGrpcTest, PropertyStatusDoesNotClaimAVendorPropertyIsAKnownAospName)
{
    const auto vendorName = VehicleProperty_GetNameById(kVendorId);
    ASSERT_EQ(VehicleProperty_GetIdByName(vendorName), 0) << "fixture assumption: not an AOSP name";

    const auto result = canoe_->Call(propertyStatus_, ToSpan(test::GetReq(vendorName)));
    ASSERT_EQ(result.status, Rpc::RpcCallStatus::Success);

    const auto view = test::ParsePropertyStatus(result.data);
    EXPECT_TRUE(view.exists) << "the VHAL does configure it";
    EXPECT_FALSE(view.knownName) << "but AOSP has never heard of it";
    EXPECT_EQ(view.valueType, static_cast<int8_t>(Api::TypeCode::Int32)) << "the vendor id still encodes its type";
}

// A real property this vehicle does not implement: known name plus its
// accessor, and nothing else. The branch that builds it lives in the adapter
// and never runs in a unit test.
TEST_F(BridgeGrpcTest, PropertyStatusForAKnownButUnconfiguredPropertyIsNameAndTypeOnly)
{
    const auto result = canoe_->Call(propertyStatus_, ToSpan(test::GetReq(kUnconfiguredName)));
    ASSERT_EQ(result.status, Rpc::RpcCallStatus::Success);
    EXPECT_EQ(result.data, (std::vector<uint8_t>{0, 0, 0, 1, 3, 0}));
}

// The status must keep tracking the VHAL after the configuration table is
// cached. Ordered, not timed: the first call is what warms the cache, so a
// second call that still claims the property exists is reporting a vehicle that
// is no longer there.
TEST_F(BridgeGrpcTest, PropertyStatusStopsClaimingAPropertyOnceTheVhalIsGone)
{
    const auto before = canoe_->Call(propertyStatus_, ToSpan(test::GetReq(kFloatName)));
    ASSERT_EQ(before.status, Rpc::RpcCallStatus::Success);
    ASSERT_TRUE(test::ParsePropertyStatus(before.data).exists) << "precondition: the table is cached";

    Server().SetTransportStatus(grpc::StatusCode::UNAVAILABLE);

    const auto after = canoe_->Call(propertyStatus_, ToSpan(test::GetReq(kFloatName)));
    ASSERT_EQ(after.status, Rpc::RpcCallStatus::Success);
    const auto view = test::ParsePropertyStatus(after.data);
    EXPECT_TRUE(view.queryFailed) << "a cached table must not outlive the VHAL it describes";
    EXPECT_FALSE(view.exists);
    EXPECT_TRUE(view.knownName);
}

// A VHAL that cannot be reached must not be reported as a VHAL with no
// properties: an all-false answer means "no such property", and a CANoe test
// would then blame its own property name.
TEST_F(BridgeGrpcTest, PropertyStatusReportsAnUnreachableVhalAsAFailureNotAnAbsence)
{
    Server().SetTransportStatus(grpc::StatusCode::UNAVAILABLE);

    const auto result = canoe_->Call(propertyStatus_, ToSpan(test::GetReq(kFloatName)));
    ASSERT_EQ(result.status, Rpc::RpcCallStatus::Success) << "the bridge answers rather than dropping the call";

    const auto view = test::ParsePropertyStatus(result.data);
    EXPECT_TRUE(view.queryFailed);
    EXPECT_FALSE(view.exists);
    EXPECT_TRUE(view.knownName) << "the AOSP table is answerable with no VHAL at all";

    Server().SetTransportStatus(grpc::StatusCode::OK);
    const auto recovered = canoe_->Call(propertyStatus_, ToSpan(test::GetReq(kFloatName)));
    ASSERT_EQ(recovered.status, Rpc::RpcCallStatus::Success);
    const auto recoveredView = test::ParsePropertyStatus(recovered.data);
    EXPECT_FALSE(recoveredView.queryFailed);
    EXPECT_TRUE(recoveredView.exists);
}

} // namespace
