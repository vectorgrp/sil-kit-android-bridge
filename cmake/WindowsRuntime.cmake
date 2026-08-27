# SPDX-FileCopyrightText: 2026 Vector Informatik GmbH
# SPDX-License-Identifier: MIT
#
# Windows has no rpath: an executable only finds its DLLs next to itself or on
# PATH. SilKit.dll is the one shared runtime dependency of this project (gRPC,
# protobuf, GoogleTest and spdlog are all linked statically), and the test
# targets use gtest_discover_tests(), which launches the test executables at
# BUILD time - so a missing DLL breaks the build, not just the test run.
#
# sk_copy_runtime_dlls(<target>) copies the runtime DLLs of an executable next to
# it after linking. It is a no-op on non-Windows platforms.

function(sk_copy_runtime_dlls target)
    if(NOT WIN32)
        return()
    endif()
    # TARGET_RUNTIME_DLLS is empty for targets that only link static libraries,
    # and `cmake -E copy_if_different` without sources is an error - hence the
    # `true` no-op branch (the idiom from the TARGET_RUNTIME_DLLS documentation).
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E
                $<IF:$<BOOL:$<TARGET_RUNTIME_DLLS:${target}>>,copy_if_different,true>
                "$<TARGET_RUNTIME_DLLS:${target}>" "$<TARGET_FILE_DIR:${target}>"
        COMMAND_EXPAND_LISTS
        COMMENT "Copying runtime DLLs next to ${target}"
    )
endfunction()
