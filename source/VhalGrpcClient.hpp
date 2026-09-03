// SPDX-FileCopyrightText: 2026 Vector Informatik GmbH
// SPDX-License-Identifier: MIT
#pragma once

#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include <grpcpp/grpcpp.h>
// winsock2.h (pulled in by grpcpp on Windows) defines TRY_AGAIN=4 and ACCESS_DENIED=5
// as macros. These names clash with enum values in the VHAL protobuf-generated headers.
#ifdef TRY_AGAIN
#undef TRY_AGAIN
#endif
#ifdef ACCESS_DENIED
#undef ACCESS_DENIED
#endif
#include "VehicleServer.grpc.pb.h"
#include "VhalProperty.hpp"

#include "IVhalClient.hpp"
#include "VhalGrpcConvert.hpp"

using namespace android::hardware::automotive::vehicle;
using namespace android::hardware::automotive::vehicle::proto;

class VhalGrpcClient : public IVhalClient
{
public:
    VhalGrpcClient(const std::string& uri);

    auto GetProperty(std::string_view name) -> std::optional<CarProperty> override;
    auto GetProperties(std::vector<std::string_view> nameList) -> std::vector<std::optional<CarProperty>> override;
    auto SetProperty(const CarProperty& property) -> bool override;
    auto AddPropertyChangedHandler(std::string_view propertyName, PropertyChangedHandler handler) -> int override;
    auto RemovePropertyChangedHandler(int handlerId) -> void override;
    auto GetAllProperties() -> std::vector<std::string> override;
    bool CheckHealth() override;

    bool PrintAllProperties(std::ostream& output);

    GetValueResults GetValues(const std::vector<std::string_view>& prop_ids_by_name);
    GetValueResults GetValues(const std::vector<int32_t>& prop_ids);

    template<typename T>
    void SetValues(int32_t prop_id, T value)
    {
        VehiclePropValueRequests request;
        auto* req = request.add_requests();
        req->set_request_id(1);
        auto* prop_value = req->mutable_value();
        prop_value->set_prop(prop_id);

        auto type = getPropertyType(prop_id);

        if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, const char*>)
        {
            if (type != VehiclePropertyType::STRING)
            {
                throw VhalClientError("SetValues: property " + std::string(VehicleProperty_GetNameById(prop_id))
                                      + " expects " + std::string(VehiclePropertyType_Name(type)) + ", not STRING");
            }
            prop_value->set_string_value(value);
        }
        else if constexpr (std::is_same_v<T, bool>)
        {
            if (type != VehiclePropertyType::BOOLEAN)
            {
                throw VhalClientError("SetValues: property " + std::string(VehicleProperty_GetNameById(prop_id))
                                      + " expects " + std::string(VehiclePropertyType_Name(type)) + ", not BOOLEAN");
            }
            prop_value->mutable_int32_values()->Add(value ? 1 : 0);
        }
        else if constexpr (std::is_floating_point_v<T>)
        {
            if (type != VehiclePropertyType::FLOAT && type != VehiclePropertyType::FLOAT_VEC)
            {
                throw VhalClientError("SetValues: property " + std::string(VehicleProperty_GetNameById(prop_id))
                                      + " expects " + std::string(VehiclePropertyType_Name(type)) + ", not FLOAT");
            }
            prop_value->mutable_float_values()->Add(static_cast<float>(value));
        }
        else if constexpr (std::is_integral_v<T> && sizeof(T) <= 4)
        {
            if (type != VehiclePropertyType::INT32 && type != VehiclePropertyType::INT32_VEC
                && type != VehiclePropertyType::BOOLEAN)
            {
                throw VhalClientError("SetValues: property " + std::string(VehicleProperty_GetNameById(prop_id))
                                      + " expects " + std::string(VehiclePropertyType_Name(type)) + ", not INT32");
            }
            prop_value->mutable_int32_values()->Add(static_cast<int32_t>(value));
        }
        else if constexpr (std::is_integral_v<T> && sizeof(T) > 4)
        {
            if (type != VehiclePropertyType::INT64 && type != VehiclePropertyType::INT64_VEC)
            {
                throw VhalClientError("SetValues: property " + std::string(VehicleProperty_GetNameById(prop_id))
                                      + " expects " + std::string(VehiclePropertyType_Name(type)) + ", not INT64");
            }
            prop_value->mutable_int64_values()->Add(static_cast<int64_t>(value));
        }
        else
        {
            static_assert(!sizeof(T), "Unsupported type for SetValues");
        }

