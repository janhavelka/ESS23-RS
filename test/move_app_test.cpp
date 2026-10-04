// SPDX-License-Identifier: MIT
// Exercise the actual cooperative application with simulated SDK/wire evidence.
// Qualification below belongs solely to this fake fixture, never the real bench.
#include "../examples/probe_cli/main.cpp"
#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <vector>

FakeSerial Serial;
namespace {
using namespace MotorControlRS;
void fresh() {
    if (app) { app->~App(); std::free(app); app = nullptr; }
    uart.~Esp32S3Uart(); new (&uart) Esp32S3Uart;
    resetHardware(); Serial = FakeSerial(); platformReady = false; actionTimingQualified = false;
    setup();
    assert(app && uart.ready() && app->owner.valid() && hardware.writes == 0);
    hardware.txCharacterUs = 87;
    assert(uart.startCapture(20, timing().holdUs));
}
void step(uint32_t us = 10) { advanceHardware(hardware.time + us); loop(); }
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
    actionTimingQualified = true;
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
    r.wire.bytes = bytes; r.wire.replyLength = 8; r.wire.responseTimeoutUs = RESPONSE_US;
    r.wire.replyGapUs = REPLY_GAP_US; r.wire.deadlineUs = nowUs() + REQUEST_US;
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
    assert(admitAxisWrite(*app, unqualified, nowUs(), write) == Probe::Action::TIMING_UNQUALIFIED);
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
        competing.wire.responseTimeoutUs = RESPONSE_US; competing.wire.deadlineUs = nowUs() + REQUEST_US;
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
} // namespace

int main() {
    testProductionGateAndImmutableAdmission(); testIndependentWritesRespectQualificationAndMoveStartup();
    testStageReservationAndFreshCompletion();
    testCommonAndProfileCliUseExactPreparation();
    testWrongStageEchoAndLostTriggerNeverReplay(); testCancellationAndStopAtSequenceBoundaries();
    testFullRetainedResultsStillReserveStop();
    testConfigurationChangesCancelContinuations();
    std::puts("Actual move application tests passed");
}
