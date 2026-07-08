// SPDX-FileCopyrightText: 2026 Vector Informatik GmbH
// SPDX-License-Identifier: MIT
//
// Manual developer tool -- requires a running VHAL gRPC server. Not a CTest case.
// Usage: grpc-client-test <grpc-socket>
//   e.g. grpc-client-test /tmp/vsock_1_1000/vm.vsock
#include <iostream>
#include <memory>
#include <string>

#include "VhalGrpcClient.hpp"

int main(int argc, char** argv) try
{
    if (argc < 2)
    {
        std::cerr << "Usage: " << argv[0] << " <grpc-domain-socket>" << std::endl;
        std::cerr << "  e.g. /tmp/vsock_1_1000/vm.vsock" << std::endl;
        return 1;
    }

    std::string vhalClientTarget = argv[1];
    VhalGrpcClient client(vhalClientTarget);

    auto ok = client.CheckHealth();
    if (!ok)
    {
        std::cout << "VhalGrpcClient health check failed" << std::endl;
    }

    std::cout << "\n--- Property Configurations ---" << std::endl;
    ok = client.PrintAllProperties(std::cout);
    if (!ok)
    {
        std::cout << "VhalGrpcClient failed to print all properties" << std::endl;
    }

    std::cout << "\n--- Testing GetValues ---" << std::endl;
    auto&& results = client.GetValues({
        "EV_BATTERY_AVERAGE_TEMPERATURE",
        "EV_CHARGE_TIME_REMAINING",
        "EV_CURRENT_BATTERY_CAPACITY",
        "EV_CHARGE_PORT_CONNECTED",
        "EV_CHARGE_SWITCH",
        "EV_BATTERY_DISPLAY_UNITS",
        "EV_CHARGE_STATE",
        "INSTANTANEOUS_EV_EFFICIENCY",
        "EV_BATTERY_LEVEL",
        "EV_BATTERY_INSTANTANEOUS_CHARGE_RATE",
        "EV_CHARGE_PORT_OPEN",
        "EV_CHARGE_CURRENT_DRAW_LIMIT",
        "EV_CHARGE_PERCENT_LIMIT",
        "EV_CHARGE_SWITCH",
        "EV_CHARGE_TIME_REMAINING",
        "INFO_EV_BATTERY_CAPACITY",
        "INFO_EV_CONNECTOR_TYPE",
        "INFO_EV_PORT_LOCATION",
        "RANGE_REMAINING",
    });
    printResults(results);

    auto&& setSome = [&](auto&& name, auto&& value) {
        std::cout << "SetValues " << name << " to " << value << std::endl;
        client.SetValues(VehicleProperty_GetIdByName(name), value);
        auto r = client.GetValues({name});
        printResults(r);
    };

    setSome("RANGE_REMAINING", 1233000.0);
    setSome("EV_BATTERY_LEVEL", 4543.0);
    setSome("EV_CURRENT_BATTERY_CAPACITY", 100.0f);
    setSome("EV_BATTERY_INSTANTANEOUS_CHARGE_RATE", 1000.0f);

    return 0;
}
catch (const std::exception& e)
{
    std::cerr << "Fatal Exception: " << e.what() << std::endl;
    return 1;
}
