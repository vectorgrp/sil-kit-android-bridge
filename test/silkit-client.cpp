// SPDX-FileCopyrightText: 2026 Vector Informatik GmbH
// SPDX-License-Identifier: MIT
//
// Manual developer tool -- the CANoe-side counterpart of the VHAL bridge. It
// connects to a running SIL Kit registry and drives the RPC *server* side of the
// bridge (VhalSilKitAdapter) as a client, using the same function names, labels
// and wire format that a CANoe/sil-adapter-builder consumer would use.
//
// It sends "PROPERTY=value" assignments (a SetProperty followed by a read-back
// GetProperty) and bare "PROPERTY" reads. Whether a property travels over the
// int32 (GetProperty/SetProperty) or float (GetPropertyFloat/SetPropertyFloat)
// methods is derived from the AOSP property-id type bits (see VhalProperty.hpp).
//
// Assignments can be given as arguments:
//   silkit-client --registry-uri silkit://localhost:8500 \
//                 RANGE_REMAINING=1234.0 EV_BATTERY_LEVEL=80
// or, if none are given, read line by line from std::cin (one per line):
//   RANGE_REMAINING=1234.0
//   EV_BATTERY_LEVEL          # bare name -> read only
//
// Not a CTest case (no add_test); built into the top-level binary dir and
// installed alongside the bridge, but run by hand against a live bridge.
#include <chrono>
#include <cstdint>
#include <future>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <regex>
#include <string>
#include <thread>
#include <vector>

#include "silkit/SilKit.hpp"
#include "silkit/config/all.hpp"
#include "silkit/services/orchestration/all.hpp"
#include "silkit/services/rpc/all.hpp"
#include "silkit/util/Span.hpp"
#include "silkit/util/serdes/Deserializer.hpp"
#include "silkit/util/serdes/Serialization.hpp"
#include "silkit/util/serdes/Serializer.hpp"

#include "CommandlineParser.hpp"
#include "Print.hpp"
#include "VhalProperty.hpp"

namespace
{
namespace Rpc = SilKit::Services::Rpc;
namespace Orchestration = SilKit::Services::Orchestration;
namespace SerDes = SilKit::Util::SerDes;
using SilKit::Util::ToSpan;
using android::hardware::automotive::vehicle::VehiclePropertyType;
using android::hardware::automotive::vehicle::VehiclePropertyType_Name;
using android::hardware::automotive::vehicle::getPropertyType;
using android::hardware::automotive::vehicle::VehicleProperty_GetIdByName;
using android::hardware::automotive::vehicle::VehicleProperty_IdToNameMap;

// ---- RpcSpec: exactly what a CANoe-generated consumer advertises ----
// The bare method name is the matching key; the three Mandatory labels must match
// the adapter's MakeRpcSpec (defaults mirror sil-adapter-builder / VhalProperties.vcdl).
auto MakeClientSpec(const std::string& functionName, const std::string& ns, const std::string& instance,
                    const std::string& vnet) -> Rpc::RpcSpec
{
    Rpc::RpcSpec spec{functionName, SerDes::MediaTypeRpc()};
    spec.AddLabel("VirtualNetwork", vnet, SilKit::Services::MatchingLabel::Kind::Mandatory);
    spec.AddLabel("Namespace", ns, SilKit::Services::MatchingLabel::Kind::Mandatory);
    spec.AddLabel("Instance", instance, SilKit::Services::MatchingLabel::Kind::Mandatory);
    return spec;
}

// ---- Wire helpers: bare fields, exactly as the generated TypeLib serializer emits. ----
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

// The adapter exposes two method families: int32 and float. Every other AOSP type
// (bool, int32-vec, ...) is carried over the int32 family, so it is the fallback.
enum class WireType
{
    Int32,
    Float
};

// Derive the family from the AOSP property-id type bits. For a name that is not in
// the compile-time table, fall back to inferring from the literal (a '.'/exponent
// means float), so ad-hoc/vendor names still work.
auto WireTypeFor(const std::string& name, const std::string& value) -> WireType
{
    const auto id = VehicleProperty_GetIdByName(name);
    if (id != 0)
    {
        const auto type = getPropertyType(id);
        if (type == VehiclePropertyType::FLOAT || type == VehiclePropertyType::FLOAT_VEC)
            return WireType::Float;
        return WireType::Int32;
    }
    const bool looksFloat = value.find_first_of(".eE") != std::string::npos;
    return looksFloat ? WireType::Float : WireType::Int32;
}

// For a read (no value hint) the family comes from the property table only.
auto WireTypeFor(const std::string& name) -> WireType { return WireTypeFor(name, std::string{}); }

auto ParseInt32Value(const std::string& s) -> int32_t
{
    try
    {
        return static_cast<int32_t>(std::stoll(s));
    }
    catch (const std::exception&)
    {
        // tolerate a decimal literal on an int property, e.g. "1234.0"
        return static_cast<int32_t>(std::llround(std::stod(s)));
    }
}

auto StatusName(Rpc::RpcCallStatus status) -> const char*
{
    switch (status)
    {
    case Rpc::RpcCallStatus::Success: return "Success";
    case Rpc::RpcCallStatus::ServerNotReachable: return "ServerNotReachable";
    case Rpc::RpcCallStatus::UndefinedError: return "UndefinedError";
    case Rpc::RpcCallStatus::InternalServerError: return "InternalServerError";
    case Rpc::RpcCallStatus::Timeout: return "Timeout";
    default: return "Unknown";
    }
}

// Owns the participant and the four RpcClients, and turns parsed assignments into
// synchronous calls. SIL Kit delivers RPC results asynchronously on a worker
// thread; Call() bridges that back to the caller via a per-call std::promise keyed
// by the userContext pointer, with a timeout and optional retry while the server
// is not yet discovered (ServerNotReachable).
class BridgeClient
{
public:
    struct CallResult
    {
        Rpc::RpcCallStatus status;
        std::vector<uint8_t> data;
    };

