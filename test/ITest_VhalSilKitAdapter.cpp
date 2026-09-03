// SPDX-FileCopyrightText: 2026 Vector Informatik GmbH
// SPDX-License-Identifier: MIT
//
// End-to-end RPC tests: a faked CANoe consumer (SkParticipant with RpcClients)
// drives the real VhalSilKitAdapter (backed by a VhalMockClient) over a live,
// in-process SIL Kit registry. This verifies the GetProperty/SetProperty
// round-trips through the bridge and locks in the wire/label/function-name
// contract that CANoe relies on. SetUp() creates every RpcClient before starting
// the consumer lifecycle - controllers created afterwards can miss SIL Kit's
// service discovery round - and ends with a discovery barrier, so every test
// below runs against a settled system and asserts on logic alone.
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "silkit/services/rpc/all.hpp"
#include "silkit/util/Span.hpp"
#include "silkit/util/serdes/Deserializer.hpp"
#include "silkit/util/serdes/Serializer.hpp"

#include "CanoeWire.hpp"
#include "SkParticipant.hpp"
#include "TestHarness.hpp"
#include "VhalMockClient.hpp"
#include "VhalSilKitAdapter.hpp"
#include "VhalSimpleApi.hpp"

namespace
{
namespace Rpc = SilKit::Services::Rpc;
using SilKit::Util::ToSpan;
using test::SkParticipant;

namespace Api = VhalSimpleApi;

// Taken from the contract header, not restated: a rename there must reach the
// adapter and this test together or the whole suite stops matching.
constexpr auto kVirtualNetwork = Api::kVirtualNetwork;
constexpr auto kNamespace = Api::kNamespace;
constexpr auto kInstance = Api::kInstance;

// The CANoe-side encoders, decoders and RpcSpec builder are shared with the
// other integration tests; see CanoeWire.hpp for why they are hand-written
// rather than taken from VhalSimpleApi.hpp.
using test::EmptyReq;
using test::GetReq;
using test::MakeClientSpec;
using test::ParseBytes;
using test::ParseFloat;
using test::ParseInt32;
using test::ParseInt64;
using test::ParseString;
using test::SetBytesReq;
using test::SetFloatReq;
using test::SetInt32Req;
using test::SetInt64Req;
using test::SetStringReq;

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
        const std::string path = std::string{kVirtualNetwork} + "::" + kNamespace + "::" + kInstance + ".";
        auto add = [&](const char* method) { return canoe_->AddRpcClient(path + method, MakeClientSpec(method)); };
        getClient_ = add(Api::Method::kGetInt32);
        setClient_ = add(Api::Method::kSetInt32);
        getInt64Client_ = add(Api::Method::kGetInt64);
        setInt64Client_ = add(Api::Method::kSetInt64);
        getFloatClient_ = add(Api::Method::kGetFloat);
        setFloatClient_ = add(Api::Method::kSetFloat);
        getStringClient_ = add(Api::Method::kGetString);
        setStringClient_ = add(Api::Method::kSetString);
        getBytesClient_ = add(Api::Method::kGetBytes);
        setBytesClient_ = add(Api::Method::kSetBytes);
        versionClient_ = add(Api::Method::kGetApiVersion);
        // Same discovery round as the well-named clients, so that
        // WrongFunctionNameDoesNotMatch compares like with like. The function name
        // here is the fully qualified path, which is the old broken behavior - it
        // has to track the rename or this guard silently stops testing anything.
        badClient_ = canoe_->AddRpcClient(
            "wrong-function-name-probe",
            MakeClientSpec(std::string{kNamespace} + "::" + kInstance + "." + Api::Method::kGetInt32));

        // Every controller must exist before the lifecycle starts, so the clients
        // above are created first and the lifecycle is started here.
        canoe_->Start();

        // Past this barrier, ServerNotReachable is a contract violation rather
        // than a slow runner.
        ASSERT_TRUE(canoe_->WaitUntilConnected("VhalBridge"))
            << "the adapter participant never joined the session";
        const auto probe = canoe_->AwaitServerReachable(getClient_, ToSpan(GetReq("EV_BATTERY_LEVEL")));
        ASSERT_EQ(probe.status, Rpc::RpcCallStatus::Success)
            << (probe.harnessTimeout ? "the probe call never completed at all (harness watchdog)"
                                     : "the adapter's RPC servers were never matched by a correctly "
                                       "specified client");
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
    Rpc::IRpcClient* getInt64Client_{nullptr};
    Rpc::IRpcClient* setInt64Client_{nullptr};
    Rpc::IRpcClient* getFloatClient_{nullptr};
    Rpc::IRpcClient* setFloatClient_{nullptr};
    Rpc::IRpcClient* getStringClient_{nullptr};
    Rpc::IRpcClient* setStringClient_{nullptr};
    Rpc::IRpcClient* getBytesClient_{nullptr};
    Rpc::IRpcClient* setBytesClient_{nullptr};
    Rpc::IRpcClient* versionClient_{nullptr};
    Rpc::IRpcClient* badClient_{nullptr};
};

