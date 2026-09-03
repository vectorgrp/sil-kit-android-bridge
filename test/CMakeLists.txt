# SPDX-FileCopyrightText: 2026 Vector Informatik GmbH
# SPDX-License-Identifier: MIT
#
# Tests and test utilities. Production code lives in ../source; test targets add
# that directory to their include path. The global targets proto_lib /
# SilKit::SilKit / spdlog::spdlog / GTest::* are defined before this directory is
# added (see the root CMakeLists.txt order: gtest.cmake, add_subdirectory(source),
# add_subdirectory(test)).

include(GoogleTest)  # provides gtest_discover_tests

set(SOURCE_DIR ${CMAKE_SOURCE_DIR}/source)

# ---- Unit tests (gtest, registered with CTest via gtest_discover_tests) ----

# Test_VhalMockClient: exercises the in-memory mock VHAL client. No SIL Kit /
# network dependency - pure unit test.
add_executable(Test_VhalMockClient
    Test_VhalMockClient.cpp
)
target_link_libraries(Test_VhalMockClient
    PRIVATE
        GTest::gtest_main
        spdlog::spdlog
)
target_include_directories(Test_VhalMockClient PRIVATE ${SOURCE_DIR})
skab_copy_runtime_dlls(Test_VhalMockClient)
gtest_discover_tests(Test_VhalMockClient)

# Test_VhalSimpleApi: guards the VhalSimple contract - the identity constants that
# must match VhalSimple.vcdl, and the CANoe-compatible binary wire format. Uses
# only the SilKit SerDes utilities.
add_executable(Test_VhalSimpleApi
    Test_VhalSimpleApi.cpp
    ${SOURCE_DIR}/VhalSimpleApi.cpp
)
target_link_libraries(Test_VhalSimpleApi
    PRIVATE
        GTest::gtest_main
        SilKit::SilKit
        spdlog::spdlog
)
target_include_directories(Test_VhalSimpleApi PRIVATE ${SOURCE_DIR})
skab_copy_runtime_dlls(Test_VhalSimpleApi)
gtest_discover_tests(Test_VhalSimpleApi)

# Test_VhalGrpcClient: covers the pure VHAL translation layer - URI validation,
# VehiclePropValue -> CarProperty, and the StatusCode gating on read and write
# results. VhalGrpcConvert.hpp is deliberately proto-only, so this needs the
# generated sources but neither a gRPC server nor SIL Kit.
add_executable(Test_VhalGrpcClient
    Test_VhalGrpcClient.cpp
    ${SOURCE_DIR}/VhalGrpcConvert.cpp
)
target_link_libraries(Test_VhalGrpcClient
    PRIVATE
        GTest::gtest_main
        proto_lib
        spdlog::spdlog
)
target_include_directories(Test_VhalGrpcClient PRIVATE ${SOURCE_DIR})
skab_copy_runtime_dlls(Test_VhalGrpcClient)
gtest_discover_tests(Test_VhalGrpcClient)

# ---- Integration tests (self-contained: in-process registry via the Vendor API) ----

# ITest_VhalSilKitAdapter: drives the real VhalSilKitAdapter (with a VhalMockClient)
# from a faked CANoe consumer over a live, in-process SIL Kit registry. Uses the
# mock client, so no gRPC/proto dependency.
add_executable(ITest_VhalSilKitAdapter
    ITest_VhalSilKitAdapter.cpp
    ${SOURCE_DIR}/VhalSimpleApi.cpp
)
target_link_libraries(ITest_VhalSilKitAdapter
    PRIVATE
        GTest::gtest_main
        SilKit::SilKit
        spdlog::spdlog
)
target_include_directories(ITest_VhalSilKitAdapter PRIVATE ${SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR})
skab_copy_runtime_dlls(ITest_VhalSilKitAdapter)
gtest_discover_tests(ITest_VhalSilKitAdapter)

# ---- Manual developer tools (built, but NOT registered with CTest) ----
# grpc-client-test: connects to a real VHAL gRPC server (pass the socket/URI as
# argv[1]) and exercises VhalGrpcClient. Run by hand; intentionally has no
# add_test/gtest_discover_tests.
add_executable(grpc-client-test
    grpc-client-test.cpp
    ${SOURCE_DIR}/VhalGrpcClient.cpp
    ${SOURCE_DIR}/VhalGrpcConvert.cpp
)
target_link_libraries(grpc-client-test
    PRIVATE
        proto_lib
        SilKit::SilKit
        spdlog::spdlog
)
target_include_directories(grpc-client-test PRIVATE ${SOURCE_DIR})
skab_copy_runtime_dlls(grpc-client-test)

# silkit-client: connects to the sil-kit-android-bridge (at registry URI passed in as
# argv[1]) and exercises the SIL Kit RPC side of the bridge:.
# add_test/gtest_discover_tests.
add_executable(silkit-client
    silkit-client.cpp
    ${SOURCE_DIR}/VhalSimpleApi.cpp
)
target_link_libraries(silkit-client
    PRIVATE
        SilKit::SilKit
        spdlog::spdlog
)
target_include_directories(silkit-client PRIVATE ${SOURCE_DIR})
# Ship the client tool next to the bridge: same top-level output dir, and install
# it alongside sil-kit-android-bridge (see source/CMakeLists.txt).
set_target_properties(silkit-client PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)
skab_copy_runtime_dlls(silkit-client)
include(GNUInstallDirs)
install(TARGETS silkit-client
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)