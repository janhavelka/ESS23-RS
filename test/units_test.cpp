// SPDX-License-Identifier: MIT
#include "MotorControlRS/Units.h"
#include "MotorControlRS/profiles/ess_rs/Defaults.h"

#include <cassert>
#include <cmath>
#include <limits>

using namespace MotorControlRS;

static void near(double actual, double expected) {
    assert(std::fabs(actual - expected) <= 1e-9 * (1 + std::fabs(expected)));
}

static void assertPreserved(const UnitConversion& output) {
    assert(output.value == 123 && output.absoluteErrorBound == 456);
}

static void testScaleDependencies() {
    UnitConfig motor;
    motor.commandStepsPerMotorTurn = UnitScale(1000, 1, ScaleSource::ASSUMED);
    motor.fullStepsPerMotorTurn = UnitScale(200, 1, ScaleSource::DOCUMENTED);
    motor.encoder.countsPerUnit = UnitScale(4000, 1, ScaleSource::ASSUMED);
    motor.encoder.sourceId = 1;
    UnitConversion output;

    // Both sides describe motor rotation: gearing and linear lead are unknown.
    assert(convertDisplacement(1000, PositionUnit::STEPS, PositionUnit::FULL_STEPS, motor, output));
    near(output.value, 200);
    assert(convertDisplacement(200, PositionUnit::FULL_STEPS, PositionUnit::STEPS, motor, output));
    near(output.value, 1000);
    assert(convertDisplacement(4000, PositionUnit::ENCODER_COUNTS, PositionUnit::STEPS, motor, output));
    near(output.value, 1000);
    assert(convertDisplacement(1000, PositionUnit::STEPS, PositionUnit::ENCODER_COUNTS, motor, output));
    near(output.value, 4000);
    assert(convertDisplacement(200, PositionUnit::FULL_STEPS, PositionUnit::ENCODER_COUNTS, motor, output));
    near(output.value, 4000);

    motor.commandPolarity = -1;
    motor.encoder.polarity = -1;
    assert(convertDisplacement(1000, PositionUnit::STEPS, PositionUnit::FULL_STEPS, motor, output));
    near(output.value, -200);
    assert(convertDisplacement(1000, PositionUnit::STEPS, PositionUnit::ENCODER_COUNTS, motor, output));
    near(output.value, 4000); // Both raw count directions are reversed.
    assert(convertVelocity(1, VelocityUnit(PositionUnit::STEPS, TimeUnit::MILLISECOND),
                           VelocityUnit(PositionUnit::FULL_STEPS), motor, output));
    near(output.value, -200);
    assert(convertAcceleration(200, AccelerationUnit(PositionUnit::FULL_STEPS),
                               AccelerationUnit(PositionUnit::ENCODER_COUNTS), motor, output));
    near(output.value, -4000);

    // Crossing to load angles still requires the gear, in either direction.
    output = UnitConversion(123, 456);
    Status status = convertDisplacement(1000, PositionUnit::STEPS, PositionUnit::DEGREES, motor, output);
    assert(status.code == Err::INVALID_CONFIG && status.detail == static_cast<int32_t>(UnitError::MISSING_SCALE));
    assertPreserved(output);
    assert(!convertDisplacement(360, PositionUnit::DEGREES, PositionUnit::STEPS, motor, output));
    assertPreserved(output);
    motor.motorTurnsPerLoadTurn = UnitScale(5, 2, ScaleSource::ASSUMED);
    assert(convertDisplacement(1000, PositionUnit::STEPS, PositionUnit::DEGREES, motor, output));
    near(output.value, -144);
    output = UnitConversion(123, 456);
    assert(!convertDisplacement(1000, PositionUnit::STEPS, PositionUnit::MILLIMETRES, motor, output));
    assertPreserved(output); // Known gearing cannot replace a missing lead.
    motor.millimetresPerLoadTurn = UnitScale(8, 1, ScaleSource::ASSUMED);
    assert(convertDisplacement(1000, PositionUnit::STEPS, PositionUnit::MILLIMETRES, motor, output));
    near(output.value, -3.2);
    assert(convertDisplacement(3.2, PositionUnit::MILLIMETRES, PositionUnit::STEPS, motor, output));
    near(output.value, -1000);
    motor.motorTurnsPerLoadTurn = UnitScale();
    output = UnitConversion(123, 456);
    assert(!convertDisplacement(1000, PositionUnit::STEPS, PositionUnit::MILLIMETRES, motor, output));
    assertPreserved(output); // Known lead cannot replace a missing gear.

    UnitConfig linear;
    linear.encoder.countsPerUnit = UnitScale(100, 1, ScaleSource::ASSUMED);
    linear.encoder.basis = EncoderBasis::MILLIMETRE;
    linear.encoder.sourceId = 2;
    // A linear encoder directly reports travel, without a rotary mechanism.
    assert(convertDisplacement(1000, PositionUnit::ENCODER_COUNTS, PositionUnit::MILLIMETRES, linear, output));
    near(output.value, 10);
    assert(convertDisplacement(10, PositionUnit::MILLIMETRES, PositionUnit::ENCODER_COUNTS, linear, output));
    near(output.value, 1000);
    linear.encoder.polarity = -1;
    assert(convertVelocity(6000, VelocityUnit(PositionUnit::ENCODER_COUNTS, TimeUnit::MINUTE),
                           VelocityUnit(PositionUnit::MILLIMETRES), linear, output));
    near(output.value, -1);
    assert(convertAcceleration(6000, AccelerationUnit(PositionUnit::ENCODER_COUNTS, TimeUnit::MINUTE),
                               AccelerationUnit(PositionUnit::MILLIMETRES), linear, output));
    near(output.value, -1);
    output = UnitConversion(123, 456);
    assert(!convertDisplacement(100, PositionUnit::ENCODER_COUNTS, PositionUnit::DEGREES, linear, output));
    assertPreserved(output);
    linear.millimetresPerLoadTurn = UnitScale(8, 1, ScaleSource::ASSUMED);
    assert(convertDisplacement(800, PositionUnit::ENCODER_COUNTS, PositionUnit::TURNS, linear, output));
    near(output.value, -1);

    UnitConfig load;
    load.encoder.countsPerUnit = UnitScale(4000, 1, ScaleSource::ASSUMED);
    load.encoder.basis = EncoderBasis::LOAD_TURN;
    load.encoder.sourceId = 3;
    assert(convertDisplacement(4000, PositionUnit::ENCODER_COUNTS, PositionUnit::DEGREES, load, output));
    near(output.value, 360); // A load encoder needs neither gearing nor lead.
    assert(convertDisplacement(360, PositionUnit::DEGREES, PositionUnit::ENCODER_COUNTS, load, output));
    near(output.value, 4000);

    // Unused unknown scales are legal; malformed known scales still invalidate
    // the configuration, as they did before dependency-aware conversion.
    motor.motorTurnsPerLoadTurn.denominator = 0;
    output = UnitConversion(123, 456);
    assert(!convertDisplacement(1000, PositionUnit::STEPS, PositionUnit::FULL_STEPS, motor, output));
    assertPreserved(output);
    motor.motorTurnsPerLoadTurn = UnitScale();
    motor.fullStepsPerMotorTurn = UnitScale();
    assert(!convertDisplacement(1000, PositionUnit::STEPS, PositionUnit::FULL_STEPS, motor, output));
    assertPreserved(output); // Required full-step geometry is not optional.
}

