// SPDX-License-Identifier: MIT
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <new>
#include <cstring>
#include <algorithm>
#include "ProbeConsole.h"
#include "../common/EssRtuValidator.h"
#include "../common/Esp32S3Uart.h"
#include "../common/BuildConfig.h"
#include "../common/BoardPins.h"
#if MOTORCONTROLRS_LOAD_FIXTURE
#include "Esp32Load.h"
#endif
using namespace MotorControlRSExample;
namespace {
constexpr uint32_t BAUD = 115200, REPLY_GAP_US = 304;
constexpr uint32_t RESPONSE_US = 200000, REQUEST_US = 500000, RECOVER_US = 500000;
constexpr std::size_t REQUEST_CAPACITY = 8, OUTPUT_LINES = 8;
Esp32S3Uart uart; // Internal driver/ISR state; App storage is PSRAM.
#if MOTORCONTROLRS_LOAD_FIXTURE
Esp32Load loadFixture;
bool fixtureReady = false;
uint64_t nextServiceUs = 0;
#endif
bool platformReady = false;
uint64_t nowUs() { return static_cast<uint64_t>(esp_timer_get_time()); }
Rtu::Storage storage(uint8_t* tx, uint8_t* rx, Rtu::Trace* trace) {
    Rtu::Storage s; s.tx = tx; s.txCapacity = 32; s.rx = rx; s.rxCapacity = 64;
    s.trace = trace; s.traceCapacity = 128; return s;
}
Rtu::Timing timing() {
    Rtu::Timing t; Rtu::setRtuTiming(BAUD, 10, t);
    t.setupUs = 20; t.holdUs = 20;
    t.busTimeoutUs = 100000; t.txTimeoutUs = 20000; t.captureTimeoutUs = 10000;
    return t;
}
Rtu::BusStorage busStorage(Rtu::PendingSlot* p, Rtu::ResultSlot* r, Rtu::ProducerSlot* producers) {
    Rtu::BusStorage s; s.pending = p; s.pendingCapacity = 5;
    s.results = r; s.resultCapacity = 9; s.producers = producers; s.producerCapacity = 1;
    s.urgentPendingCapacity = s.urgentResultCapacity = 1; return s;
}
struct App;
Probe::Host host(App*);
struct App {
    uint8_t tx[32] = {}, rx[64] = {}, viewTx[8] = {};
    Rtu::Trace trace[128];
    Rtu::PendingSlot pending[5];
    Rtu::ResultSlot results[9];
    Rtu::ProducerSlot producers[1];
    Rtu::Runner runner;
    Rtu::BusOwner owner;
    Probe::Console console;
    struct Record {
        uint32_t operationId = 0, commandId = 0;
        uint8_t address = 0;
        Rtu::RequestId requestId;
        uint64_t deliveredUs = 0;
        uint64_t deadlineUs = 0;
        bool observed = false, delivered = false;
    } records[REQUEST_CAPACITY];
    struct Recovery {
        uint32_t operationId = 0, commandId = 0;
        uint64_t id = 0, deadlineUs = 0;
        bool delivered = false, prepared = false;
    } recovery;
    struct Line { char text[Probe::OUTPUT_CAPACITY + 1]; std::size_t size = 0, offset = 0; } output[OUTPUT_LINES];
    std::size_t outputHead = 0, outputCount = 0;
    uint32_t nextOperationId = 1, latestOperationId = 0, cacheOperationId = 0, modelOperationId = 0;
    uint64_t outputBlocked = 0, outputShortWrites = 0, inputBytes = 0, inputLines = 0;
    uint64_t observedEarliestUs = 0, observedLatestUs = 0, deliveredUs = 0, recoveryGuardUntilUs = 0;
    uint8_t address = 1, modelAddress = 0;
    uint16_t model = 0;
    bool known = false, ok = false, modelKnown = false, codecChecked = false;
    MotorControlRS::Status codec;
    MotorControlRS::ESS_RS::FrameError frameError = MotorControlRS::ESS_RS::FrameError::NONE;
    App() : runner(uart.port(), storage(tx, rx, trace), timing()),
        owner(runner, busStorage(pending, results, producers)), console(host(this)) {}
};
App* app = nullptr;
bool emit(void* context, const char* text, std::size_t size) {
    App& a = *static_cast<App*>(context);
    if (size > Probe::OUTPUT_CAPACITY || a.outputCount == OUTPUT_LINES) { ++a.outputBlocked; return false; }
    App::Line& line = a.output[(a.outputHead + a.outputCount) % OUTPUT_LINES];
    std::memcpy(line.text, text, size); line.text[size] = '\n'; line.size = size + 1; line.offset = 0;
    ++a.outputCount; return true;
}
void drainOutput(App& a) {
    if (!a.outputCount) return;
    const int available = Serial.availableForWrite();
    if (available <= 0) { ++a.outputBlocked; return; }
    App::Line& line = a.output[a.outputHead];
    const std::size_t count = std::min(std::size_t(64), std::min(line.size - line.offset, static_cast<std::size_t>(available)));
    const std::size_t written = Serial.write(reinterpret_cast<const uint8_t*>(line.text + line.offset), count);
    if (written < count) ++a.outputShortWrites;
    line.offset += written;
    if (line.offset == line.size) { a.outputHead = (a.outputHead + 1) % OUTPUT_LINES; --a.outputCount; }
}
App::Record* findRecord(App& a, uint32_t operation) {
    for (auto& record : a.records) if (record.operationId && record.operationId == operation) return &record;
    return nullptr;
}
void snapshot(void* context, Probe::Snapshot& s) {
    App& a = *static_cast<App*>(context);
    s = Probe::Snapshot();
    s.address = 1; s.probeAddress = a.address; s.baud = BAUD; s.responseTimeoutUs = RESPONSE_US;
    s.replyGapUs = REPLY_GAP_US; s.gap15Us = timing().gap15Us; s.gap35Us = timing().gap35Us;
    s.uptimeMs = nowUs() / 1000; s.ready = platformReady; s.timingQualified = false;
    s.busy = a.owner.active() || a.owner.pending() || a.owner.recovering();
    s.recoveryRequired = a.owner.needsRecovery() || uart.needsRecovery();
    s.phase = a.runner.phase(); s.transport = a.runner.result().reason; s.transmitEnabled = a.runner.transmitEnabled();
    s.codecChecked = a.codecChecked; s.codec = a.codec; s.frameError = a.frameError;
    s.probeKnown = a.known; s.probeOk = a.ok; s.rawModel = a.model;
    s.modelKnown = a.modelKnown; s.modelAddress = a.modelAddress;
    s.modelOperationId = a.modelKnown ? a.modelOperationId : 0;
    s.ageMs = a.modelKnown && a.observedEarliestUs ? (nowUs() - a.observedEarliestUs) / 1000 : 0;
    s.observedEarliestUs = a.observedEarliestUs; s.observedLatestUs = a.observedLatestUs;
    s.deliveredUs = a.deliveredUs; s.recoveryGuardUntilUs = a.recoveryGuardUntilUs;
    s.operationId = a.latestOperationId; s.pending = a.owner.pending(); s.pendingCapacity = 4; s.resultCapacity = REQUEST_CAPACITY;
    for (const auto& record : a.records) if (record.operationId) {
        if (a.owner.result(record.requestId)) ++s.retained; else ++s.reserved;
    }
    s.outputQueued = a.outputCount; s.outputBlocked = a.outputBlocked; s.outputShortWrites = a.outputShortWrites;
    s.inputBytes = a.inputBytes; s.inputLines = a.inputLines; s.stats = a.runner.stats();
    s.inputDropped = a.console.inputDropped();
    s.timerCapture = uart.stats().timer;
    for (const auto& record : a.records) if (record.operationId && !a.owner.result(record.requestId)) {
        if (!s.deadlineUs || record.deadlineUs < s.deadlineUs) s.deadlineUs = record.deadlineUs;
    }
    if (a.owner.recovering() && (!s.deadlineUs || a.recovery.deadlineUs < s.deadlineUs))
        s.deadlineUs = a.recovery.deadlineUs;
    const auto capture = uart.stats(); s.maxPollGapUs = capture.maxGapUs; s.captureFaults = capture.faults; s.rxErrors = capture.rxErrors;
    s.memoryValid = true;
    s.internalFree = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    s.internalMin = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    s.internalLargest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    s.psramFree = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s.psramMin = heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s.psramLargest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s.stackFreeBytes = uxTaskGetStackHighWaterMark(nullptr);
}
MotorControlRS::Status checkProbe(const Rtu::Expectation& e, const uint8_t* bytes, std::size_t size) {
    uint16_t model = 0; return MotorControlRS::ESS_RS::parseProbe(bytes, size, e.address, model);
}
Probe::Action probe(void* context, uint32_t commandId, uint8_t address, uint32_t& operationId) {
    App& a = *static_cast<App*>(context);
    if (!platformReady) return Probe::Action::UNAVAILABLE;
    const uint64_t sampled = uart.sample();
    if (a.owner.needsRecovery() || uart.needsRecovery()) return Probe::Action::RECOVERY_REQUIRED;
    if (!a.nextOperationId) return Probe::Action::IDS_EXHAUSTED;
    App::Record* record = nullptr;
    for (auto& slot : a.records) if (!slot.operationId) { record = &slot; break; }
    if (!record) return Probe::Action::RESULTS_FULL;
    uint8_t bytes[8]; Rtu::BusRequest request;
    request.wire.bytes = bytes; request.wire.length = MotorControlRS::ESS_RS::buildProbe(address, bytes, sizeof(bytes));
    request.wire.replyLength = 7; request.wire.responseTimeoutUs = RESPONSE_US;
    request.wire.replyGapUs = REPLY_GAP_US; // Bench turnaround exception; final t3.5 is still 1750 us.
    request.wire.deadlineUs = sampled + REQUEST_US;
    request.expected.address = address; request.expected.function = 3; request.expected.count = 1;
    request.expected.target = address; request.expected.targetGeneration = 1;
    request.validator = Rtu::essValidator(); request.validator.checkReply = checkProbe;
    Rtu::RequestId id;
    switch (a.owner.admit(request, sampled, id)) {
    case Rtu::BusAdmission::ACCEPTED: break;
    case Rtu::BusAdmission::QUEUE_FULL: return Probe::Action::QUEUE_FULL;
    case Rtu::BusAdmission::RESULTS_FULL: return Probe::Action::RESULTS_FULL;
    case Rtu::BusAdmission::IDS_EXHAUSTED: return Probe::Action::IDS_EXHAUSTED;
    default: return Probe::Action::FAILED;
    }
    record->operationId = a.nextOperationId++; record->commandId = commandId; record->address = address;
    record->requestId = id; operationId = a.latestOperationId = record->operationId;
    record->deadlineUs = request.wire.deadlineUs;
    return Probe::Action::OK;
}
Probe::Action recover(void* context, uint32_t commandId, uint32_t& operationId) {
    App& a = *static_cast<App*>(context);
    if (!platformReady) return Probe::Action::UNAVAILABLE;
    if (a.recovery.operationId) return Probe::Action::RESULTS_FULL;
    if (!a.nextOperationId) return Probe::Action::IDS_EXHAUSTED;
    const uint64_t now = uart.sample(); uint64_t id = 0;
    if (a.owner.recover(now, now + 2000000, id) != Rtu::RecoveryAdmission::ACCEPTED) return Probe::Action::FAILED;
    a.recovery.id = id; a.recovery.commandId = commandId; a.recovery.operationId = a.nextOperationId++;
    a.recovery.deadlineUs = now + 2000000;
    a.recoveryGuardUntilUs = std::max(a.recoveryGuardUntilUs, now + RECOVER_US);
    operationId = a.latestOperationId = a.recovery.operationId; return Probe::Action::OK;
}
bool lookup(void* context, uint32_t operationId, Probe::ResultView& out) {
    App& a = *static_cast<App*>(context); if (!operationId) operationId = a.latestOperationId;
    if (operationId && operationId == a.recovery.operationId) {
        out = Probe::ResultView();
        out.operationId = operationId; out.commandId = a.recovery.commandId; out.recovery = true;
        const auto* result = a.owner.recoveryResult(a.recovery.id);
        out.pending = !result; if (result) out.recoveryResult = *result; return true;
    }
    const auto* record = findRecord(a, operationId); if (!record) return false;
    out = Probe::ResultView();
    out.commandId = record->commandId; out.operationId = operationId; out.address = record->address;
    const auto* result = a.owner.result(record->requestId); out.pending = !result; if (!result) return true;
    Probe::ProbeResult& p = out.probe;
    p.transport = result->transport; p.outcome = result->outcome; p.executionUnknown = result->executionUnknown;
    p.cancellation = result->cancellation; p.codecChecked = p.transport.reason == Rtu::Reason::FRAME;
    p.codec = result->validation; p.rx = result->raw; p.rxLength = p.transport.rxLength;
    p.txLength = MotorControlRS::ESS_RS::buildProbe(record->address, a.viewTx, sizeof(a.viewTx)); p.tx = a.viewTx;
    if (p.codecChecked) MotorControlRS::ESS_RS::parseProbe(p.rx, p.rxLength, record->address, p.rawModel, &p.frameError);
    p.timingValid = p.transport.closureQualified;
    p.txEndUs = p.transport.txEndUs; p.txUncertaintyUs = p.transport.txUncertaintyUs;
    p.firstRxStartUs = p.transport.firstRxStartUs; p.maxRxUncertaintyUs = p.transport.maxRxUncertaintyUs;
    p.observedEarliestUs = p.transport.closureEarliestUs; p.observedLatestUs = p.transport.closureLatestUs;
    p.deliveredUs = record->deliveredUs; return true;
}
Probe::Action cancel(void* context, uint32_t operationId) {
    App& a = *static_cast<App*>(context); if (!operationId) operationId = a.latestOperationId;
    auto* record = findRecord(a, operationId); if (!record) return Probe::Action::INVALID;
    const auto result = a.owner.cancel(record->requestId, uart.sample());
    return result == Rtu::Cancel::CANCELLED ? Probe::Action::OK :
        result == Rtu::Cancel::ALREADY_TERMINAL ? Probe::Action::ALREADY_TERMINAL : Probe::Action::INVALID;
}
Probe::Action release(void* context, uint32_t operationId) {
    App& a = *static_cast<App*>(context);
    if (operationId && operationId == a.recovery.operationId) {
        if (!a.recovery.delivered || !a.owner.releaseRecovery(a.recovery.id)) return Probe::Action::BUSY;
        a.recovery = App::Recovery(); return Probe::Action::OK;
    }
    auto* record = findRecord(a, operationId); if (!record) return Probe::Action::INVALID;
    if (!record->delivered || !a.owner.release(record->requestId)) return Probe::Action::BUSY;
    *record = App::Record(); return Probe::Action::OK;
}
void reset(void* context) {
    static_cast<App*>(context)->runner.clearStats();
#if MOTORCONTROLRS_LOAD_FIXTURE
    loadFixture.resetStats(uart);
#else
    uart.resetStats();
#endif
}
#if MOTORCONTROLRS_LOAD_FIXTURE
Probe::Action load(void* context, const Probe::LoadSettings* requested, Probe::LoadSnapshot& out) {
    App& a = *static_cast<App*>(context);
    if (!fixtureReady) return Probe::Action::UNAVAILABLE;
    if (requested && (a.owner.active() || a.owner.pending() || a.owner.recovering() || a.runner.transmitEnabled())) return Probe::Action::BUSY;
    const auto result = loadFixture.configure(requested, out, uart); out.ready = platformReady;
    if (requested) nextServiceUs = 0;
    return result;
}
#endif
Probe::Host host(App* a) {
    Probe::Host h; h.context = a; h.emitLine = emit; h.snapshot = snapshot;
    h.startProbe = probe; h.recover = recover; h.resetStats = reset;
    h.result = lookup; h.cancel = cancel; h.release = release;
#if MOTORCONTROLRS_LOAD_FIXTURE
    h.load = load;
#endif
    return h;
}
void deliver(App& a) {
    for (auto& record : a.records) {
        if (!record.operationId || record.delivered) continue;
        Probe::ResultView view; if (!lookup(&a, record.operationId, view) || view.pending) continue;
        if (!record.observed) {
            record.observed = true;
            if (view.probe.transport.txAccepted && record.operationId > a.cacheOperationId) {
                a.cacheOperationId = record.operationId; a.known = true; a.address = record.address;
                a.codecChecked = view.probe.codecChecked; a.codec = view.probe.codec; a.frameError = view.probe.frameError;
                a.ok = view.probe.outcome == Rtu::Outcome::SUCCESS;
            }
            if (view.probe.outcome == Rtu::Outcome::SUCCESS && record.operationId > a.modelOperationId) {
                a.modelKnown = true; a.model = view.probe.rawModel; a.modelAddress = record.address;
                a.modelOperationId = record.operationId; a.deliveredUs = record.deliveredUs;
                a.observedEarliestUs = view.probe.timingValid ? view.probe.observedEarliestUs : 0;
                a.observedLatestUs = view.probe.timingValid ? view.probe.observedLatestUs : 0;
            }
        }
        view.probe.deliveredUs = nowUs();
        // Output pressure must not prevent harvesting later completed observations.
        if (!a.console.reportProbe(record.commandId, record.address, record.operationId, view.probe)) continue;
        record.delivered = true; record.deliveredUs = view.probe.deliveredUs;
        if (a.modelKnown && record.operationId == a.modelOperationId) a.deliveredUs = record.deliveredUs;
    }
    if (a.recovery.operationId && !a.recovery.delivered) {
        const auto* result = a.owner.recoveryResult(a.recovery.id);
        if (result && a.console.reportRecovery(a.recovery.commandId, a.recovery.operationId, *result)) {
            a.recovery.delivered = true;
        }
    }
}
}
void setup() {
    const bool consoleReady = Serial.setTxBufferSize(1024) == 1024;
    Serial.begin(kConsoleBaud); Serial.setTxTimeoutMs(0);
    void* memory = heap_caps_malloc(sizeof(App), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!memory) { Serial.println("{\"type\":\"boot\",\"ok\":false,\"error\":\"psram_allocation\"}"); return; }
    platformReady = consoleReady && uart.begin({Board::kRs485TxPin, Board::kRs485RxPin,
        Board::kRs485DeRePin, Board::kRs485DeReActiveHigh}, BAUD);
#if MOTORCONTROLRS_TIMER_CAPTURE
    platformReady = platformReady && uart.startCapture(20, timing().holdUs);
#endif
#if MOTORCONTROLRS_LOAD_FIXTURE
    fixtureReady = loadFixture.begin(); platformReady = platformReady && fixtureReady;
#endif
    app = new (memory) App;
}
void loop() {
    if (!app) { delay(10); return; }
    App& a = *app; bool serviceDue = true;
#if MOTORCONTROLRS_LOAD_FIXTURE
    serviceDue = !a.owner.active() || nowUs() >= nextServiceUs;
#endif
    if (serviceDue) {
        uint64_t sampled = uart.sample(); bool recoveryReady = !a.owner.recovering();
        if (a.owner.recovering() && sampled < a.recovery.deadlineUs && !a.runner.busy() &&
            !a.runner.transmitEnabled() && sampled >= a.recoveryGuardUntilUs) {
            if (!a.recovery.prepared) a.recovery.prepared = uart.clear();
            recoveryReady = a.recovery.prepared; sampled = uart.sample();
        }
        const bool hadDE = a.runner.transmitEnabled();
#if MOTORCONTROLRS_LOAD_FIXTURE
        loadFixture.serviced(sampled, a.owner.active());
#endif
        a.owner.service(sampled, recoveryReady);
        if (hadDE && !a.runner.transmitEnabled()) a.recoveryGuardUntilUs = nowUs() + RECOVER_US;
#if MOTORCONTROLRS_LOAD_FIXTURE
        nextServiceUs = a.owner.active() ? sampled + loadFixture.ownerDelayUs() : 0;
#endif
    }
    const auto* recovered = a.owner.recoveryResult(a.recovery.id);
    if (recovered && recovered->outcome == Rtu::RecoveryOutcome::RECOVERED && a.recovery.operationId > a.cacheOperationId) {
        a.known = a.ok = a.modelKnown = false; a.cacheOperationId = a.recovery.operationId;
        // Both harvest watermarks exclude old results after recovery.
        a.modelOperationId = a.recovery.operationId;
        a.observedEarliestUs = a.observedLatestUs = a.deliveredUs = 0;
    }
    a.console.serviceOutput(); deliver(a);
    for (unsigned i = 0; i < 32 && Serial.available(); ++i) {
        const char c = static_cast<char>(Serial.read()); ++a.inputBytes;
        if (c == '\n') ++a.inputLines;
        a.console.feed(c);
    }
#if MOTORCONTROLRS_LOAD_FIXTURE
    if (a.outputCount < OUTPUT_LINES - 2 && !a.console.outputPending()) {
        char text[265]; std::size_t size = 0;
        if (loadFixture.takeLine(text, sizeof(text), size)) emit(&a, text, size);
    }
#endif
    drainOutput(a);
    if (!a.owner.active() || !serviceDue) delay(1);
}
