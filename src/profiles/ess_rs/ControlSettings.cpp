// SPDX-License-Identifier: MIT
#include "MotorControlRS/profiles/ess_rs/ControlSettings.h"
namespace MotorControlRS { namespace ESS_RS {
Status prepareControlRead(DriverContext& out, const ReadTarget& target, uint32_t id,
    uint32_t generation, uint64_t now, uint64_t deadline) noexcept {
    return prepareDriverRead(out, target, id, generation, now, deadline, DriverGroup::CONTROL_SETTINGS);
}
Status prepareControlSettings(DriverContext& out, const ReadTarget& target, uint32_t id,
    const DriverRequest& request, const DriverPrerequisites& prerequisites, uint64_t now, uint64_t deadline) noexcept {
    if (request.group != DriverGroup::CONTROL_SETTINGS)
        return Status(Err::INVALID_CONFIG, static_cast<int32_t>(DriverError::INVALID_CANDIDATE), "control-setting group required");
    return prepareDriverSettings(out, target, id, request, prerequisites, now, deadline);
}
Status getControl(const DriverContext& context, ControlObservation& output) noexcept {
    if (context.group != DriverGroup::CONTROL_SETTINGS)
        return Status(Err::INVALID_CONFIG, static_cast<int32_t>(DriverError::NOT_COMPLETE), "complete control-setting read required");
    DriverObservation raw;
    const Status status = getDriver(context, raw);
    if (!status) return status;
    ControlObservation value; value.target = raw.target; value.operationId = raw.operationId;
    value.configurationGeneration = raw.configurationGeneration; value.knownFields = raw.knownFields;
    for (uint8_t i = 0; i < CONTROL_FIELD_COUNT; ++i) value.raw[i] = raw.raw[i];
    value.algorithm = static_cast<ControlAlgorithm>(raw.raw[0]);
    value.algorithmKnown = (raw.knownFields & static_cast<uint32_t>(DriverField::CONTROL_ALGORITHM)) != 0;
    value.encoderResolution = raw.raw[1]; value.encoderScaleUsable = raw.raw[1] != 0;
    value.maximumEffectiveCurrentMa = raw.raw[2]; value.closedMaximumPercent = raw.raw[3];
    value.closedBasePercent = raw.raw[4]; value.openMaximumPercent = raw.raw[5];
    value.lockPercent = raw.raw[6]; value.lockDelayMs = raw.raw[7];
    for (uint8_t i = 0; i < 2; ++i) value.provenance[i] = raw.provenance[i];
    output = value; return Ok();
}
}} // namespace MotorControlRS::ESS_RS
