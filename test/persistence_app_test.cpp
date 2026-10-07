// SPDX-License-Identifier: MIT
// The actual standalone application and SDK adapter; fixture evidence is simulated.
#include "../examples/probe_cli/ProbeApp.cpp"
#include "fakes/esp32_uart/UsbConsoleFixture.h"
#include "../examples/probe_cli/ArduinoPlatform.cpp"
#include "../examples/probe_cli/main.cpp"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {
using Kind = Probe::PersistenceCommandKind;
using Persistence = ESS::PersistenceKind;
void fresh() {
    if (app) { app->~App(); std::free(app); app = nullptr; }
    uart.~Esp32S3Uart(); new (&uart) Esp32S3Uart;
    resetHardware(); resetUsbConsole(); platformReady = writeResponseConfirmed = false;
    setup(); assert(app && !hardware.writes);
    hardware.txCharacterUs = 87; assert(uart.startCapture(20, timing().holdUs));
}
void step(uint32_t us = 10) { advanceHardware(hardware.time + us); loop(); }
void pump(unsigned count = 200) { for (unsigned i = 0; i < count; ++i) step(); }
std::vector<uint8_t> crc(std::vector<uint8_t> bytes) {
    const uint16_t value = ESS::calcCrc16(bytes.data(), bytes.size());
    bytes.push_back(static_cast<uint8_t>(value)); bytes.push_back(static_cast<uint8_t>(value >> 8)); return bytes;
}
std::vector<uint8_t> words(std::initializer_list<uint16_t> values, uint8_t address = 1) {
    std::vector<uint8_t> bytes = {address, 3, static_cast<uint8_t>(values.size() * 2)};
    for (auto value : values) { bytes.push_back(static_cast<uint8_t>(value >> 8)); bytes.push_back(static_cast<uint8_t>(value)); }
    return crc(bytes);
}
bool sameRequest(const Rtu::RequestId& a, const Rtu::RequestId& b) {
    return a.owner == b.owner && a.generation == b.generation && a.slot == b.slot;
}
void readReply(uint32_t id, const std::vector<uint8_t>& bytes) {
    auto* record = findRecord(*app, id); assert(record);
    const uint8_t token = record->read.step;
    for (unsigned i = 0; i < 25000 && !app->owner.txAccepted(record->requestId); ++i) step();
    assert(app->owner.txAccepted(record->requestId));
    scheduleReply(std::max(hardware.writeStarted + 8 * 87 + 1000, hardware.time + 1000), bytes);
    for (unsigned i = 0; i < 25000 && record->read.state == MotorControlRS::ReadState::ACTIVE && record->read.step == token; ++i) step();
    assert(record->read.state != MotorControlRS::ReadState::ACTIVE || record->read.step != token);
}
uint32_t typed(ESS::ReadKind kind) {
    uint32_t id = 0;
    assert(host(app).startTypedRead(app, 1, 1, kind, id) == Probe::Action::OK); return id;
}
void configuration(uint16_t custom = 0) {
    const uint32_t id = typed(ESS::ReadKind::CONFIG);
    readReply(id, words({0, 400})); readReply(id, words({custom, 0, 0}));
    readReply(id, words({1, 0, 0})); readReply(id, words({0, 0, 0, 0, 0}));
    readReply(id, words({0, 4000})); pump(); assert(app->configuration.operationId == id);
}
void baseline() {
    const auto identity = typed(ESS::ReadKind::IDENTITY); readReply(identity, words({0x4EEA, 0x0029, 1, 0}));
    configuration();
    const auto state = typed(ESS::ReadKind::STATE);
    readReply(state, words({0, 1})); readReply(state, words({0, 0})); readReply(state, words({0, 0, 0})); pump();
    assert(app->identity.operationId == identity);
    assert(app->stateCache.blocks[0].valid && !app->stateCache.blocks[0].value.running);
}
Probe::PersistenceView inspect() {
    Probe::PersistenceView out; assert(host(app).persistence);
    assert(host(app).persistence(app, nullptr, out) == Probe::Action::OK); return out;
}
Probe::Action command(Kind kind, Persistence operation = Persistence::SAVE) {
    Probe::PersistenceCommand request; request.kind = kind; request.request = operation;
    Probe::PersistenceView out; return host(app).persistence(app, &request, out);
}
void qualify(Persistence kind = Persistence::SAVE) {
    auto& p = app->persistencePrerequisites;
    p.maxAgeUs = 5000000; p.stationaryQualified = p.effectsQualified = true; p.qualifiedKind = kind;
    p.routeBackQualified = true; p.backupSourceId = 77;
    writeResponseConfirmed = true; // Supplied fixture evidence; no production bypass.
}
void begin(Persistence kind = Persistence::SAVE) {
    qualify(kind); assert(command(Kind::BEGIN, kind) == Probe::Action::OK);
    assert(inspect().owned && inspect().pending && app->persistenceRequest.owner);
}
void waitTx() {
    for (unsigned i = 0; i < 25000 && !app->owner.txAccepted(app->persistenceRequest); ++i) step();
    assert(app->owner.txAccepted(app->persistenceRequest));
}
void reply(const std::vector<uint8_t>& supplied = {}) {
    for (unsigned i = 0; i < 25000 && !app->persistenceReadRequest.owner && !app->persistenceRequest.owner; ++i) step();
    assert(app->persistenceReadRequest.owner || app->persistenceRequest.owner);
    auto& selected = app->persistenceReadRequest.owner ? app->persistenceReadRequest : app->persistenceRequest;
    for (unsigned i = 0; i < 25000 && !app->owner.txAccepted(selected); ++i) step();
    assert(app->owner.txAccepted(selected)); const auto request = selected;
    const auto bytes = supplied.empty() ? hardware.tx : supplied;
    scheduleReply(std::max(hardware.writeStarted + 8 * 87 + 1000, hardware.time + 1000), bytes);
    for (unsigned i = 0; i < 50000 && sameRequest(request, selected); ++i) step();
    assert(!sameRequest(request, selected));
}
void verify(uint16_t custom = 0) {
    assert(command(Kind::VERIFY) == Probe::Action::OK);
    // Existing identity/configuration/state reads, one owner and one lease.
    reply(words({0x4EEA, 0x0029, 1, 0}));
    reply(words({0, 400})); reply(words({custom, 0, 0})); reply(words({1, 0, 0}));
    reply(words({0, 0, 0, 0, 0})); reply(words({0, 4000}));
    reply(words({0, 1})); reply(words({0, 0})); reply(words({0, 0, 0}));
    assert(!inspect().pending);
}
void snapshot(uint16_t custom = 0, uint8_t address = 1, uint16_t baud = 0) {
    assert(command(Kind::SNAPSHOT) == Probe::Action::OK);
    reply(words({0x4EEA, 0x0029, address, 0}, address));
    reply(words({0, 400}, address)); reply(words({custom, baud, 0}, address)); reply(words({1, 0, 0}, address));
    reply(words({0, 0, 0, 0, 0}, address)); reply(words({0, 4000}, address));
    reply(words({0, 1}, address)); reply(words({0, 0}, address)); reply(words({0, 0, 0}, address));
    assert(!inspect().pending && app->persistenceBaselineKnown);
}
void recoverHost() {
    for (char c : std::string("@88 recover\n")) app->console.feed(c);
    const auto id = app->recovery.operationId; assert(id);
    for (unsigned i = 0; i < 5000 && !app->recovery.delivered; ++i) step(1000);
    assert(app->owner.recoveryResult(app->recovery.id));
    assert(app->owner.recoveryResult(app->recovery.id)->outcome == Rtu::RecoveryOutcome::RECOVERED);
    assert(host(app).release(app, id) == Probe::Action::OK);
    while (hardware.time <= app->recoveryGuardUntilUs) step(100);
    assert(app->owner.commissioningOwned());
}

