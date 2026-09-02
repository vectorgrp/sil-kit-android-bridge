// SPDX-FileCopyrightText: 2026 Vector Informatik GmbH
// SPDX-License-Identifier: MIT
#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "silkit/SilKit.hpp"
#include "silkit/services/orchestration/all.hpp"
#include "silkit/services/rpc/all.hpp"
#include "silkit/util/Span.hpp"

#include "TestHarness.hpp"

// SkParticipant: a thin, reusable SIL Kit participant wrapper for tests. A test
// composes a "fake CANoe" by adding RpcClients with the desired RpcSpec and
// issuing synchronous calls. The participant runs an autonomous lifecycle,
// started non-blocking in the ctor.
//
// No assertion may depend on how fast the machine is, so waiting is done on
// logical conditions - WaitUntilConnected() and AwaitServerReachable() - and
// Call() never retries. kWatchdog only bounds a hang; it is not a threshold any
// passing test approaches.

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

    static constexpr std::chrono::seconds kWatchdog{60};

    SkParticipant(const std::string& name, const std::string& registryUri,
                  SilKit::Services::Orchestration::OperationMode mode =
                      SilKit::Services::Orchestration::OperationMode::Autonomous,
                  const std::string& logLevel = "Off")
    {
        participant_ = SilKit::CreateParticipant(MakeLoggingConfig(logLevel), name, registryUri);

        // Armed before the lifecycle starts, so no peer connects unobserved.
        monitor_ = participant_->CreateSystemMonitor();
        monitor_->SetParticipantConnectedHandler(
            [this](const SilKit::Services::Orchestration::ParticipantConnectionInformation& info) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                connected_.insert(info.participantName);
            }
            connectedCv_.notify_all();
        });

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
            // Dropping the participant mid-stop makes peers log an ungraceful
            // disconnect and leaves the next fixture racing a half-closed session.
            if (finalState_.valid())
            {
                finalState_.wait_for(kWatchdog);
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

    // Blocks until `participantName` has joined. False means it never did.
    auto WaitUntilConnected(const std::string& participantName) -> bool
    {
        std::unique_lock<std::mutex> lock(mutex_);
        return connectedCv_.wait_for(lock, kWatchdog, [&] {
            // The query covers a peer connected before this call, the handler the rest.
            return connected_.count(participantName) != 0
                   || monitor_->IsParticipantConnected(participantName);
        });
    }

    // Exactly one call, so the status a test sees is the one SIL Kit produced.
    auto Call(SilKit::Services::Rpc::IRpcClient* client, SilKit::Util::Span<const uint8_t> request)
        -> CallResult
    {
        auto promise = std::make_shared<std::promise<CallResult>>();
        void* ctx;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            // Never a recycled address: a late result must not complete a later call.
            ctx = reinterpret_cast<void*>(nextContext_++);
            pending_[ctx] = promise;
        }

        auto future = promise->get_future();
        client->Call(request, ctx);

        if (future.wait_for(kWatchdog) != std::future_status::ready)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            pending_.erase(ctx);
            return CallResult{SilKit::Services::Rpc::RpcCallStatus::Timeout, {}};
        }
        return future.get();
    }

    // Barrier for "matched to a server". SIL Kit has no event for RPC matching,
    // so a successful call is the only proof; `request` is really executed.
    auto AwaitServerReachable(SilKit::Services::Rpc::IRpcClient* client,
                              SilKit::Util::Span<const uint8_t> request) -> CallResult
    {
        const auto giveUp = std::chrono::steady_clock::now() + kWatchdog;
        for (;;)
        {
            auto result = Call(client, request);
            if (result.status != SilKit::Services::Rpc::RpcCallStatus::ServerNotReachable
                || std::chrono::steady_clock::now() >= giveUp)
            {
                return result;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{20});
        }
    }

private:
    std::unique_ptr<SilKit::IParticipant> participant_;
    SilKit::Services::Orchestration::ISystemMonitor* monitor_{nullptr};
    SilKit::Services::Orchestration::ILifecycleService* lifecycle_{nullptr};
    std::future<SilKit::Services::Orchestration::ParticipantState> finalState_;

    std::mutex mutex_;
    std::condition_variable connectedCv_;
    std::set<std::string> connected_;
    std::map<void*, std::shared_ptr<std::promise<CallResult>>> pending_;
    std::uintptr_t nextContext_{1};
};

} // namespace test
