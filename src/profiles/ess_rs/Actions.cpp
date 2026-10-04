// SPDX-License-Identifier: MIT
#include "MotorControlRS/profiles/ess_rs/Actions.h"
#include "MotorControlRS/profiles/ess_rs/Registers.h"
#include <cstring>

namespace MotorControlRS { namespace ESS_RS {
namespace {
Status invalid(ActionError reason, const char* message) {
    return Status(Err::INVALID_CONFIG, static_cast<int32_t>(reason), message);
}
Status failed(ActionError reason, const char* message) {
    return Status(Err::ILLEGAL_VALUE, static_cast<int32_t>(reason), message);
}
bool sameTarget(const ReadTarget& a, const ReadTarget& b) {
    return a.id == b.id && a.address == b.address && a.generation == b.generation;
}
Status command(const ActionRequest& request, uint16_t& reg, uint16_t& value) {
    if (request.kind > ActionKind::CLEAR_POSITION || request.stop.behavior > StopBehavior::DIRECT ||
        request.stop.deviceQueue > DeviceQueue::DISCARD)
        return invalid(ActionError::INVALID_POLICY, "invalid action policy");
    if (request.kind != ActionKind::STOP && (request.stop.behavior != StopBehavior::UNSPECIFIED ||
        request.stop.deviceQueue != DeviceQueue::UNSPECIFIED || request.stop.customDeceleration))
        return invalid(ActionError::INVALID_POLICY, "stop policy on another action");
    if (request.kind != ActionKind::CLEAR_POSITION && (request.devicePosition || request.positionClearQualified))
        return invalid(ActionError::INVALID_POLICY, "position-clear policy on another action");
    reg = Registers::AUXILIARY_COMMAND;
    switch (request.kind) {
    case ActionKind::ENABLE: value = static_cast<uint16_t>(AuxiliaryCommand::ENABLE); return Ok();
    case ActionKind::RELEASE: value = static_cast<uint16_t>(AuxiliaryCommand::RELEASE); return Ok();
    case ActionKind::CLEAR_ALARM: value = static_cast<uint16_t>(AuxiliaryCommand::CLEAR_ALARM); return Ok();
    case ActionKind::CLEAR_POSITION:
        if (request.devicePosition != 0)
            return Status(Err::UNSUPPORTED, static_cast<int32_t>(ActionError::UNSUPPORTED_POLICY),
                "ESS supports zero position clear only");
        if (!request.positionClearQualified)
            return invalid(ActionError::INVALID_POLICY, "position clear semantics and readiness are unqualified");
        value = static_cast<uint16_t>(AuxiliaryCommand::CLEAR_POSITION); return Ok();
    case ActionKind::STOP:
        if (request.stop.behavior == StopBehavior::UNSPECIFIED)
            return invalid(ActionError::INVALID_POLICY, "stop behavior must be explicit");
        if (request.stop.customDeceleration || request.stop.deviceQueue != DeviceQueue::UNSPECIFIED)
            return Status(Err::UNSUPPORTED, static_cast<int32_t>(ActionError::UNSUPPORTED_POLICY),
                "ESS stop has no reviewed custom ramp or device queue guarantee");
        reg = Registers::MOTION_COMMAND;
        value = static_cast<uint16_t>(request.stop.behavior == StopBehavior::CONFIGURED_DECELERATION ?
            MotionCommandBit::STOP : MotionCommandBit::EMERGENCY_STOP);
        return Ok();
    }
    return invalid(ActionError::INVALID_POLICY, "invalid action");
}
void finish(ActionContext& c, ActionOutcome outcome, Status status) {
    c.outcome = outcome; c.status = status;
    c.state = outcome == ActionOutcome::OBSERVED ? ActionState::SUCCEEDED : ActionState::FAILED;
}
void waitForObservation(ActionContext& c, uint64_t nowUs) {
    ++c.step;
    const uint64_t remaining = c.deadlineUs - nowUs;
    c.eligibleUs = c.options.pollIntervalUs >= remaining ? c.deadlineUs : nowUs + c.options.pollIntervalUs;
}
bool observed(const ActionContext& c, uint16_t alarm, uint16_t motion) {
    switch (c.request.kind) {
    case ActionKind::ENABLE: return (motion & static_cast<uint16_t>(MotionStatusBit::RELEASED)) == 0;
    case ActionKind::RELEASE: return (motion & static_cast<uint16_t>(MotionStatusBit::RELEASED)) != 0;
    case ActionKind::CLEAR_ALARM:
        return alarm == 0 && (motion & static_cast<uint16_t>(MotionStatusBit::ALARM)) == 0;
    case ActionKind::STOP: return (motion & static_cast<uint16_t>(MotionStatusBit::RUNNING)) == 0;
    case ActionKind::CLEAR_POSITION: return alarm == 0 && motion == 0; // Both current-position words must be zero.
    }
    return false;
}
Status prepareKind(ActionContext& output, const ReadTarget& target, uint32_t id, ActionKind kind,
                   uint64_t nowUs, uint64_t deadlineUs, const ActionOptions& options) {
    ActionRequest request; request.kind = kind;
    return prepareAction(output, target, id, request, nowUs, deadlineUs, options);
}
} // namespace

Status prepareAction(ActionContext& output, const ReadTarget& target, uint32_t operationId,
                     const ActionRequest& request, uint64_t nowUs, uint64_t deadlineUs,
                     const ActionOptions& options) noexcept {
    if (!target.id || !target.generation || !isValidAddress(target.address))
        return invalid(ActionError::INVALID_TARGET, "invalid action target");
    if (!operationId) return invalid(ActionError::INVALID_OPERATION, "zero action id");
    if (deadlineUs <= nowUs) return invalid(ActionError::INVALID_DEADLINE, "expired action deadline");
    if (!options.pollIntervalUs || !options.maxPolls || options.maxPolls > ACTION_MAX_POLLS)
        return invalid(ActionError::INVALID_OPTIONS, "invalid bounded action observation policy");
    ActionContext prepared;
    const Status policy = command(request, prepared.reg, prepared.value);
    if (!policy) return policy;
    const Status wire = validateWriteSingleRegisterRequest(target.address, prepared.reg, prepared.value);
    if (!wire) return wire;
    prepared.target = target; prepared.operationId = operationId;
    prepared.request = request; prepared.options = options;
    prepared.state = ActionState::ACTIVE;
    prepared.startedUs = prepared.servicedUs = prepared.eligibleUs = nowUs;
    prepared.deadlineUs = deadlineUs;
    output = prepared;
    return Ok();
}
Status prepareEnable(ActionContext& c, const ReadTarget& t, uint32_t id,
                     uint64_t now, uint64_t deadline, const ActionOptions& options) noexcept {
    return prepareKind(c, t, id, ActionKind::ENABLE, now, deadline, options);
}
Status prepareRelease(ActionContext& c, const ReadTarget& t, uint32_t id,
                      uint64_t now, uint64_t deadline, const ActionOptions& options) noexcept {
    return prepareKind(c, t, id, ActionKind::RELEASE, now, deadline, options);
}
Status prepareClearAlarm(ActionContext& c, const ReadTarget& t, uint32_t id,
                         uint64_t now, uint64_t deadline, const ActionOptions& options) noexcept {
    return prepareKind(c, t, id, ActionKind::CLEAR_ALARM, now, deadline, options);
}
Status prepareSetDevicePosition(ActionContext& c, const ReadTarget& t, uint32_t id,
        int64_t nativePosition, bool qualified, uint64_t now, uint64_t deadline,
        const ActionOptions& options) noexcept {
    ActionRequest request; request.kind = ActionKind::CLEAR_POSITION;
    request.devicePosition = nativePosition; request.positionClearQualified = qualified;
    return prepareAction(c, t, id, request, now, deadline, options);
}
Status prepareStop(ActionContext& c, const ReadTarget& t, uint32_t id, const StopPolicy& policy,
                   uint64_t now, uint64_t deadline, const ActionOptions& options) noexcept {
    ActionRequest request; request.kind = ActionKind::STOP; request.stop = policy;
    return prepareAction(c, t, id, request, now, deadline, options);
}
Status prepareNormalStop(ActionContext& c, const ReadTarget& t, uint32_t id,
                         uint64_t now, uint64_t deadline, const ActionOptions& options) noexcept {
    StopPolicy policy; policy.behavior = StopBehavior::CONFIGURED_DECELERATION;
    return ESS_RS::prepareStop(c, t, id, policy, now, deadline, options);
}
Status prepareEmergencyStop(ActionContext& c, const ReadTarget& t, uint32_t id,
                            uint64_t now, uint64_t deadline, const ActionOptions& options) noexcept {
    StopPolicy policy; policy.behavior = StopBehavior::DIRECT;
    return ESS_RS::prepareStop(c, t, id, policy, now, deadline, options);
}
Status nextAction(const ActionContext& c, uint64_t nowUs, PreparedAction& output) noexcept {
    if (c.state == ActionState::EMPTY)
        return invalid(ActionError::INVALID_STATE, "action is empty");
    if (nowUs < c.servicedUs) return invalid(ActionError::CLOCK_ERROR, "action clock moved backwards");
    PreparedAction next;
    next.target = c.target; next.operationId = c.operationId; next.step = c.step;
    next.deadlineUs = c.deadlineUs; next.eligibleUs = c.eligibleUs;
    if (c.state != ActionState::ACTIVE) { output = next; return Ok(); }
    if (nowUs >= c.deadlineUs) return failed(ActionError::DEADLINE_EXPIRED, "action deadline expired");
    if (nowUs < c.eligibleUs) { next.kind = ActionWork::WAIT; output = next; return Ok(); }
    next.kind = ActionWork::TRANSACTION; next.write = c.step == 0;
    if (next.write) {
        next.reg = c.reg; next.value = c.value;
        next.length = buildWriteSingleRegister(c.target.address, next.reg, next.value, next.bytes, sizeof(next.bytes));
    } else {
        next.reg = c.request.kind == ActionKind::CLEAR_POSITION ? Registers::CURRENT_POSITION : Registers::ERROR_CODE;
        next.count = 2;
        next.length = buildReadRegisters(c.target.address, next.reg, next.count, next.bytes, sizeof(next.bytes));
    }
    if (!next.length) return invalid(ActionError::INVALID_STATE, "invalid prepared action frame");
    output = next;
    return Ok();
}
Status advanceAction(ActionContext& c, const ActionEvent& supplied, uint64_t nowUs) noexcept {
    const ReadEvent& event = supplied.transport;
    if (c.state != ActionState::ACTIVE) return invalid(ActionError::INVALID_STATE, "action is not active");
    if (!sameTarget(c.target, event.target) || c.operationId != event.operationId || c.step != event.step)
        return invalid(ActionError::WRONG_CORRELATION, "action event correlation mismatch");
    if (nowUs < c.servicedUs) return invalid(ActionError::CLOCK_ERROR, "action clock moved backwards");
    if (event.kind > ReadEventKind::DEADLINE || event.txAccepted > READ_REQUEST_LEN ||
        (supplied.txComplete && event.txAccepted != READ_REQUEST_LEN))
        return invalid(ActionError::INVALID_EVENT, "invalid action transport envelope");
    if (event.kind == ReadEventKind::FRAME) {
        if (!event.frame || event.txAccepted != READ_REQUEST_LEN || !supplied.txComplete ||
            (event.qualified && (event.earliestUs < c.eligibleUs || event.earliestUs > event.latestUs || event.latestUs > nowUs)) ||
            (!event.qualified && (event.earliestUs || event.latestUs)))
            return invalid(ActionError::INVALID_EVENT, "invalid action frame envelope");
    } else if ((!event.frame && event.length) || event.qualified || event.earliestUs || event.latestUs ||
               supplied.responseConfirmed || (event.kind == ReadEventKind::DEADLINE && nowUs < c.deadlineUs)) {
        return invalid(ActionError::INVALID_EVENT, "invalid local action event envelope");
    }
    ActionEvidence evidence;
    evidence.step = c.step; evidence.event = event.kind;
    evidence.txAccepted = event.txAccepted; evidence.txComplete = supplied.txComplete;
    evidence.responseConfirmed = supplied.responseConfirmed; evidence.executionUnknown = event.executionUnknown;
    evidence.qualified = event.qualified; evidence.earliestUs = event.earliestUs; evidence.latestUs = event.latestUs;
    evidence.deliveredUs = nowUs; evidence.transportDetail = event.transportDetail;
    evidence.receivedLength = event.length;
    evidence.length = event.length < ACTION_MAX_REPLY_BYTES ? event.length : ACTION_MAX_REPLY_BYTES;
    if (evidence.length) std::memcpy(evidence.raw, event.frame, evidence.length);
    uint16_t words[2] = {}; std::size_t count = 0;
    if (event.kind == ReadEventKind::FRAME) {
        evidence.status = c.step == 0 ?
            parseWriteSingleRegister(event.frame, event.length, c.target.address, c.reg, c.value, &evidence.frameError) :
            parseRegisters(event.frame, event.length, c.target.address, 2, words, 2, count, &evidence.frameError);
    } else if (event.kind == ReadEventKind::TRANSPORT_FAILURE) {
        evidence.status = failed(ActionError::TRANSPORT_FAILURE, "action transport failed");
    } else if (event.kind == ReadEventKind::CANCEL) {
        evidence.status = failed(ActionError::CANCELLED, "action locally cancelled");
    } else evidence.status = failed(ActionError::DEADLINE_EXPIRED, "action deadline expired");
    if (c.step == 0) {
        c.writeEvidence = evidence;
        c.execution = event.txAccepted || event.executionUnknown ? ActionExecution::UNKNOWN : ActionExecution::NOT_TRANSMITTED;
        if (event.kind == ReadEventKind::FRAME && event.qualified && supplied.responseConfirmed) {
            if (evidence.status) c.execution = ActionExecution::ACKNOWLEDGED;
            // Function manual p12 defines only 01..07 as request errors. An
            // undocumented exception retains its raw code without establishing
            // that the drive rejected execution.
            else if (evidence.status.code == Err::EXCEPTION &&
                     evidence.status.detail >= 1 && evidence.status.detail <= 7)
                c.execution = ActionExecution::REJECTED;
        }
    }
    c.servicedUs = nowUs;
    if (event.kind == ReadEventKind::CANCEL) finish(c, ActionOutcome::CANCELLED, evidence.status);
    else if (event.kind == ReadEventKind::DEADLINE) finish(c, ActionOutcome::DEADLINE, evidence.status);
    else if (event.kind == ReadEventKind::TRANSPORT_FAILURE) finish(c, ActionOutcome::TRANSPORT_ERROR, evidence.status);
    else if (!event.qualified) finish(c, ActionOutcome::TIMING_UNQUALIFIED,
        failed(ActionError::TIMING_UNQUALIFIED, "action closure timing is unqualified"));
    else if (event.latestUs > c.deadlineUs) finish(c, ActionOutcome::DEADLINE,
        failed(ActionError::DEADLINE_EXPIRED, "action closure exceeds deadline"));
    else if (!evidence.status) finish(c, ActionOutcome::REPLY_ERROR, evidence.status);
    else if (!supplied.responseConfirmed && !(c.step == 0 && c.options.allowUnconfirmedWriteObservation))
        finish(c, ActionOutcome::UNCONFIRMED_RESPONSE,
        failed(ActionError::UNCONFIRMED_RESPONSE, "frame source is not confirmed as the drive"));
    else if (c.step == 0) {
        if (nowUs >= c.deadlineUs) finish(c, ActionOutcome::DEADLINE,
            failed(ActionError::DEADLINE_EXPIRED, "no observation budget after acknowledgement"));
        else waitForObservation(c, nowUs);
    } else {
        ++c.polls;
        c.lastObservation = evidence; c.observationKnown = true;
        if (c.request.kind == ActionKind::CLEAR_POSITION)
            c.rawPosition = (static_cast<uint32_t>(words[0]) << 16) | words[1];
        else { c.rawAlarm = words[0]; c.rawMotion = words[1]; }
        if (observed(c, words[0], words[1])) {
            c.completion = ActionCompletion::OBSERVED;
            finish(c, ActionOutcome::OBSERVED, Ok());
        } else if (c.polls >= c.options.maxPolls) finish(c, ActionOutcome::OBSERVATION_LIMIT,
            failed(ActionError::OBSERVATION_LIMIT, "action observation limit reached"));
        else if (nowUs >= c.deadlineUs) finish(c, ActionOutcome::DEADLINE,
            failed(ActionError::DEADLINE_EXPIRED, "no budget for another action observation"));
        else waitForObservation(c, nowUs);
    }
    if (c.state == ActionState::FAILED) c.failureEvidence = evidence;
    return Ok();
}
}} // namespace MotorControlRS::ESS_RS
