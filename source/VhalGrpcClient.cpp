// SPDX-FileCopyrightText: 2026 Vector Informatik GmbH
// SPDX-License-Identifier: MIT
#include <variant>

#include "VhalGrpcClient.hpp"
#include "VhalGrpcConvert.hpp"
#include "Print.hpp"

// Dial the URI, then hand the channel to the delegated-to constructor. Keeping
// ValidateUri here means every production path still rejects a malformed URI;
// the channel constructor is reached only by a caller that already has one.
VhalGrpcClient::VhalGrpcClient(const std::string& uri)
    : VhalGrpcClient(grpc::CreateChannel(VhalConvert::ValidateUri(uri), grpc::InsecureChannelCredentials()))
{
}

VhalGrpcClient::VhalGrpcClient(std::shared_ptr<grpc::Channel> channel)
    : stub_(VehicleServer::NewStub(std::move(channel)))
{
}

auto VhalGrpcClient::GetProperty(std::string_view name) -> std::optional<CarProperty>
{
    auto results = GetProperties({name});
    if (!results.empty())
    {
        return results[0];
    }
    return std::nullopt;
}

auto VhalGrpcClient::GetProperties(std::vector<std::string_view> nameList) -> std::vector<std::optional<CarProperty>>
{
    // One slot per requested name, filled by request_id rather than by arrival
    // order. A VHAL promises nothing about the order it answers in and may omit
    // a result entirely; attributing result i to name i turns either of those
    // into another property's value returned under the name that was asked for.
    // The adapter only type-checks what it gets back, so a same-typed mix-up
    // would reach CANoe looking perfectly correct.
    std::vector<std::optional<CarProperty>> results(nameList.size());
    auto&& values = GetValues(nameList);
    for (auto&& result : values.results())
    {
        const auto index = result.request_id();
        if (index < 0 || static_cast<size_t>(index) >= results.size())
        {
            // A result for something that was never requested. Dropping it is
            // the only safe option: placing it anywhere would displace a real one.
            continue;
        }
        // ToCarProperty(GetValueResult) yields nullopt for a non-OK StatusCode,
        // so a refused read arrives as "no value" rather than as data.
        VhalConvert::ToCarProperty(result).swap(results[static_cast<size_t>(index)]);
    }
    return results;
}

auto VhalGrpcClient::RemovePropertyChangedHandler(int) -> void {}

auto VhalGrpcClient::SetProperty(const CarProperty& cp) -> bool
{
    auto id = VehicleProperty_GetIdByName(cp.name.c_str());
    if (id == 0)
    {
        return false;
    }

    if (std::holds_alternative<std::monostate>(cp.value))
    {
        return false;
    }

    // The type checking and protobuf-field selection live in VhalGrpcConvert, where
    // they are unit tested; this function only moves the bytes.
    VehiclePropValueRequests request;
    auto* req = request.add_requests();
    req->set_request_id(1);
    *req->mutable_value() = VhalConvert::ToPropValue(id, cp.value);

    SetValueResults response;
    grpc::ClientContext context;
    grpc::Status status = stub_->SetValues(&context, request, &response);
    if (!status.ok())
    {
        throw VhalClientError("SetValues failed: " + status.error_message());
    }
    if (auto err = VhalConvert::FirstSetError(response))
    {
        throw VhalClientError("SetValues on property " + cp.name + ": " + *err);
    }
    return true;
}

auto VhalGrpcClient::AddPropertyChangedHandler(std::string_view, PropertyChangedHandler) -> int
{
    return 0;
}

auto VhalGrpcClient::LoadPropertyConfigs() -> std::map<std::string, PropertyInfo>
{
    std::map<std::string, PropertyInfo> loaded;

    google::protobuf::Empty request;
    grpc::ClientContext context;

    std::unique_ptr<grpc::ClientReader<VehiclePropConfig>> reader(
        stub_->GetAllPropertyConfig(&context, request));

    VehiclePropConfig config;
    while (reader->Read(&config))
    {
        loaded.emplace(VehicleProperty_GetNameById(config.prop()), VhalConvert::ToPropertyInfo(config));
    }
    // A stream that dies partway leaves a short list indistinguishable from a
    // complete one unless the final status is checked.
    grpc::Status status = reader->Finish();
    if (!status.ok())
    {
        throw VhalClientError("GetAllPropertyConfig failed: " + status.error_message());
    }
    return loaded;
}

