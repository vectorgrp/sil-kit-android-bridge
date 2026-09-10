// SPDX-FileCopyrightText: 2026 Vector Informatik GmbH
// SPDX-License-Identifier: MIT
#pragma once

// Pure translation between the VHAL protobuf messages and CarProperty, with no
// gRPC stub, no channel and no I/O. Split out of VhalGrpcClient so it can be unit
// tested: everything the bridge gets wrong about a VHAL response is decided here.
//
// Deliberately proto-only - it does not include <grpcpp/grpcpp.h> - so a test
// linking this needs protobuf and the generated sources, not the gRPC runtime.

#include <optional>
#include <stdexcept>
#include <string>

// winsock2.h (pulled in by grpcpp on Windows) defines TRY_AGAIN=4 and
// ACCESS_DENIED=5 as macros. Those names are StatusCode enumerators in the VHAL
// protobuf headers, and this file names them, so the macros have to go. The same
// guard exists in VhalGrpcClient.hpp; keep both, since either header can be
// included first.
#ifdef TRY_AGAIN
#undef TRY_AGAIN
#endif
#ifdef ACCESS_DENIED
#undef ACCESS_DENIED
#endif

#include "android/hardware/automotive/vehicle/StatusCode.pb.h"
#include "android/hardware/automotive/vehicle/VehiclePropConfig.pb.h"
#include "android/hardware/automotive/vehicle/VehiclePropValue.pb.h"
#include "android/hardware/automotive/vehicle/VehiclePropValueRequest.pb.h"

#include "IVhalClient.hpp"
#include "VhalProperty.hpp"

class VhalClientError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

namespace VhalConvert {

//! Normalise a VHAL server URI, or throw VhalClientError if the scheme is unusable.
auto ValidateUri(const std::string& uri) -> std::string;

//! Translate one VehiclePropValue. nullopt when the property type is unsupported
//! or the message carries no value for it.
auto ToCarProperty(const android::hardware::automotive::vehicle::proto::VehiclePropValue& value)
    -> std::optional<CarProperty>;

//! Translate one read result, gating on its StatusCode first. A non-OK status
//! yields nullopt even when a value is attached, so a refused read can never be
//! mistaken for data.
auto ToCarProperty(const android::hardware::automotive::vehicle::proto::GetValueResult& result)
    -> std::optional<CarProperty>;

//! The inverse of ToCarProperty: build the VehiclePropValue for a write, checking
//! the value against the type AOSP declares for the property id. Throws
//! VhalClientError on a mismatch. Keeping this next to ToCarProperty is deliberate
//! - the two have to agree on which protobuf field carries which CarProperty arm,
//! and a BYTES value written into int32_values was exactly how that drifted before.
auto ToPropValue(int32_t propId, const CarProperty::Value& value)
    -> android::hardware::automotive::vehicle::proto::VehiclePropValue;

//! Translate one property configuration entry. This is what GetPropertyStatus
//! answers from, so it decides whether a caller is told a property is readable,
//! writable or absent.
//!
//! Access rule: the top-level access, or - when that is NONE and per-area
//! configs exist - the union of the area accesses. AOSP V3+ lets a VHAL declare
//! access per area only, and reading the top-level field alone would then report
//! a perfectly usable property as inaccessible.
auto ToPropertyInfo(const android::hardware::automotive::vehicle::proto::VehiclePropConfig& config) -> PropertyInfo;

//! nullopt when every write succeeded; otherwise a message naming the first
//! offending StatusCode, ready to put in a VhalClientError.
auto FirstSetError(const android::hardware::automotive::vehicle::proto::SetValueResults& results)
    -> std::optional<std::string>;

} // namespace VhalConvert
