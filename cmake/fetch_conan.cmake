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
# Both the client and the Conan home live under _build/, so a build creates no
# state outside its own tree: no per-user directory to collide over on a shared
# machine, and `rm -rf _build` is a complete reset.
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
#   CONAN_HOME_DIR       - Conan home (package cache + extensions) for this project.
#   CONAN_EXECUTABLE     - skip the bootstrap and use this client instead.
#   CONAN_SETTINGS       - extra `--settings key=value` arguments.
#   SK_ALLOW_WINDOWS_DEBUG - allow a Debug configure despite the hour-long source
#                          build it implies (see the guard in fetch_conan()).
#
# ConanCenter publishes prebuilt Windows binaries for one configuration only
# (msvc 194, cppstd 14, Release, dynamic runtime), while Visual Studio 2026
# detects as msvc 195. Conan's package_id is an exact-match cache key, not a
# statement about ABI compatibility, so without help nothing matches and every
# dependency is compiled from source - roughly an hour.
#
# The supported way to say "these keys are interchangeable" is a compatibility
# plugin, and cmake/conan/compatibility.py adds the msvc 195 -> 194 -> 193
# fallback. Declaring compiler.version=194 in the profile instead would be wrong:
# Conan derives vcvars_ver from compiler.version, so a source build would invoke
# vcvarsall.bat with -vcvars_ver=14.4 and fail on a machine that only has the
# 14.5 toolset. Keeping the profile honest means reused binaries where they
# exist, and correct source builds where they do not.
#
# Do not pin compiler.cppstd either: abseil, protobuf, grpc and gtest mark cppstd
# as ABI-relevant (extension_properties = {"compatibility_cppstd": False}), so
# pinning it discards every prebuilt binary. The standard this project compiles
# with comes from CMAKE_CXX_STANDARD and is unrelated to that setting.
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
set(CONAN_VENV_DIR "${CMAKE_SOURCE_DIR}/_build/conan-venv" CACHE PATH
    "Virtual environment directory holding the Conan 2 client")
# A Conan home owned by this project: it carries the compatibility plugin below,
# and keeping it out of ~/.conan2 means an existing Conan setup is never touched.
#
# Deliberately ${CMAKE_SOURCE_DIR}/_build/... rather than ${CMAKE_BINARY_DIR}/...:
# the presets place binary dirs at _build/<OS>/<preset>/, so this is one cache
# beside them that every configuration shares. A per-preset home would download
# the whole dependency set once per build type. Point this elsewhere to share one
# cache across worktrees, at the price of writing outside the source tree.
set(CONAN_HOME_DIR "${CMAKE_SOURCE_DIR}/_build/conan2" CACHE PATH
    "Conan 2 home (package cache and extensions) owned by this project")
set(CONAN_EXECUTABLE "" CACHE FILEPATH
    "Conan 2 executable (empty: provision one in CONAN_VENV_DIR using uv)")
set(CONAN_REQUIREMENT "conan>=2.0,<3" CACHE STRING
    "Requirement specifier used when installing Conan into CONAN_VENV_DIR")
set(CONAN_SETTINGS "" CACHE STRING
    "Extra Conan settings as a list of key=value (e.g. compiler.version=194)")
option(SK_ALLOW_WINDOWS_DEBUG
    "Permit a Debug configure on Windows, compiling every dependency from source" OFF)

# CMAKE_CURRENT_LIST_DIR resolves to the defining file while a function body
# runs, but capturing it here keeps that independent of how the module is used.
set(_SK_FETCH_CONAN_DIR "${CMAKE_CURRENT_LIST_DIR}")

# Returns the x.y.z version of a Conan client, or an empty string when the
# executable cannot be run.
function(_sk_conan_version conan_exe out_var)
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E env "CONAN_HOME=${CONAN_HOME_DIR}" "${conan_exe}" --version
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
        COMMAND ${CMAKE_COMMAND} -E env "CONAN_HOME=${CONAN_HOME_DIR}"
            "${conan_exe}" profile path default
        RESULT_VARIABLE _res
        OUTPUT_QUIET
        ERROR_QUIET
    )
    if(_res EQUAL 0)
        return()
    endif()
    message(STATUS "Conan: no 'default' profile, running 'conan profile detect'")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E env "CONAN_HOME=${CONAN_HOME_DIR}"
            "${conan_exe}" profile detect
        RESULT_VARIABLE _res
    )
    if(NOT _res EQUAL 0)
        message(FATAL_ERROR "conan profile detect failed (exit ${_res}).")
    endif()
