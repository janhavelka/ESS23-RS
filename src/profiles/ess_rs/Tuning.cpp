// SPDX-License-Identifier: MIT
#include "MotorControlRS/profiles/ess_rs/Tuning.h"
#include "MotorControlRS/profiles/ess_rs/Registers.h"
namespace MotorControlRS { namespace ESS_RS {
namespace {
const uint16_t REGS[] = {
    Registers::INPUT_FILTER, Registers::PULSE_LOW_PASS_FILTER, Registers::POSITION_ERROR_ALARM_THRESHOLD,
    Registers::POSITION_ARRIVAL_WINDOW, Registers::ARRIVAL_TIME, Registers::PULSE_MEAN_FILTER,
    Registers::CURRENT_LOOP_KP_MULTIPLIER, Registers::CURRENT_LOOP_KP, Registers::CURRENT_LOOP_KI, Registers::CURRENT_LOOP_KC,
    Registers::LA_SPEED_KP1, Registers::LA_SPEED_KV1, Registers::LA_SPEED_NODE1, Registers::LA_SPEED_KP2,
    Registers::LA_SPEED_KV2, Registers::LA_SPEED_NODE2, Registers::LA_SPEED_FEEDFORWARD_KVF, Registers::LA_POSITION_KI,
    Registers::COLLISION_THRESHOLD_0122, Registers::COLLISION_CURRENT_0123,
    Registers::COLLISION_THRESHOLD_003B, Registers::COLLISION_CURRENT_003C
};
}
bool isTuningGroup(DriverGroup group) noexcept {
    return group >= DriverGroup::FILTERS && group <= DriverGroup::COLLISION;
}
uint8_t tuningFieldCount(DriverGroup group) noexcept {
    return group == DriverGroup::FILTERS ? 6 : group == DriverGroup::CURRENT_LOOP ? 4 :
        group == DriverGroup::LA ? 8 : group == DriverGroup::COLLISION ? 2 : 0;
}
TuningParameter tuningParameter(DriverGroup group, uint8_t slot) noexcept {
    if (slot >= tuningFieldCount(group)) return TuningParameter::NONE;
    const uint8_t first = group == DriverGroup::FILTERS ? 0 : group == DriverGroup::CURRENT_LOOP ? 6 :
        group == DriverGroup::LA ? 10 : 18;
    return static_cast<TuningParameter>(first + slot);
}
TuningParameter laStageParameter(uint8_t stage, LaStageField field) noexcept {
    if (!stage || stage > 2 || field > LaStageField::NODE) return TuningParameter::NONE;
    return tuningParameter(DriverGroup::LA, static_cast<uint8_t>((stage - 1) * 3 + static_cast<uint8_t>(field)));
}
bool tuningParameterInfo(TuningParameter parameter, TuningParameterInfo& output) noexcept {
    const uint8_t index = static_cast<uint8_t>(parameter);
    if (index >= static_cast<uint8_t>(TuningParameter::NONE)) return false;
    TuningParameterInfo info;
    info.group = index < 6 ? DriverGroup::FILTERS : index < 10 ? DriverGroup::CURRENT_LOOP :
        index < 18 ? DriverGroup::LA : DriverGroup::COLLISION;
    info.slot = index < 6 ? index : index < 10 ? index - 6 : index < 18 ? index - 10 : index - 18;
    info.reg = REGS[index]; info.maximum = 65535; info.accessReviewed = index < 20;
    switch (parameter) {
    case TuningParameter::PULSE_LOW_PASS_FILTER: info.maximum = 1024; break;
    case TuningParameter::POSITION_ERROR_ALARM_THRESHOLD: info.minimum = 1; break;
    case TuningParameter::POSITION_ARRIVAL_WINDOW: info.minimum = 1; info.maximum = 256; break;
    case TuningParameter::ARRIVAL_TIME: info.maximum = 200; break;
    case TuningParameter::PULSE_MEAN_FILTER: info.maximum = 512; break;
    case TuningParameter::COLLISION_THRESHOLD_0122: info.minimum = 200; info.maximum = 4000; break;
    case TuningParameter::COLLISION_THRESHOLD_003B: info.minimum = 50; info.maximum = 4000; break;
    case TuningParameter::COLLISION_CURRENT_0123:
    case TuningParameter::COLLISION_CURRENT_003C: info.minimum = 20; info.maximum = 100; break;
    default: break;
    }
    output = info; return true;
}
Status prepareTuningValue(DriverRequest& request, TuningParameter parameter, uint32_t value) noexcept {
    TuningParameterInfo info;
    if (!tuningParameterInfo(parameter, info))
        return Status(Err::INVALID_CONFIG, static_cast<int32_t>(DriverError::INVALID_CANDIDATE), "invalid tuning parameter");
    if (!info.accessReviewed)
        return Status(Err::UNSUPPORTED, static_cast<int32_t>(DriverError::TUNING_ACCESS_UNRESOLVED), "collision register access is unspecified");
    if (value < info.minimum || value > info.maximum || (request.fields && request.group != info.group))
        return Status(Err::INVALID_CONFIG, static_cast<int32_t>(DriverError::INVALID_CANDIDATE), "invalid native tuning value or mixed group");
    request.group = info.group; request.fields |= 1u << info.slot;
    request.tuningValues[info.slot] = static_cast<uint16_t>(value); return Ok();
}
Status prepareTuningRead(DriverContext& out, const ReadTarget& target, uint32_t id,
    uint32_t generation, uint64_t now, uint64_t deadline, DriverGroup group) noexcept {
    if (!isTuningGroup(group))
        return Status(Err::INVALID_CONFIG, static_cast<int32_t>(DriverError::INVALID_CANDIDATE), "tuning group required");
    return prepareDriverRead(out,target,id,generation,now,deadline,group);
}
Status prepareTuningSettings(DriverContext& out, const ReadTarget& target, uint32_t id,
    const DriverRequest& request, const DriverPrerequisites& prerequisites, uint64_t now, uint64_t deadline) noexcept {
    if (!isTuningGroup(request.group))
        return Status(Err::INVALID_CONFIG, static_cast<int32_t>(DriverError::INVALID_CANDIDATE), "tuning group required");
    return prepareDriverSettings(out,target,id,request,prerequisites,now,deadline);
}
Status getTuning(const DriverContext& context, TuningObservation& output) noexcept {
    if (!isTuningGroup(context.group))
        return Status(Err::INVALID_CONFIG, static_cast<int32_t>(DriverError::NOT_COMPLETE), "complete tuning read required");
    DriverObservation raw; const Status status = getDriver(context,raw);
    if (!status) return status;
    TuningObservation value; value.group = raw.group; value.target = raw.target;
    value.operationId = raw.operationId; value.configurationGeneration = raw.configurationGeneration;
    value.count = tuningFieldCount(raw.group); value.knownFields = raw.knownFields;
    for (uint8_t i = 0; i < value.count; ++i) value.raw[i] = raw.raw[i];
    const uint8_t steps = value.count > 4 ? 2 : 1;
    for (uint8_t i = 0; i < steps; ++i) value.provenance[i] = raw.provenance[i];
    output = value; return Ok();
}
}} // namespace MotorControlRS::ESS_RS
