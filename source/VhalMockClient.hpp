// SPDX-FileCopyrightText: 2026 Vector Informatik GmbH
// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "IVhalClient.hpp"
#include "Print.hpp"
#include "VhalProperty.hpp"

// An in-memory stand-in for a real Android VHAL, used by --mock and by the
// integration tests. It exists so the bridge and a CANoe configuration can be
// exercised end to end with no device attached.
//
// Three properties of a real VHAL that this deliberately reproduces:
//
//   * Every seeded value has the type AOSP declares for that property id, so
//     GetFloat("EV_BATTERY_LEVEL") works and GetInt32 on it reports a mismatch -
//     exactly as against hardware. An earlier version stored whatever it was
//     handed, which made --mock disagree with the real client about half the
//     seeded properties and turned a correct CANoe test into a silent zero.
//   * A write of the wrong type is rejected rather than quietly changing the
//     stored type, mirroring the "expects FLOAT, not INT32" error
//     VhalGrpcClient::SetValues throws.
//   * GetPropertyInfo reports every stored property as READ_WRITE. That is not
//     the access AOSP declares, it is what this mock does: it hands back
//     everything it stores and accepts a write to any of it. Reporting INFO_VIN
//     as read-only while still accepting the write would make GetPropertyStatus
//     lie about the very backend it is describing. Against a device the access
//     is whatever the VHAL's configuration table says.
//
// Names outside the AOSP table (vendor or ad-hoc ids) are stored as given, since
// there is nothing to validate against.

class VhalMockClient : public IVhalClient
{
public:
    explicit VhalMockClient(const std::string& target);

    auto GetProperty(std::string_view name) -> std::optional<CarProperty> override;
    auto GetProperties(std::vector<std::string_view> nameList) -> std::vector<std::optional<CarProperty>> override;
    auto SetProperty(const CarProperty& property) -> bool override;
    auto AddPropertyChangedHandler(std::string_view propertyName, PropertyChangedHandler handler) -> int override;
    auto RemovePropertyChangedHandler(int handlerId) -> void override;
    auto GetAllProperties() -> std::vector<std::string> override;
    bool CheckHealth() override;
    auto GetPropertyInfo(std::string_view name) -> std::optional<PropertyInfo> override;

    void SimulatePropertyChange(const CarProperty& prop);

private:
    void Seed(CarProperty prop);

    std::map<std::string, CarProperty> properties_;
    std::map<int, std::pair<std::string, PropertyChangedHandler>> handlers_;
    int nextHandlerId_ = 1;
};

// ===== Implementation =====

namespace VhalMockDetail {

//! The AOSP type a property id declares, or UNKNOWN for a name outside the table.
inline auto DeclaredType(const std::string& name) -> android::hardware::automotive::vehicle::VehiclePropertyType
{
    using namespace android::hardware::automotive::vehicle;
    const auto id = VehicleProperty_GetIdByName(name);
    return id == 0 ? VehiclePropertyType::UNKNOWN : getPropertyType(id);
}

//! Whether `value` is the variant arm the VhalSimple API uses for `declared`.
//! AOSP BOOLEAN travels as int32 (0/1), which is why it shares an arm with INT32.
inline auto MatchesDeclaredType(android::hardware::automotive::vehicle::VehiclePropertyType declared,
                                const CarProperty::Value& value) -> bool
{
    using T = android::hardware::automotive::vehicle::VehiclePropertyType;
    switch (declared)
    {
    case T::STRING:
        return std::holds_alternative<std::string>(value);
    case T::BOOLEAN:
    case T::INT32:
        return std::holds_alternative<int32_t>(value);
    case T::INT64:
        return std::holds_alternative<int64_t>(value);
    case T::FLOAT:
        return std::holds_alternative<float>(value);
    case T::BYTES:
        return std::holds_alternative<std::vector<uint8_t>>(value);
    default:
        // Vector and MIXED types have no VhalSimple representation, so there is
        // nothing meaningful to check; accept whatever was stored.
        return true;
    }
}

} // namespace VhalMockDetail

