// SPDX-FileCopyrightText: 2026 Vector Informatik GmbH
// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <functional>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

#include "Print.hpp"
#include "VhalProperty.hpp"

struct CarProperty
{
    using Value = std::variant<
        std::monostate,      // Unknown/empty
        std::string,         // String
        int32_t,             // Int32 and Boolean
        int64_t,             // Int64
        float,               // Float
        std::vector<uint8_t> // Bytes
        >;

    std::string name;
    Value value;
    int64_t timestamp{};

    CarProperty() = default;
    CarProperty(CarProperty&&) = default;
    CarProperty& operator=(CarProperty&&) = default;
    CarProperty(const CarProperty&) = default;
    CarProperty& operator=(const CarProperty&) = default;

    CarProperty(std::string n, std::string v) : name(std::move(n)), value(std::move(v)) {}
    CarProperty(std::string n, const char* v) : name(std::move(n)), value(std::string(v)) {}
    CarProperty(std::string n, bool v) : name(std::move(n)), value(v ? 1 : 0) {}
    CarProperty(std::string n, int32_t v) : name(std::move(n)), value(v) {}
    CarProperty(std::string n, int64_t v) : name(std::move(n)), value(v) {}
    CarProperty(std::string n, float v) : name(std::move(n)), value(v) {}
    CarProperty(std::string n, std::vector<uint8_t> v) : name(std::move(n)), value(std::move(v)) {}

    template<typename T>
    std::optional<T> Get() const
    {
        if (auto* ptr = std::get_if<T>(&value))
        {
            return *ptr;
        }
        return std::nullopt;
    }

    template<typename T>
    bool Holds() const
    {
        return std::holds_alternative<T>(value);
    }

    bool IsEmpty() const
    {
        return std::holds_alternative<std::monostate>(value);
    }
};

inline auto operator<<(std::ostream& os, const CarProperty& prop) -> std::ostream&
{
    os << "CarProperty{name=" << prop.name << ", timestamp=" << prop.timestamp << ", ";
    std::visit(
        [&os](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::monostate>)
                os << "value=<empty>";
            else if constexpr (std::is_same_v<T, std::string>)
                os << "value=\"" << arg << "\"";
            else if constexpr (std::is_same_v<T, bool>)
                os << "value=" << (arg ? "true" : "false");
            else if constexpr (std::is_same_v<T, int32_t>)
                os << "value=" << arg << " (int32)";
            else if constexpr (std::is_same_v<T, int64_t>)
                os << "value=" << arg << " (int64)";
            else if constexpr (std::is_same_v<T, float>)
                os << "value=" << arg << " (float)";
            else if constexpr (std::is_same_v<T, std::vector<uint8_t>>)
                os << "value=<" << arg.size() << " bytes>";
        },
        prop.value);
    os << "}";
    return os;
}

template<>
struct fmt::formatter<CarProperty> : fmt::formatter<std::string>
{
    auto format(const CarProperty& prop, fmt::format_context& ctx) const
    {
        std::ostringstream oss;
        oss << prop;
        return fmt::formatter<std::string>::format(oss.str(), ctx);
    }
};

// Names for the CarProperty variant arms, so a diagnostic can say which type was
// found and which was asked for. The arms mirror operator<< above.
inline auto CarPropertyTypeName(const CarProperty::Value& value) -> const char*
{
    return std::visit([](auto&& arg) -> const char* {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::monostate>)
            return "<empty>";
        else if constexpr (std::is_same_v<T, std::string>)
            return "string";
        else if constexpr (std::is_same_v<T, int32_t>)
            return "int32";
        else if constexpr (std::is_same_v<T, int64_t>)
            return "int64";
        else if constexpr (std::is_same_v<T, float>)
            return "float";
        else
            return "bytes";
    }, value);
}

template <typename T>
constexpr auto CarPropertyTypeNameOf() -> const char*
{
    if constexpr (std::is_same_v<T, std::string>)
        return "string";
    else if constexpr (std::is_same_v<T, int32_t>)
        return "int32";
    else if constexpr (std::is_same_v<T, int64_t>)
        return "int64";
    else if constexpr (std::is_same_v<T, float>)
        return "float";
    else if constexpr (std::is_same_v<T, std::vector<uint8_t>>)
        return "bytes";
    else
        return "<unsupported>";
}

