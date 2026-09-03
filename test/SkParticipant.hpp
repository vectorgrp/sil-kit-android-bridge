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
#include <stdexcept>
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
// issuing synchronous calls.
//
// Construction and lifecycle start are deliberately separate. SIL Kit's first
// lifecycle state is ParticipantState::ServicesCreated - "the controllers
// created state" - so every controller must exist before StartLifecycle() runs.
// A client created afterwards can miss the service discovery round and then
// never matches its server. Hence: ctor -> AddRpcClient() ... -> Start(), and
// AddRpcClient() throws once Start() has been called so the rule cannot be
// broken silently.
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
        // True when this harness gave up waiting, as opposed to SIL Kit itself
        // reporting RpcCallStatus::Timeout. Keeps a failure message honest about
        // which side ran out of patience.
        bool harnessTimeout{false};
    };

    // Hang guard for a single call and for teardown. Far beyond any plausible
    // delay; a passing test never comes near it.
    static constexpr std::chrono::seconds kWatchdog{60};
    // Per-attempt bound inside AwaitServerReachable. Shorter than kWatchdog so
    // one stuck probe cannot consume the whole discovery budget and starve the
    // retry loop.
    static constexpr std::chrono::seconds kProbeWatchdog{5};

    SkParticipant(const std::string& name, const std::string& registryUri,
                  SilKit::Services::Orchestration::OperationMode mode =
                      SilKit::Services::Orchestration::OperationMode::Autonomous,
                  const std::string& logLevel = "Off")
        : mode_{mode}
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
    }

    ~SkParticipant()
    {
        try
        {
            if (lifecycle_)
            {
                // Attempting Stop() and letting it refuse is race-free; checking
                // State() first is not, because the state can change between the
                // check and the call.
                try
                {
                    lifecycle_->Stop("SkParticipant teardown");
                    stopRequested_ = true;
                }
                catch (const std::exception&)
                {
                    // Not in a state that accepts Stop(); nothing to wind down.
                }
            }
            // Dropping the participant mid-stop makes peers log an ungraceful
            // disconnect and leaves the next fixture racing a half-closed session.
            // Only wait when a Stop() was actually accepted: nothing else ever
            // resolves the future, so an unconditional wait would burn the full
            // watchdog on every teardown that never reached Running.
            if (stopRequested_ && finalState_.valid())
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

    // Must be called before Start(): see the ServicesCreated note above.
    auto AddRpcClient(const std::string& canonicalName, const SilKit::Services::Rpc::RpcSpec& spec)
        -> SilKit::Services::Rpc::IRpcClient*
    {
        if (lifecycle_)
        {
            throw std::logic_error("SkParticipant::AddRpcClient called after Start(); all controllers "
                                   "must exist before the lifecycle starts, or they may never be discovered");
        }
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
                promise->set_value(CallResult{ev.callStatus, {ev.resultData.begin(), ev.resultData.end()}, false});
            });
    }

    // Starts the participant lifecycle without blocking. Every controller this
    // participant will ever have must already exist.
    void Start()
    {
        if (lifecycle_)
            return;
        lifecycle_ = participant_->CreateLifecycleService({mode_});
        finalState_ = lifecycle_->StartLifecycle();
    }

    // Blocks until `participantName` has joined. False means it never did.
    auto WaitUntilConnected(const std::string& participantName) -> bool
    {
        // Never call into SIL Kit while holding mutex_: the connected handler runs
        // on a SIL Kit thread and takes that same mutex, so a query issued under
        // the lock can deadlock against the very dispatch it is waiting for. One
        // query up front covers a peer that connected before this call; the
        // handler, armed in the ctor, covers every one after it.
        if (monitor_->IsParticipantConnected(participantName))
        {
            std::lock_guard<std::mutex> lock(mutex_);
            connected_.insert(participantName);
            return true;
        }

        std::unique_lock<std::mutex> lock(mutex_);
        return connectedCv_.wait_for(lock, kWatchdog,
                                     [&] { return connected_.count(participantName) != 0; });
    }

    // Exactly one call, so the status a test sees is the one SIL Kit produced.
    auto Call(SilKit::Services::Rpc::IRpcClient* client, SilKit::Util::Span<const uint8_t> request)
        -> CallResult
    {
        return CallWithin(client, request, kWatchdog);
    }

    // Barrier for "matched to a server". SIL Kit has no event for RPC matching,
    // so a successful call is the only proof; `request` is really executed.
    // After the ctor/Start() split this should succeed on the first attempt; the
    // loop remains as a safety net rather than as the mechanism.
    auto AwaitServerReachable(SilKit::Services::Rpc::IRpcClient* client,
                              SilKit::Util::Span<const uint8_t> request) -> CallResult
    {
        const auto giveUp = std::chrono::steady_clock::now() + kWatchdog;
        for (;;)
        {
            auto result = CallWithin(client, request, kProbeWatchdog);
            if (result.status != SilKit::Services::Rpc::RpcCallStatus::ServerNotReachable
                || std::chrono::steady_clock::now() >= giveUp)
            {
                return result;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{20});
        }
    }

private:
    auto CallWithin(SilKit::Services::Rpc::IRpcClient* client, SilKit::Util::Span<const uint8_t> request,
                    std::chrono::seconds timeout) -> CallResult
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

        if (future.wait_for(timeout) != std::future_status::ready)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            pending_.erase(ctx);
            return CallResult{SilKit::Services::Rpc::RpcCallStatus::Timeout, {}, true};
        }
        return future.get();
    }

    std::unique_ptr<SilKit::IParticipant> participant_;
    SilKit::Services::Orchestration::ISystemMonitor* monitor_{nullptr};
    SilKit::Services::Orchestration::ILifecycleService* lifecycle_{nullptr};
    std::future<SilKit::Services::Orchestration::ParticipantState> finalState_;
    SilKit::Services::Orchestration::OperationMode mode_;
    bool stopRequested_{false};

    std::mutex mutex_;
    std::condition_variable connectedCv_;
    std::set<std::string> connected_;
    std::map<void*, std::shared_ptr<std::promise<CallResult>>> pending_;
    std::uintptr_t nextContext_{1};
};

} // namespace test
