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
# Windows: gtest/1.16.0 via Conan 1 (conanfile.py). The `conan install` is run
#   once by cmake/gRPC.cmake's fetch_conan(); its cmake_find_package generator
#   produces FindGTest.cmake on CMAKE_MODULE_PATH. Therefore this module must be
#   included AFTER cmake/gRPC.cmake on Windows.

find_package(GTest REQUIRED)

if(WIN32)
    message(STATUS "GoogleTest: found via Conan (${GTest_VERSION})")
else()
    message(STATUS "GoogleTest: found via CMake/system (${GTest_VERSION})")
endif()
