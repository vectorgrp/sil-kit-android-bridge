# SPDX-FileCopyrightText: 2026 Vector Informatik GmbH
# SPDX-License-Identifier: MIT
#
# Locates gRPC and Protobuf, normalising to canonical targets so the rest of the
# build does not need to know how gRPC was installed.
#
# After this module is included, the following targets are available:
#   gRPC::grpc++            - gRPC C++ library
#   gRPC::grpc_cpp_plugin   - protoc plugin for gRPC C++ code generation
#   protobuf::libprotobuf   - Protobuf library
#   protobuf::protoc        - protoc compiler
#
# SKAB_STATIC_DEPS=ON (the default) resolves them with Conan 2 (conanfile.py +
# cmake/fetch_conan.cmake) and links them in; a dynamically linked build names
# the build host's libprotobuf/libgrpc++ soversions and starts nowhere else.
# OFF uses the system packages, for which apt ships no static archives.
#
# Passing a CMAKE_TOOLCHAIN_FILE (vcpkg, a custom toolchain, ...) skips the
# Conan bootstrap entirely: the toolchain is then responsible for making the
# packages findable, and the config-mode branch below picks them up unchanged.

option(SKAB_STATIC_DEPS
    "Resolve gRPC, Protobuf and GoogleTest with Conan 2 and link them statically" ON)

if(SKAB_STATIC_DEPS AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    include(${CMAKE_CURRENT_LIST_DIR}/fetch_conan.cmake)
    fetch_conan()
endif()

find_package(gRPC CONFIG QUIET)
if(gRPC_FOUND)
    message(STATUS "gRPC: found via CMake config (${gRPC_VERSION})")

    # Conan's CMakeDeps and vcpkg provide protobuf as a config package; the
    # system install on Ubuntu is found through CMake's FindProtobuf module.
    find_package(protobuf CONFIG QUIET)
    if(NOT protobuf_FOUND)
        find_package(Protobuf MODULE REQUIRED)
    endif()
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GRPC REQUIRED IMPORTED_TARGET grpc++)

    # Expose PkgConfig::GRPC under the canonical gRPC:: name
    add_library(gRPC::grpc++ IMPORTED INTERFACE GLOBAL)
    set_target_properties(gRPC::grpc++ PROPERTIES
        INTERFACE_LINK_LIBRARIES PkgConfig::GRPC)

    find_package(Protobuf MODULE REQUIRED)
    message(STATUS "gRPC: found via pkg-config")
endif()

# Conan's build modules find these with a bare find_program() over PATH, so an
# installed system copy wins over the package being linked - and protoc caches
# its choice, so one bad configure sticks.
function(_skab_pin_tool_to_package target pkg_prefix tool)
    if(NOT TARGET ${target})
        return()
    endif()
    string(TOUPPER "${CMAKE_BUILD_TYPE}" _cfg)
    set(_folder "${${pkg_prefix}_PACKAGE_FOLDER_${_cfg}}")
    if(NOT _folder)
        return()
    endif()
    set(_expected "${_folder}/bin/${tool}${CMAKE_EXECUTABLE_SUFFIX}")
    if(NOT EXISTS "${_expected}")
        return()
    endif()
    get_target_property(_actual ${target} IMPORTED_LOCATION)
    if(_actual STREQUAL _expected)
        return()
    endif()
    message(STATUS "gRPC: re-pointing ${target} from '${_actual}' to the Conan package")
    set_property(TARGET ${target} PROPERTY IMPORTED_LOCATION "${_expected}")
    if("${target}" STREQUAL "protobuf::protoc")
        # FindProtobuf and the fallback below both read this cache entry.
        set(Protobuf_PROTOC_EXECUTABLE "${_expected}" CACHE FILEPATH "The protoc compiler" FORCE)
    endif()
endfunction()

if(SKAB_STATIC_DEPS)
    _skab_pin_tool_to_package(gRPC::grpc_cpp_plugin grpc grpc_cpp_plugin)
    _skab_pin_tool_to_package(protobuf::protoc protobuf protoc)
endif()

# gRPC's own config exports grpc_cpp_plugin as an imported executable, and so do
# the CMake build modules shipped by the Conan recipe. Fall back to a plain
# lookup when it is missing (pkg-config path, older packages).
if(NOT TARGET gRPC::grpc_cpp_plugin)
    find_program(_grpc_cpp_plugin grpc_cpp_plugin REQUIRED)
    add_executable(gRPC::grpc_cpp_plugin IMPORTED GLOBAL)
    set_target_properties(gRPC::grpc_cpp_plugin PROPERTIES
        IMPORTED_LOCATION "${_grpc_cpp_plugin}")
endif()

# protobuf::protoc is defined by FindProtobuf, by protobuf's own config package
# and by the Conan recipe's protobuf-conan-protoc-target.cmake build module.
if(NOT TARGET protobuf::protoc)
    if(Protobuf_PROTOC_EXECUTABLE)
        set(_protoc "${Protobuf_PROTOC_EXECUTABLE}")
    else()
        find_program(_protoc protoc REQUIRED)
    endif()
    add_executable(protobuf::protoc IMPORTED GLOBAL)
    set_target_properties(protobuf::protoc PROPERTIES
        IMPORTED_LOCATION "${_protoc}")
endif()
