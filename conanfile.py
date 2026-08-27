# SPDX-FileCopyrightText: 2026 Vector Informatik GmbH
# SPDX-License-Identifier: MIT
#
# Conan 2 consumer recipe. Only used on Windows; see cmake/fetch_conan.cmake,
# which provisions the Conan 2 client into a uv virtual environment and runs
# `conan install` at CMake configure time.
#
# Only CMakeDeps is generated: the compiler and build type come from the CMake
# presets, and Conan contributes package configuration files that are put on
# CMAKE_PREFIX_PATH. No CMakeToolchain, so the build keeps working through
# `cmake --preset <name>` alone.
import os

from conan import ConanFile
from conan.tools.cmake import CMakeDeps


class SilKitAndroidBridge(ConanFile):
    settings = "os", "compiler", "build_type", "arch"

    # The gRPC reference is overridable so a specific Conan channel / mirror can be
    # selected without hardcoding it here. Set GRPC_CONAN_REFERENCE in the environment
    # (cmake/fetch_conan.cmake forwards it from the GRPC_CONAN_REFERENCE cache variable,
    # which you can set in a local CMakeUserPresets.json). Defaults to the ConanCenter
    # reference; ConanCenter carries 1.54.3, 1.69.0 and 1.83.0 only.
    def requirements(self):
        self.requires(os.environ.get("GRPC_CONAN_REFERENCE", "grpc/1.69.0"))
        self.requires("gtest/1.16.0")

    def generate(self):
        deps = CMakeDeps(self)
        # The CMake configuration can differ from the Conan build type: the
        # presets offer RelWithDebInfo, while ConanCenter publishes Release
        # binaries only. CMakeDeps guards every target property with a
        # $<CONFIG:...> generator expression, so the generated files have to name
        # the configuration CMake actually builds - otherwise include directories
        # and libraries silently evaluate to nothing.
        configuration = os.environ.get("SK_CMAKE_CONFIGURATION")
        if configuration:
            deps.configuration = configuration
        deps.generate()
