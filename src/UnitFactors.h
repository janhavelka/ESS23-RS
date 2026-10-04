// SPDX-License-Identifier: MIT
#pragma once
#include "MotorControlRS/Units.h"
namespace MotorControlRS { namespace detail {
constexpr long double tau = 6.283185307179586476925286766559005768L;
/** Shared factor selection for approximate conversions and exact preparation.
 * Kept factored so exact callers cancel before multiplication. */
struct UnitFactors {
    uint32_t numerator[8] = {};
    uint32_t denominator[8] = {};
    unsigned count = 0;
    int sign = 1;
    int tauPower = 0;
    void add(uint32_t n, uint32_t d, bool inverse = false) {
        numerator[count] = inverse ? d : n;
        denominator[count++] = inverse ? n : d;
    }
};
inline Status addScale(const UnitScale& s, UnitFactors& f, bool inverse) {
    if (s.source == ScaleSource::UNKNOWN)
        return Status(Err::INVALID_CONFIG, static_cast<int32_t>(UnitError::MISSING_SCALE), "conversion scale is missing");
    f.add(s.numerator, s.denominator, inverse);
    return Ok();
}
inline Status spatialFactor(PositionUnit u, const UnitConfig& c, UnitFactors& f,
                            EncoderBasis& basis, bool inverse) {
    basis = EncoderBasis::LOAD_TURN;
    switch (u) {
    case PositionUnit::TURNS: return Ok();
    case PositionUnit::DEGREES: f.add(1, 360, inverse); return Ok();
    case PositionUnit::RADIANS: f.tauPower += inverse ? 1 : -1; return Ok();
    case PositionUnit::MILLIMETRES: basis = EncoderBasis::MILLIMETRE; return Ok();
    case PositionUnit::STEPS:
        basis = EncoderBasis::MOTOR_TURN; f.sign *= c.commandPolarity;
        return addScale(c.commandStepsPerMotorTurn, f, !inverse);
    case PositionUnit::FULL_STEPS:
        basis = EncoderBasis::MOTOR_TURN;
        return addScale(c.fullStepsPerMotorTurn, f, !inverse);
    case PositionUnit::ENCODER_COUNTS:
        basis = c.encoder.basis; f.sign *= c.encoder.polarity;
        return addScale(c.encoder.countsPerUnit, f, !inverse);
    }
    return Status(Err::ILLEGAL_VALUE, static_cast<int32_t>(UnitError::INVALID_UNIT), "invalid spatial unit");
}
inline Status unitFactors(PositionUnit from, PositionUnit to, const UnitConfig& c,
                          UnitFactors& f) {
    EncoderBasis a, b;
    Status s = spatialFactor(from, c, f, a, false);
    if (!s) return s;
    s = spatialFactor(to, c, f, b, true);
    if (!s) return s;
    if (a == b) return Ok();
    if (a != EncoderBasis::LOAD_TURN) {
        s = addScale(a == EncoderBasis::MOTOR_TURN ? c.motorTurnsPerLoadTurn : c.millimetresPerLoadTurn, f, true);
        if (!s) return s;
    }
    if (b != EncoderBasis::LOAD_TURN)
        return addScale(b == EncoderBasis::MOTOR_TURN ? c.motorTurnsPerLoadTurn : c.millimetresPerLoadTurn, f, false);
    return Ok();
}
inline long double approximateFactor(const UnitFactors& f) {
    long double v = f.sign;
    for (unsigned i = 0; i < f.count; ++i)
        v *= static_cast<long double>(f.numerator[i]) / f.denominator[i];
    if (f.tauPower < 0) v /= tau;
    if (f.tauPower > 0) v *= tau;
    return v;
}
}}
