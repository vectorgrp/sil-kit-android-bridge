# SPDX-FileCopyrightText: 2026 Vector Informatik GmbH
# SPDX-License-Identifier: MIT
#
# Runs `conan install` at configure time and includes the generated
# conan_paths.cmake so that find_package() calls find Conan packages.
# Intended to be included by cmake/gRPC.cmake on Windows only.
#
# The gRPC reference and the Conan profile are overridable so a specific Conan
# channel / mirror and a matching prebuilt-binary profile can be selected without
# hardcoding them here. Set them from a local (git-ignored) CMakeUserPresets.json:
#
#   "cacheVariables": {
#     "GRPC_CONAN_REFERENCE": "grpc/1.72.0@user/channel",
#     "CONAN_PROFILE": "your-conan-profile-name"
#   }
#
# GRPC_CONAN_REFERENCE is forwarded to conanfile.py via the environment.
# If CONAN_PROFILE is empty, Conan's 'default' profile is used and the build type
# is passed explicitly.

set(GRPC_CONAN_REFERENCE "grpc/1.72.0" CACHE STRING
    "Full Conan reference for the gRPC package (e.g. grpc/1.72.0@user/channel)")
set(CONAN_PROFILE "" CACHE STRING
    "Conan profile for host/build (empty uses Conan's 'default' profile)")

macro(fetch_conan)
    if(DEFINED CMAKE_TOOLCHAIN_FILE)
        return()
    endif()

    find_program(_conan_exe conan REQUIRED)

    if(CONAN_PROFILE)
        # A profile carries the compiler/runtime/build_type settings itself.
        set(_conan_settings_args)
        set(_conan_profile_args
            --profile:host=${CONAN_PROFILE}
            --profile:build=${CONAN_PROFILE})
        message(STATUS "Conan: install with profile '${CONAN_PROFILE}' (${GRPC_CONAN_REFERENCE})")
    else()
        # No profile given: use the default profile and pin the build type.
        # RelWithDebInfo maps to Release (no separate binaries are expected).
        if(CMAKE_BUILD_TYPE STREQUAL "Debug")
            set(_conan_build_type "Debug")
        else()
            set(_conan_build_type "Release")
        endif()
        set(_conan_settings_args --settings build_type=${_conan_build_type})
        set(_conan_profile_args)
        message(STATUS "Conan: install with default profile, build_type=${_conan_build_type} (${GRPC_CONAN_REFERENCE})")
    endif()

    execute_process(
        COMMAND ${CMAKE_COMMAND} -E env "GRPC_CONAN_REFERENCE=${GRPC_CONAN_REFERENCE}"
            ${_conan_exe} install ${CMAKE_SOURCE_DIR}
            ${_conan_profile_args}
            ${_conan_settings_args}
            --output-folder ${CMAKE_BINARY_DIR}
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        RESULT_VARIABLE _conan_result
    )
    if(NOT _conan_result EQUAL 0)
        message(FATAL_ERROR "conan install failed (exit ${_conan_result}). "
            "Set GRPC_CONAN_REFERENCE / CONAN_PROFILE (e.g. via CMakeUserPresets.json) "
            "to point at a Conan remote that provides the gRPC package.")
    endif()

    include(${CMAKE_BINARY_DIR}/conan_paths.cmake)
endmacro()
