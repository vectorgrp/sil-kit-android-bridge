# SPDX-FileCopyrightText: 2026 Vector Informatik GmbH
# SPDX-License-Identifier: MIT
#
# Installs the project documentation and the third-party license texts that have
# to travel with the binaries.
#
# With SKAB_STATIC_DEPS=ON (the default) gRPC, Protobuf, abseil, OpenSSL, re2,
# c-ares and zlib are linked into the executables on both platforms, so
# distributing the package distributes them, and Apache-2.0 4(d) plus the
# BSD/MIT attribution clauses require their notices along with it. With OFF they
# come from the distribution and its own copyright files apply.
#
# SIL Kit is always separate: SilKit.dll ships inside the Windows archive,
# libsilkit comes from a .deb that carries its own copyright file.
#
# Everything goes into one tree under <docdir>. The .deb ships it there; the zip
# moves it to the top of the archive (cmake/ZipLayout.cmake):
#   README.md, LICENSE.md, THIRD_PARTY_NOTICES.md
#   docs/*.md                 the user documentation, so the README's links work
#   docs/licenses/
#     aosp-vhal/NOTICE        vendored AOSP VHAL (Apache-2.0)
#     spdlog/LICENSE          vendored spdlog (MIT)
#     fmt/fmt.license.rst     fmt, bundled by spdlog (MIT)
#     silkit/...              SIL Kit and its own third-party notices (Windows)
#     <name>-<version>/...    one per Conan dependency
#     NOTICES-conan.md        index of the above, written by the deployer

include(GNUInstallDirs)

set(SKAB_DOC_ROOT "${CMAKE_INSTALL_DOCDIR}")
set(SKAB_NOTICES_DESTINATION "${SKAB_DOC_ROOT}/docs/licenses")

install(FILES
    "${CMAKE_SOURCE_DIR}/README.md"
    "${CMAKE_SOURCE_DIR}/LICENSE.md"
    "${CMAKE_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
    DESTINATION "${SKAB_DOC_ROOT}"
)
install(DIRECTORY "${CMAKE_SOURCE_DIR}/docs/"
    DESTINATION "${SKAB_DOC_ROOT}/docs"
    FILES_MATCHING PATTERN "*.md"
)

set(_skab_vendored_root "${CMAKE_SOURCE_DIR}/source/third_party")

install(FILES
    "${_skab_vendored_root}/android-16.0.0_r3/hardware/interfaces/NOTICE"
    DESTINATION "${SKAB_NOTICES_DESTINATION}/aosp-vhal"
)
install(FILES
    "${_skab_vendored_root}/spdlog/LICENSE"
    DESTINATION "${SKAB_NOTICES_DESTINATION}/spdlog"
)
install(FILES
    "${_skab_vendored_root}/spdlog/include/spdlog/fmt/bundled/fmt.license.rst"
    DESTINATION "${SKAB_NOTICES_DESTINATION}/fmt"
)

# Resolves the file behind an imported target, trying the configuration-agnostic
# property first and then the per-configuration ones. SIL Kit exports only
# IMPORTED_LOCATION_RELEASE, so the plain property is empty for it.
function(_skab_imported_location target out_var)
    set(${out_var} "" PARENT_SCOPE)
    if(NOT TARGET ${target})
        return()
    endif()
    get_target_property(_location ${target} IMPORTED_LOCATION)
    if(_location)
        set(${out_var} "${_location}" PARENT_SCOPE)
        return()
    endif()
    get_target_property(_configs ${target} IMPORTED_CONFIGURATIONS)
    if(NOT _configs)
        return()
    endif()
    foreach(_config IN LISTS _configs)
        get_target_property(_location ${target} IMPORTED_LOCATION_${_config})
        if(_location)
            set(${out_var} "${_location}" PARENT_SCOPE)
            return()
        endif()
    endforeach()
endfunction()

# Searches upwards from a starting directory for a relative path, up to a fixed
# number of levels. The layout of a SIL Kit installation is not part of any
# stability guarantee - the MSI puts the DLL in <root>/x86_64/bin while the
# release archive nests it differently - so the files are located rather than
# addressed by a hardcoded path.
function(_skab_find_upwards start_dir relative_path levels out_var)
    set(${out_var} "" PARENT_SCOPE)
    set(_dir "${start_dir}")
    foreach(_i RANGE ${levels})
        if(EXISTS "${_dir}/${relative_path}")
            set(${out_var} "${_dir}/${relative_path}" PARENT_SCOPE)
            return()
        endif()
        get_filename_component(_parent "${_dir}" DIRECTORY)
        if(_parent STREQUAL _dir)
            return()
        endif()
        set(_dir "${_parent}")
    endforeach()
endfunction()

# SilKit.dll is redistributed inside the Windows archive, so its license and its
# own third-party notices go with it. On Linux libsilkit comes from a .deb that
# carries its own copyright file, and nothing is redistributed here.
if(WIN32)
    _skab_imported_location(SilKit::SilKit _silkit_binary)
    if(_silkit_binary)
        get_filename_component(_silkit_bindir "${_silkit_binary}" DIRECTORY)
        _skab_find_upwards("${_silkit_bindir}" "LICENSE.txt" 4 _silkit_license)
        _skab_find_upwards("${_silkit_bindir}" "Source/ThirdParty/LICENSES.rst" 4
            _silkit_third_party)
    endif()

    if(_silkit_license)
        install(FILES "${_silkit_license}"
            DESTINATION "${SKAB_NOTICES_DESTINATION}/silkit")
    endif()
    if(_silkit_third_party)
        install(FILES "${_silkit_third_party}"
            DESTINATION "${SKAB_NOTICES_DESTINATION}/silkit")
    endif()
    if(NOT _silkit_license AND NOT _silkit_third_party)
        message(WARNING
            "No license files found in the SIL Kit installation near "
            "'${_silkit_binary}'. The package will ship SilKit.dll without its "
            "license text; add it by hand before distributing.")
    endif()

endif()

# Written by cmake/conan/licenses_deployer.py during fetch_conan(). Outside the
# WIN32 block above: the dependencies are linked in on every platform, and
# shipping them without notices is a licence violation.
if(SKAB_STATIC_DEPS)
    if(SKAB_CONAN_LICENSE_DIR AND EXISTS "${SKAB_CONAN_LICENSE_DIR}")
        install(DIRECTORY "${SKAB_CONAN_LICENSE_DIR}/"
            DESTINATION "${SKAB_NOTICES_DESTINATION}")
    else()
        message(WARNING
            "Conan license texts were not collected; the package will ship "
            "statically linked dependencies without their notices.")
    endif()
endif()
