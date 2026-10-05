// SPDX-License-Identifier: MIT
// Every public core header is compiled through the component's exported include
// path. No example, Arduino, motor transport or vendor resource is available.
#include <MotorControlRS/ActionOperation.h>
#include <MotorControlRS/Axis.h>
#include <MotorControlRS/Discovery.h>
#include <MotorControlRS/MotorControlRS.h>
#include <MotorControlRS/MoveOperation.h>
#include <MotorControlRS/ReadOperation.h>
#include <MotorControlRS/Status.h>
#include <MotorControlRS/Traffic.h>
#include <MotorControlRS/Units.h>
#include <MotorControlRS/VelocityOperation.h>
#include <MotorControlRS/Version.h>
#include <MotorControlRS/profiles/ess_rs/Actions.h>
#include <MotorControlRS/profiles/ess_rs/Codec.h>
#include <MotorControlRS/profiles/ess_rs/Communication.h>
#include <MotorControlRS/profiles/ess_rs/ControlSettings.h>
#include <MotorControlRS/profiles/ess_rs/Defaults.h>
#include <MotorControlRS/profiles/ess_rs/DriverSettings.h>
#include <MotorControlRS/profiles/ess_rs/Homing.h>
#include <MotorControlRS/profiles/ess_rs/Persistence.h>
#include <MotorControlRS/profiles/ess_rs/Position.h>
#include <MotorControlRS/profiles/ess_rs/Reads.h>
#include <MotorControlRS/profiles/ess_rs/Registers.h>
#include <MotorControlRS/profiles/ess_rs/Segments.h>
#include <MotorControlRS/profiles/ess_rs/Traffic.h>
#include <MotorControlRS/profiles/ess_rs/Tuning.h>
#include <MotorControlRS/profiles/ess_rs/Types.h>
#include <MotorControlRS/profiles/ess_rs/Velocity.h>
#include <cstdlib>

extern "C" void app_main() {
    using namespace MotorControlRS;
    uint8_t request[ESS_RS::READ_REQUEST_LEN] = {};
    if (ESS_RS::buildProbe(1, request, sizeof(request)) != sizeof(request) ||
        request[0] != 1 || request[1] != 3 || ESS_RS::calcCrc16(request, sizeof(request)) != 0)
        std::abort();
    const uint8_t reply[] = {1, 3, 2, 3, 5, 0x78, 0xB7};
    uint16_t model = 0;
    if (!ESS_RS::parseProbe(reply, sizeof(reply), 1, model) || model != 0x0305)
        std::abort();
    if (ESS_RS::parseProbe(reply, sizeof(reply), 2, model) || model != 0x0305)
        std::abort(); // Rejection must preserve the previously checked value.

    AxisConfig axis; axis.target.id = 1; axis.target.address = 1; axis.target.generation = 1;
    axis.supportedRelativeBases = 1;
    PositionRequest position; position.configurationGeneration = axis.generation;
    position.value = Rational(INT64_C(9007199254740993));
    PreparedTarget prepared;
    if (!preparePosition(position, axis, nullptr, prepared) || !prepared.exactArithmetic ||
        prepared.effectiveNative != position.value.numerator)
        std::abort();
}
