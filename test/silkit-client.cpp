// SPDX-FileCopyrightText: 2026 Vector Informatik GmbH
// SPDX-License-Identifier: MIT
//
// Manual developer tool -- the CANoe-side counterpart of the VHAL bridge. It
// connects to a running SIL Kit registry and drives the RPC *server* side of the
// bridge (VhalSilKitAdapter) as a client, speaking the VhalSimple API with the
// same function names, labels and wire format a CANoe/sil-adapter-builder
// consumer would use. Everything it sends comes from VhalSimpleApi.hpp, so it
// cannot drift from the bridge.
//
// It sends "PROPERTY=value" assignments (a Set followed by a read-back Get) and
// bare "PROPERTY" reads. Which of the five type families a property travels over
// is derived from the AOSP property-id type bits (see VhalProperty.hpp).
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
#include <cctype>
#include <chrono>
#include <cstdint>
#include <future>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <regex>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
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
#include "VhalSimpleApi.hpp"

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
// the adapter's MakeRpcSpec (defaults come from VhalSimpleApi.hpp, which mirrors VhalSimple.vcdl).
auto MakeClientSpec(const std::string& functionName, const std::string& ns, const std::string& instance,
                    const std::string& vnet) -> Rpc::RpcSpec
{
    Rpc::RpcSpec spec{functionName, SerDes::MediaTypeRpc()};
    spec.AddLabel("VirtualNetwork", vnet, SilKit::Services::MatchingLabel::Kind::Mandatory);
    spec.AddLabel("Namespace", ns, SilKit::Services::MatchingLabel::Kind::Mandatory);
    spec.AddLabel("Instance", instance, SilKit::Services::MatchingLabel::Kind::Mandatory);
    return spec;
}

// ---- Wire helpers ----
// Encoding lives in VhalSimpleApi.hpp, shared with the bridge. Only the request
// side is expressed here because the API's request encoder is symmetric with the
// response encoder: both write bare fields inside a zero-width struct frame.
auto GetReq(const std::string& name) -> std::vector<uint8_t>
{
    SerDes::Serializer s;
    s.Serialize(name);
    return s.ReleaseBuffer();
}
template <typename T>
auto SetReq(const std::string& name, const T& value) -> std::vector<uint8_t>
{
    SerDes::Serializer s;
    s.Serialize(name);
    if constexpr (std::is_same_v<T, int32_t>)
        s.Serialize(value, 32);
    else if constexpr (std::is_same_v<T, int64_t>)
        s.Serialize(value, 64);
    else
        s.Serialize(value);
    return s.ReleaseBuffer();
}
template <typename T>
auto ParseValue(const std::vector<uint8_t>& bytes) -> T
{
    SerDes::Deserializer d{bytes};
    if constexpr (std::is_same_v<T, int32_t>)
        return d.Deserialize<int32_t>(32);
    else if constexpr (std::is_same_v<T, int64_t>)
        return d.Deserialize<int64_t>(64);
    else
        return d.Deserialize<T>();
}

// The five VhalSimple type families. AOSP's vector and MIXED types have no
// representation in the API, so they fall back to int32 - the same fallback the
// bridge applies, since CarProperty carries BOOLEAN as int32 too.
enum class WireType
{
    Int32,
    Int64,
    Float,
    String,
    Bytes
};

// Derive the family from the AOSP property-id type bits. For a name that is not in
// the compile-time table, fall back to inferring from the literal (a '.'/exponent
// means float, quotes or non-numeric characters mean string), so ad-hoc/vendor
// names still work.
auto WireTypeFor(const std::string& name, const std::string& value) -> WireType
{
    const auto id = VehicleProperty_GetIdByName(name);
    if (id != 0)
    {
        switch (getPropertyType(id))
        {
        case VehiclePropertyType::FLOAT:
        case VehiclePropertyType::FLOAT_VEC:
            return WireType::Float;
        case VehiclePropertyType::INT64:
        case VehiclePropertyType::INT64_VEC:
            return WireType::Int64;
        case VehiclePropertyType::STRING:
            return WireType::String;
        case VehiclePropertyType::BYTES:
            return WireType::Bytes;
        default:
            return WireType::Int32;
        }
    }
    if (value.empty())
        return WireType::Int32;
    if (value.find_first_not_of("+-0123456789.eE") != std::string::npos)
        return WireType::String;
    if (value.find_first_of(".eE") != std::string::npos)
        return WireType::Float;
    return WireType::Int32;
}

