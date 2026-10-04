// SPDX-License-Identifier: MIT
#include "MotorControlRS/profiles/ess_rs/Homing.h"
#include "MotorControlRS/profiles/ess_rs/Registers.h"
#include <cstring>
#include <limits>

namespace MotorControlRS { namespace ESS_RS {
namespace {
constexpr uint16_t RUNNING = static_cast<uint16_t>(MotionStatusBit::RUNNING);
constexpr uint16_t HOMED = static_cast<uint16_t>(MotionStatusBit::HOMING_COMPLETE);
constexpr uint16_t ARRIVED = static_cast<uint16_t>(MotionStatusBit::IN_POSITION);
constexpr uint16_t FAULTS = static_cast<uint16_t>(MotionStatusBit::ALARM) |
    static_cast<uint16_t>(MotionStatusBit::RELEASED) |
    static_cast<uint16_t>(MotionStatusBit::POSITIVE_SOFT_LIMIT) |
    static_cast<uint16_t>(MotionStatusBit::NEGATIVE_SOFT_LIMIT);
constexpr const char* SWITCH_PENDING = "method-specific switch-edge/return verification and qualified existing inputs are not implemented";
constexpr const char* COLLISION_UNRESOLVED = "negative method encoding, collision trajectory and conflicting parameter mappings are unresolved";
// Explicit descriptors; numeric membership never enables an operation.
const HomeMethodDescriptor METHODS[HOME_METHOD_COUNT] = {
    {HomingMethod::COLLISION_MINUS_4, HomeSupport::UNRESOLVED, 0, false, true, COLLISION_UNRESOLVED},
    {HomingMethod::COLLISION_MINUS_3, HomeSupport::UNRESOLVED, 0, false, true, COLLISION_UNRESOLVED},
    {HomingMethod::COLLISION_MINUS_2, HomeSupport::UNRESOLVED, 0, false, true, COLLISION_UNRESOLVED},
    {HomingMethod::COLLISION_MINUS_1, HomeSupport::UNRESOLVED, 0, false, true, COLLISION_UNRESOLVED},
    {HomingMethod::METHOD_1, HomeSupport::UNIMPLEMENTED, 4, true, true, SWITCH_PENDING},
    {HomingMethod::METHOD_2, HomeSupport::UNIMPLEMENTED, 2, true, true, SWITCH_PENDING},
    {HomingMethod::METHOD_3, HomeSupport::UNIMPLEMENTED, 1, true, true, SWITCH_PENDING},
    {HomingMethod::METHOD_4, HomeSupport::UNIMPLEMENTED, 1, true, true, SWITCH_PENDING},
    {HomingMethod::METHOD_5, HomeSupport::UNIMPLEMENTED, 1, true, true, SWITCH_PENDING},
    {HomingMethod::METHOD_6, HomeSupport::UNIMPLEMENTED, 1, true, true, SWITCH_PENDING},
    {HomingMethod::METHOD_7, HomeSupport::UNIMPLEMENTED, 3, true, true, SWITCH_PENDING},
    {HomingMethod::METHOD_8, HomeSupport::UNIMPLEMENTED, 3, true, true, SWITCH_PENDING},
    {HomingMethod::METHOD_9, HomeSupport::UNIMPLEMENTED, 3, true, true, SWITCH_PENDING},
    {HomingMethod::METHOD_10, HomeSupport::UNIMPLEMENTED, 3, true, true, SWITCH_PENDING},
    {HomingMethod::METHOD_11, HomeSupport::UNIMPLEMENTED, 5, true, true, SWITCH_PENDING},
    {HomingMethod::METHOD_12, HomeSupport::UNIMPLEMENTED, 5, true, true, SWITCH_PENDING},
    {HomingMethod::METHOD_13, HomeSupport::UNIMPLEMENTED, 5, true, true, SWITCH_PENDING},
    {HomingMethod::METHOD_14, HomeSupport::UNIMPLEMENTED, 5, true, true, SWITCH_PENDING},
    {HomingMethod::METHOD_17, HomeSupport::UNIMPLEMENTED, 4, false, true, SWITCH_PENDING},
    {HomingMethod::METHOD_18, HomeSupport::UNRESOLVED, 2, false, true, "positive-limit prose conflicts with negative-limit diagram labels"},
    {HomingMethod::METHOD_19, HomeSupport::UNIMPLEMENTED, 1, false, true, SWITCH_PENDING},
    {HomingMethod::METHOD_20, HomeSupport::UNIMPLEMENTED, 1, false, true, SWITCH_PENDING},
    {HomingMethod::METHOD_21, HomeSupport::UNIMPLEMENTED, 1, false, true, SWITCH_PENDING},
    {HomingMethod::METHOD_22, HomeSupport::UNIMPLEMENTED, 1, false, true, SWITCH_PENDING},
    {HomingMethod::METHOD_23, HomeSupport::UNIMPLEMENTED, 3, false, true, SWITCH_PENDING},
    {HomingMethod::METHOD_24, HomeSupport::UNIMPLEMENTED, 3, false, true, SWITCH_PENDING},
    {HomingMethod::METHOD_25, HomeSupport::UNIMPLEMENTED, 3, false, true, SWITCH_PENDING},
    {HomingMethod::METHOD_26, HomeSupport::UNIMPLEMENTED, 3, false, true, SWITCH_PENDING},
    {HomingMethod::METHOD_27, HomeSupport::UNIMPLEMENTED, 5, false, true, SWITCH_PENDING},
    {HomingMethod::METHOD_28, HomeSupport::UNIMPLEMENTED, 5, false, true, SWITCH_PENDING},
    {HomingMethod::METHOD_29, HomeSupport::UNIMPLEMENTED, 5, false, true, SWITCH_PENDING},
    {HomingMethod::METHOD_30, HomeSupport::UNIMPLEMENTED, 5, false, true, SWITCH_PENDING},
    {HomingMethod::METHOD_33, HomeSupport::IMPLEMENTED, 0, true, true, "negative low-speed internal Z search; qualified mechanics and index required"},
    {HomingMethod::METHOD_34, HomeSupport::IMPLEMENTED, 0, true, true, "positive low-speed internal Z search; qualified mechanics and index required"},
    {HomingMethod::METHOD_35, HomeSupport::IMPLEMENTED, 0, false, false, "current mechanical position as origin; no search trajectory"}
};
Status invalid(HomeError reason, const char* message) {
    return Status(Err::INVALID_CONFIG, static_cast<int32_t>(reason), message);
}
Status failed(HomeError reason, const char* message) {
    return Status(Err::ILLEGAL_VALUE, static_cast<int32_t>(reason), message);
}
bool sameTarget(const ReadTarget& a, const ReadTarget& b) {
    return a.id == b.id && a.address == b.address && a.generation == b.generation;
}
uint64_t stepDeadline(const HomeContext& c) {
    const auto& p = c.prerequisites;
    const uint64_t ageEnd = p.maximumAgeUs > std::numeric_limits<uint64_t>::max() - p.observedUs ?
        std::numeric_limits<uint64_t>::max() : p.observedUs + p.maximumAgeUs;
    return c.step < 2 && ageEnd < c.deadlineUs ? ageEnd : c.deadlineUs;
}
Status expired(const HomeContext& c) {
    return stepDeadline(c) < c.deadlineUs ? failed(HomeError::READINESS, "home readiness expired before write completion") :
        failed(HomeError::DEADLINE_EXPIRED, "home deadline expired");
}
void finish(HomeContext& c, ActionOutcome outcome, Status status) {
    c.outcome = outcome; c.status = status;
    c.state = outcome == ActionOutcome::OBSERVED ? ActionState::SUCCEEDED : ActionState::FAILED;
    c.uncertain = c.state == ActionState::FAILED &&
        (c.stagingEvidence.txAccepted || c.stagingEvidence.executionUnknown ||
         c.triggerEvidence.txAccepted || c.triggerEvidence.executionUnknown);
}
void wait(HomeContext& c, uint64_t nowUs) {
    ++c.step;
    c.eligibleUs = c.options.pollIntervalUs >= c.deadlineUs - nowUs ? c.deadlineUs : nowUs + c.options.pollIntervalUs;
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
} // namespace
const HomeMethodDescriptor* homeMethodAt(uint8_t index) noexcept {
    return index < HOME_METHOD_COUNT ? METHODS + index : nullptr;
}
const HomeMethodDescriptor* homeMethod(HomingMethod method) noexcept {
    for (const auto& descriptor : METHODS) if (descriptor.method == method) return &descriptor;
    return nullptr;
}

Status prepareHome(HomeContext& output, const AxisConfig& axis, uint32_t id,
        const HomeRequest& request, const HomePrerequisites& p, uint64_t nowUs,
        uint64_t deadlineUs, const ActionOptions& options) noexcept {
    if (!axis.target.id || !axis.target.generation || !isValidAddress(axis.target.address))
        return invalid(HomeError::INVALID_TARGET, "invalid home target");
    if (!id) return invalid(HomeError::INVALID_OPERATION, "zero home id");
    if (deadlineUs <= nowUs) return invalid(HomeError::INVALID_DEADLINE, "expired home deadline");
    if (!options.pollIntervalUs || !options.maxPolls || options.maxPolls > ACTION_MAX_POLLS)
        return invalid(HomeError::INVALID_OPTIONS, "invalid bounded home observation policy");
    const auto* method = homeMethod(request.method);
    if (!method) return invalid(HomeError::INVALID_METHOD, "undocumented home method");
    if (method->support != HomeSupport::IMPLEMENTED)
        return Status(Err::UNSUPPORTED, static_cast<int32_t>(method->support == HomeSupport::UNRESOLVED ?
            HomeError::METHOD_UNRESOLVED : HomeError::METHOD_UNIMPLEMENTED), method->reason);
    if (!axis.generation || !sameTarget(axis.target, p.target) || axis.generation != p.configurationGeneration ||
        request.configurationGeneration != axis.generation)
        return invalid(HomeError::STALE_CONFIGURATION, "home target/configuration binding mismatch");
    if (request.offset != 0 || !p.zeroOffsetQualified)
        return Status(Err::UNSUPPORTED, static_cast<int32_t>(HomeError::UNRESOLVED_OFFSET),
            "only qualified zero offset is implemented; nonzero order, sign and scale remain unresolved");
    if (!p.auxiliaryQualified || p.auxiliary != HomingAuxiliary::KEEP_POSITION_SET_ZERO)
        return invalid(HomeError::AUXILIARY_REQUIRED, "existing active home auxiliary 7 must be qualified");
    if (request.searchSpeed < 5 || request.searchSpeed > 3000 || request.returnSpeed < 5 || request.returnSpeed > 300 ||
        request.rampTime < 30 || request.rampTime > 2000)
        return invalid(HomeError::INVALID_PARAMETERS, "home parameters exceed the conservative admitted raw subset");
    if (request.method != p.qualifiedMethod || request.searchSpeed != p.qualifiedSearchSpeed ||
        request.returnSpeed != p.qualifiedReturnSpeed || request.rampTime != p.qualifiedRampTime)
        return invalid(HomeError::QUALIFICATION_MISMATCH, "home method and native words differ from the qualified request");
    if (!p.nativeRatesQualified) return invalid(HomeError::UNRESOLVED_RATES, "home native rate interpretation is unresolved");
    if (!p.nativeRampQualified) return invalid(HomeError::UNRESOLVED_RAMP, "home native ramp interpretation is unresolved");
    if (!p.inputsQualified || (p.availableInputs & method->requiredInputs) != method->requiredInputs)
        return invalid(HomeError::INPUT_REQUIRED, "existing inputs and interfering assignments must be qualified");
    if (method->requiresIndex && !p.indexQualified)
        return invalid(HomeError::INDEX_REQUIRED, "qualified closed-loop motor Z index is required");
    if (!p.methodQualified || !p.readinessQualified || !p.maximumAgeUs || p.observedUs > nowUs ||
        nowUs - p.observedUs >= p.maximumAgeUs || p.rawAlarm || (p.rawMotion & (FAULTS | RUNNING)))
        return invalid(HomeError::READINESS, "fresh enabled stationary alarm-free qualified home readiness is required");
    HomeContext c;
    c.target = axis.target; c.operationId = id; c.request = request; c.prerequisites = p;
    c.options = options; c.state = ActionState::ACTIVE;
    c.startedUs = c.servicedUs = c.eligibleUs = nowUs; c.deadlineUs = deadlineUs;
    c.words[0] = static_cast<uint16_t>(request.method); c.words[1] = request.searchSpeed;
    c.words[2] = request.returnSpeed; c.words[3] = request.rampTime;
    // Zero in both words requires neither unresolved offset order nor sign encoding.
    c.homedLowObserved = !method->moves && !(p.rawMotion & HOMED);
    const Status access = validateWriteMultipleRegistersRequest(c.target.address, Registers::HOMING_METHOD, c.words, 6);
    if (!access) return access;
    output = c; return Ok();
}

Status nextHome(const HomeContext& c, uint64_t nowUs, PreparedHome& output) noexcept {
    if (c.state == ActionState::EMPTY) return invalid(HomeError::INVALID_STATE, "home is empty");
    if (nowUs < c.servicedUs) return invalid(HomeError::CLOCK_ERROR, "home clock moved backwards");
    PreparedHome next;
    next.target = c.target; next.operationId = c.operationId; next.step = c.step;
    next.deadlineUs = stepDeadline(c); next.eligibleUs = c.eligibleUs;
    if (c.state != ActionState::ACTIVE) { output = next; return Ok(); }
    if (nowUs >= next.deadlineUs) return expired(c);
    if (nowUs < c.eligibleUs) { next.kind = ActionWork::WAIT; output = next; return Ok(); }
    next.kind = ActionWork::TRANSACTION; next.write = c.step < 2;
    if (c.step == 0) {
        next.function = 16; next.reg = Registers::HOMING_METHOD; next.count = 6;
        next.length = buildWriteMultipleRegisters(c.target.address, next.reg, c.words, 6, next.bytes, sizeof(next.bytes));
    } else if (c.step == 1) {
        next.function = 6; next.reg = Registers::MOTION_COMMAND; next.value = 16; next.count = 1;
        next.length = buildWriteSingleRegister(c.target.address, next.reg, next.value, next.bytes, sizeof(next.bytes));
    } else {
        next.function = 3; next.reg = c.phase == HomePhase::ZERO_CHECK ? Registers::CURRENT_POSITION : Registers::ERROR_CODE;
        next.count = 2;
        next.length = buildReadRegisters(c.target.address, next.reg, 2, next.bytes, sizeof(next.bytes));
    }
    if (!next.length) return invalid(HomeError::INVALID_STATE, "invalid home frame");
    output = next; return Ok();
}

Status advanceHome(HomeContext& c, const ActionEvent& supplied, uint64_t nowUs) noexcept {
    const auto& event = supplied.transport;
    if (c.state != ActionState::ACTIVE) return invalid(HomeError::INVALID_STATE, "home is not active");
    if (!sameTarget(c.target, event.target) || c.operationId != event.operationId || c.step != event.step)
        return invalid(HomeError::WRONG_CORRELATION, "home event correlation mismatch");
    if (nowUs < c.servicedUs) return invalid(HomeError::CLOCK_ERROR, "home clock moved backwards");
    const std::size_t expectedTx = c.step == 0 ? HOME_REQUEST_BYTES : READ_REQUEST_LEN;
    if (event.kind > ReadEventKind::DEADLINE || event.txAccepted > expectedTx ||
        (supplied.txComplete && event.txAccepted != expectedTx))
        return invalid(HomeError::INVALID_EVENT, "invalid home transport envelope");
    if (event.kind == ReadEventKind::FRAME) {
        if (!event.frame || event.txAccepted != expectedTx || !supplied.txComplete ||
            (event.qualified && (event.earliestUs < c.eligibleUs || event.earliestUs > event.latestUs || event.latestUs > nowUs)) ||
            (!event.qualified && (event.earliestUs || event.latestUs)))
            return invalid(HomeError::INVALID_EVENT, "invalid home frame envelope");
    } else if ((!event.frame && event.length) || event.qualified || event.earliestUs || event.latestUs ||
        supplied.responseConfirmed || (event.kind == ReadEventKind::DEADLINE && nowUs < stepDeadline(c)))
        return invalid(HomeError::INVALID_EVENT, "invalid local home event envelope");
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
            Registers::HOMING_METHOD, 6, &evidence.frameError);
        else if (c.step == 1) evidence.status = parseWriteSingleRegister(event.frame, event.length, c.target.address,
            Registers::MOTION_COMMAND, 16, &evidence.frameError);
        else evidence.status = parseRegisters(event.frame, event.length, c.target.address, 2, words, 2, count, &evidence.frameError);
    } else if (event.kind == ReadEventKind::CANCEL) evidence.status = failed(HomeError::CANCELLED, "home locally cancelled");
    else if (event.kind == ReadEventKind::DEADLINE) evidence.status = expired(c);
    else evidence.status = failed(HomeError::TRANSPORT_FAILURE, "home transport failed");
    if (c.step == 0) {
        c.stagingEvidence = evidence; c.setupExecution = execution(evidence);
        c.stagingApplied = c.setupExecution == ActionExecution::ACKNOWLEDGED;
    } else if (c.step == 1) { c.triggerEvidence = evidence; c.execution = execution(evidence); }
    c.servicedUs = nowUs;
    if (event.kind == ReadEventKind::CANCEL) finish(c, ActionOutcome::CANCELLED, evidence.status);
    else if (event.kind == ReadEventKind::DEADLINE) finish(c, ActionOutcome::DEADLINE, evidence.status);
    else if (event.kind == ReadEventKind::TRANSPORT_FAILURE) finish(c, ActionOutcome::TRANSPORT_ERROR, evidence.status);
    else if (!event.qualified) finish(c, ActionOutcome::TIMING_UNQUALIFIED,
        failed(HomeError::TIMING_UNQUALIFIED, "home closure timing is unqualified"));
    else if (event.latestUs > stepDeadline(c)) finish(c, ActionOutcome::DEADLINE, expired(c));
    else if (!evidence.status) finish(c, ActionOutcome::REPLY_ERROR, evidence.status);
    else if (!supplied.responseConfirmed) finish(c, ActionOutcome::UNCONFIRMED_RESPONSE,
        failed(HomeError::UNCONFIRMED_RESPONSE, "frame source is not confirmed as the drive"));
    else if (c.step < 2) {
        if (nowUs >= c.deadlineUs) finish(c, ActionOutcome::DEADLINE, failed(HomeError::DEADLINE_EXPIRED, "no budget for next home step"));
        else if (c.step == 0 && nowUs >= stepDeadline(c)) finish(c, ActionOutcome::DEADLINE, expired(c));
        else if (c.step == 0) { ++c.step; c.phase = HomePhase::TRIGGER; c.eligibleUs = nowUs; }
        else { c.phase = HomePhase::OBSERVING; wait(c, nowUs); }
    } else if (c.phase == HomePhase::ZERO_CHECK) {
        c.zeroEvidence = evidence; c.rawPositionWords[0] = words[0]; c.rawPositionWords[1] = words[1];
        if (words[0] || words[1]) finish(c, ActionOutcome::REPLY_ERROR,
            failed(HomeError::ZERO_NOT_OBSERVED, "fresh position pair does not report zero"));
        else { c.completion = ActionCompletion::OBSERVED; finish(c, ActionOutcome::OBSERVED, Ok()); }
    } else {
        ++c.polls; c.lastObservation = evidence; c.observationKnown = true;
        c.rawAlarm = words[0]; c.rawMotion = words[1];
        if (words[0] || (words[1] & FAULTS)) finish(c, ActionOutcome::REPLY_ERROR,
            failed(HomeError::DRIVE_FAULT, "drive alarm, release or limit interrupted home"));
        else if (c.request.method == HomingMethod::METHOD_35 && (words[1] & RUNNING))
            finish(c, ActionOutcome::REPLY_ERROR,
                failed(HomeError::UNEXPECTED_ACTIVITY, "running report contradicts nonmoving home method35"));
        else {
            if (!(words[1] & HOMED)) {
                if (!c.lowEvidence.length) { c.lowEvidence = evidence; c.lowObservedUs = c.eligibleUs; }
                c.homedLowObserved = true;
            }
            if (words[1] & RUNNING) {
                if (!c.runningObserved) c.activityEvidence = evidence;
                c.runningObserved = true;
            }
            const bool needsActivity = c.request.method != HomingMethod::METHOD_35;
            if (c.homedLowObserved && (!needsActivity || c.runningObserved) &&
                !(words[1] & RUNNING) && (words[1] & HOMED) && (words[1] & ARRIVED)) {
                c.completionEvidence = evidence; c.completionObservedUs = c.eligibleUs;
                c.phase = HomePhase::ZERO_CHECK;
            }
        }
        if (c.state == ActionState::ACTIVE) {
            if (c.polls >= c.options.maxPolls && c.phase != HomePhase::ZERO_CHECK)
                finish(c, ActionOutcome::OBSERVATION_LIMIT, failed(HomeError::OBSERVATION_LIMIT, "fresh homing transition was not observed"));
            else if (nowUs >= c.deadlineUs) finish(c, ActionOutcome::DEADLINE,
                failed(HomeError::DEADLINE_EXPIRED, "no budget for next home observation"));
            else wait(c, nowUs);
        }
    }
    if (c.state == ActionState::FAILED) c.failureEvidence = evidence;
    return Ok();
}

Status getHomeReference(const HomeContext& c, uint64_t nowUs, uint64_t maximumAgeUs, AxisReference& output) noexcept {
    if (c.state != ActionState::SUCCEEDED || c.completion != ActionCompletion::OBSERVED)
        return invalid(HomeError::NOT_COMPLETE, "home completion is not established");
    if (!c.prerequisites.referenceSemanticsQualified)
        return invalid(HomeError::REFERENCE_UNQUALIFIED, "homing zero to command-coordinate relation is unresolved");
    // Request eligibility bounds when the device may have sampled its values;
    // response closure/delivery must not renew the older stationary witness.
    const uint64_t observedUs = c.completionObservedUs;
    if (nowUs < c.servicedUs || nowUs < observedUs || !maximumAgeUs || nowUs - observedUs >= maximumAgeUs)
        return invalid(HomeError::STALE_REFERENCE, "homing reference is stale");
    AxisReference reference;
    reference.target = c.target; reference.configurationGeneration = c.request.configurationGeneration;
    reference.idle = reference.stationary = reference.nativeKnown = true;
    reference.nativePosition = 0; reference.basis = RelativeBasis::ACTUAL; reference.source = ScaleSource::QUALIFIED;
    reference.observedUs = observedUs; reference.nowUs = nowUs; reference.maximumAgeUs = maximumAgeUs;
    output = reference; return Ok();
}
}} // namespace MotorControlRS::ESS_RS