    BridgeClient(const std::string& registryUri, const std::string& name, const std::string& ns,
                 const std::string& instance, const std::string& vnet, const std::string& logLevel)
    {
        auto config = SilKit::Config::ParticipantConfigurationFromString(
            R"(
Logging:
  Sinks:
    - Type: Stdout
      Level: )" + logLevel + "\n");
        participant_ = SilKit::CreateParticipant(config, name, registryUri);

        // An autonomous lifecycle keeps this participant a first-class simulation
        // member so RPC discovery/communication with the bridge works standalone.
        lifecycle_ = participant_->CreateLifecycleService({Orchestration::OperationMode::Autonomous});
        finalState_ = lifecycle_->StartLifecycle();

        const std::string path = vnet + "::" + ns + "::" + instance + ".";
        getInt_ = AddClient(path + "GetProperty", MakeClientSpec("GetProperty", ns, instance, vnet));
        setInt_ = AddClient(path + "SetProperty", MakeClientSpec("SetProperty", ns, instance, vnet));
        getFloat_ = AddClient(path + "GetPropertyFloat", MakeClientSpec("GetPropertyFloat", ns, instance, vnet));
        setFloat_ = AddClient(path + "SetPropertyFloat", MakeClientSpec("SetPropertyFloat", ns, instance, vnet));
    }

    ~BridgeClient()
    {
        try
        {
            if (lifecycle_ && lifecycle_->State() == Orchestration::ParticipantState::Running)
                lifecycle_->Stop("silkit-client teardown");
            // Wait for the lifecycle to settle so we disconnect gracefully.
            if (finalState_.valid())
                finalState_.wait_for(std::chrono::seconds{2});
        }
        catch (...)
        {
            // best-effort shutdown
        }
        participant_.reset();
    }

    BridgeClient(const BridgeClient&) = delete;
    BridgeClient& operator=(const BridgeClient&) = delete;

    // Read a single property and print its value.
    void Get(const std::string& name)
    {
        if (WireTypeFor(name) == WireType::Float)
        {
            const auto res = Call(getFloat_, ToSpan(GetReq(name)));
            if (res.status == Rpc::RpcCallStatus::Success)
                print("GET  {} = {} (float)", name, ParseFloat(res.data));
            else
                print("GET  {} FAILED: {}", name, StatusName(res.status));
        }
        else
        {
            const auto res = Call(getInt_, ToSpan(GetReq(name)));
            if (res.status == Rpc::RpcCallStatus::Success)
                print("GET  {} = {} (int32)", name, ParseInt32(res.data));
            else
                print("GET  {} FAILED: {}", name, StatusName(res.status));
        }
    }