void admissionGuards() {
    fresh(); const auto count = hardware.writes;
    assert(command(Kind::BEGIN) != Probe::Action::OK && hardware.writes == count && !inspect().owned);
    baseline(); qualify(); const auto before = hardware.writes;
    auto& motion = app->stateCache.blocks[0]; const auto saved = motion;
    motion.value.running = true; motion.value.rawMotion |= static_cast<uint16_t>(ESS::MotionStatusBit::RUNNING);
    assert(command(Kind::BEGIN) != Probe::Action::OK && hardware.writes == before && !inspect().owned);
    motion = saved; motion.valid = false;
    assert(command(Kind::BEGIN) != Probe::Action::OK && hardware.writes == before && !inspect().owned);
    motion = saved; motion.observedEarliestUs = 1;
    step(5000000);
    assert(command(Kind::BEGIN) != Probe::Action::OK && hardware.writes == before && !inspect().owned);

    fresh(); baseline(); qualify(Persistence::FACTORY_RESTORE);
    assert(command(Kind::BEGIN, Persistence::FACTORY_RESTORE) != Probe::Action::OK && !inspect().owned);
    fresh(); baseline(); qualify(); app->persistencePrerequisites.routeBackQualified = false;
    const auto countBeforeRoute = hardware.writes;
    assert(command(Kind::BEGIN) != Probe::Action::OK && !inspect().owned && hardware.writes == countBeforeRoute);
    app->persistencePrerequisites.routeBackQualified = true;
    assert(command(Kind::BEGIN, Persistence::FACTORY_RESTORE) != Probe::Action::OK && !inspect().owned);
    app->persistencePrerequisites.routeBackQualified = false;
    app->persistencePrerequisites.completeBackupQualified = true; app->persistencePrerequisites.backupSourceId = 77;
    assert(command(Kind::BEGIN, Persistence::FACTORY_RESTORE) != Probe::Action::OK && !inspect().owned);
}
void acknowledgementAndLiveEvidence() {
    fresh(); baseline(); begin(); const auto lease = app->commissioningLease;
    const auto before = app->persistence.before.beforeConfig;
    reply(); assert(app->persistence.outcome == ESS::PersistenceOutcome::ACKNOWLEDGED);
    assert(app->persistence.persistence == ESS::PersistenceKnowledge::UNVERIFIED && app->persistence.restartRequiredForProof);
    assert(app->persistence.before.beforeConfig.operationId == before.operationId &&
        !std::memcmp(&app->persistence.before.beforeConfig.raw, &before.raw, sizeof(before.raw)));
    assert(command(Kind::FINISH) != Probe::Action::OK && app->commissioningLease == lease);
    const auto count = hardware.writes; pump(); assert(hardware.writes == count);
    verify(); assert(app->persistence.liveReadbackKnown && app->persistence.matchingFields == 0x7FFF);
    assert(app->persistence.persistence == ESS::PersistenceKnowledge::UNVERIFIED && !app->persistence.verifiedFields);
    assert(command(Kind::FINISH) == Probe::Action::OK && !app->owner.commissioningOwned());
}
void exclusionAndUnconfirmedReply() {
    fresh(); baseline(); qualify(); writeResponseConfirmed = false;
    assert(command(Kind::BEGIN) == Probe::Action::OK);
    uint32_t unchanged = 99;
    assert(host(app).startProbe(app, 2, 1, unchanged) == Probe::Action::BUSY && unchanged == 99);
    assert(host(app).startTypedRead(app, 2, 1, ESS::ReadKind::IDENTITY, unchanged) == Probe::Action::BUSY);
    Probe::HostRequest hostRequest; hostRequest.tuple.baud = 9600; Probe::HostSnapshot snapshot;
    assert(host(app).hostSerial(app, &hostRequest, snapshot) == Probe::Action::BUSY);
    Probe::MonitorSettings settings; settings.enabled = true; settings.count = 1;
    Probe::MonitorSnapshot monitoring;
    assert(host(app).monitor(app, &settings, monitoring) == Probe::Action::BUSY);
    uint8_t bytes[8]; Rtu::BusRequest work;
    work.wire.bytes = bytes; work.wire.length = ESS::buildProbe(1, bytes, sizeof(bytes));
    work.wire.replyLength = 7; work.wire.responseTimeoutUs = 200000; work.wire.deadlineUs = hardware.time + REQUEST_US;
    work.expected.address = work.expected.target = 1; work.expected.targetGeneration = app->bindingGeneration;
    work.expected.function = 3; work.expected.count = 1; work.validator = Rtu::essValidator(); Rtu::RequestId id;
    assert(app->owner.admit(work, hardware.time, id) == Rtu::BusAdmission::CONFIGURING);
    assert(app->owner.admitUrgent(work, hardware.time, id) == Rtu::BusAdmission::CONFIGURING);
    reply(); assert(app->persistence.outcome == ESS::PersistenceOutcome::UNCONFIRMED_RESPONSE);
    assert(app->persistence.uncertain && app->persistence.execution == MotorControlRS::ActionExecution::UNKNOWN);
    verify(); assert(app->persistence.uncertain && app->persistence.writeEvidence.responseConfirmed == false);
    assert(command(Kind::FINISH) == Probe::Action::OK);
}
void lostReplyRetainsBackup() {
    fresh(); baseline(); app->axis.originKnown = true; begin(); const auto lease = app->commissioningLease;
    const auto saved = app->persistence.before.beforeConfig; waitTx();
    assert(!app->configuration.operationId && !app->axis.originKnown);
    for (unsigned i = 0; i < 1000 && inspect().pending; ++i) step(1000);
    assert(!inspect().pending && app->persistence.uncertain && app->persistence.effects);
    assert(app->persistence.execution == MotorControlRS::ActionExecution::UNKNOWN);
    assert(!std::memcmp(&saved.raw, &app->persistence.before.beforeConfig.raw, sizeof(saved.raw)));
    const auto count = hardware.writes; pump(); assert(hardware.writes == count);
    assert(command(Kind::FINISH) != Probe::Action::OK);
    recoverHost(); assert(app->commissioningLease == lease && hardware.writes == count);
    assert(command(Kind::FINISH) != Probe::Action::OK);
    verify(); assert(app->persistence.uncertain && hardware.writes == count + 9);
    assert(command(Kind::FINISH) == Probe::Action::OK);
}
void failedVerificationDoesNotReplaceBefore() {
    fresh(); baseline(); begin(); reply(); const auto saved = app->persistence.before.beforeConfig;
    const auto count = hardware.writes;
    assert(command(Kind::VERIFY) == Probe::Action::OK);
    reply(words({0x4EEA, 0x0029, 1, 0})); reply(crc({1, 0x83, 2}));
    assert(!inspect().pending && !app->persistence.liveReadbackKnown);
    assert(!std::memcmp(&saved.raw, &app->persistence.before.beforeConfig.raw, sizeof(saved.raw)));
    assert(command(Kind::FINISH) != Probe::Action::OK);
    pump(); assert(hardware.writes == count + 2 && inspect().owned);
    assert(command(Kind::VERIFY) == Probe::Action::OK);
    reply(words({0x4EEA, 0x0029, 1, 0})); reply(crc({1, 0x83, 2}));
    assert(app->persistenceVerificationAttempts == 2 && !app->persistence.liveReadbackKnown);
    assert(command(Kind::VERIFY) == Probe::Action::INVALID);
    pump(); assert(hardware.writes == count + 4 && inspect().owned);
}
void factoryRestoreInvalidatesAssumptions() {
    fresh(); baseline();
    app->axis.originKnown = app->axis.encoderOriginKnown = app->axis.softLimitsKnown = true;
    app->axis.units.commandStepsPerMotorTurn = MotorControlRS::UnitScale(400, 1, MotorControlRS::ScaleSource::QUALIFIED);
    app->axis.units.encoder.countsPerUnit = MotorControlRS::UnitScale(4000, 1, MotorControlRS::ScaleSource::QUALIFIED);
    app->commandPolarityKnown = app->positionClearQualified = true;
    qualify(Persistence::FACTORY_RESTORE); app->persistencePrerequisites.completeBackupQualified = true;
    assert(command(Kind::BEGIN, Persistence::FACTORY_RESTORE) == Probe::Action::OK);
    const auto generation = app->axis.generation; waitTx();
    assert(app->axis.generation > generation && !app->axis.originKnown && !app->axis.encoderOriginKnown && !app->axis.softLimitsKnown);
    assert(!app->axis.units.commandStepsPerMotorTurn.numerator && !app->commandPolarityKnown && !app->positionClearQualified);
    reply(); assert(app->persistence.outcome == ESS::PersistenceOutcome::ACKNOWLEDGED);
    assert(app->persistence.manualInterventionRequired && app->persistence.before.completeBackupQualified);
    verify(2); assert(app->persistence.communicationChanged);
    assert(command(Kind::FINISH) != Probe::Action::OK && app->owner.commissioningOwned());
    const auto count = hardware.writes; recoverHost();
    assert(hardware.writes == count && app->persistence.manualInterventionRequired);
    assert(command(Kind::FINISH) != Probe::Action::OK);
}
void changedCommunicationRetainsOwnership() {
    fresh(); baseline(); begin(); reply(); verify(2);
    assert(app->persistence.communicationChanged && app->persistence.liveReadbackKnown);
    const auto lease = app->commissioningLease;
    assert(command(Kind::FINISH) != Probe::Action::OK);
    assert(app->owner.commissioningOwned() && app->commissioningLease == lease);
}
void completedInvocationCannotReuseOldBaseline() {
    fresh(); snapshot(); begin(); reply(); verify();
    assert(command(Kind::FINISH) == Probe::Action::OK);
    const auto count = hardware.writes; qualify();
    assert(command(Kind::BEGIN) != Probe::Action::OK && hardware.writes == count);
    assert(!app->owner.commissioningOwned());
}
void readOnlySnapshotAndInvocationBudget() {
    fresh(); snapshot();
    assert(app->persistenceInvocations == 0 && hardware.writes == 9 && inspect().owned);
    assert(command(Kind::FINISH) == Probe::Action::OK && !inspect().owned);
    for (unsigned attempt = 0; attempt < 2; ++attempt) {
        snapshot(); begin(); reply(); verify();
        assert(command(Kind::FINISH) == Probe::Action::OK);
        assert(app->persistenceInvocations == attempt + 1);
    }
    snapshot(); qualify(); const auto count = hardware.writes;
    assert(command(Kind::BEGIN) != Probe::Action::OK && hardware.writes == count);
    assert(app->persistenceInvocations == 2);
}
void recoveryInvalidatesVerifiedFinish() {
    fresh(); baseline(); begin(); reply(); verify();
    const auto verified = app->persistence.verification.config.operationId;
    assert(verified && app->persistenceConfirmedGeneration);
    recoverHost();
    assert(app->persistence.verification.config.operationId == verified);
    assert(!app->persistenceConfirmedGeneration && command(Kind::FINISH) != Probe::Action::OK);
    verify(); assert(command(Kind::FINISH) == Probe::Action::OK);
}
Probe::Action communicationCommand(Probe::CommunicationCommandKind kind) {
    Probe::CommunicationCommand value; value.kind = kind; value.request.field = ESS::CommunicationField::ADDRESS;
    value.request.address = 2; Probe::CommunicationView out;
    return host(app).communication(app, &value, out);
}
void communicationReply(const std::vector<uint8_t>& supplied = {}) {
    for (unsigned i = 0; i < 25000 && !app->owner.txAccepted(app->commissioningRequest); ++i) step();
    assert(app->owner.txAccepted(app->commissioningRequest));
    const auto bytes = supplied.empty() ? hardware.tx : supplied;
    scheduleReply(std::max(hardware.writeStarted + 8 * 87 + 1000, hardware.time + 1000), bytes);
    for (unsigned i = 0; i < 50000 && app->commissioningRequest.owner; ++i) step();
    assert(!app->commissioningRequest.owner);
}
void pendingAddressSaveKeepsLease() {
    fresh(); baseline();
    auto& p = app->commissioningPrerequisites;
    p.maxAgeUs = 5000000; p.stationaryQualified = p.effectsQualified = p.routeBackQualified = true;
    p.addressDipOffQualified = true; p.qualifiedRequest.field = ESS::CommunicationField::ADDRESS;
    p.qualifiedRequest.address = 2; writeResponseConfirmed = true;
    assert(communicationCommand(Probe::CommunicationCommandKind::BEGIN) == Probe::Action::OK);
    const auto lease = app->commissioningLease;
    communicationReply();
    assert(communicationCommand(Probe::CommunicationCommandKind::CONFIRM_BEFORE) == Probe::Action::OK);
    communicationReply(words({2})); assert(app->commissioningConfirmedGeneration);
    qualify(); const auto writes = hardware.writes;
    assert(command(Kind::BEGIN) != Probe::Action::OK && hardware.writes == writes);
    snapshot(2); assert(app->commissioningLease == lease);
    begin(); assert(app->commissioningLease == lease);
    reply(); assert(app->persistence.before.beforeConfig.raw.customNode == 2);
    assert(!app->commissioningConfirmedGeneration);
    assert(communicationCommand(Probe::CommunicationCommandKind::FINISH) != Probe::Action::OK);
    assert(command(Kind::FINISH) != Probe::Action::OK && app->commissioningLease == lease);
    verify(2);
    assert(command(Kind::FINISH) == Probe::Action::OK && app->owner.commissioningOwned());
    assert(app->commissioningLease == lease); // Persistence cannot release the enclosing commissioning session.
    assert(communicationCommand(Probe::CommunicationCommandKind::FINISH) != Probe::Action::OK);
    assert(communicationCommand(Probe::CommunicationCommandKind::SELECT_BEFORE) == Probe::Action::OK);
    assert(app->owner.commissioningOwned());
    assert(communicationCommand(Probe::CommunicationCommandKind::CONFIRM_BEFORE) == Probe::Action::OK);
    communicationReply(words({2}));
    assert(communicationCommand(Probe::CommunicationCommandKind::FINISH) == Probe::Action::OK);
}
void historicalCommunicationCannotFinishNewLease() {
    fresh(); baseline();
    auto& p = app->commissioningPrerequisites;
    p.maxAgeUs = 5000000; p.stationaryQualified = p.effectsQualified = p.routeBackQualified = true;
    p.addressDipOffQualified = true; p.qualifiedRequest.field = ESS::CommunicationField::ADDRESS;
    p.qualifiedRequest.address = 2; writeResponseConfirmed = true;
    assert(communicationCommand(Probe::CommunicationCommandKind::BEGIN) == Probe::Action::OK);
    assert(app->owner.cancel(app->commissioningRequest, hardware.time) == Rtu::Cancel::CANCELLED); pump();
    assert(!app->commissioning.effects);
    assert(communicationCommand(Probe::CommunicationCommandKind::FINISH) == Probe::Action::OK);
    const auto historical = app->commissioning.operationId;
    snapshot();
    assert(app->commissioning.operationId == historical && !app->persistenceParentSession);
    assert(command(Kind::SNAPSHOT) == Probe::Action::OK);
    reply(words({0x4EEA, 0x0029, 1, 0}));
    reply(words({0, 400})); reply(words({0, 0, 0})); reply(words({1, 0, 0}));
    reply(words({0, 0, 0, 0, 0})); reply(words({0, 4000}));
    reply(words({0, 1})); reply(words({0, 0})); reply(words({0, 0, 0}));
    begin(); reply(); const auto lease = app->commissioningLease;
    assert(communicationCommand(Probe::CommunicationCommandKind::FINISH) != Probe::Action::OK);
    assert(app->owner.commissioningOwned() && app->commissioningLease == lease);
    assert(app->commissioning.operationId == historical);
}
void requestedAddressSaveInvalidatesPhysicalAxis() {
    fresh(); baseline();
    auto& p = app->commissioningPrerequisites;
    p.maxAgeUs = 5000000; p.stationaryQualified = p.effectsQualified = p.routeBackQualified = true;
    p.addressDipOffQualified = true; p.qualifiedRequest.field = ESS::CommunicationField::ADDRESS;
    p.qualifiedRequest.address = 2; writeResponseConfirmed = true;
    assert(communicationCommand(Probe::CommunicationCommandKind::BEGIN) == Probe::Action::OK);
    communicationReply();
    assert(communicationCommand(Probe::CommunicationCommandKind::CONFIRM_REQUESTED) == Probe::Action::OK);
    communicationReply(words({2}, 2));
    assert(app->commissioning.observedActiveTarget.address == 2 && app->axis.target.address == 1);
    snapshot(2, 2); app->axis.originKnown = app->axis.encoderOriginKnown = app->axis.softLimitsKnown = true;
    app->axis.units.commandStepsPerMotorTurn = MotorControlRS::UnitScale(400, 1, MotorControlRS::ScaleSource::QUALIFIED);
    app->commandPolarityKnown = app->positionClearQualified = true;
    const auto generation = app->axis.generation;
    begin(); waitTx();
    assert(app->persistence.target.address == 2 && app->axis.target.address == 1);
    assert(app->axis.generation > generation && !app->axis.originKnown && !app->axis.encoderOriginKnown && !app->axis.softLimitsKnown);
    assert(!app->axis.units.commandStepsPerMotorTurn.numerator && !app->commandPolarityKnown && !app->positionClearQualified);
    reply();
}
void failedHostSetupRepairNeedsFreshBaseline() {
    fresh(); baseline();
    auto& p = app->commissioningPrerequisites;
    p.maxAgeUs = 5000000; p.stationaryQualified = p.effectsQualified = p.routeBackQualified = true;
    p.qualifiedRequest.field = ESS::CommunicationField::BAUD;
    p.qualifiedRequest.baud = ESS::BaudRateCode::BAUD_38400; writeResponseConfirmed = true;
    auto baudCommand = [](Probe::CommunicationCommandKind kind) {
        Probe::CommunicationCommand value; value.kind = kind; value.request.field = ESS::CommunicationField::BAUD;
        value.request.baud = ESS::BaudRateCode::BAUD_38400; Probe::CommunicationView out;
        return host(app).communication(app, &value, out);
    };
    assert(baudCommand(Probe::CommunicationCommandKind::BEGIN) == Probe::Action::OK); communicationReply();
    assert(baudCommand(Probe::CommunicationCommandKind::CONFIRM_BEFORE) == Probe::Action::OK); communicationReply(words({1}));
    snapshot(0, 1, 1); const auto lease = app->commissioningLease; const auto count = hardware.writes;
    hardware.configFailCalls = {hardware.configCalls + 1, hardware.configCalls + 2};
    assert(baudCommand(Probe::CommunicationCommandKind::SELECT_REQUESTED) == Probe::Action::FAILED);
    assert(app->serial.blocked && !app->serial.activeKnown && app->owner.configurationOwned());
    assert(!app->persistenceBaselineKnown && app->commissioningLease == lease);
    assert(command(Kind::FINISH) != Probe::Action::OK);
    hardware.configFailCalls.clear();
    assert(command(Kind::SELECT_BEFORE) == Probe::Action::OK);
    assert(app->serial.activeKnown && !app->serial.blocked && app->serial.active.baud == 115200);
    assert(app->commissioningLease == lease && app->owner.commissioningOwned() && hardware.writes == count);
    qualify(); assert(command(Kind::BEGIN) != Probe::Action::OK && hardware.writes == count);
    assert(baudCommand(Probe::CommunicationCommandKind::FINISH) != Probe::Action::OK);
    // Host repair does not restore either old confirmation or snapshot eligibility.
    assert(baudCommand(Probe::CommunicationCommandKind::CONFIRM_BEFORE) == Probe::Action::OK); communicationReply(words({1}));
    snapshot(0, 1, 1); begin(); reply();
    assert(app->persistence.before.beforeConfig.raw.baud == 1 && app->commissioningLease == lease);
}
}
int main() {
    admissionGuards(); acknowledgementAndLiveEvidence(); exclusionAndUnconfirmedReply();
    lostReplyRetainsBackup(); failedVerificationDoesNotReplaceBefore();
    factoryRestoreInvalidatesAssumptions();
    changedCommunicationRetainsOwnership(); completedInvocationCannotReuseOldBaseline();
    recoveryInvalidatesVerifiedFinish(); pendingAddressSaveKeepsLease(); historicalCommunicationCannotFinishNewLease();
    requestedAddressSaveInvalidatesPhysicalAxis();
    failedHostSetupRepairNeedsFreshBaseline();
    readOnlySnapshotAndInvocationBudget();
}
