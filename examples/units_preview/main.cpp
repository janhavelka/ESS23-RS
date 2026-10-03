// SPDX-License-Identifier: MIT
// The same pure-library preview runs on a desktop and on the E2 USB console.
#include <MotorControlRS/MotorControlRS.h>
#include <MotorControlRS/profiles/ess_rs/Defaults.h>
#include <MotorControlRS/profiles/ess_rs/Registers.h>

#ifdef ARDUINO
#include <Arduino.h>
#include "../common/BoardPins.h"
#include "../common/BuildConfig.h"
#define PREVIEW_PRINTF Serial.printf
#else
#include <cstdio>
#define PREVIEW_PRINTF std::printf
#endif

static int runPreview() {
    using namespace MotorControlRS;
    UnitConfig config = ESS_RS::makeBenchUnitConfig();
    config.settings.position = PositionUnit::DEGREES;
    config.settings.velocity = VelocityUnit(PositionUnit::TURNS, TimeUnit::MINUTE);
    config.settings.acceleration = AccelerationUnit(PositionUnit::DEGREES);

    UnitConversion position, speed, acceleration;
    const Status p = convertDisplacement(90, config.settings.position,
                                         PositionUnit::STEPS, config, position);
    const Status v = convertVelocity(6, config.settings.velocity,
                                    VelocityUnit(PositionUnit::STEPS), config, speed);
    const Status a = convertAcceleration(360, config.settings.acceleration,
                                        AccelerationUnit(PositionUnit::RADIANS),
                                        config, acceleration);
    if (!p || !v || !a) {
        PREVIEW_PRINTF("Conversion failed: position=%s velocity=%s acceleration=%s\n",
                       errToString(p.code), errToString(v.code), errToString(a.code));
        return 1;
    }
    PREVIEW_PRINTF("MotorControl-RS %s: unit preview; no motor communication\n", VERSION);
    PREVIEW_PRINTF("Assumptions: 1000 command steps/motor turn, direct coupling\n");
    PREVIEW_PRINTF("90 degrees = %.6f command steps\n", position.value);
    PREVIEW_PRINTF("6 rpm = %.6f command steps/s\n", speed.value);
    PREVIEW_PRINTF("360 degrees/s^2 = %.9f radians/s^2\n", acceleration.value);
    PREVIEW_PRINTF("These are host conversions; the ESS ramp register is not yet mapped.\n");
    const ESS_RS::RegisterDescriptor* subdivision =
        ESS_RS::findRegister(ESS_RS::Registers::SUBDIVISION);
    if (!subdivision) return 2;
    PREVIEW_PRINTF("ESS catalogue: %u records; subdivision source default: %s\n",
                   static_cast<unsigned>(ESS_RS::registerCount()), subdivision->sourceDefault);
    return 0;
}

#ifdef ARDUINO
void setup() {
    Serial.begin(MotorControlRSExample::kConsoleBaud);
    // Bound console attachment waiting; do not wait forever without a host.
    const uint32_t started = millis();
    while (!Serial && millis() - started < 2000) delay(10);
    PREVIEW_PRINTF("E2 HW2.0 pins: TX%d RX%d DE%d (not initialized)\n",
                   MotorControlRSExample::Board::kRs485TxPin,
                   MotorControlRSExample::Board::kRs485RxPin,
                   MotorControlRSExample::Board::kRs485DeRePin);
    runPreview();
}
void loop() { delay(1000); }
#else
int main() { return runPreview(); }
#endif

#undef PREVIEW_PRINTF
