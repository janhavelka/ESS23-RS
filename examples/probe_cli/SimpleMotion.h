// SPDX-License-Identifier: MIT
#pragma once
#include <MotorControlRS/profiles/ess_rs/Position.h>

namespace MotorControlRSExample { namespace Probe {
enum class SimpleMotionCommandKind : uint8_t { QUERY, SPEED, ACCEL, DECEL, SETUP, SCALE, MOVE_BY, MOVE_TO };
enum class SimpleMotionPhase : uint8_t { IDLE, CONFIG, PROFILE, STATE, MOVE, TERMINAL };
/** Local desired settings or one finite move. No command retains caller storage. */
struct SimpleMotionCommand {
    SimpleMotionCommandKind kind = SimpleMotionCommandKind::QUERY;
    uint16_t nativeValue = 0;
    MotorControlRS::MoveSetup setup = MotorControlRS::MoveSetup::WRITE_ALL;
    MotorControlRS::PositionRequest position;
};
/** Desired host settings. Applying these never writes the drive. */
struct SimpleMotionSettings {
    uint16_t speedRpm = 60, acceleration = 0, deceleration = 0;
    bool accelerationKnown = false, decelerationKnown = false;
    MotorControlRS::MoveSetup setup = MotorControlRS::MoveSetup::WRITE_ALL;
    MotorControlRS::Rational stepsPerTurn;
    bool scaleKnown = false;
};
/** Cached application session. The optional context is borrowed only while the
 * console formats this view; it remains owned by the ordinary move record. */
struct SimpleMotionView : SimpleMotionSettings {
    uint32_t operationId = 0, moveOperationId = 0, readOperationId = 0, commandId = 0, target = 0;
    uint8_t address = 0;
    SimpleMotionPhase phase = SimpleMotionPhase::IDLE;
    bool pending = false, delivered = false, ok = false, moveAdmitted = false, relative = true;
    bool runningObserved = false, uncertain = false, interruptedByStop = false;
    MotorControlRS::ActionOutcome outcome = MotorControlRS::ActionOutcome::NONE;
    MotorControlRS::ActionExecution execution = MotorControlRS::ActionExecution::NOT_TRANSMITTED;
    MotorControlRS::ActionCompletion completion = MotorControlRS::ActionCompletion::NOT_OBSERVED;
    MotorControlRS::Status status;
    const char* error = "none";
    const MotorControlRS::ESS_RS::MoveContext* move = nullptr;
};
}} // namespace MotorControlRSExample::Probe