static void testSmallResultPrecision() {
    UnitConfig config;
    config.commandStepsPerMotorTurn = UnitScale(1073741824u, 1, ScaleSource::ASSUMED);
    config.motorTurnsPerLoadTurn = UnitScale(1, 1, ScaleSource::ASSUMED);
    const double inputs[] = {1.1e-301, -1.1e-301};
    for (unsigned i = 0; i < 2; ++i) {
        UnitConversion output;
        assert(convertVelocity(inputs[i], VelocityUnit(PositionUnit::STEPS, TimeUnit::MILLISECOND),
                               VelocityUnit(PositionUnit::TURNS), config, output));
        // 1000 / 2^30 is exactly representable. Applying it in one multiplication
        // avoids the subnormal intermediate of (input / 2^30) * 1000 on targets
        // where long double has binary64 precision, such as the ESP toolchain.
        const double expected = inputs[i] * (1000.0 / 1073741824.0);
        assert(std::fpclassify(output.value) == FP_NORMAL);
        assert(output.absoluteErrorBound > 0);
        assert(std::fabs(output.value - expected) <= output.absoluteErrorBound);
    }
}

int main() {
    testScaleDependencies();
    testSmallResultPrecision();
    UnitConfig config = ESS_RS::makeBenchUnitConfig();
    assert(validateUnitConfig(config));
    assert(config.commandStepsPerMotorTurn.source == ScaleSource::ASSUMED);
    assert(config.fullStepsPerMotorTurn.source == ScaleSource::DOCUMENTED);
    assert(config.millimetresPerLoadTurn.source == ScaleSource::UNKNOWN);
    UnitConversion output;

    assert(convertDisplacement(1000, PositionUnit::STEPS, PositionUnit::DEGREES, config, output));
    near(output.value, 360);
    assert(output.absoluteErrorBound > 0 && output.absoluteErrorBound < config.maxConversionError);
    assert(convertDisplacement(-720, PositionUnit::DEGREES, PositionUnit::STEPS, config, output));
    near(output.value, -2000); // No modulo/wrapped-angle conversion.
    assert(convertDisplacement(200, PositionUnit::FULL_STEPS, PositionUnit::STEPS, config, output));
    near(output.value, 1000);
    assert(convertDisplacement(4000, PositionUnit::ENCODER_COUNTS, PositionUnit::STEPS, config, output));
    near(output.value, 1000);
    assert(convertDisplacement(3.14159265358979323846, PositionUnit::RADIANS, PositionUnit::DEGREES, config, output));
    near(output.value, 180);

    config.motorTurnsPerLoadTurn = UnitScale(5, 2, ScaleSource::ASSUMED);
    config.commandStepsPerMotorTurn = UnitScale(3200, 1, ScaleSource::ASSUMED);
    config.millimetresPerLoadTurn = UnitScale(8, 1, ScaleSource::ASSUMED);
    assert(convertDisplacement(90, PositionUnit::DEGREES, PositionUnit::STEPS, config, output));
    near(output.value, 2000);
    assert(convertDisplacement(2, PositionUnit::MILLIMETRES, PositionUnit::STEPS, config, output));
    near(output.value, 2000);
    assert(convertDisplacement(200, PositionUnit::FULL_STEPS, PositionUnit::TURNS, config, output));
    near(output.value, 0.4); // Full motor turn is 2/5 of a load turn.
    config.commandPolarity = -1;
    assert(convertDisplacement(2, PositionUnit::MILLIMETRES, PositionUnit::STEPS, config, output));
    near(output.value, -2000);

    config.encoder.basis = EncoderBasis::LOAD_TURN;
    config.encoder.countsPerUnit = UnitScale(800, 1, ScaleSource::ASSUMED);
    assert(convertDisplacement(800, PositionUnit::ENCODER_COUNTS, PositionUnit::TURNS, config, output));
    near(output.value, 1); // A load-mounted encoder must not acquire another gear factor.
    config.encoder.basis = EncoderBasis::MILLIMETRE;
    config.encoder.countsPerUnit = UnitScale(100, 1, ScaleSource::ASSUMED);
    config.encoder.polarity = -1;
    assert(convertDisplacement(800, PositionUnit::ENCODER_COUNTS, PositionUnit::TURNS, config, output));
    near(output.value, -1);

    config = ESS_RS::makeBenchUnitConfig();
    const VelocityUnit rpm(PositionUnit::TURNS, TimeUnit::MINUTE);
    const VelocityUnit degreesPerSecond(PositionUnit::DEGREES, TimeUnit::SECOND);
    assert(convertVelocity(60, rpm, degreesPerSecond, config, output));
    near(output.value, 360);
    assert(convertVelocity(-1, VelocityUnit(PositionUnit::STEPS, TimeUnit::MILLISECOND),
                           VelocityUnit(PositionUnit::STEPS, TimeUnit::SECOND), config, output));
    near(output.value, -1000);
    const AccelerationUnit degreesPerSecondSquared(PositionUnit::DEGREES);
    const AccelerationUnit radiansPerSecondSquared(PositionUnit::RADIANS);
    const AccelerationUnit stepsPerSecondSquared(PositionUnit::STEPS);
    assert(convertAcceleration(360, degreesPerSecondSquared, stepsPerSecondSquared, config, output));
    near(output.value, 1000);
    assert(convertAcceleration(6.28318530717958647692, radiansPerSecondSquared,
                               stepsPerSecondSquared, config, output));
    near(output.value, 1000);
    assert(convertAcceleration(60, AccelerationUnit(PositionUnit::TURNS, TimeUnit::MINUTE, TimeUnit::SECOND),
                               stepsPerSecondSquared, config, output));
    near(output.value, 1000); // rpm/s divides by 60, not 3600.
    assert(convertAcceleration(3600, AccelerationUnit(PositionUnit::TURNS, TimeUnit::MINUTE, TimeUnit::MINUTE),
                               stepsPerSecondSquared, config, output));
    near(output.value, 1000);
    assert(convertAcceleration(-0.001, AccelerationUnit(PositionUnit::STEPS, TimeUnit::MILLISECOND, TimeUnit::MILLISECOND),
                               stepsPerSecondSquared, config, output));
    near(output.value, -1000);

    config.settings.position = PositionUnit::DEGREES;
    config.settings.velocity = rpm;
    config.settings.acceleration = radiansPerSecondSquared;
    assert(validateUnitConfig(config));
    assert(convertAcceleration(6.28318530717958647692, config.settings.acceleration,
                               stepsPerSecondSquared, config, output));
    near(output.value, 1000);

    output = UnitConversion(123, 456);
    assert(!convertDisplacement(1, PositionUnit::MILLIMETRES, PositionUnit::STEPS, config, output));
    assertPreserved(output);
    assert(!convertDisplacement(std::numeric_limits<double>::infinity(), PositionUnit::STEPS,
                                PositionUnit::DEGREES, config, output));
    assertPreserved(output);
    assert(!convertVelocity(std::numeric_limits<double>::quiet_NaN(), rpm, degreesPerSecond, config, output));
    assertPreserved(output);
    assert(!convertDisplacement(1e16, PositionUnit::TURNS, PositionUnit::STEPS, config, output));
    assertPreserved(output);
    assert(!convertDisplacement(1e13, PositionUnit::TURNS, PositionUnit::STEPS, config, output));
    assertPreserved(output);
    assert(!convertDisplacement(std::numeric_limits<double>::min(), PositionUnit::STEPS,
                                PositionUnit::TURNS, config, output));
    assertPreserved(output);
    assert(!convertAcceleration(1, AccelerationUnit(PositionUnit::STEPS, static_cast<TimeUnit>(99)),
                               stepsPerSecondSquared, config, output));
    assertPreserved(output);
    assert(!convertDisplacement(1, static_cast<PositionUnit>(99), PositionUnit::STEPS, config, output));
    assertPreserved(output);
    config.maxConversionError = 1e-20;
    assert(!convertDisplacement(1, PositionUnit::TURNS, PositionUnit::STEPS, config, output));
    assertPreserved(output);
    config.maxConversionError = 0;
    assert(!validateUnitConfig(config));
    config = ESS_RS::makeBenchUnitConfig();
    config.commandStepsPerMotorTurn.denominator = 0;
    assert(!validateUnitConfig(config));
    assert(!convertDisplacement(1, PositionUnit::TURNS, PositionUnit::STEPS, config, output));
    assertPreserved(output);
    config = ESS_RS::makeBenchUnitConfig();
    config.commandStepsPerMotorTurn.numerator = 0;
    assert(!validateUnitConfig(config));
    config = ESS_RS::makeBenchUnitConfig();
    config.commandPolarity = 0;
    assert(!validateUnitConfig(config));
    config = ESS_RS::makeBenchUnitConfig();
    config.encoder.sourceId = 0;
    assert(!validateUnitConfig(config));
    config = UnitConfig();
    assert(validateUnitConfig(config));
    assert(convertDisplacement(360, PositionUnit::DEGREES, PositionUnit::TURNS, config, output));
    near(output.value, 1); // Pure angles need no motor scale.
    assert(!convertDisplacement(1, PositionUnit::DEGREES, PositionUnit::STEPS, config, output));

    int32_t native = 42;
    assert(narrowNativePosition(std::numeric_limits<int32_t>::min(), native));
    assert(native == std::numeric_limits<int32_t>::min());
    assert(narrowNativePosition(std::numeric_limits<int32_t>::max(), native));
    assert(native == std::numeric_limits<int32_t>::max());
    assert(!narrowNativePosition(static_cast<int64_t>(std::numeric_limits<int32_t>::max()) + 1, native));
    assert(native == std::numeric_limits<int32_t>::max());
    const int64_t beyondDoubleInteger = INT64_C(9007199254740993);
    assert(validateNativePosition(beyondDoubleInteger, beyondDoubleInteger, beyondDoubleInteger));
    assert(!validateNativePosition(beyondDoubleInteger, 0, beyondDoubleInteger - 1));
    assert(!narrowNativePosition(beyondDoubleInteger, native));
    assert(native == std::numeric_limits<int32_t>::max());
    assert(validateNativePosition(std::numeric_limits<int64_t>::min(),
                                  std::numeric_limits<int64_t>::min(), std::numeric_limits<int64_t>::max()));
    assert(!validateNativePosition(0, 1, -1));
    return 0;
}
