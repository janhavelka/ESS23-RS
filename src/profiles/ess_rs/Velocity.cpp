// SPDX-License-Identifier: MIT
#include "MotorControlRS/profiles/ess_rs/Velocity.h"
#include "MotorControlRS/profiles/ess_rs/Registers.h"
#include <cstring>
#include <limits>
#include <cmath>

namespace MotorControlRS { namespace ESS_RS {
namespace {
Status invalid(VelocityError e, const char* text) { return Status(Err::INVALID_CONFIG, static_cast<int32_t>(e), text); }
Status failed(VelocityError e, const char* text) { return Status(Err::ILLEGAL_VALUE, static_cast<int32_t>(e), text); }
Status unsupported(const char* text) {
    return Status(Err::UNSUPPORTED, static_cast<int32_t>(VelocityError::UNSUPPORTED_CAPABILITY), text);
}
bool sameTarget(const ReadTarget& a, const ReadTarget& b) {
    return a.id == b.id && a.address == b.address && a.generation == b.generation;
}
constexpr uint16_t RUNNING = static_cast<uint16_t>(MotionStatusBit::RUNNING);
constexpr uint16_t FAULTS = static_cast<uint16_t>(MotionStatusBit::ALARM) |
    static_cast<uint16_t>(MotionStatusBit::RELEASED) |
    static_cast<uint16_t>(MotionStatusBit::POSITIVE_SOFT_LIMIT) |
    static_cast<uint16_t>(MotionStatusBit::NEGATIVE_SOFT_LIMIT);
uint64_t sum(uint64_t a, uint64_t b) {
    return b > std::numeric_limits<uint64_t>::max() - a ? std::numeric_limits<uint64_t>::max() : a + b;
}
uint64_t stepDeadline(const VelocityContext& c) {
    if (c.phase == VelocityPhase::STOPPING) return c.deadlineUs;
    uint64_t deadline = c.stopDueUs;
    if (c.phase != VelocityPhase::OBSERVING) {
        const uint64_t readiness = sum(c.prerequisites.observedUs, c.prerequisites.maximumAgeUs);
        if (readiness < deadline) deadline = readiness;
    }
    return deadline;
}
Status expiry(const VelocityContext& c) {
    return stepDeadline(c) < c.stopDueUs ? failed(VelocityError::READINESS, "velocity readiness expired") :
        failed(VelocityError::DEADLINE_EXPIRED, "velocity step deadline expired");
}
ActionExecution execution(const ActionEvidence& e) {
    if (!e.txAccepted && !e.executionUnknown) return ActionExecution::NOT_TRANSMITTED;
    if (e.event == ReadEventKind::FRAME && e.qualified && e.responseConfirmed) {
        if (e.status) return ActionExecution::ACKNOWLEDGED;
        if (e.status.code == Err::EXCEPTION && e.status.detail >= 1 && e.status.detail <= 7)
            return ActionExecution::REJECTED;
    }
    return ActionExecution::UNKNOWN;
}
void finish(VelocityContext& c, ActionOutcome outcome, Status status) {
    c.outcome = outcome; c.status = status;
    c.state = outcome == ActionOutcome::OBSERVED ? ActionState::SUCCEEDED : ActionState::FAILED;
    c.uncertain = c.state == ActionState::FAILED &&
        (c.stagingEvidence.txAccepted || c.stagingEvidence.executionUnknown ||
         c.triggerEvidence.txAccepted || c.triggerEvidence.executionUnknown);
}
void retainFailure(VelocityContext& c, ActionOutcome outcome, Status status, const ActionEvidence* evidence = nullptr) {
    if (c.outcome != ActionOutcome::NONE) return;
    c.outcome = outcome; c.status = status;
    if (evidence) c.failureEvidence = *evidence;
    c.uncertain = c.stagingEvidence.txAccepted || c.stagingEvidence.executionUnknown ||
        c.triggerEvidence.txAccepted || c.triggerEvidence.executionUnknown;
}
void beginStop(VelocityContext& c, uint64_t now) {
    if (c.needsStop && now > sum(c.stopDueUs, c.options.pollIntervalUs)) {
        c.serviceMissed = true;
        retainFailure(c, ActionOutcome::DEADLINE,
            failed(VelocityError::SERVICE_MISSED, "finite velocity stop service obligation missed"));
    }
    if (now >= c.deadlineUs) {
        const auto outcome = c.outcome == ActionOutcome::NONE ? ActionOutcome::DEADLINE : c.outcome;
        const Status status = c.outcome == ActionOutcome::NONE ?
            failed(VelocityError::DEADLINE_EXPIRED, "velocity expired without stop settlement") : c.status;
        finish(c, outcome, status); return;
    }
    const Status prepared = ESS_RS::prepareStop(c.stop, c.target, c.operationId, c.request.stop,
        now, c.deadlineUs, c.options);
    if (!prepared) { finish(c, ActionOutcome::REPLY_ERROR, prepared); return; }
    c.phase = VelocityPhase::STOPPING; ++c.step; c.eligibleUs = now;
}
void failStep(VelocityContext& c, ActionOutcome outcome, Status status, const ActionEvidence& evidence, uint64_t now) {
    retainFailure(c, outcome, status, &evidence);
    if (c.needsStop && now < c.deadlineUs) beginStop(c, now);
    else finish(c, c.outcome, c.status);
}
void wait(VelocityContext& c, uint64_t now) {
    ++c.step;
    c.eligibleUs = sum(now, c.options.pollIntervalUs);
    if (c.eligibleUs > c.stopDueUs) c.eligibleUs = c.stopDueUs;
}
} // namespace

Status prepareVelocity(VelocityContext& output, const AxisConfig& axis, uint32_t id,
        const VelocityRequest& request, const VelocityPrerequisites& p, uint64_t now,
        uint64_t deadline, const ActionOptions& options) noexcept {
    if (!axis.target.id || !axis.target.generation || !isValidAddress(axis.target.address))
        return invalid(VelocityError::INVALID_TARGET, "invalid velocity target");
    if (!id) return invalid(VelocityError::INVALID_OPERATION, "zero velocity operation id");
    if (deadline <= now || !request.durationUs || request.durationUs >= deadline - now)
        return invalid(VelocityError::INVALID_DEADLINE, "finite velocity duration needs an additional stop budget");
    if (!options.pollIntervalUs || !options.maxPolls || options.maxPolls > ACTION_MAX_POLLS)
        return invalid(VelocityError::INVALID_OPTIONS, "invalid bounded velocity observation policy");
    if (!sameTarget(axis.target, p.target) || !axis.generation || axis.generation != p.configurationGeneration ||
        request.configurationGeneration != axis.generation)
        return invalid(VelocityError::STALE_CONFIGURATION, "velocity configuration binding mismatch");
    if (request.ramp > VelocityRamp::ACCELERATION)
        return invalid(VelocityError::INVALID_REQUEST, "invalid velocity ramp policy");
    if (!std::isfinite(request.acceleration) || !std::isfinite(request.deceleration) || !std::isfinite(request.jerk) ||
        request.acceleration < 0 || request.deceleration < 0 || request.jerk < 0)
        return invalid(VelocityError::INVALID_REQUEST, "invalid velocity acceleration or jerk");
    if (request.ramp == VelocityRamp::ACCELERATION)
        return unsupported("ESS acceleration-to-ramp formula and starting speed are unresolved");
    if (request.acceleration || request.deceleration || request.jerk || request.blending || request.liveUpdate)
        return unsupported("ESS jerk, blending, live updates and acceleration mapping are unsupported");
    if (request.ramp != VelocityRamp::VERIFIED_CONFIGURED || !p.configuredRampVerified ||
        p.accelerationTime > 2000 || p.decelerationTime > 2000)
        return invalid(VelocityError::UNRESOLVED_RAMP, "verified configured native velocity ramps are required");
    if (!p.nativeRpmVerified) return invalid(VelocityError::UNRESOLVED_UNITS, "native signed rpm interpretation is unresolved");
    if (p.minimumRpm < -3000 || p.maximumRpm > 3000 || p.minimumRpm > p.maximumRpm)
        return invalid(VelocityError::INVALID_REQUEST, "invalid qualified velocity range");
    if (!p.readinessQualified || !p.serialInputsPermit || !p.maximumAgeUs || p.observedUs > now ||
        now - p.observedUs >= p.maximumAgeUs || p.rawAlarm || (p.rawMotion & (FAULTS | RUNNING)))
        return invalid(VelocityError::READINESS, "fresh enabled stationary alarm-free serial readiness is required");
    VelocityContext c;
    const Status target = prepareVelocityTarget(request, axis, c.prepared);
    if (!target) return target;
    if (!c.prepared.nativeRpm) return invalid(VelocityError::ZERO_SPEED, "zero target velocity is not a stop or release");
    const auto& exact = c.prepared.requestedRpm;
    const bool requestedOutside = c.prepared.exactArithmetic ?
        (exact.integral < p.minimumRpm || exact.integral > p.maximumRpm ||
         (exact.integral == p.minimumRpm && exact.numerator && exact.negative) ||
         (exact.integral == p.maximumRpm && exact.numerator && !exact.negative)) :
        (c.prepared.approximateRequestedRpm - c.prepared.approximationErrorBound < p.minimumRpm ||
         c.prepared.approximateRequestedRpm + c.prepared.approximationErrorBound > p.maximumRpm);
    if (requestedOutside || c.prepared.nativeRpm < p.minimumRpm || c.prepared.nativeRpm > p.maximumRpm)
        return invalid(VelocityError::INVALID_REQUEST, "velocity exceeds the qualified native range");
    if (c.prepared.nativeRpm < 0 && !p.negativeTwosComplementVerified)
        return invalid(VelocityError::UNRESOLVED_SIGN, "negative ESS speed encoding is unresolved");
    // Validate the exact same existing stop policy before publishing any setup.
    ActionContext stopCheck;
    const Status stop = ESS_RS::prepareStop(stopCheck, axis.target, id, request.stop, now, deadline, options);
    if (!stop) return stop;
    c.words[0] = static_cast<uint16_t>(c.prepared.nativeRpm);
    c.words[1] = p.accelerationTime; c.words[2] = p.decelerationTime;
    const Status wire = validateWriteMultipleRegistersRequest(axis.target.address, Registers::JOG_SPEED, c.words, 3);
    if (!wire) return wire;
    c.target = axis.target; c.operationId = id; c.request = request; c.prerequisites = p;
    c.options = options; c.state = ActionState::ACTIVE;
    c.startedUs = c.servicedUs = c.eligibleUs = now; c.deadlineUs = deadline;
    c.stopDueUs = now + request.durationUs; // Overflow excluded by duration < deadline-now.
    output = c; return Ok();
}

Status serviceVelocity(VelocityContext& c, uint64_t now) noexcept {
    if (c.state == ActionState::EMPTY) return invalid(VelocityError::INVALID_STATE, "velocity is empty");
    if (now < c.servicedUs) return invalid(VelocityError::CLOCK_ERROR, "velocity clock moved backwards");
    if (c.state != ActionState::ACTIVE) return Ok();
    c.servicedUs = now;
    if (now >= c.deadlineUs) {
        if (c.phase == VelocityPhase::OBSERVING && c.needsStop && now > sum(c.stopDueUs, c.options.pollIntervalUs))
            c.serviceMissed = true;
        if (c.phase == VelocityPhase::STOPPING && c.stop.state == ActionState::ACTIVE) {
            ActionEvent elapsed;
            elapsed.transport.target = c.stop.target;
            elapsed.transport.operationId = c.stop.operationId;
            elapsed.transport.step = c.stop.step;
            elapsed.transport.kind = ReadEventKind::DEADLINE;
            // No admitted transaction exists here; retain the stop's earlier
            // write ACK and make missing stop delivery/observation explicit.
            const Status consumed = advanceAction(c.stop, elapsed, now);
            if (!consumed) return consumed;
        }
        if (c.outcome == ActionOutcome::NONE) retainFailure(c, ActionOutcome::DEADLINE,
            failed(VelocityError::DEADLINE_EXPIRED, "velocity expired without stop settlement"));
        finish(c, c.outcome, c.status); return Ok();
    }
    if (c.phase == VelocityPhase::OBSERVING && now >= c.stopDueUs) {
        beginStop(c, now);
    } else if ((c.phase == VelocityPhase::STAGING || c.phase == VelocityPhase::TRIGGER) && now >= stepDeadline(c)) {
        finish(c, ActionOutcome::DEADLINE, expiry(c));
    }
    return Ok();
}

Status nextVelocity(const VelocityContext& c, uint64_t now, PreparedVelocity& output) noexcept {
    if (c.state == ActionState::EMPTY) return invalid(VelocityError::INVALID_STATE, "velocity is empty");
    if (now < c.servicedUs) return invalid(VelocityError::CLOCK_ERROR, "velocity clock moved backwards");
    PreparedVelocity next;
    next.target = c.target; next.operationId = c.operationId; next.step = c.step;
    next.deadlineUs = stepDeadline(c); next.eligibleUs = c.eligibleUs;
    if (c.state != ActionState::ACTIVE) { output = next; return Ok(); }
    if (now >= c.deadlineUs) return failed(VelocityError::DEADLINE_EXPIRED, "velocity deadline expired");
    if (c.phase == VelocityPhase::STOPPING) {
        PreparedAction stop;
        const Status status = nextAction(c.stop, now, stop); if (!status) return status;
        next.kind = stop.kind; next.write = stop.write; next.urgent = stop.write;
        next.reg = stop.reg; next.count = stop.write ? 1 : stop.count; next.value = stop.value;
        next.function = stop.write ? 6 : 3; next.length = stop.length;
        next.deadlineUs = stop.deadlineUs; next.eligibleUs = stop.eligibleUs;
        if (stop.length) std::memcpy(next.bytes, stop.bytes, stop.length);
    } else if (c.phase == VelocityPhase::OBSERVING && now >= c.stopDueUs) {
        next.kind = ActionWork::WAIT; next.eligibleUs = c.stopDueUs; // serviceVelocity transitions without an admitted token.
    } else {
        if (now >= next.deadlineUs) return expiry(c);
        if (now < c.eligibleUs) { next.kind = ActionWork::WAIT; output = next; return Ok(); }
        next.kind = ActionWork::TRANSACTION; next.write = c.phase != VelocityPhase::OBSERVING;
        if (c.phase == VelocityPhase::STAGING) {
            next.function = 16; next.reg = Registers::JOG_SPEED; next.count = 3;
            next.length = buildWriteMultipleRegisters(c.target.address, next.reg, c.words, 3, next.bytes, sizeof(next.bytes));
        } else if (c.phase == VelocityPhase::TRIGGER) {
            next.function = 6; next.reg = Registers::MOTION_COMMAND; next.count = 1;
            next.value = static_cast<uint16_t>(MotionCommandBit::START_SPEED);
            next.length = buildWriteSingleRegister(c.target.address, next.reg, next.value, next.bytes, sizeof(next.bytes));
        } else {
            next.function = 3; next.reg = Registers::ERROR_CODE; next.count = 2;
            next.length = buildReadRegisters(c.target.address, next.reg, 2, next.bytes, sizeof(next.bytes));
        }
        if (!next.length) return invalid(VelocityError::INVALID_STATE, "invalid prepared velocity frame");
    }
    output = next; return Ok();
}

Status advanceVelocity(VelocityContext& c, const ActionEvent& supplied, uint64_t now) noexcept {
    const auto& event = supplied.transport;
    if (c.state != ActionState::ACTIVE) return invalid(VelocityError::INVALID_STATE, "velocity is not active");
    if (!sameTarget(c.target, event.target) || c.operationId != event.operationId || c.step != event.step)
        return invalid(VelocityError::WRONG_CORRELATION, "velocity event correlation mismatch");
    if (now < c.servicedUs) return invalid(VelocityError::CLOCK_ERROR, "velocity clock moved backwards");
    if (c.phase == VelocityPhase::STOPPING) {
        ActionEvent local = supplied; local.transport.step = c.stop.step;
        const auto oldStep = c.stop.step;
        const Status consumed = advanceAction(c.stop, local, now); if (!consumed) return consumed;
        c.servicedUs = now; c.eligibleUs = c.stop.eligibleUs;
        if (c.stop.step != oldStep) ++c.step;
        if (c.stop.state != ActionState::ACTIVE) {
            if (c.stop.completion == ActionCompletion::OBSERVED) {
                c.needsStop = false; c.completion = ActionCompletion::OBSERVED;
                if (c.outcome != ActionOutcome::NONE) finish(c, c.outcome, c.status);
                else if (!c.runningObserved) finish(c, ActionOutcome::OBSERVATION_LIMIT,
                    failed(VelocityError::ACTIVITY_NOT_OBSERVED, "fresh velocity activity was not observed"));
                else finish(c, ActionOutcome::OBSERVED, Ok());
            } else if (c.outcome != ActionOutcome::NONE) finish(c, c.outcome, c.status);
            else finish(c, c.stop.outcome, c.stop.status);
        }
        return Ok();
    }
    const std::size_t expectedTx = c.phase == VelocityPhase::STAGING ? VELOCITY_REQUEST_BYTES : READ_REQUEST_LEN;
    if (event.kind > ReadEventKind::DEADLINE || event.txAccepted > expectedTx ||
        (supplied.txComplete && event.txAccepted != expectedTx))
        return invalid(VelocityError::INVALID_EVENT, "invalid velocity transport envelope");
    if (event.kind == ReadEventKind::FRAME) {
        if (!event.frame || event.txAccepted != expectedTx || !supplied.txComplete ||
            (event.qualified && (event.earliestUs < c.eligibleUs || event.earliestUs > event.latestUs || event.latestUs > now)) ||
            (!event.qualified && (event.earliestUs || event.latestUs)))
            return invalid(VelocityError::INVALID_EVENT, "invalid velocity frame envelope");
    } else if ((!event.frame && event.length) || event.qualified || event.earliestUs || event.latestUs ||
        supplied.responseConfirmed || (event.kind == ReadEventKind::DEADLINE && now < stepDeadline(c)))
        return invalid(VelocityError::INVALID_EVENT, "invalid local velocity event envelope");
    ActionEvidence evidence;
    evidence.step = c.step; evidence.event = event.kind; evidence.txAccepted = event.txAccepted;
    evidence.txComplete = supplied.txComplete; evidence.responseConfirmed = supplied.responseConfirmed;
    evidence.executionUnknown = event.executionUnknown; evidence.qualified = event.qualified;
    evidence.earliestUs = event.earliestUs; evidence.latestUs = event.latestUs;
    evidence.deliveredUs = now; evidence.transportDetail = event.transportDetail;
    evidence.receivedLength = event.length;
    evidence.length = event.length < ACTION_MAX_REPLY_BYTES ? event.length : ACTION_MAX_REPLY_BYTES;
    if (evidence.length) std::memcpy(evidence.raw, event.frame, evidence.length);
    uint16_t words[2] = {}; std::size_t count = 0;
    if (event.kind == ReadEventKind::FRAME) {
        if (c.phase == VelocityPhase::STAGING) evidence.status = parseWriteMultipleRegisters(event.frame, event.length,
            c.target.address, Registers::JOG_SPEED, 3, &evidence.frameError);
        else if (c.phase == VelocityPhase::TRIGGER) evidence.status = parseWriteSingleRegister(event.frame, event.length,
            c.target.address, Registers::MOTION_COMMAND, static_cast<uint16_t>(MotionCommandBit::START_SPEED), &evidence.frameError);
        else evidence.status = parseRegisters(event.frame, event.length, c.target.address, 2, words, 2, count, &evidence.frameError);
    } else if (event.kind == ReadEventKind::CANCEL) evidence.status = failed(VelocityError::CANCELLED, "velocity locally cancelled");
    else if (event.kind == ReadEventKind::DEADLINE) evidence.status = expiry(c);
    else evidence.status = failed(VelocityError::TRANSPORT_FAILURE, "velocity transport failed");
    if (c.phase == VelocityPhase::STAGING) {
        c.stagingEvidence = evidence; c.setupExecution = execution(evidence);
        c.stagingApplied = c.setupExecution == ActionExecution::ACKNOWLEDGED;
    } else if (c.phase == VelocityPhase::TRIGGER) {
        c.triggerEvidence = evidence; c.execution = execution(evidence);
        c.needsStop = c.execution == ActionExecution::ACKNOWLEDGED || c.execution == ActionExecution::UNKNOWN;
    }
    c.servicedUs = now;
    if (event.kind == ReadEventKind::CANCEL) {
        c.failureEvidence = evidence; finish(c, ActionOutcome::CANCELLED, evidence.status);
    } else if (event.kind == ReadEventKind::DEADLINE && c.phase == VelocityPhase::OBSERVING && now >= c.stopDueUs) {
        // The finite observation budget ended; deadline is not standstill evidence.
        beginStop(c, now);
    } else if (event.kind == ReadEventKind::DEADLINE) failStep(c, ActionOutcome::DEADLINE, evidence.status, evidence, now);
    else if (event.kind == ReadEventKind::TRANSPORT_FAILURE) failStep(c, ActionOutcome::TRANSPORT_ERROR, evidence.status, evidence, now);
    else if (!event.qualified) failStep(c, ActionOutcome::TIMING_UNQUALIFIED,
        failed(VelocityError::TIMING_UNQUALIFIED, "velocity closure timing is unqualified"), evidence, now);
    else if (event.latestUs > stepDeadline(c)) failStep(c, ActionOutcome::DEADLINE, expiry(c), evidence, now);
    else if (!evidence.status) failStep(c, ActionOutcome::REPLY_ERROR, evidence.status, evidence, now);
    else if (!supplied.responseConfirmed) failStep(c, ActionOutcome::UNCONFIRMED_RESPONSE,
        failed(VelocityError::UNCONFIRMED_RESPONSE, "frame source is not confirmed as the drive"), evidence, now);
    else if (c.phase == VelocityPhase::STAGING) {
        if (now >= stepDeadline(c)) failStep(c, ActionOutcome::DEADLINE, expiry(c), evidence, now);
        else { c.phase = VelocityPhase::TRIGGER; ++c.step; c.eligibleUs = now; }
    } else if (c.phase == VelocityPhase::TRIGGER) {
        c.phase = VelocityPhase::OBSERVING;
        wait(c, now);
        if (now >= c.stopDueUs) {
            beginStop(c, now);
        }
    } else {
        ++c.polls; c.lastObservation = evidence; c.observationKnown = true;
        c.rawAlarm = words[0]; c.rawMotion = words[1];
        if (words[0] || (words[1] & FAULTS)) failStep(c, ActionOutcome::REPLY_ERROR,
            failed(VelocityError::DRIVE_FAULT, "drive alarm, release or limit interrupted velocity"), evidence, now);
        else {
            if (words[1] & RUNNING) {
                if (!c.runningObserved) c.activityEvidence = evidence;
                c.runningObserved = true;
            }
            if (c.polls >= c.options.maxPolls) failStep(c, ActionOutcome::OBSERVATION_LIMIT,
                failed(VelocityError::OBSERVATION_LIMIT, "bounded velocity observation budget exhausted"), evidence, now);
            else if (now >= c.stopDueUs) beginStop(c, now);
            else wait(c, now);
        }
    }
    return Ok();
}
}} // namespace MotorControlRS::ESS_RS
