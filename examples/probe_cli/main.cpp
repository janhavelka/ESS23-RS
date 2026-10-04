// SPDX-License-Identifier: MIT
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <new>
#include <cstring>
#include <algorithm>
#include "ProbeConsole.h"
#include "StateCache.h"
#include "../common/EssRtuValidator.h"
#include "../common/Esp32S3Uart.h"
#include "../common/BuildConfig.h"
#include "../common/BoardPins.h"
#if MOTORCONTROLRS_LOAD_FIXTURE
#include "Esp32Load.h"
#endif
using namespace MotorControlRSExample;
namespace ESS = MotorControlRS::ESS_RS;
using MotorControlRS::ReadState;
using MotorControlRS::ReadEventKind;
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
        bool observed = false, delivered = false, captureRead = false;
        bool typedRead = false, monitored = false;
        bool cancelContinuation = false; ///< Cancel the operation after its current frame settles.
        ESS::ReadContext read;
    } records[REQUEST_CAPACITY + 1]; // Final slot belongs to the finite poll consumer.
    Probe::StateCache stateCache;
    Probe::MonitorSnapshot monitorState;
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
    ESS::IdentityObservation identity;
    ESS::ConfigObservation configuration;
    uint32_t bindingGeneration = 1;
    MotorControlRS::ReadTarget communicationTarget;
    uint64_t communicationEarliestUs = 0, communicationLatestUs = 0;
    bool communicationKnown = false;
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
bool terminal(const App& a, const App::Record& record) {
    return record.typedRead ? record.read.state != ReadState::ACTIVE : a.owner.result(record.requestId) != nullptr;
}
bool reading(const App& a) {
    for (const auto& record : a.records)
        if (record.operationId && record.typedRead && record.read.state == ReadState::ACTIVE) return true;
    return false;
}
void snapshot(void* context, Probe::Snapshot& s) {
    App& a = *static_cast<App*>(context);
    s = Probe::Snapshot();
    s.bindingGeneration = a.bindingGeneration;
    s.nowUs = nowUs(); s.stateCache = &a.stateCache; s.monitorState = a.monitorState;
    s.communicationKnown = a.communicationKnown;
    s.communicationTarget = a.communicationTarget;
    s.communicationEarliestUs = a.communicationEarliestUs;
    s.communicationLatestUs = a.communicationLatestUs;
    s.cachedIdentityId = a.identity.operationId; s.cachedIdentityAddress = a.identity.target.address;
    s.cachedIdentityGeneration = a.identity.target.generation;
    s.cachedConfigId = a.configuration.operationId; s.cachedConfigAddress = a.configuration.target.address;
    s.cachedConfigGeneration = a.configuration.target.generation;
    s.address = 1; s.probeAddress = a.address; s.baud = BAUD; s.responseTimeoutUs = RESPONSE_US;
    s.replyGapUs = REPLY_GAP_US; s.gap15Us = timing().gap15Us; s.gap35Us = timing().gap35Us;
    s.uptimeMs = nowUs() / 1000; s.ready = platformReady; s.timingQualified = false;
    s.busy = a.owner.active() || a.owner.pending() || a.owner.recovering() || reading(a);
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
    for (const auto& record : a.records) if (record.operationId && !record.monitored) {
        if (terminal(a, record)) ++s.retained; else ++s.reserved;
    }
    s.outputQueued = a.outputCount; s.outputBlocked = a.outputBlocked; s.outputShortWrites = a.outputShortWrites;
    s.inputBytes = a.inputBytes; s.inputLines = a.inputLines; s.stats = a.runner.stats();
    s.inputDropped = a.console.inputDropped();
    s.timerCapture = uart.stats().timer;
    for (const auto& record : a.records) if (record.operationId && !terminal(a, record)) {
        if (!s.deadlineUs || record.deadlineUs < s.deadlineUs) s.deadlineUs = record.deadlineUs;
    }
    if (a.owner.recovering() && (!s.deadlineUs || a.recovery.deadlineUs < s.deadlineUs))
        s.deadlineUs = a.recovery.deadlineUs;
    const auto capture = uart.stats(); s.maxPollGapUs = capture.maxGapUs; s.captureFaults = capture.faults; s.rxErrors = capture.rxErrors;
    s.cacheOffSupported = Esp32S3Uart::CACHE_OFF_SUPPORTED;
    s.sampleGapLimitUs = capture.timer ? capture.sampleGapLimitUs : 0;
    s.sampleGapExceeded = capture.sampleGapExceeded;
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
Probe::Action startRead(void* context, uint32_t commandId, uint8_t address, uint32_t& operationId, bool captureRead) {
    App& a = *static_cast<App*>(context);
    if (!platformReady) return Probe::Action::UNAVAILABLE;
    const uint64_t sampled = uart.sample();
    if (a.owner.needsRecovery() || uart.needsRecovery()) return Probe::Action::RECOVERY_REQUIRED;
    if (!a.nextOperationId) return Probe::Action::IDS_EXHAUSTED;
    App::Record* record = nullptr;
    for (std::size_t i = 0; i < REQUEST_CAPACITY; ++i)
        if (!a.records[i].operationId) { record = &a.records[i]; break; }
    if (!record) return Probe::Action::RESULTS_FULL;
    uint8_t bytes[8]; Rtu::BusRequest request;
    const uint16_t first = captureRead ? Probe::CAPTURE_FIRST : 0;
    const uint16_t count = captureRead ? Probe::CAPTURE_WORDS : 1;
    request.wire.bytes = bytes;
    request.wire.length = captureRead ? MotorControlRS::ESS_RS::buildReadRegisters(address, first, count, bytes, sizeof(bytes)) :
        MotorControlRS::ESS_RS::buildProbe(address, bytes, sizeof(bytes));
    request.wire.replyLength = MotorControlRS::ESS_RS::expectedReadRegistersLen(count);
    request.wire.responseTimeoutUs = RESPONSE_US;
    request.wire.replyGapUs = REPLY_GAP_US; // Bench turnaround exception; final t3.5 is still 1750 us.
    request.wire.deadlineUs = sampled + REQUEST_US;
    request.expected.address = address; request.expected.function = 3; request.expected.first = first; request.expected.count = count;
    request.expected.target = address; request.expected.targetGeneration = a.bindingGeneration;
    request.validator = Rtu::essValidator();
    if (!captureRead) request.validator.checkReply = checkProbe;
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
    record->captureRead = captureRead;
    return Probe::Action::OK;
}
Probe::Action probe(void* context, uint32_t commandId, uint8_t address, uint32_t& operationId) {
    return startRead(context, commandId, address, operationId, false);
}
Probe::Action captureRead(void* context, uint32_t commandId, uint8_t address, uint32_t& operationId) {
    return startRead(context, commandId, address, operationId, true);
}
Rtu::BusAdmission admitStep(App& a, App::Record& record, uint64_t sampled) {
    ESS::PreparedRead prepared;
    if (!ESS::nextRead(record.read, sampled, prepared)) return Rtu::BusAdmission::EXPIRED;
    Rtu::BusRequest request;
    request.wire.bytes = prepared.bytes; request.wire.length = prepared.length;
    request.wire.replyLength = ESS::expectedReadRegistersLen(prepared.count);
    request.wire.responseTimeoutUs = RESPONSE_US; request.wire.replyGapUs = REPLY_GAP_US;
    request.wire.deadlineUs = prepared.deadlineUs;
    request.expected.address = prepared.target.address; request.expected.function = 3;
    request.expected.target = prepared.target.id; request.expected.targetGeneration = prepared.target.generation;
    request.expected.first = prepared.first; request.expected.count = prepared.count;
    request.validator = Rtu::essValidator();
    const auto admitted = a.owner.admit(request, sampled, record.requestId);
    if (admitted == Rtu::BusAdmission::ACCEPTED && record.read.kind == ESS::ReadKind::STATE)
        Probe::stateAttempt(a.stateCache, record.read.target, record.read.operationId, record.read.step, sampled);
    return admitted;
}
Probe::Action startTypedRead(void* context, uint32_t commandId, uint8_t address, ESS::ReadKind kind,
                             uint32_t& operationId, bool monitored) {
    App& a = *static_cast<App*>(context);
    if (!platformReady) return Probe::Action::UNAVAILABLE;
    const uint64_t sampled = uart.sample();
    if (a.owner.needsRecovery() || uart.needsRecovery()) return Probe::Action::RECOVERY_REQUIRED;
    if (!a.nextOperationId) return Probe::Action::IDS_EXHAUSTED;
    App::Record* record = nullptr;
    if (monitored) {
        if (!a.records[REQUEST_CAPACITY].operationId) record = &a.records[REQUEST_CAPACITY];
    } else for (std::size_t i = 0; i < REQUEST_CAPACITY; ++i)
        if (!a.records[i].operationId) { record = &a.records[i]; break; }
    if (!record) return Probe::Action::RESULTS_FULL;
    MotorControlRS::ReadTarget target; target.id = address; target.address = address; target.generation = a.bindingGeneration;
    MotorControlRS::ActiveSerialTuple serial; serial.known = true; serial.baud = BAUD;
    serial.dataBits = 8; serial.parity = MotorControlRS::SerialParity::NONE; serial.stopBits = 1;
    const auto* configuration = a.configuration.operationId && Probe::sameTarget(a.configuration.target, target) ?
        &a.configuration : nullptr;
    const auto prepared = kind == ESS::ReadKind::IDENTITY ?
        ESS::prepareIdentity(record->read, target, a.nextOperationId, sampled, sampled + REQUEST_US, serial) :
        kind == ESS::ReadKind::STATE ?
        ESS::prepareState(record->read, target, a.nextOperationId, sampled, sampled + REQUEST_US, serial, configuration) :
        ESS::prepareConfig(record->read, target, a.nextOperationId, sampled, sampled + REQUEST_US, serial);
    if (!prepared || (kind != ESS::ReadKind::IDENTITY && kind != ESS::ReadKind::CONFIG && kind != ESS::ReadKind::STATE)) {
        *record = App::Record(); return Probe::Action::INVALID;
    }
    const auto admitted = admitStep(a, *record, sampled);
    if (admitted != Rtu::BusAdmission::ACCEPTED) {
        *record = App::Record();
        return admitted == Rtu::BusAdmission::QUEUE_FULL ? Probe::Action::QUEUE_FULL :
            admitted == Rtu::BusAdmission::RESULTS_FULL ? Probe::Action::RESULTS_FULL : Probe::Action::FAILED;
    }
    record->operationId = a.nextOperationId++; record->commandId = commandId; record->address = address;
    record->deadlineUs = record->read.deadlineUs; record->typedRead = true;
    record->monitored = monitored; operationId = record->operationId;
    if (!monitored) a.latestOperationId = operationId;
    return Probe::Action::OK;
}
Probe::Action typedRead(void* context, uint32_t commandId, uint8_t address, ESS::ReadKind kind, uint32_t& operationId) {
    return startTypedRead(context, commandId, address, kind, operationId, false);
}
void observeCommunication(App& a, const Rtu::Completion& result) {
    const bool checked = result.outcome == Rtu::Outcome::SUCCESS || result.outcome == Rtu::Outcome::DEVICE_REJECTED;
    if (!checked || !result.transport.closureQualified || result.transport.txAccepted != result.txLength ||
        result.transport.startedUs > result.transport.closureEarliestUs ||
        result.transport.closureEarliestUs > result.transport.closureLatestUs ||
        result.transport.closureLatestUs > nowUs() ||
        result.expected.targetGeneration != a.bindingGeneration ||
        (a.communicationKnown && result.transport.closureLatestUs < a.communicationLatestUs)) return;
    a.communicationKnown = true;
    a.communicationTarget.id = result.expected.target;
    a.communicationTarget.address = result.expected.address;
    a.communicationTarget.generation = result.expected.targetGeneration;
    a.communicationEarliestUs = result.transport.startedUs;
    a.communicationLatestUs = result.transport.closureLatestUs;
}
MotorControlRS::ReadEvent readEvent(const App::Record& record, ReadEventKind kind) {
    MotorControlRS::ReadEvent event; event.target = record.read.target;
    event.operationId = record.operationId; event.step = record.read.step; event.kind = kind;
    return event;
}
// Consume one retained transaction per operation per loop. Intermediate evidence
// is copied into the public context before its owner slot is explicitly released.
// The frontend record reserves terminal storage across the gaps between steps.
void advanceReads(App& a, uint64_t sampled) {
    for (auto& record : a.records) {
        if (!record.operationId || !record.typedRead || record.read.state != ReadState::ACTIVE) continue;
        if (record.requestId.owner) {
            const auto* result = a.owner.result(record.requestId); if (!result) continue;
            const uint8_t block = record.read.step;
            const auto kind = result->transport.reason == Rtu::Reason::FRAME ? ReadEventKind::FRAME :
                result->transport.reason == Rtu::Reason::REQUEST_DEADLINE ||
                result->outcome == Rtu::Outcome::QUEUE_EXPIRED || result->outcome == Rtu::Outcome::DISPATCH_EXPIRED ? ReadEventKind::DEADLINE :
                result->outcome == Rtu::Outcome::CANCELLED ? ReadEventKind::CANCEL :
                ReadEventKind::TRANSPORT_FAILURE;
            auto event = readEvent(record, kind);
            event.target.id = result->expected.target; event.target.address = result->expected.address;
            event.target.generation = result->expected.targetGeneration;
            event.txAccepted = result->transport.txAccepted; event.executionUnknown = result->executionUnknown;
            event.transportDetail = static_cast<int32_t>(result->transport.reason);
            event.length = result->transport.rxLength; event.frame = event.length ? result->raw : nullptr;
            if (kind == ReadEventKind::FRAME) {
                event.qualified = result->transport.closureQualified;
                if (event.qualified) {
                    event.earliestUs = result->transport.closureEarliestUs;
                    event.latestUs = result->transport.closureLatestUs;
                }
            }
            if (!ESS::advanceRead(record.read, event, sampled)) {
                // An impossible owner/event envelope is an integration failure.
                // Preserve the captured prefix and terminate; never re-admit it.
                auto failed = readEvent(record, ReadEventKind::TRANSPORT_FAILURE);
                failed.transportDetail = -1;
                failed.txAccepted = event.txAccepted; failed.executionUnknown = true;
                failed.frame = event.frame; failed.length = event.length;
                if (!ESS::advanceRead(record.read, failed, sampled)) continue;
            }
            observeCommunication(a, *result);
            if (record.read.kind == ESS::ReadKind::STATE)
                Probe::stateResult(a.stateCache, record.read, block, result->transport.startedUs);
            if (record.read.state != ReadState::ACTIVE) continue;
            a.owner.release(record.requestId); record.requestId = Rtu::RequestId();
        }
        if (record.cancelContinuation || record.read.target.generation != a.bindingGeneration) {
            ESS::advanceRead(record.read, readEvent(record, ReadEventKind::CANCEL), sampled);
        } else if (sampled >= record.deadlineUs) {
            ESS::advanceRead(record.read, readEvent(record, ReadEventKind::DEADLINE), sampled);
        } else if (!a.owner.needsRecovery() && !uart.needsRecovery()) {
            admitStep(a, record, sampled); // Queue pressure defers; no admitted frame is retried.
        }
    }
}
Probe::Action recover(void* context, uint32_t commandId, uint32_t& operationId) {
    App& a = *static_cast<App*>(context);
    if (!platformReady) return Probe::Action::UNAVAILABLE;
    if (a.recovery.operationId) return Probe::Action::RESULTS_FULL;
    if (!a.nextOperationId) return Probe::Action::IDS_EXHAUSTED;
    if (a.bindingGeneration == UINT32_MAX) return Probe::Action::IDS_EXHAUSTED;
    const uint64_t now = uart.sample(); uint64_t id = 0;
    if (a.owner.recover(now, now + 2000000, id) != Rtu::RecoveryAdmission::ACCEPTED) return Probe::Action::FAILED;
    ++a.bindingGeneration;
    a.monitorState.settings.enabled = false; a.monitorState.remaining = 0;
    for (auto& record : a.records) if (record.operationId && record.typedRead &&
        record.read.state == ReadState::ACTIVE && !record.requestId.owner)
        ESS::advanceRead(record.read, readEvent(record, ReadEventKind::CANCEL), now);
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
    const auto* record = findRecord(a, operationId); if (!record || record->monitored) return false;
    out = Probe::ResultView();
    out.commandId = record->commandId; out.operationId = operationId; out.address = record->address;
    out.captureRead = record->captureRead;
    if (record->typedRead) {
        out.typedRead = &record->read; out.pending = record->read.state == ReadState::ACTIVE;
        return true;
    }
    const auto* result = a.owner.result(record->requestId); out.pending = !result; if (!result) return true;
    Probe::ProbeResult& p = out.probe;
    p.captureRead = record->captureRead;
    p.transport = result->transport; p.outcome = result->outcome; p.executionUnknown = result->executionUnknown;
    p.cancellation = result->cancellation; p.codecChecked = p.transport.reason == Rtu::Reason::FRAME;
    p.codec = result->validation; p.rx = result->raw; p.rxLength = p.transport.rxLength;
    p.txLength = record->captureRead ? MotorControlRS::ESS_RS::buildReadRegisters(record->address,
        Probe::CAPTURE_FIRST, Probe::CAPTURE_WORDS, a.viewTx, sizeof(a.viewTx)) :
        MotorControlRS::ESS_RS::buildProbe(record->address, a.viewTx, sizeof(a.viewTx)); p.tx = a.viewTx;
    if (p.codecChecked) {
        if (record->captureRead) {
            uint16_t words[Probe::CAPTURE_WORDS]; std::size_t count = 0;
            MotorControlRS::ESS_RS::parseRegisters(p.rx, p.rxLength, record->address,
                Probe::CAPTURE_WORDS, words, Probe::CAPTURE_WORDS, count, &p.frameError);
        } else MotorControlRS::ESS_RS::parseProbe(p.rx, p.rxLength, record->address, p.rawModel, &p.frameError);
    }
    p.timingValid = p.transport.closureQualified;
    p.txEndUs = p.transport.txEndUs; p.txUncertaintyUs = p.transport.txUncertaintyUs;
    p.firstRxStartUs = p.transport.firstRxStartUs; p.maxRxUncertaintyUs = p.transport.maxRxUncertaintyUs;
    p.observedEarliestUs = p.transport.closureEarliestUs; p.observedLatestUs = p.transport.closureLatestUs;
    p.deliveredUs = record->deliveredUs; return true;
}
Probe::Action cancel(void* context, uint32_t operationId) {
    App& a = *static_cast<App*>(context); if (!operationId) operationId = a.latestOperationId;
    auto* record = findRecord(a, operationId); if (!record) return Probe::Action::INVALID;
    if (record->typedRead && record->read.state != ReadState::ACTIVE) return Probe::Action::ALREADY_TERMINAL;
    if (record->typedRead && !record->requestId.owner) {
        ESS::advanceRead(record->read, readEvent(*record, ReadEventKind::CANCEL), nowUs());
        return Probe::Action::OK;
    }
    const auto result = a.owner.cancel(record->requestId, uart.sample());
    if (record->typedRead && result != Rtu::Cancel::INVALID) {
        record->cancelContinuation = true;
        return Probe::Action::OK;
    }
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
    if (!record->delivered || (record->requestId.owner && !a.owner.release(record->requestId))) return Probe::Action::BUSY;
    *record = App::Record(); return Probe::Action::OK;
}
Probe::Action monitor(void* context, const Probe::MonitorSettings* requested, Probe::MonitorSnapshot& out) {
    App& a = *static_cast<App*>(context);
    if (!platformReady) return Probe::Action::UNAVAILABLE;
    if (requested) {
        if (requested->enabled) {
            if (requested->intervalMs < 100 || requested->intervalMs > 60000 || !requested->count || requested->count > 1000)
                return Probe::Action::INVALID;
            if (a.monitorState.settings.enabled || a.records[REQUEST_CAPACITY].operationId) return Probe::Action::BUSY;
            if (a.owner.needsRecovery() || uart.needsRecovery()) return Probe::Action::RECOVERY_REQUIRED;
            a.monitorState = Probe::MonitorSnapshot(); a.monitorState.settings = *requested;
            a.monitorState.remaining = requested->count; a.monitorState.nextDueUs = nowUs();
        } else {
            a.monitorState.settings.enabled = false; a.monitorState.remaining = 0;
            auto& record = a.records[REQUEST_CAPACITY];
            if (record.operationId && record.read.state == ReadState::ACTIVE && !record.cancelContinuation) {
                cancel(&a, record.operationId); ++a.monitorState.cancelled;
            }
        }
    }
    out = a.monitorState; return Probe::Action::OK;
}
/** One opt-in finite consumer. Its ordinary admissions use the same owner queue;
 * urgent work retains its reserved priority. No catch-up burst or automatic
 * recovery occurs. Each rejected admission consumes one finite attempt. */
void serviceMonitor(App& a, uint64_t sampled) {
    if (!a.monitorState.settings.enabled || a.records[REQUEST_CAPACITY].operationId ||
        sampled < a.monitorState.nextDueUs) return;
    if (!a.monitorState.remaining || a.owner.needsRecovery() || uart.needsRecovery()) {
        a.monitorState.settings.enabled = false; return;
    }
    --a.monitorState.remaining;
    a.monitorState.nextDueUs = sampled + uint64_t(a.monitorState.settings.intervalMs) * 1000;
    uint32_t operation = 0;
    const auto result = startTypedRead(&a, 0, 1, ESS::ReadKind::STATE, operation, true);
    if (result == Probe::Action::OK) {
        ++a.monitorState.admitted; a.monitorState.operationId = operation;
    } else {
        ++a.monitorState.rejected;
        if (!a.monitorState.remaining) a.monitorState.settings.enabled = false;
    }
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
    if (requested && (a.owner.active() || a.owner.pending() || a.owner.recovering() || reading(a) || a.runner.transmitEnabled())) return Probe::Action::BUSY;
    const auto result = loadFixture.configure(requested, out, uart); out.ready = platformReady;
    if (requested) nextServiceUs = 0;
    return result;
}
#endif
Probe::Host host(App* a) {
    Probe::Host h; h.context = a; h.emitLine = emit; h.snapshot = snapshot;
    h.startProbe = probe; h.startCaptureRead = captureRead; h.recover = recover; h.resetStats = reset;
    h.startTypedRead = typedRead;
    h.monitor = monitor;
    h.result = lookup; h.cancel = cancel; h.release = release;
#if MOTORCONTROLRS_LOAD_FIXTURE
    h.load = load;
#endif
    return h;
}
void deliver(App& a) {
    for (auto& record : a.records) {
        if (!record.operationId || record.delivered) continue;
        if (record.monitored) {
            if (!terminal(a, record)) continue;
            // This explicit consumer already harvested each non-consuming block.
            // It owns release and never uses a user command correlation/retention.
            if (record.requestId.owner && !a.owner.release(record.requestId)) continue;
            record = App::Record(); a.monitorState.operationId = 0;
            if (!a.monitorState.remaining) a.monitorState.settings.enabled = false;
            continue;
        }
        Probe::ResultView view; if (!lookup(&a, record.operationId, view) || view.pending) continue;
        if (record.typedRead) {
            if (!record.observed) {
                record.observed = true;
                if (record.read.kind == ESS::ReadKind::IDENTITY && record.operationId > a.identity.operationId)
                    ESS::getIdentity(record.read, a.identity);
                if (record.read.kind == ESS::ReadKind::CONFIG && record.operationId > a.configuration.operationId)
                    ESS::getConfig(record.read, a.configuration);
            }
            if (!a.console.reportRead(record.commandId, record.operationId, record.read)) continue;
            record.delivered = true; record.deliveredUs = nowUs(); continue;
        }
        if (!record.observed && !record.captureRead) {
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
        if (const auto* completion = a.owner.result(record.requestId)) observeCommunication(a, *completion);
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
    advanceReads(a, nowUs());
    a.console.serviceOutput(); deliver(a);
    serviceMonitor(a, nowUs());
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
