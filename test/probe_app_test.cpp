// SPDX-License-Identifier: MIT
// Exercise actual setup/loop and callbacks. Fakes supply SDK/wire/USB evidence.
#include "../examples/probe_cli/main.cpp"
#include "../examples/probe_cli/StateCache.h"
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
    resetHardware(); Serial = FakeSerial(); platformReady = false; writeResponseConfirmed = false;
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
void testCaptureReadUsesOwnerAndPreservesModel() {
    fresh(); timerCapture();
    const uint32_t modelId = admit(); reply(modelId, 0);
    const auto observed = app->observedEarliestUs;
    const auto delivered = app->deliveredUs;
    command("@2 capture-read 1\n"); contains("\"result\":\"accepted\"");
    const uint32_t operation = view(0).operationId;
    assert(view(operation).captureRead && view(operation).pending);
    std::vector<uint8_t> bytes(37, 0); bytes[0] = 1; bytes[1] = 3; bytes[2] = 32;
    for (unsigned i = 0; i < 16; ++i) { bytes[3 + i * 2] = 0x12; bytes[4 + i * 2] = static_cast<uint8_t>(i); }
    const uint16_t crc = MotorControlRS::ESS_RS::calcCrc16(bytes.data(), 35);
    bytes[35] = static_cast<uint8_t>(crc); bytes[36] = static_cast<uint8_t>(crc >> 8);
    reply(operation, 1, bytes);
    assert(hardware.tx[2] == 1 && hardware.tx[3] == 0x30 && hardware.tx[4] == 0 && hardware.tx[5] == 16);
    assert(view(operation).probe.outcome == Rtu::Outcome::SUCCESS && view(operation).probe.rxLength == 37);
    contains("\"type\":\"capture_read\""); contains("\"raw_model\":null");
    contains("\"register_start\":304"); contains("\"register_count\":16");
    assert(app->model == 60 && app->modelOperationId == modelId && app->cacheOperationId == modelId);
    assert(app->observedEarliestUs == observed && app->deliveredUs == delivered && app->ok);
    const auto ended = view(operation).probe.transport.endedUs;
    command(("@3 result " + std::to_string(operation) + "\n").c_str());
    contains("\"capture_read\":true"); contains("\"raw_model\":null");
    assert(view(operation).probe.transport.endedUs == ended && hardware.writes == 2);
    command(("@4 release " + std::to_string(operation) + "\n").c_str());
    Probe::ResultView gone; assert(!lookup(app, operation, gone));
}
void testCaptureReadRejectsMalformedRepliesAndArguments() {
    for (const bool exception : {false, true}) {
        fresh(); timerCapture(); command("@1 capture-read 0\n"); contains("invalid_address");
        command("@2 capture-read 1 16\n"); contains("invalid_arguments");
        assert(hardware.writes == 0 && app->owner.pending() == 0);
        command("@3 capture-read\n"); const uint32_t operation = view(0).operationId;
        reply(operation, 0, exception ? std::vector<uint8_t>{1, 0x83, 2, 0xC0, 0xF1} : REPLY);
        assert(!app->modelKnown && !app->known);
        assert(view(operation).captureRead && view(operation).probe.outcome != Rtu::Outcome::SUCCESS);
        assert(app->owner.needsRecovery() == !exception);
        if (exception) assert(view(operation).probe.outcome == Rtu::Outcome::DEVICE_REJECTED);
        command(("@4 result " + std::to_string(operation) + "\n").c_str());
        contains("\"capture_read\":true"); contains("\"raw_model\":null");
    }
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
std::vector<uint8_t> registerReply(std::initializer_list<uint16_t> words) {
    std::vector<uint8_t> bytes = {1, 3, static_cast<uint8_t>(words.size() * 2)};
    for (uint16_t word : words) { bytes.push_back(static_cast<uint8_t>(word >> 8)); bytes.push_back(static_cast<uint8_t>(word)); }
    const uint16_t crc = ESS::calcCrc16(bytes.data(), bytes.size());
    bytes.push_back(static_cast<uint8_t>(crc)); bytes.push_back(static_cast<uint8_t>(crc >> 8)); return bytes;
}
void qualifyActions(uint8_t address = 1) {
    writeResponseConfirmed = true; app->communicationKnown = true;
    app->knownTargets[address / 8] |= static_cast<uint8_t>(1U << (address % 8));
    app->communicationTarget.id = app->communicationTarget.address = address;
    app->communicationTarget.generation = app->bindingGeneration;
}
uint32_t actionAdmission(MotorControlRS::ActionKind kind, uint32_t commandId = 1, uint8_t address = 1) {
    MotorControlRS::ActionRequest request; request.kind = kind;
    if (kind == MotorControlRS::ActionKind::STOP) request.stop.behavior = MotorControlRS::StopBehavior::CONFIGURED_DECELERATION;
    uint32_t operation = 0;
    assert(startAction(app, commandId, address, request, operation) == Probe::Action::OK);
    assert(operation && view(operation).actionContext); return operation;
}
void actionStep(uint32_t operation, const std::vector<uint8_t>& bytes = {}) {
    const uint8_t token = view(operation).actionContext->step;
    const auto* record = findRecord(*app, operation);
    const auto initialAccepted = app->owner.txAccepted(record->requestId);
    const unsigned initialWrites = hardware.writes;
    for (unsigned i = 0; i < 25000 && !app->owner.txAccepted(record->requestId); ++i) step();
    if (app->owner.txAccepted(record->requestId) != 8)
        std::fprintf(stderr, "Action frame unavailable: id=%u token=%u accepted=%zu writes=%u->%u state=%u outcome=%u phase=%u\n",
            operation, token, initialAccepted, initialWrites, hardware.writes, static_cast<unsigned>(record->action.state),
            static_cast<unsigned>(record->action.outcome), static_cast<unsigned>(app->runner.phase()));
    assert(app->owner.txAccepted(record->requestId) == 8);
    const std::vector<uint8_t> response = bytes.empty() ? hardware.tx : bytes;
    scheduleReply(std::max(hardware.writeStarted + 8 * 87 + 1000, hardware.time + 1000), response);
    for (unsigned i = 0; i < 25000 && view(operation).pending && view(operation).actionContext->step == token; ++i) step();
    assert(!view(operation).pending || view(operation).actionContext->step != token);
}
void testActionGateAndSeparateAcknowledgement() {
    using namespace MotorControlRS;
    fresh(); timerCapture(); ActionRequest request; uint32_t id = 123;
    assert(startAction(app, 1, 1, request, id) == Probe::Action::UNAVAILABLE);
    assert(id == 123 && hardware.writes == 0 && app->owner.pending() == 0);
    qualifyActions(); const uint32_t operation = actionAdmission(ActionKind::ENABLE);
    assert(axisReserved(*app, 1));
    assert(startAction(app, 2, 1, request, id) == Probe::Action::AXIS_CONFLICT && id == 123);
    actionStep(operation);
    assert(view(operation).actionContext->execution == ActionExecution::ACKNOWLEDGED);
    assert(view(operation).actionContext->completion == ActionCompletion::NOT_OBSERVED);
    assert(view(operation).pending && axisReserved(*app, 1) && !app->owner.active() && !app->owner.pending());
    // Non-consuming reads can use the released bus during the observation wait.
    uint32_t read = 0; assert(probe(app, 3, 1, read) == Probe::Action::OK);
    reply(read, 1);
    actionStep(operation, registerReply({0, 0}));
    assert(view(operation).actionContext->completion == ActionCompletion::OBSERVED && !axisReserved(*app, 1));
    assert(hardware.writes == 3 && !app->owner.needsRecovery());
}
void testStopSupersedesOnlyAfterAdmissionAndSettlesInflight() {
    using namespace MotorControlRS;
    for (unsigned phase = 0; phase < 8; ++phase) {
        fresh(); timerCapture(); qualifyActions();
        const uint32_t operation = actionAdmission(ActionKind::ENABLE);
        if (phase >= 1) { step(); assert(app->runner.phase() == Rtu::Phase::WAIT_BUS); }
        if (phase >= 2) {
            for (unsigned i = 0; i < 1000 && app->runner.phase() != Rtu::Phase::SETUP; ++i) step();
            assert(app->runner.phase() == Rtu::Phase::SETUP);
        }
        if (phase >= 3) startTx(0);
        if (phase >= 4 && phase != 7) actionStep(operation);
        if (phase >= 5 && phase != 7) startTx(1); // Already-transmitted observation, also settles.
        if (phase == 7) {
            for (unsigned i = 0; i < 1000 && app->runner.phase() != Rtu::Phase::RECEIVE; ++i) step();
            assert(app->runner.phase() == Rtu::Phase::RECEIVE && hardware.de == 0);
        }
        const unsigned oldWrites = hardware.writes;
        ActionRequest unsupported; unsupported.kind = ActionKind::STOP;
        unsupported.stop.behavior = StopBehavior::CONFIGURED_DECELERATION;
        unsupported.stop.deviceQueue = DeviceQueue::DISCARD;
        uint32_t rejected = 555;
        assert(startAction(app, 2, 1, unsupported, rejected) == Probe::Action::UNSUPPORTED);
        assert(rejected == 555 && !findRecord(*app, operation)->cancelContinuation && hardware.writes == oldWrites);
        const uint32_t stop = actionAdmission(ActionKind::STOP, 3);
        assert(findRecord(*app, operation)->cancelContinuation);
        if (phase == 3 || phase == 7) actionStep(operation);
        else if (phase >= 5) actionStep(operation, registerReply({0, static_cast<uint16_t>(phase == 5 ? 0x10 : 0)}));
        else advanceActions(*app, nowUs());
        assert(!view(operation).pending);
        const auto interrupted = *view(operation).actionContext;
        assert(interrupted.outcome == (phase == 6 ? ActionOutcome::OBSERVED : ActionOutcome::CANCELLED));
        assert(view(operation).interruptedByStop);
        actionStep(stop); actionStep(stop, registerReply({0x1234, 8})); // Alarm does not block stop completion.
        assert(view(stop).actionContext->completion == ActionCompletion::OBSERVED);
        assert(view(operation).actionContext->outcome == interrupted.outcome);
        assert(view(operation).actionContext->execution == interrupted.execution);
        assert(!axisReserved(*app, 1) && !app->owner.needsRecovery());
        assert(hardware.writes == oldWrites + 2);
    }
}
void testStopUsesReservedCapacityAndFullAdmissionPreservesWork() {
    using namespace MotorControlRS;
    fresh(); timerCapture(); qualifyActions();
    const uint32_t active = actionAdmission(ActionKind::ENABLE);
    App::Record occupied; occupied.action.request.kind = ActionKind::STOP;
    ESS::ActionContext stopContext; ReadTarget target; target.id = target.address = target.generation = 1;
    assert(ESS::prepareNormalStop(stopContext, target, 999, nowUs(), nowUs() + REQUEST_US));
    ESS::PreparedAction work; assert(ESS::nextAction(stopContext, nowUs(), work));
    assert(admitActionStep(*app, occupied, work, nowUs()) == Rtu::BusAdmission::ACCEPTED);
    const auto pending = app->owner.pending();
    ActionRequest stop; stop.kind = ActionKind::STOP; stop.stop.behavior = StopBehavior::DIRECT;
    uint32_t unchanged = 777;
    assert(startAction(app, 2, 1, stop, unchanged) == Probe::Action::RESULTS_FULL);
    assert(unchanged == 777 && app->owner.pending() == pending && hardware.writes == 0);
    assert(!findRecord(*app, active)->cancelContinuation && !view(active).interruptedByStop);
    assert(view(active).actionContext->state == ActionState::ACTIVE);

    fresh(); timerCapture(); qualifyActions();
    uint32_t retained[REQUEST_CAPACITY];
    for (unsigned i = 0; i < REQUEST_CAPACITY; ++i) { retained[i] = admit(i + 1); reply(retained[i], i); }
    command("@20 stop normal\n"); contains("\"result\":\"accepted\"");
    const uint32_t stopping = view(0).operationId;
    actionStep(stopping); actionStep(stopping, registerReply({0, 0})); pump(200);
    assert(view(stopping).actionContext->completion == ActionCompletion::OBSERVED);
    for (const auto operation : retained) assert(view(operation).probe.outcome == Rtu::Outcome::SUCCESS);
    assert(hardware.writes == REQUEST_CAPACITY + 2);
    command(("@21 release " + std::to_string(stopping) + "\n").c_str()); contains("\"result\":\"done\"");
}
void testUncertainStopSurvivesReleaseRecoveryAndCanBeStoppedAgain() {
    using namespace MotorControlRS;
    fresh(); timerCapture(); qualifyActions();
    command("@1 stop direct\n"); const uint32_t old = view(0).operationId; startTx(0);
    for (unsigned i = 0; i < 30000 && view(old).pending; ++i) step();
    assert(!view(old).pending && view(old).actionContext->execution == ActionExecution::UNKNOWN);
    assert(axisReserved(*app, 1) && app->owner.needsRecovery() && hardware.writes == 1);
    pump(1000); assert(hardware.writes == 1);
    command(("@2 release " + std::to_string(old) + "\n").c_str()); contains("\"result\":\"done\"");
    Probe::ResultView removed; assert(!lookup(app, old, removed) && axisReserved(*app, 1));
    command("@3 recover\n");
    for (unsigned i = 0; i < 80000 && app->owner.recovering(); ++i) step();
    assert(!app->owner.needsRecovery() && axisReserved(*app, 1));
    command("@4 enable\n"); contains("axis_conflict"); assert(hardware.writes == 1);
    command("@5 stop normal\n"); contains("\"result\":\"accepted\"");
    const uint32_t freshStop = view(0).operationId;
    actionStep(freshStop); actionStep(freshStop, registerReply({0xFFFF, 8})); pump(100);
    assert(!axisReserved(*app, 1) && hardware.writes == 3);
    assert(view(freshStop).actionContext->completion == ActionCompletion::OBSERVED);
}
void testBadActionReplyKeepsCodecEvidenceAndNoReplay() {
    using namespace MotorControlRS;
    fresh(); timerCapture(); qualifyActions(); const uint32_t action = actionAdmission(ActionKind::RELEASE);
    startTx(0); auto invalid = hardware.tx; invalid.back() ^= 1;
    actionStep(action, invalid);
    const auto& failed = *view(action).actionContext;
    assert(failed.outcome == ActionOutcome::REPLY_ERROR && failed.execution == ActionExecution::UNKNOWN);
    assert(failed.writeEvidence.status.code == Err::CRC_ERROR);
    assert(failed.writeEvidence.length == 8 && failed.writeEvidence.raw[7] == invalid[7]);
    assert(axisReserved(*app, 1) && app->owner.needsRecovery());
    pump(1000); assert(hardware.writes == 1);
}
void testUnknownActionExceptionKeepsReservationAcrossReleaseAndRecovery() {
    using namespace MotorControlRS;
    fresh(); timerCapture(); qualifyActions();
    command("@1 enable\n"); const uint32_t original = view(0).operationId;
    std::vector<uint8_t> exception = {1, 0x86, 0xE7};
    const uint16_t crc = ESS::calcCrc16(exception.data(), exception.size());
    exception.push_back(static_cast<uint8_t>(crc)); exception.push_back(static_cast<uint8_t>(crc >> 8));
    actionStep(original, exception); pump(100);
    const auto failed = *view(original).actionContext;
    assert(failed.outcome == ActionOutcome::REPLY_ERROR && failed.execution == ActionExecution::UNKNOWN);
    assert(failed.writeEvidence.status.code == Err::EXCEPTION && failed.writeEvidence.status.detail == 0xE7);
    assert(axisReserved(*app, 1) && hardware.writes == 1);
    assert(release(app, original) == Probe::Action::OK && axisReserved(*app, 1));
    uint32_t recovery = 0; assert(recover(app, 2, recovery) == Probe::Action::OK);
    for (unsigned i = 0; i < 80000 && app->owner.recovering(); ++i) step();
    assert(!app->owner.needsRecovery() && axisReserved(*app, 1) && hardware.writes == 1);
    ActionRequest enable; uint32_t unchanged = 777;
    assert(startAction(app, 3, 1, enable, unchanged) == Probe::Action::AXIS_CONFLICT && unchanged == 777);
    const uint32_t stopping = actionAdmission(ActionKind::STOP, 4);
    actionStep(stopping); actionStep(stopping, registerReply({0xFFFF, 8}));
    assert(view(stopping).actionContext->completion == ActionCompletion::OBSERVED);
    assert(!axisReserved(*app, 1) && hardware.writes == 3);
}
void testAcceptedStopRetainsBothResultsWhenInterruptedWriteLaterFails() {
    using namespace MotorControlRS;
    for (bool badReply : {false, true}) {
        fresh(); timerCapture(); qualifyActions();
        const uint32_t original = actionAdmission(ActionKind::ENABLE);
        startTx(0);
        command("@2 stop normal\n"); const uint32_t stopping = view(0).operationId;
        assert(view(original).interruptedByStop && app->owner.pending() == 1);
        if (badReply) {
            auto bytes = hardware.tx; bytes.back() ^= 1;
            actionStep(original, bytes);
        } else {
            for (unsigned i = 0; i < 30000 && view(original).pending; ++i) step();
        }
        assert(!view(original).pending && view(stopping).pending);
        const auto failed = *view(original).actionContext;
        assert(failed.execution == ActionExecution::UNKNOWN);
        assert(failed.outcome == (badReply ? ActionOutcome::REPLY_ERROR : ActionOutcome::TRANSPORT_ERROR));
        assert(app->owner.needsRecovery() && app->owner.pending() == 1 && hardware.writes == 1);
        pump(100); assert(hardware.writes == 1 && view(stopping).pending);
        uint32_t recovery = 0; assert(recover(app, 3, recovery) == Probe::Action::OK);
        for (unsigned i = 0; i < 80000 && app->owner.recovering(); ++i) step();
        pump(100);
        assert(!app->owner.needsRecovery() && hardware.writes == 1);
        const auto cancelled = *view(stopping).actionContext;
        assert(cancelled.outcome == ActionOutcome::CANCELLED && cancelled.execution == ActionExecution::NOT_TRANSMITTED);
        assert(view(original).actionContext->outcome == failed.outcome);
        assert(view(original).actionContext->execution == failed.execution && view(original).interruptedByStop);
        assert(axisReserved(*app, 1));
        assert(release(app, stopping) == Probe::Action::OK);
        const uint32_t explicitStop = actionAdmission(ActionKind::STOP, 4);
        actionStep(explicitStop); actionStep(explicitStop, registerReply({0, 0}));
        assert(!axisReserved(*app, 1) && hardware.writes == 3);
        assert(view(original).actionContext->execution == ActionExecution::UNKNOWN);
    }
}
void testStopKeepsKnownTargetAfterUnrelatedReadAndAction() {
    using namespace MotorControlRS;
    fresh(); timerCapture(); qualifyActions();
    const uint32_t original = actionAdmission(ActionKind::ENABLE);
    actionStep(original);
    uint32_t otherRead = 0; assert(probe(app, 2, 2, otherRead) == Probe::Action::OK);
    auto bytes = REPLY; bytes[0] = 2;
    const uint16_t crc = ESS::calcCrc16(bytes.data(), bytes.size() - 2);
    bytes[5] = static_cast<uint8_t>(crc); bytes[6] = static_cast<uint8_t>(crc >> 8);
    reply(otherRead, 1, bytes);
    assert(app->communicationTarget.address == 2 && axisReserved(*app, 1));
    const uint32_t unrelated = actionAdmission(ActionKind::RELEASE, 3, 2);
    assert(axisReserved(*app, 1) && axisReserved(*app, 2));
    assert(cancel(app, unrelated) == Probe::Action::OK);
    const uint32_t stopping = actionAdmission(ActionKind::STOP, 4, 1);
    if (app->owner.txAccepted(findRecord(*app, original)->requestId)) actionStep(original, registerReply({0, 0x10}));
    else advanceActions(*app, nowUs());
    actionStep(stopping); actionStep(stopping, registerReply({0xFFFF, 8}));
    assert(view(stopping).actionContext->completion == ActionCompletion::OBSERVED);
    assert(view(unrelated).actionContext->execution == ActionExecution::NOT_TRANSMITTED);
    assert(!axisReserved(*app, 1) && !axisReserved(*app, 2));
}
void testStopAndInterruptedResultSurviveBlockedConsole(const char* debugMode) {
    using namespace MotorControlRS;
    fresh(); timerCapture(); qualifyActions();
    command((std::string("@90 debug ") + debugMode + "\n").c_str());
    assert(hardware.writes == 0 && app->owner.pending() == 0);
    command("@1 enable\n");
    const uint32_t original = view(0).operationId; startTx(0);
    Serial.writeCapacity = 0;
    for (unsigned id = 2; id < 15; ++id) Serial.input += "@" + std::to_string(id) + " status\n";
    for (unsigned i = 0; i < 100 && !Serial.input.empty(); ++i) step();
    assert(app->console.outputPending() && app->outputCount == OUTPUT_LINES);
    Serial.input = "@20 stop normal\n";
    for (unsigned i = 0; i < 100 && !Serial.input.empty(); ++i) step();
    assert(Serial.input.empty()); const uint32_t stopping = app->latestOperationId;
    assert(stopping != original && view(stopping).actionContext && view(original).interruptedByStop);
    actionStep(original); actionStep(stopping); actionStep(stopping, registerReply({0, 0}));
    assert(!view(original).pending && !view(stopping).pending);
    assert(release(app, stopping) == Probe::Action::BUSY);
    Serial.writeCapacity = 4096; pump(2000);
    const std::string acceptance = "\"id\":20,\"command\":\"stop\",\"ok\":true,\"result\":\"accepted\"";
    const auto accepted = Serial.output.find(acceptance);
    const auto terminal = Serial.output.find("\"type\":\"action\",\"profile\":\"ess_rs\",\"id\":20");
    assert(accepted != std::string::npos && terminal != std::string::npos && accepted < terminal);
    assert(findRecord(*app, original)->delivered && findRecord(*app, stopping)->delivered);
    assert(view(original).actionContext->outcome == ActionOutcome::CANCELLED);
    assert(view(stopping).actionContext->completion == ActionCompletion::OBSERVED && hardware.writes == 3);
}
void readStep(uint32_t operation, uint8_t index, const std::vector<uint8_t>& bytes) {
    const unsigned writes = hardware.writes;
    if (app->runner.phase() != Rtu::Phase::DRAIN) startTx(writes);
    scheduleReply(std::max(hardware.writeStarted + 8 * 87 + 1000, hardware.time + 1000), bytes);
    for (unsigned i = 0; i < 25000 && view(operation).typedRead->state == ReadState::ACTIVE &&
        view(operation).typedRead->step == index; ++i) step();
    assert(view(operation).typedRead->step != index || !view(operation).pending);
}
void completeConfig(uint32_t operation, uint16_t wordOrder = 1, uint16_t algorithm = 2) {
    readStep(operation, 0, registerReply({1, 1000}));
    readStep(operation, 1, registerReply({0, 0, 0}));
    readStep(operation, 2, registerReply({1, 0, wordOrder}));
    readStep(operation, 3, registerReply({0x8005, 0, 1, 6, 17}));
    readStep(operation, 4, registerReply({algorithm, 4000})); pump(100);
}
void testTypedReadRoutesAndAtomicPublication() {
    fresh(); timerCapture(); command("@1 caps\n"); contains("\"command\":\"caps\"");
    command("@2 read identity 1\n"); const uint32_t identity = view(0).operationId;
    contains("accepted"); assert(view(identity).typedRead && !view(identity).captureRead);
    readStep(identity, 0, registerReply({0x4EEA, 0xCAFE, 1, 0xFFFF})); pump(100);
    assert(!view(identity).pending && app->identity.rawModel == 0x4EEA && app->identity.rawVersion == 0xCAFE);
    contains("\"type\":\"read\""); contains("\"read_kind\":\"identity\"");
    const auto oldIdentity = app->identity;
    command("@3 profile ess_rs config 1\n"); const uint32_t config = view(0).operationId;
    assert(app->configuration.operationId == 0); completeConfig(config);
    assert(view(config).typedRead->state == ReadState::SUCCEEDED && hardware.writes == 6);
    assert(app->configuration.raw.subdivision == 1000 && app->configuration.raw.encoderResolution == 4000);
    assert(app->configuration.raw.wordOrder == 1 && app->configuration.raw.inputFunctions[0] == 0);
    assert(app->configuration.activeSerial.known && app->configuration.activeSerial.baud == 115200);
    // This application bench declaration is independent of assignments and levels.
    for (unsigned terminal = 0; terminal < ESS::READ_INPUT_COUNT; ++terminal)
        assert(app->configuration.wiring[terminal] == MotorControlRS::InputWiring::UNCONNECTED &&
               !app->configuration.inputLevelKnown[terminal]);
    assert(app->configuration.units.commandStepsPerMotorTurn.source == MotorControlRS::ScaleSource::UNKNOWN);
    assert(app->configuration.units.encoder.countsPerUnit.source == MotorControlRS::ScaleSource::READBACK);
    const auto previous = app->configuration;
    command("@4 result 2\n"); contains("\"read_kind\":\"config\"");
    assert(hardware.writes == 6 && app->identity.rawVersion == oldIdentity.rawVersion);
    command("@5 read config\n"); const uint32_t failed = view(0).operationId;
    readStep(failed, 0, registerReply({0, 2000}));
    assert(app->configuration.operationId == previous.operationId && app->configuration.raw.subdivision == 1000);
    readStep(failed, 1, {1, 0x83, 2, 0xC0, 0xF1}); pump(100);
    assert(view(failed).typedRead->outcome == MotorControlRS::ReadOutcome::REPLY_ERROR);
    assert(view(failed).typedRead->status.code == MotorControlRS::Err::EXCEPTION && !app->owner.needsRecovery());
    assert(app->configuration.operationId == previous.operationId && app->configuration.raw.subdivision == 1000);
    assert(hardware.writes == 8); pump(500); assert(hardware.writes == 8);
    command("@6 release 2\n"); contains("done");
    Probe::ResultView released; assert(!lookup(app, config, released));
}
void testTypedReadPartialCancelRecoveryAndRetention() {
    fresh(); timerCapture(); command("@1 read config\n"); const uint32_t operation = view(0).operationId;
    readStep(operation, 0, registerReply({0, 1000}));
    const uint64_t deadline = view(operation).typedRead->deadlineUs;
    command("@2 recover\n");
    for (unsigned i = 0; i < 80000 && app->owner.recovering(); ++i) step();
    pump(100);
    assert(!view(operation).pending && view(operation).typedRead->state == ReadState::FAILED);
    assert(view(operation).typedRead->deadlineUs == deadline && app->configuration.operationId == 0);
    const unsigned writes = hardware.writes; pump(1000); assert(hardware.writes == writes);
    assert(app->bindingGeneration == 2 && view(operation).typedRead->target.generation == 1);
    const auto retained = *view(operation).typedRead;
    command("@3 result 1\n"); contains("\"read_kind\":\"config\"");
    assert(view(operation).typedRead->servicedUs == retained.servicedUs);
    assert(cancel(app, operation) == Probe::Action::ALREADY_TERMINAL);
    command("@4 read identity\n"); const uint32_t next = view(0).operationId;
    assert(view(next).typedRead->target.generation == 2);
    assert(cancel(app, next) == Probe::Action::OK);
    for (unsigned i = 0; i < 1000 && view(next).pending; ++i) step();
    assert(!view(next).pending && view(next).typedRead->outcome == MotorControlRS::ReadOutcome::CANCELLED);
    assert(hardware.writes == writes);
}
void testTypedReadPressureAndInvalidArguments() {
    fresh(); timerCapture();
    command("@1 read unsupported\n"); command("@2 read config 0\n"); command("@3 profile other identity\n");
    command("@4 profile ess_rs config 1 junk\n"); assert(hardware.writes == 0 && app->latestOperationId == 0);
    for (unsigned i = 0; i < 8; ++i) {
        uint32_t operation = 0; assert(typedRead(app, i + 10, 1, ESS::ReadKind::IDENTITY, operation) == Probe::Action::OK);
        assert(cancel(app, operation) == Probe::Action::OK); advanceReads(*app, nowUs());
        assert(!view(operation).pending);
    }
    uint32_t unchanged = 123;
    assert(typedRead(app, 99, 1, ESS::ReadKind::CONFIG, unchanged) == Probe::Action::RESULTS_FULL && unchanged == 123);
    uint32_t control = 0; assert(recover(app, 100, control) == Probe::Action::OK);
    assert(hardware.writes == 0 && app->owner.recovering());
}
void testTypedReadAbsoluteBudgetAndDelayedEvidence() {
    fresh(); timerCapture(); command("@1 read identity\n"); startTx(0);
    const auto deadline = view(1).typedRead->deadlineUs;
    scheduleReply(hardware.writeStarted + 8 * 87 + 1000, registerReply({0x4EEA, 0x1234, 1, 0}));
    advanceHardware(deadline + 10000); loop(); pump(100);
    assert(view(1).typedRead->state == ReadState::SUCCEEDED && hardware.writes == 1);
    assert(app->identity.provenance.latestUs < deadline && app->identity.provenance.deliveredUs > deadline);
    const uint64_t observed = app->identity.provenance.earliestUs;
    command("@2 result 1\n"); command("@3 status\n"); assert(app->identity.provenance.earliestUs == observed);
    fresh(); timerCapture(); command("@1 read config\n"); startTx(0);
    const auto partialDeadline = view(1).typedRead->deadlineUs;
    scheduleReply(hardware.writeStarted + 8 * 87 + 1000, registerReply({0, 1000}));
    advanceHardware(partialDeadline + 10000); loop(); pump(100);
    assert(view(1).typedRead->outcome == MotorControlRS::ReadOutcome::DEADLINE);
    assert(view(1).typedRead->completedSteps == 1 && app->configuration.operationId == 0 && hardware.writes == 1);
    fresh(); timerCapture(); command("@1 read identity\n"); startTx(0);
    const auto lateDeadline = view(1).typedRead->deadlineUs;
    const auto bytes = registerReply({0x4EEA, 0x1234, 1, 0});
    scheduleReply(lateDeadline - bytes.size() * 87 - 500, bytes);
    // Delay beyond the response timeout too: the retained physical closure is
    // already outside the earlier response/request budget, so success is forbidden.
    advanceHardware(lateDeadline + 10000); loop(); pump(100);
    assert(view(1).typedRead->state == ReadState::FAILED && app->identity.operationId == 0 && hardware.writes == 1);
}
void testTypedReadInvalidOwnerEnvelopeCannotReplay() {
    fresh(); timerCapture(); uint32_t operation = 0;
    assert(typedRead(app, 1, 1, ESS::ReadKind::CONFIG, operation) == Probe::Action::OK);
    startTx(0); scheduleReply(hardware.writeStarted + 8 * 87 + 1000, registerReply({0, 1000}));
    // Fault injection at the application handoff: real immutable completions
    // cannot be edited by callers, but a bad adapter must never replay a read.
    for (unsigned i = 0; i < 1000 && !app->owner.result(app->records[0].requestId); ++i) {
        advanceHardware(hardware.time + 10); app->owner.service(uart.sample());
    }
    auto* completion = const_cast<Rtu::Completion*>(app->owner.result(app->records[0].requestId));
    assert(completion && completion->outcome == Rtu::Outcome::SUCCESS);
    ++completion->expected.targetGeneration;
    advanceReads(*app, nowUs());
    assert(view(operation).typedRead->outcome == MotorControlRS::ReadOutcome::TRANSPORT_ERROR);
    assert(view(operation).typedRead->observations[0].transportDetail == -1);
    pump(1000); assert(hardware.writes == 1 && app->configuration.operationId == 0);
}
void testTypedReadCancelSettledIntermediateCannotContinue() {
    fresh(); timerCapture(); uint32_t operation = 0;
    assert(typedRead(app, 1, 1, ESS::ReadKind::CONFIG, operation) == Probe::Action::OK);
    startTx(0); scheduleReply(hardware.writeStarted + 8 * 87 + 1000, registerReply({0, 1000}));
    for (unsigned i = 0; i < 1000 && !app->owner.result(app->records[0].requestId); ++i) {
        advanceHardware(hardware.time + 10); app->owner.service(uart.sample());
    }
    assert(app->owner.result(app->records[0].requestId));
    assert(cancel(app, operation) == Probe::Action::OK);
    advanceReads(*app, nowUs());
    assert(view(operation).typedRead->outcome == MotorControlRS::ReadOutcome::CANCELLED);
    assert(view(operation).typedRead->completedSteps == 1 && app->configuration.operationId == 0);
    pump(1000); assert(hardware.writes == 1);
    assert(cancel(app, operation) == Probe::Action::ALREADY_TERMINAL);
}
void testTypedConfigYieldsBusAndPreservesUnknownHardwareCode() {
    fresh(); timerCapture(); uint32_t configuration = 0, probeOperation = 0;
    assert(typedRead(app, 1, 1, ESS::ReadKind::CONFIG, configuration) == Probe::Action::OK);
    startTx(0);
    assert(probe(app, 2, 1, probeOperation) == Probe::Action::OK);
    readStep(configuration, 0, registerReply({0, 1000}));
    startTx(1); assert(hardware.tx[2] == 0 && hardware.tx[3] == 0 && hardware.tx[5] == 1);
    scheduleReply(hardware.writeStarted + 8 * 87 + 1000, REPLY);
    for (unsigned i = 0; i < 1000 && view(probeOperation).pending; ++i) step();
    assert(view(probeOperation).probe.outcome == Rtu::Outcome::SUCCESS);
    readStep(configuration, 1, registerReply({0, 0, 0}));
    readStep(configuration, 2, registerReply({0, 0, 0}));
    readStep(configuration, 3, registerReply({0, 1, 2, 3, 0}));
    readStep(configuration, 4, registerReply({3, 4000})); pump(100);
    assert(view(configuration).typedRead->state == ReadState::SUCCEEDED && hardware.writes == 6);
    // Exact COM13 readback: undocumented algorithm3 remains valid raw data,
    // with no guessed enum or promotion of motion readiness.
    assert(app->configuration.raw.algorithm == 3 && !app->configuration.algorithmKnown);
    assert(app->configuration.raw.encoderResolution == 4000 && app->configuration.raw.wordOrder == 0);
    assert(app->configuration.inputFunctions[3] == ESS::InputFunction::UNDEFINED);
    assert(!app->configuration.inputLevelKnown[1] && !app->configuration.inputLevelKnown[2]);
}
ESS::ReadContext motionContext(MotorControlRS::ReadTarget target, uint32_t operation,
                              uint64_t started, uint64_t closed, uint16_t motion = 0) {
    ESS::ReadContext context;
    assert(ESS::prepareState(context, target, operation, started, closed + 1000));
    const auto bytes = registerReply({0, motion});
    MotorControlRS::ReadEvent event; event.target = target; event.operationId = operation;
    event.frame = bytes.data(); event.length = bytes.size(); event.qualified = true;
    event.earliestUs = closed - 10; event.latestUs = closed; event.txAccepted = 8;
    assert(ESS::advanceRead(context, event, closed + 20)); return context;
}
void testStateCacheRetainsValuesAcrossFailureAndGenerationChange() {
    MotorControlRS::ReadTarget target; target.id = 1; target.address = 1; target.generation = 1;
    Probe::StateCache cache;
    assert(!cache.blocks[0].valid && !Probe::fresh(cache.blocks[0], target, 1000, 10000));
    Probe::stateAttempt(cache, target, 10, 0, 100);
    const auto success = motionContext(target, 10, 100, 400, 0x10);
    Probe::stateResult(cache, success, 0, 150);
    assert(cache.blocks[0].valid && cache.blocks[0].lastAttemptOk && cache.blocks[0].value.released);
    assert(cache.blocks[0].lastAttemptUs == 100 && cache.blocks[0].lastSuccessUs == 400);
    assert(cache.blocks[0].observedEarliestUs == 150 && Probe::ageUs(cache.blocks[0], 500) == 350);
    assert(!cache.blocks[1].valid && !cache.blocks[2].valid);
    const auto previous = cache.blocks[0];
    Probe::stateAttempt(cache, target, 11, 0, 600);
    ESS::ReadContext failed; assert(ESS::prepareState(failed, target, 11, 600, 2000));
    const uint8_t exception[] = {1, 0x83, 2, 0xC0, 0xF1};
    MotorControlRS::ReadEvent event; event.target = target; event.operationId = 11;
    event.frame = exception; event.length = sizeof(exception); event.qualified = true;
    event.earliestUs = 700; event.latestUs = 710;
    assert(ESS::advanceRead(failed, event, 720)); Probe::stateResult(cache, failed, 0, 650);
    assert(cache.blocks[0].valid && !cache.blocks[0].lastAttemptOk);
    assert(cache.blocks[0].lastAttemptStatus.code == MotorControlRS::Err::EXCEPTION);
    assert(cache.blocks[0].lastAttemptUs == 600 && cache.blocks[0].lastSuccessUs == previous.lastSuccessUs);
    assert(cache.blocks[0].value.operationId == previous.value.operationId);
    assert(cache.blocks[0].observedEarliestUs == previous.observedEarliestUs);
    ++target.generation;
    assert(!Probe::current(cache.blocks[0], target) && !Probe::fresh(cache.blocks[0], target, 800, 10000));
    assert(cache.blocks[0].valid && cache.blocks[0].value.target.generation == 1);
}
void testStateCacheLateDeliveryCannotRejuvenateObservation() {
    MotorControlRS::ReadTarget target; target.id = 1; target.address = 1; target.generation = 1;
    Probe::StateCache cache;
    const auto earlier = motionContext(target, 20, 100, 400, 0x10);
    const auto newer = motionContext(target, 21, 500, 800, 0);
    Probe::stateAttempt(cache, target, 21, 0, 500); Probe::stateResult(cache, newer, 0, 550);
    const auto stored = cache.blocks[0];
    Probe::stateResult(cache, earlier, 0, 150);
    assert(cache.blocks[0].value.operationId == 21 && cache.blocks[0].value.enabled);
    assert(cache.blocks[0].lastAttemptOperationId == 21 && cache.blocks[0].lastAttemptOk);
    assert(cache.blocks[0].observedEarliestUs == stored.observedEarliestUs);
    assert(Probe::ageUs(cache.blocks[0], 5000) == 4450);
    assert(!Probe::fresh(cache.blocks[0], target, 5000, 1000));
    Probe::stateResult(cache, newer, 0, 550);
    assert(cache.blocks[0].deliveredUs == stored.deliveredUs && cache.blocks[0].lastSuccessUs == 800);
    ++target.generation;
    const auto rebound = motionContext(target, 22, 900, 1200, 0);
    Probe::stateAttempt(cache, target, 22, 0, 900); Probe::stateResult(cache, rebound, 0, 950);
    Probe::stateResult(cache, earlier, 0, 150);
    assert(cache.blocks[0].value.operationId == 22 && cache.blocks[0].value.target.generation == 2);
    assert(Probe::current(cache.blocks[0], target));
}
void completeState(uint32_t operation, uint16_t alarm = 0, uint16_t motion = 0) {
    readStep(operation, 0, registerReply({alarm, motion}));
    readStep(operation, 1, registerReply({0x8005, 0x4002}));
    readStep(operation, 2, registerReply({0x1234, 0xABCD, 0xFFFF})); pump(100);
}
void testActionEffectsInvalidateHistoricalFreshnessAndReleaseOrigin() {
    using namespace MotorControlRS;
    fresh(); timerCapture(); qualifyActions();
    command("@1 read state\n"); completeState(view(0).operationId);
    const auto prior = app->stateCache.blocks[0];
    assert(Probe::current(prior, app->axis.target));
    app->axis.originKnown = app->axis.encoderOriginKnown = app->axis.softLimitsKnown = true;
    const uint32_t generation = app->axis.generation;
    const uint32_t unsent = actionAdmission(ActionKind::RELEASE, 2);
    assert(cancel(app, unsent) == Probe::Action::OK); advanceActions(*app, nowUs());
    assert(app->axis.originKnown && app->axis.generation == generation);
    assert(Probe::current(app->stateCache.blocks[0], app->axis.target));
    const uint32_t released = actionAdmission(ActionKind::RELEASE, 3);
    startTx(3);
    assert(!app->axis.originKnown && !app->axis.encoderOriginKnown && !app->axis.softLimitsKnown);
    assert(app->axis.generation == generation + 1);
    const auto& invalidated = app->stateCache.blocks[0];
    assert(invalidated.valid && invalidated.invalidatedUs && !Probe::current(invalidated, app->axis.target));
    assert(invalidated.value.rawMotion == prior.value.rawMotion && invalidated.lastSuccessUs == prior.lastSuccessUs);
    assert(invalidated.observedEarliestUs == prior.observedEarliestUs && invalidated.observedLatestUs == prior.observedLatestUs);
    actionStep(released); actionStep(released, registerReply({0, 0x10}));
    assert(app->axis.generation == generation + 1 && !app->axis.originKnown);
    command("@4 read state\n"); completeState(view(0).operationId, 0, 0x10);
    assert(Probe::current(app->stateCache.blocks[0], app->axis.target));
    assert(app->stateCache.blocks[0].value.released && !app->axis.originKnown);
}
void testHostAxisPreparationUsesPublicApiWithoutTraffic() {
    using namespace MotorControlRS;
    fresh(); timerCapture(); command("@1 axis config\n"); contains("\"motion_command\":false");
    assert(app->axis.generation == 1 && app->axis.units.commandStepsPerMotorTurn.source == ScaleSource::UNKNOWN);
    command("@2 prepare absolute -9223372036854775808 steps native\n");
    contains("\"effective_native\":-9223372036854775808"); contains("\"displacement_known\":false");
    command("@3 axis config set command 1000\n"); contains("\"ok\":false");
    assert(app->axis.generation == 1 && hardware.writes == 0);
    command("@4 read state\n"); completeState(view(0).operationId);
    command("@5 axis config set relative-bases 1\n"); contains("\"ok\":true");
    command("@6 prepare relative 9007199254740993 steps native actual\n");
    contains("\"effective_native\":9007199254740993"); contains("\"exact_arithmetic\":true");
    command("@7 axis config set command 1000\n"); contains("\"ok\":true");
    command("@8 axis config set gear 5/2\n"); contains("\"ok\":true");
    PositionRequest request; request.configurationGeneration = app->axis.generation;
    request.value = Rational(90); request.unit = PositionUnit::DEGREES; request.frame = CoordinateFrame::LOAD;
    PreparedTarget expected; assert(preparePosition(request, app->axis, nullptr, expected));
    command("@9 prepare relative 90 deg load actual\n");
    const std::string effective = "\"effective_native\":" + std::to_string(expected.effectiveNative);
    contains(effective.c_str()); assert(expected.effectiveNative == 625);
    command("@10 prepare relative 1/2 steps native actual nearest 1/2\n");
    contains("\"effective_native\":0"); contains("\"zero_displacement\":true");
    const auto generation = app->axis.generation;
    command("@11 axis config set position-unit deg\n");
    command("@12 axis config set velocity-unit rpm\n");
    command("@13 axis config set acceleration-unit rad/s2\n");
    assert(app->axis.generation == generation + 3);
    assert(app->axis.units.settings.position == PositionUnit::DEGREES);
    assert(app->axis.units.settings.velocity.position == PositionUnit::TURNS && app->axis.units.settings.velocity.time == TimeUnit::MINUTE);
    assert(app->axis.units.settings.acceleration.position == PositionUnit::RADIANS);
    command("@14 axis origin 0\n"); contains("\"ok\":false");
    assert(!app->axis.originKnown && hardware.writes == 3);
    command("@15 prepare absolute 1 deg load\n"); contains("\"ok\":false");
    command("@16 axis config set acceleration-unit rpm/s2\n"); contains("\"ok\":false");
    assert(app->axis.generation == generation + 3 && hardware.writes == 3);
}
void testAxisConfigurationRequiresFreshIdleEvidenceAndInvalidatesPreparation() {
    using namespace MotorControlRS;
    fresh(); timerCapture(); command("@1 read state\n"); completeState(view(0).operationId);
    command("@2 axis config set relative-bases 1\n"); assert(app->axis.generation == 2);
    PositionRequest prior; prior.configurationGeneration = app->axis.generation; prior.value = Rational(3);
    const uint32_t probe = admit(3); startTx(3);
    command("@4 axis config set command 1000\n"); contains("\"ok\":false"); assert(app->axis.generation == 2);
    reply(probe, 3);
    command("@5 axis config set command 1000\n"); contains("\"ok\":true");
    PreparedTarget retained; retained.effectiveNative = 123;
    assert(!preparePosition(prior, app->axis, nullptr, retained) && retained.effectiveNative == 123);
    const auto generation = app->axis.generation;
    advanceHardware(hardware.time + 5000001);
    command("@6 status\n"); command("@7 axis config set gear 1\n"); contains("\"ok\":false");
    assert(app->axis.generation == generation && hardware.writes == 4);
    // Failed/raw history survives, but transport recovery invalidates host targets.
    uint32_t operation = 0; assert(recover(app, 8, operation) == Probe::Action::OK);
    assert(app->axis.generation == generation + 1 && app->axis.target.generation == app->bindingGeneration);
    assert(app->stateCache.blocks[0].valid && app->stateCache.blocks[0].value.target.generation == 1);
    // Exhaustion stays invalid across later recovery/configuration invalidations;
    // an old generation-one preparation must never become usable again.
    app->axis.generation = UINT32_MAX;
    invalidateAxis(*app); assert(app->axis.generation == 0);
    invalidateAxis(*app); assert(app->axis.generation == 0);
    prior.configurationGeneration = 1;
    assert(!preparePosition(prior, app->axis, nullptr, retained) && retained.effectiveNative == 123);
    command("@9 prepare relative 3 steps native actual\n"); contains("\"ok\":false");
    assert(app->axis.generation == 0 && hardware.writes == 4);
}
void testStateReadUnknownBitsAndPassiveQueries() {
    fresh(); timerCapture(); command("@1 read state\n"); const uint32_t operation = view(0).operationId;
    readStep(operation, 0, registerReply({4, 0x8010}));
    assert(app->stateCache.blocks[0].valid && !app->stateCache.blocks[1].valid);
    assert(!app->stateCache.blocks[0].value.alarmKnown && app->stateCache.blocks[0].value.rawAlarm == 4);
    assert(app->stateCache.blocks[0].value.released && !app->stateCache.blocks[0].value.enabled);
    readStep(operation, 1, registerReply({0x8005, 0x4002}));
    readStep(operation, 2, registerReply({0x1234, 0xABCD, 0xFFFF})); pump(100);
    assert(view(operation).typedRead->state == ReadState::SUCCEEDED && hardware.writes == 3);
    const auto& io = app->stateCache.blocks[1].value;
    assert(io.inputs[0] && !io.inputs[1] && io.inputs[2] && io.outputs[1]);
    assert(io.unknownInputBits == 0x8000 && io.unknownOutputBits == 0x4000);
    const auto& feedback = app->stateCache.blocks[2].value;
    assert(!feedback.pairKnown && feedback.positionSource == ESS::PositionSource::UNRESOLVED);
    assert(feedback.rawPositionWords[0] == 0x1234 && feedback.rawPositionWords[1] == 0xABCD && feedback.rawSpeed == 0xFFFF);
    const uint64_t successes[] = {app->stateCache.blocks[0].lastSuccessUs, app->stateCache.blocks[1].lastSuccessUs,
                                app->stateCache.blocks[2].lastSuccessUs};
    assert(successes[0] < successes[1] && successes[1] < successes[2]);
    command("@2 status\n"); command("@3 health\n");
    assert(hardware.writes == 3);
    command("@4 read identity\n"); const uint32_t identity = view(0).operationId;
    readStep(identity, 0, registerReply({0x4EEA, 0x29, 1, 0})); pump(100);
    for (unsigned i = 0; i < 3; ++i) assert(app->stateCache.blocks[i].lastSuccessUs == successes[i]);
    assert(hardware.writes == 4);
}
void testStatePartialRefreshPreservesPreviousBlocks() {
    fresh(); timerCapture(); command("@1 health check\n"); const uint32_t first = view(0).operationId;
    completeState(first);
    const auto previous = app->stateCache;
    command("@2 read state\n"); const uint32_t second = view(0).operationId;
    readStep(second, 0, registerReply({5, 0xC}));
    assert(app->stateCache.blocks[0].value.operationId == second && app->stateCache.blocks[0].value.running);
    readStep(second, 1, {1, 0x83, 2, 0xC0, 0xF1}); pump(100);
    assert(view(second).typedRead->state == ReadState::FAILED && !app->owner.needsRecovery());
    assert(app->stateCache.blocks[1].value.operationId == first && app->stateCache.blocks[1].valid);
    assert(!app->stateCache.blocks[1].lastAttemptOk && app->stateCache.blocks[1].lastAttemptStatus.code == MotorControlRS::Err::EXCEPTION);
    assert(app->stateCache.blocks[1].lastSuccessUs == previous.blocks[1].lastSuccessUs);
    assert(app->stateCache.blocks[1].lastAttemptUs > previous.blocks[1].lastAttemptUs);
    assert(app->stateCache.blocks[2].lastAttemptUs == previous.blocks[2].lastAttemptUs);
    assert(app->stateCache.blocks[2].lastSuccessUs == previous.blocks[2].lastSuccessUs);
    assert(app->communicationKnown && app->communicationLatestUs > previous.blocks[1].lastSuccessUs);
    command("@3 health\n"); contains("\"communication\":\"current\"");
    pump(1000); assert(hardware.writes == 5);
}
void testStateDelayedServiceAndStaleConfigurationStayExplicit() {
    fresh(); timerCapture();
    app->configuration.target.id = app->configuration.target.address = 1;
    app->configuration.target.generation = 2; app->configuration.operationId = 77;
    app->configuration.wordOrderKnown = app->configuration.algorithmKnown = true;
    app->configuration.wordOrder = ESS::WordOrder::HIGH_WORD_FIRST;
    app->configuration.algorithm = ESS::ControlAlgorithm::ALGORITHM_1;
    command("@1 read state\n"); const uint32_t operation = view(0).operationId;
    assert(view(operation).typedRead->configOperationId == 0);
    startTx(0); const uint64_t before = hardware.time;
    scheduleReply(hardware.writeStarted + 8 * 87 + 1000, registerReply({0, 0}));
    advanceHardware(before + 50000); loop();
    const auto observed = app->stateCache.blocks[0];
    assert(observed.valid && observed.observedEarliestUs <= hardware.writeStarted);
    assert(observed.observedEarliestUs < observed.observedLatestUs);
    assert(observed.deliveredUs - observed.observedLatestUs > 30000);
    assert(Probe::ageUs(observed, hardware.time) >= 50000);
    readStep(operation, 1, registerReply({0, 0})); readStep(operation, 2, registerReply({0, 0, 0})); pump(100);
    assert(!app->stateCache.blocks[2].value.pairKnown);
    const uint64_t earliest = app->stateCache.blocks[0].observedEarliestUs;
    command("@2 status\n"); command("@3 health\n");
    assert(app->stateCache.blocks[0].observedEarliestUs == earliest && hardware.writes == 3);
    command("@4 recover\n");
    MotorControlRS::ReadTarget current; current.id = current.address = 1; current.generation = app->bindingGeneration;
    assert(app->stateCache.blocks[0].valid && !Probe::current(app->stateCache.blocks[0], current));
    for (unsigned i = 0; i < 80000 && app->owner.recovering(); ++i) step();
    assert(!app->owner.recovering() && hardware.writes == 3);
    assert(app->stateCache.blocks[0].observedEarliestUs == earliest);
}
void testNewConfigurationSeparatesHistoricalFeedbackInterpretation() {
    fresh(); timerCapture(); command("@1 read config\n"); const uint32_t firstConfig = view(0).operationId;
    completeConfig(firstConfig, 0, 1);
    command("@2 read state\n"); const uint32_t firstState = view(0).operationId; completeState(firstState);
    const auto previous = app->stateCache.blocks[2];
    assert(previous.value.configOperationId == firstConfig && previous.value.pairKnown);
    assert(previous.value.rawPosition == 0x1234ABCD && previous.value.positionSource == ESS::PositionSource::COMMAND_GIVEN);
    command("@3 status\n"); contains("\"interpretation_current\":true");

    command("@4 read config\n"); const uint32_t secondConfig = view(0).operationId;
    completeConfig(secondConfig, 1, 2);
    const auto& historical = app->stateCache.blocks[2];
    assert(historical.value.configOperationId == firstConfig && historical.value.rawPosition == previous.value.rawPosition);
    assert(historical.value.positionSource == previous.value.positionSource);
    assert(historical.observedEarliestUs == previous.observedEarliestUs && historical.lastSuccessUs == previous.lastSuccessUs);
    assert(!Probe::fresh(historical, app->configuration.target, hardware.time, 5000000));
    assert(historical.invalidatedUs && historical.valid); // Raw history remains; changed decoding needs a new observation.
    command("@5 status\n"); contains("\"interpretation_current\":false");
    const std::string currentConfig = "\"current_config_operation_id\":" + std::to_string(secondConfig);
    contains(currentConfig.c_str());

    command("@6 read state\n"); const uint32_t secondState = view(0).operationId; completeState(secondState);
    const auto& refreshed = app->stateCache.blocks[2];
    assert(refreshed.value.configOperationId == secondConfig && refreshed.value.rawPosition == 0xABCD1234);
    assert(refreshed.value.positionSource == ESS::PositionSource::SUBDIVISION_EQUIVALENT_FEEDBACK);
    assert(refreshed.lastSuccessUs > previous.lastSuccessUs && app->bindingGeneration == 1);
    command("@7 health\n"); contains("\"interpretation_current\":true"); contains(currentConfig.c_str());
    assert(hardware.writes == 16);
}
void pollStep(uint8_t index, const std::vector<uint8_t>& bytes) {
    auto& record = app->records[REQUEST_CAPACITY];
    assert(record.operationId && record.read.step == index);
    const unsigned writes = hardware.writes;
    if (app->runner.phase() != Rtu::Phase::DRAIN) startTx(writes);
    scheduleReply(std::max(hardware.writeStarted + 8 * 87 + 1000, hardware.time + 1000), bytes);
    for (unsigned i = 0; i < 25000 && record.operationId && record.read.state == ReadState::ACTIVE && record.read.step == index; ++i) step();
    assert(!record.operationId || record.read.step != index || record.read.state != ReadState::ACTIVE);
}
void testMonitorDisabledFiniteAndOwnResultRelease() {
    fresh(); timerCapture(); pump(1000); assert(hardware.writes == 0);
    command("@1 monitor\n"); contains("\"enabled\":false");
    command("@2 monitor 99 1\n"); contains("invalid_arguments"); assert(hardware.writes == 0);
    command("@3 monitor 100 1\n");
    assert(app->monitorState.admitted == 1 && app->monitorState.remaining == 0);
    assert(app->latestOperationId == 0 && app->records[REQUEST_CAPACITY].monitored);
    Probe::ResultView unavailable;
    assert(!lookup(app, app->monitorState.operationId, unavailable));
    pollStep(0, registerReply({0, 0})); pollStep(1, registerReply({0, 0})); pollStep(2, registerReply({0, 0, 0})); pump(100);
    assert(!app->monitorState.settings.enabled && !app->monitorState.operationId);
    assert(!app->records[REQUEST_CAPACITY].operationId && hardware.writes == 3);
    Probe::Snapshot cached; snapshot(app, cached); assert(cached.retained == 0 && cached.reserved == 0);
    advanceHardware(hardware.time + 500000); loop(); pump(100); assert(hardware.writes == 3);
    command("@4 status\n"); command("@5 health\n"); assert(hardware.writes == 3);
}
void testMonitorCancellationSettlesTransmission() {
    fresh(); timerCapture(); hardware.txCharacterUs = 1000;
    command("@1 monitor 100 3\n"); startTx(0);
    assert(app->runner.transmitEnabled()); const uint64_t started = hardware.writeStarted;
    command("@2 monitor off\n");
    assert(!app->monitorState.settings.enabled && app->monitorState.cancelled == 1);
    assert(hardware.de == 1 && hardware.writes == 1 && hardware.time < started + 8000);
    command("@3 monitor off\n"); assert(app->monitorState.cancelled == 1);
    for (unsigned i = 0; i < 25000 && app->records[REQUEST_CAPACITY].operationId; ++i) step();
    assert(!app->records[REQUEST_CAPACITY].operationId && !app->runner.transmitEnabled());
    assert(hardware.deReleasedAt >= started + 8000 && hardware.writes == 1);
    assert(app->owner.needsRecovery());
    advanceHardware(hardware.time + 500000); loop(); pump(100); assert(hardware.writes == 1);
    command("@4 monitor off\n"); assert(app->monitorState.cancelled == 1);
}
void testMonitorYieldsToUrgentOwnerAdmission() {
    fresh(); timerCapture(); command("@1 monitor 100 1\n"); startTx(0);
    uint8_t bytes[8]; assert(ESS::buildReadRegisters(1, 0, 1, bytes, sizeof(bytes)) == sizeof(bytes));
    Rtu::BusRequest urgent; urgent.wire.bytes = bytes; urgent.wire.length = sizeof(bytes);
    urgent.wire.replyLength = REPLY.size(); urgent.wire.responseTimeoutUs = app->serial.timing.responseTimeoutUs;
    urgent.wire.replyGapUs = app->serial.timing.replyGapUs; urgent.wire.deadlineUs = nowUs() + REQUEST_US;
    urgent.expected.target = urgent.expected.address = 1; urgent.expected.targetGeneration = app->bindingGeneration;
    urgent.expected.function = 3; urgent.expected.count = 1; urgent.validator = Rtu::essValidator();
    Rtu::RequestId id; assert(app->owner.admitUrgent(urgent, nowUs(), id) == Rtu::BusAdmission::ACCEPTED);
    pollStep(0, registerReply({0, 0})); startTx(1);
    assert(hardware.tx[3] == 0 && hardware.tx[5] == 1); // urgent model read before polling I/O.
    scheduleReply(hardware.writeStarted + 8 * 87 + 1000, REPLY);
    for (unsigned i = 0; i < 1000 && !app->owner.result(id); ++i) step();
    assert(app->owner.result(id) && app->owner.result(id)->outcome == Rtu::Outcome::SUCCESS);
    assert(app->owner.release(id));
    pollStep(1, registerReply({0, 0})); pollStep(2, registerReply({0, 0, 0})); pump(100);
    assert(hardware.writes == 4 && !app->monitorState.settings.enabled);
}
void testMonitorPressurePreservesUnreadForegroundResults() {
    fresh(); timerCapture();
    for (unsigned i = 0; i < REQUEST_CAPACITY; ++i) {
        const unsigned before = hardware.writes; const uint32_t operation = admit(i + 1);
        reply(operation, before);
    }
    const auto retained = view(1).probe;
    command("@20 monitor 100 2\n");
    assert(app->monitorState.admitted == 0 && app->monitorState.rejected == 1 && app->monitorState.remaining == 1);
    assert(!app->records[REQUEST_CAPACITY].operationId && hardware.writes == 8);
    advanceHardware(app->monitorState.nextDueUs + 1); loop(); pump(100);
    assert(app->monitorState.rejected == 2 && !app->monitorState.settings.enabled);
    Probe::Snapshot state; snapshot(app, state);
    assert(state.retained == 8 && state.reserved == 0 && hardware.writes == 8);
    assert(view(1).probe.transport.endedUs == retained.transport.endedUs && view(1).probe.rawModel == retained.rawModel);
    assert(!app->stateCache.blocks[0].valid && !app->stateCache.blocks[0].attemptKnown);
    advanceHardware(hardware.time + 500000); loop(); assert(hardware.writes == 8);
}
void testMonitorCheckedFailureHasOneAttemptAndNoFeedback() {
    fresh(); timerCapture(); command("@1 monitor 100 1\n");
    pollStep(0, {1, 0x83, 2, 0xC0, 0xF1}); pump(100);
    assert(!app->records[REQUEST_CAPACITY].operationId && !app->monitorState.settings.enabled);
    assert(app->monitorState.admitted == 1 && app->monitorState.remaining == 0);
    assert(app->stateCache.blocks[0].attemptKnown && !app->stateCache.blocks[0].lastAttemptOk);
    assert(app->stateCache.blocks[0].lastAttemptStatus.code == MotorControlRS::Err::EXCEPTION);
    assert(!app->stateCache.blocks[0].valid && !app->stateCache.blocks[1].valid && !app->stateCache.blocks[2].valid);
    assert(!app->owner.needsRecovery() && hardware.writes == 1);
    advanceHardware(hardware.time + 500000); loop(); pump(100); assert(hardware.writes == 1);
}
void driverStep(uint32_t operation, const std::vector<uint8_t>& supplied) {
    auto* record = findRecord(*app, operation); assert(record && record->driverOperation);
    const uint8_t oldStep = record->driver.step;
    const unsigned before = hardware.writes;
    if (app->runner.phase() != Rtu::Phase::DRAIN && app->runner.phase() != Rtu::Phase::HOLD && app->runner.phase() != Rtu::Phase::RECEIVE) startTx(before);
    scheduleReply(std::max(hardware.writeStarted + 8 * 87 + 1000, hardware.time + 1000), supplied);
    for (unsigned i = 0; i < 25000 && record->driver.state == ReadState::ACTIVE && record->driver.step == oldStep; ++i) step();
    assert(record->driver.state != ReadState::ACTIVE || record->driver.step != oldStep);
    if (record->driver.state != ReadState::ACTIVE) pump(100);
}
void segmentBaseline() {
    fresh(); timerCapture();
    const auto probeId = admit(); reply(probeId, 0); assert(release(app, probeId) == Probe::Action::OK);
    command("@2 read state\n"); const auto stateId = app->latestOperationId;
    readStep(stateId, 0, registerReply({0, 1})); readStep(stateId, 1, registerReply({0, 0})); readStep(stateId, 2, registerReply({0, 0, 0}));
    assert(release(app, stateId) == Probe::Action::OK);
    command("@3 profile ess_rs io read\n"); const auto ioId = app->latestOperationId;
    driverStep(ioId, registerReply({0, 1, 2, 3, 0})); driverStep(ioId, registerReply({0, 0, 0})); driverStep(ioId, registerReply({0}));
    assert(release(app, ioId) == Probe::Action::OK);
}
void testSegmentRoutesStoredSettlementAndNoImplicitTrigger() {
    segmentBaseline();
    command("@4 profile ess_rs segment position 16 read\n"); contains("\"result\":\"accepted\"");
    const auto baseline = app->latestOperationId;
    startTx(hardware.writes);
    assert(hardware.tx[3] == 0xBA && hardware.tx[5] == 5);
    driverStep(baseline, registerReply({0x1234, 0xABCD, 120, 100, 100}));
    contains("\"segment_index\":16"); contains("\"driver_group\":\"position_segment\"");
    assert(app->segmentSettings.segmentIndex == 16 && app->segmentSettings.raw[3] == 0x1234);
    assert(release(app, baseline) == Probe::Action::OK);
    const auto before = hardware.writes;
    command("@5 profile ess_rs segment position 16 set speed 60 target 0\n"); contains("unsupported"); assert(hardware.writes == before);
    command("@6 profile ess_rs segment position 16 set speed 60 acceleration 90\n"); contains("accepted");
    const auto update = app->latestOperationId; auto* record = findRecord(*app, update);
    assert(record->axisReserved && record->driver.prerequisites.allowEchoReadback);
    // Unwired passive origin/limit assignments permit stored settings, without
    // clearing them or pretending that they are disabled functions.
    assert(record->driver.prerequisites.externalTriggerInhibitedQualified);
    startTx(before); const std::vector<uint8_t> echo(hardware.tx.begin(), hardware.tx.end());
    driverStep(update, echo); assert(!record->driver.progress[0].acknowledged);
    driverStep(update, registerReply({60}));
    startTx(hardware.writes); const std::vector<uint8_t> echo2(hardware.tx.begin(), hardware.tx.end());
    driverStep(update, echo2); driverStep(update, registerReply({90}));
    assert(!view(update).pending && record->driver.outcome == ESS::DriverOutcome::SUCCESS);
    assert(!record->driver.uncertain && record->driver.progress[0].execution == MotorControlRS::ActionExecution::UNKNOWN);
    assert(record->driver.progress[0].readbackKnown && !record->axisReserved);
    assert(hardware.writes == before + 4 && !app->owner.needsRecovery());
    const auto retained = record->driver.observations[0].deliveredUs;
    command("@7 result\n"); assert(record->driver.observations[0].deliveredUs == retained);
    assert(!writeResponseConfirmed && app->ioSettings.operationId == 0);
}
void testSegmentIndexCancellationAndTriggerPolicy() {
    fresh(); timerCapture();
    ESS::DriverRequest invalidGroup; invalidGroup.group = static_cast<ESS::DriverGroup>(255); uint32_t untouched = 999;
    assert(startDriver(app,99,1,ESS::DriverKind::READ,invalidGroup,untouched) == Probe::Action::INVALID && untouched == 999);
    for (const char* invalid : {"profile ess_rs segment position 0 read\n", "profile ess_rs segment speed 17 read\n", "profile ess_rs segment start 1 set value 1.0\n", "profile ess_rs segment start 1 set value 1 value 2\n", "profile ess_rs segment speed 1 set target 1\n"}) command(invalid);
    assert(hardware.writes == 0 && app->nextOperationId == 1);
    command("@20 profile ess_rs segment speed 1 read\n"); const auto id = app->latestOperationId;
    command("@21 cancel\n"); pump(1000); assert(!view(id).pending && hardware.writes <= 1);
    segmentBaseline();
    command("@4 profile ess_rs segment start 1 read\n"); const auto baseline = app->latestOperationId;
    driverStep(baseline, registerReply({50})); assert(release(app, baseline) == Probe::Action::OK);
    const auto before = hardware.writes;
    app->inputWiring[0] = MotorControlRS::InputWiring::UNKNOWN;
    command("@5 profile ess_rs segment start 1 set value 60\n"); contains("invalid"); assert(hardware.writes == before);
    app->inputWiring[0] = MotorControlRS::InputWiring::UNCONNECTED;
    app->ioSettings.raw[7] = 0x8000;
    command("@6 profile ess_rs segment start 1 set value 60\n"); contains("invalid"); assert(hardware.writes == before);
    app->ioSettings.raw[7] = 0;
    app->stateCache.blocks[static_cast<uint8_t>(ESS::StateBlock::IO)].value.rawInputs = 1;
    command("@7 profile ess_rs segment start 1 set value 60\n"); contains("invalid"); assert(hardware.writes == before);
    app->stateCache.blocks[static_cast<uint8_t>(ESS::StateBlock::IO)].value.rawInputs = 0;
    command("@8 profile ess_rs segment start 16 set value 60\n"); contains("invalid"); assert(hardware.writes == before);
    app->ioSettings.raw[8] = 17; // Not a passive assignment; no trigger policy can be inferred.
    command("@9 profile ess_rs segment start 1 set value 60\n"); contains("invalid"); assert(hardware.writes == before);
}
void testSegmentRecoveryCancelsReadbackContinuation() {
    segmentBaseline();
    command("@4 profile ess_rs segment speed 16 read\n"); const auto baseline = app->latestOperationId;
    driverStep(baseline, registerReply({50,100,100}));
    assert(release(app,baseline) == Probe::Action::OK);
    command("@5 profile ess_rs segment speed 16 set speed 60 acceleration 90\n");
    const auto update = app->latestOperationId; startTx(hardware.writes);
    const auto writes = hardware.writes;
    command("@6 recover\n"); const auto recovery = app->recovery.operationId;
    for (unsigned i = 0; i < 80000 && (app->owner.recovering() || view(update).pending); ++i) step();
    const auto& c = *view(update).driverContext;
    assert(c.outcome == ESS::DriverOutcome::CANCELLED && c.uncertain);
    assert(c.effects == static_cast<uint32_t>(ESS::DriverField::SEGMENT_SPEED));
    assert(c.progress[0].execution == MotorControlRS::ActionExecution::UNKNOWN && !c.progress[0].readbackKnown);
    assert(c.progress[1].execution == MotorControlRS::ActionExecution::NOT_TRANSMITTED);
    assert(view(recovery).recoveryResult.outcome == Rtu::RecoveryOutcome::RECOVERED && !axisReserved(*app,1));
    pump(100); assert(hardware.writes == writes && view(update).driverContext->uncertain);
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
    std::printf("Storage bytes: App=%zu Record=%zu Console=%zu ReadContext=%zu PreparedRead=%zu Identity=%zu Config=%zu StateCache=%zu StateObservation=%zu\n",
        sizeof(App), sizeof(App::Record), sizeof(Probe::Console), sizeof(ESS::ReadContext), sizeof(ESS::PreparedRead),
        sizeof(ESS::IdentityObservation), sizeof(ESS::ConfigObservation), sizeof(Probe::StateCache), sizeof(ESS::StateObservation));
    testSegmentRoutesStoredSettlementAndNoImplicitTrigger(); testSegmentIndexCancellationAndTriggerPolicy();
    testSegmentRecoveryCancelsReadbackContinuation();
    testSuccessfulProbeAndReset(); testCheckedExceptionAndParserRejection();
    testActionGateAndSeparateAcknowledgement(); testStopSupersedesOnlyAfterAdmissionAndSettlesInflight();
    testStopUsesReservedCapacityAndFullAdmissionPreservesWork();
    testUncertainStopSurvivesReleaseRecoveryAndCanBeStoppedAgain(); testBadActionReplyKeepsCodecEvidenceAndNoReplay();
    testUnknownActionExceptionKeepsReservationAcrossReleaseAndRecovery();
    testAcceptedStopRetainsBothResultsWhenInterruptedWriteLaterFails();
    testStopKeepsKnownTargetAfterUnrelatedReadAndAction();
    for (const char* mode : {"off", "raw", "decoded"})
        testStopAndInterruptedResultSurviveBlockedConsole(mode);
    testActionEffectsInvalidateHistoricalFreshnessAndReleaseOrigin();
    testCaptureReadUsesOwnerAndPreservesModel(); testCaptureReadRejectsMalformedRepliesAndArguments();
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
    testTypedReadRoutesAndAtomicPublication(); testTypedReadPartialCancelRecoveryAndRetention();
    testTypedReadPressureAndInvalidArguments();
    testTypedReadAbsoluteBudgetAndDelayedEvidence(); testTypedReadInvalidOwnerEnvelopeCannotReplay();
    testTypedReadCancelSettledIntermediateCannotContinue();
    testTypedConfigYieldsBusAndPreservesUnknownHardwareCode();
    testStateCacheRetainsValuesAcrossFailureAndGenerationChange();
    testStateCacheLateDeliveryCannotRejuvenateObservation();
    testStateReadUnknownBitsAndPassiveQueries(); testStatePartialRefreshPreservesPreviousBlocks();
    testHostAxisPreparationUsesPublicApiWithoutTraffic();
    testAxisConfigurationRequiresFreshIdleEvidenceAndInvalidatesPreparation();
    testStateDelayedServiceAndStaleConfigurationStayExplicit(); testMonitorDisabledFiniteAndOwnResultRelease();
    testNewConfigurationSeparatesHistoricalFeedbackInterpretation();
    testMonitorCancellationSettlesTransmission(); testMonitorYieldsToUrgentOwnerAdmission();
    testMonitorPressurePreservesUnreadForegroundResults(); testMonitorCheckedFailureHasOneAttemptAndNoFeedback();
#if MOTORCONTROLRS_LOAD_FIXTURE
    testLoadLocalAdmission();
    testLoadDelayExhaustsSetupTxBudgetWithoutTransmission();
#endif
    app->~App(); std::free(app); app = nullptr;
}
