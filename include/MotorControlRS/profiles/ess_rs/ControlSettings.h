/** @file ControlSettings.h
 * @brief ESS control configuration; no commanded current or torque mode.
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "MotorControlRS/profiles/ess_rs/DriverSettings.h"
namespace MotorControlRS { namespace ESS_RS {
constexpr uint8_t CONTROL_FIELD_COUNT = 8;
/** Stored native words only. Unknown algorithm remains raw. Encoder setting
 * does not identify an encoder or prove physical accuracy. Percent-current
 * denominator conflicts in the manual, so no physical-current conversion is
 * published. Lock delay is milliseconds after stopping, not a communication
 * timeout or motion-completion guarantee. */
struct ControlObservation {
    ReadTarget target;
    uint32_t operationId = 0, configurationGeneration = 0, knownFields = 0;
    uint16_t raw[CONTROL_FIELD_COUNT] = {};
    ControlAlgorithm algorithm = ControlAlgorithm::OPEN_LOOP;
    bool algorithmKnown = false, encoderScaleUsable = false;
    uint16_t encoderResolution = 0, maximumEffectiveCurrentMa = 0;
    uint16_t closedMaximumPercent = 0, closedBasePercent = 0,
             openMaximumPercent = 0, lockPercent = 0, lockDelayMs = 0;
    bool percentBaseKnown = false, activeSettingsKnown = false;
    DriverEvidence provenance[2];
};
/** Two checked, non-changing four-word reads. Outputs unchanged on rejection. */
Status prepareControlRead(DriverContext&, const ReadTarget&, uint32_t operationId,
    uint32_t configurationGeneration, uint64_t nowUs, uint64_t deadlineUs) noexcept;
/** Select control-group DriverField values in DriverRequest/CONTROL_SETTINGS; configuration
 * and effects qualify the exact stopped model/firmware. Current and algorithm
 * candidates additionally need an independently established effective-current
 * ceiling and percent-base formula. No peak-to-effective conversion, 0x2042
 * alias or rollback is guessed. Zero encoder scale is rejected before traffic.
 * Execution continues through existing nextDriver/advanceDriver. */
Status prepareControlSettings(DriverContext&, const ReadTarget&, uint32_t operationId,
    const DriverRequest&, const DriverPrerequisites&, uint64_t nowUs, uint64_t deadlineUs) noexcept;
Status getControl(const DriverContext&, ControlObservation&) noexcept;
}} // namespace MotorControlRS::ESS_RS