    // Set a property, then read it back to show the effect.
    void Set(const std::string& name, const std::string& valueStr)
    {
        try
        {
            if (WireTypeFor(name, valueStr) == WireType::Float)
            {
                const float value = std::stof(valueStr);
                const auto res = Call(setFloat_, ToSpan(SetFloatReq(name, value)));
                print("SET  {} = {} (float) -> {}", name, value, StatusName(res.status));
            }
            else
            {
                const int32_t value = ParseInt32Value(valueStr);
                const auto res = Call(setInt_, ToSpan(SetInt32Req(name, value)));
                print("SET  {} = {} (int32) -> {}", name, value, StatusName(res.status));
            }
        }
        catch (const std::exception& e)
        {
            print("SET  {} = '{}' FAILED: cannot parse value ({})", name, valueStr, e.what());
            return;
        }
        Get(name); // read-back
    }

private:
    auto AddClient(const std::string& canonicalName, const Rpc::RpcSpec& spec) -> Rpc::IRpcClient*
    {
        return participant_->CreateRpcClient(
            canonicalName, spec, [this](Rpc::IRpcClient*, const Rpc::RpcCallResultEvent& ev) {
                std::shared_ptr<std::promise<CallResult>> promise;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    auto it = pending_.find(ev.userContext);
                    if (it == pending_.end())
                        return;
                    promise = it->second;
                    pending_.erase(it);
                }
                promise->set_value(CallResult{ev.callStatus, {ev.resultData.begin(), ev.resultData.end()}});
            });
    }

    // Issue a call and block until the result, a timeout, or (optionally) giving up
    // retrying past the deadline while the server is not yet reachable.
    auto Call(Rpc::IRpcClient* client, SilKit::Util::Span<const uint8_t> request,
              std::chrono::milliseconds timeout = std::chrono::milliseconds{5000},
              bool retryUntilReachable = true) -> CallResult
    {
        using clock = std::chrono::steady_clock;
        const auto deadline = clock::now() + timeout;

        for (;;)
        {
            auto promise = std::make_shared<std::promise<CallResult>>();
            void* ctx = promise.get();
            {
                std::lock_guard<std::mutex> lock(mutex_);
                pending_[ctx] = promise;
            }

            auto future = promise->get_future();
            client->Call(request, ctx);

            auto remaining = deadline - clock::now();
            if (remaining < std::chrono::milliseconds{0})
                remaining = std::chrono::milliseconds{0};

            if (future.wait_for(remaining) != std::future_status::ready)
            {
                std::lock_guard<std::mutex> lock(mutex_);
                pending_.erase(ctx);
                return CallResult{Rpc::RpcCallStatus::Timeout, {}};
            }

            auto result = future.get();
            if (retryUntilReachable && result.status == Rpc::RpcCallStatus::ServerNotReachable
                && clock::now() < deadline)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds{20});
                continue;
            }
            return result;
        }
    }

    std::unique_ptr<SilKit::IParticipant> participant_;
    Orchestration::ILifecycleService* lifecycle_{nullptr};
    std::future<Orchestration::ParticipantState> finalState_;

    Rpc::IRpcClient* getInt_{nullptr};
    Rpc::IRpcClient* setInt_{nullptr};
    Rpc::IRpcClient* getFloat_{nullptr};
    Rpc::IRpcClient* setFloat_{nullptr};

    std::mutex mutex_;
    std::map<void*, std::shared_ptr<std::promise<CallResult>>> pending_;
};

// PROPERTY=value  (value may contain anything up to trailing whitespace)
const std::regex kAssignRe{R"(^\s*([A-Za-z_][A-Za-z0-9_.]*)\s*=\s*(.+?)\s*$)"};
// bare PROPERTY  -> read only
const std::regex kGetRe{R"(^\s*([A-Za-z_][A-Za-z0-9_.]*)\s*$)"};

// Apply one "PROPERTY=value" or "PROPERTY" line. Blank lines and '#' comments are
// ignored. Returns false only for a line that matches no accepted form.
auto ProcessLine(BridgeClient& client, const std::string& line) -> bool
{
    // strip a trailing comment and surrounding blanks
    auto hash = line.find('#');
    const std::string trimmed = (hash == std::string::npos) ? line : line.substr(0, hash);
    if (trimmed.find_first_not_of(" \t\r\n") == std::string::npos)
        return true; // blank / comment-only

    std::smatch m;
    if (std::regex_match(trimmed, m, kAssignRe))
    {
        client.Set(m[1].str(), m[2].str());
        return true;
    }
    if (std::regex_match(trimmed, m, kGetRe))
    {
        client.Get(m[1].str());
        return true;
    }
    print("Cannot parse '{}': expected PROPERTY=value or PROPERTY", line);
    return false;
}

