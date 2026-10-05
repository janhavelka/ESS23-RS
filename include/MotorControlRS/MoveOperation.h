/** @file MoveOperation.h
 * @brief Pure finite-position intent; transport and reservation are application-owned.
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "MotorControlRS/Axis.h"
#include "MotorControlRS/ActionOperation.h"

namespace MotorControlRS {
enum class MoveRamp : uint8_t { UNSPECIFIED, VERIFIED_CONFIGURED };
/** Parameter handling before start. USE_STORED explicitly trusts the drive's
 * current parameters; requested values are intent, not verified readback. */
enum class MoveSetup : uint8_t { WRITE_ALL, VERIFY_AND_UPDATE, USE_STORED };
struct MoveRequest {
    PositionRequest position;
    uint16_t speedRpm = 0; ///< Positive native motor speed; no acceleration conversion is implied.
    MoveRamp ramp = MoveRamp::UNSPECIFIED;
    MoveSetup setup = MoveSetup::WRITE_ALL;
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