TEST_F(AdapterRpcTest, GetPropertyReturnsMockValue)
{
    // INFO_MODEL_YEAR is an AOSP INT32 property, so it travels over GetInt32.
    const auto result = canoe_->Call(getClient_, ToSpan(GetReq("INFO_MODEL_YEAR")));
    ASSERT_EQ(result.status, Rpc::RpcCallStatus::Success);
    EXPECT_EQ(ParseInt32(result.data), 2026); // seeded by VhalMockClient
}

// The float counterpart, and a guard against the mock drifting back to storing
// AOSP FLOAT properties as int32 - which made GetFloat return 0 from CANoe.
TEST_F(AdapterRpcTest, GetFloatReturnsMockValue)
{
    const auto result = canoe_->Call(getFloatClient_, ToSpan(GetReq("EV_BATTERY_LEVEL")));
    ASSERT_EQ(result.status, Rpc::RpcCallStatus::Success);
    EXPECT_FLOAT_EQ(ParseFloat(result.data), 88.5f);
}

TEST_F(AdapterRpcTest, SetThenGetRoundTrip)
{
    const auto setResult = canoe_->Call(setClient_, ToSpan(SetInt32Req("EV_CHARGE_STATE", 5)));
    ASSERT_EQ(setResult.status, Rpc::RpcCallStatus::Success);
    EXPECT_TRUE(setResult.data.empty()); // void method -> empty response

    const auto getResult = canoe_->Call(getClient_, ToSpan(GetReq("EV_CHARGE_STATE")));
    ASSERT_EQ(getResult.status, Rpc::RpcCallStatus::Success);
    EXPECT_EQ(ParseInt32(getResult.data), 5);
}

TEST_F(AdapterRpcTest, SetThenGetFloatRoundTrip)
{
    const auto setResult = canoe_->Call(setFloatClient_, ToSpan(SetFloatReq("RANGE_REMAINING", 21.5f)));
    ASSERT_EQ(setResult.status, Rpc::RpcCallStatus::Success);

    const auto getResult = canoe_->Call(getFloatClient_, ToSpan(GetReq("RANGE_REMAINING")));
    ASSERT_EQ(getResult.status, Rpc::RpcCallStatus::Success);
    EXPECT_FLOAT_EQ(ParseFloat(getResult.data), 21.5f);
}

TEST_F(AdapterRpcTest, UnknownPropertyReturnsZero)
{
    const auto result = canoe_->Call(getClient_, ToSpan(GetReq("NOT_A_REAL_PROPERTY")));
    ASSERT_EQ(result.status, Rpc::RpcCallStatus::Success); // adapter answers with its error fallback
    EXPECT_EQ(ParseInt32(result.data), 0);
}

// The three types the VhalSimple API added beyond int32/float. VhalMockClient
// stores whatever variant it is handed, so a set/get pair through the bridge
// proves the whole chain: CANoe-shaped request bytes, the templated serdes, the
// adapter's variant round trip, and the response encoding.
TEST_F(AdapterRpcTest, SetThenGetInt64RoundTrip)
{
    const int64_t epoch = 1767225600000; // beyond 32 bits, so a truncation shows up
    const auto setResult = canoe_->Call(setInt64Client_, ToSpan(SetInt64Req("ANDROID_EPOCH_TIME", epoch)));
    ASSERT_EQ(setResult.status, Rpc::RpcCallStatus::Success);

    const auto getResult = canoe_->Call(getInt64Client_, ToSpan(GetReq("ANDROID_EPOCH_TIME")));
    ASSERT_EQ(getResult.status, Rpc::RpcCallStatus::Success);
    EXPECT_EQ(ParseInt64(getResult.data), epoch);
}

TEST_F(AdapterRpcTest, SetThenGetStringRoundTrip)
{
    const auto setResult = canoe_->Call(setStringClient_, ToSpan(SetStringReq("INFO_VIN", "WVWZZZ1JZXW000001")));
    ASSERT_EQ(setResult.status, Rpc::RpcCallStatus::Success);

    const auto getResult = canoe_->Call(getStringClient_, ToSpan(GetReq("INFO_VIN")));
    ASSERT_EQ(getResult.status, Rpc::RpcCallStatus::Success);
    EXPECT_EQ(ParseString(getResult.data), "WVWZZZ1JZXW000001");
}

TEST_F(AdapterRpcTest, SetThenGetBytesRoundTrip)
{
    const std::vector<uint8_t> seed{0xDE, 0xAD, 0xBE, 0xEF, 0x00};
    const auto setResult = canoe_->Call(setBytesClient_, ToSpan(SetBytesReq("STORAGE_ENCRYPTION_BINDING_SEED", seed)));
    ASSERT_EQ(setResult.status, Rpc::RpcCallStatus::Success);

    const auto getResult = canoe_->Call(getBytesClient_, ToSpan(GetReq("STORAGE_ENCRYPTION_BINDING_SEED")));
    ASSERT_EQ(getResult.status, Rpc::RpcCallStatus::Success);
    EXPECT_EQ(ParseBytes(getResult.data), seed); // embedded NUL must survive
}

