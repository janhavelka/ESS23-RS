// SPDX-License-Identifier: MIT
#include "MotorControlRS/profiles/ess_rs/Communication.h"
#include "MotorControlRS/profiles/ess_rs/Registers.h"
#include <cstring>

namespace MotorControlRS { namespace ESS_RS {
namespace {
Status invalid(CommunicationError e, const char* msg) {
    return Status(Err::INVALID_CONFIG, static_cast<int32_t>(e), msg);
}
Status failed(CommunicationError e, const char* msg) {
    return Status(Err::ILLEGAL_VALUE, static_cast<int32_t>(e), msg);
}
bool same(const ReadTarget& a, const ReadTarget& b) {
    return a.id == b.id && a.address == b.address && a.generation == b.generation;
}
bool same(const ActiveSerialTuple& a, const ActiveSerialTuple& b) {
    return a.known == b.known && a.baud == b.baud && a.dataBits == b.dataBits &&
        a.parity == b.parity && a.stopBits == b.stopBits;
}
bool valid(const ActiveSerialTuple& t) {
    return t.known && (t.baud == 115200 || t.baud == 38400 || t.baud == 19200 || t.baud == 9600) &&
        t.dataBits == 8 && ((t.parity == SerialParity::NONE && (t.stopBits == 1 || t.stopBits == 2)) ||
        ((t.parity == SerialParity::EVEN || t.parity == SerialParity::ODD) && t.stopBits == 1));
}
ActiveSerialTuple settingTuple(uint16_t baud, uint16_t format) {
    ActiveSerialTuple t;
    if (baud > 3 || format > 3) return t;
    const uint32_t rates[] = {115200, 38400, 19200, 9600};
    t.known = true; t.baud = rates[baud]; t.dataBits = 8;
    t.parity = format == 2 ? SerialParity::EVEN : format == 3 ? SerialParity::ODD : SerialParity::NONE;
    t.stopBits = format == 1 ? 2 : 1;
    return t;
}
uint16_t value(const CommunicationRequest& r) {
    return r.field == CommunicationField::ADDRESS ? r.address : r.field == CommunicationField::BAUD ?
        static_cast<uint16_t>(r.baud) : static_cast<uint16_t>(r.format);
}
void finish(CommunicationContext& c, CommunicationOutcome o, Status s) {
    c.outcome = o; c.status = s;
    c.state = o == CommunicationOutcome::ACKNOWLEDGED || o == CommunicationOutcome::CONFIRMED ?
        ReadState::SUCCEEDED : ReadState::FAILED;
    if (c.step == 0) c.writeOutcome = o;
}
} // namespace

Status prepareCommunication(CommunicationContext& output, const ReadTarget& target, uint32_t id,
                            const CommunicationRequest& request, const CommunicationPrerequisites& p,
                            uint64_t now, uint64_t deadline) noexcept {
    if (!target.id || !target.generation || !isValidAddress(target.address))
        return invalid(CommunicationError::INVALID_TARGET, "invalid communication target");
    if (!id) return invalid(CommunicationError::INVALID_OPERATION, "zero communication operation id");
    if (deadline <= now) return invalid(CommunicationError::INVALID_DEADLINE, "expired communication deadline");
    if (request.field > CommunicationField::FORMAT)
        return invalid(CommunicationError::INVALID_CANDIDATE, "invalid communication field");
    const uint16_t requested = value(request);
    if ((request.field == CommunicationField::ADDRESS && (requested < 1 || requested > 247)) ||
        (request.field != CommunicationField::ADDRESS && requested > 3))
        return invalid(CommunicationError::INVALID_CANDIDATE, "unsupported communication setting code or endpoint");
    if (!valid(p.beforeSerial) || !same(p.beforeSerial, p.previous.activeSerial))
        return invalid(CommunicationError::INVALID_TUPLE, "known matching active host tuple required");
    if (!same(p.previous.target, target) || !p.previous.operationId)
        return invalid(CommunicationError::STALE_SETTINGS, "previous settings target or age invalid");
    const ReadStepObservation& source = p.previous.provenance[1];
    uint16_t words[3] = {}; std::size_t count = 0;
    if (!source.qualified || !source.status || source.frameError != FrameError::NONE || source.event != ReadEventKind::FRAME ||
        source.first != Registers::CUSTOM_NODE || source.count != 3 || source.length != source.receivedLength ||
        source.length > READ_MAX_REPLY_BYTES || source.txAccepted != READ_REQUEST_LEN || source.executionUnknown ||
        source.attemptedUs > source.earliestUs || source.earliestUs > source.latestUs ||
        source.latestUs > source.deliveredUs || source.deliveredUs > now ||
        !evidenceAgeValid(source.attemptedUs, now, p.maxAgeUs) ||
        !parseRegisters(source.raw, source.length, target.address, 3, words, 3, count) ||
        words[0] != p.previous.raw.customNode || words[1] != p.previous.raw.baud || words[2] != p.previous.raw.format)
        return invalid(CommunicationError::STALE_SETTINGS, "invalid or stale communication read provenance");
    if (!same(settingTuple(words[1], words[2]), p.beforeSerial))
        return invalid(CommunicationError::PENDING_BASELINE, "pending_baseline: stored baud/format differs from active tuple; reconcile first");
    if (!p.stationaryQualified || !p.effectsQualified || !p.routeBackQualified ||
        p.qualifiedRequest.field != request.field || value(p.qualifiedRequest) != requested)
        return invalid(CommunicationError::PREREQUISITES_REQUIRED, "exact stationary effects and route-back qualification required");
    if (request.field == CommunicationField::ADDRESS && !p.addressDipOffQualified)
        return invalid(CommunicationError::DIP_REQUIRED, "independent address DIP OFF qualification required");
    CommunicationContext c;
    c.beforeTarget = c.requestedTarget = target;
    c.beforeSerial = c.requestedSerial = p.beforeSerial;
    c.request = request; c.operationId = id;
    c.reg = static_cast<uint16_t>(Registers::CUSTOM_NODE + static_cast<uint8_t>(request.field));
    c.previous = words[static_cast<uint8_t>(request.field)]; c.requested = requested;
    if (request.field == CommunicationField::ADDRESS) {
        c.requestedTarget.address = static_cast<uint8_t>(requested);
        c.save = CommunicationRequirement::REQUIRED;
    } else {
        c.restart = CommunicationRequirement::REQUIRED;
        if (request.field == CommunicationField::BAUD) {
            c.requestedSerial = settingTuple(requested, words[2]);
        } else {
            c.requestedSerial = settingTuple(words[1], requested);
        }
    }
    const Status checked = validateWriteSingleRegisterRequest(target.address, c.reg, c.requested);
    if (!checked) return checked;
    c.state = ReadState::ACTIVE;
    c.startedUs = c.servicedUs = c.eligibleUs = now;
    const uint64_t freshUntil = evidenceAgeDeadline(source.attemptedUs, p.maxAgeUs);
    c.deadlineUs = deadline < freshUntil ? deadline : freshUntil;
    output = c;
    return Ok();
}

Status prepareCommunicationConfirmation(CommunicationContext& c, const ReadTarget& target,
                            const ActiveSerialTuple& serial, uint64_t now, uint64_t deadline) noexcept {
    if (c.state == ReadState::EMPTY || c.state == ReadState::ACTIVE || c.confirmations >= 2)
        return invalid(CommunicationError::INVALID_STATE, "confirmation requires a terminal write and an unused slot");
    if (now < c.servicedUs) return invalid(CommunicationError::CLOCK_ERROR, "communication clock moved backwards");
    if (deadline <= now) return invalid(CommunicationError::INVALID_DEADLINE, "expired confirmation deadline");
    if (!((same(target, c.beforeTarget) && same(serial, c.beforeSerial)) ||
          (same(target, c.requestedTarget) && same(serial, c.requestedSerial))))
        return invalid(CommunicationError::INVALID_CANDIDATE, "confirmation must select an exact retained candidate");
    // Copy aliased arguments before mutating the caller-owned context.
    const ReadTarget selectedTarget = target;
    const ActiveSerialTuple selectedSerial = serial;
    c.confirmationTarget = selectedTarget; c.confirmationSerial = selectedSerial;
    c.step = static_cast<uint8_t>(++c.confirmations);
    c.eligibleUs = c.servicedUs = now; c.deadlineUs = deadline;
    c.state = ReadState::ACTIVE; c.outcome = CommunicationOutcome::NONE; c.status = Ok();
    c.observedActiveKnown = false;
    return Ok();
}

Status nextCommunication(const CommunicationContext& c, uint64_t now, PreparedCommunication& output) noexcept {
    if (c.state == ReadState::EMPTY) return invalid(CommunicationError::INVALID_STATE, "empty communication operation");
    if (now < c.servicedUs) return invalid(CommunicationError::CLOCK_ERROR, "communication clock moved backwards");
    PreparedCommunication next;
    next.target = c.step == 0 ? c.beforeTarget : c.confirmationTarget;
    next.serial = c.step == 0 ? c.beforeSerial : c.confirmationSerial;
    next.operationId = c.operationId; next.step = c.step; next.deadlineUs = c.deadlineUs;
    if (c.state != ReadState::ACTIVE) { output = next; return Ok(); }
    if (now >= c.deadlineUs) return failed(CommunicationError::DEADLINE_EXPIRED, "communication deadline expired");
    next.kind = ActionWork::TRANSACTION; next.write = c.step == 0; next.reg = c.reg;
    if (next.write) {
        next.value = c.requested; next.count = 1;
        next.length = buildWriteSingleRegister(next.target.address, next.reg, next.value, next.bytes, sizeof(next.bytes));
    } else {
        next.count = 1;
        next.length = buildReadRegisters(next.target.address, next.reg, next.count, next.bytes, sizeof(next.bytes));
    }
    if (!next.length) return invalid(CommunicationError::INVALID_STATE, "invalid prepared communication frame");
    output = next;
    return Ok();
}

Status advanceCommunication(CommunicationContext& c, const ActionEvent& supplied, uint64_t now) noexcept {
    const ReadEvent& e = supplied.transport;
    if (c.state != ReadState::ACTIVE) return invalid(CommunicationError::INVALID_STATE, "communication is not active");
    const ReadTarget& target = c.step == 0 ? c.beforeTarget : c.confirmationTarget;
    if (!same(target, e.target) || c.operationId != e.operationId || c.step != e.step)
        return invalid(CommunicationError::WRONG_CORRELATION, "communication event correlation mismatch");
    if (now < c.servicedUs) return invalid(CommunicationError::CLOCK_ERROR, "communication clock moved backwards");
    if (e.kind > ReadEventKind::DEADLINE || e.txAccepted > READ_REQUEST_LEN ||
        (supplied.txComplete && e.txAccepted != READ_REQUEST_LEN))
        return invalid(CommunicationError::INVALID_EVENT, "invalid communication transport envelope");
    if (e.kind == ReadEventKind::FRAME) {
        if (!e.frame || e.txAccepted != READ_REQUEST_LEN || !supplied.txComplete ||
            (e.qualified && (e.earliestUs < c.eligibleUs || e.earliestUs > e.latestUs || e.latestUs > now)) ||
            (!e.qualified && (e.earliestUs || e.latestUs)))
            return invalid(CommunicationError::INVALID_EVENT, "invalid communication frame envelope");
    } else if ((!e.frame && e.length) || e.qualified || e.earliestUs || e.latestUs || supplied.responseConfirmed ||
               (e.kind == ReadEventKind::DEADLINE && now < c.deadlineUs)) {
        return invalid(CommunicationError::INVALID_EVENT, "invalid local communication event envelope");
    }
    CommunicationEvidence evidence;
    evidence.target = target; evidence.serial = c.step == 0 ? c.beforeSerial : c.confirmationSerial;
    evidence.eligibleUs = c.eligibleUs; evidence.deadlineUs = c.deadlineUs;
    ActionEvidence& wire = evidence.wire;
    wire.step = c.step; wire.event = e.kind; wire.txAccepted = e.txAccepted;
    wire.txComplete = supplied.txComplete; wire.responseConfirmed = supplied.responseConfirmed;
    wire.executionUnknown = e.executionUnknown; wire.qualified = e.qualified;
    wire.earliestUs = e.earliestUs; wire.latestUs = e.latestUs; wire.deliveredUs = now;
    wire.transportDetail = e.transportDetail; wire.receivedLength = e.length;
    wire.length = e.length < ACTION_MAX_REPLY_BYTES ? e.length : ACTION_MAX_REPLY_BYTES;
    if (wire.length) std::memcpy(wire.raw, e.frame, wire.length);
    uint16_t readback = 0; std::size_t count = 0;
    if (e.kind == ReadEventKind::FRAME) {
        wire.status = c.step == 0 ? parseWriteSingleRegister(e.frame, e.length, target.address, c.reg, c.requested, &wire.frameError) :
            parseRegisters(e.frame, e.length, target.address, 1, &readback, 1, count, &wire.frameError);
    } else if (e.kind == ReadEventKind::CANCEL) wire.status = failed(CommunicationError::CANCELLED, "communication locally cancelled");
    else if (e.kind == ReadEventKind::DEADLINE) wire.status = failed(CommunicationError::DEADLINE_EXPIRED, "communication deadline expired");
    else wire.status = failed(CommunicationError::TRANSPORT_FAILURE, "communication transport failed");
    if (c.step == 0) {
        c.execution = e.txAccepted || e.executionUnknown ? ActionExecution::UNKNOWN : ActionExecution::NOT_TRANSMITTED;
        if (e.kind == ReadEventKind::FRAME && e.qualified && e.latestUs <= c.deadlineUs &&
            supplied.responseConfirmed && !e.executionUnknown) {
            if (wire.status && c.request.field != CommunicationField::ADDRESS) c.execution = ActionExecution::ACKNOWLEDGED;
            // Function PDF p12: 05 is a read-count error, hence an unexpected
            // response to FC06. Other documented codes describe validation
            // rejection. Raw effects are retained even for those rejections.
            else if (wire.status.code == Err::EXCEPTION && wire.status.detail >= 1 && wire.status.detail <= 7 &&
                     wire.status.detail != 5)
                c.execution = ActionExecution::REJECTED;
        }
        // Retain accepted-write effects even for a checked rejection, as the
        // other settings engines do; only execution classification is resolved.
        c.effects = e.txAccepted || e.executionUnknown;
        c.uncertain = c.execution == ActionExecution::UNKNOWN || c.execution == ActionExecution::ACKNOWLEDGED;
    }
    c.servicedUs = now;
    if (e.kind == ReadEventKind::CANCEL) finish(c, CommunicationOutcome::CANCELLED, wire.status);
    else if (e.kind == ReadEventKind::DEADLINE) finish(c, CommunicationOutcome::DEADLINE, wire.status);
    else if (e.kind == ReadEventKind::TRANSPORT_FAILURE) finish(c, CommunicationOutcome::TRANSPORT_ERROR, wire.status);
    else if (!e.qualified) finish(c, CommunicationOutcome::TIMING_UNQUALIFIED,
        failed(CommunicationError::TIMING_UNQUALIFIED, "communication closure timing is unqualified"));
    else if (e.latestUs > c.deadlineUs) finish(c, CommunicationOutcome::DEADLINE,
        failed(CommunicationError::DEADLINE_EXPIRED, "communication closure exceeds deadline"));
    else if (!wire.status) finish(c, CommunicationOutcome::REPLY_ERROR, wire.status);
    else if (!supplied.responseConfirmed || e.executionUnknown) finish(c, CommunicationOutcome::UNCONFIRMED_RESPONSE,
        failed(CommunicationError::UNCONFIRMED_RESPONSE, "communication response source unconfirmed"));
    else if (c.step == 0) {
        if (c.request.field == CommunicationField::ADDRESS) finish(c, CommunicationOutcome::ADDRESS_ACK_UNRESOLVED,
            failed(CommunicationError::ADDRESS_ACK_UNRESOLVED, "address acknowledgement and activation rule unresolved"));
        else finish(c, CommunicationOutcome::ACKNOWLEDGED, Ok());
    } else {
        evidence.readback = c.readback = readback;
        evidence.readbackKnown = c.readbackKnown = true;
        c.observedActiveTarget = target; c.observedActiveSerial = evidence.serial; c.observedActiveKnown = true;
        if (readback == c.requested) {
            c.uncertain = false;
            finish(c, CommunicationOutcome::CONFIRMED, Ok());
        } else finish(c, CommunicationOutcome::READBACK_MISMATCH,
            failed(CommunicationError::READBACK_MISMATCH, "communication register differs from candidate"));
    }
    if (c.step == 0) c.writeEvidence = evidence;
    else c.confirmationEvidence[c.step - 1] = evidence;
    return Ok();
}
}} // namespace MotorControlRS::ESS_RS
