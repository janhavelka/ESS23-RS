// SPDX-License-Identifier: MIT
#pragma once
#include <MotorControlRS/profiles/ess_rs/Position.h>

namespace MotorControlRSExample { namespace Probe {
struct MotionProfileView;
enum class SimpleMotionCommandKind : uint8_t { QUERY, SETTINGS, SPEED, ACCEL, DECEL, SETUP, SCALE, MOVE_BY, MOVE_TO };
enum class SimpleMotionPhase : uint8_t { IDLE, CONFIG, PROFILE, STATE, MOVE, TERMINAL };
/** Convenience moves use the existing exact converter with nearest-step
 * quantization (ties to even). Detailed requests retain their explicit policy. */
inline MotorControlRS::PositionRequest simplePositionRequest() {
    MotorControlRS::PositionRequest request;
    request.rounding = MotorControlRS::Rounding::NEAREST;
    request.maximumQuantizationError = 0.5;
    return request;
}
/** Local desired settings or one finite move. No command retains caller storage. */
struct SimpleMotionCommand {
    SimpleMotionCommandKind kind = SimpleMotionCommandKind::QUERY;
    uint16_t nativeValue = 0;
    MotorControlRS::MoveSetup setup = MotorControlRS::MoveSetup::WRITE_ALL;
    MotorControlRS::PositionRequest position = simplePositionRequest();
};
/** Desired host settings. Applying these never writes the drive. */
struct SimpleMotionSettings {
    uint16_t speedRpm = 60, acceleration = 100, deceleration = 100;
    bool accelerationKnown = true, decelerationKnown = true;
    MotorControlRS::MoveSetup setup = MotorControlRS::MoveSetup::WRITE_ALL;
    MotorControlRS::Rational stepsPerTurn = MotorControlRS::Rational(1000);
    bool scaleKnown = true; ///< Explicit nominal ASSUMED convention, not calibration.
};
/** Cached application session. The optional context is borrowed only while the
 * console formats this view; it remains owned by the ordinary move record. */
struct SimpleMotionView : SimpleMotionSettings {
    uint32_t operationId = 0, moveOperationId = 0, readOperationId = 0, commandId = 0, target = 0;
    uint8_t address = 0;
    SimpleMotionPhase phase = SimpleMotionPhase::IDLE;
    bool pending = false, delivered = false, ok = false, moveAdmitted = false, relative = true;
    bool settingsOnly = false, configKnown = false, profileKnown = false;
    const MotionProfileView* profileEvidence = nullptr; ///< Borrowed retained settings-only read evidence.
    uint16_t subdivision = 0, direction = 0, wordOrder = 0, algorithm = 0,
        encoderResolution = 0, softLimitEnable = 0, profile[6] = {};
    bool directionKnown = false, wordOrderKnown = false, algorithmKnown = false, softLimitKnown = false;
    bool runningObserved = false, uncertain = false, interruptedByStop = false;
    bool alreadyAtTarget = false;
    bool targetPrepared = false;
    int64_t effectiveNative = 0;
    double roundingError = 0;
    MotorControlRS::ActionOutcome outcome = MotorControlRS::ActionOutcome::NONE;
    MotorControlRS::ActionExecution execution = MotorControlRS::ActionExecution::NOT_TRANSMITTED;
    MotorControlRS::ActionCompletion completion = MotorControlRS::ActionCompletion::NOT_OBSERVED;
    MotorControlRS::Status status;
    const char* error = "none";
    const MotorControlRS::ESS_RS::MoveContext* move = nullptr;
};
}} // namespace MotorControlRSExample::Probe
