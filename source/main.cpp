// SPDX-FileCopyrightText: 2026 Vector Informatik GmbH
// SPDX-License-Identifier: MIT
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include "VhalSilKitAdapter.hpp"
#include "VhalGrpcClient.hpp"
#include "VhalMockClient.hpp"
#include "silkit/SilKitVersion.hpp"
#include "CommandlineParser.hpp"

#include "Print.hpp"

auto GetVersionString()
{
    std::string hash{SilKit::Version::GitHash()};
    auto shortHash = hash.substr(0, 7);
    return fmt::format(R"({{"AndroidBridge": "{}", "SilKit": {{"Version": "{}", "GitHash": "{}"}}}})", SK_VHAL_VERSION, SilKit::Version::String(), shortHash);
}

int main(int argc, char** argv) try {
    bool useMock{false};
    bool useSynchronized{false};
    std::string participantConfiguration{};
    std::string participantName{"VhalBridge"};
    std::string silkitUri{"silkit://localhost:8500"};
    std::string vhalUri{};
    std::string logLevel{"Info"};

    using CliParser = SilKit::Util::CommandlineParser;
    CliParser parser;
    parser.SetDescription("sil-kit-android-bridge - Connects a VHAL gRPC server to a SilKit participant");
    parser.Add<CliParser::Flag>("version", "v", "[--version]", "-v, --version: Get version info.");
    parser.Add<CliParser::Flag>("help", "h", "[--help]", "-h, --help: Get this help.");
    parser.Add<CliParser::Flag>("mock", "m", "[--mock]", "-m, --mock: Enable mock mode for the VHAL side.");
    parser.Add<CliParser::Flag>("synchronized", "s", "[--synchronized]", 
        "-s, --synchronized: Enable synchronized mode for the SIL Kit participant.");
    parser.Add<CliParser::Option>(
        "silkit-uri", "u", "silkit://localhost:8500", "[--silkit-uri <silkitUri>]",
        "-u, --silkit-uri <silkitUri>: The registry URI to connect to. Defaults to 'silkit://localhost:8500'.");
    parser.Add<CliParser::Option>(
        "vhal-uri", "V", "unix://tmp/vhal.sock", "[--vhal-uri <vhalUri>]",
        "-V, --vhal-uri <vhalUri>: The URI of the VHAL gRPC server to connect to. REQUIRED if not --mock is given.");
    parser.Add<CliParser::Option>("name", "n", "VhalBridge", "[--name <participantName>]",
                                             "-n, --name <participantName>: The participant name used to take "
                                             "part in the simulation. Defaults to 'VhalBridge'.");
    parser.Add<CliParser::Option>(
        "configuration", "c", "", "[--configuration <filePath>]",
        "-c, --configuration <filePath>: Path to the Participant configuration YAML or JSON file. "
        "Note that the format was changed in v3.6.11. Cannot be used together with the '--log' option.");
    parser.Add<CliParser::Option>(
        "log", "l", "Info", "[--log <level>]",
        "-l, --log <level>: SIL Kit: Log to stdout with level 'Trace', 'Debug', 'Warn', 'Info', 'Error', 'Critical' or 'Off'. "
        "Defaults to 'info' if the '--configuration' option is not specified. Cannot be used together with the "
        "'--configuration' option.");
    try {
        parser.ParseArguments(argc, argv);
    } catch (const std::runtime_error& e) {
        std::cerr << "Error parsing arguments: " << e.what() << std::endl;
        return 1;
    }

     if (parser.Get<CliParser::Flag>("help").Value()) {
        parser.PrintUsageInfo(std::cout);
        return 0;
     }
     if (parser.Get<CliParser::Flag>("version").Value())
     {
         std::cout << GetVersionString() << std::endl;
         return 0;
     }
     if(parser.Get<CliParser::Flag>("mock").Value())
     {
         useMock = true;
     }
     if(parser.Get<CliParser::Flag>("synchronized").Value())
     {
         useSynchronized = true;
     }
     if (parser.Get<CliParser::Option>("configuration").HasValue())
     {
         participantConfiguration = parser.Get<CliParser::Option>("configuration").Value();
     }
     if (parser.Get<CliParser::Option>("name").HasValue())
     {
         participantName = parser.Get<CliParser::Option>("name").Value();
     }
     if (parser.Get<CliParser::Option>("silkit-uri").HasValue())
     {
         silkitUri = parser.Get<CliParser::Option>("silkit-uri").Value();
     }
     if (parser.Get<CliParser::Option>("vhal-uri").HasValue())
     {
         vhalUri = parser.Get<CliParser::Option>("vhal-uri").Value();
     }
     if(vhalUri.empty() && !useMock)
     {
        std::cerr << "Error: --vhal-uri option or --mock option is required." << std::endl;
        return 1;
     }
     if (parser.Get<CliParser::Option>("log").HasValue())
     {
         logLevel = parser.Get<CliParser::Option>("log").Value();
     }

     std::unique_ptr<IVhalClient> client;
     if (useMock) {
         client = std::make_unique<VhalMockClient>(vhalUri);
     } else {
         client = std::make_unique<VhalGrpcClient>(vhalUri);
     }

     VhalSilKitAdapter adapter;
     adapter.Init(silkitUri, participantName, participantConfiguration, logLevel, useSynchronized,
                  std::move(client), "Default", "VhalSilkit", "vhalProperties");
     adapter.Run();
     adapter.Shutdown();

     return 0;
}
catch (const std::exception& e) {
    std::cerr << "Fatal Exception: " << e.what() << std::endl;
    return 1;
}