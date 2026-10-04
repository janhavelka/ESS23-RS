// SPDX-License-Identifier: MIT
#include "MotorControlRS/Axis.h"
#include "MotorControlRS/VelocityOperation.h"
#include "UnitFactors.h"
#include <cmath>
#include <limits>

namespace MotorControlRS {
namespace {
Status fail(AxisError e, const char* text, Err code = Err::ILLEGAL_VALUE) {
    return Status(code, static_cast<int32_t>(e), text);
}
Status unitStatus(Status s) {
    if (!s) s.detail += static_cast<int32_t>(AxisError::UNIT_ERROR_BASE);
    return s;
}
uint64_t magnitude(int64_t n) { return n < 0 ? uint64_t(-(n + 1)) + 1 : uint64_t(n); }
uint64_t gcd(uint64_t a, uint64_t b) { while (b) { const uint64_t r = a % b; a = b; b = r; } return a; }
// Compare an exact bounded unsigned fraction to binary64 without rounding the
// uint64 operands. The fractional expansion is bounded by 116 bits: values
// smaller than 2^-64 cannot reach a nonzero fraction with a uint64 denominator.
int compareRatio(uint64_t n, uint64_t d, double value) {
    if (value >= 18446744073709551616.0) return -1;
    const uint64_t integer = static_cast<uint64_t>(std::floor(value));
    const uint64_t quotient = n / d;
    if (quotient != integer) return quotient < integer ? -1 : 1;
    uint64_t remainder = n % d;
    const double fraction = value - static_cast<double>(integer);
    if (!remainder) return fraction == 0 ? 0 : -1;
    if (fraction == 0 || fraction < std::ldexp(1.0,-64)) return 1;
    int exponent = 0;
    const uint64_t mantissa = static_cast<uint64_t>(std::ldexp(std::frexp(fraction,&exponent),53));
    const int last = 53 - exponent;
    for (int i = 1; i <= last; ++i) {
        const bool ratioBit = remainder >= d - remainder;
        remainder = ratioBit ? remainder - (d - remainder) : remainder + remainder;
        const int bitIndex = last - i;
        const bool floatBit = bitIndex < 53 && ((mantissa >> bitIndex) & 1u);
        if (ratioBit != floatBit) return ratioBit ? 1 : -1;
    }
    return remainder ? 1 : 0;
}
bool add(int64_t a, int64_t b, int64_t& out) {
    if ((b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b)) return false;
    out = a + b; return true;
}
bool subtract(int64_t a, int64_t b, int64_t& out) {
    if ((b > 0 && a < INT64_MIN + b) || (b < 0 && a > INT64_MAX + b)) return false;
    out = a - b; return true;
}
bool signedMagnitude(uint64_t n, bool negative, int64_t& out) {
    const uint64_t maximum = uint64_t(INT64_MAX) + (negative ? 1u : 0u);
    if (n > maximum) return false;
    out = negative ? (n == uint64_t(INT64_MAX) + 1u ? INT64_MIN : -int64_t(n)) : int64_t(n);
    return true;
}
bool sameTarget(const ReadTarget& a, const ReadTarget& b) {
    return a.id == b.id && a.address == b.address && a.generation == b.generation;
}
bool validTarget(const ReadTarget& t) { return t.id && t.generation; }
bool sourceKnown(ScaleSource s) { return s >= ScaleSource::ASSUMED && s <= ScaleSource::QUALIFIED; }
bool sameScale(const UnitScale& a, const UnitScale& b) {
    return a.numerator == b.numerator && a.denominator == b.denominator && a.source == b.source;
}
bool sameInterpretation(const UnitConfig& a, const UnitConfig& b) {
    return sameScale(a.commandStepsPerMotorTurn,b.commandStepsPerMotorTurn) &&
        sameScale(a.motorTurnsPerLoadTurn,b.motorTurnsPerLoadTurn) &&
        sameScale(a.fullStepsPerMotorTurn,b.fullStepsPerMotorTurn) &&
        sameScale(a.millimetresPerLoadTurn,b.millimetresPerLoadTurn) &&
        sameScale(a.encoder.countsPerUnit,b.encoder.countsPerUnit) &&
        a.encoder.basis == b.encoder.basis && a.encoder.sourceId == b.encoder.sourceId &&
        a.encoder.polarity == b.encoder.polarity && a.commandPolarity == b.commandPolarity;
}
Status evidence(const AxisConfig& c, const AxisReference& r, bool stationary, bool native) {
    if (!sameTarget(c.target,r.target) || c.generation != r.configurationGeneration)
        return fail(AxisError::STALE_GENERATION,"reference target/generation mismatch",Err::INVALID_CONFIG);
    if (!sourceKnown(r.source) || r.nowUs < r.observedUs || r.nowUs - r.observedUs > r.maximumAgeUs)
        return fail(AxisError::STALE_REFERENCE,"reference is absent or stale",Err::INVALID_CONFIG);
    if (stationary && (!r.idle || !r.stationary))
        return fail(AxisError::NOT_STATIONARY,"idle stationary evidence is required",Err::INVALID_CONFIG);
    if (native && !r.nativeKnown)
        return fail(AxisError::MISSING_REFERENCE,"native coordinate relation is not established",Err::INVALID_CONFIG);
    return Ok();
}
Status increment(AxisConfig& c) {
    if (!c.generation || c.generation == UINT32_MAX) return fail(AxisError::GENERATION_EXHAUSTED,"configuration generation exhausted",Err::INVALID_CONFIG);
    ++c.generation; return Ok();
}
Status factors(const PositionRequest& r, const AxisConfig& c, detail::UnitFactors& f) {
    if (r.frame == CoordinateFrame::NATIVE) {
        if (r.unit != PositionUnit::STEPS) return fail(AxisError::INVALID_ARGUMENT,"native frame requires command increments");
        return Ok();
    }
    if (r.unit == PositionUnit::STEPS) { f.sign = c.units.commandPolarity; return Ok(); }
    if (r.frame == CoordinateFrame::MOTOR && r.unit == PositionUnit::MILLIMETRES)
        return fail(AxisError::INVALID_ARGUMENT,"linear travel uses load frame");
    UnitConfig units = c.units;
    // An explicitly selected motor angle has no load gear dependency.
    if (r.frame == CoordinateFrame::MOTOR &&
        (r.unit == PositionUnit::TURNS || r.unit == PositionUnit::DEGREES || r.unit == PositionUnit::RADIANS))
        units.motorTurnsPerLoadTurn = UnitScale(1,1,ScaleSource::DOCUMENTED);
    return unitStatus(detail::unitFactors(r.unit,PositionUnit::STEPS,units,f));
}
Status exactNative(const PositionRequest& r, const detail::UnitFactors& f, NativeQuantity& out) {
    uint64_t n[9] = {}, d[9] = {};
    n[0] = magnitude(r.value.numerator); d[0] = r.value.denominator;
    const unsigned count = f.count + 1;
    for (unsigned i = 0; i < f.count; ++i) { n[i+1] = f.numerator[i]; d[i+1] = f.denominator[i]; }
    // All factor cancellation precedes multiplication; bounded 9x9 scan.
    for (unsigned i = 0; i < count; ++i) for (unsigned j = 0; j < count; ++j) {
        const uint64_t common = gcd(n[i],d[j]);
        if (common) { n[i] /= common; d[j] /= common; }
    }
    uint64_t numerator = 1, denominator = 1;
    for (unsigned i = 0; i < count; ++i) {
        if ((n[i] && numerator > UINT64_MAX / n[i]) || denominator > UINT64_MAX / d[i])
            return fail(AxisError::ARITHMETIC_OVERFLOW,"reduced exact fraction exceeds fixed storage");
        numerator *= n[i]; denominator *= d[i];
    }
    const bool negative = (r.value.numerator < 0) != (f.sign < 0);
    if (!signedMagnitude(numerator / denominator,negative,out.integral))
        return fail(AxisError::ARITHMETIC_OVERFLOW,"native integral overflow");
    out.numerator = numerator % denominator; out.denominator = denominator;
    out.negative = negative && out.numerator != 0;
    return Ok();
}
void normalize(NativeQuantity& q) {
    if (!q.numerator) { q.negative = false; return; }
    if (q.integral > 0 && q.negative) {
        --q.integral; q.numerator = q.denominator - q.numerator; q.negative = false;
    } else if (q.integral < 0 && !q.negative) {
        ++q.integral; q.numerator = q.denominator - q.numerator; q.negative = true;
    }
}
Status quantize(const NativeQuantity& q, Rounding policy, double allowed, int64_t& result, double& error) {
    result = q.integral; error = 0;
    if (!q.numerator) return Ok();
    if (policy == Rounding::EXACT) return fail(AxisError::FRACTIONAL_TARGET,"target is not exactly representable");
    bool away = false;
    if (policy == Rounding::FLOOR) away = q.negative;
    else if (policy == Rounding::CEIL) away = !q.negative;
    else if (policy == Rounding::NEAREST) {
        const uint64_t other = q.denominator - q.numerator;
        away = q.numerator > other || (q.numerator == other && magnitude(q.integral) % 2 != 0);
    }
    if (away && !add(result,q.negative ? -1 : 1,result))
        return fail(AxisError::ARITHMETIC_OVERFLOW,"rounded native target overflow");
    const uint64_t errorNumerator = away ? q.denominator - q.numerator : q.numerator;
    const long double exactError = static_cast<long double>(errorNumerator) / q.denominator;
    if (compareRatio(errorNumerator,q.denominator,allowed) > 0)
        return fail(AxisError::QUANTIZATION_ERROR,"rounding exceeds requested allowance");
    error = static_cast<double>(exactError) * ((away != q.negative) ? 1 : -1);
    return Ok();
}
bool inBounds(const NativeQuantity& q, int64_t low, int64_t high) {
    if (q.integral < low || q.integral > high) return false;
    return !(q.integral == low && q.numerator && q.negative) &&
           !(q.integral == high && q.numerator && !q.negative);
}
Status requestedBounds(const NativeQuantity& q, const AxisConfig& c, bool soft) {
    if (!inBounds(q,c.nativeMinimum,c.nativeMaximum) ||
        (soft && !inBounds(q,c.softMinimum,c.softMaximum)))
        return fail(AxisError::LIMIT,"requested target is outside limits");
    return Ok();
}
Status effectiveBounds(int64_t q, const AxisConfig& c, bool soft) {
    if (!validateNativePosition(q,c.nativeMinimum,c.nativeMaximum) ||
        (soft && !validateNativePosition(q,c.softMinimum,c.softMaximum)))
        return fail(AxisError::LIMIT,"effective target is outside limits");
    return Ok();
}
// Modular arithmetic never multiplies native positions by a denominator. The
// loop has at most 64 iterations, including signed int64 extremes.
uint64_t modularAdd(uint64_t a, uint64_t b, uint64_t modulus) {
    return a >= modulus - b ? a - (modulus - b) : a + b;
}
uint64_t modularMultiply(uint64_t a, uint64_t b, uint64_t modulus) {
    uint64_t result = 0; a %= modulus;
    while (b) {
        if (b & 1u) result = modularAdd(result,a,modulus);
        b >>= 1;
        if (b) a = modularAdd(a,a,modulus);
    }
    return result;
}
uint64_t nativePhase(int64_t value, uint64_t denominator, uint64_t period) {
    const uint64_t phase = modularMultiply(magnitude(value),denominator,period);
    return value < 0 && phase ? period - phase : phase;
}
Status turnPeriod(const PositionRequest& request, const AxisConfig& c, NativeQuantity& period) {
    PositionRequest turn = request; turn.unit = PositionUnit::TURNS;
    turn.value = Rational(1); turn.relative = true; turn.wrapped = false;
    detail::UnitFactors f; Status s = factors(turn,c,f); if (!s) return s;
    f.sign = 1; return exactNative(turn,f,period);
}
Status exactAngle(const PositionRequest& r, const AxisConfig& c, const AxisReference& ref,
                  const detail::UnitFactors& f, PreparedTarget& next) {
    NativeQuantity angle, period;
    const bool zeroRadians = r.unit == PositionUnit::RADIANS;
    PositionRequest normalized = r;
    if (!zeroRadians) {
        const uint64_t turn = r.unit == PositionUnit::DEGREES ? 360 : 1;
        if (r.value.denominator <= UINT64_MAX / turn) {
            const uint64_t remainder = magnitude(r.value.numerator) % (r.value.denominator * turn);
            signedMagnitude(remainder,r.value.numerator < 0,normalized.value.numerator);
        }
    }
    Status s = zeroRadians ? Ok() : exactNative(normalized,f,angle); if (!s) return s;
    s = turnPeriod(r,c,period); if (!s) return s;
    const uint64_t common = gcd(angle.denominator,period.denominator);
    const uint64_t multiplier = period.denominator / common;
    if (angle.denominator > UINT64_MAX / multiplier)
        return fail(AxisError::ARITHMETIC_OVERFLOW,"wrapped denominator exceeds fixed storage");
    const uint64_t denominator = angle.denominator * multiplier;
    if (uint64_t(period.integral) > (UINT64_MAX - period.numerator) / period.denominator)
        return fail(AxisError::ARITHMETIC_OVERFLOW,"wrapped turn exceeds fixed storage");
    const uint64_t periodNumerator = uint64_t(period.integral) * period.denominator + period.numerator;
    const uint64_t periodMultiplier = denominator / period.denominator;
    if (!periodNumerator || periodNumerator > UINT64_MAX / periodMultiplier)
        return fail(AxisError::ARITHMETIC_OVERFLOW,"wrapped turn exceeds fixed storage");
    const uint64_t modulus = periodNumerator * periodMultiplier;
    uint64_t phase = nativePhase(angle.integral,denominator,modulus);
    const uint64_t fraction = modularMultiply(angle.numerator,denominator / angle.denominator,modulus);
    phase = modularAdd(phase,angle.negative && fraction ? modulus - fraction : fraction,modulus);
    phase = modularAdd(phase,nativePhase(c.originNative,denominator,modulus),modulus);
    const uint64_t current = nativePhase(ref.nativePosition,denominator,modulus);
    const uint64_t positive = phase >= current ? phase - current : modulus - (current - phase);
    const uint64_t negative = positive ? modulus - positive : 0;
    bool nativePositive = c.units.commandPolarity > 0;
    if (r.path == AnglePath::NEGATIVE) nativePositive = !nativePositive;
    if (r.path == AnglePath::SHORTEST) {
        if (positive == negative && positive) {
            if (r.tie == HalfTurnTie::REJECT)
                return fail(AxisError::AMBIGUOUS,"wrapped half-turn requires an explicit tie direction");
            nativePositive = (r.tie == HalfTurnTie::POSITIVE) == (c.units.commandPolarity > 0);
        } else nativePositive = positive < negative;
    }
    const uint64_t distance = nativePositive ? positive : negative;
    int64_t integral;
    if (!signedMagnitude(distance / denominator,!nativePositive,integral) ||
        !add(ref.nativePosition,integral,next.requestedNative.integral))
        return fail(AxisError::ARITHMETIC_OVERFLOW,"wrapped endpoint overflows");
    next.requestedNative.numerator = distance % denominator;
    next.requestedNative.denominator = denominator;
    next.requestedNative.negative = !nativePositive && next.requestedNative.numerator;
    normalize(next.requestedNative);
    s = requestedBounds(next.requestedNative,c,c.softLimitsKnown); if (!s) return s;
    s = quantize(next.requestedNative,r.rounding,r.maximumQuantizationError,next.effectiveNative,next.roundingError); if (!s) return s;
    s = effectiveBounds(next.effectiveNative,c,c.softLimitsKnown); if (!s) return s;
    if (!subtract(next.effectiveNative,ref.nativePosition,next.displacementNative))
        return fail(AxisError::ARITHMETIC_OVERFLOW,"wrapped displacement overflows");
    if ((next.displacementNative > 0 && !nativePositive) || (next.displacementNative < 0 && nativePositive))
        return fail(AxisError::AMBIGUOUS,"wrapped rounding reverses selected direction");
    next.endpointKnown = next.displacementKnown = true;
    next.endpointNative = next.effectiveNative;
    next.zeroDisplacement = next.displacementNative == 0;
    return Ok();
}
Status approximateNative(const PositionRequest& r, const detail::UnitFactors& f,
                         NativeQuantity& q, int64_t origin, int64_t& effective, double& roundError, double& error) {
    if (!r.approximate || !std::isfinite(r.maximumApproximationError) || r.maximumApproximationError <= 0)
        return fail(AxisError::APPROXIMATION_REQUIRED,"radians require an explicit approximation allowance");
    const long double input = r.rationalRadians ? static_cast<long double>(r.value.numerator) / r.value.denominator : r.radians;
    if (!std::isfinite(input) || (input != 0 && std::fpclassify(static_cast<double>(input)) != FP_NORMAL))
        return fail(AxisError::INVALID_ARGUMENT,"nonfinite or underflowing radian input");
    const long double v = input * detail::approximateFactor(f);
    if (!std::isfinite(v) || std::fabs(v) > 9007199254740992.0L)
        return fail(AxisError::ARITHMETIC_OVERFLOW,"radian native result exceeds binary64 precision");
    if (input != 0 && (v == 0 || std::fpclassify(static_cast<double>(v)) != FP_NORMAL))
        return fail(AxisError::APPROXIMATION_REQUIRED,"radian native conversion underflows");
    if (std::fabs(static_cast<long double>(origin)) > 9007199254740992.0L ||
        std::fabs(v + origin) > 9007199254740992.0L)
        return fail(AxisError::ARITHMETIC_OVERFLOW,"radian origin exceeds binary64 precision");
    const double value = static_cast<double>(v + origin);
    error = std::fabs(static_cast<double>(v)) * (64 * std::numeric_limits<double>::epsilon()) +
        (origin ? std::fabs(value) * (2 * std::numeric_limits<double>::epsilon()) : 0);
    if (error > r.maximumApproximationError)
        return fail(AxisError::APPROXIMATION_REQUIRED,"radian precision exceeds allowance");
    if (r.rounding == Rounding::EXACT && input != 0)
        return fail(AxisError::APPROXIMATION_REQUIRED,"nonzero radians are not mathematically exact native counts");
    const auto rounded = [&r](long double x) -> long double {
        switch (r.rounding) {
        case Rounding::FLOOR: return std::floor(x);
        case Rounding::CEIL: return std::ceil(x);
        case Rounding::TOWARD_ZERO: return std::trunc(x);
        case Rounding::NEAREST: {
            const long double low = std::floor(x), fraction = x - low;
            return fraction < 0.5L ? low : (fraction > 0.5L ? low + 1 :
                (std::fmod(std::fabs(low),2.0L) == 0 ? low : low + 1));
        }
        case Rounding::EXACT: return x;
        }
        return x;
    };
    const long double lo = static_cast<long double>(value) - error;
    const long double hi = static_cast<long double>(value) + error;
    const long double a = rounded(lo), b = rounded(hi);
    if (a != b) return fail(AxisError::AMBIGUOUS,"radian interval crosses a quantization boundary");
    effective = static_cast<int64_t>(a);
    roundError = static_cast<double>(a - value);
    if (std::fabs(roundError) + error > r.maximumQuantizationError)
        return fail(AxisError::QUANTIZATION_ERROR,"radian quantization exceeds allowance");
    q.integral = static_cast<int64_t>(std::trunc(v + origin));
    // Approximate quantities are never labelled as exact rational fractions.
    q.numerator = 0; q.denominator = 1; q.negative = false;
    return Ok();
}
Status approximateAngle(const PositionRequest& r, const AxisConfig& c, const AxisReference& ref,
                        const detail::UnitFactors& f, PreparedTarget& next) {
    if (!r.approximate || !std::isfinite(r.maximumApproximationError) || r.maximumApproximationError <= 0)
        return fail(AxisError::APPROXIMATION_REQUIRED,"radians require an explicit approximation allowance");
    const long double input = r.rationalRadians ? static_cast<long double>(r.value.numerator) / r.value.denominator : r.radians;
    if (!std::isfinite(input) || (input && std::fpclassify(static_cast<double>(input)) != FP_NORMAL))
        return fail(AxisError::INVALID_ARGUMENT,"nonfinite or underflowing radian input");
    const long double value = input * detail::approximateFactor(f);
    NativeQuantity exactPeriod; Status s = turnPeriod(r,c,exactPeriod); if (!s) return s;
    const long double period = static_cast<long double>(exactPeriod.integral) +
        static_cast<long double>(exactPeriod.numerator) / exactPeriod.denominator;
    if (!std::isfinite(value) || !period || std::fabs(value) > 9007199254740992.0L ||
        std::fabs(static_cast<long double>(ref.nativePosition)) > 9007199254740992.0L ||
        std::fabs(static_cast<long double>(c.originNative)) > 9007199254740992.0L)
        return fail(AxisError::ARITHMETIC_OVERFLOW,"wrapped radian arithmetic exceeds binary64 precision");
    const double error = static_cast<double>((std::fabs(value) + period +
        std::fabs(static_cast<long double>(ref.nativePosition)) + std::fabs(static_cast<long double>(c.originNative))) *
        (64 * std::numeric_limits<double>::epsilon()));
    if (error > r.maximumApproximationError)
        return fail(AxisError::APPROXIMATION_REQUIRED,"wrapped radian precision exceeds allowance");
    long double positive = std::fmod(value + c.originNative - ref.nativePosition,period);
    if (positive < 0) positive += period;
    if (positive <= error || period - positive <= error)
        return fail(AxisError::AMBIGUOUS,"radian interval crosses the same-angle boundary");
    bool nativePositive = c.units.commandPolarity > 0;
    if (r.path == AnglePath::NEGATIVE) nativePositive = !nativePositive;
    if (r.path == AnglePath::SHORTEST) {
        if (std::fabs(positive - period / 2) <= error)
            return fail(AxisError::AMBIGUOUS,"radian interval cannot resolve the half-turn path");
        nativePositive = positive < period / 2;
    }
    const long double delta = nativePositive ? positive : positive - period;
    // Reuse the radians interval quantizer for both conservative path bounds.
    // The identity factor represents an already resolved native displacement.
    PositionRequest bound = r; bound.rationalRadians = false;
    detail::UnitFactors identity;
    NativeQuantity q; int64_t low, high; double lowRound, highRound, lowError, highError;
    bound.radians = static_cast<double>(delta - error);
    s = approximateNative(bound,identity,q,ref.nativePosition,low,lowRound,lowError); if (!s) return s;
    bound.radians = static_cast<double>(delta + error);
    s = approximateNative(bound,identity,q,ref.nativePosition,high,highRound,highError); if (!s) return s;
    if (low != high) return fail(AxisError::AMBIGUOUS,"wrapped radian interval crosses a quantization boundary");
    next.approximationErrorBound = error + (lowError > highError ? lowError : highError);
    if (next.approximationErrorBound > r.maximumApproximationError)
        return fail(AxisError::APPROXIMATION_REQUIRED,"wrapped endpoint exceeds precision allowance");
    next.approximateRequestedNative = static_cast<double>(ref.nativePosition + delta);
    next.roundingError = static_cast<double>(low - (static_cast<long double>(ref.nativePosition) + delta));
    if (std::fabs(next.roundingError) + next.approximationErrorBound > r.maximumQuantizationError)
        return fail(AxisError::QUANTIZATION_ERROR,"wrapped radian quantization exceeds allowance");
    const long double endpoint = static_cast<long double>(ref.nativePosition) + delta;
    if (endpoint - next.approximationErrorBound < c.nativeMinimum || endpoint + next.approximationErrorBound > c.nativeMaximum ||
        (c.softLimitsKnown && (endpoint - next.approximationErrorBound < c.softMinimum || endpoint + next.approximationErrorBound > c.softMaximum)))
        return fail(AxisError::LIMIT,"wrapped radian interval crosses a limit");
    s = effectiveBounds(low,c,c.softLimitsKnown); if (!s) return s;
    if (!subtract(low,ref.nativePosition,next.displacementNative))
        return fail(AxisError::ARITHMETIC_OVERFLOW,"wrapped displacement overflows");
    if ((next.displacementNative > 0 && !nativePositive) || (next.displacementNative < 0 && nativePositive))
        return fail(AxisError::AMBIGUOUS,"wrapped rounding reverses selected direction");
    next.exactArithmetic = false;
    next.effectiveNative = next.endpointNative = low;
    next.endpointKnown = next.displacementKnown = true;
    next.zeroDisplacement = next.displacementNative == 0;
    next.requestedNative.integral = static_cast<int64_t>(std::trunc(endpoint));
    return Ok();
}
} // namespace

Status parseExactNumber(const char* text, Rational& output) {
    if (!text) return fail(AxisError::INVALID_ARGUMENT,"missing exact number");
    unsigned length = 0;
    while (length < 128 && text[length]) ++length;
    if (!length || length == 128) return fail(AxisError::INVALID_ARGUMENT,"exact number is empty or too long");
    unsigned i = 0; bool negative = false;
    if (text[i] == '-' || text[i] == '+') { negative = text[i++] == '-'; if (i == length) return fail(AxisError::INVALID_ARGUMENT,"missing digits"); }
    if (text[length-1] == '.') return fail(AxisError::INVALID_ARGUMENT,"missing decimal digits");
    unsigned point = length;
    for (unsigned j = i; j < length; ++j) if (text[j] == '.') { point = j; break; }
    if (point < length) {
        for (unsigned j = i; j < length; ++j)
            if (text[j] == '/') return fail(AxisError::INVALID_ARGUMENT,"fraction operands must be integers");
        while (length > point + 1 && text[length-1] == '0') --length;
        if (length == point + 1) length = point;
    }
    uint64_t n = 0, d = 1; bool digits = false, decimal = false, fraction = false;
    for (; i < length; ++i) {
        const char ch = text[i];
        if (ch == '.' && !decimal && !fraction && digits) { decimal = true; continue; }
        if (ch == '/' && !decimal && !fraction && digits) { fraction = true; d = 0; digits = false; continue; }
        if (ch < '0' || ch > '9') return fail(AxisError::INVALID_ARGUMENT,"invalid exact number syntax");
        digits = true; const unsigned digit = unsigned(ch - '0');
        uint64_t& part = fraction ? d : n;
        if (part > (UINT64_MAX - digit) / 10) return fail(AxisError::ARITHMETIC_OVERFLOW,"exact number overflows");
        part = part * 10 + digit;
        if (decimal) { if (d > UINT64_MAX / 10) return fail(AxisError::ARITHMETIC_OVERFLOW,"decimal precision exceeds storage"); d *= 10; }
    }
    if (!digits || !d || text[length-1] == '.') return fail(AxisError::INVALID_ARGUMENT,"invalid exact denominator/digits");
    const uint64_t common = gcd(n,d); n /= common; d /= common;
    int64_t signedValue;
    if (!signedMagnitude(n,negative,signedValue)) return fail(AxisError::ARITHMETIC_OVERFLOW,"exact numerator overflows signed storage");
    output = Rational(signedValue,d); return Ok();
}
Status rationalToDouble(const Rational& value, double& output) {
    if (!value.denominator) return fail(AxisError::INVALID_ARGUMENT,"zero rational denominator");
    const long double v = static_cast<long double>(value.numerator) / value.denominator;
    const double converted = static_cast<double>(v);
    if (!std::isfinite(converted) || (value.numerator && (converted == 0 || std::fpclassify(converted) != FP_NORMAL)))
        return fail(AxisError::ARITHMETIC_OVERFLOW,"policy allowance cannot be represented");
    double absolute = std::fabs(converted);
    // At most two uint64 conversions and one division precede this comparison.
    // Correct their bounded rounding with a short directed adjustment, retaining
    // exact dyadic values and the nearest downward representable allowance.
    unsigned corrections = 0;
    while (compareRatio(magnitude(value.numerator),value.denominator,absolute) < 0 && corrections < 8) {
        absolute = std::nextafter(absolute,0.0); ++corrections;
    }
    if (compareRatio(magnitude(value.numerator),value.denominator,absolute) < 0)
        return fail(AxisError::ARITHMETIC_OVERFLOW,"policy conversion cannot establish conservative rounding");
    output = value.numerator < 0 ? -absolute : absolute; return Ok();
}
Status validateAxisConfig(const AxisConfig& c) {
    Status s = unitStatus(validateUnitConfig(c.units)); if (!s) return s;
    if (!validTarget(c.target) || !c.generation) return fail(AxisError::INVALID_TARGET,"invalid axis target/generation",Err::INVALID_CONFIG);
    if (c.nativeMinimum > c.nativeMaximum || (c.supportedRelativeBases & ~7u) ||
        (c.originKnown && (!sourceKnown(c.originSource) || c.originNative < c.nativeMinimum || c.originNative > c.nativeMaximum)) ||
        (c.encoderOriginKnown && (!sourceKnown(c.encoderOriginSource) || !c.units.encoder.sourceId || c.units.encoder.countsPerUnit.source == ScaleSource::UNKNOWN ||
            c.encoderOriginNative < c.nativeMinimum || c.encoderOriginNative > c.nativeMaximum)) ||
        (c.softLimitsKnown && (c.softMinimum > c.softMaximum || c.softMinimum < c.nativeMinimum || c.softMaximum > c.nativeMaximum)))
        return fail(AxisError::INVALID_ARGUMENT,"invalid axis interpretation/limits",Err::INVALID_CONFIG);
    return Ok();
}
Status configureAxis(AxisConfig& current, const AxisConfig& candidate, const AxisReference& e,
                     AxisReference* retainedReference) {
    Status s = validateAxisConfig(current); if (!s) return s;
    s = validateAxisConfig(candidate); if (!s) return s;
    s = evidence(current,e,true,false); if (!s) return s;
    if (!sameTarget(current.target,candidate.target) || current.generation != candidate.generation)
        return fail(AxisError::STALE_GENERATION,"candidate axis binding/generation mismatch",Err::INVALID_CONFIG);
    if (candidate.originKnown != current.originKnown || candidate.originNative != current.originNative || candidate.originSource != current.originSource ||
        candidate.encoderOriginKnown != current.encoderOriginKnown || candidate.encoderOriginNative != current.encoderOriginNative || candidate.encoderOriginSource != current.encoderOriginSource)
        return fail(AxisError::MISSING_REFERENCE,"origins must use explicit referenced origin operation",Err::INVALID_CONFIG);
    if (candidate.softLimitsKnown && (!current.softLimitsKnown || candidate.softMinimum != current.softMinimum || candidate.softMaximum != current.softMaximum)) {
        s = evidence(current,e,true,true); if (!s) return s;
    }
    AxisConfig next = candidate;
    const bool interpretationUnchanged = sameInterpretation(current.units,candidate.units);
    if (!interpretationUnchanged) {
        next.originKnown = false; next.originSource = ScaleSource::UNKNOWN;
        next.encoderOriginKnown = false; next.encoderOriginSource = ScaleSource::UNKNOWN;
        next.softLimitsKnown = false;
    }
    s = increment(next); if (!s) return s;
    AxisReference reference;
    if (interpretationUnchanged) {
        reference = e;
        reference.configurationGeneration = next.generation;
    }
    current = next;
    if (retainedReference) *retainedReference = reference;
    return Ok();
}
Status setAxisOrigin(AxisConfig& c, int64_t origin, const AxisReference& e) {
    Status s = validateAxisConfig(c); if (!s) return s;
    s = evidence(c,e,true,true); if (!s) return s;
    if (!validateNativePosition(origin,c.nativeMinimum,c.nativeMaximum)) return fail(AxisError::LIMIT,"origin outside native limits");
    AxisConfig next = c; s = increment(next); if (!s) return s;
    next.originKnown = true; next.originNative = origin; next.originSource = e.source;
    next.encoderOriginKnown = false; next.encoderOriginSource = ScaleSource::UNKNOWN;
    next.softLimitsKnown = false;
    c = next; return Ok();
}
Status invalidateAxisReference(AxisConfig& c, AxisReference& ref) {
    c.originKnown = c.encoderOriginKnown = c.softLimitsKnown = false;
    c.originSource = c.encoderOriginSource = ScaleSource::UNKNOWN;
    ref.nativeKnown = ref.idle = ref.stationary = false;
    ref.configurationGeneration = 0;
    return increment(c);
}
Status preparePosition(const PositionRequest& r, const AxisConfig& c, const AxisReference* ref, PreparedTarget& output) {
    Status s = validateAxisConfig(c); if (!s) return s;
    if (r.configurationGeneration != c.generation) return fail(AxisError::STALE_GENERATION,"position configuration generation mismatch",Err::INVALID_CONFIG);
    if (!r.value.denominator || r.frame > CoordinateFrame::LOAD || r.unit > PositionUnit::MILLIMETRES ||
        r.basis > RelativeBasis::QUEUED || r.rounding > Rounding::CEIL ||
        r.path > AnglePath::SHORTEST || r.tie > HalfTurnTie::NEGATIVE ||
        !std::isfinite(r.maximumQuantizationError) || r.maximumQuantizationError < 0 ||
        (r.approximate && r.unit != PositionUnit::RADIANS))
        return fail(AxisError::INVALID_ARGUMENT,"invalid position argument");
    if (r.wrapped && (r.relative || r.frame == CoordinateFrame::NATIVE ||
        (r.unit != PositionUnit::TURNS && r.unit != PositionUnit::DEGREES && r.unit != PositionUnit::RADIANS)))
        return fail(AxisError::INVALID_ARGUMENT,"wrapped orientation requires an absolute motor/load angle");
    if (r.wrapped && (!ref || !ref->nativeKnown))
        return fail(AxisError::MISSING_REFERENCE,"wrapped orientation requires a multi-turn native reference",Err::INVALID_CONFIG);
    if (r.wrapped && ref->basis != r.basis)
        return fail(AxisError::MISSING_REFERENCE,"wrapped reference basis mismatch",Err::INVALID_CONFIG);
    if (r.relative && !(c.supportedRelativeBases & (1u << unsigned(r.basis))))
        return fail(AxisError::UNSUPPORTED_BASIS,"relative basis is not supported",Err::UNSUPPORTED);
    // Unestablished optional feedback is not a prerequisite for a native displacement.
    if (ref && !ref->nativeKnown && !(r.relative && c.softLimitsKnown)) ref = nullptr;
    if (ref) {
        s = evidence(c,*ref,false,true); if (!s) return s;
        s = effectiveBounds(ref->nativePosition,c,c.softLimitsKnown); if (!s) return s;
    }
    if (r.relative && c.softLimitsKnown && (!ref || ref->basis != r.basis))
        return fail(AxisError::MISSING_REFERENCE,"limited relative target requires matching basis evidence",Err::INVALID_CONFIG);
    if (r.relative && ref && ref->basis != r.basis)
        return fail(AxisError::MISSING_REFERENCE,"relative reference basis mismatch",Err::INVALID_CONFIG);
    int64_t origin = 0;
    if (!r.relative && r.frame != CoordinateFrame::NATIVE) {
        if (r.unit == PositionUnit::ENCODER_COUNTS) {
            if (!c.encoderOriginKnown) return fail(AxisError::MISSING_ORIGIN,"encoder command-coordinate origin is missing",Err::INVALID_CONFIG);
            origin = c.encoderOriginNative;
        } else {
            if (!c.originKnown) return fail(AxisError::MISSING_ORIGIN,"host origin is missing",Err::INVALID_CONFIG);
            origin = c.originNative;
        }
    }
    detail::UnitFactors f; s = factors(r,c,f); if (!s) return s;
    PreparedTarget next; next.requested = r; next.target = c.target; next.configurationGeneration = c.generation;
    int64_t effective = 0;
    const bool zeroRadians = r.unit == PositionUnit::RADIANS &&
        (r.rationalRadians ? r.value.numerator == 0 : r.radians == 0);
    if (r.wrapped) {
        s = r.unit == PositionUnit::RADIANS && !zeroRadians ? approximateAngle(r,c,*ref,f,next) : exactAngle(r,c,*ref,f,next);
        if (!s) return s;
        output = next; return Ok();
    }
    if (r.unit == PositionUnit::RADIANS && !zeroRadians) {
        s = approximateNative(r,f,next.requestedNative,origin,effective,next.roundingError,next.approximationErrorBound);
        if (!s) return s;
        next.exactArithmetic = false;
        // Test the complete uncertainty interval, including fractional native boundaries.
        const long double input = r.rationalRadians ? static_cast<long double>(r.value.numerator) / r.value.denominator : r.radians;
        const long double v = input * detail::approximateFactor(f);
        if (r.relative && (v - next.approximationErrorBound < c.nativeMinimum ||
                           v + next.approximationErrorBound > c.nativeMaximum))
            return fail(AxisError::LIMIT,"radian requested displacement crosses native range");
        const int64_t base = r.relative && ref ? ref->nativePosition : origin;
        if (std::fabs(static_cast<long double>(base)) > 9007199254740992.0L ||
            std::fabs(v + base) > 9007199254740992.0L)
            return fail(AxisError::ARITHMETIC_OVERFLOW,"radian endpoint exceeds binary64 precision");
        next.approximateRequestedNative = static_cast<double>(v + base);
        if (r.relative && ref) {
            next.approximationErrorBound += std::fabs(next.approximateRequestedNative) *
                (2 * std::numeric_limits<double>::epsilon());
            if (next.approximationErrorBound > r.maximumApproximationError)
                return fail(AxisError::APPROXIMATION_REQUIRED,"radian endpoint addition exceeds precision allowance");
        }
        const long double lo = v - next.approximationErrorBound + base;
        const long double hi = v + next.approximationErrorBound + base;
        if (lo < c.nativeMinimum || hi > c.nativeMaximum ||
            (c.softLimitsKnown && (lo < c.softMinimum || hi > c.softMaximum)))
            return fail(AxisError::LIMIT,"radian requested interval crosses a limit");
    } else {
        // Zero radians are exactly zero without evaluating pi or floating origin
        // arithmetic. Preserve the selected input and ordinary metadata checks.
        if (!zeroRadians) { s = exactNative(r,f,next.requestedNative); if (!s) return s; }
        if (r.relative) {
            s = requestedBounds(next.requestedNative,c,false); if (!s) return s;
        }
        if (!r.relative) {
            if (!add(next.requestedNative.integral,origin,next.requestedNative.integral))
                return fail(AxisError::ARITHMETIC_OVERFLOW,"origin addition overflows");
            normalize(next.requestedNative);
        }
        s = quantize(next.requestedNative,r.rounding,r.maximumQuantizationError,effective,next.roundingError); if (!s) return s;
    }
    if (r.relative && ref) {
        if (!add(next.requestedNative.integral,ref->nativePosition,next.requestedNative.integral))
            return fail(AxisError::ARITHMETIC_OVERFLOW,"requested endpoint addition overflows");
        normalize(next.requestedNative);
    }
    next.effectiveNative = effective;
    const bool endpointKnown = !r.relative || ref;
    s = requestedBounds(next.requestedNative,c,endpointKnown && c.softLimitsKnown); if (!s) return s;
    s = effectiveBounds(next.effectiveNative,c,false); if (!s) return s;
    next.endpointKnown = endpointKnown;
    if (endpointKnown) {
        if (r.relative) { if (!add(ref->nativePosition,next.effectiveNative,next.endpointNative)) return fail(AxisError::ARITHMETIC_OVERFLOW,"relative endpoint overflow"); }
        else next.endpointNative = next.effectiveNative;
        s = effectiveBounds(next.endpointNative,c,c.softLimitsKnown); if (!s) return s;
    }
    if (r.relative) { next.displacementKnown = true; next.displacementNative = next.effectiveNative; }
    else if (ref) {
        if (!subtract(next.effectiveNative,ref->nativePosition,next.displacementNative)) return fail(AxisError::ARITHMETIC_OVERFLOW,"absolute displacement overflow");
        next.displacementKnown = true;
    }
    next.zeroDisplacement = next.displacementKnown && next.displacementNative == 0;
    output = next; return Ok();
}
Status prepareVelocityTarget(const VelocityRequest& r, const AxisConfig& c,
                             PreparedVelocityTarget& output) noexcept {
    Status s = validateAxisConfig(c); if (!s) return s;
    if (r.configurationGeneration != c.generation)
        return fail(AxisError::STALE_GENERATION,"velocity configuration generation mismatch",Err::INVALID_CONFIG);
    if (!r.value.denominator || r.frame > CoordinateFrame::LOAD ||
        r.unit.position > PositionUnit::MILLIMETRES || r.unit.time > TimeUnit::MILLISECOND ||
        r.rounding > Rounding::CEIL || !std::isfinite(r.maximumQuantizationErrorRpm) ||
        r.maximumQuantizationErrorRpm < 0 || !std::isfinite(r.maximumApproximationErrorRpm) ||
        r.maximumApproximationErrorRpm < 0 || (r.approximate && r.unit.position != PositionUnit::RADIANS))
        return fail(AxisError::INVALID_ARGUMENT,"invalid velocity quantity");
    detail::UnitFactors f;
    if (r.frame == CoordinateFrame::NATIVE) {
        if (r.unit.position != PositionUnit::TURNS || r.unit.time != TimeUnit::MINUTE)
            return fail(AxisError::INVALID_ARGUMENT,"native velocity requires motor rpm");
    } else {
        if (r.frame == CoordinateFrame::MOTOR && r.unit.position == PositionUnit::MILLIMETRES)
            return fail(AxisError::INVALID_ARGUMENT,"linear velocity uses load frame");
        UnitConfig units = c.units;
        // Command increments need their scale. For other sources a unit command
        // scale cancels out; do not require unrelated subdivision metadata.
        if (r.unit.position != PositionUnit::STEPS)
            units.commandStepsPerMotorTurn = UnitScale(1,1,ScaleSource::DOCUMENTED);
        if (r.frame == CoordinateFrame::MOTOR &&
            (r.unit.position == PositionUnit::TURNS || r.unit.position == PositionUnit::DEGREES ||
             r.unit.position == PositionUnit::RADIANS))
            units.motorTurnsPerLoadTurn = UnitScale(1,1,ScaleSource::DOCUMENTED);
        s = unitStatus(detail::unitFactors(r.unit.position,PositionUnit::STEPS,units,f)); if (!s) return s;
        if (r.unit.position == PositionUnit::STEPS) f.sign = units.commandPolarity;
        // Convert the resulting command increments to motor turns. The factored
        // arithmetic cancels before multiplication, including time conversion.
        f.add(units.commandStepsPerMotorTurn.denominator,units.commandStepsPerMotorTurn.numerator);
        if (r.unit.time != TimeUnit::MINUTE) f.add(r.unit.time == TimeUnit::SECOND ? 60 : 60000,1);
    }
    PositionRequest quantity;
    quantity.value = r.value; quantity.unit = r.unit.position; quantity.rounding = r.rounding;
    quantity.maximumQuantizationError = r.maximumQuantizationErrorRpm;
    quantity.approximate = r.approximate; quantity.maximumApproximationError = r.maximumApproximationErrorRpm;
    PreparedVelocityTarget next; int64_t effective = 0;
    if (r.unit.position == PositionUnit::RADIANS && r.value.numerator) {
        s = approximateNative(quantity,f,next.requestedRpm,0,effective,next.roundingError,next.approximationErrorBound);
        if (!s) return s;
        next.exactArithmetic = false;
        next.approximateRequestedRpm = static_cast<double>(static_cast<long double>(r.value.numerator) /
            r.value.denominator * detail::approximateFactor(f));
        if (next.approximateRequestedRpm - next.approximationErrorBound < INT16_MIN ||
            next.approximateRequestedRpm + next.approximationErrorBound > INT16_MAX)
            return fail(AxisError::LIMIT,"requested velocity interval exceeds signed native range");
    } else {
        s = exactNative(quantity,f,next.requestedRpm); if (!s) return s;
        if (!inBounds(next.requestedRpm,INT16_MIN,INT16_MAX))
            return fail(AxisError::LIMIT,"requested velocity exceeds signed native range");
        s = quantize(next.requestedRpm,r.rounding,r.maximumQuantizationErrorRpm,effective,next.roundingError);
        if (!s) return s;
    }
    if (effective < INT16_MIN || effective > INT16_MAX)
        return fail(AxisError::LIMIT,"effective velocity exceeds signed native range");
    if (!effective) return Status(Err::ILLEGAL_VALUE,static_cast<int32_t>(VelocityError::ZERO_SPEED),
        "zero velocity is not a stop or release");
    next.nativeRpm = static_cast<int16_t>(effective); output = next; return Ok();
}
} // namespace MotorControlRS
