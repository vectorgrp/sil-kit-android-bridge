# SPDX-FileCopyrightText: 2026 Vector Informatik GmbH
# SPDX-License-Identifier: MIT
#
# Provisions a Conan 2 client into a local, uv-managed virtual environment and
# runs `conan install` at configure time. The CMakeDeps generator writes package
# configuration files into the build tree; that directory is put on
# CMAKE_PREFIX_PATH so the ordinary find_package() calls in cmake/gRPC.cmake and
# cmake/gtest.cmake resolve them.
#
# The Conan client on PATH is deliberately NOT used. Corporate machines commonly
# have a system-wide Conan 1.x installation, which cannot read the Conan 2
# conanfile.py in this repository. Bootstrapping into ${CONAN_VENV_DIR} keeps the
# build self-contained and leaves an existing Conan 1 setup untouched: Conan 1
# uses ~/.conan and CONAN_USER_HOME, Conan 2 uses ~/.conan2 and CONAN_HOME, and
# the two share nothing.
#
# Intended to be included by cmake/gRPC.cmake on Windows only.
#
# Cache variables:
#   GRPC_CONAN_REFERENCE - full Conan reference for gRPC, e.g. to select an
#                          internal Artifactory channel instead of ConanCenter.
#   CONAN_PROFILE        - Conan 2 profile name for the host and build context.
#                          Empty uses the 'default' profile, which is created by
#                          `conan profile detect` if it does not exist yet.
#   CONAN_VENV_DIR       - location of the uv virtual environment.
#   CONAN_EXECUTABLE     - skip the bootstrap and use this client instead.
#   CONAN_SETTINGS       - extra `--settings key=value` arguments.
#
# ConanCenter publishes prebuilt Windows binaries for one configuration only
# (msvc 194, cppstd 14, Release, dynamic runtime). A newer Visual Studio detects
# as msvc 195, for which no binaries exist, so `--build=missing` would compile
# gRPC, protobuf, abseil and OpenSSL from source - roughly an hour. MSVC keeps
# binary compatibility across the v143 toolset updates, so requesting the
# published configuration is the faster and equally valid choice:
#
#   cmake --preset relwithdebinfo -DCONAN_SETTINGS=compiler.version=194
#
# Note that Conan's compiler.cppstd only selects which package variant is used;
# the standard this project is compiled with comes from CMAKE_CXX_STANDARD.
#
# These are best set from a local (git-ignored) CMakeUserPresets.json:
#
#   "cacheVariables": {
#     "GRPC_CONAN_REFERENCE": "grpc/1.69.0@user/channel",
#     "CONAN_PROFILE": "your-conan2-profile-name"
#   }

set(GRPC_CONAN_REFERENCE "grpc/1.69.0" CACHE STRING
    "Full Conan reference for the gRPC package (e.g. grpc/1.69.0@user/channel)")
set(CONAN_PROFILE "" CACHE STRING
    "Conan 2 profile for host/build (empty uses Conan's 'default' profile)")
set(CONAN_VENV_DIR "${CMAKE_SOURCE_DIR}/.venv" CACHE PATH
    "Virtual environment directory holding the Conan 2 client")
set(CONAN_EXECUTABLE "" CACHE FILEPATH
    "Conan 2 executable (empty: provision one in CONAN_VENV_DIR using uv)")
set(CONAN_REQUIREMENT "conan>=2.0,<3" CACHE STRING
    "Requirement specifier used when installing Conan into CONAN_VENV_DIR")
set(CONAN_SETTINGS "" CACHE STRING
    "Extra Conan settings as a list of key=value (e.g. compiler.version=194)")

