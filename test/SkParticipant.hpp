// SPDX-FileCopyrightText: 2026 Vector Informatik GmbH
// SPDX-License-Identifier: MIT
#pragma once

#include <chrono>
#include <cstdint>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "silkit/SilKit.hpp"
#include "silkit/services/orchestration/all.hpp"
#include "silkit/services/rpc/all.hpp"
#include "silkit/util/Span.hpp"

#include "TestHarness.hpp"

// SkParticipant: a thin, reusable SIL Kit participant wrapper for tests. It is
// not specific to any role - a test composes a "fake CANoe" by adding RpcClients
// with the desired RpcSpec and issuing synchronous calls.
//
// The participant runs an autonomous lifecycle (started, non-blocking, in the
// ctor). RPC results arrive asynchronously on a SIL Kit worker thread; Call()
// bridges that back to the calling thread via a per-call std::promise keyed by
// the userContext pointer, with a timeout and optional retry while the server is
// not yet discovered (ServerNotReachable).

namespace test
{

class SkParticipant
{
public:
    struct CallResult
    {
        SilKit::Services::Rpc::RpcCallStatus status;
        std::vector<uint8_t> data;
    };

    SkParticipant(const std::string& name, const std::string& registryUri,
                  SilKit::Services::Orchestration::OperationMode mode =
                      SilKit::Services::Orchestration::OperationMode::Autonomous,
                  const std::string& logLevel = "Off")
    {
        participant_ = SilKit::CreateParticipant(MakeLoggingConfig(logLevel), name, registryUri);
        lifecycle_ = participant_->CreateLifecycleService({mode});
        finalState_ = lifecycle_->StartLifecycle();
    }

    ~SkParticipant()
    {
        try
        {
            if (lifecycle_
                && lifecycle_->State() == SilKit::Services::Orchestration::ParticipantState::Running)
            {
                lifecycle_->Stop("SkParticipant teardown");
            }
        }
        catch (...)
        {
            // best-effort shutdown
        }
        participant_.reset();
    }

    SkParticipant(const SkParticipant&) = delete;
    SkParticipant& operator=(const SkParticipant&) = delete;

    auto Get() -> SilKit::IParticipant& { return *participant_; }

    auto AddRpcClient(const std::string& canonicalName, const SilKit::Services::Rpc::RpcSpec& spec)
        -> SilKit::Services::Rpc::IRpcClient*
    {
        return participant_->CreateRpcClient(
            canonicalName, spec,
            [this](SilKit::Services::Rpc::IRpcClient*, const SilKit::Services::Rpc::RpcCallResultEvent& ev) {
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

    // Issue a call and block until the result, a timeout, or (optionally) give up
    // retrying past the deadline while the server is not yet reachable.
    auto Call(SilKit::Services::Rpc::IRpcClient* client, SilKit::Util::Span<const uint8_t> request,
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
                return CallResult{SilKit::Services::Rpc::RpcCallStatus::Timeout, {}};
            }

            auto result = future.get();
            if (retryUntilReachable
                && result.status == SilKit::Services::Rpc::RpcCallStatus::ServerNotReachable
                && clock::now() < deadline)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds{20});
                continue;
            }
            return result;
        }
    }

private:
    std::unique_ptr<SilKit::IParticipant> participant_;
    SilKit::Services::Orchestration::ILifecycleService* lifecycle_{nullptr};
    std::future<SilKit::Services::Orchestration::ParticipantState> finalState_;

    std::mutex mutex_;
    std::map<void*, std::shared_ptr<std::promise<CallResult>>> pending_;
};

} // namespace test
