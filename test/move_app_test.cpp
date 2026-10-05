// SPDX-License-Identifier: MIT
// Exercise the actual cooperative application with simulated SDK/wire evidence.
// Qualification below belongs solely to this fake fixture, never the real bench.
// Both platform variants run these same scenarios and assertions.
#include "../examples/probe_cli/ProbeApp.cpp"
#if MOTORCONTROLRS_TEST_IDF
#include "fakes/esp32_uart/FakeUsb.h"
#include "../examples/probe_idf/main/IdfPlatform.cpp"
#include "../examples/common/BoardPins.h"
#else
#include "../examples/probe_cli/ArduinoPlatform.cpp"
#include "../examples/probe_cli/main.cpp"
#endif
#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <vector>

#if !MOTORCONTROLRS_TEST_IDF
FakeSerial Serial;
#endif
namespace {
using namespace MotorControlRS;
void fresh(uint32_t maximumAgeMs = 0) {
    if (app) { app->~App(); std::free(app); app = nullptr; }
    uart.~Esp32S3Uart(); new (&uart) Esp32S3Uart;
    resetHardware(); Serial = FakeSerial(); platformReady = false; writeResponseConfirmed = false;
#if MOTORCONTROLRS_TEST_IDF
    resetUsbHardware(); Platform::consoleReady = false; Platform::pendingByte = -1;
#endif
    ApplicationOptions options; options.observationMaxAgeMs = maximumAgeMs;
    assert(beginApplication({Board::kRs485TxPin, Board::kRs485RxPin, Board::kRs485DeRePin,
        Board::kRs485DeReActiveHigh}, Board::kRs485ReceiverDisabledDuringTransmit, options));
    assert(app && uart.ready() && app->owner.valid() && hardware.writes == 0);
    hardware.txCharacterUs = 87;
    assert(uart.startCapture(20, timing().holdUs));
}
void step(uint32_t us = 10) { advanceHardware(hardware.time + us); serviceApplication(); }
void pump(unsigned count = 64) { for (unsigned i = 0; i < count; ++i) step(); }
Probe::ResultView view(uint32_t operation) {
    Probe::ResultView result;
    assert(host(app).result(app, operation, result)); return result;
}
void command(const std::string& text) {
    Serial.input = text; Serial.output.clear();
    for (unsigned i = 0; i < 500 && !Serial.input.empty(); ++i) step();
    assert(Serial.input.empty()); pump();
}
std::vector<uint8_t> crc(std::vector<uint8_t> bytes) {
    const uint16_t value = ESS::calcCrc16(bytes.data(), bytes.size());
    bytes.push_back(static_cast<uint8_t>(value)); bytes.push_back(static_cast<uint8_t>(value >> 8));
    return bytes;
}
std::vector<uint8_t> registers(uint8_t address, std::initializer_list<uint16_t> words) {
    std::vector<uint8_t> bytes = {address, 3, static_cast<uint8_t>(words.size() * 2)};
    for (const auto word : words) { bytes.push_back(static_cast<uint8_t>(word >> 8)); bytes.push_back(static_cast<uint8_t>(word)); }
    return crc(bytes);
}
void waitTx(uint32_t operation) {
    for (unsigned i = 0; i < 25000 && !app->owner.txAccepted(findRecord(*app, operation)->requestId); ++i) step();
    assert(app->owner.txAccepted(findRecord(*app, operation)->requestId));
}
std::vector<uint8_t> acknowledgement() {
    assert(hardware.tx.size() >= 8);
    if (hardware.tx[1] == 6) return hardware.tx;
    assert(hardware.tx[1] == 16);
    return crc(std::vector<uint8_t>(hardware.tx.begin(), hardware.tx.begin() + 6));
}
void moveStep(uint32_t operation, const std::vector<uint8_t>& supplied = {}) {
    const uint8_t token = view(operation).moveContext->step;
    waitTx(operation);
    const auto response = supplied.empty() ? acknowledgement() : supplied;
    scheduleReply(std::max(hardware.writeStarted + hardware.tx.size() * 87 + 1000, hardware.time + 1000), response);
    for (unsigned i = 0; i < 25000 && view(operation).pending && view(operation).moveContext->step == token; ++i) step();
    assert(!view(operation).pending || view(operation).moveContext->step != token);
}
void actionStep(uint32_t operation, const std::vector<uint8_t>& supplied = {}) {
    const uint8_t token = view(operation).actionContext->step;
    waitTx(operation);
    const auto response = supplied.empty() ? acknowledgement() : supplied;
    scheduleReply(std::max(hardware.writeStarted + 8 * 87 + 1000, hardware.time + 1000), response);
    for (unsigned i = 0; i < 25000 && view(operation).pending && view(operation).actionContext->step == token; ++i) step();
    assert(!view(operation).pending || view(operation).actionContext->step != token);
}
void finishProbe(uint32_t operation, uint8_t address) {
    waitTx(operation); scheduleReply(hardware.time + 2000, registers(address, {0x4EEA}));
    for (unsigned i = 0; i < 25000 && view(operation).pending; ++i) step();
    assert(!view(operation).pending && view(operation).probe.outcome == Rtu::Outcome::SUCCESS);
}
MoveRequest request() {
    MoveRequest r; r.position.value = Rational(20); r.position.configurationGeneration = app->axis.generation;
    r.speedRpm = 60; r.ramp = MoveRamp::VERIFIED_CONFIGURED; return r;
}
void qualify() {
    // Deliberately simulated qualifications. Production has no corresponding
    // console control and starts with every verification flag false.
    writeResponseConfirmed = true;
    app->knownTargets[0] |= 2;
    app->axis.nativeMinimum = -1000; app->axis.nativeMaximum = 1000;
    app->axis.supportedRelativeBases = 1;
    auto& p = app->movePrerequisites;
    p.target = app->axis.target; p.configurationGeneration = app->axis.generation;
    p.commandUnitsVerified = p.relativeBasisVerified = p.negativeTwosComplementVerified = true;
    p.configuredRampVerified = p.serialInputsPermit = p.readinessQualified = true;
    p.accelerationTime = p.decelerationTime = 100;
    p.wordOrderKnown = p.startSpeedKnown = true; p.startSpeed = 10;
    p.rawMotion = 1; p.observedUs = hardware.time; p.maximumAgeUs = 1000000;
    app->configuration.target = app->axis.target;
    app->configuration.operationId = 99;
    app->configuration.wordOrderKnown = true;
    app->configuration.raw.subdivision = 1000;
    app->configuration.raw.algorithm = 2; app->configuration.raw.encoderResolution = 4000;
    app->configuration.raw.inputFunctions[0] = 1;
    app->configuration.raw.inputFunctions[1] = 2; app->configuration.raw.inputFunctions[2] = 3;
    auto& motion = app->stateCache.blocks[static_cast<uint8_t>(ESS::StateBlock::MOTION)];
    motion.valid = true; motion.value.target = app->axis.target;
    motion.value.rawMotion = 1; motion.value.inPosition = motion.value.enabled = true;
    motion.observedEarliestUs = motion.observedLatestUs = hardware.time;
}
uint32_t admitMove(uint32_t commandId = 1) {
    uint32_t id = 0;
    assert(host(app).startMove(app, commandId, 1, request(), id) == Probe::Action::OK);
    assert(id && view(id).moveContext && axisReserved(*app, 1)); return id;
}
uint32_t admitStop(uint32_t commandId = 50) {
    ActionRequest stop; stop.kind = ActionKind::STOP; stop.stop.behavior = StopBehavior::CONFIGURED_DECELERATION;
    uint32_t id = 0;
    assert(host(app).startAction(app, commandId, 1, stop, id) == Probe::Action::OK); return id;
}
Rtu::BusRequest configurationWrite(uint8_t* bytes, std::size_t capacity) {
    Rtu::BusRequest r;
    r.expected.address = 1; r.expected.target = 1; r.expected.targetGeneration = app->bindingGeneration;
    r.expected.function = 6; r.expected.first = 0x10; r.expected.count = r.expected.value = 1;
    r.wire.length = ESS::buildWriteSingleRegister(1, 0x10, 1, bytes, capacity);
    r.wire.bytes = bytes; r.wire.replyLength = 8; r.wire.responseTimeoutUs = app->serial.timing.responseTimeoutUs;
    r.wire.replyGapUs = app->serial.timing.replyGapUs; r.wire.deadlineUs = nowUs() + REQUEST_US;
    r.validator = Rtu::essValidator(); return r;
}
void testProductionGateAndImmutableAdmission() {
    fresh(); uint32_t unchanged = 77;
    assert(host(app).startMove(app, 1, 1, request(), unchanged) != Probe::Action::OK);
    assert(unchanged == 77 && hardware.writes == 0 && !app->owner.pending());
    command("@1 move relative 20 steps native 60 configured\n");
    assert(hardware.writes == 0 && !app->owner.pending());
    qualify(); auto r = request(); r.speedRpm = 3001;
    assert(host(app).startMove(app, 2, 1, r, unchanged) == Probe::Action::INVALID);
    assert(unchanged == 77 && hardware.writes == 0 && !axisReserved(*app, 1));
    r = request(); ++app->axis.generation; // Caller intent must not be rebound silently.
    app->movePrerequisites.configurationGeneration = app->axis.generation;
    assert(host(app).startMove(app, 3, 1, r, unchanged) == Probe::Action::INVALID);
    assert(unchanged == 77 && hardware.writes == 0 && !axisReserved(*app, 1));
}
void testIndependentWritesRespectQualificationAndMoveStartup() {
    fresh(); uint8_t bytes[8] = {}; Rtu::RequestId write;
    const auto unqualified = configurationWrite(bytes, sizeof(bytes));
    assert(admitAxisWrite(*app, unqualified, nowUs(), write) == Probe::Action::UNAVAILABLE);
    assert(!write.owner && hardware.writes == 0 && !app->owner.pending());
    qualify(); const auto configured = configurationWrite(bytes, sizeof(bytes));
    auto& unrelated = app->stateCache.blocks[static_cast<uint8_t>(ESS::StateBlock::FEEDBACK)];
    unrelated.valid = true; unrelated.value.target = app->axis.target; unrelated.value.target.address = 2;
    unrelated.value.target.id = 2; unrelated.observedEarliestUs = unrelated.observedLatestUs = nowUs();
    const uint32_t generation = app->axis.generation;
    auto stale = configured; --stale.expected.targetGeneration;
    assert(admitAxisWrite(*app, stale, nowUs(), write) == Probe::Action::INVALID);
    stale = configured; stale.expected.target = 2;
    assert(admitAxisWrite(*app, stale, nowUs(), write) == Probe::Action::INVALID);
    assert(!write.owner && hardware.writes == 0 && !app->owner.pending());
    assert(app->axis.generation == generation && app->configuration.operationId);
    assert(admitAxisWrite(*app, configured, nowUs(), write) == Probe::Action::OK);
    assert(app->axis.generation == generation + 1 && !app->configuration.operationId);
    assert(!app->movePrerequisites.commandUnitsVerified);
    const auto& motion = app->stateCache.blocks[static_cast<uint8_t>(ESS::StateBlock::MOTION)];
    assert(motion.valid && !Probe::fresh(motion, app->axis.target, nowUs(), 1000000));
    assert(unrelated.valid && !unrelated.invalidatedUs);
    uint32_t unchanged = 77;
    assert(host(app).startMove(app, 1, 1, request(), unchanged) == Probe::Action::BUSY);
    assert(unchanged == 77 && app->owner.pending() == 1 && hardware.writes == 0);
    for (unsigned i = 0; i < 1000 && !app->owner.txAccepted(write); ++i) step();
    assert(app->owner.txAccepted(write) == 8);
    scheduleReply(hardware.writeStarted + 8 * 87 + 1000, acknowledgement());
    for (unsigned i = 0; i < 25000 && !app->owner.result(write); ++i) step();
    assert(app->owner.result(write) && app->owner.result(write)->outcome == Rtu::Outcome::SUCCESS);
    assert(hardware.writes == 1 && !axisReserved(*app, 1));
    assert(host(app).startMove(app, 2, 1, request(), unchanged) == Probe::Action::UNAVAILABLE);
    assert(unchanged == 77 && hardware.writes == 1);
}
void testStageReservationAndFreshCompletion() {
    fresh(); qualify(); const uint32_t operation = admitMove();
    moveStep(operation);
    assert(view(operation).moveContext->stagingApplied && view(operation).moveContext->step == 1);
    assert(hardware.tx.size() == 19 && hardware.tx[3] == 0x21 && hardware.tx[5] == 5);
    const unsigned writes = hardware.writes;
    const auto pending = app->owner.pending();
    for (uint16_t reg : {uint16_t(0x24), uint16_t(0x23), uint16_t(0x10)}) {
        uint8_t bytes[19] = {}; Rtu::BusRequest competing;
        competing.expected.address = 1; competing.expected.target = 1;
        competing.expected.targetGeneration = app->bindingGeneration;
        competing.expected.first = reg; competing.expected.count = reg == 0x24 ? 2 : 1;
        competing.expected.function = reg == 0x24 ? 16 : 6; competing.expected.value = 1;
        const uint16_t words[2] = {0, 999};
        competing.wire.length = reg == 0x24 ? ESS::buildWriteMultipleRegisters(1, reg, words, 2, bytes, sizeof(bytes)) :
            ESS::buildWriteSingleRegister(1, reg, 1, bytes, sizeof(bytes));
        competing.wire.bytes = bytes; competing.wire.replyLength = 8;
        competing.wire.responseTimeoutUs = app->serial.timing.responseTimeoutUs; competing.wire.deadlineUs = nowUs() + REQUEST_US;
        competing.validator = Rtu::essValidator(); Rtu::RequestId id;
        assert(admitAxisWrite(*app, competing, nowUs(), id) == Probe::Action::AXIS_CONFLICT);
        assert(!id.owner && app->owner.pending() == pending && hardware.writes == writes);
    }
    moveStep(operation); assert(hardware.tx[1] == 6 && hardware.tx[3] == 0x27 && hardware.tx[5] == 1);
    assert(view(operation).moveContext->execution == ActionExecution::ACKNOWLEDGED);
    moveStep(operation, registers(1, {0, 1})); // Old in-place condition cannot complete a fresh move.
    assert(view(operation).pending && !view(operation).moveContext->runningObserved);
    uint32_t unrelated = 0; assert(probe(app, 7, 2, unrelated) == Probe::Action::OK);
    finishProbe(unrelated, 2); assert(axisReserved(*app, 1));
    moveStep(operation, registers(1, {0, 4}));
    assert(view(operation).pending && view(operation).moveContext->runningObserved);
    moveStep(operation, registers(1, {0, 1}));
    assert(!view(operation).pending && view(operation).moveContext->completion == ActionCompletion::OBSERVED);
    assert(!axisReserved(*app, 1) && !app->owner.needsRecovery());
    assert(view(operation).moveContext->prepared.effectiveNative == 20);
}
void testCommonAndProfileCliUseExactPreparation() {
    for (bool native : {false, true}) {
        fresh(); qualify();
        app->axis.units.commandStepsPerMotorTurn = UnitScale(1000, 1, ScaleSource::ASSUMED);
        command(native ? "@1 profile ess_rs move-relative 20 steps native 60 configured\n" :
            "@1 move relative 0.02 turn motor 60 configured\n");
        if (Serial.output.find("\"result\":\"accepted\"") == std::string::npos)
            std::fprintf(stderr, "Move CLI rejected: %s\n", Serial.output.c_str());
        assert(Serial.output.find("\"result\":\"accepted\"") != std::string::npos);
        const uint32_t operation = view(0).operationId;
        assert(view(operation).moveContext->prepared.effectiveNative == 20);
        moveStep(operation); moveStep(operation); moveStep(operation, registers(1, {0, 4}));
        moveStep(operation, registers(1, {0, 1})); pump(1000);
        assert(Serial.output.find("\"type\":\"move\"") != std::string::npos);
        assert(Serial.output.find("\"effective_native\":20") != std::string::npos);
        assert(!view(operation).pending);
    }
}
void testWrongStageEchoAndLostTriggerNeverReplay() {
    for (bool lostTrigger : {false, true}) {
        fresh(); qualify(); const uint32_t operation = admitMove();
        if (lostTrigger) {
            moveStep(operation); waitTx(operation);
            for (unsigned i = 0; i < 30000 && view(operation).pending; ++i) step();
        } else {
            waitTx(operation); auto bytes = acknowledgement(); bytes[5] = 4;
            bytes.resize(6); moveStep(operation, crc(bytes));
        }
        assert(!view(operation).pending && view(operation).moveContext->uncertain);
        const auto retained = *view(operation).moveContext;
        assert(axisReserved(*app, 1) && app->owner.needsRecovery());
        const unsigned writes = hardware.writes; pump(1000); assert(hardware.writes == writes);
        uint32_t recovery = 0; assert(recover(app, 8, recovery) == Probe::Action::OK);
        for (unsigned i = 0; i < 80000 && app->owner.recovering(); ++i) step();
        assert(!app->owner.needsRecovery() && axisReserved(*app, 1));
        assert(view(operation).moveContext->execution == retained.execution);
        assert(view(operation).moveContext->outcome == retained.outcome && hardware.writes == writes);
    }
}
void testCancellationAndStopAtSequenceBoundaries() {
    for (unsigned phase = 0; phase < 8; ++phase) {
        fresh(); qualify(); const uint32_t operation = admitMove();
        if (phase == 1) {
            for (unsigned i = 0; i < 1000 && app->runner.phase() != Rtu::Phase::SETUP; ++i) step();
            assert(app->runner.phase() == Rtu::Phase::SETUP);
        }
        if ((phase >= 2 && phase <= 4) || phase >= 6) moveStep(operation);
        if ((phase >= 3 && phase <= 4) || phase == 7) moveStep(operation);
        if (phase == 4) moveStep(operation, registers(1, {0, 4}));
        if (phase >= 5) waitTx(operation); // Settle the already accepted physical frame before stop.
        const unsigned previousWrites = hardware.writes;
        const uint32_t stopping = admitStop();
        if (phase == 5 || phase == 6) moveStep(operation);
        if (phase == 7) moveStep(operation, registers(1, {0, 4}));
        for (unsigned i = 0; i < 1000 && view(operation).pending; ++i) step();
        assert(!view(operation).pending && view(operation).interruptedByStop);
        assert(view(operation).moveContext->outcome == ActionOutcome::CANCELLED);
        const auto cancelled = *view(operation).moveContext;
        actionStep(stopping); actionStep(stopping, registers(1, {0, 1}));
        assert(view(stopping).actionContext->completion == ActionCompletion::OBSERVED);
        assert(view(operation).moveContext->outcome == cancelled.outcome && !axisReserved(*app, 1));
        assert(hardware.writes == previousWrites + 2);
    }
    fresh(); qualify(); const uint32_t unsent = admitMove();
    assert(cancel(app, unsent) == Probe::Action::OK); pump();
    assert(!view(unsent).pending && hardware.writes == 0 && !axisReserved(*app, 1));
    fresh(); qualify(); const uint32_t transmitting = admitMove(); waitTx(transmitting);
    const uint64_t physicalEnd = hardware.writeStarted + hardware.tx.size() * 87;
    assert(cancel(app, transmitting) == Probe::Action::OK);
    for (unsigned i = 0; i < 1000 && view(transmitting).pending; ++i) step();
    assert(!view(transmitting).pending && view(transmitting).moveContext->uncertain);
    assert(hardware.deReleasedAt >= physicalEnd && hardware.writes == 1 && axisReserved(*app, 1));
    fresh(); qualify(); const uint32_t moving = admitMove();
    moveStep(moving); moveStep(moving); moveStep(moving, registers(1, {0, 4}));
    assert(cancel(app, moving) == Probe::Action::OK); pump();
    assert(!view(moving).pending && view(moving).moveContext->uncertain && axisReserved(*app, 1));
    const unsigned writes = hardware.writes; pump(1000); assert(hardware.writes == writes);
}
void testFullRetainedResultsStillReserveStop() {
    fresh(); qualify();
    uint32_t retained[7] = {};
    for (unsigned i = 0; i < 7; ++i) {
        assert(probe(app, 10 + i, 2, retained[i]) == Probe::Action::OK); finishProbe(retained[i], 2);
    }
    const uint32_t operation = admitMove(); moveStep(operation); moveStep(operation);
    uint32_t unchanged = 123;
    assert(probe(app, 30, 2, unchanged) == Probe::Action::RESULTS_FULL && unchanged == 123);
    const uint32_t stopping = admitStop(); pump();
    assert(!view(operation).pending && view(operation).moveContext->uncertain);
    actionStep(stopping); actionStep(stopping, registers(1, {0, 1}));
    assert(view(stopping).actionContext->completion == ActionCompletion::OBSERVED);
    for (const auto id : retained) assert(view(id).probe.outcome == Rtu::Outcome::SUCCESS);
    assert(!axisReserved(*app, 1));
}
void testConsoleResetPreservesUncertainTriggerUnderBackpressure() {
    fresh(); qualify();
    command("@1 move relative 20 steps native 60 configured\n");
    const uint32_t operation = view(0).operationId;
    moveStep(operation); waitTx(operation); // Trigger accepted, acknowledgement absent.
    assert(hardware.tx[1] == 6 && hardware.tx[3] == 0x27 && hardware.tx[5] == 1);
    const uint64_t physicalEnd = hardware.writeStarted + hardware.tx.size() * 87;
    const auto releasedBefore = hardware.deReleasedAt;
    const auto writes = hardware.writes, resets = hardware.rxResets;
    const auto binding = app->bindingGeneration, configuration = app->axis.generation;
    const auto serial = app->serial.generation;
    const auto trigger = hardware.tx;
    assert(hardware.time < physicalEnd && app->runner.transmitEnabled() && hardware.de == 1);

    Serial.output.clear(); Serial.writeCapacity = 0;
    Serial.input = "@2 cancel " + std::to_string(operation) + "\n@3 reset\n";
    step();
    assert(Serial.input.empty() && Serial.output.empty() && app->outputCount);
    assert(app->runner.stats().started == 0); // The ordinary console reset executed.
    assert(view(operation).pending && app->runner.transmitEnabled() && hardware.de == 1);
    assert(hardware.deReleasedAt == releasedBefore && hardware.writes == writes && hardware.tx == trigger);
    assert(app->bindingGeneration == binding && app->axis.generation == configuration && app->serial.generation == serial);

    for (unsigned i = 0; i < 1000 && view(operation).pending; ++i) step();
    assert(!view(operation).pending && axisReserved(*app, 1));
    const auto retained = *view(operation).moveContext;
    assert(retained.outcome == ActionOutcome::CANCELLED && retained.uncertain);
    assert(retained.execution == ActionExecution::UNKNOWN && retained.triggerEvidence.txAccepted == trigger.size());
    assert(app->owner.needsRecovery());
    assert(hardware.deReleasedAt >= physicalEnd && !app->runner.transmitEnabled() && hardware.de == 0);
    const auto releasedAfter = hardware.deReleasedAt;

    // Output remains blocked while reset runs again against the retained unknown
    // trigger. Counter reset is neither transport recovery nor a motor stop.
    command("@4 reset\n");
    assert(Serial.output.empty() && app->runner.stats().started == 0);
    assert(view(operation).moveContext->outcome == retained.outcome && view(operation).moveContext->uncertain);
    assert(view(operation).moveContext->execution == retained.execution && axisReserved(*app, 1));
    assert(app->bindingGeneration == binding && app->axis.generation == configuration && app->serial.generation == serial);
    assert(hardware.writes == writes && hardware.tx == trigger && hardware.rxResets == resets);
    assert(hardware.deReleasedAt == releasedAfter && !app->runner.transmitEnabled() && hardware.de == 0);
    assert(!app->owner.pending() && !app->owner.recovering() && !app->persistenceInvocations);
    assert(app->owner.needsRecovery());

    Serial.writeCapacity = 4096; pump(3000);
    assert(Serial.output.find("\"id\":3,\"command\":\"reset\",\"ok\":true") != std::string::npos);
    assert(Serial.output.find("\"id\":4,\"command\":\"reset\",\"ok\":true") != std::string::npos);
    assert(hardware.writes == writes && view(operation).moveContext->uncertain && axisReserved(*app, 1));
    assert(app->owner.needsRecovery());
}
void testConsoleStopUnderFullUsbBackpressure() {
    fresh(); qualify();
    command("@1 move relative 20 steps native 60 configured\n");
    const uint32_t operation = view(0).operationId;
    moveStep(operation); moveStep(operation); moveStep(operation, registers(1, {0, 4}));
    assert(view(operation).pending && view(operation).moveContext->runningObserved);
    const unsigned writes = hardware.writes;
    Serial.output.clear(); Serial.writeCapacity = 0;
    // Fill real output storage with cached queries. Stop uses its reserved
    // admission/output handoff even when ordinary responses cannot be queued.
    Serial.input.clear();
    for (unsigned id = 10; id < 19; ++id)
        Serial.input += "@" + std::to_string(id) + " status\n";
    Serial.input += "@50 stop direct\n";
    for (unsigned i = 0; i < 50 && !Serial.input.empty(); ++i) step();
    assert(Serial.input.empty() && Serial.output.empty() && app->outputCount == OUTPUT_LINES);
    const uint32_t stopping = view(0).operationId;
    assert(stopping != operation && findRecord(*app, stopping)->commandId == 50);
    // Input is fed after sequence service; the admitted stop's cancellation
    // handoff is applied on the next cooperative turn, while USB stays blocked.
    for (unsigned i = 0; i < 100 && view(operation).pending; ++i) step();
    assert(!view(operation).pending && view(operation).interruptedByStop);
    actionStep(stopping); actionStep(stopping, registers(1, {0, 1}));
    assert(!view(stopping).pending && view(stopping).actionContext->completion == ActionCompletion::OBSERVED);
    assert(hardware.writes == writes + 2 && !app->runner.transmitEnabled() && !axisReserved(*app, 1));
    const auto retainedMove = *view(operation).moveContext;
    const auto retainedStop = *view(stopping).actionContext;
    pump(100);
    assert(Serial.output.empty() && view(operation).moveContext->outcome == retainedMove.outcome);
    assert(view(stopping).actionContext->completion == retainedStop.completion);
    Serial.writeCapacity = 4096; pump(3000);
    assert(!app->outputCount && !app->console.outputPending());
    const auto count = [](const std::string& text, const char* token) {
        unsigned total = 0; std::size_t pos = 0;
        while ((pos = text.find(token, pos)) != std::string::npos) { ++total; pos += std::strlen(token); }
        return total;
    };
    assert(count(Serial.output, "\"type\":\"move\"") == 1);
    assert(count(Serial.output, "\"type\":\"action\"") == 1);
    assert(Serial.output.find("\"id\":50,\"command\":\"stop\"") != std::string::npos);
    assert(view(operation).moveContext->uncertain && view(stopping).actionContext->completion == ActionCompletion::OBSERVED);
    assert(hardware.writes == writes + 2); // Output draining never replays motor work.
}
void deliverChangedConfiguration(unsigned field) {
    // Supply a completed checked read, then exercise the actual application's
    // delivery/invalidation/cancelUnsent path. These are synthetic core events,
    // not a claim that five reads fit between two adjacent wire transactions.
    auto raw = app->configuration.raw;
    if (field == 0) raw.inputPolarity = 1;
    if (field == 1) raw.inputFunctions[0] = 0;
    if (field == 2) raw.overLimitStop = 1;
    if (field == 3) raw.softLimitEnable = 1;
    auto& record = app->records[REQUEST_CAPACITY - 1];
    assert(!record.operationId);
    record.operationId = record.commandId = 1000; record.address = 1; record.typedRead = true;
    record.serialTuple = app->serial.active; record.serialGeneration = app->serial.generation;
    record.configurationGeneration = app->axis.generation;
    assert(ESS::prepareConfig(record.read, app->axis.target, 1000, hardware.time - 1000, hardware.time + REQUEST_US));
    const std::vector<uint8_t> replies[] = {
        registers(1, {raw.direction, raw.subdivision}),
        registers(1, {raw.customNode, raw.baud, raw.format}),
        registers(1, {raw.overLimitStop, raw.softLimitEnable, raw.wordOrder}),
        registers(1, {raw.inputPolarity, raw.inputFunctions[0], raw.inputFunctions[1], raw.inputFunctions[2], raw.inputFunctions[3]}),
        registers(1, {raw.algorithm, raw.encoderResolution})
    };
    for (uint8_t i = 0; i < 5; ++i) {
        const uint64_t suppliedTime = hardware.time - 900 + i * 100;
        ReadEvent e; e.target = app->axis.target; e.operationId = 1000; e.step = i;
        e.frame = replies[i].data(); e.length = replies[i].size(); e.txAccepted = 8;
        e.qualified = true; e.earliestUs = suppliedTime - 20; e.latestUs = suppliedTime - 10;
        assert(ESS::advanceRead(record.read, e, suppliedTime));
    }
    assert(record.read.state == ReadState::SUCCEEDED); deliver(*app);
    assert(record.observed && app->configuration.operationId == 1000);
}
void testConfigurationChangesCancelContinuations() {
    for (unsigned field = 0; field < 4; ++field) {
        fresh(); qualify(); const uint32_t operation = admitMove(); moveStep(operation);
        const uint32_t generation = app->axis.generation;
        assert(view(operation).moveContext->step == 1 && hardware.writes == 1);
        deliverChangedConfiguration(field); pump();
        assert(app->axis.generation == generation + 1);
        assert(!view(operation).pending && view(operation).moveContext->outcome == ActionOutcome::CANCELLED);
        assert(view(operation).moveContext->execution == ActionExecution::NOT_TRANSMITTED);
        assert(view(operation).moveContext->uncertain && hardware.writes == 1);
    }
    fresh(); qualify(); const uint32_t moving = admitMove();
    moveStep(moving); moveStep(moving); waitTx(moving);
    deliverChangedConfiguration(0);
    assert(view(moving).pending); // Already transmitted observation must settle.
    moveStep(moving, registers(1, {0, 4})); pump();
    assert(!view(moving).pending && view(moving).moveContext->outcome == ActionOutcome::CANCELLED);
    assert(view(moving).moveContext->execution == ActionExecution::ACKNOWLEDGED);
    assert(view(moving).moveContext->uncertain && hardware.writes == 3);
}
void testReadinessBoundsActualOwnerWrites() {
    // Admission reserves a result, but does not authorize a stale queued write.
    fresh(); qualify(); app->movePrerequisites.maximumAgeUs = 20000;
    const uint32_t queued = admitMove();
    const auto* queuedMove = view(queued).moveContext;
    const uint64_t cap = queuedMove->prerequisites.observedUs + queuedMove->prerequisites.maximumAgeUs;
    const uint64_t operationDeadline = queuedMove->deadlineUs;
    advanceHardware(cap + 1000); serviceApplication(); pump();
    assert(!view(queued).pending && hardware.writes == 0 && !axisReserved(*app, 1));
    assert(view(queued).moveContext->outcome == ActionOutcome::DEADLINE);
    assert(view(queued).moveContext->status.detail == static_cast<int32_t>(MoveError::READINESS));
    assert(view(queued).moveContext->deadlineUs == operationDeadline);

    fresh(); qualify(); app->movePrerequisites.maximumAgeUs = 20000;
    const uint32_t setup = admitMove();
    for (unsigned i = 0; i < 25000 && !app->runner.transmitEnabled(); ++i) step();
    assert(app->runner.transmitEnabled() && hardware.writes == 0);
    const uint64_t setupCap = view(setup).moveContext->prerequisites.observedUs + 20000;
    advanceHardware(setupCap + 1000); serviceApplication(); pump();
    assert(!view(setup).pending && hardware.writes == 0 && !view(setup).moveContext->uncertain);
    assert(view(setup).moveContext->status.detail == static_cast<int32_t>(MoveError::READINESS));

    // Expiry during physical TX settles the accepted frame; it never cuts TX
    // short or turns its unknown device effects into a safe automatic replay.
    fresh(); qualify(); app->movePrerequisites.maximumAgeUs = 8000;
    hardware.txCharacterUs = 500;
    const uint32_t transmitting = admitMove(); waitTx(transmitting);
    const uint64_t physicalEnd = hardware.writeStarted + hardware.tx.size() * 500;
    const uint64_t txCap = view(transmitting).moveContext->prerequisites.observedUs + 8000;
    assert(physicalEnd > txCap);
    advanceHardware(txCap + 100); serviceApplication();
    assert(app->runner.transmitEnabled() && app->owner.needsRecovery());
    for (unsigned i = 0; i < 5000 && app->runner.transmitEnabled(); ++i) step();
    assert(!view(transmitting).pending && hardware.deReleasedAt >= physicalEnd);
    assert(view(transmitting).moveContext->status.detail == static_cast<int32_t>(MoveError::READINESS));
    assert(view(transmitting).moveContext->uncertain && hardware.writes == 1);
    assert(view(transmitting).moveContext->setupExecution == ActionExecution::UNKNOWN);

    // The capture timer closes an on-time staging response independently of
    // owner service. Applied staging remains uncertain; no stale trigger follows.
    fresh(); qualify(); app->movePrerequisites.maximumAgeUs = 20000;
    const uint32_t staging = admitMove(); waitTx(staging);
    const uint64_t stageCap = view(staging).moveContext->prerequisites.observedUs + 20000;
    scheduleReply(hardware.writeStarted + hardware.tx.size() * 87 + 1000, acknowledgement());
    advanceHardware(stageCap + 1000); serviceApplication(); pump();
    const auto* settled = view(staging).moveContext;
    assert(!view(staging).pending && settled->stagingApplied && settled->uncertain);
    assert(settled->stagingEvidence.latestUs < stageCap && settled->stagingEvidence.deliveredUs > stageCap);
    assert(settled->status.detail == static_cast<int32_t>(MoveError::READINESS));
    assert(settled->execution == ActionExecution::NOT_TRANSMITTED && hardware.writes == 1);
    const unsigned writes = hardware.writes; pump(1000); assert(hardware.writes == writes);

    // A trigger physically acknowledged before its cap may be delivered later;
    // completion observations use the immutable, longer operation deadline.
    fresh(); qualify(); app->movePrerequisites.maximumAgeUs = 20000;
    const uint32_t triggered = admitMove(); moveStep(triggered); waitTx(triggered);
    const uint64_t triggerCap = view(triggered).moveContext->prerequisites.observedUs + 20000;
    scheduleReply(hardware.writeStarted + hardware.tx.size() * 87 + 1000, acknowledgement());
    advanceHardware(triggerCap + 1000); serviceApplication();
    const auto* active = view(triggered).moveContext;
    assert(view(triggered).pending && active->step == 2 && hardware.writes == 2);
    assert(active->triggerEvidence.latestUs < triggerCap && active->triggerEvidence.deliveredUs > triggerCap);
    assert(active->execution == ActionExecution::ACKNOWLEDGED);
    moveStep(triggered, registers(1, {0, 4})); moveStep(triggered, registers(1, {0, 1}));
    assert(!view(triggered).pending && view(triggered).moveContext->completion == ActionCompletion::OBSERVED);
}
void testCheckedStagingExceptionRetainsPartialSetupUncertainty() {
    fresh(); qualify(); const uint32_t operation = admitMove();
    moveStep(operation, crc({1, 0x90, 3}));
    const auto* rejected = view(operation).moveContext;
    assert(!view(operation).pending && rejected->outcome == ActionOutcome::REPLY_ERROR);
    assert(rejected->stagingEvidence.status.code == Err::EXCEPTION && rejected->stagingEvidence.status.detail == 3);
    assert(rejected->setupExecution == ActionExecution::REJECTED && !rejected->stagingApplied);
    assert(rejected->uncertain && rejected->execution == ActionExecution::NOT_TRANSMITTED);
    assert(axisReserved(*app, 1) && !app->owner.needsRecovery());
    pump(1000); assert(hardware.writes == 1);
}
void testDeferredTriggerExpiresBeforeAdmission() {
    fresh(); qualify(); app->movePrerequisites.maximumAgeUs = 20000;
    const uint32_t operation = admitMove(); waitTx(operation);
    scheduleReply(hardware.writeStarted + hardware.tx.size() * 87 + 1000, acknowledgement());
    auto* record = findRecord(*app, operation);
    // Settle only the real owner so the application can deliver staging while
    // ordinary pending storage is occupied by other, read-only producers.
    for (unsigned i = 0; i < 25000 && !app->owner.result(record->requestId); ++i) {
        advanceHardware(hardware.time + 10); app->owner.service(uart.sample());
    }
    assert(app->owner.result(record->requestId));
    uint32_t producers[4] = {};
    for (unsigned i = 0; i < 4; ++i)
        assert(probe(app, 20 + i, 2, producers[i]) == Probe::Action::OK);
    advanceActions(*app, nowUs());
    assert(view(operation).pending && view(operation).moveContext->step == 1 && !record->requestId.owner);
    const uint64_t cap = view(operation).moveContext->prerequisites.observedUs + 20000;
    advanceHardware(cap + 1000); advanceActions(*app, nowUs());
    assert(!view(operation).pending && hardware.writes == 1);
    assert(view(operation).moveContext->outcome == ActionOutcome::DEADLINE);
    assert(view(operation).moveContext->status.detail == static_cast<int32_t>(MoveError::READINESS));
    assert(view(operation).moveContext->stagingApplied && view(operation).moveContext->uncertain);
    assert(view(operation).moveContext->execution == ActionExecution::NOT_TRANSMITTED);
    assert(axisReserved(*app, 1)); // The applied staging uncertainty is retained.
}
void coordinates(int64_t native = 0) {
    auto& a = app->axis; a.originKnown = true; a.originSource = ScaleSource::QUALIFIED;
    a.units.commandStepsPerMotorTurn = UnitScale(1000, 1, ScaleSource::QUALIFIED);
    a.units.fullStepsPerMotorTurn = UnitScale(200, 1, ScaleSource::DOCUMENTED);
    a.units.motorTurnsPerLoadTurn = UnitScale(1, 1, ScaleSource::ASSUMED);
    a.units.millimetresPerLoadTurn = UnitScale(4, 1, ScaleSource::ASSUMED);
    auto& ref = app->coordinateReference; ref.target = a.target; ref.configurationGeneration = a.generation;
    ref.nativeKnown = ref.stationary = ref.idle = true; ref.nativePosition = native;
    ref.source = ScaleSource::QUALIFIED; ref.observedUs = hardware.time; ref.maximumAgeUs = 1000000;
}
void testAbsoluteAngleApiCliAndLostReference() {
    for (const char* line : {"@1 move absolute 20 steps native 60 configured\n",
        "@1 profile ess_rs move-absolute 7.2 deg motor 60 configured\n",
        "@1 move absolute 4 fullsteps motor 60 configured\n",
        "@1 move absolute 0.02 turn motor 60 configured\n",
        "@1 move absolute 0.08 mm load 60 configured\n",
        "@1 move absolute 0.1256637061435917 rad motor 60 configured round nearest 1 approx 0.00001\n",
        "@1 move angle 7.2 deg motor positive reject 60 configured\n",
        "@1 profile ess_rs move-angle 7.2 deg motor shortest reject 60 configured\n"}) {
        fresh(); qualify(); coordinates();
        auto r = request(); r.position.relative = false;
        ESS::MoveContext direct;
        assert(MotorControlRS::prepareMoveAbsolute(direct, app->axis, &app->coordinateReference, 99,
            r, app->movePrerequisites, nowUs(), nowUs() + 3000000));
        command(line);
        assert(Serial.output.find("\"result\":\"accepted\"") != std::string::npos);
        const auto operation = view(0).operationId;
        assert(view(operation).moveContext->prepared.effectiveNative == direct.prepared.effectiveNative);
        moveStep(operation); moveStep(operation);
        assert(hardware.tx[1] == 6 && hardware.tx[3] == 0x27 && hardware.tx[5] == 5);
        assert(!app->coordinateReference.nativeKnown); // Trigger makes prior pose unavailable immediately.
        const auto retainedReference = view(operation).moveContext->reference;
        advanceHardware(retainedReference.observedUs + retainedReference.maximumAgeUs + 1);
        step(); assert(view(operation).pending); // Starting-reference age cannot cancel a triggered move.
        moveStep(operation, registers(1, {0, 4})); moveStep(operation, registers(1, {0, 1}));
        assert(!view(operation).pending && view(operation).moveContext->state == ActionState::SUCCEEDED);
        assert(!app->axis.originKnown && !app->coordinateReference.nativeKnown);
        assert(view(operation).moveContext->reference.nativePosition == retainedReference.nativePosition);
    }
    // Preserve a multi-turn endpoint even though the bounded bench displacement is small.
    fresh(); qualify(); coordinates(1990); app->axis.nativeMaximum = 10000;
    command("@2 move absolute 720 deg motor 60 configured\n");
    assert(view(0).moveContext->prepared.effectiveNative == 2000 && view(0).moveContext->prepared.displacementNative == 10);

    fresh(); qualify(); coordinates(); const unsigned writes = hardware.writes;
    for (const char* invalid : {"@3 move absolute 20 steps native 60 configured\n",
        "@4 move angle 90 deg motor positive reject 60 configured\n"}) {
        app->coordinateReference.nativeKnown = false; command(invalid);
        assert(hardware.writes == writes && !axisReserved(*app, 1));
    }
    coordinates(); ++app->coordinateReference.configurationGeneration;
    command("@5 move absolute 20 steps native 60 configured\n");
    assert(hardware.writes == writes && !axisReserved(*app, 1));
}
void testOriginIsHostOnlyAndConfidenceInvalidates() {
    fresh(); qualify(); coordinates(50);
    const auto observed = app->coordinateReference.observedUs;
    command("@1 axis origin 10\n");
    assert(Serial.output.find("\"ok\":true") != std::string::npos);
    assert(app->axis.originKnown && app->axis.originNative == 10 && hardware.writes == 0);
    assert(app->coordinateReference.observedUs == observed && app->coordinateReference.nativePosition == 50);
    command("@2 status\n"); command("@3 axis config\n");
    assert(app->coordinateReference.observedUs == observed && hardware.writes == 0);
    advanceHardware(observed + app->coordinateReference.maximumAgeUs + 1); step();
    assert(!app->axis.originKnown && !app->coordinateReference.nativeKnown && hardware.writes == 0);
    qualify(); coordinates();
    auto& motion = app->stateCache.blocks[static_cast<uint8_t>(ESS::StateBlock::MOTION)];
    motion.value.released = true; step();
    assert(!app->axis.originKnown && !app->coordinateReference.nativeKnown);
    qualify(); coordinates(); motion.value.running = true; motion.value.released = false; step();
    assert(!app->axis.originKnown && !app->coordinateReference.nativeKnown);
}
void testHostPreferencesPreserveReferenceAge() {
    for (const char* setting : {"position-unit deg", "velocity-unit rpm", "acceleration-unit deg/s2",
        "native-limits -900 900", "soft-limits -900 900", "relative-bases 3"}) {
        fresh(); qualify(); coordinates(50);
        const auto observed = app->coordinateReference.observedUs;
        const auto maximumAge = app->coordinateReference.maximumAgeUs;
        const auto generation = app->axis.generation;
        command(std::string("@1 axis config set ") + setting + "\n");
        assert(Serial.output.find("\"ok\":true") != std::string::npos);
        assert(app->axis.generation == generation + 1 && app->axis.originKnown);
        assert(app->coordinateReference.nativeKnown && app->coordinateReference.nativePosition == 50);
        assert(app->coordinateReference.configurationGeneration == app->axis.generation);
        assert(app->coordinateReference.observedUs == observed && app->coordinateReference.maximumAgeUs == maximumAge);
        command("@2 prepare angle 36 deg motor shortest reject\n");
        assert(Serial.output.find("\"ok\":true") != std::string::npos);
        assert(hardware.writes == 0);
        advanceHardware(observed + maximumAge + 1); step();
        assert(!app->axis.originKnown && !app->coordinateReference.nativeKnown && !app->axis.softLimitsKnown);
        assert(hardware.writes == 0);
    }
    fresh(); qualify(); coordinates();
    command("@1 axis config set command 1000\n"); // Same magnitude but explicit source changes QUALIFIED to ASSUMED.
    assert(!app->coordinateReference.nativeKnown && !app->axis.originKnown);
    coordinates(); app->axis.units.commandStepsPerMotorTurn.source = ScaleSource::ASSUMED;
    const auto observed = app->coordinateReference.observedUs;
    command("@2 axis config set command 1000\n"); // Idempotent interpretation keeps the original witness.
    assert(app->coordinateReference.nativeKnown && app->axis.originKnown && app->coordinateReference.observedUs == observed);
    advanceHardware(observed + app->coordinateReference.maximumAgeUs + 1); step();
    assert(!app->axis.originKnown && !app->coordinateReference.nativeKnown);
    qualify(); coordinates();
    command("@1 axis config set polarity -1\n");
    assert(Serial.output.find("\"ok\":true") != std::string::npos);
    assert(!app->axis.originKnown && !app->coordinateReference.nativeKnown && hardware.writes == 0);
}
void testPositionClearApiCliAndUncertainInvalidation() {
    for (bool lost : {false, true}) {
        fresh(); qualify(); coordinates(); uint32_t unchanged = 77;
        ActionRequest clear; clear.kind = ActionKind::CLEAR_POSITION; clear.positionClearQualified = true;
        assert(host(app).startAction(app, 1, 1, clear, unchanged) == Probe::Action::UNAVAILABLE);
        assert(unchanged == 77 && hardware.writes == 0); // Caller cannot bypass app qualification.
        app->positionClearQualified = true;
        clear.devicePosition = 1;
        assert(host(app).startAction(app, 2, 1, clear, unchanged) == Probe::Action::UNSUPPORTED);
        assert(unchanged == 77 && hardware.writes == 0);
        command(lost ? "@3 position-clear\n" : "@3 profile ess_rs clear-position\n");
        assert(Serial.output.find("\"result\":\"accepted\"") != std::string::npos);
        const auto operation = view(0).operationId; waitTx(operation); step();
        assert(hardware.tx[3] == 0x2D && hardware.tx[5] == 0x31);
        assert(!app->axis.originKnown && !app->coordinateReference.nativeKnown && axisReserved(*app, 1));
        if (lost) {
            for (unsigned i = 0; i < 30000 && view(operation).pending; ++i) step();
            assert(view(operation).actionContext->execution == ActionExecution::UNKNOWN);
            const auto retained = *view(operation).actionContext;
            pump(); assert(hardware.writes == 1 && !app->axis.originKnown);
            uint32_t recovery = 0; assert(recover(app, 8, recovery) == Probe::Action::OK);
            for (unsigned i = 0; i < 80000 && app->owner.recovering(); ++i) step();
            assert(view(operation).actionContext->execution == retained.execution && !app->axis.originKnown);
            assert(hardware.writes == 1 && axisReserved(*app, 1));
        } else {
            actionStep(operation); actionStep(operation, registers(1, {0, 1}));
            assert(view(operation).pending && view(operation).actionContext->rawPosition == 1);
            assert(hardware.tx[3] == 0xA && hardware.tx[5] == 2);
            actionStep(operation, registers(1, {0, 0}));
            assert(!view(operation).pending && view(operation).actionContext->completion == ActionCompletion::OBSERVED);
            assert(!app->axis.originKnown && !app->coordinateReference.nativeKnown);
            assert(!axisReserved(*app, 1)); // Zero readback establishes neither host origin nor mapping.
        }
    }
}
void testExternalFeedbackInvalidatesOnlyCurrentTarget() {
    for (bool sameTarget : {false, true}) {
        // Test historical feedback target isolation without a separate native
        // zero witness, which itself detects fresh nonzero feedback.
        fresh(); qualify(); coordinates(50);
        auto& old = app->stateCache.blocks[static_cast<uint8_t>(ESS::StateBlock::FEEDBACK)];
        old.valid = true; old.value.target = app->axis.target; old.value.pairKnown = true;
        old.value.rawPosition = 50; old.observedLatestUs = old.observedEarliestUs = nowUs();
        if (!sameTarget) { old.value.target.address = 2; old.value.target.id = 2; }
        uint32_t operation = 0;
        assert(startTypedRead(app, 1, 1, ESS::ReadKind::STATE, operation, false) == Probe::Action::OK);
        const std::vector<uint8_t> responses[] = {registers(1, {0, 1}), registers(1, {0, 0}), registers(1, {0, 51, 0})};
        for (unsigned block = 0; block < 3; ++block) {
            waitTx(operation); scheduleReply(hardware.time + 2000, responses[block]);
            for (unsigned i = 0; i < 25000 && view(operation).pending && view(operation).typedRead->step == block; ++i) step();
            assert(!view(operation).pending || view(operation).typedRead->step != block);
        }
        assert(!view(operation).pending && hardware.writes == 3);
        assert(app->axis.originKnown != sameTarget && app->coordinateReference.nativeKnown != sameTarget);
    }
}
void testExternalFeedbackDuringStopInvalidatesCoordinates() {
    for (bool runningOnly : {false, true}) {
        fresh(); qualify(); coordinates();
        auto& old = app->stateCache.blocks[static_cast<uint8_t>(ESS::StateBlock::FEEDBACK)];
        old.valid = true; old.value.target = app->axis.target; old.value.pairKnown = true;
        old.value.rawPosition = 0; old.observedLatestUs = old.observedEarliestUs = nowUs();
        const uint32_t stop = admitStop(); actionStep(stop);
        uint32_t read = 0;
        assert(startTypedRead(app, 1, 1, ESS::ReadKind::STATE, read, false) == Probe::Action::OK);
        // The stop remains pending while eligible observation work shares the bus.
        // Each actual TX gets its own checked fake response, including stop polls.
        unsigned answered = hardware.writes;
        for (unsigned i = 0; i < 50000 && view(read).pending; ++i) {
            step();
            if (hardware.writes == answered) continue;
            answered = hardware.writes;
            const auto* stopRecord = findRecord(*app, stop);
            const bool stopTx = app->owner.txAccepted(stopRecord->requestId);
            const uint16_t reg = uint16_t(hardware.tx[2]) << 8 | hardware.tx[3];
            const auto reply = stopTx ? registers(1, {0, 4}) : reg == 6 ? registers(1, {0, uint16_t(runningOnly ? 4 : 1)}) :
                reg == 8 ? registers(1, {0, 0}) : registers(1, {0, uint16_t(runningOnly ? 0 : 1), 0});
            scheduleReply(hardware.time + hardware.tx.size() * 87 + 1000, reply);
        }
        assert(!view(read).pending && view(stop).pending && axisReserved(*app, 1));
        assert(!app->axis.originKnown && !app->coordinateReference.nativeKnown);
    }
}
void testPositionClearBadObservationKeepsConflict() {
    fresh(); qualify(); coordinates(); app->positionClearQualified = true;
    command("@1 position-clear\n");
    const uint32_t operation = view(0).operationId;
    actionStep(operation);
    auto corrupt = registers(1, {0, 0}); corrupt.back() ^= 1;
    actionStep(operation, corrupt);
    assert(!view(operation).pending && hardware.writes == 2);
    const auto retained = *view(operation).actionContext;
    assert(retained.execution == ActionExecution::ACKNOWLEDGED && retained.completion == ActionCompletion::NOT_OBSERVED);
    assert(retained.status.code == Err::CRC_ERROR && axisReserved(*app, 1));
    assert(!app->axis.originKnown && !app->coordinateReference.nativeKnown);
    pump(); assert(release(app, operation) == Probe::Action::OK);
    assert(axisReserved(*app, 1));
    uint32_t recovery = 0; assert(recover(app, 8, recovery) == Probe::Action::OK);
    for (unsigned i = 0; i < 80000 && app->owner.recovering(); ++i) step();
    assert(!app->owner.needsRecovery() && axisReserved(*app, 1) && hardware.writes == 2);
    assert(!app->axis.originKnown && !app->coordinateReference.nativeKnown);
}
} // namespace

