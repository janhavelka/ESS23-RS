/** @file MoveOperation.h
 * @brief Pure finite-position intent; transport and reservation are application-owned.
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "MotorControlRS/Axis.h"
#include "MotorControlRS/ActionOperation.h"

namespace MotorControlRS {
enum class MoveRamp : uint8_t { UNSPECIFIED, VERIFIED_CONFIGURED };
struct MoveRequest {
    PositionRequest position;
    uint16_t speedRpm = 0; ///< Positive native motor speed; no acceleration conversion is implied.
    MoveRamp ramp = MoveRamp::UNSPECIFIED;
};
enum class MoveError : int32_t {
    NONE, INVALID_TARGET, INVALID_OPERATION, INVALID_DEADLINE, INVALID_OPTIONS,
    INVALID_REQUEST, READINESS, STALE_CONFIGURATION, UNRESOLVED_UNITS,
    UNRESOLVED_SIGN, UNRESOLVED_BASIS, UNRESOLVED_RAMP, INVALID_STATE,
    WRONG_CORRELATION, INVALID_EVENT, CLOCK_ERROR, DEADLINE_EXPIRED,
    TRANSPORT_FAILURE, CANCELLED, TIMING_UNQUALIFIED, UNCONFIRMED_RESPONSE,
    OBSERVATION_LIMIT, DRIVE_FAULT
};
} // namespace MotorControlRS
