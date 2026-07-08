# SPDX-FileCopyrightText: 2026 Vector Informatik GmbH
# SPDX-License-Identifier: MIT
#
# Locates gRPC and Protobuf, normalising to canonical gRPC:: targets so the
# rest of the build does not need to know how gRPC was installed.
#
# After this module is included, the following targets are available:
#   gRPC::grpc++            - gRPC C++ library
#   gRPC::grpc_cpp_plugin   - protoc plugin for gRPC C++ code generation
#   protobuf::libprotobuf   - Protobuf library
#   Protobuf_PROTOC_EXECUTABLE - protoc binary
#
# Linux (Ubuntu 22.04): apt install libgrpc++-dev libprotobuf-dev protobuf-compiler-grpc
# Windows: gRPC via Conan (conanfile.py + cmake/fetch_conan.cmake); the gRPC reference
#          and Conan profile are overridable (GRPC_CONAN_REFERENCE / CONAN_PROFILE).

if(WIN32)
    include(${CMAKE_CURRENT_LIST_DIR}/fetch_conan.cmake)
    fetch_conan()

    # Conan 1.x cmake_find_package generator creates Findgrpc.cmake in CMAKE_MODULE_PATH
    # with library components as grpc:: targets.  Findgrpc.cmake also calls
    # find_dependency(Protobuf) internally, so protobuf::libprotobuf and
    # Protobuf_PROTOC_EXECUTABLE are set as a side-effect.
    find_package(grpc REQUIRED)

    # Alias the Conan library target to the canonical gRPC:: namespace.
    if(NOT TARGET gRPC::grpc++)
        add_library(gRPC::grpc++ ALIAS grpc::grpc++)
    endif()

    # grpc_cpp_plugin is an executable, not a CMake target in Findgrpc.cmake.
    # conan_paths.cmake adds ${CONAN_GRPC_ROOT} to CMAKE_PREFIX_PATH, so
    # find_program() locates it under ${CONAN_GRPC_ROOT}/bin/.
    if(NOT TARGET gRPC::grpc_cpp_plugin)
        find_program(_grpc_cpp_plugin grpc_cpp_plugin REQUIRED)
        add_executable(gRPC::grpc_cpp_plugin IMPORTED GLOBAL)
        set_target_properties(gRPC::grpc_cpp_plugin PROPERTIES IMPORTED_LOCATION "${_grpc_cpp_plugin}")
    endif()

    message(STATUS "gRPC: found via Conan (${grpc_VERSION})")
    return()
endif()

find_package(gRPC CONFIG QUIET)
if(gRPC_FOUND)
    message(STATUS "gRPC: found via CMake config (${gRPC_VERSION})")
    find_package(Protobuf REQUIRED)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GRPC REQUIRED IMPORTED_TARGET grpc++)

    find_program(_grpc_cpp_plugin grpc_cpp_plugin REQUIRED)
    add_executable(gRPC::grpc_cpp_plugin IMPORTED GLOBAL)
    set_target_properties(gRPC::grpc_cpp_plugin PROPERTIES
        IMPORTED_LOCATION "${_grpc_cpp_plugin}")

    # Expose PkgConfig::GRPC under the canonical gRPC:: name
    add_library(gRPC::grpc++ IMPORTED INTERFACE GLOBAL)
    set_target_properties(gRPC::grpc++ PROPERTIES
        INTERFACE_LINK_LIBRARIES PkgConfig::GRPC)

    find_package(Protobuf REQUIRED)
    message(STATUS "gRPC: found via pkg-config")
endif()