void VhalGrpcClient::EnsureConfigCache()
{
    if (configLoaded_)
    {
        return;
    }
    // Completed into a local before anything is committed, so a truncated
    // stream cannot leave a half-populated cache behind. configLoaded_ is set
    // only on success: a failure is retried by the next call rather than
    // remembered as an empty table, which would answer "no such property"
    // forever after one transport blip.
    auto loaded = LoadPropertyConfigs();
    configByName_ = std::move(loaded);
    configLoaded_ = true;
}

auto VhalGrpcClient::GetAllProperties() -> std::vector<std::string>
{
    std::lock_guard<std::mutex> lock{configMutex_};
    EnsureConfigCache();

    std::vector<std::string> result;
    result.reserve(configByName_.size());
    for (const auto& [name, info] : configByName_)
    {
        result.push_back(name);
    }
    return result;
}

auto VhalGrpcClient::GetPropertyInfo(std::string_view name) -> std::optional<PropertyInfo>
{
    std::lock_guard<std::mutex> lock{configMutex_};
    EnsureConfigCache();

    auto it = configByName_.find(std::string(name));
    if (it == configByName_.end())
    {
        return std::nullopt;
    }
    return it->second;
}

bool VhalGrpcClient::CheckHealth()
{
    google::protobuf::Empty request;
    VehicleHalCallStatus response;
    grpc::ClientContext context;
    grpc::Status status = stub_->CheckHealth(&context, request, &response);
    // The transport status only says the call arrived. A VHAL that is up but not
    // usable answers it successfully and reports the problem in the payload, so
    // checking only the transport reads an unhealthy VHAL as healthy.
    // VhalSilKitAdapter::Init refuses to start when this returns false, which
    // makes it the bridge's only startup gate on the VHAL being usable.
    return status.ok() && response.status_code() == StatusCode::OK;
}

bool VhalGrpcClient::PrintAllProperties(std::ostream& out)
{
    google::protobuf::Empty request;
    grpc::ClientContext context;

    std::unique_ptr<grpc::ClientReader<VehiclePropConfig>> reader(
        stub_->GetAllPropertyConfig(&context, request));

    VehiclePropConfig config;
    while (reader->Read(&config))
    {
        out << "Property: prop=" << VehicleProperty_GetNameById(config.prop())
            << ", access=" << VehiclePropertyAccess_Name(config.access())
            << ", change_mode=" << VehiclePropertyChangeMode_Name(config.change_mode()) << std::endl;
    }
    grpc::Status status = reader->Finish();
    return status.ok();
}

GetValueResults VhalGrpcClient::GetValues(const std::vector<std::string_view>& prop_ids_by_name)
{
    std::vector<int32_t> prop_ids;
    for (const auto& name : prop_ids_by_name)
    {
        int32_t id = VehicleProperty_GetIdByName(name);
        if (id != 0)
        {
            prop_ids.push_back(id);
        }
        else
        {
            throw VhalClientError("Unknown property name: " + std::string(name));
        }
    }
    return GetValues(prop_ids);
}

GetValueResults VhalGrpcClient::GetValues(const std::vector<int32_t>& prop_ids)
{
    VehiclePropValueRequests request;
    for (size_t i = 0; i < prop_ids.size(); ++i)
    {
        auto* req = request.add_requests();
        // The request index, not the property id. It is what lets a caller line
        // the results back up with its own request list, and unlike the id it
        // stays unique when the same property is asked for twice in one call.
        req->set_request_id(static_cast<int64_t>(i));
        req->mutable_value()->set_prop(prop_ids[i]);
    }

    GetValueResults response;
    grpc::ClientContext context;
    grpc::Status status = stub_->GetValues(&context, request, &response);
    if (!status.ok())
    {
        throw VhalClientError("GetValues failed: " + status.error_message());
    }
    return response;
}

void VhalGrpcClient::StartPropertyValuesStream()
{
    google::protobuf::Empty request;
    grpc::ClientContext context;

    std::unique_ptr<grpc::ClientReader<VehiclePropValues>> reader(
        stub_->StartPropertyValuesStream(&context, request));

    VehiclePropValues values;
    while (reader->Read(&values))
    {
        for (const auto& value : values.values())
        {
            std::cout << "Received update: prop=" << value.prop() << ", timestamp=" << value.timestamp()
                      << std::endl;
        }
    }
    grpc::Status status = reader->Finish();
    if (!status.ok())
    {
        throw VhalClientError("Stream ended: " + status.error_message());
    }
}