void ListProperties()
{
    // Sort by name for a stable, greppable listing.
    std::vector<std::pair<std::string, int32_t>> all;
    all.reserve(VehicleProperty_IdToNameMap().size());
    for (const auto& [id, name] : VehicleProperty_IdToNameMap())
        all.emplace_back(name, id);
    std::sort(all.begin(), all.end());

    for (const auto& [name, id] : all)
    {
        const auto family = (getPropertyType(id) == VehiclePropertyType::FLOAT
                             || getPropertyType(id) == VehiclePropertyType::FLOAT_VEC)
                                ? "float"
                                : "int32";
        print("{:<52} id={:<12} type={:<10} wire={}", name, id, std::string{VehiclePropertyType_Name(id)}, family);
    }
    print("{} properties", all.size());
}

} // namespace

int main(int argc, char** argv) try
{
    using CliParser = SilKit::Util::CommandlineParser;
    CliParser parser;
    parser.SetDescription(
        "silkit-client - CANoe-side RPC client for the sil-kit-android-bridge.\n"
        "Sends PROPERTY=value assignments (Set + read-back) and bare PROPERTY reads,\n"
        "either from the command line or, if none are given, line by line from stdin.");
    parser.Add<CliParser::Flag>("help", "h", "[--help]", "-h, --help: Get this help.");
    parser.Add<CliParser::Flag>("list-properties", "L", "[--list-properties]",
                                "-L, --list-properties: List all known VHAL property names and exit.");
    parser.Add<CliParser::Option>(
        "registry-uri", "u", "silkit://localhost:8500", "[--registry-uri <uri>]",
        "-u, --registry-uri <uri>: The registry URI to connect to. Defaults to 'silkit://localhost:8500'.");
    parser.Add<CliParser::Option>(
        "name", "n", "silkit-client", "[--name <participantName>]",
        "-n, --name <participantName>: The participant name. Defaults to 'silkit-client'.");
    parser.Add<CliParser::Option>("namespace", "N", "VhalSilkit", "[--namespace <ns>]",
                                  "-N, --namespace <ns>: SIL Kit RPC namespace label. Defaults to 'VhalSilkit'.");
    parser.Add<CliParser::Option>(
        "instance", "i", "vhalProperties", "[--instance <instance>]",
        "-i, --instance <instance>: SIL Kit RPC instance label. Defaults to 'vhalProperties'.");
    parser.Add<CliParser::Option>("virtual-network", "V", "Default", "[--virtual-network <vnet>]",
                                  "-V, --virtual-network <vnet>: SIL Kit VirtualNetwork label. Defaults to 'Default'.");
    parser.Add<CliParser::Option>(
        "log", "l", "Off", "[--log <level>]",
        "-l, --log <level>: SIL Kit log level (Trace, Debug, Info, Warn, Error, Critical, Off). Defaults to 'Off'.");
    parser.Add<CliParser::PositionalList>(
        "assignments", "[PROPERTY=value ...]",
        "PROPERTY=value ...: Properties to set (with read-back). A bare PROPERTY reads. "
        "If omitted, assignments are read from stdin.");

    try
    {
        parser.ParseArguments(argc, argv);
    }
    catch (const std::runtime_error& e)
    {
        std::cerr << "Error parsing arguments: " << e.what() << std::endl;
        parser.PrintUsageInfo(std::cerr);
        return 1;
    }

    if (parser.Get<CliParser::Flag>("help").Value())
    {
        parser.PrintUsageInfo(std::cout);
        return 0;
    }

    if (parser.Get<CliParser::Flag>("list-properties").Value())
    {
        ListProperties();
        return 0;
    }

    const auto registryUri = parser.Get<CliParser::Option>("registry-uri").Value();
    const auto name = parser.Get<CliParser::Option>("name").Value();
    const auto ns = parser.Get<CliParser::Option>("namespace").Value();
    const auto instance = parser.Get<CliParser::Option>("instance").Value();
    const auto vnet = parser.Get<CliParser::Option>("virtual-network").Value();
    const auto logLevel = parser.Get<CliParser::Option>("log").Value();

    print("Connecting participant '{}' to registry '{}' (Namespace={}, Instance={}, VirtualNetwork={})", name,
          registryUri, ns, instance, vnet);
    BridgeClient client{registryUri, name, ns, instance, vnet, logLevel};

    const auto assignments = parser.Get<CliParser::PositionalList>("assignments").Values();
    if (!assignments.empty())
    {
        for (const auto& assignment : assignments)
        {
            ProcessLine(client, assignment);
        }
    }
    else
    {
        print("No assignments on the command line; reading PROPERTY=value lines from stdin (Ctrl+D / Ctrl+Z to end).");
        std::string line;
        while (std::getline(std::cin, line))
        {
            ProcessLine(client, line);
        }
    }

    return 0;
}
catch (const std::exception& e)
{
    std::cerr << "Fatal Exception: " << e.what() << std::endl;
    return 1;
}
