// SPDX-FileCopyrightText: 2026 Vector Informatik GmbH
// SPDX-License-Identifier: MIT
#include "VhalGrpcConvert.hpp"

#include <type_traits>
#include <variant>

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

auto ToPropValue(int32_t propId, const CarProperty::Value& value) -> proto::VehiclePropValue
{
    using android::hardware::automotive::vehicle::VehiclePropertyType_Name;

    proto::VehiclePropValue out;
    out.set_prop(propId);
    const auto declared = getPropertyType(propId);

    auto reject = [&](const char* offered) {
        throw VhalClientError("SetValues: property " + std::string(VehicleProperty_GetNameById(propId)) + " expects "
                              + std::string(VehiclePropertyType_Name(declared)) + ", not " + offered);
    };

    std::visit(
        [&](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::monostate>)
            {
                reject("an empty value");
            }
            else if constexpr (std::is_same_v<T, std::string>)
            {
                if (declared != VehiclePropertyType::STRING)
                    reject("STRING");
                out.set_string_value(arg);
            }
            else if constexpr (std::is_same_v<T, std::vector<uint8_t>>)
            {
                // BYTES lives in byte_values. Writing it into int32_values is the
                // bug this function exists to make untestable-by-accident no more.
                if (declared != VehiclePropertyType::BYTES)
                    reject("BYTES");
                out.set_byte_values(std::string(arg.begin(), arg.end()));
            }
            else if constexpr (std::is_same_v<T, float>)
            {
                if (declared != VehiclePropertyType::FLOAT && declared != VehiclePropertyType::FLOAT_VEC)
                    reject("FLOAT");
                out.mutable_float_values()->Add(arg);
            }
            else if constexpr (std::is_same_v<T, int64_t>)
            {
                if (declared != VehiclePropertyType::INT64 && declared != VehiclePropertyType::INT64_VEC)
                    reject("INT64");
                out.mutable_int64_values()->Add(arg);
            }
            else // int32_t, which also carries AOSP BOOLEAN as 0/1
            {
                if (declared != VehiclePropertyType::INT32 && declared != VehiclePropertyType::INT32_VEC
                    && declared != VehiclePropertyType::BOOLEAN)
                    reject("INT32");
                out.mutable_int32_values()->Add(arg);
            }
        },
        value);
    return out;
}

// PropertyAccess restates these numbers so that IVhalClient.hpp stays
// protobuf-free. This is the one translation unit that sees both, so it is the
// only place the correspondence can be checked - and a silent divergence would
// report readable properties as inaccessible.
static_assert(static_cast<int32_t>(PropertyAccess::None) == proto::VehiclePropertyAccess::NONE, "");
static_assert(static_cast<int32_t>(PropertyAccess::Read) == proto::VehiclePropertyAccess::READ, "");
static_assert(static_cast<int32_t>(PropertyAccess::Write) == proto::VehiclePropertyAccess::WRITE, "");
static_assert(static_cast<int32_t>(PropertyAccess::ReadWrite) == proto::VehiclePropertyAccess::READ_WRITE, "");

auto ToPropertyInfo(const proto::VehiclePropConfig& config) -> PropertyInfo
{
    PropertyInfo info;
    info.id = config.prop();
    info.type = ToCarPropertyType(getPropertyType(config.prop()));

    auto access = static_cast<int32_t>(config.access());
    if (access == proto::VehiclePropertyAccess::NONE)
    {
        // AOSP V3+ per-area access. The union, not the intersection: if any area
        // of the vehicle can be read, the property is readable - GetPropertyStatus
        // answers about the property, and the SimpleApi has no area parameter to
        // narrow it with.
        for (const auto& area : config.area_configs())
        {
            access |= static_cast<int32_t>(area.access());
        }
    }
    info.access = static_cast<PropertyAccess>(access & static_cast<int32_t>(PropertyAccess::ReadWrite));
    return info;
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
