// SPDX-FileCopyrightText: 2026 Vector Informatik GmbH
// SPDX-License-Identifier: MIT
#pragma once

#include <memory>
#include <string>

#include "silkit/SilKit.hpp"
#include "silkit/vendor/CreateSilKitRegistry.hpp"

// Test harness helpers for integration tests that need a live SIL Kit system.
//
// LiveRegistry starts an in-process SIL Kit registry on the wildcard URI
// "silkit://localhost:0". The Vendor API returns the actually-bound URI (with a
// random free port) from StartListening, which Uri() exposes. Every test gets
// its own registry on its own port, so tests are idempotent and parallel-safe -
// no fixed port, no external sil-kit-registry process.

namespace test
{

inline auto MakeLoggingConfig(const std::string& level)
    -> std::shared_ptr<SilKit::Config::IParticipantConfiguration>
{
    // A single stdout sink at the requested level. Use "Off" to stay quiet.
    return SilKit::Config::ParticipantConfigurationFromString("Logging:\n  Sinks:\n    - Type: Stdout\n      Level: "
                                                              + level + "\n");
}

class LiveRegistry
{
public:
    explicit LiveRegistry(const std::string& logLevel = "Off")
    {
        registry_ = SilKit::Vendor::Vector::CreateSilKitRegistry(MakeLoggingConfig(logLevel));
        uri_ = registry_->StartListening("silkit://localhost:0");
    }

    auto Uri() const -> const std::string& { return uri_; }

private:
    std::unique_ptr<SilKit::Vendor::Vector::ISilKitRegistry> registry_;
    std::string uri_;
};

} // namespace test