// The version handshake: a CANoe configuration can confirm it speaks the same
// dialect instead of discovering a contract mismatch as ServerNotReachable.
TEST_F(AdapterRpcTest, GetApiVersionReportsTheContractVersion)
{
    const auto result = canoe_->Call(versionClient_, ToSpan(EmptyReq()));
    ASSERT_EQ(result.status, Rpc::RpcCallStatus::Success);
    EXPECT_EQ(ParseInt32(result.data), Api::kApiVersion);
}

// An unset property of a non-numeric type answers with an empty value rather
// than throwing, which is the string/bytes counterpart of UnknownPropertyReturnsZero.
TEST_F(AdapterRpcTest, UnknownStringPropertyReturnsEmpty)
{
    const auto result = canoe_->Call(getStringClient_, ToSpan(GetReq("NOT_A_REAL_PROPERTY")));
    ASSERT_EQ(result.status, Rpc::RpcCallStatus::Success);
    EXPECT_EQ(ParseString(result.data), "");

    const auto asBytes = canoe_->Call(getBytesClient_, ToSpan(GetReq("NOT_A_REAL_PROPERTY")));
    ASSERT_EQ(asBytes.status, Rpc::RpcCallStatus::Success);
    EXPECT_TRUE(ParseBytes(asBytes.data).empty());
}

// Writing through the wrong method family is refused by the VHAL - the mock
// mirrors the "expects FLOAT, not int32" error the real client throws - but
// Set<Type> is a void method, so CANoe still sees Success. The only signal is the
// bridge log, and the stored value being untouched. Worth pinning: it is the
// write-side counterpart of WrongTypeReturnsZero.
TEST_F(AdapterRpcTest, WrongTypedWriteIsRefusedButStillReportsSuccess)
{
    // EV_BATTERY_LEVEL is an AOSP FLOAT property, seeded 88.5.
    const auto setResult = canoe_->Call(setClient_, ToSpan(SetInt32Req("EV_BATTERY_LEVEL", 7)));
    EXPECT_EQ(setResult.status, Rpc::RpcCallStatus::Success) << "void method: the wire cannot carry the refusal";

    const auto getResult = canoe_->Call(getFloatClient_, ToSpan(GetReq("EV_BATTERY_LEVEL")));
    ASSERT_EQ(getResult.status, Rpc::RpcCallStatus::Success);
    EXPECT_FLOAT_EQ(ParseFloat(getResult.data), 88.5f) << "the refused write must not have changed the value";
}

// A property that exists but holds another type must not be mistaken for data.
// CABIN_TEMP is deliberately not an AOSP property name: the mock validates writes
// against the AOSP table, so only a name outside that table can be stored with an
// arbitrary type and then read back through the wrong method. That reaches the
// adapter's type-mismatch branch. The VCDL methods carry no status
// out-parameter, so 0 is still what CANoe receives - what changed is that the
// bridge now logs which type it actually found instead of staying mute.
TEST_F(AdapterRpcTest, WrongTypeReturnsZero)
{
    const auto setResult = canoe_->Call(setFloatClient_, ToSpan(SetFloatReq("CABIN_TEMP", 21.5f)));
    ASSERT_EQ(setResult.status, Rpc::RpcCallStatus::Success);

    // Same property, read through the int32 method rather than the float one.
    const auto getResult = canoe_->Call(getClient_, ToSpan(GetReq("CABIN_TEMP")));
    ASSERT_EQ(getResult.status, Rpc::RpcCallStatus::Success);
    EXPECT_EQ(ParseInt32(getResult.data), 0);

    // ...while reading it through the matching method still works, which proves
    // the zero above came from the type mismatch and not from a lost write.
    const auto asFloat = canoe_->Call(getFloatClient_, ToSpan(GetReq("CABIN_TEMP")));
    ASSERT_EQ(asFloat.status, Rpc::RpcCallStatus::Success);
    EXPECT_FLOAT_EQ(ParseFloat(asFloat.data), 21.5f);
}

// Regression guard for the CANoe-interop bug: the RpcSpec function name is the
// matching key. A client that uses the full DO path as the function name (the
// old, broken behavior) must NOT match the adapter's server.
//
// Proven by ordering, not by waiting: SetUp() established that a correctly
// specified client matched, and the adapter creates every server in Init(),
// so its complete server set is known here.
TEST_F(AdapterRpcTest, WrongFunctionNameDoesNotMatch)
{
    const auto result = canoe_->Call(badClient_, ToSpan(GetReq("INFO_MODEL_YEAR")));
    EXPECT_EQ(result.status, Rpc::RpcCallStatus::ServerNotReachable);

    // ...while the correctly-named client on the same participant does match.
    const auto ok = canoe_->Call(getClient_, ToSpan(GetReq("INFO_MODEL_YEAR")));
    ASSERT_EQ(ok.status, Rpc::RpcCallStatus::Success);
    EXPECT_EQ(ParseInt32(ok.data), 2026);
}

} // namespace
