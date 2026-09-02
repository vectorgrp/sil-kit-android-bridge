# SPDX-FileCopyrightText: 2026 Vector Informatik GmbH
# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.21)

# The project version has a single source: cmake/Version.cmake, which conanfile.py
# and the release workflow read as well. Included before project() so the triple
# is available to it.
include(${CMAKE_CURRENT_LIST_DIR}/cmake/Version.cmake)
project(sil-kit-android-bridge VERSION ${SKAB_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(cmake/WindowsRuntime.cmake)
include(cmake/gRPC.cmake)
find_package(SilKit REQUIRED)
include(cmake/gtest.cmake)  # must follow gRPC.cmake (shares the Conan install on Windows)

enable_testing()
add_subdirectory(source)
add_subdirectory(test)
include(cmake/Notices.cmake)
include(cmake/Packaging.cmake)
