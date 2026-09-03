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
};