endfunction()

# Installs the project's compatibility plugin into the Conan home. Must run
# before the first Conan command, so the home is initialised with it in place.
# The copied file carries no "generated by Conan" header, which is what stops
# Conan from overwriting it later. configure_file() creates the directories and
# only rewrites the file when its content changed.
function(_sk_install_compatibility_plugin)
    configure_file(
        "${_SK_FETCH_CONAN_DIR}/conan/compatibility.py"
        "${CONAN_HOME_DIR}/extensions/plugins/compatibility/compatibility.py"
        COPYONLY
    )
endfunction()

# Refuses a Debug configure unless it was asked for explicitly. ConanCenter
# publishes exactly one Windows binary for grpc/1.69.0 (msvc 194, Release,
# dynamic runtime) and no Debug binary for any package in the graph, so a Debug
# configure compiles grpc, protobuf, abseil, openssl, re2, c-ares and zlib from
# source - roughly an hour, inside execute_process, where it looks like a hang.
# The debug preset is disabled on Windows for the same reason; this catches the
# other entry points (Visual Studio's CMake integration, a hand-written
# -DCMAKE_BUILD_TYPE=Debug).
function(_sk_check_build_type)
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug" OR SK_ALLOW_WINDOWS_DEBUG)
        return()
    endif()
    message(FATAL_ERROR
        "CMAKE_BUILD_TYPE=Debug is not supported on Windows: ConanCenter publishes no Debug "
        "binaries for gRPC and its dependencies, so this configure would compile the entire "
        "dependency tree from source (about an hour). Use the 'relwithdebinfo' preset instead - "
        "it carries full debug symbols and is what CI builds. If you really do need debug builds "
        "of the dependencies themselves, re-run with -DSK_ALLOW_WINDOWS_DEBUG=ON.")
endfunction()

# Runs `conan install` and prepends the generator output directory to
# CMAKE_PREFIX_PATH in the caller's scope.
function(fetch_conan)
    _sk_check_build_type()
    message(STATUS "Conan: home '${CONAN_HOME_DIR}'")
    _sk_install_compatibility_plugin()
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
        # to match this build. Release for every configuration - it is the only
        # build type ConanCenter publishes, RelWithDebInfo and Release both compile
        # /MD, and Debug is refused above unless SK_ALLOW_WINDOWS_DEBUG is set.
        if(CMAKE_BUILD_TYPE STREQUAL "Debug")
            set(_build_type "Debug")
            message(WARNING
                "Conan: resolving Debug dependencies. Nothing is published for that "
                "configuration, so every package is about to be compiled from source.")
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

    # The dependencies are linked statically into the shipped executables, so
    # their license texts have to be redistributed with them. This deployer copies
    # each package's licenses/ folder out of the Conan cache; cmake/Notices.cmake
    # installs the result. SK_CONAN_LICENSE_DIR is read there.
    set(SK_CONAN_LICENSE_DIR "${CMAKE_BINARY_DIR}/licenses" CACHE INTERNAL
        "Directory the Conan licenses deployer writes to")

    # COMMAND_ECHO because this call can take a long time on an unusual compiler
    # with no matching prebuilt binaries, and silence there reads as a hang.
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E env
            "CONAN_HOME=${CONAN_HOME_DIR}"
            "GRPC_CONAN_REFERENCE=${GRPC_CONAN_REFERENCE}"
            "SK_CMAKE_CONFIGURATION=${CMAKE_BUILD_TYPE}"
            "${_conan_exe}" install "${CMAKE_SOURCE_DIR}"
            --output-folder "${_generators_dir}"
            --build=missing
            --deployer "${_SK_FETCH_CONAN_DIR}/conan/licenses_deployer.py"
            --deployer-folder "${SK_CONAN_LICENSE_DIR}"
            ${_profile_args}
            ${_settings_args}
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMAND_ECHO STDOUT
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
