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
void readStep(uint32_t operation, uint8_t index, const std::vector<uint8_t>& bytes) {
    const unsigned writes = hardware.writes;
    if (app->runner.phase() != Rtu::Phase::DRAIN) startTx(writes);
    scheduleReply(std::max(hardware.writeStarted + 8 * 87 + 1000, hardware.time + 1000), bytes);
    for (unsigned i = 0; i < 25000 && view(operation).typedRead->state == ReadState::ACTIVE &&
        view(operation).typedRead->step == index; ++i) step();
    assert(view(operation).typedRead->step != index || !view(operation).pending);
}
void completeConfig(uint32_t operation) {
    readStep(operation, 0, registerReply({1, 1000}));
    readStep(operation, 1, registerReply({0, 0, 0}));
    readStep(operation, 2, registerReply({1, 0, 1}));
    readStep(operation, 3, registerReply({0x8005, 0, 1, 6, 17}));
    readStep(operation, 4, registerReply({2, 4000})); pump(100);
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
    assert(app->configuration.wiring[0] == MotorControlRS::InputWiring::UNKNOWN && !app->configuration.inputLevelKnown[0]);
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
    urgent.wire.replyLength = REPLY.size(); urgent.wire.responseTimeoutUs = RESPONSE_US;
    urgent.wire.replyGapUs = REPLY_GAP_US; urgent.wire.deadlineUs = nowUs() + REQUEST_US;
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
    testSuccessfulProbeAndReset(); testCheckedExceptionAndParserRejection();
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
    testStateDelayedServiceAndStaleConfigurationStayExplicit(); testMonitorDisabledFiniteAndOwnResultRelease();
    testMonitorCancellationSettlesTransmission(); testMonitorYieldsToUrgentOwnerAdmission();
    testMonitorPressurePreservesUnreadForegroundResults(); testMonitorCheckedFailureHasOneAttemptAndNoFeedback();
#if MOTORCONTROLRS_LOAD_FIXTURE
    testLoadLocalAdmission();
    testLoadDelayExhaustsSetupTxBudgetWithoutTransmission();
#endif
    app->~App(); std::free(app); app = nullptr;
}