# Returns the x.y.z version of a Conan client, or an empty string when the
# executable cannot be run.
function(_sk_conan_version conan_exe out_var)
    execute_process(
        COMMAND "${conan_exe}" --version
        OUTPUT_VARIABLE _out
        ERROR_VARIABLE _err
        RESULT_VARIABLE _res
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(NOT _res EQUAL 0)
        set(${out_var} "" PARENT_SCOPE)
        return()
    endif()
    # Output looks like: Conan version 2.31.2
    string(REGEX MATCH "[0-9]+\\.[0-9]+(\\.[0-9]+)?" _ver "${_out}")
    set(${out_var} "${_ver}" PARENT_SCOPE)
endfunction()

# Creates ${CONAN_VENV_DIR} with uv and installs Conan 2 into it, unless a client
# is already present. Sets out_var to the conan executable.
function(_sk_provision_conan out_var)
    if(CONAN_EXECUTABLE)
        set(${out_var} "${CONAN_EXECUTABLE}" PARENT_SCOPE)
        return()
    endif()

    if(WIN32)
        set(_venv_bin "${CONAN_VENV_DIR}/Scripts")
        set(_exe_suffix ".exe")
    else()
        set(_venv_bin "${CONAN_VENV_DIR}/bin")
        set(_exe_suffix "")
    endif()
    set(_conan_exe "${_venv_bin}/conan${_exe_suffix}")
    set(_venv_python "${_venv_bin}/python${_exe_suffix}")

    if(NOT EXISTS "${_conan_exe}")
        find_program(UV_EXECUTABLE uv)
        if(NOT UV_EXECUTABLE)
            message(FATAL_ERROR
                "uv was not found on PATH. It is used to provision a Conan 2 client into "
                "'${CONAN_VENV_DIR}'. Install uv (https://docs.astral.sh/uv/), or point "
                "-DCONAN_EXECUTABLE=<path> at an existing Conan 2 client.")
        endif()

        if(NOT EXISTS "${_venv_python}")
            message(STATUS "Conan: creating virtual environment '${CONAN_VENV_DIR}' (uv)")
            execute_process(
                COMMAND "${UV_EXECUTABLE}" venv "${CONAN_VENV_DIR}"
                RESULT_VARIABLE _res
            )
            if(NOT _res EQUAL 0)
                message(FATAL_ERROR "uv venv '${CONAN_VENV_DIR}' failed (exit ${_res}).")
            endif()
        endif()

        message(STATUS "Conan: installing '${CONAN_REQUIREMENT}' into '${CONAN_VENV_DIR}' (uv)")
        execute_process(
            COMMAND "${UV_EXECUTABLE}" pip install --python "${_venv_python}" "${CONAN_REQUIREMENT}"
            RESULT_VARIABLE _res
        )
        if(NOT _res EQUAL 0)
            message(FATAL_ERROR "uv pip install '${CONAN_REQUIREMENT}' failed (exit ${_res}).")
        endif()
    endif()

    if(NOT EXISTS "${_conan_exe}")
        message(FATAL_ERROR "Conan client not found at '${_conan_exe}' after provisioning.")
    endif()
    set(${out_var} "${_conan_exe}" PARENT_SCOPE)
endfunction()

# Creates the 'default' profile when the client does not have one yet. Only
# called when CONAN_PROFILE is empty.
function(_sk_ensure_default_profile conan_exe)
    execute_process(
        COMMAND "${conan_exe}" profile path default
        RESULT_VARIABLE _res
        OUTPUT_QUIET
        ERROR_QUIET
    )
    if(_res EQUAL 0)
        return()
    endif()
    message(STATUS "Conan: no 'default' profile, running 'conan profile detect'")
    execute_process(
        COMMAND "${conan_exe}" profile detect
        RESULT_VARIABLE _res
    )
    if(NOT _res EQUAL 0)
        message(FATAL_ERROR "conan profile detect failed (exit ${_res}).")
    endif()
endfunction()

# Runs `conan install` and prepends the generator output directory to
# CMAKE_PREFIX_PATH in the caller's scope.
function(fetch_conan)
    _sk_provision_conan(_conan_exe)

    _sk_conan_version("${_conan_exe}" _conan_version)
    if(NOT _conan_version)
        message(FATAL_ERROR "Could not determine the version of '${_conan_exe}'.")
    endif()
    if(_conan_version VERSION_LESS 2.0)
        message(FATAL_ERROR
            "Conan ${_conan_version} ('${_conan_exe}') is too old: this project uses a Conan 2 "
            "conanfile.py. Clear CONAN_EXECUTABLE to let uv provision a Conan 2 client into "
            "'${CONAN_VENV_DIR}'.")
    endif()

    if(CONAN_PROFILE)
        # A profile carries the compiler/runtime/build_type settings itself.
        set(_settings_args)
        set(_profile_args
            --profile:host=${CONAN_PROFILE}
            --profile:build=${CONAN_PROFILE})
        message(STATUS "Conan ${_conan_version}: install with profile '${CONAN_PROFILE}' (${GRPC_CONAN_REFERENCE})")
    else()
        _sk_ensure_default_profile("${_conan_exe}")
        # No profile given: use the default profile and pin the settings that have
        # to match this build. RelWithDebInfo maps to Release (no separate
        # binaries are expected).
        if(CMAKE_BUILD_TYPE STREQUAL "Debug")
            set(_build_type "Debug")
        else()
            set(_build_type "Release")
        endif()
        set(_settings_args --settings build_type=${_build_type})
        set(_profile_args)
        message(STATUS "Conan ${_conan_version}: install with default profile, build_type=${_build_type} (${GRPC_CONAN_REFERENCE})")
    endif()

    foreach(_setting IN LISTS CONAN_SETTINGS)
        list(APPEND _settings_args --settings ${_setting})
    endforeach()

    set(_generators_dir "${CMAKE_BINARY_DIR}/conan")

    execute_process(
        COMMAND ${CMAKE_COMMAND} -E env
            "GRPC_CONAN_REFERENCE=${GRPC_CONAN_REFERENCE}"
            "SK_CMAKE_CONFIGURATION=${CMAKE_BUILD_TYPE}"
            "${_conan_exe}" install "${CMAKE_SOURCE_DIR}"
            --output-folder "${_generators_dir}"
            --build=missing
            ${_profile_args}
            ${_settings_args}
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        RESULT_VARIABLE _res
    )
    if(NOT _res EQUAL 0)
        message(FATAL_ERROR "conan install failed (exit ${_res}). "
            "Set GRPC_CONAN_REFERENCE / CONAN_PROFILE (e.g. via CMakeUserPresets.json) "
            "to point at a Conan remote that provides the gRPC package.")
    endif()

    list(PREPEND CMAKE_PREFIX_PATH "${_generators_dir}")
    set(CMAKE_PREFIX_PATH "${CMAKE_PREFIX_PATH}" PARENT_SCOPE)
endfunction()
