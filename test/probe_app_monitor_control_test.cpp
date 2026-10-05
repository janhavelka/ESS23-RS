// SPDX-License-Identifier: MIT
// Local polling controls exercise the production application and console paths.
#include "../examples/probe_cli/main.cpp"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>

FakeSerial Serial;
namespace {
void fresh() {
    if (app) { app->~App(); std::free(app); app = nullptr; }
    uart.~Esp32S3Uart(); new (&uart) Esp32S3Uart;
    resetHardware(); Serial = FakeSerial(); platformReady = writeResponseConfirmed = false;
    setup(); assert(app && uart.ready() && app->owner.valid() && !hardware.writes);
    hardware.txCharacterUs = 87;
    assert(uart.startCapture(20, timing().holdUs));
    Serial.output.clear();
}
void feed(const char* command) {
    for (const char* at = command; *at; ++at) app->console.feed(*at);
}
void drain() {
    for (unsigned i = 0; i < 1000 && app->outputCount; ++i) drainOutput(*app);
    assert(!app->outputCount);
}
void enable() {
    Probe::MonitorSettings requested; requested.enabled = true;
    requested.intervalMs = 100; requested.count = 3;
    Probe::MonitorSnapshot state;
    assert(host(app).monitor(app, &requested, state) == Probe::Action::OK);
    assert(state.settings.enabled && state.remaining == 3);
}
void assertStopped() {
    Probe::MonitorSnapshot state;
    assert(host(app).monitor(app, nullptr, state) == Probe::Action::OK);
    assert(!state.settings.enabled && state.remaining == 0);
}

void testCachedQueryAndOffUnderHostFailures() {
    for (unsigned failure = 0; failure < 4; ++failure) {
        fresh(); enable();
        if (failure == 0) platformReady = false;
        if (failure == 1) app->serial.blocked = true;
        if (failure == 2) app->serial.configuring = true;
        if (failure == 3) assert(app->owner.beginConfiguration(nowUs()));
        const unsigned writes = hardware.writes, resets = hardware.rxResets,
            configCalls = hardware.configCalls;
        Probe::MonitorSnapshot state;
        assert(host(app).monitor(app, nullptr, state) == Probe::Action::OK);
        assert(state.settings.enabled && state.remaining == 3);
        feed("@1 monitor\n"); drain();
        assert(Serial.output.find("\"enabled\":true") != std::string::npos);
        feed("@2 monitor off\n"); drain(); assertStopped();
        assert(Serial.output.find("\"enabled\":false") != std::string::npos);
        assert(hardware.writes == writes && hardware.rxResets == resets && hardware.configCalls == configCalls);
        assert(!app->owner.pending() && !app->owner.active());
        if (failure == 3) assert(app->owner.configurationOwned());
    }
}

void testOffCancelsUnsentMonitorWithoutTraffic() {
    for (bool throughConsole : {false, true}) {
        fresh(); enable(); serviceMonitor(*app, nowUs());
        auto& record = app->records[REQUEST_CAPACITY];
        assert(record.monitored && record.read.state == ReadState::ACTIVE && app->owner.pending() == 1);
        const auto request = record.requestId;
        platformReady = false; app->serial.blocked = true;
        if (throughConsole) feed("@1 monitor off\n");
        else {
            Probe::MonitorSettings off; Probe::MonitorSnapshot state;
            assert(host(app).monitor(app, &off, state) == Probe::Action::OK);
        }
        assertStopped();
        assert(record.cancelContinuation && app->monitorState.cancelled == 1);
        const auto* cancelled = app->owner.result(request);
        assert(cancelled && cancelled->outcome == Rtu::Outcome::CANCELLED && !cancelled->transport.txAccepted);
        advanceReads(*app, nowUs());
        assert(record.read.state == ReadState::FAILED && record.read.outcome == MotorControlRS::ReadOutcome::CANCELLED);
        feed("@2 monitor off\n"); assert(app->monitorState.cancelled == 1);
        platformReady = true; app->serial.blocked = false;
        for (unsigned i = 0; i < 100; ++i) { advanceHardware(hardware.time + 10); loop(); }
        assert(!hardware.writes && !app->owner.pending() && !app->owner.active());
    }
}

void testOffCancelsContinuationDuringConfigurationOwnership() {
    fresh(); enable();
    // Represent the actual read sequence between settled transactions. No
    // owner request is admitted, so idle configuration ownership is possible.
    auto& record = app->records[REQUEST_CAPACITY];
    record.operationId = 1; record.typedRead = record.monitored = true;
    assert(ESS::prepareState(record.read, app->axis.target, record.operationId,
        nowUs(), nowUs() + REQUEST_US, communicationTuple(app->serial.active)));
    app->monitorState.operationId = record.operationId;
    assert(app->owner.beginConfiguration(nowUs()));
    // Fill the output sink and console pending line; local off still dispatches.
    Serial.writeCapacity = 0;
    for (unsigned i = 0; i < OUTPUT_LINES + 1; ++i) feed("status\n");
    assert(app->console.outputPending());
    feed("@20 monitor off\n"); assertStopped();
    assert(record.read.state == ReadState::FAILED && record.read.outcome == MotorControlRS::ReadOutcome::CANCELLED);
    assert(app->monitorState.cancelled == 1 && app->owner.configurationOwned());
    assert(!hardware.writes && !app->owner.pending() && !app->owner.active());
}

void testTargetSelectionIsLocalAndPreservesUncertainResults() {
    using namespace MotorControlRS;
    fresh();
    auto& record = app->records[0]; record.operationId = 1;
    record.address = record.commandId = 1; record.actionOperation = true;
    record.serialTuple = app->serial.active; record.serialGeneration = app->serial.generation;
    assert(ESS::prepareNormalStop(record.action, app->axis.target, record.operationId,
        nowUs(), nowUs() + REQUEST_US));
    ActionEvent event; event.transport.target = app->axis.target;
    event.transport.operationId = record.operationId; event.transport.kind = ReadEventKind::TRANSPORT_FAILURE;
    event.transport.txAccepted = 8; event.transport.executionUnknown = true; event.txComplete = true;
    assert(ESS::advanceAction(record.action, event, nowUs()));
    assert(record.action.execution == ActionExecution::UNKNOWN);
    updateActionReservation(*app, record); assert(axisReserved(*app, 1));
    const auto original = record.action;
    // A checked old identity may still await output delivery at rebinding.
    auto& identity = app->records[1]; identity.operationId = identity.commandId = 2;
    identity.address = 1; identity.typedRead = true;
    identity.serialTuple = app->serial.active; identity.serialGeneration = app->serial.generation;
    assert(ESS::prepareIdentity(identity.read, app->axis.target, identity.operationId,
        nowUs(), nowUs() + REQUEST_US, communicationTuple(app->serial.active)));
    uint8_t bytes[] = {1, 3, 8, 0x4E, 0xEA, 0, 0x29, 0, 1, 0, 0, 0, 0};
    const auto crc = ESS::calcCrc16(bytes, sizeof(bytes) - 2);
    bytes[11] = static_cast<uint8_t>(crc); bytes[12] = static_cast<uint8_t>(crc >> 8);
    ReadEvent read; read.target = app->axis.target; read.operationId = identity.operationId;
    read.kind = ReadEventKind::FRAME; read.frame = bytes; read.length = sizeof(bytes); read.txAccepted = 8;
    read.qualified = true; read.earliestUs = read.latestUs = nowUs();
    assert(ESS::advanceRead(identity.read, read, nowUs()) && identity.read.state == ReadState::SUCCEEDED);
    app->axis.units.commandStepsPerMotorTurn = UnitScale(1000, 1, ScaleSource::ASSUMED);
    app->axis.units.settings.position = PositionUnit::DEGREES;
    app->axis.originKnown = app->axis.softLimitsKnown = true;
    app->configuration.operationId = 23; app->identity.operationId = 24;
    app->coordinateReference.nativeKnown = true;
    const auto generation = app->axis.generation, binding = app->bindingGeneration;
    const unsigned writes = hardware.writes, rxResets = hardware.rxResets,
        configCalls = hardware.configCalls;
    assert(host(app).selectTarget(app, 2) == Probe::Action::OK);
    assert(app->axis.target.id == 2 && app->axis.target.address == 2 && app->axis.target.generation == binding + 1);
    assert(app->axis.generation == generation + 1 && !app->axis.originKnown && !app->axis.softLimitsKnown);
    assert(app->axis.units.commandStepsPerMotorTurn.source == ScaleSource::UNKNOWN);
    assert(app->axis.units.settings.position == PositionUnit::DEGREES && !app->coordinateReference.nativeKnown);
    assert(!app->configuration.operationId && !app->identity.operationId && !app->communicationKnown);
    assert(app->inputWiring[0] == InputWiring::UNKNOWN && app->outputWiring[0] == InputWiring::UNKNOWN);
    Probe::ResultView view;
    assert(lookup(app, 1, view) && view.actionContext->execution == ActionExecution::UNKNOWN);
    assert(!std::memcmp(&original, view.actionContext, sizeof(original)) && axisReserved(*app, 1));
    deliver(*app); drain();
    assert(!app->identity.operationId && !app->communicationKnown);
    feed("@3 useaddr 3\n"); drain();
    assert(app->axis.target.address == 3 && app->axis.generation == generation + 2);
    assert(lookup(app, 1, view) && !std::memcmp(&original, view.actionContext, sizeof(original)));
    assert(hardware.writes == writes && hardware.rxResets == rxResets && hardware.configCalls == configCalls);
    assert(host(app).selectTarget(app, 3) == Probe::Action::OK && app->axis.generation == generation + 2);
}

void testTargetSelectionRefusesBusyInvalidAndExhaustedRequests() {
    fresh(); const auto original = app->axis;
    assert(host(app).selectTarget(app, 0) == Probe::Action::INVALID);
    assert(host(app).selectTarget(app, 248) == Probe::Action::INVALID);
    for (unsigned busy = 0; busy < 6; ++busy) {
        fresh();
        if (busy == 0) enable();
        if (busy == 1) app->discovery.owned = true;
        if (busy == 2) app->persistenceCapture = true;
        if (busy == 3) app->motionProfile.view.pending = true;
        if (busy == 4) assert(app->owner.beginConfiguration(nowUs()));
        if (busy == 5) {
            uint32_t operation = 0; assert(probe(app, 1, 1, operation) == Probe::Action::OK);
        }
        assert(host(app).selectTarget(app, 2) == Probe::Action::BUSY);
        assert(app->axis.target.address == original.target.address && app->axis.generation == original.generation);
        assert(!hardware.writes);
    }
    fresh(); app->axis.generation = UINT32_MAX;
    assert(host(app).selectTarget(app, 2) == Probe::Action::IDS_EXHAUSTED && app->axis.target.address == 1);
    app->axis.generation = 0;
    assert(host(app).selectTarget(app, 2) == Probe::Action::IDS_EXHAUSTED && app->axis.generation == 0);
    app->axis.generation = 1; app->bindingGeneration = UINT32_MAX;
    assert(host(app).selectTarget(app, 2) == Probe::Action::IDS_EXHAUSTED && !hardware.writes);
}

void testHomeAvailabilityReasonsBeforeTraffic() {
    fresh(); ESS::HomeRequest request; uint32_t operation = 777;
    request.method = static_cast<ESS::HomingMethod>(0);
    assert(startHome(app, 1, 1, request, operation) == Probe::Action::INVALID);
    request.method = ESS::HomingMethod::COLLISION_MINUS_1;
    assert(startHome(app, 1, 1, request, operation) == Probe::Action::UNRESOLVED);
    request.method = ESS::HomingMethod::METHOD_1;
    assert(startHome(app, 1, 1, request, operation) == Probe::Action::UNIMPLEMENTED);
    request.method = ESS::HomingMethod::METHOD_35; request.offset = 1;
    assert(startHome(app, 1, 1, request, operation) == Probe::Action::UNRESOLVED);
    assert(operation == 777 && !hardware.writes && !app->owner.pending());
}

void testOldMotionProfileCannotRestoreAfterRebinding() {
    fresh();
    auto& saved = app->motionProfile;
    saved.view.saved = saved.view.ok = true;
    saved.view.address = app->axis.target.address; saved.view.generation = app->axis.generation;
    saved.view.serialGeneration = app->serial.generation; saved.bindingGeneration = app->bindingGeneration;
    saved.configuration = app->configuration.raw;
    const auto original = saved;
    const unsigned writes = hardware.writes, rxResets = hardware.rxResets, configCalls = hardware.configCalls;
    assert(host(app).selectTarget(app, 2) == Probe::Action::OK);
    assert(host(app).selectTarget(app, 1) == Probe::Action::OK);
    // Even newly checked current configuration at the original address cannot
    // relabel the old restoration snapshot with this binding generation.
    app->configuration.operationId = 50; app->configuration.target = app->axis.target;
    app->knownTargets[0] |= 2;
    Probe::MotionProfileView view;
    assert(motionProfileCommand(app, Probe::MotionProfileCommand::RESTORE, view) == Probe::Action::UNAVAILABLE);
    assert(!std::memcmp(&original, &saved, sizeof(original)) && !saved.view.pending);
    assert(hardware.writes == writes && hardware.rxResets == rxResets && hardware.configCalls == configCalls);
}
}
int main() {
    testCachedQueryAndOffUnderHostFailures();
    testOffCancelsUnsentMonitorWithoutTraffic();
    testOffCancelsContinuationDuringConfigurationOwnership();
    testTargetSelectionIsLocalAndPreservesUncertainResults();
    testTargetSelectionRefusesBusyInvalidAndExhaustedRequests();
    testHomeAvailabilityReasonsBeforeTraffic();
    testOldMotionProfileCannotRestoreAfterRebinding();
    std::puts("Production monitor local-control tests passed");
}
