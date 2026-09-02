// SPDX-FileCopyrightText: 2026 Vector Informatik GmbH
// SPDX-License-Identifier: MIT
//
// End-to-end RPC tests: a faked CANoe consumer (SkParticipant with RpcClients)
// drives the real VhalSilKitAdapter (backed by a VhalMockClient) over a live,
// in-process SIL Kit registry. This verifies the GetProperty/SetProperty
// round-trips through the bridge and locks in the wire/label/function-name
// contract that CANoe relies on.
//
// SetUp() ends with a discovery barrier, so every test below runs against a
// settled system and asserts on logic alone. Nothing here waits out a duration
// or treats a slow CI runner as a failure; see "Timing discipline" in
// SkParticipant.hpp.
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "silkit/services/rpc/all.hpp"
#include "silkit/util/Span.hpp"
#include "silkit/util/serdes/Deserializer.hpp"
#include "silkit/util/serdes/Serializer.hpp"

#include "SkParticipant.hpp"
#include "TestHarness.hpp"
#include "VhalMockClient.hpp"
#include "VhalSilKitAdapter.hpp"

namespace
{
namespace Rpc = SilKit::Services::Rpc;
namespace SerDes = SilKit::Util::SerDes;
using SilKit::Util::ToSpan;
using test::SkParticipant;

constexpr auto kVirtualNetwork = "Default";
constexpr auto kNamespace = "VhalSilkit";
constexpr auto kInstance = "vhalProperties";

// Build the RpcSpec a CANoe-generated consumer uses: the bare method name as the
// function name (the matching key) plus the three Mandatory labels. Defaults
// mirror VhalSilKitAdapter::MakeRpcSpec / sil-adapter-builder.
auto MakeClientSpec(const std::string& functionName, const std::string& ns = kNamespace,
                    const std::string& instance = kInstance, const std::string& vnet = kVirtualNetwork)
    -> Rpc::RpcSpec
{
    Rpc::RpcSpec spec{functionName, SerDes::MediaTypeRpc()};
    spec.AddLabel("VirtualNetwork", vnet, SilKit::Services::MatchingLabel::Kind::Mandatory);
    spec.AddLabel("Namespace", ns, SilKit::Services::MatchingLabel::Kind::Mandatory);
    spec.AddLabel("Instance", instance, SilKit::Services::MatchingLabel::Kind::Mandatory);
    return spec;
}

// Request encoders - bare fields, exactly as the generated TypeLib serializer emits.
auto GetReq(const std::string& name) -> std::vector<uint8_t>
{
    SerDes::Serializer s;
    s.Serialize(name);
    return s.ReleaseBuffer();
}
auto SetInt32Req(const std::string& name, int32_t value) -> std::vector<uint8_t>
{
    SerDes::Serializer s;
    s.Serialize(name);
    s.Serialize(value, 32);
    return s.ReleaseBuffer();
}
auto SetFloatReq(const std::string& name, float value) -> std::vector<uint8_t>
{
    SerDes::Serializer s;
    s.Serialize(name);
    s.Serialize(value);
    return s.ReleaseBuffer();
}
auto ParseInt32(const std::vector<uint8_t>& bytes) -> int32_t
{
    SerDes::Deserializer d{bytes};
    return d.Deserialize<int32_t>(32);
}
auto ParseFloat(const std::vector<uint8_t>& bytes) -> float
{
    SerDes::Deserializer d{bytes};
    return d.Deserialize<float>();
}

class AdapterRpcTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        adapter_.Init(registry_.Uri(), "VhalBridge", /*config*/ "", /*log*/ "Warn",
                      /*useSynchronized*/ false, std::make_unique<VhalMockClient>("mock://test"),
                      kVirtualNetwork, kNamespace, kInstance);
        adapter_.Start();

        canoe_ = std::make_unique<SkParticipant>("canoe-vhal-test", registry_.Uri());
        // Canonical (controller) name carries the full path like CANoe; the RpcSpec
        // function name (bare method) is what actually matches.
        getClient_ = canoe_->AddRpcClient("Default::VhalSilkit::vhalProperties.GetProperty",
                                          MakeClientSpec("GetProperty"));
        setClient_ = canoe_->AddRpcClient("Default::VhalSilkit::vhalProperties.SetProperty",
                                          MakeClientSpec("SetProperty"));
        getFloatClient_ = canoe_->AddRpcClient("Default::VhalSilkit::vhalProperties.GetPropertyFloat",
                                               MakeClientSpec("GetPropertyFloat"));
        setFloatClient_ = canoe_->AddRpcClient("Default::VhalSilkit::vhalProperties.SetPropertyFloat",
                                               MakeClientSpec("SetPropertyFloat"));
        // Created here, in the same discovery round as the well-named clients, so
        // that WrongFunctionNameDoesNotMatch compares like with like. Distinct
        // canonical (controller) name - it must be unique per participant and does
        // not affect matching; only the RpcSpec function name does.
        badClient_ = canoe_->AddRpcClient("wrong-function-name-probe",
                                          MakeClientSpec("VhalSilkit::vhalProperties.GetProperty"));