// For a read (no value hint) the family comes from the property table only.
auto WireTypeFor(const std::string& name) -> WireType { return WireTypeFor(name, std::string{}); }

// The accessor a GetPropertyStatus type nibble names, for printing.
auto TypeCodeName(VhalSimpleApi::TypeCode type) -> const char*
{
    switch (type)
    {
    case VhalSimpleApi::TypeCode::Int32:
        return "int32";
    case VhalSimpleApi::TypeCode::Int64:
        return "int64";
    case VhalSimpleApi::TypeCode::Float:
        return "float";
    case VhalSimpleApi::TypeCode::String:
        return "string";
    case VhalSimpleApi::TypeCode::Bytes:
        return "bytes";
    case VhalSimpleApi::TypeCode::None:
        break;
    }
    return "none";
}

// Bytes are shown and accepted as hex, since a byte array has no useful literal
// form on a command line.
auto HexOf(const std::vector<uint8_t>& bytes) -> std::string
{
    static constexpr char kDigits[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (auto b : bytes)
    {
        out.push_back(kDigits[b >> 4]);
        out.push_back(kDigits[b & 0x0F]);
    }
    return out;
}

auto BytesFromHex(const std::string& text) -> std::vector<uint8_t>
{
    std::string hex;
    for (char c : text)
    {
        if (!std::isspace(static_cast<unsigned char>(c)))
            hex.push_back(c);
    }
    if (hex.size() % 2 != 0)
        throw std::invalid_argument("hex string must have an even number of digits");
    std::vector<uint8_t> out;
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2)
    {
        out.push_back(static_cast<uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
    }
    return out;
}

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

        // Every controller must exist before StartLifecycle(): the first lifecycle
        // state is ParticipantState::ServicesCreated, and a controller created
        // after that point can miss the service discovery round and then never
        // match its server.
        const std::string path = vnet + "::" + ns + "::" + instance + ".";
        auto add = [&](const char* method) {
            return AddClient(path + method, MakeClientSpec(method, ns, instance, vnet));
        };
        namespace Method = VhalSimpleApi::Method;
        getInt_ = add(Method::kGetInt32);
        setInt_ = add(Method::kSetInt32);
        getInt64_ = add(Method::kGetInt64);
        setInt64_ = add(Method::kSetInt64);
        getFloat_ = add(Method::kGetFloat);
        setFloat_ = add(Method::kSetFloat);
        getString_ = add(Method::kGetString);
        setString_ = add(Method::kSetString);
        getBytes_ = add(Method::kGetBytes);
        setBytes_ = add(Method::kSetBytes);
        version_ = add(Method::kGetApiVersion);
        propertyStatus_ = add(Method::kGetPropertyStatus);

        // An autonomous lifecycle keeps this participant a first-class simulation
        // member so RPC discovery/communication with the bridge works standalone.
        lifecycle_ = participant_->CreateLifecycleService({Orchestration::OperationMode::Autonomous});
        finalState_ = lifecycle_->StartLifecycle();
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

    // Ask the bridge which contract version it serves. Returns nullopt when the
    // method does not match at all, which is what an older bridge looks like.
    auto ApiVersion() -> std::optional<int32_t>
    {
        const auto res = Call(version_, ToSpan(std::vector<uint8_t>{}));
        if (res.status != Rpc::RpcCallStatus::Success)
            return std::nullopt;
        return ParseValue<int32_t>(res.data);
    }

    //! Ask the bridge what it knows about a property, and print it decoded. This
    //! is the only way to tell a property that holds 0 from one that does not
    //! exist, so it is worth reaching for before blaming a name.
    void PropertyStatus(const std::string& name)
    {
        namespace S = VhalSimpleApi::PropertyStatus;

        const auto reqBytes = GetReq(name);
        const auto res = Call(propertyStatus_, ToSpan(reqBytes));
        if (res.status != Rpc::RpcCallStatus::Success)
        {
            print("STAT {} FAILED: {}", name, StatusName(res.status));
            return;
        }

        const auto status = ParseValue<int32_t>(res.data);
        if (status < 0)
        {
            // Cast so the sign bit prints as part of the word. fmt renders a
            // negative int32 as "-80000000", which is the one value a reader
            // most needs to recognise on sight.
            print("STAT {} = 0x{:08X} the bridge could not answer (VHAL unreachable, or a malformed request)", name,
                  static_cast<uint32_t>(status));
        }
        else if (status == S::kAbsent)
        {
            print("STAT {} = 0 unknown to the bridge: not configured, and not an AOSP property name", name);
        }
        else
        {
            print("STAT {} = 0x{:X} {}{}{}{} accessor={}", name, static_cast<uint32_t>(status),
                  (status & S::kExists) ? "exists " : "not-configured ", (status & S::kReadable) ? "readable " : "",
                  (status & S::kWritable) ? "writable " : "", (status & S::kKnownName) ? "aosp-name " : "",
                  TypeCodeName(VhalSimpleApi::StatusTypeCode(status)));
        }
    }

    // Read a single property and print its value.
    void Get(const std::string& name)
    {
        auto report = [&](const CallResult& res, const char* type, auto&& render) {
            if (res.status == Rpc::RpcCallStatus::Success)
                print("GET  {} = {} ({})", name, render(res.data), type);
            else
                print("GET  {} FAILED: {}", name, StatusName(res.status));
        };
        // GetReq returns by value and ToSpan does not own: the buffer has to outlive
        // every Call below, so it needs a name rather than being a temporary.
        const auto reqBytes = GetReq(name);
        const auto req = ToSpan(reqBytes);
        switch (WireTypeFor(name))
        {
        case WireType::Float:
            report(Call(getFloat_, req), "float", [](const auto& b) { return ParseValue<float>(b); });
            return;
        case WireType::Int64:
            report(Call(getInt64_, req), "int64", [](const auto& b) { return ParseValue<int64_t>(b); });
            return;
        case WireType::String:
            report(Call(getString_, req), "string", [](const auto& b) { return ParseValue<std::string>(b); });
            return;
        case WireType::Bytes:
            report(Call(getBytes_, req), "bytes",
                   [](const auto& b) { return HexOf(ParseValue<std::vector<uint8_t>>(b)); });
            return;
        case WireType::Int32:
            break;
        }
        report(Call(getInt_, req), "int32", [](const auto& b) { return ParseValue<int32_t>(b); });
    }

    // Set a property, then read it back to show the effect.
    void Set(const std::string& name, const std::string& valueStr)
    {
        try
        {
            switch (WireTypeFor(name, valueStr))
            {
            case WireType::Float:
            {
                const float value = std::stof(valueStr);
                const auto res = Call(setFloat_, ToSpan(SetReq(name, value)));
                print("SET  {} = {} (float) -> {}", name, value, StatusName(res.status));
                break;
            }
            case WireType::Int64:
            {
                const int64_t value = std::stoll(valueStr);
                const auto res = Call(setInt64_, ToSpan(SetReq(name, value)));
                print("SET  {} = {} (int64) -> {}", name, value, StatusName(res.status));
                break;
            }
            case WireType::String:
            {
                const auto res = Call(setString_, ToSpan(SetReq(name, valueStr)));
                print("SET  {} = '{}' (string) -> {}", name, valueStr, StatusName(res.status));
                break;
            }
            case WireType::Bytes:
            {
                const auto value = BytesFromHex(valueStr);
                const auto res = Call(setBytes_, ToSpan(SetReq(name, value)));
                print("SET  {} = {} (bytes) -> {}", name, HexOf(value), StatusName(res.status));
                break;
            }
            case WireType::Int32:
            {
                const int32_t value = ParseInt32Value(valueStr);
                const auto res = Call(setInt_, ToSpan(SetReq(name, value)));
                print("SET  {} = {} (int32) -> {}", name, value, StatusName(res.status));
                break;
            }
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
            void* ctx;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                // Never the promise's address: this loop frees promises as it
                // retries, and the allocator can hand the same address back, which
                // would let a late result complete a later call.
                ctx = reinterpret_cast<void*>(nextContext_++);
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

    std::uintptr_t nextContext_{1};
    Rpc::IRpcClient* getInt64_{nullptr};
    Rpc::IRpcClient* setInt64_{nullptr};
    Rpc::IRpcClient* getString_{nullptr};
    Rpc::IRpcClient* setString_{nullptr};
    Rpc::IRpcClient* getBytes_{nullptr};
    Rpc::IRpcClient* setBytes_{nullptr};
    Rpc::IRpcClient* version_{nullptr};
    Rpc::IRpcClient* propertyStatus_{nullptr};
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
// "?PROPERTY" asks what the bridge knows about a property instead of reading it.
// Quote it on a shell command line, or the '?' is taken as a glob.
const std::regex kStatusRe{R"(^\s*\?\s*([A-Za-z_][A-Za-z0-9_.]*)\s*$)"};

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
    if (std::regex_match(trimmed, m, kStatusRe))
    {
        client.PropertyStatus(m[1].str());
        return true;
    }
    if (std::regex_match(trimmed, m, kGetRe))
    {
        client.Get(m[1].str());
        return true;
    }
    print("Cannot parse '{}': expected PROPERTY=value, PROPERTY or ?PROPERTY", line);
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
    parser.Add<CliParser::Option>("namespace", "N", VhalSimpleApi::kNamespace, "[--namespace <ns>]",
                                  "-N, --namespace <ns>: SIL Kit RPC namespace label. Defaults to 'VhalSimple'.");
    parser.Add<CliParser::Option>("instance", "i", VhalSimpleApi::kInstance, "[--instance <instance>]",
                                  "-i, --instance <instance>: SIL Kit RPC instance label. Defaults to 'properties'.");
    parser.Add<CliParser::Option>("virtual-network", "V", VhalSimpleApi::kVirtualNetwork, "[--virtual-network <vnet>]",
                                  "-V, --virtual-network <vnet>: SIL Kit VirtualNetwork label. Defaults to 'Default'.");
    parser.Add<CliParser::Option>(
        "log", "l", "Off", "[--log <level>]",
        "-l, --log <level>: SIL Kit log level (Trace, Debug, Info, Warn, Error, Critical, Off). Defaults to 'Off'.");
    parser.Add<CliParser::PositionalList>(
        "assignments", "[PROPERTY=value | PROPERTY | ?PROPERTY ...]",
        "PROPERTY=value ...: Properties to set (with read-back). A bare PROPERTY reads, and "
        "'?PROPERTY' (quote it, or the shell globs it) reports whether the property exists and "
        "which accessor it needs. If omitted, assignments are read from stdin.");

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

    // A version mismatch otherwise shows up only as ServerNotReachable on every
    // call, which says nothing about why.
    if (const auto served = client.ApiVersion())
    {
        if (*served != VhalSimpleApi::kApiVersion)
        {
            print("WARNING: bridge serves VhalSimple API v{}, this client speaks v{}; some methods may not match.",
                  *served, VhalSimpleApi::kApiVersion);
        }
        else
        {
            print("Connected to the VhalSimple API v{}.", *served);
        }
    }
    else
    {
        print("WARNING: no answer from {}; check --namespace/--instance/--virtual-network and that the bridge is "
              "running.",
              VhalSimpleApi::Method::kGetApiVersion);
    }

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
