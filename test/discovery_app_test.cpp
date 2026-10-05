// SPDX-License-Identifier: MIT
// Actual setup/loop, owner, profile parsers and UART adapter; SDK/wire are faked.
#include "../examples/probe_cli/main.cpp"
#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <vector>

FakeSerial Serial;
namespace {
using ScanPhase = Probe::DiscoveryPhase;
using ScanOutcome = Probe::DiscoveryOutcome;
using ProbeOutcome = MotorControlRS::ProbeOutcome;
void fresh() {
    if (app) { app->~App(); std::free(app); app = nullptr; }
    uart.~Esp32S3Uart(); new (&uart) Esp32S3Uart;
    resetHardware(); Serial = FakeSerial(); platformReady = false; writeResponseConfirmed = false;
    setup(); assert(app && uart.ready() && app->owner.valid() && !hardware.writes);
    hardware.txCharacterUs = 87;
    assert(uart.startCapture(20, timing().holdUs));
}
void step(uint32_t us = 10) {
    hardware.txCharacterUs = (1000000U * bitsPerCharacter(app->serial.active.format) +
        app->serial.active.baud - 1) / app->serial.active.baud;
    advanceHardware(hardware.time + us); loop();
}
void pump(unsigned count = 200) { for (unsigned i = 0; i < count; ++i) step(); }
Probe::Action control(Probe::DiscoveryCommandKind kind, const Probe::DiscoverySettings* settings = nullptr) {
    Probe::DiscoveryCommand command; command.kind = kind;
    if (settings) command.settings = *settings;
    Probe::DiscoveryView view;
    assert(host(app).discovery);
    return host(app).discovery(app, &command, view);
}
const Probe::DiscoveryScan& scan() {
    Probe::DiscoveryView view;
    assert(host(app).discovery(app, nullptr, view) == Probe::Action::OK && view.scan);
    return *view.scan;
}
std::vector<uint8_t> crc(std::vector<uint8_t> bytes) {
    const uint16_t value = ESS::calcCrc16(bytes.data(), bytes.size());
    bytes.push_back(static_cast<uint8_t>(value)); bytes.push_back(static_cast<uint8_t>(value >> 8));
    return bytes;
}
std::vector<uint8_t> words(uint8_t address, std::initializer_list<uint16_t> values) {
    std::vector<uint8_t> bytes = {address, 3, static_cast<uint8_t>(2 * values.size())};
    for (auto value : values) { bytes.push_back(static_cast<uint8_t>(value >> 8)); bytes.push_back(static_cast<uint8_t>(value)); }
    return crc(bytes);
}
void waitTx(unsigned before) {
    for (unsigned i = 0; i < 100000 && hardware.writes == before; ++i) step();
    assert(hardware.writes == before + 1 && hardware.tx.size() == 8);
}
void reviewedFrame(uint8_t address, uint16_t count) {
    assert(hardware.tx[0] == address && hardware.tx[1] == 3 && hardware.tx[2] == 0 && hardware.tx[3] == 0);
    assert(hardware.tx[4] == 0 && hardware.tx[5] == count);
    assert(ESS::calcCrc16(hardware.tx.data(), hardware.tx.size()) == 0);
}
void reply(unsigned before, const std::vector<uint8_t>& bytes) {
    waitTx(before);
    const uint32_t character = hardware.txCharacterUs;
    scheduleReply(std::max(hardware.writeStarted + 8 * character + app->serial.timing.replyGapUs + 500,
        hardware.time + app->serial.timing.replyGapUs + 500), bytes, character);
    const uint64_t finish = hardware.events.back().at + app->serial.timing.runner.gap35Us + 3000;
    while (hardware.time < finish) step();
}
void waitSettled() {
    for (unsigned i = 0; i < 100000 && scan().phase != ScanPhase::TERMINAL && scan().phase != ScanPhase::INTERLOCK; ++i) step();
    assert(scan().phase == ScanPhase::TERMINAL || scan().phase == ScanPhase::INTERLOCK);
}
Probe::DiscoverySettings range(uint8_t first, uint8_t last) {
    Probe::DiscoverySettings value; value.first = first; value.last = last; return value;
}
void minimalCurrentTupleAndRetainedEvidence() {
    fresh(); const auto original = app->serial.active;
    const auto target = app->axis.target; const auto generation = app->axis.generation;
    assert(control(Probe::DiscoveryCommandKind::BEGIN) == Probe::Action::OK);
    waitTx(0); reviewedFrame(1, 1);
    reply(0, words(1, {0x4EEA})); waitSettled();
    assert(scan().outcome == ScanOutcome::COMPLETE && scan().restored && scan().count == 1 && scan().requests == 1);
    const auto& finding = scan().findings[0];
    assert(finding.probe.outcome == ProbeOutcome::RESPONDER && finding.probe.rawModelKnown && finding.probe.rawModel == 0x4EEA);
    assert(finding.probe.confidence == MotorControlRS::ProbeConfidence::RESPONDER_MODEL_UNRESOLVED);
    assert(!finding.collisionExcluded && !finding.identityAttempted && !finding.identityKnown);
    assert(finding.request.target.address == 1 && finding.request.activeSerial.baud == original.baud);
    assert(finding.probe.provenance.latestUs && finding.probe.provenance.latestUs <= finding.request.deadlineUs);
    assert(sameTuple(app->serial.active, original) && app->axis.target.id == target.id && app->axis.target.address == target.address);
    assert(app->axis.generation == generation && hardware.writes == 1);
    const auto observed = finding.probe.provenance.latestUs;
    reset(app); pump();
    assert(scan().findings[0].probe.provenance.latestUs == observed && scan().count == 1 && hardware.writes == 1);
}
void rejectedCandidatesAndExclusiveAdmission() {
    fresh(); const unsigned configurations = hardware.configCalls;
    Probe::DiscoverySettings candidates;
    candidates.first = 2; candidates.last = 1;
    assert(control(Probe::DiscoveryCommandKind::BEGIN, &candidates) == Probe::Action::INVALID);
    candidates = range(0, 2);
    assert(control(Probe::DiscoveryCommandKind::BEGIN, &candidates) == Probe::Action::INVALID);
    candidates = range(1, 248);
    assert(control(Probe::DiscoveryCommandKind::BEGIN, &candidates) == Probe::Action::INVALID);
    candidates = range(1, 1); candidates.tupleCount = 2;
    assert(control(Probe::DiscoveryCommandKind::BEGIN, &candidates) == Probe::Action::INVALID);
    candidates = range(1, 1); candidates.tupleCount = 1; candidates.tuples[0].baud = 57600;
    assert(control(Probe::DiscoveryCommandKind::BEGIN, &candidates) == Probe::Action::UNSUPPORTED);
    candidates = range(1, 1); candidates.queryMs = 0;
    assert(control(Probe::DiscoveryCommandKind::BEGIN, &candidates) == Probe::Action::INVALID);
    candidates = range(1, 1); candidates.overallMs = 0;
    assert(control(Probe::DiscoveryCommandKind::BEGIN, &candidates) == Probe::Action::INVALID);
    candidates = range(1, 1); candidates.requestLimit = 0;
    assert(control(Probe::DiscoveryCommandKind::BEGIN, &candidates) == Probe::Action::INVALID);
    candidates = range(1, 1); candidates.resultLimit = 0;
    assert(control(Probe::DiscoveryCommandKind::BEGIN, &candidates) == Probe::Action::INVALID);
    candidates = range(1, 1); candidates.resultLimit = Probe::DISCOVERY_MAX_RESULTS + 1;
    assert(control(Probe::DiscoveryCommandKind::BEGIN, &candidates) == Probe::Action::INVALID);
    candidates = range(1, 1); candidates.profile = static_cast<MotorControlRS::DriveProfile>(99);
    assert(control(Probe::DiscoveryCommandKind::BEGIN, &candidates) == Probe::Action::UNSUPPORTED);
    assert(hardware.configCalls == configurations && !hardware.writes && !app->owner.pending());

    Probe::MonitorSettings monitoring; monitoring.enabled = true; monitoring.intervalMs = 100; monitoring.count = 1;
    Probe::MonitorSnapshot monitored;
    assert(host(app).monitor(app, &monitoring, monitored) == Probe::Action::OK);
    assert(control(Probe::DiscoveryCommandKind::BEGIN) == Probe::Action::BUSY);
    monitoring.enabled = false;
    assert(host(app).monitor(app, &monitoring, monitored) == Probe::Action::OK);
    assert(control(Probe::DiscoveryCommandKind::BEGIN) == Probe::Action::OK);
    uint32_t unchanged = 123;
    assert(host(app).startProbe(app, 4, 1, unchanged) == Probe::Action::BUSY);
    assert(host(app).startTypedRead(app, 4, 1, ESS::ReadKind::IDENTITY, unchanged) == Probe::Action::BUSY);
    ESS::DriverRequest driver; ESS::HomeRequest home;
    MotorControlRS::MoveRequest move; MotorControlRS::VelocityRequest velocity;
    MotorControlRS::ActionRequest enable; enable.kind = MotorControlRS::ActionKind::ENABLE;
    assert(host(app).startAction(app, 4, 1, enable, unchanged) == Probe::Action::BUSY);
    assert(host(app).startMove(app, 4, 1, move, unchanged) == Probe::Action::BUSY);
    assert(host(app).startVelocity(app, 4, 1, velocity, unchanged) == Probe::Action::BUSY);
    assert(host(app).startHome(app, 4, 1, home, unchanged) == Probe::Action::BUSY);
    assert(host(app).startDriver(app, 4, 1, ESS::DriverKind::READ, driver, unchanged) == Probe::Action::BUSY);
    monitoring.enabled = true;
    assert(host(app).monitor(app, &monitoring, monitored) == Probe::Action::BUSY);
    Probe::HostRequest serial; serial.tuple.baud = 9600; Probe::HostSnapshot state;
    assert(host(app).hostSerial(app, &serial, state) == Probe::Action::BUSY);
    assert(control(Probe::DiscoveryCommandKind::BEGIN) == Probe::Action::BUSY);
    assert(unchanged == 123 && !hardware.writes && hardware.configCalls == configurations);
}
void finiteBudgetsAndCheckedExceptions() {
    fresh(); auto candidates = range(1, 2); candidates.requestLimit = 1;
    assert(control(Probe::DiscoveryCommandKind::BEGIN, &candidates) == Probe::Action::OK);
    reply(0, words(1, {60})); waitSettled();
    assert(scan().outcome == ScanOutcome::REQUEST_LIMIT && scan().count == 1 && scan().requests == 1);
    assert(scan().restored && hardware.writes == 1);

    fresh(); candidates = range(1, 2); candidates.resultLimit = 1;
    assert(control(Probe::DiscoveryCommandKind::BEGIN, &candidates) == Probe::Action::OK);
    reply(0, words(1, {60})); waitSettled();
    assert(scan().outcome == ScanOutcome::RESULT_LIMIT && scan().count == 1 && scan().requests == 1);
    assert(scan().restored && hardware.writes == 1);

    fresh(); candidates = range(1, 2);
    assert(control(Probe::DiscoveryCommandKind::BEGIN, &candidates) == Probe::Action::OK);
    reply(0, crc({1, 0x83, 2}));
    assert(!app->owner.needsRecovery());
    reply(1, words(2, {0xABCD})); reviewedFrame(2, 1); waitSettled();
    assert(scan().outcome == ScanOutcome::COMPLETE && scan().count == 2 && scan().requests == 2);
    assert(scan().findings[0].probe.outcome == ProbeOutcome::EXCEPTION);
    assert(scan().findings[0].probe.status.code == MotorControlRS::Err::EXCEPTION && scan().findings[0].probe.status.detail == 2);
    assert(!scan().findings[0].probe.rawModelKnown && scan().findings[1].probe.rawModel == 0xABCD);
    assert(scan().findings[0].request.target.address == 1 && scan().findings[1].request.target.address == 2);
    assert(!app->modelKnown && !app->configuration.operationId && !app->identity.operationId);
}
void identityRefinementAndBudget() {
    fresh(); auto candidates = range(1, 1); candidates.identity = true;
    assert(control(Probe::DiscoveryCommandKind::BEGIN, &candidates) == Probe::Action::OK);
    reply(0, words(1, {0x4EEA}));
    reply(1, words(1, {0x4EEA, 0x29, 1, 0})); reviewedFrame(1, 4); waitSettled();
    assert(scan().outcome == ScanOutcome::COMPLETE && scan().count == 1 && scan().requests == 2);
    const auto& finding = scan().findings[0];
    assert(finding.identityAttempted && finding.identityKnown && !finding.identityAmbiguous);
    assert(finding.identity.rawModel == 0x4EEA && !finding.collisionExcluded);
    assert(finding.identityOperationId == finding.identity.operationId && finding.identityOperationId != finding.request.operationId);
    assert(finding.identityDeadlineUs <= scan().deadlineUs);
    assert(!app->identity.operationId && !app->modelKnown); // Scan does not bind selected-axis caches.

    fresh(); candidates.requestLimit = 1;
    assert(control(Probe::DiscoveryCommandKind::BEGIN, &candidates) == Probe::Action::OK);
    reply(0, words(1, {0x4EEA})); waitSettled();
    assert(scan().outcome == ScanOutcome::REQUEST_LIMIT && scan().count == 1 && scan().requests == 1);
    assert(!scan().findings[0].identityAttempted && !scan().findings[0].identityKnown && hardware.writes == 1);

    fresh(); candidates.requestLimit = 2;
    assert(control(Probe::DiscoveryCommandKind::BEGIN, &candidates) == Probe::Action::OK);
    reply(0, words(1, {0x4EEA})); reply(1, words(1, {60, 0x29, 1, 0})); waitSettled();
    assert(scan().count == 1 && scan().findings[0].probe.rawModel == 0x4EEA);
    assert(scan().findings[0].identityAmbiguous && !scan().findings[0].collisionExcluded);
}
void partialFindingsStopOnTransportFault() {
    for (unsigned scenario = 0; scenario < 6; ++scenario) {
        fresh(); const auto candidates = range(1, 3);
        assert(control(Probe::DiscoveryCommandKind::BEGIN, &candidates) == Probe::Action::OK);
        reply(0, words(1, {0x4EEA}));
        if (scenario == 0) {
            auto broken = words(2, {60}); broken.back() ^= 1; reply(1, broken);
        } else if (scenario == 1) {
            reply(1, words(3, {60})); // Checked frame belongs to the wrong endpoint.
        } else if (scenario == 2) {
            reply(1, words(2, {60, 60})); // Wrong byte count/length.
        } else if (scenario == 3) {
            waitTx(1); // A timeout may not be silently recovered to scan endpoint 3.
        } else if (scenario == 5) {
            auto duplicate = words(2, {60}); const auto second = duplicate;
            duplicate.insert(duplicate.end(), second.begin(), second.end());
            reply(1, duplicate); // Two contending replies are not unique identity evidence.
        } else {
            waitTx(1);
            const auto late = hardware.writeStarted + 8 * hardware.txCharacterUs +
                app->serial.timing.responseTimeoutUs + 1000;
            scheduleReply(late, words(2, {60}), hardware.txCharacterUs);
        }
        waitSettled();
        assert(scan().outcome == ScanOutcome::TRANSPORT_FAULT && scan().count == 2 && scan().requests == 2);
        assert(scan().findings[0].probe.outcome == ProbeOutcome::RESPONDER && scan().findings[0].probe.rawModel == 0x4EEA);
        assert(scan().findings[1].probe.outcome != ProbeOutcome::RESPONDER && app->owner.needsRecovery());
        if (scenario == 0 || scenario == 2 || scenario == 5) assert(scan().findings[1].probe.outcome == ProbeOutcome::MALFORMED);
        if (scenario == 1) assert(scan().findings[1].probe.outcome == ProbeOutcome::MISMATCH);
        if (scenario == 3 || scenario == 4) assert(scan().findings[1].probe.outcome == ProbeOutcome::NO_RESPONSE);
        const unsigned configurations = hardware.configCalls;
        pump(1000);
        assert(hardware.writes == 2 && hardware.configCalls == configurations && scan().count == 2);
        uint32_t unchanged = 123;
        assert(host(app).startProbe(app, 4, 1, unchanged) != Probe::Action::OK);
        assert(unchanged == 123 && hardware.writes == 2);
    }
}
void cancellationSettlesCurrentFrame() {
    fresh(); auto candidates = range(1, 3);
    assert(control(Probe::DiscoveryCommandKind::BEGIN, &candidates) == Probe::Action::OK);
    assert(host(app).cancel(app, scan().operationId) == Probe::Action::OK);
    waitSettled(); assert(scan().outcome == ScanOutcome::CANCELLED && scan().restored && !hardware.writes);

    fresh(); assert(control(Probe::DiscoveryCommandKind::BEGIN, &candidates) == Probe::Action::OK);
    for (unsigned i = 0; i < 10000 && app->runner.phase() != Rtu::Phase::SETUP; ++i) step();
    assert(app->runner.phase() == Rtu::Phase::SETUP && !hardware.writes);
    assert(host(app).cancel(app, 0) == Probe::Action::OK);
    waitSettled(); assert(scan().outcome == ScanOutcome::CANCELLED && !hardware.writes);

    for (bool receive : {false, true}) {
        fresh(); assert(control(Probe::DiscoveryCommandKind::BEGIN, &candidates) == Probe::Action::OK);
        waitTx(0); reviewedFrame(1, 1);
        if (receive) {
            for (unsigned i = 0; i < 10000 && app->runner.phase() != Rtu::Phase::RECEIVE; ++i) step();
            assert(app->runner.phase() == Rtu::Phase::RECEIVE);
        } else assert(app->runner.transmitEnabled());
        assert(control(Probe::DiscoveryCommandKind::CANCEL) == Probe::Action::OK);
        reply(0, words(1, {0x4EEA})); waitSettled();
        assert(scan().outcome == ScanOutcome::CANCELLED && scan().count == 1 && scan().restored);
        assert(scan().findings[0].probe.outcome == ProbeOutcome::RESPONDER && !app->owner.needsRecovery());
        assert(!app->runner.transmitEnabled() && hardware.writes == 1);
    }
}
void exactOriginalTupleRestoration() {
    fresh(); Probe::HostRequest initial; initial.tuple.baud = 38400; initial.tuple.format = HostFormat::E8_1;
    Probe::HostSnapshot state;
    assert(host(app).hostSerial(app, &initial, state) == Probe::Action::OK);
    app->axis.originKnown = true; app->axis.originNative = 99;
    const uint32_t generation = app->axis.generation, binding = app->bindingGeneration;
    auto candidates = range(1, 1); candidates.tupleCount = 2;
    candidates.tuples[0].baud = 9600; candidates.tuples[0].format = HostFormat::N8_2;
    candidates.tuples[1].baud = 115200;
    assert(control(Probe::DiscoveryCommandKind::BEGIN, &candidates) == Probe::Action::OK);
    reply(0, words(1, {60})); reply(1, words(1, {0x4EEA})); waitSettled();
    assert(scan().restored && scan().outcome == ScanOutcome::COMPLETE && scan().count == 2);
    assert(sameTuple(scan().originalTuple, initial.tuple) && sameTuple(app->serial.active, initial.tuple));
    assert(app->serial.original.baud == 115200); // Restore scan entry, not startup defaults.
    assert(scan().findings[0].request.activeSerial.baud == 9600 && scan().findings[0].request.activeSerial.stopBits == 2);
    assert(scan().findings[1].request.activeSerial.baud == 115200);
    assert(app->axis.generation == generation && app->bindingGeneration == binding);
    assert(app->axis.originKnown && app->axis.originNative == 99 && hardware.writes == 2);
}
void restorationFailureAndExplicitRepair() {
    fresh(); auto candidates = range(1, 1); candidates.tupleCount = 1; candidates.tuples[0].baud = 9600;
    assert(control(Probe::DiscoveryCommandKind::BEGIN, &candidates) == Probe::Action::OK);
    waitTx(0); assert(app->serial.active.baud == 9600);
    hardware.configFailCalls = {hardware.configCalls + 1};
    reply(0, words(1, {0x4EEA})); waitSettled();
    assert(scan().phase == ScanPhase::INTERLOCK && !scan().restored && scan().owned);
    assert(scan().outcome == ScanOutcome::RESTORE_FAILED && scan().count == 1);
    assert(app->serial.blocked && !app->serial.activeKnown && app->owner.configurationOwned());
    const auto evidence = scan().findings[0].probe.provenance.latestUs;
    const auto calls = hardware.configCalls;
    pump(1000); assert(hardware.configCalls == calls && hardware.writes == 1);
    assert(control(Probe::DiscoveryCommandKind::FINISH) == Probe::Action::BUSY);
    uint32_t unchanged = 123;
    assert(host(app).startProbe(app, 9, 1, unchanged) != Probe::Action::OK && unchanged == 123);
    hardware.configFailCalls.clear();
    assert(control(Probe::DiscoveryCommandKind::RESTORE) == Probe::Action::OK);
    waitSettled();
    assert(scan().restored && !app->serial.blocked && app->serial.activeKnown && app->serial.active.baud == 115200);
    assert(!app->owner.configurationOwned() && scan().findings[0].probe.provenance.latestUs == evidence && hardware.writes == 1);
}
void timeoutOnCandidateNeedsExplicitRecovery() {
    fresh(); auto candidates = range(1, 2); candidates.tupleCount = 1; candidates.tuples[0].baud = 9600;
    assert(control(Probe::DiscoveryCommandKind::BEGIN, &candidates) == Probe::Action::OK);
    waitTx(0); waitSettled();
    assert(scan().phase == ScanPhase::INTERLOCK && !scan().restored && scan().owned && scan().count == 1);
    assert(scan().outcome == ScanOutcome::TRANSPORT_FAULT && scan().findings[0].probe.outcome == ProbeOutcome::NO_RESPONSE);
    assert(app->serial.active.baud == 9600 && app->owner.needsRecovery());
    assert(control(Probe::DiscoveryCommandKind::RESTORE) == Probe::Action::RECOVERY_REQUIRED);
    const unsigned configurations = hardware.configCalls;
    pump(1000); assert(hardware.configCalls == configurations && hardware.writes == 1);
    uint32_t id = 0; assert(host(app).recover(app, 99, id) == Probe::Action::OK);
    for (unsigned i = 0; i < 10000 && app->owner.recovering(); ++i) step(1000);
    assert(!app->owner.needsRecovery() && !uart.needsRecovery());
    assert(control(Probe::DiscoveryCommandKind::RESTORE) == Probe::Action::OK);
    waitSettled();
    assert(scan().restored && app->serial.active.baud == 115200 && scan().count == 1 && hardware.writes == 1);
    assert(scan().findings[0].request.activeSerial.baud == 9600);
}
void overallDeadlineIncludesAdapterSetup() {
    fresh(); auto candidates = range(1, 1); candidates.tupleCount = 1; candidates.tuples[0].baud = 9600;
    candidates.overallMs = 1; hardware.configDelayUs = 5000;
    assert(control(Probe::DiscoveryCommandKind::BEGIN, &candidates) == Probe::Action::OK);
    waitSettled();
    assert(scan().outcome == ScanOutcome::DEADLINE && scan().restored && !hardware.writes && !scan().requests);
    assert(app->serial.active.baud == 115200);
}
void capturedClosureSurvivesLateCancellationDelivery() {
    fresh(); const auto candidates = range(1, 2);
    assert(control(Probe::DiscoveryCommandKind::BEGIN, &candidates) == Probe::Action::OK);
    waitTx(0);
    const auto id = app->discoveryRequest;
    scheduleReply(std::max(hardware.writeStarted + 8 * 87 + 1000, hardware.time + 1000), words(1, {0x4EEA}));
    for (unsigned i = 0; i < 25000 && !app->owner.result(id); ++i) {
        advanceHardware(hardware.time + 10); app->owner.service(uart.sample());
    }
    assert(app->owner.result(id) && app->owner.result(id)->outcome == Rtu::Outcome::SUCCESS && !scan().count);
    advanceHardware(scan().deadlineUs + 1000);
    assert(control(Probe::DiscoveryCommandKind::CANCEL) == Probe::Action::OK);
    serviceDiscovery(*app, uart.sample()); waitSettled();
    assert(scan().outcome == ScanOutcome::CANCELLED && scan().restored && scan().count == 1);
    const auto& finding = scan().findings[0];
    assert(finding.probe.outcome == ProbeOutcome::RESPONDER && finding.probe.rawModel == 0x4EEA);
    assert(finding.probe.provenance.latestUs <= finding.request.deadlineUs);
    assert(finding.probe.provenance.deliveredUs > scan().deadlineUs && hardware.writes == 1);
}
void urgentStopUsesReservedCapacityAndDoesNotReplayScan() {
    fresh();
    for (unsigned i = 0; i < 6; ++i) {
        uint32_t operation = 0;
        assert(host(app).startProbe(app, i + 1, 1, operation) == Probe::Action::OK);
        reply(i, words(1, {0x4EEA}));
        Probe::ResultView result;
        assert(host(app).result(app, operation, result) && !result.pending && result.probe.outcome == Rtu::Outcome::SUCCESS);
    }
    assert(app->knownTargets[0] & 2);
    const auto candidates = range(1, 3);
    assert(control(Probe::DiscoveryCommandKind::BEGIN, &candidates) == Probe::Action::OK);
    waitTx(6); reviewedFrame(1, 1);
    MotorControlRS::ActionRequest stop; stop.kind = MotorControlRS::ActionKind::STOP;
    uint32_t id = 123;
    assert(host(app).startAction(app, 77, 1, stop, id) == Probe::Action::INVALID);
    assert(id == 123 && !scan().cancelRequested); // Rejected stop cannot cancel the scan.
    stop.stop.behavior = MotorControlRS::StopBehavior::CONFIGURED_DECELERATION;
    assert(host(app).startAction(app, 78, 1, stop, id) == Probe::Action::OK && id != 123);
    assert(scan().cancelRequested && scan().outcome == ScanOutcome::PREEMPTED);
    reply(6, words(1, {0x4EEA}));
    waitTx(7);
    assert(hardware.tx[0] == 1 && hardware.tx[1] == 6);
    const auto echo = hardware.tx; writeResponseConfirmed = true; // Simulated response-source qualification.
    reply(7, echo);
    reply(8, words(1, {0, 0}));
    Probe::ResultView result;
    assert(host(app).result(app, id, result) && result.actionContext && !result.pending);
    assert(result.actionContext->execution == MotorControlRS::ActionExecution::ACKNOWLEDGED);
    assert(result.actionContext->completion == MotorControlRS::ActionCompletion::OBSERVED);
    assert(scan().outcome == ScanOutcome::PREEMPTED && scan().restored && scan().count == 1 && hardware.writes == 9);
    pump(1000); assert(hardware.writes == 9 && !app->owner.needsRecovery());
}
void consoleAndDirectProductionParity() {
    fresh();
    const auto command = [](const std::string& text) {
        Serial.output.clear(); Serial.input = text;
        for (unsigned i = 0; i < 1000 && !Serial.input.empty(); ++i) step();
        assert(Serial.input.empty()); pump(1000);
    };
    command("@1 profile list\n");
    assert(Serial.output.find("\"manufacturer\":\"stepperonline\"") != std::string::npos &&
        Serial.output.find("\"profile\":\"ess_rs\"") != std::string::npos);
    assert(!hardware.writes);
    command("@2 discover addresses 1 1 requests 1 results 1\n");
    assert(Serial.output.find("\"command\":\"discover\"") != std::string::npos);
    assert(scan().settings.first == 1 && scan().settings.last == 1 && scan().settings.requestLimit == 1 && scan().settings.resultLimit == 1);
    reply(0, words(1, {0x4EEA})); waitSettled();
    command("@3 discover inspect\n");
    assert(Serial.output.find("\"raw_model\":20202") != std::string::npos);
    assert(scan().findings[0].probe.rawModel == 0x4EEA && hardware.writes == 1);
    command("@4 discover finish\n"); assert(scan().released);
    command("@5 ping\n"); reply(1, words(1, {0x4EEA}));
    Probe::ResultView result;
    assert(host(app).result(app, app->latestOperationId, result) && !result.pending);
    assert(result.probe.rawModel == scan().findings[0].probe.rawModel && hardware.writes == 2);
}
void deferredStopDeadlineReleasesAxisWhileScanIsInterlocked() {
    fresh(); uint32_t probeId = 0;
    assert(host(app).startProbe(app, 1, 1, probeId) == Probe::Action::OK);
    reply(0, words(1, {0x4EEA}));
    assert(app->knownTargets[0] & 2);
    const auto candidates = range(2, 3);
    assert(control(Probe::DiscoveryCommandKind::BEGIN, &candidates) == Probe::Action::OK);
    waitTx(1); reviewedFrame(2, 1);
    MotorControlRS::ActionRequest stop; stop.kind = MotorControlRS::ActionKind::STOP;
    stop.stop.behavior = MotorControlRS::StopBehavior::CONFIGURED_DECELERATION;
    uint32_t stopId = 0;
    assert(host(app).startAction(app, 2, 1, stop, stopId) == Probe::Action::OK);
    assert(axisReserved(*app, 1));
    waitSettled(); assert(scan().phase == ScanPhase::INTERLOCK && app->owner.needsRecovery());
    Probe::ResultView result;
    for (unsigned i = 0; i < 10000; ++i) {
        assert(host(app).result(app, stopId, result));
        if (!result.pending) break;
        step(1000);
    }
    assert(!result.pending && result.actionContext);
    assert(result.actionContext->outcome == MotorControlRS::ActionOutcome::DEADLINE);
    assert(result.actionContext->execution == MotorControlRS::ActionExecution::NOT_TRANSMITTED);
    assert(result.actionContext->completion == MotorControlRS::ActionCompletion::NOT_OBSERVED);
    assert(!axisReserved(*app, 1) && scan().phase == ScanPhase::INTERLOCK && scan().owned && hardware.writes == 2);
    pump(1000); assert(hardware.writes == 2); // No deferred FC06 write or scan replay.
}
void fullOrdinaryResultsCannotConsumeScanOrStopStorage() {
    fresh();
    for (unsigned i = 0; i < REQUEST_CAPACITY; ++i) {
        uint32_t operation = 0;
        assert(host(app).startProbe(app, i + 1, 1, operation) == Probe::Action::OK);
        reply(i, words(1, {0x4EEA}));
    }
    assert(control(Probe::DiscoveryCommandKind::BEGIN) == Probe::Action::OK);
    pump(); assert(scan().owned && !scan().count && !scan().requests && hardware.writes == REQUEST_CAPACITY);
    MotorControlRS::ActionRequest stop; stop.kind = MotorControlRS::ActionKind::STOP;
    stop.stop.behavior = MotorControlRS::StopBehavior::DIRECT;
    uint32_t id = 0;
    assert(host(app).startAction(app, 99, 1, stop, id) == Probe::Action::OK);
    waitTx(REQUEST_CAPACITY); assert(hardware.tx[1] == 6);
    const auto echo = hardware.tx; writeResponseConfirmed = true;
    reply(REQUEST_CAPACITY, echo); reply(REQUEST_CAPACITY + 1, words(1, {0, 0}));
    Probe::ResultView result;
    assert(host(app).result(app, id, result) && !result.pending && result.actionContext);
    assert(result.actionContext->completion == MotorControlRS::ActionCompletion::OBSERVED);
    assert(scan().outcome == ScanOutcome::PREEMPTED && scan().restored && scan().count == 0);
    assert(hardware.writes == REQUEST_CAPACITY + 2);
    for (unsigned i = 1; i <= REQUEST_CAPACITY; ++i) {
        assert(host(app).result(app, i, result) && result.probe.outcome == Rtu::Outcome::SUCCESS);
    }
}
}
int main() {
    minimalCurrentTupleAndRetainedEvidence();
    rejectedCandidatesAndExclusiveAdmission(); finiteBudgetsAndCheckedExceptions();
    identityRefinementAndBudget();
    partialFindingsStopOnTransportFault(); cancellationSettlesCurrentFrame();
    exactOriginalTupleRestoration(); restorationFailureAndExplicitRepair();
    timeoutOnCandidateNeedsExplicitRecovery(); overallDeadlineIncludesAdapterSetup();
    capturedClosureSurvivesLateCancellationDelivery(); urgentStopUsesReservedCapacityAndDoesNotReplayScan();
    consoleAndDirectProductionParity();
    deferredStopDeadlineReleasesAxisWhileScanIsInterlocked();
    fullOrdinaryResultsCannotConsumeScanOrStopStorage();
    if (app) { app->~App(); std::free(app); app = nullptr; }
    std::puts("Discovery application tests passed");
}
