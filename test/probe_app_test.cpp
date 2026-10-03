// SPDX-License-Identifier: MIT
// Exercise actual setup/loop and callbacks. Fakes supply SDK/wire/USB evidence.
#include "../examples/probe_cli/main.cpp"
#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <vector>

FakeSerial Serial;
namespace {
const std::vector<uint8_t> REPLY = {1, 3, 2, 0, 0x3C, 0xB8, 0x55};
void fresh() {
    if (app) { app->~App(); std::free(app); app = nullptr; }
    uart.~Esp32S3Uart(); new (&uart) Esp32S3Uart;
#if MOTORCONTROLRS_LOAD_FIXTURE
    loadFixture.~Esp32Load(); new (&loadFixture) Esp32Load;
    fixtureReady = false; nextServiceUs = 0;
#endif
    resetHardware(); Serial = FakeSerial(); platformReady = false;
    setup();
    assert(app && uart.ready() && app->owner.valid() && hardware.writes == 0);
    assert(Serial.txTimeoutMs == 0);
    hardware.txCharacterUs = 87;
}
void step(uint32_t us = 10) { advanceHardware(hardware.time + us); loop(); }
void pump(unsigned loops = 64) { for (unsigned i = 0; i < loops; ++i) step(); }
void contains(const char* value) {
    if (Serial.output.find(value) == std::string::npos)
        std::fprintf(stderr, "Missing [%s] in output [%s]\n", value, Serial.output.c_str());
    assert(Serial.output.find(value) != std::string::npos);
}
std::size_t occurrences(const std::string& text, const std::string& value) {
    std::size_t count = 0, at = 0;
    while ((at = text.find(value, at)) != std::string::npos) { ++count; at += value.size(); }
    return count;
}
void command(const char* text) {
    Serial.input = text; Serial.output.clear();
    for (unsigned i = 0; i < 500 && !Serial.input.empty(); ++i) step();
    assert(Serial.input.empty()); pump();
}
Probe::ResultView view(uint32_t operation) {
    Probe::ResultView result;
    assert(host(app).result(app, operation, result)); return result;
}
uint32_t admit(uint32_t commandId = 1, uint8_t address = 1) {
    const std::string line = "@" + std::to_string(commandId) + " probe " + std::to_string(address) + "\n";
    command(line.c_str()); contains("\"result\":\"accepted\"");
    Probe::ResultView admitted;
    assert(host(app).result(app, 0, admitted) && admitted.operationId);
    return admitted.operationId;
}
void startTx(unsigned beforeWrites) {
    for (unsigned i = 0; i < 1000 && hardware.writes == beforeWrites; ++i) step();
    assert(hardware.writes == beforeWrites + 1);
}
void reply(uint32_t operation, unsigned beforeWrites, const std::vector<uint8_t>& bytes = REPLY) {
    startTx(beforeWrites);
    scheduleReply(std::max(hardware.writeStarted + 8 * 87 + 1000, hardware.time + 1000), bytes);
    for (unsigned i = 0; i < 25000 && view(operation).pending; ++i) step();
    assert(!view(operation).pending); pump();
}
void timerCapture() { if (!uart.stats().timer) assert(uart.startCapture(20, timing().holdUs)); }

void testSuccessfulProbeAndReset() {
    fresh(); command("@1 probe 1\n");
    contains("\"result\":\"accepted\""); contains("\"operation_id\":1");
    reply(1, 0); contains("\"type\":\"probe\""); contains("\"raw_model\":60");
    assert(app->known && app->ok && hardware.writes == 1);
    assert(view(1).probe.outcome == Rtu::Outcome::SUCCESS);
    Probe::Snapshot cached; snapshot(app, cached); snapshot(app, cached);
    assert(cached.retained == 1 && cached.reserved == 0 && cached.deadlineUs == 0);
    command("@2 reset\n"); contains("\"result\":\"done\"");
    assert(app->runner.stats().started == 0 && app->ok);
    command("@3 status\n"); contains("\"codec\":\"OK\""); contains("\"transmit_enabled\":false");
    command("@4 result 1\n"); contains("\"operation_id\":1"); contains("\"raw_model\":60");
    assert(hardware.writes == 1);
}
void testCheckedExceptionAndParserRejection() {
    fresh(); const uint32_t operation = admit();
    reply(operation, 0, {1, 0x83, 2, 0xC0, 0xF1});
    assert(view(operation).probe.outcome == Rtu::Outcome::DEVICE_REJECTED);
    assert(app->known && !app->ok && !app->owner.needsRecovery());
    Probe::Snapshot failed; snapshot(app, failed);
    assert(!failed.modelKnown && failed.modelOperationId == 0 && failed.deliveredUs == 0);
    command("@2 status\n"); contains("\"codec\":\"EXCEPTION\""); contains("\"recovery_required\":false");
    fresh(); const uint32_t corrupt = admit();
    reply(corrupt, 0, {1, 3, 2, 0, 0x3C, 0xB8, 0x54});
    assert(view(corrupt).probe.outcome == Rtu::Outcome::INVALID_REPLY);
    assert(!app->ok && app->owner.needsRecovery());
    command("@3 reset\n"); assert(app->owner.needsRecovery());
    command("@4 probe\n"); contains("\"result\":\"recovery_required\"");
    assert(hardware.writes == 1);
    command("@5 result 1\n"); contains("\"codec\":\"CRC_ERROR\"");
}
void testActiveConsoleAndBoundedInputOutput() {
    fresh(); const uint32_t operation = admit(); startTx(0);
    assert(app->owner.active() && app->runner.transmitEnabled());
    const std::string text = "@2 status\n@3 health\n@4 drv\n@5 stats\n";
    Serial.input = text; Serial.output.clear(); const uint64_t before = hardware.time;
    step(); assert(text.size() - Serial.input.size() <= 32);
    assert(!Serial.input.empty() && hardware.time - before < 1000);
    for (unsigned i = 0; i < 100 && !Serial.input.empty(); ++i) step();
    assert(Serial.input.empty() && app->owner.active() && Serial.maxWriteSize <= 64);
    scheduleReply(hardware.writeStarted + 8 * 87 + 1000, REPLY);
    for (unsigned i = 0; i < 1000 && view(operation).pending; ++i) step();
    assert(!view(operation).pending); pump();
    contains("\"command\":\"status\""); contains("\"command\":\"health\"");
    assert(hardware.writes == 1 && view(operation).probe.outcome == Rtu::Outcome::SUCCESS);
}
void testQueuePressureAndQueuedCancellation() {
    fresh(); const uint32_t active = admit(); startTx(0); uint32_t queued[4];
    for (unsigned i = 0; i < 4; ++i) queued[i] = admit(i + 2);
    uint32_t rejected = 0xA55A;
    assert(probe(app, 7, 1, rejected) == Probe::Action::QUEUE_FULL && rejected == 0xA55A);
    assert(app->owner.pending() == 4 && hardware.writes == 1);
    const std::string cancelLine = "@8 cancel " + std::to_string(queued[1]) + "\n";
    command(cancelLine.c_str()); contains("\"result\":\"done\"");
    assert(view(queued[1]).probe.outcome == Rtu::Outcome::CANCELLED);
    assert(view(queued[1]).probe.transport.txAccepted == 0 && hardware.writes == 1);
    command("@9 recover\n"); contains("\"result\":\"accepted\"");
    for (unsigned i = 0; i < 4; ++i) assert(!view(queued[i]).pending);
    for (unsigned i = 0; i < 80000 && app->owner.recovering(); ++i) step();
    assert(!app->owner.recovering() && !view(active).pending && hardware.writes == 1);
}
void testOutputBackpressureKeepsTransportAndTerminal() {
    fresh(); timerCapture(); Serial.writeCapacity = 0; Serial.input = "@10 probe\n";
    for (unsigned i = 0; i < 1000 && !Serial.input.empty(); ++i) step();
    assert(Serial.input.empty()); startTx(0);
    scheduleReply(hardware.writeStarted + 8 * 87 + 1000, REPLY);
    for (unsigned i = 0; i < 1000 && view(1).pending; ++i) step();
    assert(!view(1).pending && app->ok && Serial.output.empty() && Serial.writeCalls == 0);
    const Probe::ResultView retained = view(1); pump();
    assert(view(1).probe.transport.endedUs == retained.probe.transport.endedUs);
    Serial.writeCapacity = 4096; Serial.writeLimit = 7; pump(1000);
    contains("\"result\":\"accepted\""); contains("\"type\":\"probe\"");
    assert(occurrences(Serial.output, "\"type\":\"probe\"") == 1);
    assert(Serial.maxWriteSize <= 64 && hardware.writes == 1);
    command("@11 result 1\n"); contains("\"raw_model\":60");
}
void testFullRetainedResultsAndIndependentRecovery() {
    fresh();
    for (unsigned i = 0; i < 8; ++i) {
        const unsigned writes = hardware.writes; const uint32_t operation = admit(i + 1);
        reply(operation, writes);
    }
    uint32_t rejected = 0x1234;
    assert(probe(app, 9, 1, rejected) == Probe::Action::RESULTS_FULL && rejected == 0x1234);
    command("@10 recover\n"); contains("\"result\":\"accepted\"");
    for (unsigned i = 0; i < 80000 && app->owner.recovering(); ++i) step();
    assert(!app->owner.recovering()); pump(); contains("\"type\":\"recovery\"");
    for (unsigned i = 1; i <= 8; ++i) assert(view(i).probe.outcome == Rtu::Outcome::SUCCESS);
    assert(hardware.writes == 8);
    command("@11 release 1\n"); contains("\"result\":\"done\"");
    command("@12 probe\n"); contains("\"result\":\"accepted\"");
    command("@13 cancel\n");
    for (unsigned i = 0; i < 1000 && app->owner.active(); ++i) step();
}
void testSaturatedUsbDoesNotBlockCancellation() {
    fresh(); timerCapture(); const uint32_t operation = admit(); startTx(0);
    Serial.writeCapacity = 0; Serial.output.clear(); Serial.input.clear();
    for (unsigned i = 2; i < 15; ++i) Serial.input += "@" + std::to_string(i) + " status\n";
    Serial.input += "@99 cancel " + std::to_string(operation) + "\n";
    Serial.input += "@100 probe\n"; // Full output cannot admit another bus request.
    for (unsigned i = 0; i < 1000 && !Serial.input.empty(); ++i) step();
    assert(Serial.input.empty());
    for (unsigned i = 0; i < 1000 && view(operation).pending; ++i) step();
    assert(!view(operation).pending && view(operation).probe.cancellation == Rtu::Cancellation::REQUEST);
    Probe::ResultView rejected;
    assert(!host(app).result(app, 2, rejected));
    assert(app->console.inputDropped() && hardware.writes == 1);
    assert(app->outputCount == OUTPUT_LINES && app->console.outputPending());
    Serial.writeCapacity = 4096; pump(1000);
    assert(occurrences(Serial.output, "\"type\":\"probe\"") == 1);
}
void testRecoveryWaitsForPhysicalSettlementAndSeparateGuard() {
    fresh(); hardware.txCharacterUs = 0; const uint32_t operation = admit(); startTx(0);
    advanceHardware(hardware.time + 30000); loop(); pump();
    assert(!view(operation).pending && app->runner.transmitEnabled());
    assert(view(operation).probe.transport.reason == Rtu::Reason::TX_TIMEOUT);
    const unsigned resets = hardware.rxResets;
    command("@2 recover\n"); contains("\"result\":\"accepted\"");
    assert(app->owner.recovering() && hardware.de == 1 && hardware.rxResets == resets);
    advanceHardware(hardware.time + RECOVER_US); loop();
    assert(app->owner.recovering() && hardware.rxResets == resets);
    fakeUart.status.txfifo_cnt = 0; fakeUart.fsm_status.st_utx_out = 0;
    for (unsigned i = 0; i < 20 && app->runner.transmitEnabled(); ++i) step(100);
    assert(!app->runner.transmitEnabled() && hardware.de == 0);
    assert(app->recoveryGuardUntilUs >= hardware.deReleasedAt + RECOVER_US);
    const uint64_t guard = app->recoveryGuardUntilUs; pump();
    assert(app->owner.recovering() && hardware.rxResets == resets);
    advanceHardware(guard - 10000); loop();
    assert(app->owner.recovering() && hardware.rxResets == resets);
    advanceHardware(guard + 10000); loop(); pump();
    assert(!app->owner.recovering() && hardware.rxResets == resets + 1 && hardware.writes == 1);
    assert(view(operation).probe.transport.reason == Rtu::Reason::TX_TIMEOUT);
    assert(view(operation).probe.executionUnknown);
}
void testCaptureFaultAndExplicitRecovery() {
    fresh(); hardware.rx.push_back(1); hardware.rx.push_back(3); uint32_t untouched = 77;
    assert(probe(app, 1, 1, untouched) == Probe::Action::RECOVERY_REQUIRED && untouched == 77);
    assert(hardware.writes == 0 && !app->owner.active());
    command("@2 health\n"); contains("\"communication\":\"failed\"");
    const unsigned resets = hardware.rxResets;
    command("@3 recover\n"); contains("\"result\":\"accepted\"");
    for (unsigned i = 0; i < 80000 && app->owner.recovering(); ++i) step();
    assert(!app->owner.recovering() && !uart.needsRecovery() && hardware.rxResets == resets + 1);
    assert(hardware.writes == 0);
}
void testRecoveryExpiryStillSettlesDriverAndRetainsBothResults() {
    fresh(); hardware.txCharacterUs = 0; const uint32_t interrupted = admit(); startTx(0);
    command("@2 recover\n"); const Probe::ResultView admittedRecovery = view(2);
    assert(admittedRecovery.pending && admittedRecovery.recovery);
    const unsigned resets = hardware.rxResets;
    // The platform gate remains shut while physical TX is busy. Deadline expiry
    // must still publish the distinct control result, without a UART reset.
    advanceHardware(hardware.time + 2100000); loop(); pump();
    const auto expired = view(2);
    assert(!expired.pending && expired.recoveryResult.outcome == Rtu::RecoveryOutcome::EXPIRED);
    assert(app->runner.transmitEnabled() && hardware.de == 1 && hardware.rxResets == resets);
    assert(!view(interrupted).pending && view(interrupted).probe.executionUnknown);
    const auto retained = view(interrupted).probe.transport;
    command("@3 release 2\n"); contains("\"result\":\"done\"");
    command("@4 probe\n"); contains("\"result\":\"recovery_required\"");
    assert(hardware.writes == 1);
    fakeUart.status.txfifo_cnt = 0; fakeUart.fsm_status.st_utx_out = 0;
    for (unsigned i = 0; i < 20 && app->runner.transmitEnabled(); ++i) step(100);
    assert(!app->runner.transmitEnabled() && hardware.de == 0 && hardware.rxResets == resets);
    command("@5 recover\n"); contains("\"result\":\"accepted\"");
    for (unsigned i = 0; i < 80000 && app->owner.recovering(); ++i) step();
    assert(!app->owner.recovering() && hardware.rxResets == resets + 1 && hardware.writes == 1);
    assert(view(interrupted).probe.transport.endedUs == retained.endedUs);
    assert(view(interrupted).probe.executionUnknown);
}
void testRetainedTimingDiagnosticsSurviveLaterTransaction() {
    fresh(); const uint32_t first = admit(); reply(first, 0);
    const auto retained = view(first).probe;
    assert(retained.txEndUs && retained.firstRxStartUs > retained.txEndUs);
    assert(retained.txUncertaintyUs && retained.maxRxUncertaintyUs);
    std::vector<uint8_t> other = {2, 3, 2, 0, 61};
    const uint16_t crc = MotorControlRS::ESS_RS::calcCrc16(other.data(), other.size());
    other.push_back(static_cast<uint8_t>(crc)); other.push_back(static_cast<uint8_t>(crc >> 8));
    const uint32_t second = admit(2, 2); reply(second, 1, other);
    assert(view(second).probe.rawModel == 61 && app->address == 2);
    const auto historical = view(first).probe;
    assert(historical.txEndUs == retained.txEndUs && historical.firstRxStartUs == retained.firstRxStartUs);
    assert(historical.txUncertaintyUs == retained.txUncertaintyUs && historical.maxRxUncertaintyUs == retained.maxRxUncertaintyUs);
    assert(historical.rawModel == 60 && historical.rx[0] == 1);
    assert(hardware.writes == 2);
}
void testUnsentCancellationPreservesCachedObservation() {
    fresh(); const uint32_t observed = admit(); reply(observed, 0);
    Probe::Snapshot before; snapshot(app, before);
    // Both complete command lines enter in one bounded input service. Cancellation
    // reaches the queued request before the next owner service can assert DE.
    command("@2 probe\n@3 cancel 2\n");
    const auto cancelled = view(2).probe;
    assert(cancelled.outcome == Rtu::Outcome::CANCELLED && cancelled.transport.txAccepted == 0);
    Probe::Snapshot after; snapshot(app, after);
    assert(after.probeKnown && after.probeOk && after.rawModel == before.rawModel);
    assert(after.observedEarliestUs == before.observedEarliestUs && after.observedLatestUs == before.observedLatestUs);
    assert(after.deliveredUs == before.deliveredUs && after.ageMs >= before.ageMs && hardware.writes == 1);
}
void testRecoveryInvalidationIgnoresOutputAndOccursOnce() {
    fresh(); timerCapture(); const uint32_t observed = admit(); reply(observed, 0);
    assert(app->known && app->ok);
    Serial.writeCapacity = 0; Serial.input = "@2 recover\n";
    for (unsigned i = 0; i < 100 && !Serial.input.empty(); ++i) step();
    assert(Serial.input.empty() && app->owner.recovering());
    for (unsigned i = 3; i < 16; ++i) Serial.input += "@" + std::to_string(i) + " status\n";
    for (unsigned i = 0; i < 1000 && app->owner.recovering(); ++i) step();
    assert(!app->owner.recovering() && !app->known && !app->ok);
    assert(view(observed).probe.outcome == Rtu::Outcome::SUCCESS && hardware.writes == 1);
    Serial.writeCapacity = 4096; pump(1000);
    const uint32_t freshObservation = admit(20); reply(freshObservation, 1);
    assert(app->known && app->ok && app->model == 60);
    pump(); assert(app->known && app->ok); // Unread recovery evidence must not erase newer data.
    assert(!view(2).pending && view(2).recovery);
}
void testDelayedCaptureKeepsObservationAge() {
    fresh(); timerCapture(); const uint32_t operation = admit(); startTx(0);
    scheduleReply(hardware.writeStarted + 8 * 87 + 1000, REPLY);
    advanceHardware(hardware.time + 50000); loop(); pump();
    assert(!view(operation).pending && app->ok); const auto observed = view(operation).probe;
    assert(observed.transport.closureQualified && observed.observedLatestUs < observed.deliveredUs);
    assert(observed.deliveredUs - observed.observedLatestUs > 30000);
    Probe::Snapshot first; snapshot(app, first); assert(first.ageMs >= 30);
    const uint64_t earliest = first.observedEarliestUs, latest = first.observedLatestUs;
    command("@2 status\n"); command("@3 health\n");
    Probe::Snapshot later; snapshot(app, later);
    assert(later.observedEarliestUs == earliest && later.observedLatestUs == latest);
    assert(later.ageMs >= first.ageMs && hardware.writes == 1);
    const unsigned resets = hardware.rxResets; command("@4 recover\n");
    assert(app->owner.recovering() && hardware.rxResets == resets);
    for (unsigned i = 0; i < 80000 && app->owner.recovering(); ++i) step();
    assert(!app->owner.recovering() && hardware.writes == 1);
}
void testLookupReplacesReusedResultView() {
    fresh(); const uint32_t first = admit(); reply(first, 0);
    command("@2 recover\n");
    for (unsigned i = 0; i < 80000 && app->owner.recovering(); ++i) step();
    assert(!app->owner.recovering());
    Probe::ResultView reused;
    assert(host(app).result(app, 2, reused) && reused.recovery && !reused.pending);
    assert(host(app).result(app, first, reused));
    assert(!reused.recovery && !reused.pending && reused.address == 1 && reused.probe.rawModel == 60);
    const Probe::ResultView preserved = reused;
    assert(!host(app).result(app, 0xFFFFFFFF, reused));
    assert(reused.operationId == preserved.operationId && reused.commandId == preserved.commandId);
    assert(reused.recovery == preserved.recovery && reused.pending == preserved.pending);
    assert(reused.probe.rawModel == preserved.probe.rawModel && reused.probe.rx == preserved.probe.rx);
    assert(reused.probe.deliveredUs == preserved.probe.deliveredUs);
    const uint32_t pending = admit(3);
    assert(host(app).result(app, pending, reused));
    assert(!reused.recovery && reused.pending && reused.operationId == pending);
    assert(!reused.probe.codecChecked && reused.probe.transport.reason == Rtu::Reason::NONE);
    assert(reused.probe.rawModel == 0 && reused.probe.tx == nullptr && reused.probe.rx == nullptr);
}
void testExpiredRecoveryDoesNotResetIdleAdapter() {
    fresh(); command("@1 recover\n");
    assert(app->owner.recovering() && !app->runner.busy() && !app->runner.transmitEnabled());
    const unsigned resets = hardware.rxResets;
    advanceHardware(app->recovery.deadlineUs + 10000); loop();
    const auto expired = view(1);
    assert(!expired.pending && expired.recoveryResult.outcome == Rtu::RecoveryOutcome::EXPIRED);
    assert(hardware.rxResets == resets && hardware.writes == 0 && app->owner.needsRecovery());
}
void testBlockedOutputDoesNotBlockNewerCachedObservation() {
    fresh(); timerCapture(); command("@1 probe 1\n@2 probe 2\n");
    assert(view(1).pending && view(2).pending);
    Serial.writeCapacity = 0; Serial.output.clear();
    for (unsigned i = 10; i < 24; ++i) Serial.input += "@" + std::to_string(i) + " status\n";
    for (unsigned i = 0; i < 1000 && !Serial.input.empty(); ++i) step();
    assert(Serial.input.empty() && app->outputCount == OUTPUT_LINES && app->console.outputPending());
    reply(1, 0);
    std::vector<uint8_t> second = {2, 3, 2, 0, 61};
    const uint16_t crc = MotorControlRS::ESS_RS::calcCrc16(second.data(), second.size());
    second.push_back(static_cast<uint8_t>(crc)); second.push_back(static_cast<uint8_t>(crc >> 8));
    reply(2, 1, second);
    assert(view(1).probe.outcome == Rtu::Outcome::SUCCESS && view(2).probe.outcome == Rtu::Outcome::SUCCESS);
    assert(hardware.writes == 2 && app->console.outputPending());
    Probe::Snapshot cached; snapshot(app, cached);
    assert(cached.probeKnown && cached.probeOk && cached.probeAddress == 2 && cached.rawModel == 61);
    assert(cached.modelKnown && cached.modelAddress == 2 && cached.modelOperationId == 2);
    assert(cached.observedEarliestUs == view(2).probe.observedEarliestUs);
    assert(cached.observedLatestUs == view(2).probe.observedLatestUs);
    assert(cached.deliveredUs == 0); // New evidence has not transferred to the console yet.
    Serial.writeCapacity = 4096; pump(1000);
    assert(occurrences(Serial.output, "\"type\":\"probe\"") == 2);
    snapshot(app, cached);
    assert(cached.deliveredUs && cached.deliveredUs == view(2).probe.deliveredUs);
    assert(cached.modelAddress == 2 && cached.modelOperationId == 2);
}
void testSnapshotReportsEarliestActiveAndRecoveryDeadline() {
    fresh(); hardware.txCharacterUs = 0; const uint32_t operation = admit(); startTx(0);
    const uint64_t requestDeadline = app->records[0].deadlineUs;
    command("@2 recover\n");
    assert(app->owner.active() && app->owner.recovering() && view(operation).pending);
    assert(requestDeadline < app->recovery.deadlineUs);
    Probe::Snapshot reused; snapshot(app, reused);
    assert(reused.deadlineUs == requestDeadline);
    fakeUart.status.txfifo_cnt = 0; fakeUart.fsm_status.st_utx_out = 0;
    for (unsigned i = 0; i < 1000 && view(operation).pending; ++i) step();
    assert(!view(operation).pending && app->owner.recovering());
    snapshot(app, reused); assert(reused.deadlineUs == app->recovery.deadlineUs);
    for (unsigned i = 0; i < 80000 && app->owner.recovering(); ++i) step();
    assert(!app->owner.recovering()); snapshot(app, reused); assert(reused.deadlineUs == 0);
}
void checkFailedProbeKeepsValidModelAndAge(const std::vector<uint8_t>& failedReply,
                                         MotorControlRS::Err codecError) {
    fresh(); const uint32_t first = admit(); reply(first, 0);
    Probe::Snapshot before; snapshot(app, before);
    assert(before.probeKnown && before.probeOk && before.modelKnown && before.rawModel == 60);
    assert(before.modelAddress == 1 && before.modelOperationId == first && before.deliveredUs);
    const uint32_t failed = admit(2, 2); reply(failed, 1, failedReply);
    contains("\"raw_model\":null");
    const auto terminal = view(failed).probe;
    assert(terminal.codecChecked && terminal.codec.code == codecError && terminal.rawModel == 0);
    Probe::Snapshot after; snapshot(app, after);
    assert(after.probeKnown && !after.probeOk && after.codecChecked && after.codec.code == codecError);
    assert(after.probeAddress == 2 && after.modelKnown && after.modelAddress == 1);
    assert(after.modelOperationId == first);
    assert(after.rawModel == before.rawModel);
    assert(after.observedEarliestUs == before.observedEarliestUs && after.observedLatestUs == before.observedLatestUs);
    assert(after.deliveredUs == before.deliveredUs && after.ageMs >= before.ageMs);
    assert(view(first).probe.rawModel == 60 && hardware.writes == 2);
    command("@3 status\n"); contains("\"probe_address\":2"); contains("\"model_address\":1");
    contains("\"raw_model\":60");
}
void testCheckedExceptionKeepsValidModelAndAge() {
    std::vector<uint8_t> exception = {2, 0x83, 2};
    const uint16_t crc = MotorControlRS::ESS_RS::calcCrc16(exception.data(), exception.size());
    exception.push_back(static_cast<uint8_t>(crc)); exception.push_back(static_cast<uint8_t>(crc >> 8));
    checkFailedProbeKeepsValidModelAndAge(exception, MotorControlRS::Err::EXCEPTION);
}
void testBadCrcKeepsValidModelAndAge() {
    std::vector<uint8_t> corrupt = {2, 3, 2, 0, 0x3C};
    const uint16_t crc = MotorControlRS::ESS_RS::calcCrc16(corrupt.data(), corrupt.size());
    corrupt.push_back(static_cast<uint8_t>(crc)); corrupt.push_back(static_cast<uint8_t>((crc >> 8) ^ 1));
    checkFailedProbeKeepsValidModelAndAge(corrupt, MotorControlRS::Err::CRC_ERROR);
}
void testDelayedHarvestKeepsSuccessIndependentOfLatestFailure() {
    fresh(); timerCapture(); command("@1 probe 1\n@2 probe 2\n");
    // Advance the actual UART and owner while application harvesting is delayed.
    // Result slots retain both completions; their bookkeeping order is unrelated
    // to wire order when released records are reused.
    const auto ownerStep = []() {
        advanceHardware(hardware.time + 10); app->owner.service(uart.sample());
    };
    for (unsigned i = 0; i < 1000 && hardware.writes < 1; ++i) ownerStep();
    assert(hardware.writes == 1);
    scheduleReply(hardware.time + 2000, REPLY);
    for (unsigned i = 0; i < 25000 && view(1).pending; ++i) ownerStep();
    assert(!view(1).pending && !app->records[0].observed);
    for (unsigned i = 0; i < 1000 && hardware.writes < 2; ++i) ownerStep();
    assert(hardware.writes == 2);
    std::vector<uint8_t> exception = {2, 0x83, 2};
    const uint16_t crc = MotorControlRS::ESS_RS::calcCrc16(exception.data(), exception.size());
    exception.push_back(static_cast<uint8_t>(crc)); exception.push_back(static_cast<uint8_t>(crc >> 8));
    scheduleReply(hardware.time + 2000, exception);
    for (unsigned i = 0; i < 25000 && view(2).pending; ++i) ownerStep();
    assert(view(1).probe.outcome == Rtu::Outcome::SUCCESS);
    assert(view(2).probe.outcome == Rtu::Outcome::DEVICE_REJECTED);
    assert(!app->records[1].observed);
    std::swap(app->records[0], app->records[1]);
    deliver(*app);
    Probe::Snapshot cached; snapshot(app, cached);
    assert(cached.probeKnown && !cached.probeOk && cached.probeAddress == 2);
    assert(cached.codecChecked && cached.codec.code == MotorControlRS::Err::EXCEPTION);
    assert(cached.modelKnown && cached.rawModel == 60 && cached.modelAddress == 1 && cached.modelOperationId == 1);
    assert(cached.observedEarliestUs == view(1).probe.observedEarliestUs);
    assert(cached.observedLatestUs == view(1).probe.observedLatestUs);
    assert(cached.deliveredUs && cached.deliveredUs == view(1).probe.deliveredUs);
}
#if MOTORCONTROLRS_LOAD_FIXTURE
void testLoadLocalAdmission() {
    fresh(); command("@1 load\n"); contains("\"command\":\"load\"");
    command("@2 load 1000 5000 32\n"); Probe::LoadSnapshot settings;
    assert(load(app, nullptr, settings) == Probe::Action::OK);
    assert(settings.settings.workUs == 1000 && settings.settings.ownerDelayUs == 5000 && settings.settings.consoleBytes == 32);
    command("@3 load 0 0 0\n");
    const uint32_t operation = admit(4); startTx(0);
    command("@5 load\n"); contains("\"command\":\"load\"");
    command("@6 load 1000 1000 1\n"); contains("\"result\":\"busy\"");
    reply(operation, 0);
    assert(view(operation).probe.outcome == Rtu::Outcome::SUCCESS && hardware.writes == 1);
}
void testLoadDelayExhaustsSetupTxBudgetWithoutTransmission() {
    fresh(); command("@1 load 0 20000 0\n");
    Serial.input = "@2 probe\n";
    for (unsigned i = 0; i < 100 && !Serial.input.empty(); ++i) step();
    assert(Serial.input.empty());
    const uint32_t operation = view(0).operationId;
    for (unsigned i = 0; i < 100 && app->runner.phase() != Rtu::Phase::SETUP; ++i) step(100);
    assert(app->runner.phase() == Rtu::Phase::SETUP && hardware.de == 1 && hardware.writes == 0);
    // The real fixture delays every active owner service, including SETUP.
    // Its 20 ms setting plus service jitter exceeds the unchanged 20 ms
    // DE-to-TX deadline before the port's write callback may be called.
    advanceHardware(hardware.time + 20050); loop(); pump();
    const auto failed = view(operation).probe;
    assert(failed.transport.reason == Rtu::Reason::TX_TIMEOUT);
    assert(failed.transport.txAccepted == 0 && !failed.executionUnknown);
    assert(!app->owner.active() && app->owner.needsRecovery());
    assert(!app->runner.transmitEnabled() && hardware.de == 0 && hardware.writes == 0);
    assert(uart.stats().faults == 0);
    pump(1000);
    assert(view(operation).probe.transport.reason == Rtu::Reason::TX_TIMEOUT && hardware.writes == 0);
    command("@3 probe\n"); contains("\"result\":\"recovery_required\"");
    assert(hardware.writes == 0);
}
#endif
}
int main() {
    testSuccessfulProbeAndReset(); testCheckedExceptionAndParserRejection();
    testActiveConsoleAndBoundedInputOutput(); testQueuePressureAndQueuedCancellation();
    testOutputBackpressureKeepsTransportAndTerminal(); testFullRetainedResultsAndIndependentRecovery();
    testSaturatedUsbDoesNotBlockCancellation();
    testRecoveryWaitsForPhysicalSettlementAndSeparateGuard(); testCaptureFaultAndExplicitRecovery();
    testRecoveryExpiryStillSettlesDriverAndRetainsBothResults(); testRetainedTimingDiagnosticsSurviveLaterTransaction();
    testUnsentCancellationPreservesCachedObservation(); testRecoveryInvalidationIgnoresOutputAndOccursOnce();
    testDelayedCaptureKeepsObservationAge();
    testLookupReplacesReusedResultView(); testExpiredRecoveryDoesNotResetIdleAdapter();
    testBlockedOutputDoesNotBlockNewerCachedObservation(); testSnapshotReportsEarliestActiveAndRecoveryDeadline();
    testCheckedExceptionKeepsValidModelAndAge(); testBadCrcKeepsValidModelAndAge();
    testDelayedHarvestKeepsSuccessIndependentOfLatestFailure();
#if MOTORCONTROLRS_LOAD_FIXTURE
    testLoadLocalAdmission();
    testLoadDelayExhaustsSetupTxBudgetWithoutTransmission();
#endif
    app->~App(); std::free(app); app = nullptr;
}
