# SPDX-FileCopyrightText: 2026 Vector Informatik GmbH
# SPDX-License-Identifier: MIT
import os

from conans import ConanFile


class SilKitAndroidBridge(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    # The gRPC reference is overridable so a specific Conan channel / mirror can be
    # selected without hardcoding it here. Set GRPC_CONAN_REFERENCE in the environment
    # (cmake/fetch_conan.cmake forwards it from the GRPC_CONAN_REFERENCE cache variable,
    # which you can set in a local CMakeUserPresets.json). Defaults to the plain name.
    requires = [os.environ.get("GRPC_CONAN_REFERENCE", "grpc/1.72.0"), "gtest/1.16.0"]
    generators = "cmake_paths", "cmake_find_package"
