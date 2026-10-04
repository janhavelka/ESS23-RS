// SPDX-License-Identifier: MIT
#include "MotorControlRS/Axis.h"
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
    if (c.generation == UINT32_MAX) return fail(AxisError::GENERATION_EXHAUSTED,"configuration generation exhausted",Err::INVALID_CONFIG);
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
Status configureAxis(AxisConfig& current, const AxisConfig& candidate, const AxisReference& e) {
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
    if (!sameInterpretation(current.units,candidate.units)) {
        next.originKnown = false; next.originSource = ScaleSource::UNKNOWN;
        next.encoderOriginKnown = false; next.encoderOriginSource = ScaleSource::UNKNOWN;
        next.softLimitsKnown = false;
    }
    s = increment(next); if (!s) return s;
    current = next; return Ok();
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
Status preparePosition(const PositionRequest& r, const AxisConfig& c, const AxisReference* ref, PreparedTarget& output) {
    Status s = validateAxisConfig(c); if (!s) return s;
    if (r.configurationGeneration != c.generation) return fail(AxisError::STALE_GENERATION,"position configuration generation mismatch",Err::INVALID_CONFIG);
    if (!r.value.denominator || r.frame > CoordinateFrame::LOAD || r.unit > PositionUnit::MILLIMETRES ||
        r.basis > RelativeBasis::QUEUED || r.rounding > Rounding::CEIL ||
        !std::isfinite(r.maximumQuantizationError) || r.maximumQuantizationError < 0 ||
        (r.approximate && r.unit != PositionUnit::RADIANS))
        return fail(AxisError::INVALID_ARGUMENT,"invalid position argument");
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
} // namespace MotorControlRS
