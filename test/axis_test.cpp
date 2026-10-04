// SPDX-License-Identifier: MIT
#include "MotorControlRS/Axis.h"
#include <cassert>
#include <cmath>
#include <initializer_list>
#include <limits>
using namespace MotorControlRS;
static AxisConfig config() {
    AxisConfig c; c.target.id = 1; c.target.address = 1; c.target.generation = 1;
    c.supportedRelativeBases = 7;
    return c;
}
static AxisReference reference(const AxisConfig& c, int64_t position = 0) {
    AxisReference r; r.target = c.target; r.configurationGeneration = c.generation;
    r.idle = r.stationary = r.nativeKnown = true; r.nativePosition = position;
    r.source = ScaleSource::QUALIFIED; r.observedUs = 100; r.nowUs = 110; r.maximumAgeUs = 20;
    return r;
}
static PositionRequest request(const AxisConfig& c, int64_t n, uint64_t d = 1) {
    PositionRequest r; r.value = Rational(n,d); r.configurationGeneration = c.generation;
    return r;
}
static void testNative() {
    AxisConfig c = config(); PreparedTarget out;
    assert(preparePosition(request(c,INT64_MIN),c,nullptr,out)); assert(out.effectiveNative == INT64_MIN);
    assert(preparePosition(request(c,INT64_MAX),c,nullptr,out)); assert(out.effectiveNative == INT64_MAX);
    assert(out.requested.value.numerator == INT64_MAX && !out.endpointKnown);
    PositionRequest r = request(c,INT64_C(9007199254740993));
    assert(preparePosition(r,c,nullptr,out)); assert(out.effectiveNative == INT64_C(9007199254740993));
    r.relative = false;
    assert(preparePosition(r,c,nullptr,out)); assert(out.endpointKnown && out.endpointNative == r.value.numerator);
    r.configurationGeneration++; out.effectiveNative = 71;
    assert(!preparePosition(r,c,nullptr,out)); assert(out.effectiveNative == 71);
    r.configurationGeneration--; r.relative = true; c.supportedRelativeBases = 0;
    assert(preparePosition(r,c,nullptr,out).code == Err::UNSUPPORTED);
    c.supportedRelativeBases = 7; r.basis = RelativeBasis::COMMANDED;
    assert(preparePosition(r,c,nullptr,out)); // No unrelated origin/gear/current observation.
    AxisReference unresolved = reference(c); unresolved.nativeKnown = false;
    assert(preparePosition(r,c,&unresolved,out)); assert(!out.endpointKnown);
    AxisReference ref = reference(c); ref.configurationGeneration++;
    assert(!preparePosition(r,c,&ref,out));
}
static void testRounding() {
    AxisConfig c = config(); PreparedTarget out;
    const Rounding modes[] = { Rounding::NEAREST,Rounding::TOWARD_ZERO,Rounding::FLOOR,Rounding::CEIL };
    const int64_t positive[] = { 2,1,1,2 }, negative[] = { -2,-1,-2,-1 };
    for (unsigned i = 0; i < 4; ++i) {
        PositionRequest r = request(c,3,2); r.rounding = modes[i]; r.maximumQuantizationError = 1;
        assert(preparePosition(r,c,nullptr,out)); assert(out.effectiveNative == positive[i]);
        assert(std::fabs(out.roundingError - (positive[i] - 1.5)) < 1e-12);
        r.value.numerator = -3;
        assert(preparePosition(r,c,nullptr,out)); assert(out.effectiveNative == negative[i]);
        assert(std::fabs(out.roundingError - (negative[i] + 1.5)) < 1e-12);
    }
    PositionRequest r = request(c,1,2); assert(!preparePosition(r,c,nullptr,out));
    r.rounding = Rounding::NEAREST; r.maximumQuantizationError = 0.5;
    assert(preparePosition(r,c,nullptr,out)); assert(out.zeroDisplacement && out.effectiveNative == 0);
    r.value = Rational(5,2); assert(preparePosition(r,c,nullptr,out)); assert(out.effectiveNative == 2);
    r.value = Rational(-5,2); assert(preparePosition(r,c,nullptr,out)); assert(out.effectiveNative == -2);
    r.maximumQuantizationError = 0.49; out.effectiveNative = 71;
    assert(!preparePosition(r,c,nullptr,out)); assert(out.effectiveNative == 71);
    c.originKnown = true; c.originNative = 1; c.originSource = ScaleSource::ASSUMED;
    r.relative = false; r.frame = CoordinateFrame::MOTOR; r.value = Rational(1,2); r.maximumQuantizationError = 0.5;
    assert(preparePosition(r,c,nullptr,out)); assert(out.effectiveNative == 2); // Nearest-even applies to final absolute1.5.
    assert(out.requestedNative.integral == 1 && out.requestedNative.numerator == 1);
    c.originNative = 2; r.value = Rational(-1,2); r.rounding = Rounding::TOWARD_ZERO;
    assert(preparePosition(r,c,nullptr,out)); assert(out.effectiveNative == 1);
    c.originNative = -1; r.value = Rational(-1,2); r.rounding = Rounding::NEAREST;
    assert(preparePosition(r,c,nullptr,out)); assert(out.effectiveNative == -2);
}
static void testFactorsAndOrigins() {
    AxisConfig c = config(); PreparedTarget out;
    c.units.commandStepsPerMotorTurn = UnitScale(3200,1,ScaleSource::ASSUMED);
    c.units.fullStepsPerMotorTurn = UnitScale(200,1,ScaleSource::DOCUMENTED);
    PositionRequest r = request(c,90); r.unit = PositionUnit::DEGREES; r.frame = CoordinateFrame::MOTOR;
    assert(preparePosition(r,c,nullptr,out)); assert(out.effectiveNative == 800);
    r.frame = CoordinateFrame::LOAD;
    Status missing = preparePosition(r,c,nullptr,out);
    assert(missing.code == Err::INVALID_CONFIG && missing.detail == 100 + static_cast<int32_t>(UnitError::MISSING_SCALE));
    c.units.motorTurnsPerLoadTurn = UnitScale(5,2,ScaleSource::ASSUMED);
    assert(preparePosition(r,c,nullptr,out)); assert(out.effectiveNative == 2000);
    r.unit = PositionUnit::FULL_STEPS; r.value = Rational(200); c.units.motorTurnsPerLoadTurn = UnitScale();
    assert(preparePosition(r,c,nullptr,out)); assert(out.effectiveNative == 3200);
    c.units.commandPolarity = -1; assert(preparePosition(r,c,nullptr,out)); assert(out.effectiveNative == -3200);
    PositionRequest steps = request(c,1); steps.frame = CoordinateFrame::MOTOR;
    assert(preparePosition(steps,c,nullptr,out)); assert(out.effectiveNative == -1);
    steps.frame = CoordinateFrame::NATIVE; assert(preparePosition(steps,c,nullptr,out)); assert(out.effectiveNative == 1);
    c.units.commandPolarity = 1;
    // All large factors cancel before products: no gearing required for full steps.
    c.units.commandStepsPerMotorTurn = UnitScale(UINT32_MAX,UINT32_MAX-1,ScaleSource::ASSUMED);
    c.units.fullStepsPerMotorTurn = c.units.commandStepsPerMotorTurn;
    r.value = Rational(INT64_MAX);
    assert(preparePosition(r,c,nullptr,out)); assert(out.effectiveNative == INT64_MAX);
    c.units.commandStepsPerMotorTurn = UnitScale(UINT32_MAX,1,ScaleSource::ASSUMED);
    c.units.fullStepsPerMotorTurn = UnitScale(1,1,ScaleSource::ASSUMED);
    assert(!preparePosition(r,c,nullptr,out));
    r = request(c,1); r.relative = false; r.frame = CoordinateFrame::LOAD;
    assert(!preparePosition(r,c,nullptr,out));
    c.originKnown = true; c.originSource = ScaleSource::QUALIFIED; c.originNative = INT64_MAX;
    assert(!preparePosition(r,c,nullptr,out));
    r.value.numerator = -1; assert(preparePosition(r,c,nullptr,out)); assert(out.effectiveNative == INT64_MAX-1);
    r.value = Rational(-1,2); r.rounding = Rounding::NEAREST; r.maximumQuantizationError = 0.5;
    assert(preparePosition(r,c,nullptr,out)); assert(out.effectiveNative == INT64_MAX-1);
    c.originNative = INT64_MIN; r.value = Rational(1,2);
    assert(preparePosition(r,c,nullptr,out)); assert(out.effectiveNative == INT64_MIN);
    r.value = Rational(-1); assert(!preparePosition(r,c,nullptr,out));
    c = config(); c.units.commandPolarity = -1; r = request(c,INT64_MIN); r.frame = CoordinateFrame::MOTOR;
    assert(!preparePosition(r,c,nullptr,out)); // Sign reversal cannot silently wrap signed minimum.
    r.frame = CoordinateFrame::NATIVE; assert(preparePosition(r,c,nullptr,out)); assert(out.effectiveNative == INT64_MIN);
}
static void testEncodersAndLimits() {
    AxisConfig c = config(); PreparedTarget out;
    c.units.commandStepsPerMotorTurn = UnitScale(1000,1,ScaleSource::ASSUMED);
    c.units.encoder.countsPerUnit = UnitScale(100,1,ScaleSource::ASSUMED);
    c.units.encoder.sourceId = 7; c.units.encoder.basis = EncoderBasis::MILLIMETRE;
    PositionRequest r = request(c,100); r.unit = PositionUnit::ENCODER_COUNTS; r.frame = CoordinateFrame::LOAD;
    assert(!preparePosition(r,c,nullptr,out));
    c.units.millimetresPerLoadTurn = UnitScale(8,1,ScaleSource::ASSUMED);
    assert(!preparePosition(r,c,nullptr,out));
    c.units.motorTurnsPerLoadTurn = UnitScale(1,1,ScaleSource::ASSUMED);
    assert(preparePosition(r,c,nullptr,out)); assert(out.effectiveNative == 125);
    r.relative = false; c.originKnown = true; c.originSource = ScaleSource::ASSUMED;
    assert(!preparePosition(r,c,nullptr,out)); // A host command origin is not the encoder's zero.
    c.encoderOriginKnown = true; c.encoderOriginSource = ScaleSource::QUALIFIED; c.encoderOriginNative = 10;
    assert(preparePosition(r,c,nullptr,out)); assert(out.effectiveNative == 135);
    c.units.encoder.sourceId = 0; assert(!preparePosition(r,c,nullptr,out));
    c = config(); c.softLimitsKnown = true; c.softMinimum = -10; c.softMaximum = 10;
    r = request(c,1); assert(!preparePosition(r,c,nullptr,out));
    AxisReference ref = reference(c,9); assert(preparePosition(r,c,&ref,out)); assert(out.endpointNative == 10);
    r.value = Rational(3,2); r.rounding = Rounding::TOWARD_ZERO; r.maximumQuantizationError = 0.5;
    assert(!preparePosition(r,c,&ref,out)); // Requested endpoint10.5 fails even rounded10 is legal.
    r.relative = false; r.frame = CoordinateFrame::NATIVE; r.value = Rational(19,2);
    r.rounding = Rounding::CEIL; assert(preparePosition(r,c,nullptr,out)); assert(out.effectiveNative == 10);
    c.nativeMinimum = -2; c.nativeMaximum = 2; c.softLimitsKnown = false; r.value = Rational(5,2);
    assert(!preparePosition(r,c,nullptr,out)); // Cannot use rounding to evade native range.
    c = config(); c.softLimitsKnown = true; c.softMinimum = -10; c.softMaximum = 10;
    r = request(c,-2); ref = reference(c,11); assert(!preparePosition(r,c,&ref,out)); // Entire path includes its start.
    c = config(); c.softLimitsKnown = true; c.softMinimum = 0; c.softMaximum = 0;
    r = request(c,1,UINT64_MAX); r.rounding = Rounding::TOWARD_ZERO; r.maximumQuantizationError = 1;
    ref = reference(c); assert(!preparePosition(r,c,&ref,out)); // Tiny positive requested endpoint outside zero-onlylimit.
}
static void testHostConfiguration() {
    AxisConfig c = config(); AxisReference e = reference(c,17);
    AxisConfig candidate = c; candidate.units.commandPolarity = -1;
    e.stationary = false; assert(!configureAxis(c,candidate,e)); assert(c.generation == 1);
    e.stationary = true; e.nowUs = 121; assert(!configureAxis(c,candidate,e));
    e.nowUs = 110; assert(configureAxis(c,candidate,e)); assert(c.generation == 2);
    assert(!setAxisOrigin(c,17,e)); e = reference(c,17); e.nativeKnown = false;
    assert(!setAxisOrigin(c,17,e)); e.nativeKnown = true;
    assert(setAxisOrigin(c,17,e)); assert(c.generation == 3 && c.originKnown && c.originNative == 17);
    e = reference(c); candidate = c; candidate.originNative = 9;
    assert(!configureAxis(c,candidate,e));
    candidate = c; candidate.softLimitsKnown = true; candidate.softMinimum = -100; candidate.softMaximum = 100;
    e.nativeKnown = false; assert(!configureAxis(c,candidate,e)); e.nativeKnown = true;
    assert(configureAxis(c,candidate,e)); assert(c.generation == 4 && c.originKnown && c.softLimitsKnown);
    e = reference(c); candidate = c; candidate.units.commandPolarity = 1;
    assert(configureAxis(c,candidate,e)); assert(!c.originKnown && !c.softLimitsKnown && c.generation == 5);
    e = reference(c); candidate = c; candidate.units.settings.position = PositionUnit::DEGREES;
    candidate.units.settings.velocity = VelocityUnit(PositionUnit::TURNS,TimeUnit::MINUTE);
    candidate.units.settings.acceleration = AccelerationUnit(PositionUnit::TURNS,TimeUnit::MINUTE,TimeUnit::SECOND);
    assert(configureAxis(c,candidate,e));
    assert(c.units.settings.position == PositionUnit::DEGREES && c.units.settings.velocity.time == TimeUnit::MINUTE && c.units.settings.acceleration.accelerationTime == TimeUnit::SECOND);
    c.generation = UINT32_MAX; candidate = c; e = reference(c);
    assert(!configureAxis(c,candidate,e)); assert(c.generation == UINT32_MAX);
    c = config(); c.units.commandPolarity = 0;
    assert(validateAxisConfig(c).detail == 100 + static_cast<int32_t>(UnitError::INVALID_POLARITY));
    c = config(); c.nativeMinimum = -10; c.nativeMaximum = 10;
    c.originKnown = true; c.originSource = ScaleSource::QUALIFIED; c.originNative = 11;
    assert(!validateAxisConfig(c));
}
static void testRelativeRequestedRange() {
    AxisConfig c = config(); c.nativeMinimum = -2; c.nativeMaximum = 2;
    PreparedTarget out; out.effectiveNative = 71;
    PositionRequest r = request(c,5,2); r.rounding = Rounding::TOWARD_ZERO;
    r.maximumQuantizationError = 0.5;
    for (int sign : { -1,1 }) {
        r.value.numerator = sign * 5;
        AxisReference ref = reference(c,-sign);
        assert(preparePosition(r,c,nullptr,out).detail == static_cast<int32_t>(AxisError::LIMIT));
        assert(preparePosition(r,c,&ref,out).detail == static_cast<int32_t>(AxisError::LIMIT));
        assert(out.effectiveNative == 71); // A legal rounded delta/endpoint cannot hide an illegal requested delta.
    }
    c.units.commandStepsPerMotorTurn = UnitScale(1,1,ScaleSource::ASSUMED);
    r.unit = PositionUnit::RADIANS; r.frame = CoordinateFrame::MOTOR;
    r.approximate = true; r.rationalRadians = false;
    r.maximumApproximationError = 1e-10; r.maximumQuantizationError = 0.6;
    for (int sign : { -1,1 }) {
        r.radians = sign * 2.5 * 6.28318530717958647692;
        AxisReference ref = reference(c,-sign);
        assert(preparePosition(r,c,&ref,out).detail == static_cast<int32_t>(AxisError::LIMIT));
        assert(out.effectiveNative == 71);
    }
}
static void testZeroRadiansAndCancellation() {
    AxisConfig c = config(); c.units.commandStepsPerMotorTurn = UnitScale(1,1,ScaleSource::ASSUMED);
    c.originKnown = true; c.originSource = ScaleSource::QUALIFIED;
    PositionRequest r = request(c,0); r.unit = PositionUnit::RADIANS;
    r.frame = CoordinateFrame::MOTOR; r.relative = false;
    PreparedTarget out;
    for (int64_t origin : { INT64_MIN,INT64_C(17),INT64_MAX }) {
        c.originNative = origin;
        for (Rounding rounding : { Rounding::EXACT,Rounding::NEAREST,Rounding::TOWARD_ZERO,Rounding::FLOOR,Rounding::CEIL }) {
            r.rounding = rounding;
            assert(preparePosition(r,c,nullptr,out));
            assert(out.effectiveNative == origin && out.requestedNative.integral == origin);
            assert(out.exactArithmetic && out.roundingError == 0 && out.approximationErrorBound == 0);
        }
    }
    r.relative = true; r.rounding = Rounding::EXACT;
    c.softLimitsKnown = true; c.softMinimum = c.softMaximum = INT64_MAX;
    AxisReference ref = reference(c,INT64_MAX);
    assert(preparePosition(r,c,&ref,out));
    assert(out.endpointNative == INT64_MAX && out.zeroDisplacement && out.approximationErrorBound == 0);
    // The selected binary64 input is retained and need not match the unused rational field.
    r.rationalRadians = false; r.radians = -0.0; r.value = Rational(1);
    assert(preparePosition(r,c,&ref,out));
    assert(out.zeroDisplacement && out.requested.value.numerator == 1 && out.exactArithmetic);
    AxisConfig missing = c; missing.units.commandStepsPerMotorTurn = UnitScale();
    assert(!preparePosition(r,missing,&ref,out)); // Zero does not waive the selected frame's scale contract.

    c.softLimitsKnown = false; c.originNative = 1;
    r.relative = false; r.approximate = true; r.radians = -6.28318530717958647692;
    r.rounding = Rounding::NEAREST; r.maximumApproximationError = 1e-10; r.maximumQuantizationError = 1e-10;
    assert(preparePosition(r,c,nullptr,out)); // Normal conversion cancelling an origin is not underflow.
    assert(out.effectiveNative == 0 && !out.exactArithmetic && out.approximationErrorBound > 0);
    r.relative = true; r.radians = std::numeric_limits<double>::min(); out.effectiveNative = 71;
    assert(!preparePosition(r,c,nullptr,out)); // Actual converted subnormal values still reject.
    assert(out.effectiveNative == 71);
}
static void testRadiansAndParsing() {
    AxisConfig c = config(); c.units.commandStepsPerMotorTurn = UnitScale(1000,1,ScaleSource::ASSUMED);
    PositionRequest r = request(c,1); r.frame = CoordinateFrame::MOTOR; r.unit = PositionUnit::RADIANS;
    PreparedTarget out; assert(!preparePosition(r,c,nullptr,out));
    PositionRequest badAllowance = request(c,1); badAllowance.maximumQuantizationError = std::numeric_limits<double>::infinity();
    assert(!preparePosition(badAllowance,c,nullptr,out));
    badAllowance.maximumQuantizationError = std::numeric_limits<double>::quiet_NaN(); assert(!preparePosition(badAllowance,c,nullptr,out));
    r.approximate = true; r.maximumApproximationError = 1e-9; r.maximumQuantizationError = 1; r.rounding = Rounding::NEAREST;
    assert(preparePosition(r,c,nullptr,out)); assert(out.effectiveNative == 159 && !out.exactArithmetic && out.approximationErrorBound > 0);
    r.rationalRadians = false; r.radians = std::numeric_limits<double>::quiet_NaN(); assert(!preparePosition(r,c,nullptr,out));
    r.radians = std::numeric_limits<double>::infinity(); assert(!preparePosition(r,c,nullptr,out));
    r.radians = std::numeric_limits<double>::denorm_min(); assert(!preparePosition(r,c,nullptr,out));
    r.radians = 1e300; assert(!preparePosition(r,c,nullptr,out));
    r.radians = 0.5 * 6.28318530717958647692 / 1000;
    assert(!preparePosition(r,c,nullptr,out)); // Native half-count interval is ambiguous.
    r.radians = 1; r.rounding = Rounding::EXACT; assert(!preparePosition(r,c,nullptr,out));
    r.radians = 0.0001 * 6.28318530717958647692 / 1000; r.rounding = Rounding::TOWARD_ZERO;
    c.softLimitsKnown = true; c.softMinimum = 0; c.softMaximum = INT64_C(9007199254740992);
    AxisReference large = reference(c,c.softMaximum);
    assert(!preparePosition(r,c,&large,out)); // Tiny displacement at binary64-large endpoint cannot hide in rounding.
    c.softLimitsKnown = false;
    Rational number(71,3); assert(parseExactNumber("-9223372036854775808",number)); assert(number.numerator == INT64_MIN);
    assert(parseExactNumber("9223372036854775807",number)); assert(number.numerator == INT64_MAX);
    assert(parseExactNumber("9223372036854775807.0",number)); assert(number.numerator == INT64_MAX && number.denominator == 1);
    assert(parseExactNumber("-9223372036854775808.000",number)); assert(number.numerator == INT64_MIN && number.denominator == 1);
    assert(parseExactNumber("18446744073709551615/18446744073709551615",number)); assert(number.numerator == 1 && number.denominator == 1);
    assert(parseExactNumber("-1.250",number)); assert(number.numerator == -5 && number.denominator == 4);
    const char* bad[] = {"", "1e3", "NaN", "inf", "1/0", "1.", ".5", " 1", "1 ", "9223372036854775808", "-9223372036854775809", "1/-2", "--1", "1.2.3", "1/2.0", "1/2.000", "1.0/2", "1.00/2.000"};
    for (unsigned i = 0; i < sizeof(bad)/sizeof(bad[0]); ++i) { number = Rational(71,3); assert(!parseExactNumber(bad[i],number)); assert(number.numerator == 71 && number.denominator == 3); }
    char longText[130]; for (unsigned i = 0; i < 129; ++i) longText[i] = '1'; longText[129] = 0;
    assert(!parseExactNumber(longText,number));
    double allowance = 71; assert(rationalToDouble(Rational(1,2),allowance)); assert(allowance == 0.5);
    assert(rationalToDouble(Rational(1,3),allowance)); assert(allowance == 1.0/3);
    c = config(); r = request(c,1,3); r.rounding = Rounding::TOWARD_ZERO; r.maximumQuantizationError = 1.0/3;
    assert(!preparePosition(r,c,nullptr,out));
    r.maximumQuantizationError = 1.0/3 + 1e-14; assert(preparePosition(r,c,nullptr,out));
    r.value = Rational(INT64_C(8738681121152269348),UINT64_C(10439786832528708783));
    r.maximumQuantizationError = 0.8370555128505052; assert(!preparePosition(r,c,nullptr,out));
    assert(rationalToDouble(Rational(INT64_C(4730525896183341671),UINT64_C(13991455622818038268)),allowance));
    assert(allowance < 0.33810105422258846);
    r.value = Rational(INT64_C(9007199254740993),UINT64_C(18014398509481984)); r.maximumQuantizationError = 0.5;
    assert(!preparePosition(r,c,nullptr,out)); // Power-of-two denominator alone does not imply binary64 exactness.

}
int main() {
    testNative(); testRounding(); testFactorsAndOrigins(); testEncodersAndLimits(); testHostConfiguration();
    testRelativeRequestedRange(); testZeroRadiansAndCancellation(); testRadiansAndParsing();
    return 0;
}
