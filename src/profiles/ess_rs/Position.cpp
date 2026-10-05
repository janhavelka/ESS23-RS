// SPDX-License-Identifier: MIT
#include "MotorControlRS/profiles/ess_rs/Position.h"
#include "MotorControlRS/profiles/ess_rs/Registers.h"
#include <cstring>
#include <limits>

namespace MotorControlRS { namespace ESS_RS {
namespace {
Status invalid(MoveError reason, const char* message) {
    return Status(Err::INVALID_CONFIG, static_cast<int32_t>(reason), message);
}
Status failed(MoveError reason, const char* message) {
    return Status(Err::ILLEGAL_VALUE, static_cast<int32_t>(reason), message);
}
bool sameTarget(const ReadTarget& a, const ReadTarget& b) {
    return a.id == b.id && a.address == b.address && a.generation == b.generation;
}
constexpr uint16_t RUNNING = static_cast<uint16_t>(MotionStatusBit::RUNNING);
constexpr uint16_t ARRIVED = static_cast<uint16_t>(MotionStatusBit::IN_POSITION);
constexpr uint16_t FAULTS = static_cast<uint16_t>(MotionStatusBit::ALARM) |
    static_cast<uint16_t>(MotionStatusBit::RELEASED) |
    static_cast<uint16_t>(MotionStatusBit::POSITIVE_SOFT_LIMIT) |
    static_cast<uint16_t>(MotionStatusBit::NEGATIVE_SOFT_LIMIT);
std::size_t requestLength(const MoveContext& c) { return c.step == 0 ? MOVE_REQUEST_BYTES : READ_REQUEST_LEN; }
uint64_t readinessDeadline(const MoveContext& c) {
    return evidenceAgeDeadline(c.prerequisites.observedUs, c.prerequisites.maximumAgeUs);
}
uint64_t stepDeadline(const MoveContext& c) {
    const uint64_t readiness = readinessDeadline(c);
    return c.step < 2 && readiness < c.deadlineUs ? readiness : c.deadlineUs;
}
Status expiredStep(const MoveContext& c) {
    return stepDeadline(c) < c.deadlineUs ?
        failed(MoveError::READINESS, "move readiness expired before write completion") :
        failed(MoveError::DEADLINE_EXPIRED, "move deadline expired");
}
void finish(MoveContext& c, ActionOutcome outcome, Status status) {
    c.outcome = outcome; c.status = status;
    c.state = (outcome == ActionOutcome::OBSERVED || outcome == ActionOutcome::ACKNOWLEDGED)
        ? ActionState::SUCCEEDED : ActionState::FAILED;
    // FC10 has no documented atomic-application guarantee, even on an exception.
    // A failed operation after any setup TX cannot establish untouched parameters.
    c.uncertain = c.state == ActionState::FAILED &&
        (c.stagingEvidence.txAccepted || c.stagingEvidence.executionUnknown ||
         c.triggerEvidence.txAccepted || c.triggerEvidence.executionUnknown);
}
void wait(MoveContext& c, uint64_t nowUs) {
    ++c.step;
    const uint64_t remaining = c.deadlineUs - nowUs;
    c.eligibleUs = c.options.pollIntervalUs >= remaining ? c.deadlineUs : nowUs + c.options.pollIntervalUs;
}
ActionExecution execution(const ActionEvidence& evidence) {
    if (!evidence.txAccepted && !evidence.executionUnknown) return ActionExecution::NOT_TRANSMITTED;
    if (evidence.event == ReadEventKind::FRAME && evidence.qualified && evidence.responseConfirmed) {
        if (evidence.status) return ActionExecution::ACKNOWLEDGED;
        if (evidence.status.code == Err::EXCEPTION && evidence.status.detail >= 1 && evidence.status.detail <= 7)
            return ActionExecution::REJECTED;
    }
    return ActionExecution::UNKNOWN;
}
void beginMove(MoveContext& c, const ReadTarget& target, uint32_t id,
               uint64_t now, uint64_t deadline, const ActionOptions& options) {
    c.target = target; c.operationId = id; c.options = options;
    c.state = ActionState::ACTIVE;
    c.startedUs = c.servicedUs = c.eligibleUs = now; c.deadlineUs = deadline;
}
uint16_t startValue(bool relative) {
    return static_cast<uint16_t>(MotionCommandBit::START_POSITION) |
        (relative ? 0 : static_cast<uint16_t>(MotionCommandBit::ABSOLUTE_POSITION));
}
} // namespace

std::size_t buildReadPositionProfile(uint8_t address, uint8_t* out, std::size_t capacity) noexcept {
    return buildReadRegisters(address, Registers::POSITION_START_SPEED, 6, out, capacity);
}
Status parsePositionProfile(const uint8_t* frame, std::size_t length, uint8_t address,
                            WordOrder order, PositionProfile& output) noexcept {
    uint16_t words[6] = {}; std::size_t count = 0;
    Status status = parseRegisters(frame, length, address, 6, words, 6, count);
    if (!status) return status;
    PositionProfile parsed;
    parsed.startSpeed = words[0]; parsed.accelerationTime = words[1];
    parsed.decelerationTime = words[2]; parsed.speed = words[3];
    status = decodeUint32(words + 4, 2, order, parsed.targetBits);
    if (!status) return status;
    output = parsed; return Ok();
}
std::size_t buildWritePositionProfile(uint8_t address, const PositionProfile& profile,
                                     WordOrder order, uint8_t* out, std::size_t capacity) noexcept {
    if (profile.accelerationTime > 2000 || profile.decelerationTime > 2000 || profile.speed > 3000)
        return 0;
    uint16_t words[5] = {profile.accelerationTime, profile.decelerationTime, profile.speed, 0, 0};
    if (!encodeUint32(profile.targetBits, order, words + 3, 2)) return 0;
    return buildWriteMultipleRegisters(address, Registers::POSITION_ACCELERATION_TIME, words, 5, out, capacity);
}
std::size_t buildStartPosition(uint8_t address, bool relative, uint8_t* out, std::size_t capacity) noexcept {
    return buildWriteSingleRegister(address, Registers::MOTION_COMMAND, startValue(relative), out, capacity);
}

static Status prepareNative(MoveContext& output, const PositionCommand& command,
        uint32_t id, uint32_t bits, bool relative, uint64_t now, uint64_t deadline) noexcept {
    if (!command.target.id || !command.target.generation || !isValidAddress(command.target.address))
        return invalid(MoveError::INVALID_TARGET, "invalid native position target");
    if (!id) return invalid(MoveError::INVALID_OPERATION, "zero move id");
    if (deadline <= now) return invalid(MoveError::INVALID_DEADLINE, "expired move deadline");
    if (!command.speedRpm || command.speedRpm > 3000 || command.accelerationTime > 2000 ||
        command.decelerationTime > 2000 || (relative && !bits))
        return invalid(MoveError::INVALID_REQUEST, "invalid native position parameters");
    MoveContext prepared;
    prepared.admission = MoveAdmission::NATIVE_INTENT;
    prepared.request.position.relative = relative;
    prepared.words[0] = command.accelerationTime; prepared.words[1] = command.decelerationTime;
    prepared.words[2] = command.speedRpm;
    const Status encoded = encodeUint32(bits, command.wordOrder, prepared.words + 3, 2);
    if (!encoded) return encoded;
    const Status access = validateWriteMultipleRegistersRequest(command.target.address,
        Registers::POSITION_ACCELERATION_TIME, prepared.words, 5);
    if (!access) return access;
    beginMove(prepared, command.target, id, now, deadline, ActionOptions());
    output = prepared; return Ok();
}
Status PositionCommand::prepareRelative(MoveContext& out, uint32_t id, uint32_t bits,
        uint64_t now, uint64_t deadline) const noexcept {
    return prepareNative(out, *this, id, bits, true, now, deadline);
}
Status PositionCommand::prepareAbsolute(MoveContext& out, uint32_t id, uint32_t bits,
        uint64_t now, uint64_t deadline) const noexcept {
    return prepareNative(out, *this, id, bits, false, now, deadline);
}

static Status prepareMove(MoveContext& output, const AxisConfig& axis, const AxisReference* reference,
        uint32_t operationId, const MoveRequest& request, const MovePrerequisites& prerequisites,
        uint64_t nowUs, uint64_t deadlineUs, const ActionOptions& options) noexcept {
    if (!axis.target.id || !axis.target.generation || !isValidAddress(axis.target.address))
        return invalid(MoveError::INVALID_TARGET, "invalid move target");
    if (!operationId) return invalid(MoveError::INVALID_OPERATION, "zero move id");
    if (deadlineUs <= nowUs) return invalid(MoveError::INVALID_DEADLINE, "expired move deadline");
    if (!options.pollIntervalUs || !options.maxPolls || options.maxPolls > ACTION_MAX_POLLS)
        return invalid(MoveError::INVALID_OPTIONS, "invalid bounded move observation policy");
    if (request.position.relative && request.position.basis != RelativeBasis::ACTUAL)
        return Status(Err::UNSUPPORTED, static_cast<int32_t>(MoveError::UNRESOLVED_BASIS),
            "ESS finite relative move requires the verified actual-position basis");
    if (!sameTarget(axis.target, prerequisites.target) ||
        axis.generation != prerequisites.configurationGeneration)
        return invalid(MoveError::STALE_CONFIGURATION, "move prerequisite binding mismatch");
    if (!prerequisites.commandUnitsVerified)
        return invalid(MoveError::UNRESOLVED_UNITS, "device command increment interpretation is unresolved");
    if (request.position.relative && !prerequisites.relativeBasisVerified)
        return invalid(MoveError::UNRESOLVED_BASIS, "relative basis is unresolved for this configuration");
    // An unwrapped absolute device-native target needs no current-position
    // reference. Arithmetic/limits decide when metadata is actually required;
    // absent reference leaves displacement unknown rather than inventing one.
    if (!request.position.relative && reference && (!reference->nativeKnown || reference->basis != RelativeBasis::ACTUAL || !reference->stationary))
        return invalid(MoveError::READINESS, "supplied move reference must be stationary actual command coordinates");
    if (request.ramp != MoveRamp::VERIFIED_CONFIGURED || !prerequisites.configuredRampVerified ||
        prerequisites.accelerationTime > 2000 || prerequisites.decelerationTime > 2000)
        return invalid(MoveError::UNRESOLVED_RAMP, "verified configured native ramps are required");
    if (!request.speedRpm || request.speedRpm > 3000 || !prerequisites.startSpeedKnown ||
        prerequisites.startSpeed > request.speedRpm)
        return invalid(MoveError::INVALID_REQUEST, "invalid speed or unresolved configured start speed");
    if (!prerequisites.wordOrderKnown || prerequisites.wordOrder > WordOrder::LOW_WORD_FIRST)
        return invalid(MoveError::STALE_CONFIGURATION, "move word order is unresolved");
    if (!prerequisites.readinessQualified || !prerequisites.serialInputsPermit ||
        !evidenceAgeValid(prerequisites.observedUs, nowUs, prerequisites.maximumAgeUs) ||
        prerequisites.rawAlarm || (prerequisites.rawMotion & (FAULTS | RUNNING)))
        return invalid(MoveError::READINESS, "fresh enabled stationary alarm-free serial readiness is required");
    MoveContext prepared;
    // Position preparation consumes only an established native reference. Its
    // cached nowUs must not substitute for this operation's admission time.
    AxisReference currentReference;
    if (reference && reference->nativeKnown) {
        currentReference = *reference; currentReference.nowUs = nowUs;
        reference = &currentReference;
    }
    const Status arithmetic = preparePosition(request.position, axis, reference, prepared.prepared);
    if (!arithmetic) return arithmetic;
    if (prepared.prepared.endpointKnown && reference && reference->nativeKnown) {
        const uint64_t readinessEnd = evidenceAgeDeadline(prerequisites.observedUs, prerequisites.maximumAgeUs);
        const uint64_t writeEnd = readinessEnd < deadlineUs ? readinessEnd : deadlineUs;
        if (evidenceAgeDeadline(reference->observedUs, reference->maximumAgeUs) < writeEnd)
            return invalid(MoveError::READINESS, "native reference freshness must cover both write budgets");
        prepared.reference = *reference;
    }
    if (prepared.prepared.zeroDisplacement)
        return failed(MoveError::INVALID_REQUEST, "effective relative displacement is zero");
    const int64_t native = prepared.prepared.effectiveNative;
    if (native < std::numeric_limits<int32_t>::min() || native > std::numeric_limits<int32_t>::max())
        return failed(MoveError::INVALID_REQUEST, "target exceeds the supported signed 32-bit subset");
    if (native < 0 && !prerequisites.negativeTwosComplementVerified)
        return invalid(MoveError::UNRESOLVED_SIGN, "negative ESS position encoding is unresolved");
    prepared.words[0] = prerequisites.accelerationTime;
    prepared.words[1] = prerequisites.decelerationTime;
    prepared.words[2] = request.speedRpm;
    const Status pair = encodeInt32(static_cast<int32_t>(native), prerequisites.wordOrder, prepared.words + 3, 2);
    if (!pair) return pair;
    const Status access = validateWriteMultipleRegistersRequest(axis.target.address,
        Registers::POSITION_ACCELERATION_TIME, prepared.words, 5);
    if (!access) return access;
    prepared.request = request; prepared.prerequisites = prerequisites;
    beginMove(prepared, axis.target, operationId, nowUs, deadlineUs, options);
    output = prepared;
    return Ok();
}

Status prepareMoveRelative(MoveContext& output, const AxisConfig& axis, const AxisReference* reference,
        uint32_t id, const MoveRequest& request, const MovePrerequisites& prerequisites,
        uint64_t now, uint64_t deadline, const ActionOptions& options) noexcept {
    if (!request.position.relative || request.position.wrapped)
        return invalid(MoveError::INVALID_REQUEST, "relative preparation requires an unwrapped relative request");
    return prepareMove(output, axis, reference, id, request, prerequisites, now, deadline, options);
}
Status prepareMoveAbsolute(MoveContext& output, const AxisConfig& axis, const AxisReference* reference,
        uint32_t id, const MoveRequest& request, const MovePrerequisites& prerequisites,
        uint64_t now, uint64_t deadline, const ActionOptions& options) noexcept {
    if (request.position.relative || request.position.wrapped)
        return invalid(MoveError::INVALID_REQUEST, "absolute preparation requires an unwrapped absolute request");
    return prepareMove(output, axis, reference, id, request, prerequisites, now, deadline, options);
}
Status prepareMoveAngle(MoveContext& output, const AxisConfig& axis, const AxisReference* reference,
        uint32_t id, const MoveRequest& request, const MovePrerequisites& prerequisites,
        uint64_t now, uint64_t deadline, const ActionOptions& options) noexcept {
    if (request.position.relative || !request.position.wrapped)
        return invalid(MoveError::INVALID_REQUEST, "angle preparation requires an explicit wrapped request");
    return prepareMove(output, axis, reference, id, request, prerequisites, now, deadline, options);
}

Status nextMove(const MoveContext& c, uint64_t nowUs, PreparedMove& output) noexcept {
    if (c.state == ActionState::EMPTY) return invalid(MoveError::INVALID_STATE, "move is empty");
    if (nowUs < c.servicedUs) return invalid(MoveError::CLOCK_ERROR, "move clock moved backwards");
    PreparedMove next;
    next.target = c.target; next.operationId = c.operationId; next.step = c.step;
    next.deadlineUs = stepDeadline(c); next.eligibleUs = c.eligibleUs;
    if (c.state != ActionState::ACTIVE) { output = next; return Ok(); }
    if (nowUs >= c.deadlineUs) return failed(MoveError::DEADLINE_EXPIRED, "move deadline expired");
    if (nowUs >= next.deadlineUs) return expiredStep(c);
    if (nowUs < c.eligibleUs) { next.kind = ActionWork::WAIT; output = next; return Ok(); }
    next.kind = ActionWork::TRANSACTION; next.write = c.step < 2;
    if (c.step == 0) {
        next.function = 16; next.reg = Registers::POSITION_ACCELERATION_TIME; next.count = 5;
        next.length = buildWriteMultipleRegisters(c.target.address, next.reg, c.words, next.count,
            next.bytes, sizeof(next.bytes));
    } else if (c.step == 1) {
        next.function = 6; next.reg = Registers::MOTION_COMMAND;
        next.value = startValue(c.request.position.relative); next.count = 1;
        next.length = buildStartPosition(c.target.address, c.request.position.relative, next.bytes, sizeof(next.bytes));
    } else {
        next.function = 3; next.reg = Registers::ERROR_CODE; next.count = 2;
        next.length = buildReadRegisters(c.target.address, next.reg, next.count, next.bytes, sizeof(next.bytes));
    }
    if (!next.length) return invalid(MoveError::INVALID_STATE, "invalid move frame");
    output = next;
    return Ok();
}

Status advanceMove(MoveContext& c, const ActionEvent& supplied, uint64_t nowUs) noexcept {
    const ReadEvent& event = supplied.transport;
    if (c.state != ActionState::ACTIVE) return invalid(MoveError::INVALID_STATE, "move is not active");
    if (!sameTarget(c.target, event.target) || c.operationId != event.operationId || c.step != event.step)
        return invalid(MoveError::WRONG_CORRELATION, "move event correlation mismatch");
    if (nowUs < c.servicedUs) return invalid(MoveError::CLOCK_ERROR, "move clock moved backwards");
    const std::size_t expectedTx = requestLength(c);
    if (event.kind > ReadEventKind::DEADLINE || event.txAccepted > expectedTx ||
        (supplied.txComplete && event.txAccepted != expectedTx))
        return invalid(MoveError::INVALID_EVENT, "invalid move transport envelope");
    if (event.kind == ReadEventKind::FRAME) {
        if (!event.frame || event.txAccepted != expectedTx || !supplied.txComplete ||
            (event.qualified && (event.earliestUs < c.eligibleUs || event.earliestUs > event.latestUs || event.latestUs > nowUs)) ||
            (!event.qualified && (event.earliestUs || event.latestUs)))
            return invalid(MoveError::INVALID_EVENT, "invalid move frame envelope");
    } else if ((!event.frame && event.length) || event.qualified || event.earliestUs || event.latestUs ||
        supplied.responseConfirmed || (event.kind == ReadEventKind::DEADLINE && nowUs < stepDeadline(c)))
        return invalid(MoveError::INVALID_EVENT, "invalid local move event envelope");
    ActionEvidence evidence;
    evidence.step = c.step; evidence.event = event.kind; evidence.txAccepted = event.txAccepted;
    evidence.txComplete = supplied.txComplete; evidence.responseConfirmed = supplied.responseConfirmed;
    evidence.executionUnknown = event.executionUnknown; evidence.qualified = event.qualified;
    evidence.earliestUs = event.earliestUs; evidence.latestUs = event.latestUs;
    evidence.deliveredUs = nowUs; evidence.transportDetail = event.transportDetail;
    evidence.receivedLength = event.length;
    evidence.length = event.length < ACTION_MAX_REPLY_BYTES ? event.length : ACTION_MAX_REPLY_BYTES;
    if (evidence.length) std::memcpy(evidence.raw, event.frame, evidence.length);
    uint16_t words[2] = {}; std::size_t count = 0;
    if (event.kind == ReadEventKind::FRAME) {
        if (c.step == 0) evidence.status = parseWriteMultipleRegisters(event.frame, event.length, c.target.address,
            Registers::POSITION_ACCELERATION_TIME, 5, &evidence.frameError);
        else if (c.step == 1) evidence.status = parseWriteSingleRegister(event.frame, event.length, c.target.address,
            Registers::MOTION_COMMAND, startValue(c.request.position.relative), &evidence.frameError);
        else evidence.status = parseRegisters(event.frame, event.length, c.target.address, 2,
            words, 2, count, &evidence.frameError);
    } else if (event.kind == ReadEventKind::CANCEL)
        evidence.status = failed(MoveError::CANCELLED, "move locally cancelled");
    else if (event.kind == ReadEventKind::DEADLINE)
        evidence.status = expiredStep(c);
    else evidence.status = failed(MoveError::TRANSPORT_FAILURE, "move transport failed");
    if (c.step == 0) {
        c.stagingEvidence = evidence; c.setupExecution = execution(evidence);
        c.stagingApplied = c.setupExecution == ActionExecution::ACKNOWLEDGED;
    } else if (c.step == 1) { c.triggerEvidence = evidence; c.execution = execution(evidence); }
    c.servicedUs = nowUs;
    if (event.kind == ReadEventKind::CANCEL) finish(c, ActionOutcome::CANCELLED, evidence.status);
    else if (event.kind == ReadEventKind::DEADLINE) finish(c, ActionOutcome::DEADLINE, evidence.status);
    else if (event.kind == ReadEventKind::TRANSPORT_FAILURE) finish(c, ActionOutcome::TRANSPORT_ERROR, evidence.status);
    else if (!event.qualified) finish(c, ActionOutcome::TIMING_UNQUALIFIED,
        failed(MoveError::TIMING_UNQUALIFIED, "move closure timing is unqualified"));
    else if (event.latestUs > stepDeadline(c)) finish(c, ActionOutcome::DEADLINE, expiredStep(c));
    else if (!evidence.status) finish(c, ActionOutcome::REPLY_ERROR, evidence.status);
    else if (!supplied.responseConfirmed && (c.step != 1 || c.admission == MoveAdmission::NATIVE_INTENT))
        finish(c, ActionOutcome::UNCONFIRMED_RESPONSE,
        failed(MoveError::UNCONFIRMED_RESPONSE, "frame source is not confirmed as the drive"));
    else if (c.step == 1 && c.admission == MoveAdmission::NATIVE_INTENT)
        finish(c, ActionOutcome::ACKNOWLEDGED, Ok());
    else if (c.step < 2) {
        if (nowUs >= c.deadlineUs) finish(c, ActionOutcome::DEADLINE,
            failed(MoveError::DEADLINE_EXPIRED, "no budget for the next move step"));
        else if (c.step == 0 && nowUs >= stepDeadline(c))
            finish(c, ActionOutcome::DEADLINE, expiredStep(c));
        else if (c.step == 0) { ++c.step; c.eligibleUs = nowUs; }
        else wait(c, nowUs);
    } else {
        ++c.polls; c.lastObservation = evidence; c.observationKnown = true;
        c.rawAlarm = words[0]; c.rawMotion = words[1];
        if (words[0] || (words[1] & FAULTS)) finish(c, ActionOutcome::REPLY_ERROR,
            failed(MoveError::DRIVE_FAULT, "drive alarm, release or limit interrupted the move"));
        else if (words[1] & RUNNING) {
            if (!c.runningObserved) c.activityEvidence = evidence;
            c.runningObserved = true;
        } else if (c.runningObserved && (words[1] & ARRIVED)) {
            c.completion = ActionCompletion::OBSERVED;
            finish(c, ActionOutcome::OBSERVED, Ok());
        }
        if (c.state == ActionState::ACTIVE) {
            if (c.polls >= c.options.maxPolls) finish(c, ActionOutcome::OBSERVATION_LIMIT,
                failed(MoveError::OBSERVATION_LIMIT, "fresh move completion was not observed"));
            else if (nowUs >= c.deadlineUs) finish(c, ActionOutcome::DEADLINE,
                failed(MoveError::DEADLINE_EXPIRED, "no budget for another move observation"));
            else wait(c, nowUs);
        }
    }
    if (c.state == ActionState::FAILED) c.failureEvidence = evidence;
    return Ok();
}
}} // namespace MotorControlRS::ESS_RS
