# SPDX-FileCopyrightText: 2026 Vector Informatik GmbH
# SPDX-License-Identifier: MIT
#
# Conan 2 consumer recipe, used on both platforms when SKAB_STATIC_DEPS is ON
# (the default); see cmake/fetch_conan.cmake,
# which provisions the Conan 2 client into a uv virtual environment and runs
# `conan install` at CMake configure time.
#
# Only CMakeDeps is generated: the compiler and build type come from the CMake
# presets, and Conan contributes package configuration files that are put on
# CMAKE_PREFIX_PATH. No CMakeToolchain, so the build keeps working through
# `cmake --preset <name>` alone.
#
# The version is not declared here either: it is read from cmake/Version.cmake,
# the single source shared with CMake and the release workflow.
import os
import re

from conan import ConanFile
from conan.errors import ConanException
from conan.tools.cmake import CMakeDeps


def _cmake_version(fragment):
    """Read the major.minor.patch triple out of cmake/Version.cmake.

    That fragment is the single source of the project version - the top-level
    CMakeLists.txt includes it and passes SKAB_VERSION to project(). It is
    parsed textually here rather than evaluated, so it has to keep its plain
    `set(SKAB_VERSION_MAJOR 0)` shape; the fragment says so in its header.
    """
    with open(fragment, encoding="utf-8") as handle:
        text = handle.read()

    components = []
    for component in ("MAJOR", "MINOR", "PATCH"):
        match = re.search(
            r"^set\(SKAB_VERSION_{}\s+([0-9]+)\)".format(component), text, re.MULTILINE
        )
        if not match:
            raise ConanException(
                "SKAB_VERSION_{} not found in '{}'".format(component, fragment)
            )
        components.append(match.group(1))
    return ".".join(components)


class SilKitAndroidBridge(ConanFile):
    name = "sil-kit-android-bridge"
    settings = "os", "compiler", "build_type", "arch"

    # Not declared as a class attribute: the version comes from cmake/Version.cmake
    # so that a bump is a single edit and the two build systems cannot disagree.
    def set_version(self):
        self.version = _cmake_version(
            os.path.join(self.recipe_folder, "cmake", "Version.cmake")
        )

    # Static everywhere. This is already the ConanCenter default, but stating it
    # keeps a profile or a stray -o from silently turning the shipped executables
    # into something that needs a directory full of DLLs beside it. The only DLL
    # in the Windows archive is SilKit.dll, which is unavoidable: SIL Kit is
    # distributed as a shared library with no static variant.
    default_options = {"*:shared": False}

    # The gRPC reference is overridable so a specific Conan channel / mirror can be
    # selected without hardcoding it here. Set SKAB_GRPC_CONAN_REFERENCE in the environment
    # (cmake/fetch_conan.cmake forwards it from the SKAB_GRPC_CONAN_REFERENCE cache variable,
    # which you can set in a local CMakeUserPresets.json). Defaults to the ConanCenter
    # reference; ConanCenter carries 1.54.3, 1.69.0 and 1.83.0 only.
    def requirements(self):
        self.requires(os.environ.get("SKAB_GRPC_CONAN_REFERENCE", "grpc/1.69.0"))

    # GoogleTest is linked only into the test executables, which are not packaged.
    # Declaring it as a test requirement keeps it out of dependencies.host, and so
    # out of the redistributed license set gathered by
    # cmake/conan/licenses_deployer.py. CMakeDeps still generates its package
    # configuration, which is what cmake/gtest.cmake finds.
    def build_requirements(self):
        self.test_requires("gtest/1.16.0")

    def generate(self):
        deps = CMakeDeps(self)
        # The CMake configuration can differ from the Conan build type: the
        # presets offer RelWithDebInfo, while ConanCenter publishes Release
        # binaries only. CMakeDeps guards every target property with a
        # $<CONFIG:...> generator expression, so the generated files have to name
        # the configuration CMake actually builds - otherwise include directories
        # and libraries silently evaluate to nothing.
        configuration = os.environ.get("SKAB_CMAKE_CONFIGURATION")
        if configuration:
            deps.configuration = configuration
        deps.generate()
