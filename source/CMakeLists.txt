# SPDX-FileCopyrightText: 2026 Vector Informatik GmbH
# SPDX-License-Identifier: MIT

set(ANDROID_VHAL_DIR
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/android-16.0.0_r3/hardware/interfaces/automotive/vehicle/aidl/impl/current)
set(VHAL_PROTO_PREFIX android/hardware/automotive/vehicle)

set(PROTO_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${PROTO_BINARY_DIR}/${VHAL_PROTO_PREFIX})

set(PROTO_FILES_ABS
    ${ANDROID_VHAL_DIR}/proto/${VHAL_PROTO_PREFIX}/DumpOptions.proto
    ${ANDROID_VHAL_DIR}/proto/${VHAL_PROTO_PREFIX}/DumpResult.proto
    ${ANDROID_VHAL_DIR}/proto/${VHAL_PROTO_PREFIX}/GetMinMaxSupportedValuesTypes.proto
    ${ANDROID_VHAL_DIR}/proto/${VHAL_PROTO_PREFIX}/GetSupportedValuesListsTypes.proto
    ${ANDROID_VHAL_DIR}/proto/${VHAL_PROTO_PREFIX}/HasSupportedValueInfo.proto
    ${ANDROID_VHAL_DIR}/proto/${VHAL_PROTO_PREFIX}/PropIdAreaId.proto
    ${ANDROID_VHAL_DIR}/proto/${VHAL_PROTO_PREFIX}/RawPropValues.proto
    ${ANDROID_VHAL_DIR}/proto/${VHAL_PROTO_PREFIX}/StatusCode.proto
    ${ANDROID_VHAL_DIR}/proto/${VHAL_PROTO_PREFIX}/SubscribeOptions.proto
    ${ANDROID_VHAL_DIR}/proto/${VHAL_PROTO_PREFIX}/SubscribeRequest.proto
    ${ANDROID_VHAL_DIR}/proto/${VHAL_PROTO_PREFIX}/SupportedValuesChange.proto
    ${ANDROID_VHAL_DIR}/proto/${VHAL_PROTO_PREFIX}/UnsubscribeRequest.proto
    ${ANDROID_VHAL_DIR}/proto/${VHAL_PROTO_PREFIX}/VehicleAreaConfig.proto
    ${ANDROID_VHAL_DIR}/proto/${VHAL_PROTO_PREFIX}/VehiclePropConfig.proto
    ${ANDROID_VHAL_DIR}/proto/${VHAL_PROTO_PREFIX}/VehiclePropertyAccess.proto
    ${ANDROID_VHAL_DIR}/proto/${VHAL_PROTO_PREFIX}/VehiclePropertyChangeMode.proto
    ${ANDROID_VHAL_DIR}/proto/${VHAL_PROTO_PREFIX}/VehiclePropertyStatus.proto
    ${ANDROID_VHAL_DIR}/proto/${VHAL_PROTO_PREFIX}/VehiclePropValue.proto
    ${ANDROID_VHAL_DIR}/proto/${VHAL_PROTO_PREFIX}/VehiclePropValueRequest.proto
)

set(GRPC_FILES_ABS
    ${ANDROID_VHAL_DIR}/grpc/proto/VehicleServer.proto
)