        // Discovery barrier. Everything below this point is a settled system, so
        // no individual test needs a retry or a duration of its own: a test that
        // sees ServerNotReachable is seeing a contract violation, not a slow
        // runner. Both waits are on logical conditions (see SkParticipant.hpp).
        ASSERT_TRUE(canoe_->WaitUntilConnected("VhalBridge"))
            << "the adapter participant never joined the session";
        const auto probe = canoe_->AwaitServerReachable(getClient_, ToSpan(GetReq("EV_BATTERY_LEVEL")));
        ASSERT_EQ(probe.status, Rpc::RpcCallStatus::Success)
            << "the adapter's RPC servers were never matched by a correctly specified client";
    }

    void TearDown() override
    {
        canoe_.reset();
        adapter_.Stop();
        adapter_.Shutdown();
    }

    test::LiveRegistry registry_;
    VhalSilKitAdapter adapter_;
    std::unique_ptr<SkParticipant> canoe_;
    Rpc::IRpcClient* getClient_{nullptr};
    Rpc::IRpcClient* setClient_{nullptr};
    Rpc::IRpcClient* getFloatClient_{nullptr};
    Rpc::IRpcClient* setFloatClient_{nullptr};
    Rpc::IRpcClient* badClient_{nullptr};
};

TEST_F(AdapterRpcTest, GetPropertyReturnsMockValue)
{
    const auto result = canoe_->Call(getClient_, ToSpan(GetReq("EV_BATTERY_LEVEL")));
    ASSERT_EQ(result.status, Rpc::RpcCallStatus::Success);
    EXPECT_EQ(ParseInt32(result.data), 75); // seeded by VhalMockClient
}

TEST_F(AdapterRpcTest, SetThenGetRoundTrip)
{
    const auto setResult = canoe_->Call(setClient_, ToSpan(SetInt32Req("EV_BATTERY_LEVEL", 85)));
    ASSERT_EQ(setResult.status, Rpc::RpcCallStatus::Success);
    EXPECT_TRUE(setResult.data.empty()); // void method -> empty response

    const auto getResult = canoe_->Call(getClient_, ToSpan(GetReq("EV_BATTERY_LEVEL")));
    ASSERT_EQ(getResult.status, Rpc::RpcCallStatus::Success);
    EXPECT_EQ(ParseInt32(getResult.data), 85);
}

TEST_F(AdapterRpcTest, SetThenGetFloatRoundTrip)
{
    const auto setResult = canoe_->Call(setFloatClient_, ToSpan(SetFloatReq("CABIN_TEMP", 21.5f)));
    ASSERT_EQ(setResult.status, Rpc::RpcCallStatus::Success);

    const auto getResult = canoe_->Call(getFloatClient_, ToSpan(GetReq("CABIN_TEMP")));
    ASSERT_EQ(getResult.status, Rpc::RpcCallStatus::Success);
    EXPECT_FLOAT_EQ(ParseFloat(getResult.data), 21.5f);
}

TEST_F(AdapterRpcTest, UnknownPropertyReturnsZero)
{
    const auto result = canoe_->Call(getClient_, ToSpan(GetReq("NOT_A_REAL_PROPERTY")));
    ASSERT_EQ(result.status, Rpc::RpcCallStatus::Success); // adapter answers with its error fallback
    EXPECT_EQ(ParseInt32(result.data), 0);
}

// Regression guard for the CANoe-interop bug: the RpcSpec function name is the
// matching key. A client that uses the full DO path as the function name (the
// old, broken behavior) must NOT match the adapter's server.
//
// "Does not match" cannot be proven by waiting - absence of a match is always
// indistinguishable from a match that has not happened yet. It is proven by
// ordering instead: SetUp() has already established that this participant
// matched a correctly specified client to the adapter, and the adapter creates
// all four of its RPC servers in Init(), before its lifecycle starts. So the
// adapter's complete server set is known here, badClient_ was announced in that
// same round, and its being unmatched is a conclusion rather than a deadline.
TEST_F(AdapterRpcTest, WrongFunctionNameDoesNotMatch)
{
    const auto result = canoe_->Call(badClient_, ToSpan(GetReq("EV_BATTERY_LEVEL")));
    EXPECT_EQ(result.status, Rpc::RpcCallStatus::ServerNotReachable);

    // ...while the correctly-named client on the same participant does match.
    const auto ok = canoe_->Call(getClient_, ToSpan(GetReq("EV_BATTERY_LEVEL")));
    ASSERT_EQ(ok.status, Rpc::RpcCallStatus::Success);
    EXPECT_EQ(ParseInt32(ok.data), 75);
}

} // namespace