        SetValueResults response;
        grpc::ClientContext context;
        grpc::Status status = stub_->SetValues(&context, request, &response);
        if (!status.ok())
        {
            throw VhalClientError("SetValues failed: " + status.error_message());
        }
        // The transport status only says the call arrived. A VHAL that accepts
        // the call and refuses the write reports that per result.
        if (auto err = VhalConvert::FirstSetError(response))
        {
            throw VhalClientError("SetValues on property "
                                  + std::string(VehicleProperty_GetNameById(prop_id)) + ": " + *err);
        }
    }

    template<typename T>
    void SetValues(int32_t prop_id, const std::vector<T>& values)
    {
        VehiclePropValueRequests request;
        auto* req = request.add_requests();
        req->set_request_id(1);
        auto* prop_value = req->mutable_value();
        prop_value->set_prop(prop_id);

        auto type = getPropertyType(prop_id);

        // A vector<uint8_t> is how CarProperty models a BYTES property, and the VHAL
        // carries those in byte_values - not in int32_values. This branch has to come
        // before the integral one below, which would otherwise claim uint8_t and
        // reject every BYTES write with "expects BYTES, not INT32_VEC".
        if constexpr (std::is_same_v<T, uint8_t>)
        {
            if (type != VehiclePropertyType::BYTES)
            {
                throw VhalClientError("SetValues: property " + std::string(VehicleProperty_GetNameById(prop_id))
                                      + " expects " + std::string(VehiclePropertyType_Name(type)) + ", not BYTES");
            }
            prop_value->set_byte_values(std::string(values.begin(), values.end()));
        }
        else if constexpr (std::is_floating_point_v<T>)
        {
            if (type != VehiclePropertyType::FLOAT_VEC && type != VehiclePropertyType::FLOAT)
            {
                throw VhalClientError("SetValues: property " + std::string(VehicleProperty_GetNameById(prop_id))
                                      + " expects " + std::string(VehiclePropertyType_Name(type)) + ", not FLOAT_VEC");
            }
            for (const auto& v : values)
                prop_value->mutable_float_values()->Add(static_cast<float>(v));
        }
        else if constexpr (std::is_integral_v<T> && sizeof(T) <= 4)
        {
            if (type != VehiclePropertyType::INT32_VEC && type != VehiclePropertyType::INT32)
            {
                throw VhalClientError("SetValues: property " + std::string(VehicleProperty_GetNameById(prop_id))
                                      + " expects " + std::string(VehiclePropertyType_Name(type)) + ", not INT32_VEC");
            }
            for (const auto& v : values)
                prop_value->mutable_int32_values()->Add(static_cast<int32_t>(v));
        }
        else if constexpr (std::is_integral_v<T> && sizeof(T) > 4)
        {
            if (type != VehiclePropertyType::INT64_VEC && type != VehiclePropertyType::INT64)
            {
                throw VhalClientError("SetValues: property " + std::string(VehicleProperty_GetNameById(prop_id))
                                      + " expects " + std::string(VehiclePropertyType_Name(type)) + ", not INT64_VEC");
            }
            for (const auto& v : values)
                prop_value->mutable_int64_values()->Add(static_cast<int64_t>(v));
        }
        else
        {
            static_assert(!sizeof(T), "Unsupported type for SetValues vector");
        }

        SetValueResults response;
        grpc::ClientContext context;
        grpc::Status status = stub_->SetValues(&context, request, &response);
        if (!status.ok())
        {
            throw VhalClientError("SetValues failed: " + status.error_message());
        }
        // The transport status only says the call arrived. A VHAL that accepts
        // the call and refuses the write reports that per result.
        if (auto err = VhalConvert::FirstSetError(response))
        {
            throw VhalClientError("SetValues on property "
                                  + std::string(VehicleProperty_GetNameById(prop_id)) + ": " + *err);
        }
    }

    void StartPropertyValuesStream();

private:
    std::unique_ptr<VehicleServer::Stub> stub_;
    std::map<int, std::pair<std::string, PropertyChangedHandler>> handlers_;
    int nextHandlerId_ = 1;
};

inline void printResults(const GetValueResults& results, std::ostream& out = std::cout)
{
    for (const auto& result : results.results())
    {
        out << "GetValue prop=" << VehicleProperty_GetNameById(result.value().prop())
            << ", timestamp=" << result.value().timestamp()
            << ", value=" << VehiclePropValue_ValueString(result.value()) << std::endl;
    }
}
