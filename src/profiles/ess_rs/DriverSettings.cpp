// SPDX-License-Identifier: MIT
#include "MotorControlRS/profiles/ess_rs/DriverSettings.h"
#include "MotorControlRS/profiles/ess_rs/Registers.h"
#include "MotorControlRS/profiles/ess_rs/Tuning.h"
#include <cstring>
#include <limits>
#include <new>

namespace MotorControlRS { namespace ESS_RS {
namespace {
const uint32_t FIELD_MASK = 0x007F, PAIR_MASK = 0x0180, GEOMETRY_MASK = 0x0007, IO_MASK = 0x3FE00, CONTROL_MASK = 0x7F800000u;
const uint16_t CONTROL_REGS[8] = {
    Registers::CONTROL_ALGORITHM, Registers::ENCODER_RESOLUTION, Registers::MAXIMUM_CURRENT,
    Registers::CLOSED_LOOP_CURRENT_PERCENT, Registers::BASE_CURRENT_PERCENT,
    Registers::OPEN_LOOP_CURRENT_PERCENT, Registers::LOCK_CURRENT_PERCENT, Registers::LOCK_TIME
};
const uint16_t REGS[DRIVER_FIELD_COUNT] = {
    Registers::DEFAULT_DIRECTION, Registers::SUBDIVISION, Registers::WORD_ORDER,
    Registers::SOFT_LIMIT_ENABLE, Registers::OVER_LIMIT_STOP,
    Registers::PV_TRIGGER_MODE, Registers::EXTERNAL_POSITION_MODE,
    Registers::INPUT_POLARITY, Registers::INPUT_X0_FUNCTION, Registers::INPUT_X1_FUNCTION,
    Registers::INPUT_X2_FUNCTION, Registers::INPUT_X3_FUNCTION, Registers::OUTPUT_POLARITY,
    Registers::OUTPUT_Y0_FUNCTION, Registers::OUTPUT_Y1_FUNCTION, Registers::CUSTOM_OUTPUT
};
Status invalid(DriverError reason, const char* message) {
    return Status(Err::INVALID_CONFIG, static_cast<int32_t>(reason), message);
}
Status failure(DriverError reason, const char* message) {
    return Status(Err::ILLEGAL_VALUE, static_cast<int32_t>(reason), message);
}
bool sameTarget(const ReadTarget& a, const ReadTarget& b) {
    return a.id == b.id && a.address == b.address && a.generation == b.generation;
}
bool aliasesContext(const DriverContext& context, const void* input, std::size_t size) {
    const uintptr_t begin = reinterpret_cast<uintptr_t>(&context);
    const uintptr_t other = reinterpret_cast<uintptr_t>(input);
    return other >= begin ? other - begin < sizeof(context) : begin - other < size;
}
bool segmentGroup(DriverGroup group) { return group >= DriverGroup::POSITION_SEGMENT && group <= DriverGroup::SEGMENT_START_SPEED; }
uint8_t readSteps(DriverGroup group) { return isTuningGroup(group) ? (tuningFieldCount(group) > 4 ? 2 : 1) : segmentGroup(group) ? 1 : group == DriverGroup::CONTROL_SETTINGS ? 2 : group == DriverGroup::IO ? 3 : DRIVER_READ_STEPS; }
uint32_t bit(uint8_t index, DriverGroup group = DriverGroup::DRIVE) {
    if (isTuningGroup(group)) return index < tuningFieldCount(group) ? 1u << index : 0;
    if (group == DriverGroup::CONTROL_SETTINGS) return index < 8 ? 1u << (23 + index) : 0;
    if (segmentGroup(group)) return group == DriverGroup::SEGMENT_START_SPEED ?
        (index == 0 ? 1u << 21 : 0) : (index < 3 ? 1u << (18 + index) : 0);
    return index < 7 ? 1u << index : 1u << (index + 2);
}
const RegisterDescriptor* segmentRegister(DriverGroup group, uint8_t record, uint8_t index) {
    if (group == DriverGroup::POSITION_SEGMENT) {
        const PositionSegmentField fields[] = { PositionSegmentField::SPEED, PositionSegmentField::ACCELERATION_TIME, PositionSegmentField::DECELERATION_TIME };
        return index < 3 ? positionSegmentRegister(record, fields[index]) : nullptr;
    }
    if (group == DriverGroup::SPEED_SEGMENT) return index < 3 ? speedSegmentRegister(record, static_cast<SpeedSegmentField>(index)) : nullptr;
    return group == DriverGroup::SEGMENT_START_SPEED && index == 0 ? segmentStartSpeedRegister(record) : nullptr;
}
uint16_t fieldRegister(const DriverRequest& r, uint8_t index) {
    if (isTuningGroup(r.group)) {
        TuningParameterInfo info;
        return tuningParameterInfo(tuningParameter(r.group,index),info) && info.accessReviewed ? info.reg : 0;
    }
    if (r.group == DriverGroup::CONTROL_SETTINGS) return index < 8 ? CONTROL_REGS[index] : 0;
    if (!segmentGroup(r.group)) return REGS[index];
    const RegisterDescriptor* d = segmentRegister(r.group, r.segmentIndex, index);
    return d ? d->address : 0;
}
bool legal(uint8_t index, uint16_t value) {
    if (index >= 8 && index <= 11) return value <= 17;
    if (index == 13 || index == 14) return value <= 5 || value == 9 || value == 10;
    if (index == 7) return (value & ~0xFu) == 0;
    if (index == 12 || index == 15) return (value & ~3u) == 0;
    return index == 1 ? value >= 400 && value <= 51200 : value <= 1;
}
bool legalControl(uint8_t index, uint16_t value) {
    const uint16_t maxima[] = {2, 65535, 5600, 150, 75, 100, 100, 20000};
    return index < 8 && value <= maxima[index] && (index != 0 || value >= 1);
}
uint16_t requested(const DriverRequest& request, uint8_t index) {
    if (isTuningGroup(request.group)) return request.tuningValues[index];
    if (request.group == DriverGroup::CONTROL_SETTINGS) {
        switch (index) {
        case 0: return static_cast<uint16_t>(request.controlAlgorithm);
        case 1: return request.encoderResolution;
        case 2: return request.maximumEffectiveCurrentMa;
        case 3: return request.closedMaximumPercent;
        case 4: return request.closedBasePercent;
        case 5: return request.openMaximumPercent;
        case 6: return request.lockPercent;
        case 7: return request.lockDelayMs;
        default: return 0;
        }
    }
    if (segmentGroup(request.group)) {
        if (request.group == DriverGroup::SEGMENT_START_SPEED) return static_cast<uint16_t>(request.segmentStartSpeed);
        return index == 0 ? static_cast<uint16_t>(request.segmentSpeed) : index == 1 ? request.segmentAcceleration : request.segmentDeceleration;
    }
    switch (index) {
    case 0: return static_cast<uint16_t>(request.direction);
    case 1: return request.subdivision;
    case 2: return static_cast<uint16_t>(request.wordOrder);
    case 3: return static_cast<uint16_t>(request.softLimitEnable);
    case 4: return static_cast<uint16_t>(request.overLimitStop);
    case 5: return static_cast<uint16_t>(request.interruption);
    case 6: return static_cast<uint16_t>(request.positionMode);
    case 7: return request.inputPolarity;
    case 12: return request.outputPolarity;
    case 13: case 14: return static_cast<uint16_t>(request.outputFunctions[index - 13]);
    case 15: return request.customOutput;
    default: return static_cast<uint16_t>(request.inputFunctions[index - 8]);
    }
}
bool readWindow(DriverGroup group, uint8_t step, uint16_t& reg, uint16_t& count, uint8_t record = 1) {
    if (isTuningGroup(group)) {
        const uint8_t first = static_cast<uint8_t>(step * 4), total = tuningFieldCount(group);
        TuningParameterInfo info;
        if (first >= total || !tuningParameterInfo(tuningParameter(group,first),info) || !info.accessReviewed) return false;
        reg = info.reg; count = total - first > 4 ? 4 : total - first; return true;
    }
    if (group == DriverGroup::CONTROL_SETTINGS) {
        if (step > 1) return false;
        reg = step ? Registers::BASE_CURRENT_PERCENT : Registers::CONTROL_ALGORITHM;
        count = 4; return true;
    }
    if (segmentGroup(group)) {
        if (step || !record || record > 16) return false;
        const RegisterDescriptor* d = group == DriverGroup::POSITION_SEGMENT ? positionSegmentRegister(record, PositionSegmentField::PULSES) : segmentRegister(group, record, 0);
        if (!d) return false;
        reg = d->address; count = group == DriverGroup::POSITION_SEGMENT ? 5 : group == DriverGroup::SPEED_SEGMENT ? 3 : 1;
        return true;
    }
    if (group == DriverGroup::IO) {
        switch (step) {
        case 0: reg = Registers::INPUT_POLARITY; count = 5; return true;
        case 1: reg = Registers::OUTPUT_POLARITY; count = 3; return true;
        case 2: reg = Registers::CUSTOM_OUTPUT; count = 1; return true;
        default: return false;
        }
    }
    switch (step) {
    case 0: reg = Registers::DEFAULT_DIRECTION; count = 2; return true;
    case 1: reg = Registers::OVER_LIMIT_STOP; count = 3; return true;
    case 2: reg = Registers::POSITIVE_SOFT_LIMIT; count = 4; return true;
    case 3: reg = Registers::EXTERNAL_POSITION_MODE; count = 2; return true;
    default: return false;
    }
}
uint8_t totalSteps(const DriverContext& c) {
    return c.kind == DriverKind::READ ? readSteps(c.group) : static_cast<uint8_t>(c.fieldCount * 2);
}
void finish(DriverContext& c, DriverOutcome outcome, Status status) {
    c.outcome = outcome; c.status = status;
    c.state = outcome == DriverOutcome::SUCCESS ? ReadState::SUCCEEDED : ReadState::FAILED;
}
uint64_t stationaryDeadline(const DriverContext& c) {
    const DriverPrerequisites& p = c.prerequisites;
    const uint64_t maximum = std::numeric_limits<uint64_t>::max();
    const uint64_t until = p.maxAgeUs > maximum - p.stationaryEarliestUs ? maximum :
        p.stationaryEarliestUs + p.maxAgeUs;
    uint64_t deadline = until < c.deadlineUs ? until : c.deadlineUs;
    for (uint8_t i = 0; i < readSteps(c.group); ++i) {
        const uint64_t attempted = p.previous.provenance[i].attemptedUs;
        const uint64_t settingsUntil = p.maxAgeUs > maximum - attempted ? maximum : attempted + p.maxAgeUs;
        if (settingsUntil < deadline) deadline = settingsUntil;
    }
    if (c.group == DriverGroup::IO) {
        const uint64_t ioUntil = p.maxAgeUs > maximum - p.ioEarliestUs ? maximum : p.ioEarliestUs + p.maxAgeUs;
        if (ioUntil < deadline) deadline = ioUntil;
    }
    if (segmentGroup(c.group)) {
        const uint64_t triggerUntil = p.maxAgeUs > maximum - p.triggerEarliestUs ? maximum : p.triggerEarliestUs + p.maxAgeUs;
        if (triggerUntil < deadline) deadline = triggerUntil;
    }
    if (c.group == DriverGroup::CONTROL_SETTINGS || isTuningGroup(c.group)) {
        const uint64_t effectsEarliest = isTuningGroup(c.group) ? p.tuningEarliestUs : p.controlEarliestUs;
        const uint64_t controlUntil = p.maxAgeUs > maximum - effectsEarliest ? maximum : effectsEarliest + p.maxAgeUs;
        const uint64_t identityUntil = p.maxAgeUs > maximum - p.controlIdentity.provenance.attemptedUs ? maximum : p.controlIdentity.provenance.attemptedUs + p.maxAgeUs;
        if (controlUntil < deadline) deadline = controlUntil;
        if (identityUntil < deadline) deadline = identityUntil;
    }
    return deadline;
}
Status validateIdentity(const ReadTarget& target, uint32_t id,
                        uint32_t generation, uint64_t now, uint64_t deadline) {
    if (!target.id || !target.generation || !isValidAddress(target.address))
        return invalid(DriverError::INVALID_TARGET, "invalid driver-settings target");
    if (!id || !generation) return invalid(DriverError::INVALID_OPERATION, "zero operation or configuration generation");
    if (deadline <= now) return invalid(DriverError::INVALID_DEADLINE, "expired driver-settings deadline");
    return Ok();
}
void initialize(DriverContext& output, const ReadTarget& target, uint32_t id,
                uint32_t generation, uint64_t now, uint64_t deadline) {
    const ReadTarget copied = target; // Target may be output.target.
    output.~DriverContext();
    ::new (static_cast<void*>(&output)) DriverContext; // Caller-owned storage, no allocation.
    output.target = copied; output.operationId = id; output.configurationGeneration = generation;
    output.state = ReadState::ACTIVE; output.deadlineUs = deadline;
    output.startedUs = output.servicedUs = output.eligibleUs = now;
}
Status decode(const DriverEvidence* evidence, const ReadTarget& target, DriverGroup group, DriverObservation& out, uint8_t record = 1) {
    uint16_t data[4][5] = {};
    out.group = group;
    const uint8_t steps = readSteps(group);
    for (uint8_t i = 0; i < steps; ++i) {
        uint16_t reg = 0, count = 0; readWindow(group, i, reg, count, record);
        std::size_t decoded = 0;
        if (evidence[i].event != ReadEventKind::FRAME || evidence[i].step != i ||
            evidence[i].write || evidence[i].reg != reg || evidence[i].count != count ||
            evidence[i].txAccepted != READ_REQUEST_LEN || !evidence[i].txComplete ||
            evidence[i].executionUnknown || evidence[i].receivedLength != evidence[i].length ||
            !evidence[i].qualified || !evidence[i].responseConfirmed || !evidence[i].status)
            return invalid(DriverError::NOT_COMPLETE, "driver read provenance is incomplete");
        const Status status = parseRegisters(evidence[i].raw, evidence[i].length,
            target.address, count, data[i], 5, decoded);
        if (!status) return status;
        out.provenance[i] = evidence[i];
    }
    if (isTuningGroup(group)) {
        for (uint8_t i = 0; i < tuningFieldCount(group); ++i) {
            out.raw[i] = data[i / 4][i % 4];
            TuningParameterInfo info;
            if (tuningParameterInfo(tuningParameter(group,i),info) && info.accessReviewed &&
                out.raw[i] >= info.minimum && out.raw[i] <= info.maximum) out.knownFields |= bit(i,group);
        }
        return Ok();
    }
    if (group == DriverGroup::CONTROL_SETTINGS) {
        for (uint8_t i = 0; i < 8; ++i) {
            out.raw[i] = i < 4 ? data[0][i] : data[1][i - 4];
            if (legalControl(i, out.raw[i]) && (i != 1 || out.raw[i])) out.knownFields |= bit(i, group);
        }
        return Ok();
    }
    if (segmentGroup(group)) {
        out.segmentIndex = record;
        const uint8_t values = group == DriverGroup::SEGMENT_START_SPEED ? 1 : 3;
        for (uint8_t i = 0; i < values; ++i) {
            out.raw[i] = data[0][i + (group == DriverGroup::POSITION_SEGMENT ? 2 : 0)];
            const uint16_t maximum = group == DriverGroup::SEGMENT_START_SPEED ? 180 : i == 0 ? 3000 : 2000;
            if (out.raw[i] <= maximum) out.knownFields |= bit(i, group);
        }
        if (group == DriverGroup::POSITION_SEGMENT) { out.raw[3] = data[0][0]; out.raw[4] = data[0][1]; }
        return Ok();
    }
    if (group == DriverGroup::IO) {
        for (uint8_t i = 0; i < 5; ++i) out.raw[7 + i] = data[0][i];
        for (uint8_t i = 0; i < 3; ++i) out.raw[12 + i] = data[1][i];
        out.raw[15] = data[2][0];
        for (uint8_t i = 7; i < DRIVER_FIELD_COUNT; ++i)
            if (legal(i, out.raw[i])) out.knownFields |= bit(i);
        return Ok();
    }
    out.raw[0] = data[0][0]; out.raw[1] = data[0][1];
    out.raw[4] = data[1][0]; out.raw[3] = data[1][1]; out.raw[2] = data[1][2];
    out.raw[6] = data[3][0]; out.raw[5] = data[3][1];
    for (uint8_t i = 0; i < 7; ++i)
        if (legal(i, out.raw[i])) out.knownFields |= bit(i);
    for (uint8_t i = 0; i < 2; ++i) {
        out.positiveWords[i] = data[2][i]; out.negativeWords[i] = data[2][i + 2];
    }
    out.pairKnown = (out.knownFields & bit(2)) != 0;
    if (out.pairKnown) {
        const uint8_t high = out.raw[2] == 0 ? 0 : 1;
        out.positiveBits = (static_cast<uint32_t>(out.positiveWords[high]) << 16) | out.positiveWords[1 - high];
        out.negativeBits = (static_cast<uint32_t>(out.negativeWords[high]) << 16) | out.negativeWords[1 - high];
    }
    return Ok();
}
bool unresolvedWrite(const DriverContext& c) {
    for (uint8_t i = 0; i < DRIVER_FIELD_COUNT; ++i) {
        const DriverProgress& p = c.progress[i];
        const bool storedSettled = (c.group == DriverGroup::IO || segmentGroup(c.group) || c.group == DriverGroup::CONTROL_SETTINGS || isTuningGroup(c.group)) && c.prerequisites.allowEchoReadback &&
            p.readbackKnown && p.readback == p.requested;
        if (p.selected && ((p.execution == ActionExecution::UNKNOWN && !storedSettled) ||
            (p.execution == ActionExecution::ACKNOWLEDGED &&
             (!p.readbackKnown || p.readback != p.requested)))) return true;
    }
    return false;
}
} // namespace

Status prepareDriverRead(DriverContext& output, const ReadTarget& target, uint32_t operationId,
                         uint32_t configurationGeneration, uint64_t nowUs, uint64_t deadlineUs, DriverGroup group, uint8_t segmentIndex) noexcept {
    const Status status = validateIdentity(target, operationId, configurationGeneration, nowUs, deadlineUs);
    if (!status) return status;
    if (group > DriverGroup::COLLISION) return invalid(DriverError::INVALID_CANDIDATE, "invalid driver group");
    if (segmentGroup(group) && (!segmentIndex || segmentIndex > 16)) return invalid(DriverError::SEGMENT_INDEX, "segment index must be 1..16");
    for (uint8_t i = 0; i < readSteps(group); ++i) {
        uint16_t reg = 0, count = 0; readWindow(group, i, reg, count, segmentIndex);
        const Status wire = validateReadRegistersRequest(target.address, reg, count);
        if (!wire) return wire;
    }
    initialize(output, target, operationId, configurationGeneration, nowUs, deadlineUs);
    output.group = group; output.request.group = group; output.request.segmentIndex = segmentIndex;
    return Ok();
}
Status prepareDriverSettings(DriverContext& output, const ReadTarget& target, uint32_t operationId,
                             const DriverRequest& request, const DriverPrerequisites& prerequisites,
                             uint64_t nowUs, uint64_t deadlineUs) noexcept {
    const Status initial = validateIdentity(target, operationId, request.configurationGeneration, nowUs, deadlineUs);
    if (!initial) return initial;
    if (aliasesContext(output, &request, sizeof(request)) || aliasesContext(output, &prerequisites, sizeof(prerequisites)))
        return invalid(DriverError::INVALID_CANDIDATE, "preparation inputs must not alias output members");
    const bool segment = segmentGroup(request.group);
    const bool control = request.group == DriverGroup::CONTROL_SETTINGS;
    const bool tuning = isTuningGroup(request.group);
    const uint32_t allowed = tuning ? (1u << tuningFieldCount(request.group)) - 1 : control ? CONTROL_MASK : segment ? (request.group == DriverGroup::SEGMENT_START_SPEED ? 1u << 21 : (7u << 18) | (request.group == DriverGroup::POSITION_SEGMENT ? 1u << 22 : 0)) :
        request.group == DriverGroup::IO ? IO_MASK : FIELD_MASK | PAIR_MASK;
    if (request.group > DriverGroup::COLLISION || !request.fields || (request.fields & ~allowed))
        return invalid(DriverError::INVALID_CANDIDATE, "invalid or empty driver-settings selection");
    if (request.group == DriverGroup::DRIVE && prerequisites.allowEchoReadback)
        return invalid(DriverError::INVALID_CANDIDATE, "echo/readback policy requires IO or inhibited segment settings");
    if (segment && (!request.segmentIndex || request.segmentIndex > 16)) return invalid(DriverError::SEGMENT_INDEX, "segment index must be 1..16");
    if (segment && (request.fields & (1u << 22)))
        return Status(Err::UNSUPPORTED, static_cast<int32_t>(DriverError::PAIR_WRITE_UNSUPPORTED), "stored pulse pair has no reviewed FC10 window");
    if (segment) {
        const int32_t speed = request.group == DriverGroup::SEGMENT_START_SPEED ? request.segmentStartSpeed : request.segmentSpeed;
        const uint32_t speedBit = request.group == DriverGroup::SEGMENT_START_SPEED ? 1u << 21 : 1u << 18;
        if ((request.fields & speedBit) && speed < 0) {
            if (request.group == DriverGroup::POSITION_SEGMENT) return invalid(DriverError::INVALID_CANDIDATE, "PT speed is nonnegative");
            return Status(Err::UNSUPPORTED, static_cast<int32_t>(DriverError::SEGMENT_SIGN_UNRESOLVED), "negative segment speed encoding unresolved");
        }
        if (((request.fields & speedBit) && speed > (request.group == DriverGroup::SEGMENT_START_SPEED ? 180 : 3000)) ||
            ((request.fields & (1u << 19)) && request.segmentAcceleration > 2000) ||
            ((request.fields & (1u << 20)) && request.segmentDeceleration > 2000))
            return invalid(DriverError::INVALID_CANDIDATE, "segment native value out of range");
    }
    // Reject the entire candidate before producing even an otherwise legal single write.
    if (request.group == DriverGroup::DRIVE && (request.fields & PAIR_MASK))
        return Status(Err::UNSUPPORTED, static_cast<int32_t>(DriverError::PAIR_WRITE_UNSUPPORTED),
            "software-limit pair has no reviewed FC10 window; split writes are unavailable");
    for (uint8_t i = 0; i < DRIVER_FIELD_COUNT; ++i) {
        if (!(request.fields & bit(i, request.group))) continue;
        if (!segment && !control && !tuning && (i == 13 || i == 14) && requested(request, i) == 11)
            return Status(Err::UNSUPPORTED, static_cast<int32_t>(DriverError::OUTPUT_FUNCTION_UNRESOLVED), "custom output 2 has no resolved ESS terminal mapping");
        TuningParameterInfo tuningInfo;
        if (tuning && (!tuningParameterInfo(tuningParameter(request.group,i),tuningInfo) || !tuningInfo.accessReviewed ||
            requested(request,i) < tuningInfo.minimum || requested(request,i) > tuningInfo.maximum))
            return invalid(DriverError::INVALID_CANDIDATE, "tuning native value out of range");
        if ((!segment && !control && !tuning && !legal(i, requested(request, i))) || (control && !legalControl(i, requested(request, i))))
            return invalid(DriverError::INVALID_CANDIDATE, "illegal driver-setting enum or native range");
        const Status wire = validateWriteSingleRegisterRequest(target.address, fieldRegister(request, i), requested(request, i));
        if (!wire) return wire;
        const Status readback = validateReadRegistersRequest(target.address, fieldRegister(request, i), 1);
        if (!readback) return readback;
    }
    if (control && (request.fields & bit(1, request.group)) && !request.encoderResolution)
        return invalid(DriverError::ZERO_ENCODER_SCALE, "zero configured encoder resolution is not a mathematical scale");
    const DriverObservation& previous = prerequisites.previous;
    if (request.configurationGeneration != prerequisites.configurationGeneration ||
        request.configurationGeneration != previous.configurationGeneration ||
        !sameTarget(previous.target, target) || !previous.operationId || previous.group != request.group || (segment && previous.segmentIndex != request.segmentIndex))
        return invalid(DriverError::STALE_SETTINGS, "previous driver-settings context does not match");
    DriverObservation checked;
    const Status decoded = decode(previous.provenance, target, request.group, checked, request.segmentIndex);
    if (!decoded) return invalid(DriverError::STALE_SETTINGS, "previous driver-settings provenance is invalid");
    if (checked.knownFields != previous.knownFields || checked.pairKnown != previous.pairKnown ||
        checked.positiveBits != previous.positiveBits || checked.negativeBits != previous.negativeBits ||
        std::memcmp(checked.raw, previous.raw, sizeof(checked.raw)) ||
        std::memcmp(checked.positiveWords, previous.positiveWords, sizeof(checked.positiveWords)) ||
        std::memcmp(checked.negativeWords, previous.negativeWords, sizeof(checked.negativeWords)))
        return invalid(DriverError::STALE_SETTINGS, "previous settings disagree with checked raw provenance");
    if (!prerequisites.stationaryQualified || (request.group == DriverGroup::DRIVE && !prerequisites.inputsPermit) ||
        !sameTarget(prerequisites.stationaryTarget, target) || !prerequisites.maxAgeUs ||
        prerequisites.stationaryEarliestUs > prerequisites.stationaryLatestUs ||
        prerequisites.stationaryLatestUs > nowUs ||
        nowUs - prerequisites.stationaryEarliestUs >= prerequisites.maxAgeUs ||
        prerequisites.rawAlarm || (prerequisites.rawMotion & 0xFFEC))
        return invalid(DriverError::STATIONARY_REQUIRED, "fresh qualified stopped-state and input policy required");
    for (uint8_t i = 0; i < readSteps(request.group); ++i) {
        const DriverEvidence& e = previous.provenance[i];
        if (e.attemptedUs > e.earliestUs || e.earliestUs > e.latestUs || e.latestUs > nowUs ||
            nowUs - e.attemptedUs >= prerequisites.maxAgeUs)
            return invalid(DriverError::STALE_SETTINGS, "previous driver-settings observation is stale");
    }
    if (segment) {
        const DriverRequest& q = prerequisites.qualifiedSegment;
        if (!prerequisites.externalTriggerInhibitedQualified || !sameTarget(prerequisites.triggerTarget, target) ||
            prerequisites.triggerConfigurationGeneration != request.configurationGeneration ||
            prerequisites.triggerEarliestUs > prerequisites.triggerLatestUs || prerequisites.triggerLatestUs > nowUs ||
            nowUs - prerequisites.triggerEarliestUs >= prerequisites.maxAgeUs ||
            q.group != request.group || q.segmentIndex != request.segmentIndex || q.fields != request.fields ||
            q.configurationGeneration != request.configurationGeneration)
            return invalid(DriverError::TRIGGER_POLICY_REQUIRED, "fresh exact external-trigger inhibition policy required");
        if (((request.fields & (1u << 18)) && request.segmentSpeed != q.segmentSpeed) ||
            ((request.fields & (1u << 19)) && request.segmentAcceleration != q.segmentAcceleration) ||
            ((request.fields & (1u << 20)) && request.segmentDeceleration != q.segmentDeceleration) ||
            ((request.fields & (1u << 21)) && request.segmentStartSpeed != q.segmentStartSpeed))
            return invalid(DriverError::TRIGGER_POLICY_REQUIRED, "qualified segment values differ from candidate");
    }
    if (control || tuning) {
        const IdentityObservation& identity = prerequisites.controlIdentity;
        const ReadStepObservation& source = identity.provenance;
        uint16_t identityWords[4] = {}; std::size_t decodedIdentity = 0;
        if (!prerequisites.exactModelQualified || !prerequisites.modelSourceId || !identity.operationId ||
            !sameTarget(identity.target, target) || identity.rawModel != prerequisites.qualifiedModelCode ||
            identity.rawVersion != prerequisites.qualifiedFirmwareCode || source.first != Registers::DRIVER_MODEL || source.count != 4 ||
            source.event != ReadEventKind::FRAME || !source.qualified || !source.status || source.executionUnknown ||
            source.length != 13 || source.receivedLength != source.length || source.txAccepted != READ_REQUEST_LEN ||
            source.attemptedUs > source.earliestUs || source.earliestUs > source.latestUs || source.latestUs > nowUs ||
            nowUs - source.attemptedUs >= prerequisites.maxAgeUs ||
            !parseRegisters(source.raw, source.length, target.address, 4, identityWords, 4, decodedIdentity) ||
            identityWords[0] != identity.rawModel || identityWords[1] != identity.rawVersion ||
            identityWords[2] != identity.rawActiveNode || identityWords[3] != identity.rawDip)
            return invalid(DriverError::MODEL_REQUIRED, "fresh checked identity and independent exact-model qualification required");
        const DriverRequest& q = tuning ? prerequisites.qualifiedTuning : prerequisites.qualifiedControl;
        const uint32_t qualifiedFields = tuning ? prerequisites.tuningEffectsQualifiedFields : prerequisites.controlEffectsQualifiedFields;
        const uint64_t earliest = tuning ? prerequisites.tuningEarliestUs : prerequisites.controlEarliestUs;
        const uint64_t latest = tuning ? prerequisites.tuningLatestUs : prerequisites.controlLatestUs;
        const DriverError effectsError = tuning ? DriverError::TUNING_EFFECTS_REQUIRED : DriverError::CONTROL_EFFECTS_REQUIRED;
        if ((qualifiedFields & request.fields) != request.fields)
            return Status(Err::UNSUPPORTED, static_cast<int32_t>(effectsError), "setting effects are not independently qualified");
        if (q.group != request.group || q.fields != request.fields || q.configurationGeneration != request.configurationGeneration ||
            earliest > latest || latest > nowUs || nowUs - earliest >= prerequisites.maxAgeUs)
            return invalid(effectsError, "fresh exact setting effects qualification required");
        for (uint8_t i = 0; i < (tuning ? tuningFieldCount(request.group) : 8); ++i)
            if ((request.fields & bit(i, request.group)) && requested(request, i) != requested(q, i))
                return invalid(effectsError, "qualified setting values differ from candidate");
        const uint32_t currentFields = bit(0, request.group) | bit(2, request.group) | bit(3, request.group) | bit(4, request.group) |
            bit(5, request.group) | bit(6, request.group);
        if (control && (request.fields & currentFields)) {
            if (!prerequisites.nativeCurrentLimitQualified || !prerequisites.maximumEffectiveLimitMa ||
                prerequisites.maximumEffectiveLimitMa > 5600)
                return Status(Err::UNSUPPORTED, static_cast<int32_t>(DriverError::CURRENT_LIMIT_UNRESOLVED), "exact-model effective-current ceiling is not qualified");
            if (!prerequisites.currentPercentBaseQualified)
                return Status(Err::UNSUPPORTED, static_cast<int32_t>(DriverError::CURRENT_BASE_UNRESOLVED), "current-percent reference conflicts with ESS map; independent formula qualification required");
            uint16_t candidate[8];
            for (uint8_t i = 0; i < 8; ++i) candidate[i] = previous.raw[i];
            // Check each selected write in the exact published order, before
            // yielding the first one. A safe final tuple cannot hide an unsafe
            // intermediate tuple. No inferred rollback or reordered writes.
            for (uint8_t changed = 0; changed < 8; ++changed) {
                if (!(request.fields & bit(changed, request.group))) continue;
                candidate[changed] = requested(request, changed);
                if (changed == 0 && candidate[0] == static_cast<uint16_t>(ControlAlgorithm::ALGORITHM_1) && !candidate[1])
                    return invalid(DriverError::ZERO_ENCODER_SCALE, "closed-loop transition requires a positive previously configured encoder scale");
                if (!legalControl(0, candidate[0]))
                    return Status(Err::UNSUPPORTED, static_cast<int32_t>(DriverError::CURRENT_BASE_UNRESOLVED), "current mode is unknown");
                if (candidate[2] > prerequisites.maximumEffectiveLimitMa)
                    return invalid(DriverError::CURRENT_DEPENDENCY, "intermediate effective current exceeds qualified model limit");
                for (uint8_t i = 3; i <= 6; ++i) {
                    if (i == 5 && candidate[0] != static_cast<uint16_t>(ControlAlgorithm::OPEN_LOOP)) continue;
                    if ((i == 3 || i == 4) && candidate[0] == static_cast<uint16_t>(ControlAlgorithm::OPEN_LOOP)) continue;
                    if (!legalControl(i, candidate[i]) || static_cast<uint32_t>(candidate[2]) * candidate[i] >
                        static_cast<uint32_t>(prerequisites.maximumEffectiveLimitMa) * 100)
                        return invalid(DriverError::CURRENT_DEPENDENCY, "intermediate current formula exceeds qualified model limit");
                }
            }
        }
    }
    if (request.group == DriverGroup::IO) {
        const uint8_t maskIndices[] = {7, 12, 15};
        for (uint8_t index : maskIndices)
            if ((request.fields & bit(index)) && !legal(index, previous.raw[index]))
                return invalid(DriverError::INVALID_CANDIDATE, "reserved stored IO mask bits cannot be overwritten");
        if (!prerequisites.ioLevelsQualified || !sameTarget(prerequisites.ioTarget, target) ||
            prerequisites.ioConfigurationGeneration != request.configurationGeneration ||
            (prerequisites.actualInputs & ~0xFu) || (prerequisites.actualOutputs & ~3u) ||
            prerequisites.ioEarliestUs > prerequisites.ioLatestUs || prerequisites.ioLatestUs > nowUs ||
            nowUs - prerequisites.ioEarliestUs >= prerequisites.maxAgeUs)
            return invalid(DriverError::IO_EVIDENCE_REQUIRED, "fresh qualified actual IO levels required");
        const DriverRequest& qualified = prerequisites.qualifiedIo;
        if ((prerequisites.ioEffectsQualifiedFields & request.fields) != request.fields ||
            qualified.group != DriverGroup::IO || qualified.configurationGeneration != request.configurationGeneration ||
            qualified.fields != request.fields)
            return invalid(DriverError::IO_EFFECTS_REQUIRED, "exact external-effect qualification required");
        for (uint8_t i = 7; i < DRIVER_FIELD_COUNT; ++i)
            if ((request.fields & bit(i)) && requested(request, i) != requested(qualified, i))
                return invalid(DriverError::IO_EFFECTS_REQUIRED, "qualified IO values differ from candidate");
        uint16_t affectedInputs = 0, affectedOutputs = 0;
        if (request.fields & bit(7)) affectedInputs |= request.inputPolarity ^ previous.raw[7];
        if (request.fields & bit(12)) affectedOutputs |= request.outputPolarity ^ previous.raw[12];
        if (request.fields & bit(15)) affectedOutputs |= request.customOutput ^ previous.raw[15];
        for (uint8_t i = 0; i < 4; ++i) if (request.fields & bit(8 + i)) affectedInputs |= 1u << i;
        for (uint8_t i = 0; i < 2; ++i) if (request.fields & bit(13 + i)) affectedOutputs |= 1u << i;
        for (uint8_t i = 0; i < 4; ++i)
            if ((affectedInputs & (1u << i)) && (prerequisites.inputWiring[i] == InputWiring::UNKNOWN || prerequisites.inputWiring[i] > InputWiring::CONNECTED))
                return invalid(DriverError::WIRING_REQUIRED, "affected input wiring must be declared");
        for (uint8_t i = 0; i < 2; ++i)
            if ((affectedOutputs & (1u << i)) && (prerequisites.outputWiring[i] == InputWiring::UNKNOWN || prerequisites.outputWiring[i] > InputWiring::CONNECTED))
                return invalid(DriverError::WIRING_REQUIRED, "affected output wiring must be declared");
        if (prerequisites.allowEchoReadback) {
            for (uint8_t i = 0; i < 4; ++i)
                if ((affectedInputs & (1u << i)) && prerequisites.inputWiring[i] != InputWiring::UNCONNECTED)
                    return invalid(DriverError::WIRING_REQUIRED, "unconfirmed echo verification requires affected terminals unconnected");
            for (uint8_t i = 0; i < 2; ++i)
                if ((affectedOutputs & (1u << i)) && prerequisites.outputWiring[i] != InputWiring::UNCONNECTED)
                    return invalid(DriverError::WIRING_REQUIRED, "unconfirmed echo verification requires affected terminals unconnected");
        }
        if (request.fields & bit(15)) {
            for (uint8_t i = 0; i < 2; ++i) {
                if (!((request.customOutput | previous.raw[15]) & (1u << i))) continue;
                const uint16_t function = request.fields & bit(13 + i) ? requested(request, 13 + i) : previous.raw[13 + i];
                if (function != 9 + i)
                    return invalid(DriverError::CUSTOM_DEPENDENCY, "custom bits require corresponding terminal's canonical custom function; cross-mapping unresolved");
            }
        }
    }
    const bool disabling = request.group == DriverGroup::DRIVE && (request.fields & bit(3)) && request.softLimitEnable == SoftLimitEnable::LIMITS_OFF;
    const bool enabling = request.group == DriverGroup::DRIVE && (request.fields & bit(3)) && request.softLimitEnable == SoftLimitEnable::AFTER_HOMING;
    if (request.group == DriverGroup::DRIVE && (request.fields & GEOMETRY_MASK) && !disabling &&
        (!(previous.knownFields & bit(3)) || previous.raw[3] != 0))
        return invalid(DriverError::LIMIT_DEPENDENCY, "geometry changes require known disabled limits or explicit disable");
    if (enabling) {
        const int64_t limit = 0x0FFFFFFF;
        if (request.fields & GEOMETRY_MASK)
            return invalid(DriverError::LIMIT_DEPENDENCY, "reconcile changed scale/order before enabling limits");
        if (!prerequisites.limitSemanticsQualified || !prerequisites.homedReferenceQualified ||
            !sameTarget(prerequisites.referenceTarget, target) || !previous.pairKnown ||
            prerequisites.referenceConfigurationGeneration != request.configurationGeneration ||
            !(prerequisites.rawMotion & static_cast<uint16_t>(MotionStatusBit::HOMING_COMPLETE)) ||
            (prerequisites.rawMotion & static_cast<uint16_t>(MotionStatusBit::RELEASED)) ||
            std::memcmp(prerequisites.qualifiedPositiveWords, previous.positiveWords, sizeof(previous.positiveWords)) ||
            std::memcmp(prerequisites.qualifiedNegativeWords, previous.negativeWords, sizeof(previous.negativeWords)) ||
            prerequisites.negativeLimit < -limit || prerequisites.positiveLimit > limit ||
            prerequisites.negativeLimit >= prerequisites.positiveLimit)
            return invalid(DriverError::LIMIT_REFERENCE_REQUIRED, "qualified ordered native limits and homed reference required");
    }
    initialize(output, target, operationId, request.configurationGeneration, nowUs, deadlineUs);
    output.group = request.group; output.kind = DriverKind::UPDATE; output.request = request; output.prerequisites = prerequisites;
    for (uint8_t i = 0; i < DRIVER_FIELD_COUNT; ++i) {
        DriverProgress& progress = output.progress[i];
        progress.field = static_cast<DriverField>(bit(i, request.group)); progress.reg = fieldRegister(request, i);
        progress.previous = previous.raw[i]; progress.requested = requested(request, i);
        progress.selected = (request.fields & bit(i, request.group)) != 0;
    }
    // Only an explicit OFF request can precede dependent changes. Never invent one.
    if (disabling) output.order[output.fieldCount++] = 3;
    for (uint8_t i = 0; i < DRIVER_FIELD_COUNT; ++i)
        if (output.progress[i].selected && !(disabling && i == 3))
            output.order[output.fieldCount++] = i;
    return Ok();
}
uint64_t driverWriteDeadline(const DriverContext& c) noexcept {
    return c.kind == DriverKind::UPDATE ? stationaryDeadline(c) : c.deadlineUs;
}

Status nextDriver(const DriverContext& c, uint64_t nowUs, PreparedDriver& output) noexcept {
    if (c.state == ReadState::EMPTY) return invalid(DriverError::INVALID_STATE, "driver operation is empty");
    if (nowUs < c.servicedUs) return invalid(DriverError::CLOCK_ERROR, "driver-settings clock moved backwards");
    PreparedDriver next; next.target = c.target; next.operationId = c.operationId; next.step = c.step;
    next.deadlineUs = c.deadlineUs;
    if (c.state != ReadState::ACTIVE) { output = next; return Ok(); }
    if (nowUs >= c.deadlineUs) return failure(DriverError::DEADLINE_EXPIRED, "driver-settings deadline expired");
    if (c.step >= totalSteps(c)) return invalid(DriverError::INVALID_STATE, "invalid driver-settings step");
    if (c.kind == DriverKind::READ) readWindow(c.group, c.step, next.reg, next.count, c.request.segmentIndex);
    else {
        const DriverProgress& progress = c.progress[c.order[c.step / 2]];
        next.reg = progress.reg; next.value = progress.requested; next.count = 1;
        next.write = (c.step % 2) == 0;
        if (next.write) {
            next.deadlineUs = driverWriteDeadline(c);
            if (nowUs >= next.deadlineUs)
                return failure(DriverError::STATIONARY_REQUIRED, "stopped-state evidence expired before settings write");
        }
    }
    next.kind = ActionWork::TRANSACTION;
    next.length = next.write ? buildWriteSingleRegister(c.target.address, next.reg, next.value, next.bytes, sizeof(next.bytes)) :
        buildReadRegisters(c.target.address, next.reg, next.count, next.bytes, sizeof(next.bytes));
    if (!next.length) return invalid(DriverError::INVALID_STATE, "invalid driver-settings request");
    output = next; return Ok();
}
Status advanceDriver(DriverContext& c, const ActionEvent& supplied, uint64_t nowUs) noexcept {
    const ReadEvent& event = supplied.transport;
    if (c.state != ReadState::ACTIVE || c.step >= totalSteps(c))
        return invalid(DriverError::INVALID_STATE, "driver operation is not active");
    if (!sameTarget(c.target, event.target) || c.operationId != event.operationId || c.step != event.step)
        return invalid(DriverError::WRONG_CORRELATION, "driver event correlation mismatch");
    if (nowUs < c.servicedUs) return invalid(DriverError::CLOCK_ERROR, "driver-settings clock moved backwards");
    const bool currentWrite = c.kind == DriverKind::UPDATE && (c.step % 2) == 0;
    const uint64_t transactionDeadline = currentWrite ? driverWriteDeadline(c) : c.deadlineUs;
    if (event.kind > ReadEventKind::DEADLINE || event.txAccepted > READ_REQUEST_LEN ||
        (supplied.txComplete && event.txAccepted != READ_REQUEST_LEN))
        return invalid(DriverError::INVALID_EVENT, "invalid driver transport envelope");
    if (event.kind == ReadEventKind::FRAME) {
        if (!event.frame || event.txAccepted != READ_REQUEST_LEN || !supplied.txComplete ||
            (event.qualified && (event.earliestUs < c.eligibleUs || event.earliestUs > event.latestUs || event.latestUs > nowUs)) ||
            (!event.qualified && (event.earliestUs || event.latestUs)))
            return invalid(DriverError::INVALID_EVENT, "invalid driver frame envelope");
    } else if ((!event.frame && event.length) || event.qualified || event.earliestUs || event.latestUs ||
               supplied.responseConfirmed || (event.kind == ReadEventKind::DEADLINE && nowUs < transactionDeadline))
        return invalid(DriverError::INVALID_EVENT, "invalid local driver event envelope");
    DriverEvidence evidence;
    evidence.step = c.step; evidence.event = event.kind; evidence.txAccepted = event.txAccepted;
    evidence.txComplete = supplied.txComplete; evidence.responseConfirmed = supplied.responseConfirmed;
    evidence.executionUnknown = event.executionUnknown; evidence.qualified = event.qualified;
    evidence.attemptedUs = c.eligibleUs;
    evidence.earliestUs = event.earliestUs; evidence.latestUs = event.latestUs; evidence.deliveredUs = nowUs;
    evidence.transportDetail = event.transportDetail; evidence.receivedLength = event.length;
    evidence.length = event.length < DRIVER_MAX_REPLY_BYTES ? event.length : DRIVER_MAX_REPLY_BYTES;
    if (evidence.length) std::memcpy(evidence.raw, event.frame, evidence.length);
    DriverProgress* progress = nullptr;
    if (c.kind == DriverKind::READ) readWindow(c.group, c.step, evidence.reg, evidence.count, c.request.segmentIndex);
    else {
        progress = &c.progress[c.order[c.step / 2]]; evidence.reg = progress->reg;
        evidence.count = 1; evidence.write = (c.step % 2) == 0;
    }
    uint16_t words[5] = {}; std::size_t count = 0;
    if (event.kind == ReadEventKind::FRAME)
        evidence.status = evidence.write ? parseWriteSingleRegister(event.frame, event.length, c.target.address,
            evidence.reg, progress->requested, &evidence.frameError) :
            parseRegisters(event.frame, event.length, c.target.address, evidence.count, words, 5, count, &evidence.frameError);
    else if (event.kind == ReadEventKind::TRANSPORT_FAILURE)
        evidence.status = failure(DriverError::TRANSPORT_FAILURE, "driver transport failed");
    else if (event.kind == ReadEventKind::CANCEL)
        evidence.status = failure(DriverError::CANCELLED, "driver update locally cancelled");
    else evidence.status = failure(DriverError::DEADLINE_EXPIRED, "driver-settings deadline expired");
    if (evidence.write) {
        if (event.txAccepted || event.executionUnknown) {
            c.effects |= static_cast<uint32_t>(progress->field); progress->execution = ActionExecution::UNKNOWN;
        }
        if (event.kind == ReadEventKind::FRAME && event.qualified &&
            event.latestUs <= transactionDeadline && supplied.responseConfirmed) {
            if (evidence.status) {
                progress->acknowledged = true; progress->execution = ActionExecution::ACKNOWLEDGED;
            } else if (evidence.status.code == Err::EXCEPTION && evidence.status.detail >= 1 && evidence.status.detail <= 7)
                progress->execution = ActionExecution::REJECTED;
        }
    }
    c.observations[c.step] = evidence; c.servicedUs = nowUs;
    if (event.kind == ReadEventKind::CANCEL) finish(c, DriverOutcome::CANCELLED, evidence.status);
    else if (event.kind == ReadEventKind::DEADLINE) finish(c, DriverOutcome::DEADLINE, evidence.status);
    else if (event.kind == ReadEventKind::TRANSPORT_FAILURE) finish(c, DriverOutcome::TRANSPORT_ERROR, evidence.status);
    else if (!event.qualified) finish(c, DriverOutcome::TIMING_UNQUALIFIED,
        failure(DriverError::TIMING_UNQUALIFIED, "driver frame closure timing is unqualified"));
    else if (event.latestUs > transactionDeadline) finish(c, DriverOutcome::DEADLINE,
        failure(DriverError::DEADLINE_EXPIRED, "driver frame closure exceeds transaction budget"));
    else if (!evidence.status) finish(c, DriverOutcome::REPLY_ERROR, evidence.status);
    else if (!supplied.responseConfirmed && !(evidence.write && (c.group == DriverGroup::IO || segmentGroup(c.group) || c.group == DriverGroup::CONTROL_SETTINGS || isTuningGroup(c.group)) && c.prerequisites.allowEchoReadback)) finish(c, DriverOutcome::UNCONFIRMED_RESPONSE,
        failure(DriverError::UNCONFIRMED_RESPONSE, "driver response source is not confirmed"));
    else {
        if (progress && !evidence.write) {
            progress->readbackKnown = true; progress->readback = words[0];
            if (words[0] != progress->requested) {
                finish(c, DriverOutcome::READBACK_MISMATCH,
                    failure(DriverError::READBACK_MISMATCH, "settings readback disagrees with requested value"));
            }
        }
        if (c.state == ReadState::ACTIVE) {
            ++c.completedSteps;
            if (c.completedSteps == totalSteps(c)) finish(c, DriverOutcome::SUCCESS, Ok());
            else if (nowUs >= c.deadlineUs) finish(c, DriverOutcome::DEADLINE,
                failure(DriverError::DEADLINE_EXPIRED, "no budget for another settings transaction"));
            else { ++c.step; c.eligibleUs = nowUs; }
        }
    }
    c.uncertain = unresolvedWrite(c);
    return Ok();
}
Status getDriver(const DriverContext& c, DriverObservation& output) noexcept {
    if (c.kind != DriverKind::READ || c.state != ReadState::SUCCEEDED || c.completedSteps != totalSteps(c))
        return invalid(DriverError::NOT_COMPLETE, "complete driver READ required");
    DriverObservation observation; observation.target = c.target; observation.operationId = c.operationId;
    observation.configurationGeneration = c.configurationGeneration;
    const Status status = decode(c.observations, c.target, c.group, observation, c.request.segmentIndex);
    if (!status) return status;
    output = observation; return Ok();
}
DriverField driverFieldAt(uint8_t index, DriverGroup group) noexcept {
    return static_cast<DriverField>(index < DRIVER_FIELD_COUNT && group <= DriverGroup::COLLISION ? bit(index, group) : 0);
}
Status prepareInputFunction(DriverRequest& request, uint8_t terminal, InputFunction function) noexcept {
    if (terminal >= 4 || !legal(8 + terminal, static_cast<uint16_t>(function)))
        return invalid(DriverError::INVALID_CANDIDATE, "invalid ESS input terminal or function");
    if ((request.group != DriverGroup::DRIVE && request.group != DriverGroup::IO) || request.fields & (FIELD_MASK | PAIR_MASK))
        return invalid(DriverError::INVALID_CANDIDATE, "cannot mix drive and IO settings");
    request.group = DriverGroup::IO; request.fields |= bit(8 + terminal);
    request.inputFunctions[terminal] = function; return Ok();
}
Status prepareOutputFunction(DriverRequest& request, uint8_t terminal, OutputFunction function) noexcept {
    if (terminal >= 2) return invalid(DriverError::INVALID_CANDIDATE, "invalid ESS output terminal");
    if (static_cast<uint16_t>(function) == 11)
        return Status(Err::UNSUPPORTED, static_cast<int32_t>(DriverError::OUTPUT_FUNCTION_UNRESOLVED), "custom output 2 terminal mapping unresolved");
    if (!legal(13 + terminal, static_cast<uint16_t>(function)) || ((request.group != DriverGroup::DRIVE && request.group != DriverGroup::IO) || (request.fields & (FIELD_MASK | PAIR_MASK))))
        return invalid(DriverError::INVALID_CANDIDATE, "invalid ESS output function or mixed settings");
    request.group = DriverGroup::IO; request.fields |= bit(13 + terminal);
    request.outputFunctions[terminal] = function; return Ok();
}
Status getIo(const DriverContext& c, IoObservation& output) noexcept {
    if (c.group != DriverGroup::IO) return invalid(DriverError::NOT_COMPLETE, "complete IO read required");
    DriverObservation driver;
    const Status status = getDriver(c, driver);
    if (!status) return status;
    IoObservation value; value.target = driver.target; value.operationId = driver.operationId;
    value.configurationGeneration = driver.configurationGeneration;
    value.inputPolarity = driver.raw[7]; value.outputPolarity = driver.raw[12]; value.customOutput = driver.raw[15];
    value.unknownInputPolarityBits = value.inputPolarity & ~0xFu;
    value.unknownOutputPolarityBits = value.outputPolarity & ~3u;
    value.unknownCustomOutputBits = value.customOutput & ~3u;
    for (uint8_t i = 0; i < 4; ++i) {
        value.rawInputFunctions[i] = driver.raw[8 + i];
        value.inputFunctions[i] = static_cast<InputFunction>(driver.raw[8 + i]);
        if (driver.knownFields & bit(8 + i)) value.knownInputFunctions |= 1u << i;
    }
    for (uint8_t i = 0; i < 2; ++i) {
        value.rawOutputFunctions[i] = driver.raw[13 + i];
        value.outputFunctions[i] = static_cast<OutputFunction>(driver.raw[13 + i]);
        if (driver.knownFields & bit(13 + i)) value.knownOutputFunctions |= 1u << i;
    }
    for (uint8_t i = 0; i < 3; ++i) value.provenance[i] = driver.provenance[i];
    output = value; return Ok();
}
}} // namespace MotorControlRS::ESS_RS
