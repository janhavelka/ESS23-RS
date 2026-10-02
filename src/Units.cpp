// SPDX-License-Identifier: MIT
#include "RS485Motion/Units.h"

#include <cmath>
#include <limits>

namespace RS485Motion {
namespace {

const double kMaxEngineeringMagnitude = 9007199254740992.0; // 2^53
const long double kTau = 6.283185307179586476925286766559005768L;
static_assert(std::numeric_limits<double>::digits >= 53, "Units require at least binary64 precision");

Status fail(Err code, UnitError detail, const char* message) {
    return Status(code, static_cast<int32_t>(detail), message);
}

bool validPositionUnit(PositionUnit unit) {
    return unit >= PositionUnit::STEPS && unit <= PositionUnit::MILLIMETRES;
}

bool validTimeUnit(TimeUnit unit) {
    return unit >= TimeUnit::SECOND && unit <= TimeUnit::MILLISECOND;
}

Status validateScale(const UnitScale& scale) {
    if (scale.source < ScaleSource::UNKNOWN || scale.source > ScaleSource::QUALIFIED ||
        scale.denominator == 0 ||
        (scale.source == ScaleSource::UNKNOWN && (scale.numerator != 0 || scale.denominator != 1)) ||
        (scale.source != ScaleSource::UNKNOWN && scale.numerator == 0)) {
        return fail(Err::INVALID_CONFIG, UnitError::INVALID_SCALE, "invalid positive scale");
    }
    return Ok();
}

Status requireScale(const UnitScale& scale, long double& value) {
    if (scale.source == ScaleSource::UNKNOWN) {
        return fail(Err::INVALID_CONFIG, UnitError::MISSING_SCALE, "conversion scale is missing");
    }
    value = static_cast<long double>(scale.numerator) / scale.denominator;
    return Ok();
}

// Spatial factors use load turns as the common basis. No origins are applied.
Status turnsPerUnit(PositionUnit unit, const UnitConfig& config, long double& factor) {
    long double scale = 1;
    long double gear = 1;
    Status status;
    switch (unit) {
    case PositionUnit::TURNS: factor = 1; return Ok();
    case PositionUnit::DEGREES: factor = 1.0L / 360; return Ok();
    case PositionUnit::RADIANS: factor = 1.0L / kTau; return Ok();
    case PositionUnit::MILLIMETRES:
        status = requireScale(config.millimetresPerLoadTurn, scale);
        if (!status) return status;
        factor = 1 / scale;
        return Ok();
    case PositionUnit::STEPS:
    case PositionUnit::FULL_STEPS:
        status = requireScale(unit == PositionUnit::STEPS ? config.commandStepsPerMotorTurn
                                                        : config.fullStepsPerMotorTurn, scale);
        if (!status) return status;
        status = requireScale(config.motorTurnsPerLoadTurn, gear);
        if (!status) return status;
        factor = (unit == PositionUnit::STEPS ? config.commandPolarity : 1) / (scale * gear);
        return Ok();
    case PositionUnit::ENCODER_COUNTS:
        status = requireScale(config.encoder.countsPerUnit, scale);
        if (!status) return status;
        if (config.encoder.basis == EncoderBasis::MOTOR_TURN) {
            status = requireScale(config.motorTurnsPerLoadTurn, gear);
        } else if (config.encoder.basis == EncoderBasis::MILLIMETRE) {
            status = requireScale(config.millimetresPerLoadTurn, gear);
        }
        if (!status) return status;
        factor = config.encoder.polarity / (scale * gear);
        return Ok();
    }
    return fail(Err::ILLEGAL_VALUE, UnitError::INVALID_UNIT, "invalid spatial unit");
}

long double secondsPerUnit(TimeUnit unit) {
    switch (unit) {
    case TimeUnit::SECOND: return 1;
    case TimeUnit::MINUTE: return 60;
    case TimeUnit::MILLISECOND: return 0.001L;
    }
    return 0; // Callers validate the enum before reaching this helper.
}

Status convert(double value, PositionUnit from, PositionUnit to, long double timeFactor,
               bool sameTime, const UnitConfig& config, UnitConversion& output) {
    Status status = validateUnitConfig(config);
    if (!status) return status;
    if (!validPositionUnit(from) || !validPositionUnit(to)) {
        return fail(Err::ILLEGAL_VALUE, UnitError::INVALID_UNIT, "invalid spatial unit");
    }
    if (!std::isfinite(value)) {
        return fail(Err::ILLEGAL_VALUE, UnitError::NONFINITE_VALUE, "nonfinite engineering value");
    }
    if (std::fabs(value) > kMaxEngineeringMagnitude) {
        return fail(Err::ILLEGAL_VALUE, UnitError::PRECISION_LIMIT, "engineering input exceeds precision limit");
    }
    if ((from == PositionUnit::ENCODER_COUNTS || to == PositionUnit::ENCODER_COUNTS) &&
        config.encoder.countsPerUnit.source == ScaleSource::UNKNOWN) {
        return fail(Err::INVALID_CONFIG, UnitError::MISSING_SCALE, "encoder scale is missing");
    }
    if (from == to && sameTime) {
        output = UnitConversion(value, 0);
        return Ok();
    }

    long double fromFactor = 1;
    long double toFactor = 1;
    if (from != to) {
        status = turnsPerUnit(from, config, fromFactor);
        if (!status) return status;
        status = turnsPerUnit(to, config, toFactor);
        if (!status) return status;
    }
    const long double result = static_cast<long double>(value) * (fromFactor / toFactor) * timeFactor;
    if (!std::isfinite(result) || std::fabs(result) > kMaxEngineeringMagnitude) {
        return fail(Err::ILLEGAL_VALUE, UnitError::PRECISION_LIMIT, "engineering output exceeds precision limit");
    }
    const double converted = static_cast<double>(result);
    if (value != 0 && (result == 0 || converted == 0 || std::fpclassify(converted) == FP_SUBNORMAL)) {
        return fail(Err::ILLEGAL_VALUE, UnitError::PRECISION_LIMIT, "engineering result underflows precision");
    }
    // At most 20 elementary floating operations including rational scales,
    // time factors and pi approximation. Use a conservative binary64 bound
    // even on hosts where long double carries more precision. This describes
    // arithmetic rounding only, not inaccurate source scales or input text.
    const double error = std::fabs(converted) * (32 * std::numeric_limits<double>::epsilon());
    if (error > config.maxConversionError) {
        return fail(Err::ILLEGAL_VALUE, UnitError::PRECISION_LIMIT, "conversion exceeds permitted arithmetic error");
    }
    output = UnitConversion(converted, error);
    return Ok();
}

} // namespace

Status validateUnitConfig(const UnitConfig& config) {
    const UnitScale* scales[] = { &config.commandStepsPerMotorTurn, &config.motorTurnsPerLoadTurn,
                                 &config.fullStepsPerMotorTurn, &config.millimetresPerLoadTurn,
                                 &config.encoder.countsPerUnit };
    for (unsigned i = 0; i < sizeof(scales) / sizeof(scales[0]); ++i) {
        const Status status = validateScale(*scales[i]);
        if (!status) return status;
    }
    if ((config.commandPolarity != 1 && config.commandPolarity != -1) ||
        (config.encoder.polarity != 1 && config.encoder.polarity != -1)) {
        return fail(Err::INVALID_CONFIG, UnitError::INVALID_POLARITY, "polarity must be +1 or -1");
    }
    if (config.encoder.basis < EncoderBasis::MOTOR_TURN || config.encoder.basis > EncoderBasis::MILLIMETRE) {
        return fail(Err::INVALID_CONFIG, UnitError::INVALID_UNIT, "invalid encoder basis");
    }
    if (config.encoder.countsPerUnit.source != ScaleSource::UNKNOWN && config.encoder.sourceId == 0) {
        return fail(Err::INVALID_CONFIG, UnitError::MISSING_ENCODER_SOURCE, "encoder source must be identified");
    }
    const UnitSettings& s = config.settings;
    if (!validPositionUnit(s.position) || !validPositionUnit(s.velocity.position) ||
        !validPositionUnit(s.acceleration.position) || !validTimeUnit(s.velocity.time) ||
        !validTimeUnit(s.acceleration.velocityTime) || !validTimeUnit(s.acceleration.accelerationTime)) {
        return fail(Err::INVALID_CONFIG, UnitError::INVALID_UNIT, "invalid preferred unit");
    }
    if (!std::isfinite(config.maxConversionError) || config.maxConversionError <= 0) {
        return fail(Err::INVALID_CONFIG, UnitError::INVALID_ERROR_BOUND, "arithmetic error allowance must be positive");
    }
    return Ok();
}

Status convertDisplacement(double value, PositionUnit from, PositionUnit to,
                           const UnitConfig& config, UnitConversion& output) {
    return convert(value, from, to, 1, true, config, output);
}

Status convertVelocity(double value, VelocityUnit from, VelocityUnit to,
                       const UnitConfig& config, UnitConversion& output) {
    if (!validTimeUnit(from.time) || !validTimeUnit(to.time)) {
        return fail(Err::ILLEGAL_VALUE, UnitError::INVALID_UNIT, "invalid velocity time unit");
    }
    return convert(value, from.position, to.position,
                   secondsPerUnit(to.time) / secondsPerUnit(from.time),
                   from.time == to.time, config, output);
}

Status convertAcceleration(double value, AccelerationUnit from, AccelerationUnit to,
                           const UnitConfig& config, UnitConversion& output) {
    if (!validTimeUnit(from.velocityTime) || !validTimeUnit(to.velocityTime) ||
        !validTimeUnit(from.accelerationTime) || !validTimeUnit(to.accelerationTime)) {
        return fail(Err::ILLEGAL_VALUE, UnitError::INVALID_UNIT, "invalid acceleration time unit");
    }
    const long double timeFactor = (secondsPerUnit(to.velocityTime) / secondsPerUnit(from.velocityTime)) *
                                  (secondsPerUnit(to.accelerationTime) / secondsPerUnit(from.accelerationTime));
    return convert(value, from.position, to.position, timeFactor,
                   from.velocityTime == to.velocityTime && from.accelerationTime == to.accelerationTime,
                   config, output);
}

Status validateNativePosition(int64_t value, int64_t minimum, int64_t maximum) {
    if (minimum > maximum) {
        return fail(Err::INVALID_CONFIG, UnitError::OUT_OF_RANGE, "inverted native position limits");
    }
    if (value < minimum || value > maximum) {
        return fail(Err::ILLEGAL_VALUE, UnitError::OUT_OF_RANGE, "native position is outside limits");
    }
    return Ok();
}

Status narrowNativePosition(int64_t value, int32_t& output) {
    const Status status = validateNativePosition(value, std::numeric_limits<int32_t>::min(),
                                                std::numeric_limits<int32_t>::max());
    if (!status) return status;
    output = static_cast<int32_t>(value);
    return Ok();
}

} // namespace RS485Motion