set(PROTO_SRCS
    ${PROTO_BINARY_DIR}/${VHAL_PROTO_PREFIX}/DumpOptions.pb.cc
    ${PROTO_BINARY_DIR}/${VHAL_PROTO_PREFIX}/DumpResult.pb.cc
    ${PROTO_BINARY_DIR}/${VHAL_PROTO_PREFIX}/GetMinMaxSupportedValuesTypes.pb.cc
    ${PROTO_BINARY_DIR}/${VHAL_PROTO_PREFIX}/GetSupportedValuesListsTypes.pb.cc
    ${PROTO_BINARY_DIR}/${VHAL_PROTO_PREFIX}/HasSupportedValueInfo.pb.cc
    ${PROTO_BINARY_DIR}/${VHAL_PROTO_PREFIX}/PropIdAreaId.pb.cc
    ${PROTO_BINARY_DIR}/${VHAL_PROTO_PREFIX}/RawPropValues.pb.cc
    ${PROTO_BINARY_DIR}/${VHAL_PROTO_PREFIX}/StatusCode.pb.cc
    ${PROTO_BINARY_DIR}/${VHAL_PROTO_PREFIX}/SubscribeOptions.pb.cc
    ${PROTO_BINARY_DIR}/${VHAL_PROTO_PREFIX}/SubscribeRequest.pb.cc
    ${PROTO_BINARY_DIR}/${VHAL_PROTO_PREFIX}/SupportedValuesChange.pb.cc
    ${PROTO_BINARY_DIR}/${VHAL_PROTO_PREFIX}/UnsubscribeRequest.pb.cc
    ${PROTO_BINARY_DIR}/${VHAL_PROTO_PREFIX}/VehicleAreaConfig.pb.cc
    ${PROTO_BINARY_DIR}/${VHAL_PROTO_PREFIX}/VehiclePropConfig.pb.cc
    ${PROTO_BINARY_DIR}/${VHAL_PROTO_PREFIX}/VehiclePropertyAccess.pb.cc
    ${PROTO_BINARY_DIR}/${VHAL_PROTO_PREFIX}/VehiclePropertyChangeMode.pb.cc
    ${PROTO_BINARY_DIR}/${VHAL_PROTO_PREFIX}/VehiclePropertyStatus.pb.cc
    ${PROTO_BINARY_DIR}/${VHAL_PROTO_PREFIX}/VehiclePropValue.pb.cc
    ${PROTO_BINARY_DIR}/${VHAL_PROTO_PREFIX}/VehiclePropValueRequest.pb.cc
    ${PROTO_BINARY_DIR}/VehicleServer.pb.cc
    ${PROTO_BINARY_DIR}/VehicleServer.grpc.pb.cc
)

add_custom_command(
    OUTPUT ${PROTO_SRCS}
    COMMAND $<TARGET_FILE:protobuf::protoc>
        --proto_path=${ANDROID_VHAL_DIR}/proto
        --proto_path=${ANDROID_VHAL_DIR}/grpc/proto
        --cpp_out=${PROTO_BINARY_DIR}
        ${PROTO_FILES_ABS}
    COMMAND $<TARGET_FILE:protobuf::protoc>
        --proto_path=${ANDROID_VHAL_DIR}/proto
        --proto_path=${ANDROID_VHAL_DIR}/grpc/proto
        --cpp_out=${PROTO_BINARY_DIR}
        --grpc_out=${PROTO_BINARY_DIR}
        --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
        ${GRPC_FILES_ABS}
    DEPENDS ${PROTO_FILES_ABS} ${GRPC_FILES_ABS}
    COMMENT "Generating protobuf and gRPC sources"
    VERBATIM
)

add_custom_target(generate_protos DEPENDS ${PROTO_SRCS})

add_library(proto_lib ${PROTO_SRCS})
add_dependencies(proto_lib generate_protos)
target_link_libraries(proto_lib
    PUBLIC
        protobuf::libprotobuf
        gRPC::grpc++
)
target_include_directories(proto_lib PUBLIC ${PROTO_BINARY_DIR})

# The generated VHAL translation units are large: MSVC needs more object file
# sections than the default, and /utf-8 keeps the source and execution charsets
# consistent for the generated string literals.
if(MSVC)
    target_compile_options(proto_lib PRIVATE /bigobj /utf-8)
endif()

set(SPDLOG_BUILD_SHARED OFF)
add_subdirectory(third_party/spdlog)

set(EXE_NAME sil-kit-android-bridge)
add_executable(${EXE_NAME}
    main.cpp
    VhalGrpcClient.cpp
    VhalGrpcConvert.cpp
)
set_target_properties(${EXE_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)
target_compile_definitions(${EXE_NAME} PRIVATE "SKAB_VHAL_VERSION=\"${PROJECT_VERSION}\"")
target_link_libraries(${EXE_NAME}
    PRIVATE
        proto_lib
        SilKit::SilKit
        spdlog::spdlog
)
target_include_directories(${EXE_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
skab_copy_runtime_dlls(${EXE_NAME})

include(GNUInstallDirs)
install(TARGETS ${EXE_NAME}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# The Debian package resolves libsilkit through dpkg-shlibdeps; Windows has no
# such mechanism, so SilKit.dll is shipped inside the archive.
if(WIN32)
    install(IMPORTED_RUNTIME_ARTIFACTS SilKit::SilKit
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()
# README.md, LICENSE.md, THIRD_PARTY_NOTICES.md and the third-party license
# texts are installed by cmake/Notices.cmake.

# Tests and test utilities live in the top-level test/ directory
# (added via add_subdirectory(test) from the root CMakeLists.txt).
