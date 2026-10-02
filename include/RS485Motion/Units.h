/** @file Units.h
 * @brief Pure configurable displacement, velocity and acceleration conversions.
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdint.h>
#include "RS485Motion/Status.h"

namespace RS485Motion {

/** Spatial unit. Angles and travel refer to the configured load axis. */
enum class PositionUnit : uint8_t {
    STEPS, FULL_STEPS, ENCODER_COUNTS, TURNS, DEGREES, RADIANS, MILLIMETRES
};

enum class TimeUnit : uint8_t { SECOND, MINUTE, MILLISECOND };

/** Provenance is retained metadata, not proof that a setting is active. */
enum class ScaleSource : uint8_t { UNKNOWN, ASSUMED, DOCUMENTED, READBACK, QUALIFIED };

/** Positive rational scale; UNKNOWN is represented by numerator=0, denominator=1. */
struct UnitScale {
    uint32_t numerator;
    uint32_t denominator;
    ScaleSource source;
    constexpr UnitScale(uint32_t n = 0, uint32_t d = 1,
                        ScaleSource s = ScaleSource::UNKNOWN)
        : numerator(n), denominator(d), source(s) {}
};

enum class EncoderBasis : uint8_t { MOTOR_TURN, LOAD_TURN, MILLIMETRE };

/** One explicitly selected feedback source; sourceId is application-defined. */
struct EncoderScale {
    UnitScale countsPerUnit;
    EncoderBasis basis;
    uint32_t sourceId;
    int8_t polarity;
    constexpr EncoderScale()
        : countsPerUnit(), basis(EncoderBasis::MOTOR_TURN), sourceId(0), polarity(1) {}
};

struct VelocityUnit {
    PositionUnit position;
    TimeUnit time;
    constexpr VelocityUnit(PositionUnit p = PositionUnit::STEPS,
                           TimeUnit t = TimeUnit::SECOND) : position(p), time(t) {}
};

/** Distinct denominators allow turns/minute/second (rpm/s), not just minute^2. */
struct AccelerationUnit {
    PositionUnit position;
    TimeUnit velocityTime;
    TimeUnit accelerationTime;
    constexpr AccelerationUnit(PositionUnit p = PositionUnit::STEPS,
                               TimeUnit v = TimeUnit::SECOND,
                               TimeUnit a = TimeUnit::SECOND)
        : position(p), velocityTime(v), accelerationTime(a) {}
};

/** Independent input/display preferences. Changing these does not alter a drive. */
struct UnitSettings {
    PositionUnit position;
    VelocityUnit velocity;
    AccelerationUnit acceleration;
    constexpr UnitSettings() : position(PositionUnit::STEPS), velocity(), acceleration() {}
};

/**
 * Caller-owned interpretation only. No I/O, origin, absolute-position reference,
 * device configuration or motion limits are implied. Missing optional scales
 * are legal until a conversion needs them. Accepted assumptions are usable;
 * application policy decides when documented/readback/qualified values are needed.
 */
struct UnitConfig {
    UnitScale commandStepsPerMotorTurn;
    UnitScale motorTurnsPerLoadTurn;
    UnitScale fullStepsPerMotorTurn;
    UnitScale millimetresPerLoadTurn;
    EncoderScale encoder;
    int8_t commandPolarity;
    /** Maximum arithmetic error bound in the requested output unit; positive. */
    double maxConversionError;
    UnitSettings settings;
    constexpr UnitConfig()
        : commandStepsPerMotorTurn(), motorTurnsPerLoadTurn(), fullStepsPerMotorTurn(),
          millimetresPerLoadTurn(), encoder(), commandPolarity(1),
          maxConversionError(0.000001), settings() {}
};

/** Structured reason stored in Status::detail for unit failures. */
enum class UnitError : int32_t {
    NONE, INVALID_SCALE, MISSING_SCALE, INVALID_POLARITY, INVALID_UNIT,
    MISSING_ENCODER_SOURCE, NONFINITE_VALUE, PRECISION_LIMIT, OUT_OF_RANGE,
    INVALID_ERROR_BOUND
};

/** Approximate engineering value; error excludes uncertainty in supplied scales. */
struct UnitConversion {
    double value;
    double absoluteErrorBound;
    constexpr UnitConversion(double v = 0, double e = 0) : value(v), absoluteErrorBound(e) {}
};

/** Validate settings, positive known scales and +/-1 polarity; performs no I/O. */
Status validateUnitConfig(const UnitConfig& config);

/**
 * Convert signed displacement, preserving turns (no wrap or origin transform).
 * STEPS follow commandPolarity; ENCODER_COUNTS follow the selected encoder's
 * polarity/basis. Other units follow positive load-axis direction.
 * All conversion outputs stay unchanged on error. Finite input/output magnitude
 * must be <=2^53; larger native integers must stay in the integer API.
 * Arithmetic error must fit config.maxConversionError in the destination unit.
 */
Status convertDisplacement(double value, PositionUnit from, PositionUnit to,
                           const UnitConfig& config, UnitConversion& output);

/** Convert signed velocity, including load rpm as TURNS/MINUTE. No motion occurs. */
Status convertVelocity(double value, VelocityUnit from, VelocityUnit to,
                       const UnitConfig& config, UnitConversion& output);

/**
 * Convert signed acceleration. E.g. rpm/s is TURNS/MINUTE/SECOND. Ramp magnitude
 * and profile-specific ramp-time validation belong to later motion preparation.
 */
Status convertAcceleration(double value, AccelerationUnit from, AccelerationUnit to,
                           const UnitConfig& config, UnitConversion& output);

/** Validate an exact native int64 position against a profile's inclusive limits. */
Status validateNativePosition(int64_t value, int64_t minimum, int64_t maximum);

/** Narrow an exact native position to a signed 32-bit field, without floating point. */
Status narrowNativePosition(int64_t value, int32_t& output);

} // namespace RS485Motion