// Establish admission through actual typed reads and the normal profile snapshot.
// No qualify() flags or fabricated cache entries are used on the accepting path.
static void readProductionMoveBaseline() {
    const auto read = [](ESS::ReadKind kind, const std::vector<std::vector<uint8_t>>& responses) {
        uint32_t operation = 0;
        assert(startTypedRead(app, 1, 1, kind, operation, false) == Probe::Action::OK);
        for (const auto& response : responses) {
            const auto token = view(operation).typedRead->step;
            waitTx(operation);
            scheduleReply(hardware.time + 2000, response);
            for (unsigned i = 0; i < 25000 && view(operation).pending &&
                 view(operation).typedRead->step == token; ++i) step();
            assert(!view(operation).pending || view(operation).typedRead->step != token);
        }
        assert(!view(operation).pending && view(operation).typedRead->state == ReadState::SUCCEEDED);
        pump(); // Retain the completed read; profile/move admission must coexist with it.
    };
    read(ESS::ReadKind::CONFIG, {registers(1, {0, 1000}), registers(1, {0, 0, 0}),
        registers(1, {0, 0, 0}), registers(1, {0, 1, 2, 3, 0}), registers(1, {3, 4000})});
    read(ESS::ReadKind::STATE, {registers(1, {0, 1}), registers(1, {0, 0}), registers(1, {0, 0, 0})});
    Probe::MotionProfileView profile;
    assert(motionProfileCommand(app, Probe::MotionProfileCommand::SNAPSHOT, profile) == Probe::Action::OK);
    const auto ownerRequest = app->motionProfile.request;
    for (unsigned i = 0; i < 25000 && !app->owner.txAccepted(ownerRequest); ++i) step();
    assert(app->owner.txAccepted(ownerRequest));
    scheduleReply(hardware.time + 2000, registers(1, {30, 100, 100, 60, 0, 250}));
    for (unsigned i = 0; i < 25000 && app->motionProfile.view.pending; ++i) step();
    assert(!app->motionProfile.view.pending && app->motionProfile.view.saved && app->motionProfile.view.ok);
    assert(!app->movePrerequisites.commandUnitsVerified);
}
static void testProductionMoveOptionalObservationAge() {
    for (unsigned scenario = 0; scenario < 4; ++scenario) {
        fresh(scenario == 1 ? 5000 : 0);
        readProductionMoveBaseline();
        const auto observed = app->stateCache.blocks[0].observedEarliestUs;
        advanceHardware(hardware.time + 31000000);
        if (scenario == 2) app->stateCache.blocks[1].valid = false; // Missing required input evidence.
        if (scenario == 3) invalidateAxis(*app); // Configuration invalidation still revokes the snapshot.
        const auto writes = hardware.writes;
        uint32_t operation = 99;
        const auto admitted = host(app).startMove(app, 2, 1, request(), operation);
        if (scenario) {
            assert(admitted != Probe::Action::OK && operation == 99 && hardware.writes == writes);
            assert(!app->owner.active() && !app->owner.pending());
            continue;
        }
        assert(admitted == Probe::Action::OK);
        assert(view(operation).moveContext->prerequisites.maximumAgeUs == 0);
        assert(view(operation).moveContext->prerequisites.observedUs == observed);
        moveStep(operation); moveStep(operation);
        moveStep(operation, registers(1, {0, 4})); moveStep(operation, registers(1, {0, 1}));
        assert(!view(operation).pending && view(operation).moveContext->completion == ActionCompletion::OBSERVED);
        assert(hardware.writes == writes + 4 && !app->owner.needsRecovery());
    }
}
int main() {
    testProductionMoveOptionalObservationAge();
    testProductionGateAndImmutableAdmission(); testIndependentWritesRespectQualificationAndMoveStartup();
    testStageReservationAndFreshCompletion();
    testCommonAndProfileCliUseExactPreparation();
    testWrongStageEchoAndLostTriggerNeverReplay(); testCancellationAndStopAtSequenceBoundaries();
    testFullRetainedResultsStillReserveStop();
    testConsoleResetPreservesUncertainTriggerUnderBackpressure();
    testConsoleStopUnderFullUsbBackpressure();
    testConfigurationChangesCancelContinuations();
    testReadinessBoundsActualOwnerWrites(); testDeferredTriggerExpiresBeforeAdmission();
    testCheckedStagingExceptionRetainsPartialSetupUncertainty();
    testAbsoluteAngleApiCliAndLostReference(); testOriginIsHostOnlyAndConfidenceInvalidates();
    testHostPreferencesPreserveReferenceAge();
    testPositionClearApiCliAndUncertainInvalidation();
    testExternalFeedbackInvalidatesOnlyCurrentTarget();
    testExternalFeedbackDuringStopInvalidatesCoordinates(); testPositionClearBadObservationKeepsConflict();
    std::puts("Actual move application tests passed");
}
