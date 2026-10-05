// SPDX-License-Identifier: MIT
#include "MotorControlRS/profiles/ess_rs/Discovery.h"
#include "MotorControlRS/profiles/ess_rs/Registers.h"
#include <cstring>

namespace MotorControlRS { namespace ESS_RS {
namespace {
Status invalid(ReadError reason, const char* message) {
    return Status(Err::INVALID_CONFIG, static_cast<int32_t>(reason), message);
}
Status failure(ReadError reason, const char* message) {
    return Status(Err::ILLEGAL_VALUE, static_cast<int32_t>(reason), message);
}
bool sameTarget(const ReadTarget& a, const ReadTarget& b) {
    return a.id == b.id && a.address == b.address && a.generation == b.generation;
}
bool validTuple(const ActiveSerialTuple& tuple) {
    return !tuple.known || (tuple.baud && tuple.dataBits >= 5 && tuple.dataBits <= 8 &&
        tuple.parity >= SerialParity::NONE && tuple.parity <= SerialParity::ODD &&
        (tuple.stopBits == 1 || tuple.stopBits == 2));
}
} // namespace

DiscoveryCapabilities probeCapabilities() noexcept {
    DiscoveryCapabilities caps;
    const ReadCapabilities reads = readCapabilities();
    caps.probe = reads.probe; caps.identity = reads.identity; caps.nonChanging = true;
    caps.minimumAddress = 1; caps.maximumAddress = 247;
    caps.probeRequestBytes = READ_REQUEST_LEN;
    caps.probeReplyBytes = static_cast<uint8_t>(expectedReadRegistersLen(1));
    caps.exceptionReplyBytes = EXCEPTION_RESPONSE_LEN;
    caps.probeFirst = caps.identityFirst = Registers::DRIVER_MODEL;
    caps.probeCount = 1; caps.identityCount = 4;
    return caps;
}
Status prepareProbe(PreparedProbe& output, const ReadTarget& target,
                    uint32_t operationId, uint64_t nowUs, uint64_t deadlineUs,
                    const ActiveSerialTuple& activeSerial) noexcept {
    if (!target.id || !target.generation || !isValidAddress(target.address))
        return invalid(ReadError::INVALID_TARGET, "invalid probe target");
    if (!operationId) return invalid(ReadError::INVALID_OPERATION, "zero probe operation id");
    if (deadlineUs <= nowUs) return invalid(ReadError::INVALID_DEADLINE, "expired probe deadline");
    if (!validTuple(activeSerial)) return invalid(ReadError::INVALID_TUPLE, "invalid observed serial tuple");
    PreparedProbe prepared;
    prepared.target = target; prepared.operationId = operationId;
    prepared.startedUs = nowUs; prepared.deadlineUs = deadlineUs; prepared.activeSerial = activeSerial;
    prepared.length = buildProbe(target.address, prepared.bytes, sizeof(prepared.bytes));
    if (!prepared.length) return invalid(ReadError::INVALID_STATE, "invalid reviewed probe query");
    output = prepared;
    return Ok();
}
Status checkProbe(const PreparedProbe& prepared, const ReadEvent& event,
                  uint64_t nowUs, ProbeObservation& output) noexcept {
    if (prepared.profile != DriveProfile::ESS_RS)
        return Status(Err::UNSUPPORTED, 0, "discovery profile is not implemented");
    PreparedProbe expected;
    const Status valid = ESS_RS::prepareProbe(expected, prepared.target, prepared.operationId,
        prepared.startedUs, prepared.deadlineUs, prepared.activeSerial);
    if (!valid) return valid;
    if (prepared.length != expected.length || std::memcmp(prepared.bytes, expected.bytes, expected.length))
        return invalid(ReadError::INVALID_STATE, "probe request snapshot was changed");
    if (!sameTarget(prepared.target, event.target) || prepared.operationId != event.operationId || event.step != 0)
        return invalid(ReadError::WRONG_CORRELATION, "probe event correlation mismatch");
    if (nowUs < prepared.startedUs) return invalid(ReadError::CLOCK_ERROR, "probe clock moved backwards");
    if (event.kind > ReadEventKind::DEADLINE || event.txAccepted > prepared.length)
        return invalid(ReadError::INVALID_EVENT, "invalid probe event");
    if (event.kind == ReadEventKind::FRAME) {
        if (!event.frame || (event.qualified && (event.earliestUs < prepared.startedUs ||
            event.earliestUs > event.latestUs || event.latestUs > nowUs)) ||
            (!event.qualified && (event.earliestUs || event.latestUs)))
            return invalid(ReadError::INVALID_EVENT, "invalid probe frame envelope");
    } else if ((!event.frame && event.length) || event.qualified || event.earliestUs || event.latestUs ||
               (event.kind == ReadEventKind::DEADLINE && nowUs < prepared.deadlineUs)) {
        return invalid(ReadError::INVALID_EVENT, "invalid local probe envelope");
    }
    ProbeObservation observed;
    observed.profile = prepared.profile; observed.target = prepared.target;
    observed.operationId = prepared.operationId; observed.activeSerial = prepared.activeSerial;
    ReadStepObservation& provenance = observed.provenance;
    provenance.first = Registers::DRIVER_MODEL; provenance.count = 1;
    provenance.event = event.kind; provenance.attemptedUs = prepared.startedUs;
    provenance.deliveredUs = nowUs; provenance.transportDetail = event.transportDetail;
    provenance.txAccepted = event.txAccepted; provenance.executionUnknown = event.executionUnknown;
    provenance.qualified = event.qualified;
    provenance.earliestUs = event.earliestUs; provenance.latestUs = event.latestUs;
    if (event.frame) {
        provenance.receivedLength = event.length;
        provenance.length = event.length < sizeof(provenance.raw) ? event.length : sizeof(provenance.raw);
        if (provenance.length) std::memcpy(provenance.raw, event.frame, provenance.length);
    }
    if (event.kind == ReadEventKind::CANCEL) {
        observed.outcome = ProbeOutcome::CANCELLED;
        observed.status = failure(ReadError::CANCELLED, "probe locally cancelled");
    } else if (event.kind == ReadEventKind::TRANSPORT_FAILURE) {
        observed.outcome = ProbeOutcome::TRANSPORT_ERROR;
        observed.status = failure(ReadError::TRANSPORT_FAILURE, "probe transport failed");
    } else if (event.kind == ReadEventKind::DEADLINE) {
        observed.outcome = event.length ? ProbeOutcome::DEADLINE : ProbeOutcome::NO_RESPONSE;
        observed.status = failure(ReadError::DEADLINE_EXPIRED, "probe deadline expired");
    } else if (!event.qualified) {
        observed.outcome = ProbeOutcome::TIMING_UNQUALIFIED;
        observed.status = failure(ReadError::TIMING_UNQUALIFIED, "probe closure timing is unqualified");
    } else if (event.latestUs > prepared.deadlineUs) {
        observed.outcome = ProbeOutcome::DEADLINE;
        observed.status = failure(ReadError::DEADLINE_EXPIRED, "probe closure exceeds deadline");
    } else {
        uint16_t model = 0;
        observed.status = parseProbe(event.frame, event.length, prepared.target.address,
            model, &provenance.frameError);
        if (observed.status) {
            observed.outcome = ProbeOutcome::RESPONDER;
            observed.confidence = ProbeConfidence::RESPONDER_MODEL_UNRESOLVED;
            observed.rawModelKnown = true; observed.rawModel = model;
        } else if (observed.status.code == Err::EXCEPTION) {
            observed.outcome = ProbeOutcome::EXCEPTION;
            observed.confidence = ProbeConfidence::RESPONDER_ONLY;
        } else {
            observed.outcome = provenance.frameError == FrameError::ADDRESS
                ? ProbeOutcome::MISMATCH : ProbeOutcome::MALFORMED;
        }
    }
    provenance.status = observed.status;
    output = observed;
    return Ok();
}
}} // namespace MotorControlRS::ESS_RS
