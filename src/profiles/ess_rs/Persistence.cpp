// SPDX-License-Identifier: MIT
#include "MotorControlRS/profiles/ess_rs/Persistence.h"
#include "MotorControlRS/profiles/ess_rs/Registers.h"
#include <cstring>
#include <limits>
#include <new>

namespace MotorControlRS { namespace ESS_RS {
namespace {
Status invalid(PersistenceError e, const char* msg) {
    return Status(Err::INVALID_CONFIG, static_cast<int32_t>(e), msg);
}
Status failed(PersistenceError e, const char* msg) {
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
const uint16_t starts[] = {Registers::DEFAULT_DIRECTION, Registers::CUSTOM_NODE,
    Registers::OVER_LIMIT_STOP, Registers::INPUT_POLARITY, Registers::CONTROL_ALGORITHM};
const uint8_t counts[] = {2, 3, 3, 5, 2};
void configWords(const RawConfig& c, uint16_t* w) {
    w[0] = c.direction; w[1] = c.subdivision; w[2] = c.customNode; w[3] = c.baud; w[4] = c.format;
    w[5] = c.overLimitStop; w[6] = c.softLimitEnable; w[7] = c.wordOrder; w[8] = c.inputPolarity;
    for (uint8_t i = 0; i < 4; ++i) w[9 + i] = c.inputFunctions[i];
    w[13] = c.algorithm; w[14] = c.encoderResolution;
}
bool source(const ReadStepObservation& e, const ReadTarget& t, uint16_t first, uint16_t n,
            const uint16_t* expected, uint64_t now, uint64_t age, uint64_t after, uint64_t& freshUntil) {
    if (!age || !e.qualified || !e.status || e.frameError != FrameError::NONE || e.event != ReadEventKind::FRAME ||
        e.first != first || e.count != n || e.length != e.receivedLength || e.length > READ_MAX_REPLY_BYTES ||
        e.txAccepted != READ_REQUEST_LEN || e.executionUnknown || e.attemptedUs < after ||
        e.attemptedUs > e.earliestUs || e.earliestUs > e.latestUs || e.latestUs > e.deliveredUs ||
        e.deliveredUs > now || now - e.attemptedUs >= age) return false;
    uint16_t words[5] = {}; std::size_t count = 0;
    if (!parseRegisters(e.raw, e.length, t.address, n, words, 5, count)) return false;
    for (uint16_t i = 0; i < n; ++i) if (words[i] != expected[i]) return false;
    const uint64_t maximum = std::numeric_limits<uint64_t>::max();
    const uint64_t end = age > maximum - e.attemptedUs ? maximum : e.attemptedUs + age;
    if (end < freshUntil) freshUntil = end;
    return true;
}
Status snapshots(const ReadTarget& t, const ActiveSerialTuple& serial,
                 const IdentityObservation& identity, const ConfigObservation& config,
                 const StateObservation& stationary, uint64_t now, uint64_t age,
                 uint64_t after, uint64_t& freshUntil) {
    if (!valid(serial) || !same(identity.activeSerial, serial) || !same(config.activeSerial, serial) ||
        !same(identity.target, t) || !same(config.target, t) || !same(stationary.target, t) ||
        !identity.operationId || !config.operationId || !stationary.operationId || stationary.block != StateBlock::MOTION)
        return invalid(PersistenceError::STALE_EVIDENCE, "identity/configuration/state binding invalid");
    const uint16_t identityWords[] = {identity.rawModel, identity.rawVersion, identity.rawActiveNode, identity.rawDip};
    if (!source(identity.provenance, t, Registers::DRIVER_MODEL, 4, identityWords, now, age, after, freshUntil))
        return invalid(PersistenceError::STALE_EVIDENCE, "identity provenance invalid or stale");
    uint16_t words[PERSISTENCE_FIELD_COUNT]; configWords(config.raw, words);
    uint8_t offset = 0;
    for (uint8_t i = 0; i < READ_MAX_STEPS; ++i) {
        if (!source(config.provenance[i], t, starts[i], counts[i], words + offset, now, age, after, freshUntil))
            return invalid(PersistenceError::STALE_EVIDENCE, "configuration provenance invalid or stale");
        offset = static_cast<uint8_t>(offset + counts[i]);
    }
    const uint16_t motionWords[] = {stationary.rawAlarm, stationary.rawMotion};
    if (!source(stationary.provenance, t, Registers::ERROR_CODE, 2, motionWords, now, age, after, freshUntil))
        return invalid(PersistenceError::STALE_EVIDENCE, "stationary provenance invalid or stale");
    if (stationary.rawAlarm || (stationary.rawMotion & ~uint16_t(0x0073)))
        return invalid(PersistenceError::STATIONARY_REQUIRED, "fresh known alarm-free stopped state required");
    return Ok();
}
Status prepare(PersistenceContext& c, const ReadTarget& t, uint32_t id, PersistenceKind kind,
               const PersistencePrerequisites& p, uint64_t now, uint64_t deadline) {
    if (&p == &c.before) return invalid(PersistenceError::INVALID_POLICY, "prerequisites alias retained output");
    if (!t.id || !t.generation || !isValidAddress(t.address))
        return invalid(PersistenceError::INVALID_TARGET, "invalid persistence target");
    if (!id) return invalid(PersistenceError::INVALID_OPERATION, "zero persistence operation id");
    if (deadline <= now) return invalid(PersistenceError::INVALID_DEADLINE, "expired persistence deadline");
    if (!p.configurationGeneration || !p.effectsQualified || p.qualifiedKind != kind)
        return invalid(PersistenceError::INVALID_POLICY, "exact persistence scope/effects qualification required");
    if (!p.stationaryQualified)
        return invalid(PersistenceError::STATIONARY_REQUIRED, "stationary qualification required");
    if (kind == PersistenceKind::FACTORY_RESTORE && (!p.completeBackupQualified || !p.backupSourceId))
        return invalid(PersistenceError::BACKUP_REQUIRED, "complete external configuration backup required");
    if (kind == PersistenceKind::FACTORY_RESTORE && !p.routeBackQualified)
        return invalid(PersistenceError::ROUTE_BACK_REQUIRED, "actual factory-restore recommissioning route required");
    uint64_t freshUntil = deadline;
    const Status checked = snapshots(t, p.beforeSerial, p.beforeIdentity, p.beforeConfig, p.stationary,
        now, p.maxAgeUs, 0, freshUntil);
    if (!checked) return checked;
    const uint16_t reg = Registers::AUXILIARY_COMMAND;
    const uint16_t value = static_cast<uint16_t>(kind == PersistenceKind::SAVE ?
        AuxiliaryCommand::SAVE_PARAMETERS : AuxiliaryCommand::RESTORE_FACTORY);
    const Status wire = validateWriteSingleRegisterRequest(t.address, reg, value);
    if (!wire) return wire;
    // Avoid a several-KiB temporary context on MCU stacks. Copy inputs only after
    // all validation, retaining original target/time binding in the snapshots.
    const ReadTarget selected = t;
    c.~PersistenceContext();
    new (&c) PersistenceContext(); // Reconstruct caller-owned storage; no heap allocation or full-context temporary.
    c.target = selected; c.operationId = id; c.kind = kind; c.before = p;
    c.reg = reg; c.value = value; c.state = ReadState::ACTIVE;
    c.startedUs = c.servicedUs = now; c.deadlineUs = freshUntil;
    uint16_t words[PERSISTENCE_FIELD_COUNT]; configWords(p.beforeConfig.raw, words);
    uint8_t index = 0;
    for (uint8_t i = 0; i < READ_MAX_STEPS; ++i) for (uint8_t j = 0; j < counts[i]; ++j, ++index) {
        c.fields[index].reg = static_cast<uint16_t>(starts[i] + j); c.fields[index].before = words[index];
        const RegisterDescriptor* field = findRegister(c.fields[index].reg);
        c.fields[index].sourceAccess = field ? field->sourceAccess : nullptr;
    }
    return Ok();
}
void finish(PersistenceContext& c, PersistenceOutcome outcome, Status s) {
    c.outcome = outcome; c.status = s;
    c.state = outcome == PersistenceOutcome::ACKNOWLEDGED ? ReadState::SUCCEEDED : ReadState::FAILED;
}
} // namespace

Status prepareSave(PersistenceContext& c, const ReadTarget& t, uint32_t id,
                   const PersistencePrerequisites& p, uint64_t now, uint64_t deadline) noexcept {
    return prepare(c, t, id, PersistenceKind::SAVE, p, now, deadline);
}
Status prepareFactoryRestore(PersistenceContext& c, const ReadTarget& t, uint32_t id,
                             const PersistencePrerequisites& p, uint64_t now, uint64_t deadline) noexcept {
    return prepare(c, t, id, PersistenceKind::FACTORY_RESTORE, p, now, deadline);
}
Status nextPersistence(const PersistenceContext& c, uint64_t now, PreparedPersistence& output) noexcept {
    if (c.state == ReadState::EMPTY) return invalid(PersistenceError::INVALID_STATE, "empty persistence operation");
    if (now < c.servicedUs) return invalid(PersistenceError::CLOCK_ERROR, "persistence clock moved backwards");
    PreparedPersistence next; next.target = c.target; next.serial = c.before.beforeSerial;
    next.operationId = c.operationId; next.deadlineUs = c.deadlineUs;
    if (c.state != ReadState::ACTIVE) { output = next; return Ok(); }
    if (now >= c.deadlineUs) return failed(PersistenceError::DEADLINE_EXPIRED, "persistence deadline expired");
    next.kind = ActionWork::TRANSACTION; next.write = true; next.reg = c.reg; next.value = c.value;
    next.length = buildWriteSingleRegister(c.target.address, c.reg, c.value, next.bytes, sizeof(next.bytes));
    if (!next.length) return invalid(PersistenceError::INVALID_STATE, "invalid prepared persistence frame");
    output = next; return Ok();
}
Status advancePersistence(PersistenceContext& c, const ActionEvent& supplied, uint64_t now) noexcept {
    const ReadEvent& e = supplied.transport;
    if (c.state != ReadState::ACTIVE) return invalid(PersistenceError::INVALID_STATE, "persistence operation is not active");
    if (!same(c.target, e.target) || c.operationId != e.operationId || e.step != 0)
        return invalid(PersistenceError::WRONG_CORRELATION, "persistence event correlation mismatch");
    if (now < c.servicedUs) return invalid(PersistenceError::CLOCK_ERROR, "persistence clock moved backwards");
    if (e.kind > ReadEventKind::DEADLINE || e.txAccepted > READ_REQUEST_LEN ||
        (supplied.txComplete && e.txAccepted != READ_REQUEST_LEN))
        return invalid(PersistenceError::INVALID_EVENT, "invalid persistence transport envelope");
    if (e.kind == ReadEventKind::FRAME) {
        if (!e.frame || e.txAccepted != READ_REQUEST_LEN || !supplied.txComplete ||
            (e.qualified && (e.earliestUs < c.startedUs || e.earliestUs > e.latestUs || e.latestUs > now)) ||
            (!e.qualified && (e.earliestUs || e.latestUs)))
            return invalid(PersistenceError::INVALID_EVENT, "invalid persistence frame envelope");
    } else if ((!e.frame && e.length) || e.qualified || e.earliestUs || e.latestUs || supplied.responseConfirmed ||
               (e.kind == ReadEventKind::DEADLINE && now < c.deadlineUs))
        return invalid(PersistenceError::INVALID_EVENT, "invalid local persistence envelope");
    ActionEvidence evidence; evidence.event = e.kind; evidence.txAccepted = e.txAccepted;
    evidence.txComplete = supplied.txComplete; evidence.responseConfirmed = supplied.responseConfirmed;
    evidence.executionUnknown = e.executionUnknown; evidence.qualified = e.qualified;
    evidence.earliestUs = e.earliestUs; evidence.latestUs = e.latestUs; evidence.deliveredUs = now;
    evidence.transportDetail = e.transportDetail; evidence.receivedLength = e.length;
    evidence.length = e.length < ACTION_MAX_REPLY_BYTES ? e.length : ACTION_MAX_REPLY_BYTES;
    if (evidence.length) std::memcpy(evidence.raw, e.frame, evidence.length);
    if (e.kind == ReadEventKind::FRAME)
        evidence.status = parseWriteSingleRegister(e.frame, e.length, c.target.address, c.reg, c.value, &evidence.frameError);
    else if (e.kind == ReadEventKind::CANCEL) evidence.status = failed(PersistenceError::CANCELLED, "persistence locally cancelled");
    else if (e.kind == ReadEventKind::DEADLINE) evidence.status = failed(PersistenceError::DEADLINE_EXPIRED, "persistence deadline expired");
    else evidence.status = failed(PersistenceError::TRANSPORT_FAILURE, "persistence transport failed");
    c.execution = e.txAccepted || e.executionUnknown ? ActionExecution::UNKNOWN : ActionExecution::NOT_TRANSMITTED;
    if (e.kind == ReadEventKind::FRAME && e.qualified && e.latestUs <= c.deadlineUs && supplied.responseConfirmed && !e.executionUnknown) {
        if (evidence.status) c.execution = ActionExecution::ACKNOWLEDGED;
        else if (evidence.status.code == Err::EXCEPTION && evidence.status.detail >= 1 && evidence.status.detail <= 7 && evidence.status.detail != 5)
            c.execution = ActionExecution::REJECTED;
    }
    c.effects = e.txAccepted || e.executionUnknown;
    c.uncertain = c.execution == ActionExecution::UNKNOWN;
    c.configurationInvalidated = c.effects;
    c.manualInterventionRequired = c.effects && c.kind == PersistenceKind::FACTORY_RESTORE;
    c.writeEvidence = evidence; c.servicedUs = now;
    if (e.kind == ReadEventKind::CANCEL) finish(c, PersistenceOutcome::CANCELLED, evidence.status);
    else if (e.kind == ReadEventKind::DEADLINE) finish(c, PersistenceOutcome::DEADLINE, evidence.status);
    else if (e.kind == ReadEventKind::TRANSPORT_FAILURE) finish(c, PersistenceOutcome::TRANSPORT_ERROR, evidence.status);
    else if (!e.qualified) finish(c, PersistenceOutcome::TIMING_UNQUALIFIED, failed(PersistenceError::TIMING_UNQUALIFIED, "persistence closure timing unqualified"));
    else if (e.latestUs > c.deadlineUs) finish(c, PersistenceOutcome::DEADLINE, failed(PersistenceError::DEADLINE_EXPIRED, "persistence closure exceeds deadline"));
    else if (!evidence.status) finish(c, PersistenceOutcome::REPLY_ERROR, evidence.status);
    else if (!supplied.responseConfirmed || e.executionUnknown) finish(c, PersistenceOutcome::UNCONFIRMED_RESPONSE,
        failed(PersistenceError::UNCONFIRMED_RESPONSE, "persistence response source unconfirmed"));
    else finish(c, PersistenceOutcome::ACKNOWLEDGED, Ok());
    return Ok();
}
Status verifyPersistence(PersistenceContext& c, const PersistenceVerification& v, uint64_t now) noexcept {
    if (&v == &c.verification) return invalid(PersistenceError::INVALID_POLICY, "verification aliases retained output");
    if (c.state == ReadState::EMPTY || c.state == ReadState::ACTIVE || c.verificationCount >= 2)
        return invalid(PersistenceError::INVALID_STATE, "verification requires terminal invocation and unused slot");
    if (now < c.servicedUs) return invalid(PersistenceError::CLOCK_ERROR, "persistence clock moved backwards");
    if (!v.configurationGeneration || v.configurationGeneration < c.before.configurationGeneration ||
        !same(v.serial, c.before.beforeSerial))
        return invalid(PersistenceError::WRONG_CORRELATION, "verification configuration/host generation mismatch");
    if ((!v.restartObserved && (v.restartUs || v.restartSourceId)) || (v.restartObserved &&
        (!v.restartSourceId || v.restartUs <= c.writeEvidence.deliveredUs || v.restartUs > now || !c.effects)))
        return invalid(PersistenceError::INVALID_RESTART, "actual post-invocation motor restart evidence required");
    const uint64_t after = v.restartObserved ? v.restartUs : c.writeEvidence.deliveredUs;
    uint64_t freshUntil = std::numeric_limits<uint64_t>::max();
    const Status checked = snapshots(c.target, v.serial, v.identity, v.config, v.stationary, now, v.maxAgeUs, after, freshUntil);
    if (!checked) return checked;
    if (v.identity.rawModel != c.before.beforeIdentity.rawModel || v.identity.rawVersion != c.before.beforeIdentity.rawVersion)
        return invalid(PersistenceError::IDENTITY_MISMATCH, "verification model/firmware differs from retained identity");
    uint16_t words[PERSISTENCE_FIELD_COUNT]; configWords(v.config.raw, words);
    c.verification = v; ++c.verificationCount; c.verificationKnown = c.liveReadbackKnown = true;
    c.matchingFields = c.verifiedFields = 0;
    for (uint8_t i = 0; i < PERSISTENCE_FIELD_COUNT; ++i) {
        PersistenceField& field = c.fields[i]; field.readbackKnown = true; field.readback = words[i];
        const bool match = field.before == field.readback;
        if (match) c.matchingFields |= uint16_t(1u << i);
        field.survivedRestart = match && v.restartObserved && c.kind == PersistenceKind::SAVE;
        if (field.survivedRestart) c.verifiedFields |= uint16_t(1u << i);
    }
    c.persistence = c.verifiedFields ? PersistenceKnowledge::VERIFIED_FIELDS : PersistenceKnowledge::UNVERIFIED;
    c.restartRequiredForProof = c.persistence == PersistenceKnowledge::UNVERIFIED;
    c.communicationChanged = v.config.raw.customNode != c.before.beforeConfig.raw.customNode ||
        v.config.raw.baud != c.before.beforeConfig.raw.baud || v.config.raw.format != c.before.beforeConfig.raw.format ||
        v.identity.rawActiveNode != c.before.beforeIdentity.rawActiveNode;
    c.servicedUs = now;
    return Ok();
}
}} // namespace MotorControlRS::ESS_RS
