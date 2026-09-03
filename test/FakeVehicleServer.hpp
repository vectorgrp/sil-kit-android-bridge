// SPDX-FileCopyrightText: 2026 Vector Informatik GmbH
// SPDX-License-Identifier: MIT
#pragma once

// A programmable in-process stand-in for the Android VHAL gRPC server.
//
// It exists because VhalGrpcClient was the one production component with no
// automated coverage: VhalGrpcConvert was split out so the pure translation
// could be unit tested, but everything that talks to a stub - result handling,
// CheckHealth, the streaming config read, the transport-failure paths, and the
// header-only SetValues<T> templates - was reachable only by pointing the bridge
// at real hardware. Both bugs that shipped in the 0.3 cycle lived exactly there.
//
// Two deliberate choices:
//
//   * Values are seeded with explicit protobuf setters, NOT with
//     VhalConvert::ToPropValue. ToPropValue is the CarProperty -> proto
//     direction and ToCarProperty is its inverse, so building a fixture with one
//     and reading it back with the other would make the test a round trip
//     through both. A symmetric field-selection bug - precisely the class of bug
//     being guarded here - would cancel itself out and the test would pass.
//   * ReceivedSetRequests() records what actually arrived on the wire, so a test
//     can assert that a BYTES value landed in byte_values rather than only that
//     the call returned true. That is the assertion that would have caught
//     "expects BYTES, not INT32_VEC" before a human found it on a device.
//
// Only the four RPCs VhalGrpcClient uses are overridden. The other eight inherit
// the generated UNIMPLEMENTED handler, which keeps the bridge honest about what
// it calls.

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

#include <grpcpp/grpcpp.h>

// winsock2.h (pulled in by grpcpp on Windows) defines TRY_AGAIN=4 and
// ACCESS_DENIED=5 as macros, and both are StatusCode enumerators in the VHAL
// protobuf headers. Same guard as VhalGrpcClient.hpp / VhalGrpcConvert.hpp.
#ifdef TRY_AGAIN
#undef TRY_AGAIN
#endif
#ifdef ACCESS_DENIED
#undef ACCESS_DENIED
#endif

#include "VehicleServer.grpc.pb.h"

namespace test {

namespace vhal = android::hardware::automotive::vehicle::proto;

//! How the fake orders the results it answers a GetValues request with. A real
//! VHAL makes no ordering promise, so the client may not assume one.
enum class ResultOrder
{
    AsRequested, //!< result i answers request i
    Reversed,    //!< every result present, in the opposite order
    DropFirst,   //!< the first request gets no result at all
};

class FakeVehicleServer final : public vhal::VehicleServer::Service
{
public:
    // ---- Seeding. Explicit setters on purpose - see the note at the top. ----
    void SeedInt32(int32_t propId, int32_t value);
    void SeedInt64(int32_t propId, int64_t value);
    void SeedFloat(int32_t propId, float value);
    void SeedString(int32_t propId, const std::string& value);
    void SeedBytes(int32_t propId, const std::vector<uint8_t>& value);

    // ---- Knobs, each reproducing one thing a real VHAL does ----

    //! Answer reads of this property with a non-OK StatusCode (a refused read).
    void SetReadStatus(int32_t propId, vhal::StatusCode status);
    //! Answer writes of this property with a non-OK StatusCode (a refused write).
    void SetWriteStatus(int32_t propId, vhal::StatusCode status);
    //! Fail the RPC itself, as a dead or unreachable server would.
    void SetTransportStatus(grpc::StatusCode code);
    //! Control the order GetValues answers in. Default AsRequested.
    void SetResultOrder(ResultOrder order);
    //! The status_code CheckHealth reports. Default healthy.
    void SetHealthy(bool healthy);
    //! Abort the GetAllPropertyConfig stream after n configs. Negative: never.
    void SetConfigStreamFailsAfter(int n);

    // ---- Observation ----

    //! Every SetValues request received, in arrival order. This is how a test
    //! asserts which protobuf field a value was written into.
    auto ReceivedSetRequests() const -> std::vector<vhal::VehiclePropValueRequests>;

private:
    auto GetValues(grpc::ServerContext*, const vhal::VehiclePropValueRequests*, vhal::GetValueResults*)
        -> grpc::Status override;
    auto SetValues(grpc::ServerContext*, const vhal::VehiclePropValueRequests*, vhal::SetValueResults*)
        -> grpc::Status override;
    auto CheckHealth(grpc::ServerContext*, const google::protobuf::Empty*, vhal::VehicleHalCallStatus*)
        -> grpc::Status override;
    auto GetAllPropertyConfig(grpc::ServerContext*, const google::protobuf::Empty*,
                              grpc::ServerWriter<vhal::VehiclePropConfig>*) -> grpc::Status override;

    //! The stored value for propId, or nullptr. Caller holds mutex_.
    auto Find(int32_t propId) const -> const vhal::VehiclePropValue*;
    //! Fresh or existing entry for propId, with prop set. Caller holds mutex_.
    auto Entry(int32_t propId) -> vhal::VehiclePropValue&;