using PropertyChangedHandler = std::function<void(const CarProperty&)>;

// ---- Property configuration ----
//
// What a backend can say about a property without reading its value. This is
// the model behind the VhalSimple GetPropertyStatus method, kept here in
// backend terms rather than as that method's bitfield: IVhalClient describes
// the VHAL, and it must not depend on the CANoe-facing contract.
//
// The enums restate the AOSP proto values numerically rather than including
// them. That is deliberate and load-bearing - this header is protobuf-free, and
// Test_VhalMockClient links neither proto_lib nor SilKit, which is what keeps a
// failure at the mock layer attributable to the mock. VhalGrpcConvert.cpp is the
// one translation unit that sees both, and static_asserts the correspondence.

//! Mirrors android...vehicle.proto.VehiclePropertyAccess. A bitmask: READ_WRITE
//! is READ | WRITE, which is why IsReadable/IsWritable test bits.
enum class PropertyAccess : int32_t
{
    None = 0,
    Read = 1,
    Write = 2,
    ReadWrite = 3,
};

//! Which CarProperty::Value arm a property uses, or None for a type VhalSimple
//! cannot represent (AOSP's INT32_VEC, FLOAT_VEC, MIXED) or a name outside the
//! AOSP table. Mirrors the variant arms, like CarPropertyTypeName above.
enum class CarPropertyType : int32_t
{
    None = 0,
    Int32 = 1, //!< also AOSP BOOLEAN, carried as 0/1
    Int64 = 2,
    Float = 3,
    String = 4,
    Bytes = 5,
};

//! The CarPropertyType an AOSP type maps to. One definition, used by both the
//! mock and the gRPC client: they answer the same question, so a second copy is
//! how --mock and a device would quietly start disagreeing about a property.
//!
//! VhalProperty.hpp is the compiled-in AOSP name/id/type table and is itself
//! protobuf-free, so including it here does not break this header's rule.
inline auto ToCarPropertyType(android::hardware::automotive::vehicle::VehiclePropertyType declared) -> CarPropertyType
{
    using T = android::hardware::automotive::vehicle::VehiclePropertyType;
    switch (declared)
    {
    case T::BOOLEAN:
    case T::INT32:
        return CarPropertyType::Int32;
    case T::INT64:
        return CarPropertyType::Int64;
    case T::FLOAT:
        return CarPropertyType::Float;
    case T::STRING:
        return CarPropertyType::String;
    case T::BYTES:
        return CarPropertyType::Bytes;
    default:
        // Vector and MIXED types, and UNKNOWN for a name outside the table.
        return CarPropertyType::None;
    }
}

//! One entry of the VHAL's property configuration table.
struct PropertyInfo
{
    int32_t id{0}; //!< the AOSP property id, or 0 for a name outside the table
    PropertyAccess access{PropertyAccess::None};
    CarPropertyType type{CarPropertyType::None};

    auto IsReadable() const -> bool
    {
        return (static_cast<int32_t>(access) & static_cast<int32_t>(PropertyAccess::Read)) != 0;
    }

    auto IsWritable() const -> bool
    {
        return (static_cast<int32_t>(access) & static_cast<int32_t>(PropertyAccess::Write)) != 0;
    }
};

class IVhalClient
{
public:
    virtual ~IVhalClient() = default;
    virtual auto GetProperty(std::string_view name) -> std::optional<CarProperty> = 0;
    virtual auto GetProperties(std::vector<std::string_view> nameList) -> std::vector<std::optional<CarProperty>> = 0;
    virtual auto SetProperty(const CarProperty& prop) -> bool = 0;
    virtual auto AddPropertyChangedHandler(std::string_view propertyName, PropertyChangedHandler handler) -> int = 0;
    virtual auto RemovePropertyChangedHandler(int handlerId) -> void = 0;
    virtual auto GetAllProperties() -> std::vector<std::string> = 0;
    virtual bool CheckHealth() = 0;

    //! The configuration entry for `name`, or nullopt when this backend does not
    //! have that property.
    //!
    //! Throws when the configuration could not be read at all. A failure is not
    //! an absence, and collapsing the two is how a transport outage would reach
    //! a caller as "no such property" - the exact ambiguity the introspection
    //! method exists to remove.
    virtual auto GetPropertyInfo(std::string_view name) -> std::optional<PropertyInfo> = 0;
};