inline VhalMockClient::VhalMockClient(const std::string& target)
{
    // One property per VhalSimple type, each with a distinctive value so a wrong
    // read is obvious rather than looking like a plausible zero. Types are the
    // ones AOSP declares - see the comment at the top of this file.

    // int32
    Seed({"INFO_MODEL_YEAR", int32_t{2026}});
    Seed({"EV_CHARGE_TIME_REMAINING", int32_t{1234}}); // seconds
    Seed({"EV_CHARGE_STATE", int32_t{3}});
    Seed({"GEAR_SELECTION", int32_t{4}});
    // int32 carrying an AOSP BOOLEAN
    Seed({"EV_CHARGE_PORT_CONNECTED", int32_t{1}});
    Seed({"NIGHT_MODE", int32_t{0}});

    // int64
    Seed({"ANDROID_EPOCH_TIME", int64_t{1767225600000}}); // 2026-01-01T00:00:00Z, in ms

    // float
    Seed({"EV_BATTERY_LEVEL", 88.5f});               // Wh
    Seed({"RANGE_REMAINING", 412.5f});               // metres
    Seed({"EV_CURRENT_BATTERY_CAPACITY", 64000.0f}); // Wh
    Seed({"INSTANTANEOUS_EV_EFFICIENCY", 6.25f});
    Seed({"PERF_VEHICLE_SPEED", 27.75f}); // m/s
    Seed({"PERF_ODOMETER", 123456.75f});  // metres

    // string
    Seed({"INFO_VIN", std::string{"WVWZZZ1JZXW000001"}});
    Seed({"INFO_MAKE", std::string{"Vector"}});
    Seed({"INFO_MODEL", std::string{"SIL Kit Android Bridge"}});

    // bytes
    Seed({"STORAGE_ENCRYPTION_BINDING_SEED", std::vector<uint8_t>{0xDE, 0xAD, 0xBE, 0xEF}});

    print("VhalMockClient initialized with target: {}", target);
    print("VhalMockClient mocking {} properties (int32, int64, float, string and bytes)", properties_.size());
}

inline void VhalMockClient::Seed(CarProperty prop)
{
    // A seed that disagrees with AOSP is a bug in the table above, not something
    // to tolerate at run time: it would make --mock behave unlike hardware.
    const auto declared = VhalMockDetail::DeclaredType(prop.name);
    if (!VhalMockDetail::MatchesDeclaredType(declared, prop.value))
    {
        throw std::logic_error(
            "VhalMockClient: seeded '" + prop.name + "' as " + CarPropertyTypeName(prop.value) + ", but AOSP declares "
            + std::string(android::hardware::automotive::vehicle::VehiclePropertyType_Name(declared)));
    }
    const auto name = prop.name;
    properties_[name] = std::move(prop);
}

inline auto VhalMockClient::GetProperty(std::string_view name) -> std::optional<CarProperty>
{
    print("VhalMockClient GetProperty called: {}", name);
    auto it = properties_.find(std::string(name));
    if (it != properties_.end())
    {
        return it->second;
    }
    return std::nullopt;
}

inline auto VhalMockClient::GetProperties(std::vector<std::string_view> nameList)
    -> std::vector<std::optional<CarProperty>>
{
    std::vector<std::optional<CarProperty>> result;
    result.reserve(nameList.size());
    for (const auto& name : nameList)
    {
        auto it = properties_.find(std::string(name));
        result.push_back(it != properties_.end() ? std::optional<CarProperty>{it->second} : std::nullopt);
    }
    return result;
}

inline auto VhalMockClient::SetProperty(const CarProperty& property) -> bool
{
    print("VhalMockClient SetProperty called: {} = {}", property.name, property);

    const auto declared = VhalMockDetail::DeclaredType(property.name);
    if (!VhalMockDetail::MatchesDeclaredType(declared, property.value))
    {
        // Same shape as the error VhalGrpcClient::SetValues throws, so a CANoe
        // test sees the same failure against --mock as against a device.
        throw std::runtime_error(
            "SetProperty: property " + property.name + " expects "
            + std::string(android::hardware::automotive::vehicle::VehiclePropertyType_Name(declared)) + ", not "
            + CarPropertyTypeName(property.value));
    }
    properties_[property.name] = property;
    return true;
}

inline auto VhalMockClient::AddPropertyChangedHandler(std::string_view propertyName, PropertyChangedHandler handler)
    -> int
{
    int id = nextHandlerId_++;
    handlers_[id] = {std::string(propertyName), handler};
    return id;
}

inline auto VhalMockClient::RemovePropertyChangedHandler(int handlerId) -> void
{
    handlers_.erase(handlerId);
}

inline auto VhalMockClient::GetAllProperties() -> std::vector<std::string>
{
    std::vector<std::string> result;
    result.reserve(properties_.size());
    for (const auto& [name, prop] : properties_)
    {
        result.push_back(name);
    }
    return result;
}

inline auto VhalMockClient::GetPropertyInfo(std::string_view name) -> std::optional<PropertyInfo>
{
    const auto key = std::string(name);
    if (properties_.find(key) == properties_.end())
    {
        return std::nullopt;
    }

    // ReadWrite for everything, because that is what this mock actually does -
    // see the third bullet at the top of this file. id and type come from the
    // AOSP table, so they match what a real VHAL reports; a name outside the
    // table gets id 0 and type None, which is the honest answer for a value
    // stored under an ad-hoc name.
    PropertyInfo info;
    info.id = android::hardware::automotive::vehicle::VehicleProperty_GetIdByName(key);
    info.access = PropertyAccess::ReadWrite;
    info.type = ToCarPropertyType(VhalMockDetail::DeclaredType(key));
    return info;
}

inline void VhalMockClient::SimulatePropertyChange(const CarProperty& prop)
{
    for (const auto& [id, entry] : handlers_)
    {
        if (entry.first == prop.name)
        {
            entry.second(prop);
        }
    }
}

inline bool VhalMockClient::CheckHealth()
{
    return true;
}
