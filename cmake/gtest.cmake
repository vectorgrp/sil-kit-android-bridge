# SPDX-FileCopyrightText: 2026 Vector Informatik GmbH
# SPDX-License-Identifier: MIT
#
# Locates GoogleTest, normalising to the canonical GTest:: targets so the
# rest of the build does not need to know how GoogleTest was installed.
#
# After this module is included, the following targets are available:
#   GTest::gtest        - GoogleTest library
#   GTest::gtest_main   - GoogleTest library with a default main()
#   GTest::gmock        - GoogleMock library
#   GTest::gmock_main   - GoogleMock library with a default main()
#
# Linux (Ubuntu 22.04): apt install libgtest-dev libgmock-dev
# Windows: gtest via Conan 2 (conanfile.py). The `conan install` is run once by
#   cmake/gRPC.cmake's fetch_conan(), which puts the CMakeDeps-generated
#   GTest-config.cmake on CMAKE_PREFIX_PATH. Therefore this module must be
#   included AFTER cmake/gRPC.cmake on Windows.
#
# Config mode is tried first on purpose: CMake ships a FindGTest module that can
# half-match a package manager's layout, so being explicit avoids surprises.

find_package(GTest CONFIG QUIET)
if(GTest_FOUND)
    message(STATUS "GoogleTest: found via CMake config (${GTest_VERSION})")
else()
    find_package(GTest MODULE REQUIRED)
    message(STATUS "GoogleTest: found via CMake's FindGTest module (${GTest_VERSION})")
endif()
