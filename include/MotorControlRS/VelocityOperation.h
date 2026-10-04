/** @file VelocityOperation.h
 * @brief Signed velocity intent and finite host stop policy. No I/O.
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "MotorControlRS/Axis.h"
#include "MotorControlRS/ActionOperation.h"

namespace MotorControlRS {
enum class VelocityRamp : uint8_t { UNSPECIFIED, VERIFIED_CONFIGURED, ACCELERATION };
/** A finite host duration includes setup; zero speed is not a stop or release.
 * Unsupported acceleration mapping, jerk, blending and live updates reject
 * before traffic. Independent unit conversions remain available in Units.h. */
struct VelocityRequest {
    Rational value;
    VelocityUnit unit = VelocityUnit(PositionUnit::TURNS, TimeUnit::MINUTE);
    CoordinateFrame frame = CoordinateFrame::NATIVE;
    uint32_t configurationGeneration = 0;
    Rounding rounding = Rounding::EXACT;
    double maximumQuantizationErrorRpm = 0;
    bool approximate = false;
    double maximumApproximationErrorRpm = 0;
    VelocityRamp ramp = VelocityRamp::UNSPECIFIED;
    double acceleration = 0, deceleration = 0;
    AccelerationUnit accelerationUnit, decelerationUnit;
    double jerk = 0;
    bool blending = false, liveUpdate = false;
    uint32_t durationUs = 0;
    StopPolicy stop;
};
struct PreparedVelocityTarget {
    int16_t nativeRpm = 0;
    NativeQuantity requestedRpm;
    double approximateRequestedRpm = 0, roundingError = 0, approximationErrorBound = 0;
    bool exactArithmetic = true;
};
enum class VelocityError : int32_t {
    NONE, INVALID_TARGET, INVALID_OPERATION, INVALID_DEADLINE, INVALID_OPTIONS,
    INVALID_REQUEST, STALE_CONFIGURATION, UNRESOLVED_UNITS, UNRESOLVED_SIGN,
    UNRESOLVED_RAMP, READINESS, ZERO_SPEED, UNSUPPORTED_CAPABILITY,
    INVALID_STATE, WRONG_CORRELATION, INVALID_EVENT, CLOCK_ERROR,
    DEADLINE_EXPIRED, TRANSPORT_FAILURE, CANCELLED, TIMING_UNQUALIFIED,
    UNCONFIRMED_RESPONSE, OBSERVATION_LIMIT, DRIVE_FAULT, SERVICE_MISSED,
    ACTIVITY_NOT_OBSERVED
};
/** Shared exact conversion/quantization; output unchanged on error. NATIVE is
 * native motor rpm, not command steps/s. Other frames require only their actual
 * conversion scales; no position origin or reference is consumed. */
Status prepareVelocityTarget(const VelocityRequest&, const AxisConfig&,
                             PreparedVelocityTarget&) noexcept;
} // namespace MotorControlRS
