// SPDX-FileCopyrightText: 2026 Vector Informatik GmbH
// SPDX-License-Identifier: MIT
#pragma once

#include <iostream>

//#include <format>
#include "spdlog/fmt/fmt.h"

template<typename... Args>
inline void print(fmt::format_string<Args...> formatString, Args&&... args)
{
    std::cout << fmt::format(formatString, std::forward<Args>(args)...) << std::endl;
}