    // The synchronous gRPC server dispatches handlers on its own threads, so
    // every member below is guarded.
    mutable std::mutex mutex_;
    std::map<int32_t, vhal::VehiclePropValue> values_;
    std::map<int32_t, vhal::StatusCode> readStatus_;
    std::map<int32_t, vhal::StatusCode> writeStatus_;
    std::vector<vhal::VehiclePropValueRequests> setRequests_;
    grpc::StatusCode transportStatus_{grpc::StatusCode::OK};
    ResultOrder order_{ResultOrder::AsRequested};
    bool healthy_{true};
    int configStreamFailsAfter_{-1};
};

//! Owns a running server on gRPC's in-process transport and hands out channels
//! to it. Shaped like test::LiveRegistry in TestHarness.hpp: construct it, use
//! it, let it go out of scope. No socket, no port, no ValidateUri.
class FakeVhal
{
public:
    FakeVhal();
    ~FakeVhal();

    FakeVhal(const FakeVhal&) = delete;
    auto operator=(const FakeVhal&) -> FakeVhal& = delete;

    //! A channel to this server. Pass it to the VhalGrpcClient channel constructor.
    auto Channel() -> std::shared_ptr<grpc::Channel>;
    auto Service() -> FakeVehicleServer&
    {
        return service_;
    }

private:
    FakeVehicleServer service_;
    std::unique_ptr<grpc::Server> server_;
};

// ===== Implementation =====

inline auto FakeVehicleServer::Entry(int32_t propId) -> vhal::VehiclePropValue&
{
    auto& value = values_[propId];
    value.set_prop(propId);
    return value;
}

inline auto FakeVehicleServer::Find(int32_t propId) const -> const vhal::VehiclePropValue*
{
    auto it = values_.find(propId);
    return it == values_.end() ? nullptr : &it->second;
}

inline void FakeVehicleServer::SeedInt32(int32_t propId, int32_t value)
{
    std::lock_guard<std::mutex> lock{mutex_};
    auto& entry = Entry(propId);
    entry.clear_int32_values();
    entry.add_int32_values(value);
}

inline void FakeVehicleServer::SeedInt64(int32_t propId, int64_t value)
{
    std::lock_guard<std::mutex> lock{mutex_};
    auto& entry = Entry(propId);
    entry.clear_int64_values();
    entry.add_int64_values(value);
}

inline void FakeVehicleServer::SeedFloat(int32_t propId, float value)
{
    std::lock_guard<std::mutex> lock{mutex_};
    auto& entry = Entry(propId);
    entry.clear_float_values();
    entry.add_float_values(value);
}

inline void FakeVehicleServer::SeedString(int32_t propId, const std::string& value)
{
    std::lock_guard<std::mutex> lock{mutex_};
    Entry(propId).set_string_value(value);
}

inline void FakeVehicleServer::SeedBytes(int32_t propId, const std::vector<uint8_t>& value)
{
    std::lock_guard<std::mutex> lock{mutex_};
    Entry(propId).set_byte_values(std::string(value.begin(), value.end()));
}

inline void FakeVehicleServer::SetReadStatus(int32_t propId, vhal::StatusCode status)
{
    std::lock_guard<std::mutex> lock{mutex_};
    readStatus_[propId] = status;
}

inline void FakeVehicleServer::SetWriteStatus(int32_t propId, vhal::StatusCode status)
{
    std::lock_guard<std::mutex> lock{mutex_};
    writeStatus_[propId] = status;
}

inline void FakeVehicleServer::SetTransportStatus(grpc::StatusCode code)
{
    std::lock_guard<std::mutex> lock{mutex_};
    transportStatus_ = code;
}

inline void FakeVehicleServer::SetResultOrder(ResultOrder order)
{
    std::lock_guard<std::mutex> lock{mutex_};
    order_ = order;
}

inline void FakeVehicleServer::SetHealthy(bool healthy)
{
    std::lock_guard<std::mutex> lock{mutex_};
    healthy_ = healthy;
}

inline void FakeVehicleServer::SetConfigStreamFailsAfter(int n)
{
    std::lock_guard<std::mutex> lock{mutex_};
    configStreamFailsAfter_ = n;
}

inline auto FakeVehicleServer::ReceivedSetRequests() const -> std::vector<vhal::VehiclePropValueRequests>
{
    std::lock_guard<std::mutex> lock{mutex_};
    return setRequests_;
}

inline auto FakeVehicleServer::GetValues(grpc::ServerContext*, const vhal::VehiclePropValueRequests* request,
                                         vhal::GetValueResults* response) -> grpc::Status
{
    std::lock_guard<std::mutex> lock{mutex_};
    if (transportStatus_ != grpc::StatusCode::OK)
    {
        return grpc::Status{transportStatus_, "FakeVehicleServer: GetValues refused"};
    }

    // Collect answers in request order first, then reorder, so the knob decides
    // the wire order and nothing else does.
    std::vector<vhal::GetValueResult> answers;
    for (int i = 0; i < request->requests_size(); ++i)
    {
        const auto& req = request->requests(i);
        const auto propId = req.value().prop();

        vhal::GetValueResult result;
        result.set_request_id(req.request_id());

        auto statusIt = readStatus_.find(propId);
        if (statusIt != readStatus_.end())
        {
            // A refused read. The value is attached anyway: a client that trusts
            // the payload without gating on the status has to fail here.
            result.set_status(statusIt->second);
            if (const auto* stored = Find(propId))
            {
                *result.mutable_value() = *stored;
            }
        }
        else if (const auto* stored = Find(propId))
        {
            result.set_status(vhal::StatusCode::OK);
            *result.mutable_value() = *stored;
        }
        else
        {
            result.set_status(vhal::StatusCode::INVALID_ARG);
            result.mutable_value()->set_prop(propId);
        }
        answers.push_back(std::move(result));
    }

    switch (order_)
    {
    case ResultOrder::Reversed:
        for (auto it = answers.rbegin(); it != answers.rend(); ++it)
        {
            *response->add_results() = *it;
        }
        break;
    case ResultOrder::DropFirst:
        for (size_t i = 1; i < answers.size(); ++i)
        {
            *response->add_results() = answers[i];
        }
        break;
    case ResultOrder::AsRequested:
    default:
        for (const auto& answer : answers)
        {
            *response->add_results() = answer;
        }
        break;
    }
    return grpc::Status::OK;
}

inline auto FakeVehicleServer::SetValues(grpc::ServerContext*, const vhal::VehiclePropValueRequests* request,
                                         vhal::SetValueResults* response) -> grpc::Status
{
    std::lock_guard<std::mutex> lock{mutex_};
    setRequests_.push_back(*request);

    if (transportStatus_ != grpc::StatusCode::OK)
    {
        return grpc::Status{transportStatus_, "FakeVehicleServer: SetValues refused"};
    }

    for (int i = 0; i < request->requests_size(); ++i)
    {
        const auto& req = request->requests(i);
        const auto propId = req.value().prop();

        auto* result = response->add_results();
        result->set_request_id(req.request_id());

        auto statusIt = writeStatus_.find(propId);
        if (statusIt != writeStatus_.end())
        {
            result->set_status(statusIt->second);
            continue;
        }
        result->set_status(vhal::StatusCode::OK);
        // Store it verbatim, so a later read shows exactly which field the
        // client wrote into rather than a normalised copy.
        values_[propId] = req.value();
        values_[propId].set_prop(propId);
    }
    return grpc::Status::OK;
}

inline auto FakeVehicleServer::CheckHealth(grpc::ServerContext*, const google::protobuf::Empty*,
                                           vhal::VehicleHalCallStatus* response) -> grpc::Status
{
    std::lock_guard<std::mutex> lock{mutex_};
    if (transportStatus_ != grpc::StatusCode::OK)
    {
        return grpc::Status{transportStatus_, "FakeVehicleServer: CheckHealth refused"};
    }
    // An unhealthy VHAL answers the call successfully and reports the problem in
    // the payload. A client that only checks the transport status sees "healthy".
    response->set_status_code(healthy_ ? vhal::StatusCode::OK : vhal::StatusCode::INTERNAL_ERROR);
    return grpc::Status::OK;
}

inline auto FakeVehicleServer::GetAllPropertyConfig(grpc::ServerContext*, const google::protobuf::Empty*,
                                                    grpc::ServerWriter<vhal::VehiclePropConfig>* writer) -> grpc::Status
{
    std::lock_guard<std::mutex> lock{mutex_};
    if (transportStatus_ != grpc::StatusCode::OK)
    {
        return grpc::Status{transportStatus_, "FakeVehicleServer: GetAllPropertyConfig refused"};
    }

    int written = 0;
    for (const auto& entry : values_)
    {
        if (configStreamFailsAfter_ >= 0 && written >= configStreamFailsAfter_)
        {
            // A stream that dies partway. Everything already written stays
            // written, so a client that ignores Finish() sees a short list and
            // cannot tell it from a complete one.
            return grpc::Status{grpc::StatusCode::UNAVAILABLE, "FakeVehicleServer: config stream aborted"};
        }
        vhal::VehiclePropConfig config;
        config.set_prop(entry.first);
        writer->Write(config);
        ++written;
    }
    return grpc::Status::OK;
}

inline FakeVhal::FakeVhal()
{
    grpc::ServerBuilder builder;
    builder.RegisterService(&service_);
    server_ = builder.BuildAndStart();
    if (!server_)
    {
        throw std::runtime_error("FakeVhal: failed to start the in-process server");
    }
}

inline FakeVhal::~FakeVhal()
{
    if (server_)
    {
        server_->Shutdown();
        server_->Wait();
    }
}

inline auto FakeVhal::Channel() -> std::shared_ptr<grpc::Channel>
{
    return server_->InProcessChannel(grpc::ChannelArguments{});
}

} // namespace test
