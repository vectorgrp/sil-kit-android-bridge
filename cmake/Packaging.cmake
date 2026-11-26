# SPDX-FileCopyrightText: 2026 Vector Informatik GmbH
# SPDX-License-Identifier: MIT
#
# CPack configuration: produces a .zip and a .deb from the install() rules
# defined in source/CMakeLists.txt.
#
# Usage (from the build directory after cmake --build):
#   cpack              -- all generators for the platform
#   cpack -G ZIP       -- zip only
#   cpack -G DEB       -- deb only (Linux)
#
# Windows produces a ZIP only.

set(CPACK_PACKAGE_NAME "sil-kit-android-bridge")
set(CPACK_PACKAGE_VENDOR "Vector Informatik GmbH")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Android VHAL to SIL Kit simulation network bridge")
set(CPACK_RESOURCE_FILE_README "${CMAKE_SOURCE_DIR}/README.md")

# Normalise the processor token: Windows reports AMD64 where Linux reports
# x86_64, and the release assets should agree on one spelling.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^([Aa][Mm][Dd]64|x86_64)$")
    set(_package_arch "x86_64")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^([Aa][Rr][Mm]64|aarch64)$")
    set(_package_arch "aarch64")
else()
    set(_package_arch "${CMAKE_SYSTEM_PROCESSOR}")
endif()

# Package file name without generator extension, e.g.:
#   sil-kit-android-bridge-<version>-Linux-x86_64.zip / .deb
#   sil-kit-android-bridge-<version>-Windows-x86_64.zip
# where <version> comes from cmake/Version.cmake by way of PROJECT_VERSION.
set(CPACK_PACKAGE_FILE_NAME
    "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${CMAKE_SYSTEM_NAME}-${_package_arch}")

# ZIP: empty install prefix gives a flat bin/ layout inside the archive, and
# cmake/ZipLayout.cmake moves the documentation from <docdir> to the top.
set(CPACK_PACKAGING_INSTALL_PREFIX "")
set(CPACK_SKAB_DOC_ROOT "${SKAB_DOC_ROOT}")
set(CPACK_PRE_BUILD_SCRIPTS "${CMAKE_CURRENT_LIST_DIR}/ZipLayout.cmake")

if(WIN32)
    # No DEB on Windows; the archive carries SilKit.dll (see source/CMakeLists.txt).
    set(CPACK_GENERATOR "ZIP")
else()
    # DEB: override to FHS /usr so the binary lands in /usr/bin
    # CPACK_DEBIAN_PACKAGE_INSTALL_PREFIX requires CMake >= 3.20 (project requires 3.21)
    set(CPACK_DEBIAN_PACKAGE_INSTALL_PREFIX "/usr")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Vector Informatik GmbH")
    set(CPACK_DEBIAN_PACKAGE_SECTION "utils")

    # Map CMake processor names to Debian architecture tokens
    if(_package_arch STREQUAL "x86_64")
        set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
    elseif(_package_arch STREQUAL "aarch64")
        set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "arm64")
    else()
        set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "${_package_arch}")
    endif()

    # Auto-detect runtime shared-library dependencies from the installed binary.
    # Requires dpkg-dev (provides dpkg-shlibdeps) on the build host.
    find_program(_dpkg_shlibdeps dpkg-shlibdeps)
    if(_dpkg_shlibdeps)
        set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    endif()

    set(CPACK_GENERATOR "ZIP;DEB")
endif()

include(CPack)
