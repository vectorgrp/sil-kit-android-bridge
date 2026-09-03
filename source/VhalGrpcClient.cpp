// SPDX-FileCopyrightText: 2026 Vector Informatik GmbH
// SPDX-License-Identifier: MIT
#include "VhalGrpcClient.hpp"
#include "VhalGrpcConvert.hpp"
#include "Print.hpp"

VhalGrpcClient::VhalGrpcClient(const std::string& uri)
    : stub_(VehicleServer::NewStub(
          grpc::CreateChannel(VhalConvert::ValidateUri(uri), grpc::InsecureChannelCredentials())))
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
    std::vector<std::optional<CarProperty>> results;
    auto&& values = GetValues(nameList);
    for (auto&& result : values.results())
    {
        // ToCarProperty(GetValueResult) yields nullopt for a non-OK StatusCode,
        // so a refused read arrives as "no value" rather than as data.
        VhalConvert::ToCarProperty(result).swap(results.emplace_back());
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

    return std::visit(
        [this, id](auto&& arg) -> bool {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::monostate>)
            {
                return false;
            }
            else
            {
                SetValues(id, arg);
                return true;
            }
        },
        cp.value);
}

auto VhalGrpcClient::AddPropertyChangedHandler(std::string_view, PropertyChangedHandler) -> int
{
    return 0;
}

auto VhalGrpcClient::GetAllProperties() -> std::vector<std::string>
{
    std::vector<std::string> result;

    google::protobuf::Empty request;
    grpc::ClientContext context;

    std::unique_ptr<grpc::ClientReader<VehiclePropConfig>> reader(
        stub_->GetAllPropertyConfig(&context, request));

    VehiclePropConfig config;
    while (reader->Read(&config))
    {
        result.emplace_back(VehicleProperty_GetNameById(config.prop()));
    }
    (void)reader->Finish();
    return result;
}

bool VhalGrpcClient::CheckHealth()
{
    google::protobuf::Empty request;
    VehicleHalCallStatus response;
    grpc::ClientContext context;
    grpc::Status status = stub_->CheckHealth(&context, request, &response);
    return status.ok();
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
    for (int32_t prop_id : prop_ids)
    {
        auto* req = request.add_requests();
        req->set_request_id(prop_id);
        req->mutable_value()->set_prop(prop_id);
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
