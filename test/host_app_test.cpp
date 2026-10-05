// SPDX-License-Identifier: MIT
// Exercise the actual application owner, serial callback and console with SDK fakes.
#include "../examples/probe_cli/ProbeApp.cpp"
#include "../examples/probe_cli/ArduinoPlatform.cpp"
#include "../examples/probe_cli/main.cpp"
#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <vector>

FakeSerial Serial;
namespace {
void fresh() {
    if (app) { app->~App(); std::free(app); app = nullptr; }
    uart.~Esp32S3Uart(); new (&uart) Esp32S3Uart;
    resetHardware(); Serial = FakeSerial(); platformReady = writeResponseConfirmed = false;
    setup(); assert(app && uart.ready() && app->owner.valid() && !hardware.writes);
    hardware.txCharacterUs = 87;
    assert(uart.startCapture(20, timing().holdUs));
}
void step(uint32_t us = 10) { advanceHardware(hardware.time + us); loop(); }
void pump(unsigned count = 200) { for (unsigned i = 0; i < count; ++i) step(); }
void command(const char* text) {
    Serial.input = text; Serial.output.clear();
    for (unsigned i = 0; i < 1000 && !Serial.input.empty(); ++i) step();
    assert(Serial.input.empty()); pump();
}
void contains(const char* text) {
    if (Serial.output.find(text) == std::string::npos)
        std::fprintf(stderr, "Missing [%s] in [%s]\n", text, Serial.output.c_str());
    assert(Serial.output.find(text) != std::string::npos);
}
Probe::HostRequest request(uint32_t baud, HostFormat format = HostFormat::N8_1) {
    Probe::HostRequest value; value.tuple.baud = baud; value.tuple.format = format; return value;
}
Probe::Action change(const Probe::HostRequest& value, Probe::HostSnapshot& state) {
    assert(host(app).hostSerial);
    return host(app).hostSerial(app, &value, state);
}
Probe::HostSnapshot query() {
    Probe::HostSnapshot state; assert(host(app).hostSerial(app, nullptr, state) == Probe::Action::OK); return state;
}
Probe::ResultView view(uint32_t operation) {
    Probe::ResultView value; assert(host(app).result(app, operation, value)); return value;
}
void finishProbe(uint32_t operation, bool harvest = true) {
    const unsigned before = hardware.writes;
    for (unsigned i = 0; i < 10000 && hardware.writes == before; ++i) {
        advanceHardware(hardware.time + 10);
        if (harvest) loop(); else app->owner.service(uart.sample());
    }
    assert(hardware.writes == before + 1);
    scheduleReply(std::max(hardware.writeStarted + 8 * 87 + 1000, hardware.time + 1000),
        {1, 3, 2, 0, 0x3C, 0xB8, 0x55});
    for (unsigned i = 0; i < 25000 && view(operation).pending; ++i) {
        advanceHardware(hardware.time + 10);
        if (harvest) loop(); else app->owner.service(uart.sample());
    }
    assert(!view(operation).pending && view(operation).probe.outcome == Rtu::Outcome::SUCCESS);
    if (harvest) pump();
}
uint32_t probeNow() {
    uint32_t operation = 0;
    assert(host(app).startProbe(app, 1, 1, operation) == Probe::Action::OK); return operation;
}

void testRejectedAndNoopTuples() {
    fresh(); const auto before = query(); const auto config = hardware.config;
    const unsigned calls = hardware.configCalls, resets = hardware.rxResets;
    Probe::HostSnapshot state;
    assert(change(request(57600), state) == Probe::Action::UNSUPPORTED);
    assert(change(request(115200, static_cast<HostFormat>(99)), state) == Probe::Action::UNSUPPORTED);
    assert(hardware.configCalls == calls && hardware.rxResets == resets && !hardware.writes);
    assert(hardware.config.baud_rate == config.baud_rate && hardware.config.parity == config.parity);
    assert(query().generation == before.generation && !app->owner.configurationOwned());
    assert(change(request(115200), state) == Probe::Action::OK);
    assert(hardware.configCalls == calls && state.generation == before.generation);
}

void testQueuedActiveAndWaitOwnership() {
    fresh(); const uint32_t operation = probeNow();
    const auto original = query(); const unsigned calls = hardware.configCalls;
    Probe::HostSnapshot state;
    assert(app->owner.pending());
    assert(change(request(9600), state) == Probe::Action::BUSY);
    assert(hardware.configCalls == calls && app->owner.pending() == 1 && view(operation).pending);
    for (unsigned i = 0; i < 10000 && !app->runner.transmitEnabled(); ++i) step();
    assert(app->owner.active() && app->runner.transmitEnabled());
    assert(change(request(9600), state) == Probe::Action::BUSY);
    assert(query().generation == original.generation && hardware.configCalls == calls);

    fresh();
    // A real public read context can be active between transactions: transport
    // idleness alone is insufficient permission to change its intended tuple.
    auto& record = app->records[0]; record.operationId = 1; record.typedRead = true;
    MotorControlRS::ActiveSerialTuple serial; serial.known = true; serial.baud = 115200;
    serial.dataBits = 8; serial.stopBits = 1; serial.parity = MotorControlRS::SerialParity::NONE;
    assert(ESS::prepareIdentity(record.read, app->axis.target, 1, hardware.time,
        hardware.time + REQUEST_US, serial));
    assert(!app->owner.pending() && !app->owner.active() && reading(*app));
    assert(change(request(9600), state) == Probe::Action::BUSY && hardware.configCalls == 1);

    fresh(); app->monitorState.settings.enabled = true; app->monitorState.remaining = 2;
    assert(change(request(9600), state) == Probe::Action::BUSY && hardware.configCalls == 1);
}

void testPreserveLogicalConfigurationAndInvalidateConfidence() {
    fresh(); const uint32_t operation = probeNow(); finishProbe(operation);
    assert(app->known && app->modelKnown && app->communicationKnown);
    const auto oldResult = view(operation).probe.transport;
    const uint32_t axisGeneration = app->axis.generation, binding = app->bindingGeneration;
    app->axis.originKnown = true; app->axis.originNative = 123;
    app->axis.originSource = MotorControlRS::ScaleSource::ASSUMED;
    app->axis.supportedRelativeBases = 1;
    MotorControlRS::PositionRequest intent; intent.value = MotorControlRS::Rational(10);
    intent.configurationGeneration = axisGeneration;
    MotorControlRS::PreparedTarget prepared;
    assert(MotorControlRS::preparePosition(intent, app->axis, nullptr, prepared));
    auto& block = app->stateCache.blocks[static_cast<uint8_t>(ESS::StateBlock::MOTION)];
    block.valid = true; block.value.target = app->axis.target;
    block.observedEarliestUs = block.observedLatestUs = hardware.time;
    app->configuration.operationId = 23; app->configuration.raw.baud = 4;
    app->driverSettings.operationId = 24; app->driverInputsQualified = true;
    app->actionConflicts[0] = 2;
    const auto before = query(); Probe::HostSnapshot state;
    assert(change(request(9600, HostFormat::E8_1), state) == Probe::Action::OK);
    assert(state.activeKnown && !state.blocked && state.generation > before.generation);
    assert(state.active.baud == 9600 && state.active.format == HostFormat::E8_1);
    assert(state.timing.replyGapUs == 4011 && state.timing.runner.gap15Us == 1719);
    assert(hardware.config.baud_rate == 9600 && hardware.config.parity == UART_PARITY_EVEN);
    assert(app->axis.generation == axisGeneration && app->bindingGeneration == binding);
    assert(app->axis.originKnown && app->axis.originNative == 123 && app->actionConflicts[0] == 2);
    assert(prepared.configurationGeneration == app->axis.generation && prepared.target.generation == binding);
    assert(!app->known && !app->modelKnown && !app->communicationKnown && !app->knownTargets[0]);
    assert(!Probe::fresh(block, app->axis.target, hardware.time, 5000000));
    assert(!app->configuration.operationId && !app->driverSettings.operationId && !app->driverInputsQualified);
    assert(view(operation).probe.transport.endedUs == oldResult.endedUs);
    assert(view(operation).probe.rawModel == 60);
    const auto* retained = findRecord(*app, operation);
    assert(retained && retained->serialTuple.baud == 115200 && retained->serialGeneration == before.generation);
    command("@2 result 1\n"); contains("\"baud\":115200"); contains("\"format\":\"8N1\"");
    Probe::HostRequest restore; restore.restore = true;
    assert(change(restore, state) == Probe::Action::OK && state.active.baud == 115200);
    assert(state.active.format == HostFormat::N8_1 && state.timing.replyGapUs == 304);
    assert(app->axis.generation == axisGeneration && app->bindingGeneration == binding && app->actionConflicts[0] == 2);
}

void testStaleUnharvestedResultCannotRestoreConfidence() {
    fresh();
    for (const char value : std::string("@1 probe\n")) app->console.feed(value);
    const uint32_t operation = app->latestOperationId; finishProbe(operation, false);
    assert(!findRecord(*app, operation)->observed && !app->modelKnown);
    Probe::HostSnapshot state; assert(change(request(19200), state) == Probe::Action::OK);
    pump();
    assert(!app->known && !app->modelKnown && !app->communicationKnown && !app->knownTargets[0]);
    assert(view(operation).probe.rawModel == 60 && findRecord(*app, operation)->delivered);
}

void testStaleIdentityCannotRepopulateSelectedCache() {
    fresh();
    for (const char value : std::string("@1 read identity\n")) app->console.feed(value);
    const uint32_t operation = app->latestOperationId;
    auto* record = findRecord(*app, operation); assert(record && record->typedRead);
    for (unsigned i = 0; i < 10000 && !hardware.writes; ++i) {
        advanceHardware(hardware.time + 10); app->owner.service(uart.sample());
    }
    assert(hardware.writes == 1);
    std::vector<uint8_t> bytes = {1, 3, 8, 0x4E, 0xEA, 0, 0x29, 0, 1, 0, 0};
    const uint16_t crc = ESS::calcCrc16(bytes.data(), bytes.size());
    bytes.push_back(static_cast<uint8_t>(crc)); bytes.push_back(static_cast<uint8_t>(crc >> 8));
    scheduleReply(std::max(hardware.writeStarted + 8 * 87 + 1000, hardware.time + 1000), bytes);
    for (unsigned i = 0; i < 25000 && !app->owner.result(record->requestId); ++i) {
        advanceHardware(hardware.time + 10); app->owner.service(uart.sample());
    }
    assert(app->owner.result(record->requestId));
    advanceReads(*app, uart.sample());
    assert(!view(operation).pending && !record->observed && !app->identity.operationId);
    Probe::HostSnapshot state; assert(change(request(9600), state) == Probe::Action::OK);
    pump();
    assert(!app->identity.operationId && !app->configuration.operationId && !app->communicationKnown);
    assert(record->delivered && record->read.activeSerial.baud == 115200);
    ESS::IdentityObservation identity;
    assert(ESS::getIdentity(record->read, identity) && identity.operationId == operation);
    assert(identity.rawModel == 0x4EEA);
}

void testFailedChangeRestorationAndExplicitRepair() {
    fresh(); const uint32_t axisGeneration = app->axis.generation, binding = app->bindingGeneration;
    const auto original = query(); const unsigned calls = hardware.configCalls;
    hardware.configFailCalls = {calls + 1, calls + 2};
    Probe::HostSnapshot state;
    assert(change(request(9600, HostFormat::O8_1), state) == Probe::Action::FAILED);
    assert(state.blocked && !state.activeKnown && state.failure == Probe::HostFailure::ADAPTER);
    assert(state.requested.baud == 9600 && state.requested.format == HostFormat::O8_1);
    assert(sameTuple(state.original, original.original) && app->owner.configurationOwned());
    uint32_t refused = 0x1234;
    assert(host(app).startProbe(app, 8, 1, refused) == Probe::Action::RECOVERY_REQUIRED && refused == 0x1234);
    assert(host(app).startTypedRead(app, 9, 1, ESS::ReadKind::IDENTITY, refused) == Probe::Action::RECOVERY_REQUIRED);
    assert(host(app).recover(app, 10, refused) == Probe::Action::RECOVERY_REQUIRED);
    MotorControlRS::ActionRequest stop; stop.kind = MotorControlRS::ActionKind::STOP;
    MotorControlRS::MoveRequest move; MotorControlRS::VelocityRequest velocity;
    ESS::DriverRequest driver; ESS::HomeRequest home;
    assert(host(app).startAction(app, 12, 1, stop, refused) == Probe::Action::RECOVERY_REQUIRED);
    assert(host(app).startMove(app, 13, 1, move, refused) == Probe::Action::RECOVERY_REQUIRED);
    assert(host(app).startVelocity(app, 14, 1, velocity, refused) == Probe::Action::RECOVERY_REQUIRED);
    assert(host(app).startDriver(app, 15, 1, ESS::DriverKind::READ, driver, refused) == Probe::Action::RECOVERY_REQUIRED);
    assert(host(app).startDriver(app, 16, 1, ESS::DriverKind::UPDATE, driver, refused) == Probe::Action::RECOVERY_REQUIRED);
    assert(host(app).startHome(app, 17, 1, home, refused) == Probe::Action::RECOVERY_REQUIRED);
    Probe::MonitorSettings monitorSettings; monitorSettings.enabled = true;
    monitorSettings.intervalMs = 100; monitorSettings.count = 1; Probe::MonitorSnapshot monitoring;
    assert(host(app).monitor(app, &monitorSettings, monitoring) == Probe::Action::RECOVERY_REQUIRED);
    assert(refused == 0x1234);
    assert(!hardware.writes && query().blocked);
    uint8_t bytes[8]; Rtu::BusRequest work;
    work.wire.bytes = bytes; work.wire.length = ESS::buildProbe(1, bytes, sizeof(bytes));
    work.wire.replyLength = 7; work.wire.responseTimeoutUs = query().timing.responseTimeoutUs;
    work.wire.deadlineUs = hardware.time + REQUEST_US;
    work.expected.address = 1; work.expected.function = 3; work.expected.count = 1;
    work.validator = Rtu::essValidator(); Rtu::RequestId id;
    assert(app->owner.admit(work, hardware.time, id) == Rtu::BusAdmission::CONFIGURING);
    assert(app->owner.admitUrgent(work, hardware.time, id) == Rtu::BusAdmission::CONFIGURING);
    Rtu::SequenceId sequence;
    assert(!app->owner.beginSequence(0, hardware.time + REQUEST_US, hardware.time, sequence));
    pump(); assert(!hardware.writes && query().blocked);
    Probe::HostRequest restore; restore.restore = true;
    assert(change(restore, state) == Probe::Action::FAILED);
    assert(state.blocked && !state.activeKnown && state.failure == Probe::HostFailure::RESTORE);
    assert(sameTuple(state.original, original.original) && state.requested.baud == 115200);
    assert(change(restore, state) == Probe::Action::OK);
    assert(!state.blocked && state.activeKnown && state.failure == Probe::HostFailure::NONE);
    assert(!app->owner.configurationOwned() && sameTuple(state.active, original.original));
    assert(app->axis.generation == axisGeneration && app->bindingGeneration == binding);
    assert(host(app).startProbe(app, 11, 1, refused) == Probe::Action::OK);
}

void testConsoleHostRoutes() {
    fresh(); command("@1 host\n"); contains("\"command\":\"host\""); contains("8N1");
    command("@2 host baud 9600\n"); contains("\"ok\":true"); assert(query().active.baud == 9600);
    command("@3 host fmt 8N2\n"); contains("\"ok\":true"); assert(query().active.format == HostFormat::N8_2);
    command("@4 host restore\n"); contains("\"ok\":true"); assert(query().active.baud == 115200);
    const unsigned calls = hardware.configCalls;
    command("@5 host baud 9600junk\n"); contains("\"ok\":false");
    command("@6 host fmt 7E1\n"); contains("\"ok\":false");
    command("@7 host baud 9600 extra\n"); contains("\"ok\":false");
    assert(hardware.configCalls == calls && !hardware.writes);
}

void testSetupDelayPreservesClockEpoch() {
    fresh(); hardware.configDelayUs = 5000;
    const uint64_t before = hardware.time; Probe::HostSnapshot state;
    assert(change(request(38400, HostFormat::E8_1), state) == Probe::Action::OK);
    assert(hardware.time >= before + 5000 && !app->runner.needsRecovery());
    assert(state.timing.replyGapUs == 1750 && state.timing.runner.gap15Us == 750);
    hardware.txCharacterUs = 287;
    const uint32_t operation = probeNow();
    for (unsigned i = 0; i < 10000 && !hardware.writes; ++i) step();
    assert(hardware.writes == 1);
    scheduleReply(std::max(hardware.writeStarted + 8 * 287 + 2000, hardware.time + 2000),
        {1, 3, 2, 0, 0x3C, 0xB8, 0x55}, 287);
    for (unsigned i = 0; i < 25000 && view(operation).pending; ++i) step();
    assert(view(operation).probe.outcome == Rtu::Outcome::SUCCESS);
    assert(findRecord(*app, operation)->serialTuple.baud == 38400);
    assert(app->runner.result().reason != Rtu::Reason::CLOCK_ERROR);
}

void testCompleteRepliesAtEverySupportedTuple() {
    for (const uint32_t baud : {9600U, 19200U, 38400U, 115200U}) {
        for (const auto format : {HostFormat::N8_1, HostFormat::N8_2, HostFormat::E8_1, HostFormat::O8_1}) {
          for (const bool capture : {false, true}) {
            fresh(); hardware.configDelayUs = 5000;
            Probe::HostSnapshot state;
            assert(change(request(baud, format), state) == Probe::Action::OK);
            const uint32_t character = (uint32_t(bitsPerCharacter(format)) * 1000000U + baud - 1) / baud;
            hardware.txCharacterUs = character;
            uint32_t operation = 0;
            assert((capture ? host(app).startCaptureRead(app, 1, 1, operation) :
                host(app).startProbe(app, 1, 1, operation)) == Probe::Action::OK);
            for (unsigned i = 0; i < 10000 && !hardware.writes; ++i) step();
            assert(hardware.writes == 1);
            std::vector<uint8_t> bytes = {1, 3, static_cast<uint8_t>(capture ? 32 : 2)};
            bytes.resize(capture ? 35 : 5, 0);
            if (!capture) bytes[4] = 60;
            const uint16_t crc = ESS::calcCrc16(bytes.data(), bytes.size());
            bytes.push_back(static_cast<uint8_t>(crc)); bytes.push_back(static_cast<uint8_t>(crc >> 8));
            scheduleReply(std::max(hardware.writeStarted + 8 * character + state.timing.replyGapUs + 500,
                hardware.time + state.timing.replyGapUs + 500), bytes, character);
            for (unsigned i = 0; i < 25000 && view(operation).pending; ++i) step();
            const auto result = view(operation);
            assert(!result.pending && result.probe.outcome == Rtu::Outcome::SUCCESS);
            assert((capture || result.probe.rawModel == 60) && result.probe.transport.txAccepted == 8);
            assert(result.probe.transport.rxLength == bytes.size() && !app->owner.needsRecovery());
            assert(result.serialTuple.baud == baud && result.serialTuple.format == format);
            assert(result.serialGeneration == state.generation && hardware.writes == 1);
            assert(app->runner.result().reason != Rtu::Reason::CLOCK_ERROR);
          }
        }
    }
}

void testRefusedAdapterWithSettledStrayTrafficCanRepair() {
    for (const bool captured : {false, true}) {
        fresh(); hardware.rx.push_back(0x5A);
        if (captured) uart.sample();
        Probe::HostSnapshot state;
        assert(change(request(9600), state) == Probe::Action::FAILED);
        assert(state.blocked && !state.activeKnown && app->owner.configurationOwned());
        // A setup refusal before the adapter's own interlock must still be
        // repairable through the application's held configuration ownership.
        Probe::HostRequest restore; restore.restore = true;
        assert(change(restore, state) == Probe::Action::OK);
        assert(state.activeKnown && !state.blocked && hardware.rx.empty() && !hardware.writes);
        assert(!app->owner.configurationOwned());
    }
}

void testOwnerRejectsInvalidTimingAndRetainsResults() {
    fresh(); const uint32_t operation = probeNow(); finishProbe(operation);
    const auto* record = findRecord(*app, operation); const auto id = record->requestId;
    const auto* result = app->owner.result(id); assert(result);
    const auto originalEnded = result->transport.endedUs;
    assert(app->owner.beginConfiguration(hardware.time));
    auto invalid = query().timing.runner; invalid.busTimeoutUs = invalid.gap35Us - 1;
    assert(!app->owner.finishConfiguration(invalid, hardware.time));
    assert(app->owner.configurationOwned() && app->owner.result(id)->transport.endedUs == originalEnded);
    invalid = query().timing.runner; invalid.txTimeoutUs = invalid.setupUs;
    assert(!app->owner.finishConfiguration(invalid, hardware.time));
    invalid = query().timing.runner; invalid.txTimeoutUs = invalid.holdUs;
    assert(!app->owner.finishConfiguration(invalid, hardware.time));
    invalid = query().timing.runner; invalid.txTimeoutUs = invalid.setupUs + invalid.holdUs;
    assert(!app->owner.finishConfiguration(invalid, hardware.time));
    Rtu::SequenceId sequence;
    assert(!app->owner.beginSequence(0, hardware.time + REQUEST_US, hardware.time, sequence));
    assert(app->owner.finishConfiguration(query().timing.runner, hardware.time));
    assert(!app->owner.configurationOwned() && app->owner.result(id)->transport.endedUs == originalEnded);
}
}
int main() {
    testRejectedAndNoopTuples(); testQueuedActiveAndWaitOwnership();
    fresh(); platformReady = false;
    Probe::HostSnapshot unavailable; unavailable.generation = 97;
    assert(hostSerial(app, nullptr, unavailable) == Probe::Action::UNAVAILABLE);
    assert(unavailable.generation == 97 && !hardware.writes);
    platformReady = true;
    testPreserveLogicalConfigurationAndInvalidateConfidence();
    testStaleUnharvestedResultCannotRestoreConfidence();
    testStaleIdentityCannotRepopulateSelectedCache();
    testFailedChangeRestorationAndExplicitRepair(); testConsoleHostRoutes();
    testSetupDelayPreservesClockEpoch();
    testCompleteRepliesAtEverySupportedTuple();
    testRefusedAdapterWithSettledStrayTrafficCanRepair();
    testOwnerRejectsInvalidTimingAndRetainsResults();
    if (app) { app->~App(); std::free(app); app = nullptr; }
    std::puts("Host application tests passed");
}
