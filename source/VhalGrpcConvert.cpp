// SPDX-FileCopyrightText: 2026 Vector Informatik GmbH
// SPDX-License-Identifier: MIT
#include "VhalGrpcConvert.hpp"

namespace proto = android::hardware::automotive::vehicle::proto;
using android::hardware::automotive::vehicle::VehicleProperty_GetNameById;
using android::hardware::automotive::vehicle::VehiclePropertyType;
using android::hardware::automotive::vehicle::getPropertyType;

namespace VhalConvert {

auto ValidateUri(const std::string& uri) -> std::string
{
    if (uri.rfind("/", 0) == 0)
    {
        return "unix://" + uri;
    }
    if (uri.rfind("unix://", 0) != 0 && uri.rfind("http://", 0) != 0 && uri.rfind("https://", 0) != 0)
    {
        throw VhalClientError("Invalid URI scheme. URI must start with unix://, http://, or https://");
    }
    return uri;
}

auto ToCarProperty(const proto::VehiclePropValue& value) -> std::optional<CarProperty>
{
    auto name = VehicleProperty_GetNameById(value.prop());
    auto type = getPropertyType(value.prop());
    // A message that carries no value for its type is normal: a VHAL that
    // declined the read still returns a result. Indexing a repeated field that
    // far is a protobuf CHECK failure in a debug build and an out-of-bounds read
    // in a release one, so every scalar case is guarded.
    switch (type)
    {
    case VehiclePropertyType::STRING:
        return CarProperty{name, value.string_value()};
    case VehiclePropertyType::BOOLEAN:
        if (value.int32_values_size() == 0)
            return std::nullopt;
        return CarProperty{name, value.int32_values(0) != 0};
    case VehiclePropertyType::INT32:
        if (value.int32_values_size() == 0)
            return std::nullopt;
        return CarProperty{name, value.int32_values(0)};
    case VehiclePropertyType::INT64:
        if (value.int64_values_size() == 0)
            return std::nullopt;
        return CarProperty{name, value.int64_values(0)};
    case VehiclePropertyType::FLOAT:
        if (value.float_values_size() == 0)
            return std::nullopt;
        return CarProperty{name, value.float_values(0)};
    case VehiclePropertyType::BYTES:
        return CarProperty{name, std::vector<uint8_t>(value.byte_values().begin(), value.byte_values().end())};
    default:
        return std::nullopt;
    }
}

auto ToCarProperty(const proto::GetValueResult& result) -> std::optional<CarProperty>
{
    // The status gate comes first. A NOT_AVAILABLE or ACCESS_DENIED result can
    // still carry a value field, and converting it would hand the caller a
    // number indistinguishable from a real reading.
    if (result.status() != proto::StatusCode::OK)
    {
        return std::nullopt;
    }
    return ToCarProperty(result.value());
}

auto FirstSetError(const proto::SetValueResults& results) -> std::optional<std::string>
{
    for (const auto& result : results.results())
    {
        if (result.status() != proto::StatusCode::OK)
        {
            return "VHAL rejected the write with status " + proto::StatusCode_Name(result.status()) + " (request_id "
                   + std::to_string(result.request_id()) + ")";
        }
    }
    return std::nullopt;
}

} // namespace VhalConvert
