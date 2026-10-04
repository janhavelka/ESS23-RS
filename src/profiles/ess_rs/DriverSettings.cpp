// SPDX-License-Identifier: MIT
#include "MotorControlRS/profiles/ess_rs/DriverSettings.h"
#include "MotorControlRS/profiles/ess_rs/Registers.h"
#include <cstring>
#include <limits>
#include <new>

namespace MotorControlRS { namespace ESS_RS {
namespace {
const uint16_t FIELD_MASK = 0x007F, PAIR_MASK = 0x0180, GEOMETRY_MASK = 0x0007;
const uint16_t REGS[DRIVER_FIELD_COUNT] = {
    Registers::DEFAULT_DIRECTION, Registers::SUBDIVISION, Registers::WORD_ORDER,
    Registers::SOFT_LIMIT_ENABLE, Registers::OVER_LIMIT_STOP,
    Registers::PV_TRIGGER_MODE, Registers::EXTERNAL_POSITION_MODE
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
uint16_t bit(uint8_t index) { return static_cast<uint16_t>(1u << index); }
bool legal(uint8_t index, uint16_t value) {
    return index == 1 ? value >= 400 && value <= 51200 : value <= 1;
}
uint16_t requested(const DriverRequest& request, uint8_t index) {
    switch (index) {
    case 0: return static_cast<uint16_t>(request.direction);
    case 1: return request.subdivision;
    case 2: return static_cast<uint16_t>(request.wordOrder);
    case 3: return static_cast<uint16_t>(request.softLimitEnable);
    case 4: return static_cast<uint16_t>(request.overLimitStop);
    case 5: return static_cast<uint16_t>(request.interruption);
    default: return static_cast<uint16_t>(request.positionMode);
    }
}
bool readWindow(uint8_t step, uint16_t& reg, uint16_t& count) {
    switch (step) {
    case 0: reg = Registers::DEFAULT_DIRECTION; count = 2; return true;
    case 1: reg = Registers::OVER_LIMIT_STOP; count = 3; return true;
    case 2: reg = Registers::POSITIVE_SOFT_LIMIT; count = 4; return true;
    case 3: reg = Registers::EXTERNAL_POSITION_MODE; count = 2; return true;
    default: return false;
    }
}
uint8_t totalSteps(const DriverContext& c) {
    return c.kind == DriverKind::READ ? DRIVER_READ_STEPS : static_cast<uint8_t>(c.fieldCount * 2);
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
    return until < c.deadlineUs ? until : c.deadlineUs;
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
Status decode(const DriverEvidence* evidence, const ReadTarget& target, DriverObservation& out) {
    uint16_t data[4][4] = {};
    for (uint8_t i = 0; i < DRIVER_READ_STEPS; ++i) {
        uint16_t reg = 0, count = 0; readWindow(i, reg, count);
        std::size_t decoded = 0;
        if (evidence[i].write || evidence[i].reg != reg || evidence[i].count != count ||
            !evidence[i].qualified || !evidence[i].status)
            return invalid(DriverError::NOT_COMPLETE, "driver read provenance is incomplete");
        const Status status = parseRegisters(evidence[i].raw, evidence[i].length,
            target.address, count, data[i], 4, decoded);
        if (!status) return status;
        out.provenance[i] = evidence[i];
    }
    out.raw[0] = data[0][0]; out.raw[1] = data[0][1];
    out.raw[4] = data[1][0]; out.raw[3] = data[1][1]; out.raw[2] = data[1][2];
    out.raw[6] = data[3][0]; out.raw[5] = data[3][1];
    for (uint8_t i = 0; i < DRIVER_FIELD_COUNT; ++i)
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
        if (p.selected && (p.execution == ActionExecution::UNKNOWN ||
            (p.execution == ActionExecution::ACKNOWLEDGED &&
             (!p.readbackKnown || p.readback != p.requested)))) return true;
    }
    return false;
}
} // namespace

Status prepareDriverRead(DriverContext& output, const ReadTarget& target, uint32_t operationId,
                         uint32_t configurationGeneration, uint64_t nowUs, uint64_t deadlineUs) noexcept {
    const Status status = validateIdentity(target, operationId, configurationGeneration, nowUs, deadlineUs);
    if (!status) return status;
    for (uint8_t i = 0; i < DRIVER_READ_STEPS; ++i) {
        uint16_t reg = 0, count = 0; readWindow(i, reg, count);
        const Status wire = validateReadRegistersRequest(target.address, reg, count);
        if (!wire) return wire;
    }
    initialize(output, target, operationId, configurationGeneration, nowUs, deadlineUs);
    return Ok();
}
Status prepareDriverSettings(DriverContext& output, const ReadTarget& target, uint32_t operationId,
                             const DriverRequest& request, const DriverPrerequisites& prerequisites,
                             uint64_t nowUs, uint64_t deadlineUs) noexcept {
    const Status initial = validateIdentity(target, operationId, request.configurationGeneration, nowUs, deadlineUs);
    if (!initial) return initial;
    if (&request == &output.request || &prerequisites == &output.prerequisites)
        return invalid(DriverError::INVALID_CANDIDATE, "preparation inputs must not alias output members");
    if (!request.fields || (request.fields & ~(FIELD_MASK | PAIR_MASK)))
        return invalid(DriverError::INVALID_CANDIDATE, "invalid or empty driver-settings selection");
    // Reject the entire candidate before producing even an otherwise legal single write.
    if (request.fields & PAIR_MASK)
        return Status(Err::UNSUPPORTED, static_cast<int32_t>(DriverError::PAIR_WRITE_UNSUPPORTED),
            "software-limit pair has no reviewed FC10 window; split writes are unavailable");
    for (uint8_t i = 0; i < DRIVER_FIELD_COUNT; ++i) {
        if (!(request.fields & bit(i))) continue;
        if (!legal(i, requested(request, i)))
            return invalid(DriverError::INVALID_CANDIDATE, "illegal driver-setting enum or native range");
        const Status wire = validateWriteSingleRegisterRequest(target.address, REGS[i], requested(request, i));
        if (!wire) return wire;
        const Status readback = validateReadRegistersRequest(target.address, REGS[i], 1);
        if (!readback) return readback;
    }
    const DriverObservation& previous = prerequisites.previous;
    if (request.configurationGeneration != prerequisites.configurationGeneration ||
        request.configurationGeneration != previous.configurationGeneration ||
        !sameTarget(previous.target, target) || !previous.operationId)
        return invalid(DriverError::STALE_SETTINGS, "previous driver-settings context does not match");
    DriverObservation checked;
    const Status decoded = decode(previous.provenance, target, checked);
    if (!decoded) return invalid(DriverError::STALE_SETTINGS, "previous driver-settings provenance is invalid");
    if (checked.knownFields != previous.knownFields || checked.pairKnown != previous.pairKnown ||
        checked.positiveBits != previous.positiveBits || checked.negativeBits != previous.negativeBits ||
        std::memcmp(checked.raw, previous.raw, sizeof(checked.raw)) ||
        std::memcmp(checked.positiveWords, previous.positiveWords, sizeof(checked.positiveWords)) ||
        std::memcmp(checked.negativeWords, previous.negativeWords, sizeof(checked.negativeWords)))
        return invalid(DriverError::STALE_SETTINGS, "previous settings disagree with checked raw provenance");
    if (!prerequisites.stationaryQualified || !prerequisites.inputsPermit ||
        !sameTarget(prerequisites.stationaryTarget, target) || !prerequisites.maxAgeUs ||
        prerequisites.stationaryEarliestUs > prerequisites.stationaryLatestUs ||
        prerequisites.stationaryLatestUs > nowUs ||
        nowUs - prerequisites.stationaryEarliestUs >= prerequisites.maxAgeUs ||
        prerequisites.rawAlarm || (prerequisites.rawMotion & 0xFFEC))
        return invalid(DriverError::STATIONARY_REQUIRED, "fresh qualified stopped-state and input policy required");
    for (uint8_t i = 0; i < DRIVER_READ_STEPS; ++i) {
        const DriverEvidence& e = previous.provenance[i];
        if (e.attemptedUs > e.earliestUs || e.earliestUs > e.latestUs || e.latestUs > nowUs ||
            nowUs - e.attemptedUs >= prerequisites.maxAgeUs)
            return invalid(DriverError::STALE_SETTINGS, "previous driver-settings observation is stale");
    }
    const bool disabling = (request.fields & bit(3)) && request.softLimitEnable == SoftLimitEnable::LIMITS_OFF;
    const bool enabling = (request.fields & bit(3)) && request.softLimitEnable == SoftLimitEnable::AFTER_HOMING;
    if ((request.fields & GEOMETRY_MASK) && !disabling &&
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
    output.kind = DriverKind::UPDATE; output.request = request; output.prerequisites = prerequisites;
    for (uint8_t i = 0; i < DRIVER_FIELD_COUNT; ++i) {
        DriverProgress& progress = output.progress[i];
        progress.field = static_cast<DriverField>(bit(i)); progress.reg = REGS[i];
        progress.previous = previous.raw[i]; progress.requested = requested(request, i);
        progress.selected = (request.fields & bit(i)) != 0;
    }
    // Only an explicit OFF request can precede dependent changes. Never invent one.
    if (disabling) output.order[output.fieldCount++] = 3;
    for (uint8_t i = 0; i < DRIVER_FIELD_COUNT; ++i)
        if (output.progress[i].selected && !(disabling && i == 3))
            output.order[output.fieldCount++] = i;
    return Ok();
}
Status nextDriver(const DriverContext& c, uint64_t nowUs, PreparedDriver& output) noexcept {
    if (c.state == ReadState::EMPTY) return invalid(DriverError::INVALID_STATE, "driver operation is empty");
    if (nowUs < c.servicedUs) return invalid(DriverError::CLOCK_ERROR, "driver-settings clock moved backwards");
    PreparedDriver next; next.target = c.target; next.operationId = c.operationId; next.step = c.step;
    next.deadlineUs = c.deadlineUs;
    if (c.state != ReadState::ACTIVE) { output = next; return Ok(); }
    if (nowUs >= c.deadlineUs) return failure(DriverError::DEADLINE_EXPIRED, "driver-settings deadline expired");
    if (c.step >= totalSteps(c)) return invalid(DriverError::INVALID_STATE, "invalid driver-settings step");
    if (c.kind == DriverKind::READ) readWindow(c.step, next.reg, next.count);
    else {
        const DriverProgress& progress = c.progress[c.order[c.step / 2]];
        next.reg = progress.reg; next.value = progress.requested; next.count = 1;
        next.write = (c.step % 2) == 0;
        if (next.write) {
            next.deadlineUs = stationaryDeadline(c);
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
    const uint64_t transactionDeadline = currentWrite ? stationaryDeadline(c) : c.deadlineUs;
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
    if (c.kind == DriverKind::READ) readWindow(c.step, evidence.reg, evidence.count);
    else {
        progress = &c.progress[c.order[c.step / 2]]; evidence.reg = progress->reg;
        evidence.count = 1; evidence.write = (c.step % 2) == 0;
    }
    uint16_t words[4] = {}; std::size_t count = 0;
    if (event.kind == ReadEventKind::FRAME)
        evidence.status = evidence.write ? parseWriteSingleRegister(event.frame, event.length, c.target.address,
            evidence.reg, progress->requested, &evidence.frameError) :
            parseRegisters(event.frame, event.length, c.target.address, evidence.count, words, 4, count, &evidence.frameError);
    else if (event.kind == ReadEventKind::TRANSPORT_FAILURE)
        evidence.status = failure(DriverError::TRANSPORT_FAILURE, "driver transport failed");
    else if (event.kind == ReadEventKind::CANCEL)
        evidence.status = failure(DriverError::CANCELLED, "driver update locally cancelled");
    else evidence.status = failure(DriverError::DEADLINE_EXPIRED, "driver-settings deadline expired");
    if (evidence.write) {
        if (event.txAccepted || event.executionUnknown) {
            c.effects |= static_cast<uint16_t>(progress->field); progress->execution = ActionExecution::UNKNOWN;
        }
        if (event.kind == ReadEventKind::FRAME && event.qualified && supplied.responseConfirmed) {
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
    else if (evidence.write && !supplied.responseConfirmed) finish(c, DriverOutcome::UNCONFIRMED_RESPONSE,
        failure(DriverError::UNCONFIRMED_RESPONSE, "settings echo source is not confirmed"));
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
    if (c.kind != DriverKind::READ || c.state != ReadState::SUCCEEDED || c.completedSteps != DRIVER_READ_STEPS)
        return invalid(DriverError::NOT_COMPLETE, "complete driver READ required");
    DriverObservation observation; observation.target = c.target; observation.operationId = c.operationId;
    observation.configurationGeneration = c.configurationGeneration;
    const Status status = decode(c.observations, c.target, observation);
    if (!status) return status;
    output = observation; return Ok();
}
}} // namespace MotorControlRS::ESS_RS
