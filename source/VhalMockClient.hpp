// SPDX-FileCopyrightText: 2026 Vector Informatik GmbH
// SPDX-License-Identifier: MIT
#pragma once

#include <map>
#include <string>

#include "IVhalClient.hpp"
#include "Print.hpp"

class VhalMockClient : public IVhalClient
{
public:
    explicit VhalMockClient(const std::string& target)
    {
        properties_["EV_BATTERY_LEVEL"] = CarProperty{"EV_BATTERY_LEVEL", int32_t(75)};
        properties_["EV_CHARGE_TIME_REMAINING"] = CarProperty{"EV_CHARGE_TIME_REMAINING", int32_t(0)};
        properties_["EV_CURRENT_BATTERY_CAPACITY"] = CarProperty{"EV_CURRENT_BATTERY_CAPACITY", int32_t(0)};
        properties_["EV_CHARGE_PORT_CONNECTED"] = CarProperty{"EV_CHARGE_PORT_CONNECTED", int32_t(0)};
        properties_["EV_CHARGE_STATE"] = CarProperty{"EV_CHARGE_STATE", int32_t(0)};
        properties_["INSTANTANEOUS_EV_EFFICIENCY"] = CarProperty{"INSTANTANEOUS_EV_EFFICIENCY", int32_t(0)};
        print("VhalMockClient initialized with target: {}", target);
        print("VhalMockClient mocking {} properties", properties_.size());
    }

    auto GetProperties(std::vector<std::string_view> nameList) -> std::vector<std::optional<CarProperty>> override
    {
        std::vector<std::optional<CarProperty>> result;
        result.reserve(nameList.size());
        for (const auto& name : nameList)
        {
            auto it = properties_.find(std::string(name));
            if (it != properties_.end())
            {
                result.push_back(it->second);
            }
            else
            {
                result.push_back(std::nullopt);
            }
        }
        return result;
    }

    auto GetProperty(std::string_view name) -> std::optional<CarProperty> override
    {
        print("VhalMockClient GetProperty called: {}", name);
        auto it = properties_.find(std::string(name));
        if (it != properties_.end())
        {
            return it->second;
        }
        return std::nullopt;
    }

    auto SetProperty(const CarProperty& property) -> bool override
    {
        print("VhalMockClient SetProperty called: {} = {}", property.name, property);
        properties_[property.name] = property;
        return true;
    }

    auto AddPropertyChangedHandler(std::string_view propertyName, PropertyChangedHandler handler) -> int override
    {
        int id = nextHandlerId_++;
        handlers_[id] = {std::string(propertyName), handler};
        return id;
    }

    auto RemovePropertyChangedHandler(int handlerId) -> void override
    {
        handlers_.erase(handlerId);
    }

    auto GetAllProperties() -> std::vector<std::string> override
    {
        std::vector<std::string> result;
        for (const auto& [name, prop] : properties_)
        {
            result.push_back(name);
        }
        return result;
    }

    void SimulatePropertyChange(const CarProperty& prop)
    {
        for (const auto& [id, entry] : handlers_)
        {
            if (entry.first == prop.name)
            {
                entry.second(prop);
            }
        }
    }

    bool CheckHealth() override
    {
        return true;
    }

private:
    std::map<std::string, CarProperty> properties_;
    std::map<int, std::pair<std::string, PropertyChangedHandler>> handlers_;
    int nextHandlerId_ = 1;
};
