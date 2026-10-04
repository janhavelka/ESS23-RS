// SPDX-License-Identifier: MIT
#pragma once
#include "MotorControlRS/Axis.h"

namespace MotorControlRSExample { namespace Probe {

/** Synchronous host-only requests; these never reserve a bus operation ID. */
enum class AxisCommandKind : uint8_t { QUERY, CONFIGURE, ORIGIN, PREPARE };
enum class AxisField : uint8_t {
    COMMAND_SCALE, GEAR, FULL_STEP_SCALE, LEAD, ENCODER_SCALE, ENCODER_ID,
    ENCODER_BASIS, ENCODER_POLARITY, POLARITY, POSITION_UNIT, VELOCITY_UNIT,
    ACCELERATION_UNIT, NATIVE_LIMITS, SOFT_LIMITS, RELATIVE_BASES
};
struct AxisCommand {
    AxisCommandKind kind = AxisCommandKind::QUERY;
    AxisField field = AxisField::COMMAND_SCALE;
    MotorControlRS::Rational value, secondValue;
    bool clear = false; ///< Explicit unknown scale or disabled host soft limits.
    MotorControlRS::EncoderBasis encoderBasis = MotorControlRS::EncoderBasis::MOTOR_TURN;
    MotorControlRS::PositionUnit positionUnit = MotorControlRS::PositionUnit::STEPS;
    MotorControlRS::VelocityUnit velocityUnit;
    MotorControlRS::AccelerationUnit accelerationUnit;
    MotorControlRS::PositionRequest position;
};
struct AxisView {
    bool commandPolarityKnown = true;
    MotorControlRS::AxisConfig configuration;
    MotorControlRS::PreparedTarget prepared;
};

}} // namespace MotorControlRSExample::Probe
