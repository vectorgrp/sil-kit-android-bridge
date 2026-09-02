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

// SkParticipant: a thin, reusable SIL Kit participant wrapper for tests. It is
// not specific to any role - a test composes a "fake CANoe" by adding RpcClients
// with the desired RpcSpec and issuing synchronous calls.
//
// The participant runs an autonomous lifecycle (started, non-blocking, in the
// ctor). RPC results arrive asynchronously on a SIL Kit worker thread; Call()
// bridges that back to the calling thread via a per-call std::promise keyed by a
// unique context id.
//
// Timing discipline
// -----------------
// No assertion in a test may depend on how fast the machine is. A loaded CI
// runner can stall any of these steps for seconds, so nothing here treats "not
// finished yet" as "broken". Waiting is done on logical conditions:
//
//   WaitUntilConnected()      - the peer participant has joined the session
//                               (ISystemMonitor, event driven).
//   AwaitServerReachable()    - SIL Kit has matched an RpcClient to a server.
//                               SIL Kit publishes no event for RPC matching, so
//                               a successful call is the only sound proof that
//                               it happened; this retries until it does.
//   Call()                    - issues exactly one call and reports whatever
//                               status comes back. It never retries, so a test
//                               asserting on a status gets the real one.
//
// kWatchdog bounds every wait. It is NOT a correctness threshold and no test
// should ever come close to it: it exists only so that a genuinely broken build
// fails instead of hanging a CI job forever.

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

    // See "Timing discipline" above: a hang guard, not a deadline anyone races.
    static constexpr std::chrono::seconds kWatchdog{60};

    SkParticipant(const std::string& name, const std::string& registryUri,
                  SilKit::Services::Orchestration::OperationMode mode =
                      SilKit::Services::Orchestration::OperationMode::Autonomous,
                  const std::string& logLevel = "Off")
    {
        participant_ = SilKit::CreateParticipant(MakeLoggingConfig(logLevel), name, registryUri);

        // Installed before the lifecycle starts, so no peer can connect between
        // the monitor being created and the handler being armed.
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
            // Wait for the lifecycle to actually reach its final state before the
            // participant is destroyed. Dropping it while the stop is still in
            // flight makes peers log an ungraceful disconnect and leaves the next
            // fixture racing a half-closed session.
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

    // Blocks until `participantName` has joined the session. Event driven: it
    // returns the moment the connection is observed, however long that takes.
    // False means the watchdog expired, i.e. the peer never showed up at all.
    auto WaitUntilConnected(const std::string& participantName) -> bool
    {
        std::unique_lock<std::mutex> lock(mutex_);
        return connectedCv_.wait_for(lock, kWatchdog, [&] {
            // IsParticipantConnected() covers a peer that was already connected
            // before this call; the handler covers every later one.
            return connected_.count(participantName) != 0
                   || monitor_->IsParticipantConnected(participantName);
        });
    }

    // Issue exactly one call and wait for its result. No retrying, so the status
    // a test sees is the status SIL Kit produced. Timeout means the watchdog
    // expired without any result event at all.
    auto Call(SilKit::Services::Rpc::IRpcClient* client, SilKit::Util::Span<const uint8_t> request)
        -> CallResult
    {
        auto promise = std::make_shared<std::promise<CallResult>>();
        void* ctx;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            // A unique id per call, never a recycled address: an abandoned call
            // whose result arrives late must not be able to complete a later one.
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

    // Barrier for "this client has been matched to a server". SIL Kit exposes no
    // event for RPC matching, so the only sound proof is a call that succeeds;
    // ServerNotReachable before that means "not matched yet", never "wrong".
    // `request` must be a harmless probe - it really is executed by the server.
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
            // Backoff between attempts. Only throttles the retry loop; no
            // assertion depends on how long it takes to leave it.
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
