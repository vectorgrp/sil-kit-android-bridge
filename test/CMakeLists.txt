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

# Hang guard for every registered case. Far above SkParticipant::kWatchdog (60s),
# which is what a stuck call hits first and fails on with a usable message; this
# only stops a wedged process from hanging CI until the job times out.
set(SKAB_TEST_TIMEOUT 180)

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
gtest_discover_tests(Test_VhalMockClient PROPERTIES TIMEOUT ${SKAB_TEST_TIMEOUT})

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
gtest_discover_tests(Test_VhalSimpleApi PROPERTIES TIMEOUT ${SKAB_TEST_TIMEOUT})

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
gtest_discover_tests(Test_VhalGrpcClient PROPERTIES TIMEOUT ${SKAB_TEST_TIMEOUT})

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
gtest_discover_tests(ITest_VhalSilKitAdapter PROPERTIES TIMEOUT ${SKAB_TEST_TIMEOUT})

# ITest_VhalGrpcClient: drives the real VhalGrpcClient against a programmable
# fake VehicleServer (FakeVehicleServer.hpp) over gRPC's in-process transport.
# This is the coverage Test_VhalGrpcClient cannot provide: that target stops at
# VhalGrpcConvert, the pure translation, while everything here needs a stub.
add_executable(ITest_VhalGrpcClient
    ITest_VhalGrpcClient.cpp
    ${SOURCE_DIR}/VhalGrpcClient.cpp
    ${SOURCE_DIR}/VhalGrpcConvert.cpp
)
target_link_libraries(ITest_VhalGrpcClient
    PRIVATE
        GTest::gtest_main
        proto_lib
        spdlog::spdlog
)
target_include_directories(ITest_VhalGrpcClient PRIVATE ${SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR})
skab_copy_runtime_dlls(ITest_VhalGrpcClient)
gtest_discover_tests(ITest_VhalGrpcClient PROPERTIES TIMEOUT ${SKAB_TEST_TIMEOUT})

# ITest_VhalBridgeGrpc: the whole chain a CANoe test bench exercises, minus
# CANoe and minus the device - faked consumer over SIL Kit -> VhalSilKitAdapter
# -> VhalGrpcClient -> fake VHAL over gRPC. Deliberately thin: it proves the
# seams line up, while ITest_VhalGrpcClient carries the depth.
add_executable(ITest_VhalBridgeGrpc
    ITest_VhalBridgeGrpc.cpp
    ${SOURCE_DIR}/VhalGrpcClient.cpp
    ${SOURCE_DIR}/VhalGrpcConvert.cpp
    ${SOURCE_DIR}/VhalSimpleApi.cpp
)
target_link_libraries(ITest_VhalBridgeGrpc
    PRIVATE
        GTest::gtest_main
        proto_lib
        SilKit::SilKit
        spdlog::spdlog
)
target_include_directories(ITest_VhalBridgeGrpc PRIVATE ${SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR})
skab_copy_runtime_dlls(ITest_VhalBridgeGrpc)
gtest_discover_tests(ITest_VhalBridgeGrpc PROPERTIES TIMEOUT ${SKAB_TEST_TIMEOUT})

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

# sil-kit-android-bridge-client: connects to the sil-kit-android-bridge (at registry URI passed in as
# argv[1]) and exercises the SIL Kit RPC side of the bridge:.
# add_test/gtest_discover_tests.
add_executable(sil-kit-android-bridge-client
    sil-kit-android-bridge-client.cpp
    ${SOURCE_DIR}/VhalSimpleApi.cpp
)
target_link_libraries(sil-kit-android-bridge-client
    PRIVATE
        SilKit::SilKit
        spdlog::spdlog
)
target_include_directories(sil-kit-android-bridge-client PRIVATE ${SOURCE_DIR})
# Ship the client tool next to the bridge: same top-level output dir, and install
# it alongside sil-kit-android-bridge (see source/CMakeLists.txt).
set_target_properties(sil-kit-android-bridge-client PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)
skab_copy_runtime_dlls(sil-kit-android-bridge-client)
include(GNUInstallDirs)
install(TARGETS sil-kit-android-bridge-client
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# ---- Packaging guard ----

# Nothing else in this suite looks at how the binary was linked: the 0.4.x
# artifact named the runner's libprotobuf/libgrpc++ soversions and started
# nowhere else, with every test here passing.
if(UNIX AND NOT APPLE AND SKAB_STATIC_DEPS)
    add_test(NAME PackagedBinaryHasNoDynamicVendoredDeps
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/../ci/assert-static-deps.sh
                $<TARGET_FILE:sil-kit-android-bridge>
                $<TARGET_FILE:sil-kit-android-bridge-client>)
    set_tests_properties(PackagedBinaryHasNoDynamicVendoredDeps
        PROPERTIES TIMEOUT ${SKAB_TEST_TIMEOUT})
endif()
