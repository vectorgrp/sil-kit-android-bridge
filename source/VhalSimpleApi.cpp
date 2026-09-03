// SPDX-FileCopyrightText: 2026 Vector Informatik GmbH
// SPDX-License-Identifier: MIT
#include "VhalSimpleApi.hpp"

namespace VhalSimpleApi {

auto ToVector(SilKit::Util::Span<const uint8_t> span) -> std::vector<uint8_t>
{
    return {span.begin(), span.end()};
}

auto DeserializeGetRequest(SilKit::Util::Span<const uint8_t> data) -> std::string
{
    SilKit::Util::SerDes::Deserializer d{ToVector(data)};
    d.BeginStruct();
    auto name = d.Deserialize<std::string>();
    d.EndStruct();
    return name;
}

auto SerializeVoidResponse() -> std::vector<uint8_t>
{
    return {};
}

} // namespace VhalSimpleApi
