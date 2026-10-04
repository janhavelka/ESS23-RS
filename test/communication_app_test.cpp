// SPDX-License-Identifier: MIT
// Actual standalone application with fake UART/SDK and explicit simulated fixture evidence.
#include "../examples/probe_cli/main.cpp"
#include <cassert>
#include <cstdlib>
#include <string>
#include <vector>

FakeSerial Serial;
namespace {
using Kind = Probe::CommunicationCommandKind;
void fresh() {
    if (app) { app->~App(); std::free(app); app = nullptr; }
    uart.~Esp32S3Uart(); new (&uart) Esp32S3Uart;
    resetHardware(); Serial = FakeSerial(); platformReady = writeResponseConfirmed = false;
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
Probe::ResultView result(uint32_t id) { Probe::ResultView out; assert(host(app).result(app, id, out)); return out; }
void readReply(uint32_t id, const std::vector<uint8_t>& bytes) {
    auto* record = findRecord(*app, id); assert(record);
    const uint8_t token = record->read.step;
    for (unsigned i = 0; i < 25000 && !app->owner.txAccepted(record->requestId); ++i) step();
    assert(app->owner.txAccepted(record->requestId));
    scheduleReply(std::max(hardware.writeStarted + 8 * 87 + 1000, hardware.time + 1000), bytes);
    for (unsigned i = 0; i < 25000 && record->read.state == MotorControlRS::ReadState::ACTIVE && record->read.step == token; ++i) step();
    assert(record->read.state != MotorControlRS::ReadState::ACTIVE || record->read.step != token);
}
void configuration() {
    uint32_t id = 0;
    assert(host(app).startTypedRead(app, 1, 1, ESS::ReadKind::CONFIG, id) == Probe::Action::OK);
    readReply(id, words({0, 400})); readReply(id, words({1, 0, 0}));
    readReply(id, words({1, 0, 0})); readReply(id, words({0, 0, 0, 0, 0}));
    readReply(id, words({0, 4000})); pump();
    assert(app->configuration.operationId == id && app->configuration.provenance[1].qualified);
}
ESS::CommunicationRequest baudRequest() {
    ESS::CommunicationRequest request; request.field = ESS::CommunicationField::BAUD;
    request.baud = ESS::BaudRateCode::BAUD_38400; return request;
}
void qualify(const ESS::CommunicationRequest& request) {
    auto& p = app->commissioningPrerequisites;
    p.maxAgeUs = 5000000; p.stationaryQualified = p.effectsQualified = p.routeBackQualified = true;
    p.addressDipOffQualified = true; p.qualifiedRequest = request;
    writeResponseConfirmed = true; // Simulated source/timing qualification, not hardware evidence.
}
Probe::Action command(Kind kind, const ESS::CommunicationRequest& request = baudRequest()) {
    Probe::CommunicationCommand supplied; supplied.kind = kind; supplied.request = request;
    Probe::CommunicationView out; assert(host(app).communication);
    return host(app).communication(app, &supplied, out);
}
void begin(const ESS::CommunicationRequest& request = baudRequest()) {
    qualify(request); assert(command(Kind::BEGIN, request) == Probe::Action::OK);
    assert(app->owner.commissioningOwned() && app->commissioningRequest.owner);
}
void waitTx() {
    for (unsigned i = 0; i < 25000 && !app->owner.txAccepted(app->commissioningRequest); ++i) step();
    assert(app->owner.txAccepted(app->commissioningRequest));
}
void reply(const std::vector<uint8_t>& supplied = {}) {
    waitTx();
    const auto bytes = supplied.empty() ? hardware.tx : supplied;
    const auto character = hardware.txCharacterUs;
    scheduleReply(std::max(hardware.writeStarted + 8 * character + app->serial.timing.replyGapUs + 500,
        hardware.time + app->serial.timing.replyGapUs + 500), bytes, character);
    for (unsigned i = 0; i < 100000 && app->commissioningRequest.owner; ++i) step();
    assert(!app->commissioningRequest.owner);
}
void recoverHost() {
    for (char c : std::string("@88 recover\n")) app->console.feed(c);
    const uint32_t id = app->recovery.operationId; assert(id);
    for (unsigned i = 0; i < 5000 && (result(id).pending || !app->recovery.delivered); ++i) step(1000);
    assert(!result(id).pending && result(id).recoveryResult.outcome == Rtu::RecoveryOutcome::RECOVERED);
    assert(host(app).release(app, id) == Probe::Action::OK);
    // Explicit application late-response guard is retained by recovery.
    while (hardware.time <= app->recoveryGuardUntilUs) step(100);
    assert(app->owner.commissioningOwned());
}
void changeCharacter() {
    hardware.txCharacterUs = (1000000 * bitsPerCharacter(app->serial.active.format) + app->serial.active.baud - 1) /
        app->serial.active.baud;
}

void prerequisitesAndExclusiveAdmission() {
    fresh(); configuration(); const auto initial = hardware.writes;
    assert(command(Kind::BEGIN) == Probe::Action::UNAVAILABLE && hardware.writes == initial);
    qualify(baudRequest());
    auto invalid = baudRequest(); invalid.baud = static_cast<ESS::BaudRateCode>(99);
    assert(command(Kind::BEGIN, invalid) == Probe::Action::INVALID && !app->owner.commissioningOwned());
    Probe::MonitorSettings settings; settings.enabled = true; settings.count = 1; settings.intervalMs = 100;
    Probe::MonitorSnapshot monitoring;
    assert(host(app).monitor(app, &settings, monitoring) == Probe::Action::OK);
    assert(command(Kind::BEGIN) == Probe::Action::BUSY);
    settings.enabled = false; assert(host(app).monitor(app, &settings, monitoring) == Probe::Action::OK);
    uint32_t id = 0; assert(host(app).startProbe(app, 2, 1, id) == Probe::Action::OK);
    assert(command(Kind::BEGIN) == Probe::Action::BUSY && app->owner.pending());
    assert(host(app).cancel(app, id) == Probe::Action::OK); pump();
    begin(); uint32_t unchanged = 123;
    assert(host(app).startProbe(app, 3, 1, unchanged) == Probe::Action::BUSY);
    assert(host(app).startTypedRead(app, 3, 1, ESS::ReadKind::CONFIG, unchanged) == Probe::Action::BUSY);
    MotorControlRS::ActionRequest stop; stop.kind = MotorControlRS::ActionKind::STOP;
    assert(host(app).startAction(app, 3, 1, stop, unchanged) == Probe::Action::BUSY);
    ESS::DriverRequest driver;
    assert(host(app).startDriver(app, 3, 1, ESS::DriverKind::READ, driver, unchanged) == Probe::Action::BUSY);
    settings.enabled = true; assert(host(app).monitor(app, &settings, monitoring) == Probe::Action::BUSY);
    Probe::HostRequest serial; serial.tuple.baud = 9600; Probe::HostSnapshot snapshot;
    assert(host(app).hostSerial(app, &serial, snapshot) == Probe::Action::BUSY);
    assert(unchanged == 123 && hardware.writes == initial);
    assert(command(Kind::FINISH) == Probe::Action::BUSY);
}

void storedReadbackAndKnownRestoration() {
    fresh(); configuration(); begin(); const unsigned count = hardware.writes;
    reply(); assert(app->commissioning.effects && !app->commissioning.observedActiveKnown);
    assert(app->commissioning.restart == ESS::CommunicationRequirement::REQUIRED);
    assert(command(Kind::FINISH) == Probe::Action::RECOVERY_REQUIRED);
    assert(command(Kind::CONFIRM_REQUESTED) == Probe::Action::INVALID);
    assert(command(Kind::CONFIRM_BEFORE) == Probe::Action::OK); reply(words({1}));
    assert(app->commissioning.readbackKnown && app->commissioning.readback == 1);
    assert(app->commissioning.observedActiveKnown && app->commissioning.observedActiveSerial.baud == 115200);
    assert(app->commissioning.requestedSerial.baud == 38400 && app->commissioning.activationUnknown);
    assert(hardware.writes == count + 2 && command(Kind::FINISH) == Probe::Action::OK);
    assert(!app->owner.commissioningOwned());
    uint32_t probeId = 0; assert(host(app).startProbe(app, 4, 1, probeId) == Probe::Action::OK);
    assert(app->owner.pending());
}

void lostAcknowledgementRecoveryNoReplay() {
    fresh(); configuration(); begin(); waitTx(); const auto count = hardware.writes;
    for (unsigned i = 0; i < 100000 && app->commissioningRequest.owner; ++i) step();
    assert(!app->commissioningRequest.owner && app->commissioning.uncertain);
    const auto before = app->commissioning.beforeSerial;
    const auto requested = app->commissioning.requestedSerial;
    assert(command(Kind::FINISH) == Probe::Action::RECOVERY_REQUIRED);
    recoverHost(); pump();
    assert(hardware.writes == count && app->commissioning.beforeSerial.baud == before.baud &&
        app->commissioning.requestedSerial.baud == requested.baud && app->commissioning.uncertain);
    assert(command(Kind::FINISH) == Probe::Action::RECOVERY_REQUIRED);
    assert(command(Kind::CONFIRM_BEFORE) == Probe::Action::OK); reply(words({1}));
    assert(app->commissioning.writeOutcome == ESS::CommunicationOutcome::TRANSPORT_ERROR);
    assert(command(Kind::FINISH) == Probe::Action::OK && hardware.writes == count + 1);
}

void failedHostSelectionAndRestore() {
    fresh(); configuration(); begin(); reply(); const unsigned count = hardware.writes;
    hardware.configFailCalls = {hardware.configCalls + 1, hardware.configCalls + 2};
    assert(command(Kind::SELECT_REQUESTED) == Probe::Action::FAILED);
    assert(app->serial.blocked && !app->serial.activeKnown && app->owner.configurationOwned());
    assert(command(Kind::FINISH) == Probe::Action::RECOVERY_REQUIRED);
    assert(command(Kind::CONFIRM_BEFORE) == Probe::Action::RECOVERY_REQUIRED);
    hardware.configFailCalls = {hardware.configCalls + 1, hardware.configCalls + 2};
    assert(command(Kind::SELECT_BEFORE) == Probe::Action::FAILED);
    assert(app->owner.commissioningOwned() && app->commissioning.requestedSerial.baud == 38400);
    hardware.configFailCalls.clear(); assert(command(Kind::SELECT_BEFORE) == Probe::Action::OK);
    assert(app->serial.activeKnown && app->owner.commissioningOwned() && hardware.writes == count);
    assert(command(Kind::FINISH) == Probe::Action::RECOVERY_REQUIRED);
    assert(command(Kind::CONFIRM_BEFORE) == Probe::Action::OK); reply(words({1}));
    assert(command(Kind::FINISH) == Probe::Action::OK);
}

void mismatchedReadbackAndExplicitSecondCandidate() {
    fresh(); configuration(); begin(); reply();
    assert(command(Kind::CONFIRM_BEFORE) == Probe::Action::OK); reply(words({0}));
    assert(app->commissioning.outcome == ESS::CommunicationOutcome::READBACK_MISMATCH);
    assert(app->commissioning.readback == 0 && app->commissioning.uncertain);
    // Even a mismatched stored value can establish which interface responded.
    assert(app->commissioning.observedActiveKnown && app->commissioning.observedActiveSerial.baud == 115200);
    assert(command(Kind::SELECT_REQUESTED) == Probe::Action::OK); changeCharacter();
    assert(command(Kind::FINISH) == Probe::Action::RECOVERY_REQUIRED);
    assert(command(Kind::CONFIRM_REQUESTED) == Probe::Action::OK); reply(words({1}));
    assert(app->commissioning.observedActiveSerial.baud == 38400 && app->commissioning.confirmations == 2);
    assert(command(Kind::CONFIRM_BEFORE) == Probe::Action::INVALID);
    assert(command(Kind::FINISH) == Probe::Action::OK);
}

void oldAndNewAddressExpectations() {
    fresh(); configuration(); auto request = baudRequest(); request.field = ESS::CommunicationField::ADDRESS; request.address = 2;
    begin(request); reply();
    assert(app->commissioning.writeOutcome == ESS::CommunicationOutcome::ADDRESS_ACK_UNRESOLVED);
    assert(app->commissioning.save == ESS::CommunicationRequirement::REQUIRED);
    assert(command(Kind::CONFIRM_REQUESTED) == Probe::Action::OK); reply(words({2}, 1));
    assert(!app->commissioning.observedActiveKnown && app->owner.needsRecovery());
    assert(command(Kind::FINISH) == Probe::Action::RECOVERY_REQUIRED);
    recoverHost(); assert(command(Kind::CONFIRM_REQUESTED) == Probe::Action::OK); reply(words({2}, 2));
    assert(app->commissioning.observedActiveTarget.address == 2 && app->commissioning.beforeTarget.address == 1);
    assert(command(Kind::FINISH) == Probe::Action::OK && app->axis.target.address == 2 && app->address == 2);
    Probe::Snapshot snapshot; host(app).snapshot(app, snapshot); assert(snapshot.address == 2);
    Serial.input = "@99 probe\n";
    for (unsigned i = 0; i < 1000 && !Serial.input.empty(); ++i) step();
    const auto* probe = findRecord(*app, app->latestOperationId);
    assert(probe && probe->address == 2);
    assert(host(app).cancel(app, probe->operationId) == Probe::Action::OK); pump();
    Probe::MonitorSettings settings; settings.enabled = true; settings.count = 1; settings.intervalMs = 100;
    Probe::MonitorSnapshot monitoring;
    assert(host(app).monitor(app, &settings, monitoring) == Probe::Action::OK);
    for (unsigned i = 0; i < 1000 && !app->records[REQUEST_CAPACITY].operationId; ++i) step();
    const auto& monitored = app->records[REQUEST_CAPACITY];
    assert(monitored.operationId && monitored.address == 2 && monitored.read.target.address == 2);
}

void unsentWriteDoesNotApproveChangedHost() {
    fresh(); configuration(); begin(); const auto count = hardware.writes;
    assert(app->owner.cancel(app->commissioningRequest, hardware.time) == Rtu::Cancel::CANCELLED); pump();
    assert(!app->commissioning.effects && app->commissioning.execution == MotorControlRS::ActionExecution::NOT_TRANSMITTED);
    assert(command(Kind::SELECT_REQUESTED) == Probe::Action::OK);
    assert(command(Kind::FINISH) == Probe::Action::RECOVERY_REQUIRED && app->owner.commissioningOwned());
    assert(command(Kind::SELECT_BEFORE) == Probe::Action::OK);
    assert(command(Kind::FINISH) == Probe::Action::OK && hardware.writes == count);
}

void staleProbeCannotRestoreConfidence() {
    fresh(); configuration();
    for (char c : std::string("@9 probe\n")) app->console.feed(c);
    const uint32_t id = app->latestOperationId;
    auto* old = findRecord(*app, id); assert(old);
    for (unsigned i = 0; i < 25000 && !app->owner.txAccepted(old->requestId); ++i) {
        advanceHardware(hardware.time + 10); app->owner.service(uart.sample());
    }
    scheduleReply(std::max(hardware.writeStarted + 8 * 87 + 1000, hardware.time + 1000), words({60}));
    for (unsigned i = 0; i < 25000 && !app->owner.result(old->requestId); ++i) {
        advanceHardware(hardware.time + 10); app->owner.service(uart.sample());
    }
    assert(app->owner.result(old->requestId) && !old->observed);
    begin();
    for (unsigned i = 0; i < 25000 && !app->owner.txAccepted(app->commissioningRequest); ++i) {
        advanceHardware(hardware.time + 10); app->owner.service(uart.sample());
    }
    serviceCommissioning(*app, uart.sample()); pump();
    assert(old->delivered && !app->modelKnown && !app->communicationKnown && !app->knownTargets[0]);
    assert(result(id).probe.outcome == Rtu::Outcome::SUCCESS);
}

void rejectedWriteRetainsCandidates() {
    fresh(); configuration(); begin(); reply(crc({1, 0x86, 3}));
    assert(app->commissioning.execution == MotorControlRS::ActionExecution::REJECTED);
    assert(app->commissioning.writeOutcome == ESS::CommunicationOutcome::REPLY_ERROR);
    assert(app->commissioning.beforeSerial.baud == 115200 && app->commissioning.requestedSerial.baud == 38400);
    assert(!app->owner.needsRecovery() && command(Kind::FINISH) == Probe::Action::RECOVERY_REQUIRED);
    const unsigned count = hardware.writes; pump(); assert(hardware.writes == count);
    assert(command(Kind::CONFIRM_BEFORE) == Probe::Action::OK); reply(words({0}));
    assert(app->commissioning.outcome == ESS::CommunicationOutcome::READBACK_MISMATCH);
    assert(command(Kind::FINISH) == Probe::Action::OK && hardware.writes == count + 1);
}

void delayedDeliveryUsesCapturedClosure() {
    fresh(); configuration(); begin(); waitTx();
    const auto request = app->commissioningRequest;
    const unsigned count = hardware.writes;
    scheduleReply(std::max(hardware.writeStarted + 8 * 87 + 1000, hardware.time + 1000), hardware.tx);
    for (unsigned i = 0; i < 25000 && !app->owner.result(request); ++i) {
        advanceHardware(hardware.time + 10); app->owner.service(uart.sample());
    }
    assert(app->owner.result(request) && app->owner.result(request)->outcome == Rtu::Outcome::SUCCESS);
    advanceHardware(app->commissioning.deadlineUs + 1000);
    serviceCommissioning(*app, uart.sample());
    assert(!app->commissioningRequest.owner && app->commissioning.writeOutcome == ESS::CommunicationOutcome::ACKNOWLEDGED);
    assert(app->commissioning.writeEvidence.wire.latestUs <= app->commissioning.deadlineUs);
    assert(app->commissioning.writeEvidence.wire.deliveredUs > app->commissioning.deadlineUs);
    assert(app->owner.commissioningOwned() && hardware.writes == count);
}

void recoveryInvalidatesPriorConfirmation() {
    fresh(); configuration(); begin(); reply();
    assert(command(Kind::CONFIRM_BEFORE) == Probe::Action::OK); reply(words({1}));
    assert(app->commissioning.observedActiveKnown && app->commissioningConfirmedGeneration);
    const auto prior = app->commissioning.confirmationEvidence[0].wire.latestUs;
    recoverHost();
    assert(app->commissioning.confirmationEvidence[0].wire.latestUs == prior);
    assert(command(Kind::FINISH) == Probe::Action::RECOVERY_REQUIRED);
    assert(command(Kind::CONFIRM_BEFORE) == Probe::Action::OK); reply(words({1}));
    assert(command(Kind::FINISH) == Probe::Action::OK);
}

void recoveryExcludesUnharvestedConfirmation() {
    fresh(); configuration(); begin(); reply();
    assert(command(Kind::CONFIRM_BEFORE) == Probe::Action::OK); waitTx();
    const auto request = app->commissioningRequest;
    scheduleReply(std::max(hardware.writeStarted + 8 * 87 + 1000, hardware.time + 1000), words({1}));
    for (unsigned i = 0; i < 25000 && !app->owner.result(request); ++i) {
        advanceHardware(hardware.time + 10); app->owner.service(uart.sample());
    }
    assert(app->owner.result(request) && !app->commissioning.observedActiveKnown);
    recoverHost();
    assert(app->commissioning.observedActiveKnown); // Historical successful reply survives.
    assert(command(Kind::FINISH) == Probe::Action::RECOVERY_REQUIRED);
    assert(command(Kind::CONFIRM_BEFORE) == Probe::Action::OK); reply(words({1}));
    assert(command(Kind::FINISH) == Probe::Action::OK);
}

void unchangedHostSelectionPreservesConfirmation() {
    fresh(); configuration(); begin(); reply();
    assert(command(Kind::CONFIRM_BEFORE) == Probe::Action::OK); reply(words({1}));
    assert(command(Kind::CONFIRM_BEFORE) == Probe::Action::OK); reply(words({1}));
    assert(app->commissioning.confirmations == 2);
    const auto generation = app->serial.generation;
    const auto writes = hardware.writes;
    const auto configurations = hardware.configCalls;
    assert(app->commissioningConfirmedGeneration == generation);
    assert(command(Kind::SELECT_BEFORE) == Probe::Action::OK);
    assert(app->serial.generation == generation && hardware.configCalls == configurations);
    assert(app->commissioningConfirmedGeneration == generation);
    assert(command(Kind::FINISH) == Probe::Action::OK && hardware.writes == writes);
}

void confirmationWaitsForRecoveryQuarantine() {
    fresh(); configuration(); begin(); reply();
    uint32_t id = 0;
    assert(host(app).recover(app, 77, id) == Probe::Action::OK);
    const auto guard = app->recoveryGuardUntilUs;
    const auto writes = hardware.writes;
    while (hardware.time + 100 < guard) {
        assert(command(Kind::CONFIRM_BEFORE) == Probe::Action::RECOVERY_REQUIRED);
        step(100);
    }
    assert(!app->commissioning.confirmations && hardware.writes == writes);
    for (unsigned i = 0; i < 10000 && !app->owner.recoveryResult(app->recovery.id); ++i) step();
    assert(hardware.time >= guard);
    assert(app->owner.recoveryResult(app->recovery.id)->outcome == Rtu::RecoveryOutcome::RECOVERED);
    assert(command(Kind::CONFIRM_BEFORE) == Probe::Action::OK); reply(words({1}));
    assert(command(Kind::FINISH) == Probe::Action::OK);
}
}
int main() {
    prerequisitesAndExclusiveAdmission(); storedReadbackAndKnownRestoration();
    lostAcknowledgementRecoveryNoReplay(); failedHostSelectionAndRestore();
    mismatchedReadbackAndExplicitSecondCandidate(); oldAndNewAddressExpectations();
    unsentWriteDoesNotApproveChangedHost(); staleProbeCannotRestoreConfidence();
    rejectedWriteRetainsCandidates(); delayedDeliveryUsesCapturedClosure();
    recoveryInvalidatesPriorConfirmation();
    recoveryExcludesUnharvestedConfirmation();
    unchangedHostSelectionPreservesConfirmation();
    confirmationWaitsForRecoveryQuarantine();
}
