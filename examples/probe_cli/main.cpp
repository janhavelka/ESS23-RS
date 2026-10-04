// SPDX-License-Identifier: MIT
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <new>
#include <cstring>
#include <algorithm>
#include "ProbeConsole.h"
#include "StateCache.h"
#include <MotorControlRS/profiles/ess_rs/Actions.h>
#include <MotorControlRS/profiles/ess_rs/Position.h>
#include <MotorControlRS/profiles/ess_rs/Velocity.h>
#include <MotorControlRS/profiles/ess_rs/DriverSettings.h>
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
// Physical TX/RX/DE and local-echo exclusion remain unqualified on this bench.
bool actionTimingQualified = false;
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
        bool actionOperation = false, axisReserved = false, effectsInvalidated = false, interruptedByStop = false;
        ESS::ActionContext action;
        bool moveOperation = false;
        ESS::MoveContext move;
        bool velocityOperation = false;
        ESS::VelocityContext velocity;
        bool driverOperation = false;
        uint32_t configurationGeneration = 0;
        uint16_t driverEffects = 0;
        ESS::DriverContext driver;
    } records[REQUEST_CAPACITY + 2]; // Dedicated monitor and urgent-stop frontend reservations.
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
    ESS::DriverObservation driverSettings;
    ESS::DriverObservation driverObserved; // Scratch for whole-read publication/effects comparison.
    ESS::DriverPrerequisites driverPrerequisites; // PSRAM scratch; no large stack copies.
    bool driverInputsQualified = false; // Explicit commissioning evidence, no console bypass.
    MotorControlRS::AxisConfig axis;
    bool commandPolarityKnown = true; // Host declaration; invalidated by possible device-direction changes.
    MotorControlRS::AxisReference coordinateReference; // Qualified command-coordinate evidence; never inferred from an unsigned/raw zero.
    bool positionClearQualified = false; // Supplied commissioning semantics, independent of electrical qualification.
    ESS::MovePrerequisites movePrerequisites; // Supplied commissioning evidence; false until verified.
    ESS::VelocityPrerequisites velocityPrerequisites; // Independent commissioning evidence, initially unqualified.
    uint32_t bindingGeneration = 1;
    uint8_t actionConflicts[32] = {}; ///< Physical address conflicts survive result release and host recovery.
    uint8_t knownTargets[32] = {}; ///< Checked physical addresses, independent of the latest passive observation.
    MotorControlRS::ReadTarget communicationTarget;
    uint64_t communicationEarliestUs = 0, communicationLatestUs = 0;
    bool communicationKnown = false;
    MotorControlRS::ESS_RS::FrameError frameError = MotorControlRS::ESS_RS::FrameError::NONE;
    App() : runner(uart.port(), storage(tx, rx, trace), timing()),
        owner(runner, busStorage(pending, results, producers)), console(host(this)) {
        axis.target.id = axis.target.address = 1; axis.target.generation = bindingGeneration;
    }
};
App* app = nullptr;
void clearRecord(App::Record& record) {
    // Construct in its caller-owned PSRAM slot. A value-assignment temporary
    // would put the complete retained read/action/move contexts on the stack.
    record.~Record();
    new (&record) App::Record();
}
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
    if (record.driverOperation) return record.driver.state != ReadState::ACTIVE;
    if (record.velocityOperation) return record.velocity.state != MotorControlRS::ActionState::ACTIVE;
    if (record.moveOperation) return record.move.state != MotorControlRS::ActionState::ACTIVE;
    if (record.actionOperation) return record.action.state != MotorControlRS::ActionState::ACTIVE;
    return record.typedRead ? record.read.state != ReadState::ACTIVE : a.owner.result(record.requestId) != nullptr;
}
bool axisReserved(const App& a, uint8_t address = 0) {
    // Recovery changes correlation generation, not the physical target's uncertainty.
    if (address) {
        if (a.actionConflicts[address / 8] & (1U << (address % 8))) return true;
    } else for (const auto value : a.actionConflicts) if (value) return true;
    for (const auto& record : a.records)
        if (record.axisReserved && (!address || record.address == address)) return true;
    return false;
}
bool acting(const App& a) {
    for (const auto& record : a.records)
        if ((record.actionOperation || record.moveOperation || record.velocityOperation || record.driverOperation) && !terminal(a, record)) return true;
    return false;
}
bool reading(const App& a) {
    for (const auto& record : a.records)
        if (record.operationId && record.typedRead && record.read.state == ReadState::ACTIVE) return true;
    return false;
}
// Raw observations keep their original transport generation. Only derived host
// coordinates are invalidated; exhaustion disables preparation instead of wrap.
void invalidateAxis(App& a, const App::Record* changing = nullptr) {
    if (!MotorControlRS::invalidateAxisReference(a.axis, a.coordinateReference)) a.axis.generation = 0;
    a.axis.target.generation = a.bindingGeneration;
    for (auto& record : a.records) if (&record != changing &&
        (record.moveOperation || record.velocityOperation ||
         (record.driverOperation && record.driver.kind == ESS::DriverKind::UPDATE) ||
         (record.typedRead && record.read.kind == ESS::ReadKind::STATE)) && !terminal(a, record)) {
        record.cancelContinuation = true;
        if (record.requestId.owner) a.owner.cancelUnsent(record.requestId, nowUs());
    }
}
bool coordinateKnowledge(const App& a) {
    return a.axis.originKnown || a.axis.encoderOriginKnown || a.axis.softLimitsKnown || a.coordinateReference.nativeKnown;
}
bool triggeredMotion(const App& a, uint8_t address) {
    for (const auto& record : a.records) {
        if (record.address != address || terminal(a, record)) continue;
        if (record.velocityOperation && (record.velocity.triggerEvidence.txAccepted ||
            (record.velocity.phase == ESS::VelocityPhase::TRIGGER && record.requestId.owner && a.owner.txAccepted(record.requestId)))) return true;
        if (!record.moveOperation) continue;
        if (record.move.triggerEvidence.txAccepted ||
            (record.move.step == 1 && record.requestId.owner && a.owner.txAccepted(record.requestId))) return true;
    }
    return false;
}
void serviceCoordinates(App& a, uint64_t now) {
    if (!coordinateKnowledge(a)) return;
    const auto& reference = a.coordinateReference;
    if (reference.nativeKnown && (!reference.maximumAgeUs || now < reference.observedUs ||
        now - reference.observedUs > reference.maximumAgeUs)) {
        invalidateAxis(a); return;
    }
    const auto& motion = a.stateCache.blocks[static_cast<uint8_t>(ESS::StateBlock::MOTION)];
    if (Probe::fresh(motion, a.axis.target, now, 5000000) &&
        (motion.value.released || (motion.value.running && !triggeredMotion(a, a.axis.target.address))))
        invalidateAxis(a);
}
MotorControlRS::AxisReference axisReference(const App& a) {
    using namespace MotorControlRS;
    AxisReference evidence = a.coordinateReference;
    if (!Probe::sameTarget(evidence.target, a.axis.target) ||
        evidence.configurationGeneration != a.axis.generation) evidence = AxisReference();
    evidence.target = a.axis.target; evidence.configurationGeneration = a.axis.generation;
    evidence.nowUs = nowUs();
    if (!evidence.nativeKnown) evidence.maximumAgeUs = 5000000;
    evidence.idle = !a.owner.active() && !a.owner.pending() && !a.owner.recovering() &&
        !a.owner.needsRecovery() && !uart.needsRecovery() && !reading(a) && !a.monitorState.settings.enabled &&
        !axisReserved(a, a.axis.target.address);
    const auto& motion = a.stateCache.blocks[static_cast<uint8_t>(ESS::StateBlock::MOTION)];
    evidence.stationary = false;
    if (Probe::fresh(motion, a.axis.target, evidence.nowUs, evidence.maximumAgeUs)) {
        if (!evidence.nativeKnown) {
            evidence.observedUs = motion.observedEarliestUs;
            evidence.source = ScaleSource::READBACK;
        }
        evidence.stationary = !motion.value.running && !motion.value.released && !motion.value.alarmFlag && motion.value.rawAlarm == 0;
    }
    // ESS pair signedness and its exact command relation remain unresolved.
    // A zero raw position is insufficient evidence for nativeKnown or an origin.
    return evidence;
}
MotorControlRS::Status axisCommand(void* context, const Probe::AxisCommand& command, Probe::AxisView& view) {
    using namespace MotorControlRS;
    App& a = *static_cast<App*>(context);
    view.commandPolarityKnown = a.commandPolarityKnown;
    if (command.kind == Probe::AxisCommandKind::QUERY) { view.configuration = a.axis; return Ok(); }
    const AxisReference evidence = axisReference(a);
    if (command.kind == Probe::AxisCommandKind::PREPARE) {
        if (!a.commandPolarityKnown && command.position.frame != CoordinateFrame::NATIVE)
            return Status(Err::INVALID_CONFIG, 0, "host polarity requires reconciliation");
        PositionRequest request = command.position; request.configurationGeneration = a.axis.generation;
        const Status status = preparePosition(request, a.axis, evidence.nativeKnown ? &evidence : nullptr, view.prepared);
        if (status) view.configuration = a.axis;
        return status;
    }
    if (command.kind == Probe::AxisCommandKind::ORIGIN) {
        const Status status = setAxisOrigin(a.axis, command.value.numerator, evidence);
        if (status) {
            a.coordinateReference = evidence;
            a.coordinateReference.configurationGeneration = a.axis.generation;
            view.configuration = a.axis;
        }
        return status;
    }
    AxisConfig candidate = a.axis;
    UnitScale* scale = nullptr;
    switch (command.field) {
    case Probe::AxisField::COMMAND_SCALE: scale = &candidate.units.commandStepsPerMotorTurn; break;
    case Probe::AxisField::GEAR: scale = &candidate.units.motorTurnsPerLoadTurn; break;
    case Probe::AxisField::FULL_STEP_SCALE: scale = &candidate.units.fullStepsPerMotorTurn; break;
    case Probe::AxisField::LEAD: scale = &candidate.units.millimetresPerLoadTurn; break;
    case Probe::AxisField::ENCODER_SCALE: scale = &candidate.units.encoder.countsPerUnit; break;
    case Probe::AxisField::ENCODER_ID: candidate.units.encoder.sourceId = static_cast<uint32_t>(command.value.numerator); break;
    case Probe::AxisField::ENCODER_BASIS: candidate.units.encoder.basis = command.encoderBasis; break;
    case Probe::AxisField::ENCODER_POLARITY: candidate.units.encoder.polarity = static_cast<int8_t>(command.value.numerator); break;
    case Probe::AxisField::POLARITY: candidate.units.commandPolarity = static_cast<int8_t>(command.value.numerator); break;
    case Probe::AxisField::POSITION_UNIT: candidate.units.settings.position = command.positionUnit; break;
    case Probe::AxisField::VELOCITY_UNIT: candidate.units.settings.velocity = command.velocityUnit; break;
    case Probe::AxisField::ACCELERATION_UNIT: candidate.units.settings.acceleration = command.accelerationUnit; break;
    case Probe::AxisField::NATIVE_LIMITS:
        candidate.nativeMinimum = command.value.numerator; candidate.nativeMaximum = command.secondValue.numerator; break;
    case Probe::AxisField::SOFT_LIMITS:
        candidate.softLimitsKnown = !command.clear;
        candidate.softMinimum = command.value.numerator; candidate.softMaximum = command.secondValue.numerator; break;
    case Probe::AxisField::RELATIVE_BASES: candidate.supportedRelativeBases = static_cast<uint8_t>(command.value.numerator); break;
    }
    if (scale) *scale = command.clear ? UnitScale() :
        UnitScale(static_cast<uint32_t>(command.value.numerator), static_cast<uint32_t>(command.value.denominator), ScaleSource::ASSUMED);
    const Status status = configureAxis(a.axis, candidate, evidence, &a.coordinateReference);
    if (status) {
        if (command.field == Probe::AxisField::POLARITY) a.commandPolarityKnown = true;
        view.commandPolarityKnown = a.commandPolarityKnown;
        view.configuration = a.axis;
    }
    return status;
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
    s.actionsQualified = actionTimingQualified; s.axisReserved = axisReserved(a);
    s.busy = a.owner.active() || a.owner.pending() || a.owner.recovering() || reading(a) || acting(a);
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
        clearRecord(*record); return Probe::Action::INVALID;
    }
    const auto admitted = admitStep(a, *record, sampled);
    if (admitted != Rtu::BusAdmission::ACCEPTED) {
        clearRecord(*record);
        return admitted == Rtu::BusAdmission::QUEUE_FULL ? Probe::Action::QUEUE_FULL :
            admitted == Rtu::BusAdmission::RESULTS_FULL ? Probe::Action::RESULTS_FULL : Probe::Action::FAILED;
    }
    record->operationId = a.nextOperationId++; record->commandId = commandId; record->address = address;
    record->deadlineUs = record->read.deadlineUs; record->typedRead = true;
    record->configurationGeneration = a.axis.generation;
    record->monitored = monitored; operationId = record->operationId;
    if (!monitored) a.latestOperationId = operationId;
    return Probe::Action::OK;
}
Probe::Action typedRead(void* context, uint32_t commandId, uint8_t address, ESS::ReadKind kind, uint32_t& operationId) {
    return startTypedRead(context, commandId, address, kind, operationId, false);
}
MotorControlRS::ActionEvent actionEvent(const App::Record& record, ReadEventKind kind) {
    MotorControlRS::ActionEvent event;
    event.transport.target = record.driverOperation ? record.driver.target : record.velocityOperation ? record.velocity.target : record.moveOperation ? record.move.target : record.action.target;
    event.transport.operationId = record.operationId;
    event.transport.step = record.driverOperation ? record.driver.step : record.velocityOperation ? record.velocity.step : record.moveOperation ? record.move.step : record.action.step; event.transport.kind = kind;
    return event;
}
Rtu::BusAdmission admitActionStep(App& a, App::Record& record, const ESS::PreparedAction& prepared, uint64_t now) {
    Rtu::BusRequest request;
    request.wire.bytes = prepared.bytes; request.wire.length = prepared.length;
    request.wire.replyLength = prepared.write ? ESS::WRITE_RESPONSE_LEN : ESS::expectedReadRegistersLen(prepared.count);
    request.wire.responseTimeoutUs = RESPONSE_US; request.wire.replyGapUs = REPLY_GAP_US;
    request.wire.deadlineUs = prepared.deadlineUs;
    request.expected.address = prepared.target.address; request.expected.function = prepared.write ? 6 : 3;
    request.expected.target = prepared.target.id; request.expected.targetGeneration = prepared.target.generation;
    request.expected.first = prepared.reg; request.expected.count = prepared.write ? 1 : prepared.count;
    request.expected.value = prepared.value; request.validator = Rtu::essValidator();
    return record.action.request.kind == MotorControlRS::ActionKind::STOP ?
        a.owner.admitUrgent(request, now, record.requestId) : a.owner.admit(request, now, record.requestId);
}
Probe::Action startAction(void* context, uint32_t commandId, uint8_t address,
                          const MotorControlRS::ActionRequest& request, uint32_t& operationId) {
    using namespace MotorControlRS;
    App& a = *static_cast<App*>(context);
    if (!platformReady) return Probe::Action::UNAVAILABLE;
    if (!a.nextOperationId) return Probe::Action::IDS_EXHAUSTED;
    const uint64_t now = uart.sample();
    ActionRequest admittedRequest = request;
    uint64_t deadline = now + REQUEST_US;
    if (request.kind == ActionKind::CLEAR_POSITION) {
        if (request.devicePosition != 0) return Probe::Action::UNSUPPORTED;
        if (!actionTimingQualified) return Probe::Action::TIMING_UNQUALIFIED;
        if (!a.positionClearQualified) return Probe::Action::UNAVAILABLE;
        const auto& motion = a.stateCache.blocks[static_cast<uint8_t>(ESS::StateBlock::MOTION)];
        const uint64_t age = 5000000;
        if (a.axis.target.address != address || !Probe::fresh(motion, a.axis.target, now, age) ||
            motion.value.running || motion.value.released || motion.value.rawAlarm || motion.value.alarmFlag)
            return Probe::Action::UNAVAILABLE;
        const uint64_t readinessEnd = motion.observedEarliestUs > UINT64_MAX - age ? UINT64_MAX : motion.observedEarliestUs + age;
        if (readinessEnd <= now) return Probe::Action::UNAVAILABLE;
        if (readinessEnd < deadline) deadline = readinessEnd;
        admittedRequest.positionClearQualified = true;
    }
    ReadTarget target; target.id = address; target.address = address; target.generation = a.bindingGeneration;
    ESS::ActionContext preparedContext;
    const Status prepared = ESS::prepareAction(preparedContext, target, a.nextOperationId, admittedRequest, now, deadline);
    if (!prepared) return prepared.code == Err::UNSUPPORTED ? Probe::Action::UNSUPPORTED : Probe::Action::INVALID;
    if (!actionTimingQualified) return Probe::Action::TIMING_UNQUALIFIED;
    if (a.owner.needsRecovery() || uart.needsRecovery()) return Probe::Action::RECOVERY_REQUIRED;
    if (!(a.knownTargets[address / 8] & (1U << (address % 8)))) return Probe::Action::UNAVAILABLE;
    const bool stop = request.kind == ActionKind::STOP;
    if (!stop && axisReserved(a, address)) return Probe::Action::AXIS_CONFLICT;
    App::Record* record = nullptr;
    if (stop) {
        if (!a.records[REQUEST_CAPACITY + 1].operationId) record = &a.records[REQUEST_CAPACITY + 1];
    } else for (std::size_t i = 0; i < REQUEST_CAPACITY; ++i)
        if (!a.records[i].operationId) { record = &a.records[i]; break; }
    if (!record) return Probe::Action::RESULTS_FULL;
    record->action = preparedContext;
    ESS::PreparedAction work;
    if (!ESS::nextAction(record->action, now, work) || work.kind != ESS::ActionWork::TRANSACTION) {
        clearRecord(*record); return Probe::Action::FAILED;
    }
    const auto admitted = admitActionStep(a, *record, work, now);
    if (admitted != Rtu::BusAdmission::ACCEPTED) {
        clearRecord(*record);
        return admitted == Rtu::BusAdmission::QUEUE_FULL ? Probe::Action::QUEUE_FULL :
            admitted == Rtu::BusAdmission::RESULTS_FULL || admitted == Rtu::BusAdmission::URGENT_FULL ?
            Probe::Action::RESULTS_FULL : Probe::Action::FAILED;
    }
    record->operationId = a.nextOperationId++; record->commandId = commandId; record->address = address;
    record->deadlineUs = preparedContext.deadlineUs; record->actionOperation = record->axisReserved = true;
    operationId = a.latestOperationId = record->operationId;
    if (stop) for (auto& interrupted : a.records) {
        if (&interrupted == record || (!interrupted.actionOperation && !interrupted.moveOperation && !interrupted.velocityOperation && !interrupted.driverOperation) || interrupted.address != address ||
            terminal(a, interrupted)) continue;
        interrupted.cancelContinuation = true;
        interrupted.interruptedByStop = true;
        if (interrupted.requestId.owner) a.owner.cancelUnsent(interrupted.requestId, now);
    }
    return Probe::Action::OK;
}
Probe::Action admissionResult(Rtu::BusAdmission result) {
    switch (result) {
    case Rtu::BusAdmission::ACCEPTED: return Probe::Action::OK;
    case Rtu::BusAdmission::QUEUE_FULL: return Probe::Action::QUEUE_FULL;
    case Rtu::BusAdmission::RESULTS_FULL: return Probe::Action::RESULTS_FULL;
    case Rtu::BusAdmission::URGENT_FULL: return Probe::Action::RESULTS_FULL;
    case Rtu::BusAdmission::IDS_EXHAUSTED: return Probe::Action::IDS_EXHAUSTED;
    case Rtu::BusAdmission::RECOVERING: return Probe::Action::RECOVERY_REQUIRED;
    default: return Probe::Action::INVALID;
    }
}
// Other cooperative producers use this admission adapter for ordinary writes.
// The operation executor below already owns its same-axis reservation. A raw
// call to the bus owner cannot substitute for this application policy boundary.
Probe::Action checkAxisWrite(const App& a, const Rtu::BusRequest& request) {
    if (!ESS::isValidAddress(request.expected.address) ||
        (request.expected.function != 6 && request.expected.function != 0x10) ||
        request.expected.target != request.expected.address ||
        request.expected.targetGeneration != a.bindingGeneration) return Probe::Action::INVALID;
    if (!platformReady) return Probe::Action::UNAVAILABLE;
    if (!actionTimingQualified) return Probe::Action::TIMING_UNQUALIFIED;
    if (a.owner.needsRecovery() || uart.needsRecovery()) return Probe::Action::RECOVERY_REQUIRED;
    if (!(a.knownTargets[request.expected.address / 8] & (1U << (request.expected.address % 8))))
        return Probe::Action::UNAVAILABLE;
    if (axisReserved(a, request.expected.address)) return Probe::Action::AXIS_CONFLICT;
    return Probe::Action::OK;
}
inline Probe::Action admitAxisWrite(App& a, const Rtu::BusRequest& request, uint64_t now, Rtu::RequestId& id) {
    const Probe::Action checked = checkAxisWrite(a, request);
    if (checked != Probe::Action::OK) return checked;
    const Probe::Action admitted = admissionResult(a.owner.admit(request, now, id));
    if (admitted == Probe::Action::OK && request.expected.address == a.axis.target.address) {
        // Even an uncertain independent write can change configuration or state.
        // Require new reads and qualification before any subsequent move.
        invalidateAxis(a);
        a.configuration.operationId = 0;
        a.movePrerequisites = ESS::MovePrerequisites();
        a.velocityPrerequisites = ESS::VelocityPrerequisites();
    }
    if (admitted == Probe::Action::OK)
        for (auto& block : a.stateCache.blocks)
            if (block.value.target.address == request.expected.address) block.invalidatedUs = now;
    return admitted;
}
Probe::Action admitMoveStep(App& a, App::Record& record, const ESS::PreparedMove& prepared, uint64_t now) {
    Rtu::BusRequest request;
    request.wire.bytes = prepared.bytes; request.wire.length = prepared.length;
    request.wire.replyLength = prepared.write ? ESS::WRITE_RESPONSE_LEN : ESS::expectedReadRegistersLen(prepared.count);
    request.wire.responseTimeoutUs = RESPONSE_US; request.wire.replyGapUs = REPLY_GAP_US;
    request.wire.deadlineUs = prepared.deadlineUs;
    request.expected.address = prepared.target.address; request.expected.function = prepared.function;
    request.expected.target = prepared.target.id; request.expected.targetGeneration = prepared.target.generation;
    request.expected.first = prepared.reg; request.expected.count = prepared.count;
    request.expected.value = prepared.value; request.validator = Rtu::essValidator();
    if (!record.operationId) {
        const Probe::Action checked = checkAxisWrite(a, request);
        if (checked != Probe::Action::OK) return checked;
    }
    return admissionResult(a.owner.admit(request, now, record.requestId));
}
Probe::Action startMove(void* context, uint32_t commandId, uint8_t address,
                        const MotorControlRS::MoveRequest& supplied, uint32_t& operationId) {
    using namespace MotorControlRS;
    App& a = *static_cast<App*>(context);
    if (!platformReady) return Probe::Action::UNAVAILABLE;
    if (!ESS::isValidAddress(address) || !supplied.speedRpm ||
        supplied.ramp != MoveRamp::VERIFIED_CONFIGURED) return Probe::Action::INVALID;
    if (!a.commandPolarityKnown && supplied.position.frame != CoordinateFrame::NATIVE)
        return Probe::Action::UNAVAILABLE;
    if (!a.nextOperationId) return Probe::Action::IDS_EXHAUSTED;
    if (!actionTimingQualified) return Probe::Action::TIMING_UNQUALIFIED;
    if (a.owner.needsRecovery() || uart.needsRecovery()) return Probe::Action::RECOVERY_REQUIRED;
    if (axisReserved(a, address)) return Probe::Action::AXIS_CONFLICT;
    // Begin on a quiescent bus so an earlier producer's admitted write cannot
    // remain queued across our staging reservation. Reads may join afterward.
    if (a.owner.active() || a.owner.pending()) return Probe::Action::BUSY;
    if (a.axis.target.address != address || !a.configuration.operationId ||
        !Probe::sameTarget(a.configuration.target, a.axis.target)) return Probe::Action::UNAVAILABLE;
    App::Record* record = nullptr;
    for (std::size_t i = 0; i < REQUEST_CAPACITY; ++i)
        if (!a.records[i].operationId) { record = &a.records[i]; break; }
    if (!record) return Probe::Action::RESULTS_FULL;
    const uint64_t now = uart.sample();
    ESS::MovePrerequisites prerequisites = a.movePrerequisites;
    const auto& motion = a.stateCache.blocks[static_cast<uint8_t>(ESS::StateBlock::MOTION)];
    if (!Probe::fresh(motion, a.axis.target, now, prerequisites.maximumAgeUs)) prerequisites.readinessQualified = false;
    else {
        prerequisites.rawAlarm = motion.value.rawAlarm; prerequisites.rawMotion = motion.value.rawMotion;
        prerequisites.observedUs = motion.observedEarliestUs;
    }
    prerequisites.wordOrderKnown = a.configuration.wordOrderKnown;
    prerequisites.wordOrder = a.configuration.wordOrder;
    const MoveRequest& request = supplied;
    const AxisReference reference = axisReference(a);
    ESS::MoveContext& prepared = record->move;
    const uint64_t deadline = now + 3000000;
    ActionOptions options; options.maxPolls = ESS::ACTION_MAX_POLLS; options.pollIntervalUs = 20000;
    const auto prepare = request.position.wrapped ? ESS::prepareMoveAngle :
        request.position.relative ? ESS::prepareMoveRelative : ESS::prepareMoveAbsolute;
    const Status checked = prepare(prepared, a.axis, reference.nativeKnown ? &reference : nullptr,
        a.nextOperationId, request, prerequisites, now, deadline, options);
    if (!checked) return checked.code == Err::UNSUPPORTED ? Probe::Action::UNSUPPORTED : Probe::Action::INVALID;
    // Proposed free-shaft software envelope, not a qualified physical envelope.
    if (!prepared.prepared.displacementKnown || prepared.prepared.displacementNative < -250 ||
        prepared.prepared.displacementNative > 250 || request.speedRpm > 60) {
        clearRecord(*record); return Probe::Action::INVALID;
    }
    ESS::PreparedMove work;
    if (!ESS::nextMove(record->move, now, work)) { clearRecord(*record); return Probe::Action::FAILED; }
    const auto admitted = admitMoveStep(a, *record, work, now);
    if (admitted != Probe::Action::OK) { clearRecord(*record); return admitted; }
    record->operationId = a.nextOperationId++; record->commandId = commandId; record->address = address;
    record->deadlineUs = deadline; record->moveOperation = record->axisReserved = true;
    operationId = a.latestOperationId = record->operationId;
    return Probe::Action::OK;
}
Probe::Action admitVelocityStep(App& a, App::Record& record, const ESS::PreparedVelocity& prepared, uint64_t now) {
    Rtu::BusRequest request;
    request.wire.bytes = prepared.bytes; request.wire.length = prepared.length;
    request.wire.replyLength = prepared.write ? ESS::WRITE_RESPONSE_LEN : ESS::expectedReadRegistersLen(prepared.count);
    request.wire.responseTimeoutUs = RESPONSE_US; request.wire.replyGapUs = REPLY_GAP_US;
    request.wire.deadlineUs = prepared.deadlineUs;
    request.expected.address = prepared.target.address; request.expected.function = prepared.function;
    request.expected.target = prepared.target.id; request.expected.targetGeneration = prepared.target.generation;
    request.expected.first = prepared.reg; request.expected.count = prepared.count;
    request.expected.value = prepared.value; request.validator = Rtu::essValidator();
    if (!record.operationId) {
        const auto checked = checkAxisWrite(a, request); if (checked != Probe::Action::OK) return checked;
    }
    return admissionResult(prepared.urgent ? a.owner.admitUrgent(request, now, record.requestId) :
        a.owner.admit(request, now, record.requestId));
}
Probe::Action startVelocity(void* context, uint32_t commandId, uint8_t address,
                            const MotorControlRS::VelocityRequest& supplied, uint32_t& operationId) {
    using namespace MotorControlRS;
    App& a = *static_cast<App*>(context);
    if (!platformReady) return Probe::Action::UNAVAILABLE;
    if (!ESS::isValidAddress(address) || !supplied.durationUs || supplied.durationUs > 1000000)
        return Probe::Action::INVALID;
    if (!a.commandPolarityKnown && supplied.frame != CoordinateFrame::NATIVE) return Probe::Action::UNAVAILABLE;
    if (supplied.ramp == VelocityRamp::ACCELERATION || supplied.acceleration || supplied.deceleration ||
        supplied.jerk || supplied.blending || supplied.liveUpdate) return Probe::Action::UNSUPPORTED;
    if (!a.nextOperationId) return Probe::Action::IDS_EXHAUSTED;
    if (!actionTimingQualified) return Probe::Action::TIMING_UNQUALIFIED;
    if (a.owner.needsRecovery() || uart.needsRecovery()) return Probe::Action::RECOVERY_REQUIRED;
    if (axisReserved(a, address)) return Probe::Action::AXIS_CONFLICT;
    if (a.owner.active() || a.owner.pending()) return Probe::Action::BUSY;
    if (a.axis.target.address != address || !a.configuration.operationId ||
        !Probe::sameTarget(a.configuration.target, a.axis.target)) return Probe::Action::UNAVAILABLE;
    App::Record* record = nullptr;
    for (std::size_t i = 0; i < REQUEST_CAPACITY; ++i)
        if (!a.records[i].operationId) { record = &a.records[i]; break; }
    if (!record) return Probe::Action::RESULTS_FULL;
    const uint64_t now = uart.sample();
    auto prerequisites = a.velocityPrerequisites;
    prerequisites.minimumRpm = std::max<int16_t>(prerequisites.minimumRpm,-60);
    prerequisites.maximumRpm = std::min<int16_t>(prerequisites.maximumRpm,60);
    const auto& motion = a.stateCache.blocks[static_cast<uint8_t>(ESS::StateBlock::MOTION)];
    if (!Probe::fresh(motion, a.axis.target, now, prerequisites.maximumAgeUs)) prerequisites.readinessQualified = false;
    else {
        prerequisites.rawAlarm = motion.value.rawAlarm; prerequisites.rawMotion = motion.value.rawMotion;
        prerequisites.observedUs = motion.observedEarliestUs;
    }
    // Proposed finite free-shaft envelope only; commissioning flags remain
    // false in production. Setup consumes duration and two seconds remain for stop.
    const uint64_t deadline = now + supplied.durationUs + 2000000;
    ActionOptions options; options.maxPolls = ESS::ACTION_MAX_POLLS; options.pollIntervalUs = 20000;
    const auto checked = ESS::prepareVelocity(record->velocity, a.axis, a.nextOperationId,
        supplied, prerequisites, now, deadline, options);
    if (!checked) { clearRecord(*record); return checked.code == Err::UNSUPPORTED ? Probe::Action::UNSUPPORTED : Probe::Action::INVALID; }
    if (record->velocity.prepared.nativeRpm < -60 || record->velocity.prepared.nativeRpm > 60) {
        clearRecord(*record); return Probe::Action::INVALID;
    }
    ESS::PreparedVelocity work;
    if (!ESS::nextVelocity(record->velocity, now, work)) { clearRecord(*record); return Probe::Action::FAILED; }
    const auto admitted = admitVelocityStep(a, *record, work, now);
    if (admitted != Probe::Action::OK) { clearRecord(*record); return admitted; }
    record->operationId = a.nextOperationId++; record->commandId = commandId; record->address = address;
    record->deadlineUs = deadline; record->velocityOperation = record->axisReserved = true;
    operationId = a.latestOperationId = record->operationId; return Probe::Action::OK;
}
Probe::Action admitDriverStep(App& a, App::Record& record, const ESS::PreparedDriver& work, uint64_t now) {
    Rtu::BusRequest request;
    request.wire.bytes = work.bytes; request.wire.length = work.length;
    request.wire.replyLength = work.write ? ESS::WRITE_RESPONSE_LEN : ESS::expectedReadRegistersLen(work.count);
    request.wire.responseTimeoutUs = RESPONSE_US; request.wire.replyGapUs = REPLY_GAP_US;
    request.wire.deadlineUs = work.deadlineUs;
    request.expected.address = work.target.address; request.expected.function = work.write ? 6 : 3;
    request.expected.target = work.target.id; request.expected.targetGeneration = work.target.generation;
    request.expected.first = work.reg; request.expected.count = work.count;
    request.expected.value = work.value; request.validator = Rtu::essValidator();
    if (!record.operationId && work.write) {
        const auto checked = checkAxisWrite(a, request); if (checked != Probe::Action::OK) return checked;
    }
    return admissionResult(a.owner.admit(request, now, record.requestId));
}
Probe::Action startDriver(void* context, uint32_t commandId, uint8_t address, ESS::DriverKind kind,
                         const ESS::DriverRequest& supplied, uint32_t& operationId) {
    using namespace MotorControlRS;
    App& a = *static_cast<App*>(context);
    if (!platformReady) return Probe::Action::UNAVAILABLE;
    if (!ESS::isValidAddress(address)) return Probe::Action::INVALID;
    if (!a.nextOperationId) return Probe::Action::IDS_EXHAUSTED;
    if (a.owner.needsRecovery() || uart.needsRecovery()) return Probe::Action::RECOVERY_REQUIRED;
    if (axisReserved(a, address)) return Probe::Action::AXIS_CONFLICT;
    // Existing admitted writes cannot cross this new configuration reservation.
    if (kind == ESS::DriverKind::UPDATE && (a.owner.active() || a.owner.pending())) return Probe::Action::BUSY;
    if (kind == ESS::DriverKind::UPDATE && address != a.axis.target.address) return Probe::Action::UNAVAILABLE;
    App::Record* record = nullptr;
    for (std::size_t i = 0; i < REQUEST_CAPACITY; ++i)
        if (!a.records[i].operationId) { record = &a.records[i]; break; }
    if (!record) return Probe::Action::RESULTS_FULL;
    const uint64_t now = uart.sample();
    ReadTarget target; target.id = target.address = address; target.generation = a.bindingGeneration;
    const uint32_t generation = address == a.axis.target.address ? a.axis.generation : a.bindingGeneration;
    Status checked;
    if (kind == ESS::DriverKind::READ) {
        checked = ESS::prepareDriverRead(record->driver, target, a.nextOperationId, generation, now, now + REQUEST_US);
    } else if (kind == ESS::DriverKind::UPDATE) {
        auto& prerequisites = a.driverPrerequisites;
        prerequisites.~DriverPrerequisites(); new (&prerequisites) ESS::DriverPrerequisites();
        prerequisites.previous = a.driverSettings; prerequisites.configurationGeneration = generation;
        prerequisites.stationaryTarget = target; prerequisites.maxAgeUs = 5000000;
        const auto& motion = a.stateCache.blocks[static_cast<uint8_t>(ESS::StateBlock::MOTION)];
        prerequisites.stationaryQualified = Probe::fresh(motion, target, now, prerequisites.maxAgeUs);
        prerequisites.rawAlarm = motion.value.rawAlarm; prerequisites.rawMotion = motion.value.rawMotion;
        prerequisites.stationaryEarliestUs = motion.observedEarliestUs;
        prerequisites.stationaryLatestUs = motion.observedLatestUs;
        prerequisites.inputsPermit = a.driverInputsQualified;
        ESS::DriverRequest request = supplied;
        if (!request.configurationGeneration) request.configurationGeneration = generation;
        checked = ESS::prepareDriverSettings(record->driver, target, a.nextOperationId, request, prerequisites, now, now + 3000000);
    } else return Probe::Action::INVALID;
    if (!checked) { clearRecord(*record); return checked.code == Err::UNSUPPORTED ? Probe::Action::UNSUPPORTED : Probe::Action::INVALID; }
    ESS::PreparedDriver work;
    if (!ESS::nextDriver(record->driver, now, work)) { clearRecord(*record); return Probe::Action::FAILED; }
    const auto admitted = admitDriverStep(a, *record, work, now);
    if (admitted != Probe::Action::OK) { clearRecord(*record); return admitted; }
    record->operationId = a.nextOperationId++; record->commandId = commandId; record->address = address;
    record->deadlineUs = record->driver.deadlineUs; record->configurationGeneration = generation;
    record->driverOperation = true; record->axisReserved = kind == ESS::DriverKind::UPDATE;
    operationId = a.latestOperationId = record->operationId; return Probe::Action::OK;
}
// Apply possible changes as soon as TX is accepted. Readback never rewrites
// historical contexts or silently promotes stored codes into active settings.
void invalidateDriverAssumptions(App& a, uint8_t address, uint16_t changed, uint64_t now, const App::Record* changing = nullptr) {
    if (address == a.axis.target.address) {
        invalidateAxis(a, changing);
        if (changed & (static_cast<uint16_t>(ESS::DriverField::DIRECTION) | static_cast<uint16_t>(ESS::DriverField::SUBDIVISION)))
            a.axis.units.commandStepsPerMotorTurn = MotorControlRS::UnitScale();
        if (changed & static_cast<uint16_t>(ESS::DriverField::DIRECTION)) a.commandPolarityKnown = false;
        a.configuration.operationId = 0;
        a.movePrerequisites = ESS::MovePrerequisites(); a.velocityPrerequisites = ESS::VelocityPrerequisites();
        a.positionClearQualified = false;
    }
    if (a.driverSettings.target.address == address) a.driverSettings.operationId = 0;
    if (changed & (static_cast<uint16_t>(ESS::DriverField::INTERRUPTION) | static_cast<uint16_t>(ESS::DriverField::POSITION_MODE)))
        a.driverInputsQualified = false;
    for (auto& block : a.stateCache.blocks)
        if (block.value.target.address == address) block.invalidatedUs = now;
}
void driverEffects(App& a, App::Record& record, uint16_t effects, uint64_t now) {
    const uint16_t changed = effects & static_cast<uint16_t>(~record.driverEffects);
    if (!changed) return;
    record.driverEffects |= changed;
    invalidateDriverAssumptions(a, record.address, changed, now, &record);
}
uint16_t driverConfigEffects(const ESS::RawConfig& config, const ESS::DriverObservation& driver) {
    uint16_t effects = 0;
    if (config.direction != driver.raw[0]) effects |= static_cast<uint16_t>(ESS::DriverField::DIRECTION);
    if (config.subdivision != driver.raw[1]) effects |= static_cast<uint16_t>(ESS::DriverField::SUBDIVISION);
    if (config.wordOrder != driver.raw[2]) effects |= static_cast<uint16_t>(ESS::DriverField::WORD_ORDER);
    if (config.softLimitEnable != driver.raw[3]) effects |= static_cast<uint16_t>(ESS::DriverField::SOFT_LIMIT_ENABLE);
    if (config.overLimitStop != driver.raw[4]) effects |= static_cast<uint16_t>(ESS::DriverField::OVER_LIMIT_STOP);
    return effects;
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
    a.knownTargets[result.expected.address / 8] |= static_cast<uint8_t>(1U << (result.expected.address % 8));
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
            if (record.read.kind == ESS::ReadKind::STATE && record.configurationGeneration == a.axis.generation) {
                const auto& previous = a.stateCache.blocks[static_cast<uint8_t>(block)];
                const bool hadPosition = previous.valid && previous.value.pairKnown &&
                    Probe::sameTarget(previous.value.target, a.axis.target);
                const auto previousPosition = previous.value.rawPosition;
                const auto previousSuccess = previous.observedLatestUs;
                Probe::stateResult(a.stateCache, record.read, block, result->transport.startedUs);
                const auto& current = a.stateCache.blocks[static_cast<uint8_t>(block)];
                if (block == static_cast<uint8_t>(ESS::StateBlock::FEEDBACK) && hadPosition && current.valid && current.value.pairKnown &&
                    current.observedLatestUs > previousSuccess && current.value.rawPosition != previousPosition &&
                    Probe::sameTarget(current.value.target, a.axis.target) && coordinateKnowledge(a) &&
                    !triggeredMotion(a, a.axis.target.address)) invalidateAxis(a);
            }
            if (record.read.state != ReadState::ACTIVE) continue;
            a.owner.release(record.requestId); record.requestId = Rtu::RequestId();
        }
        if (record.cancelContinuation || record.read.target.generation != a.bindingGeneration ||
            (record.read.kind == ESS::ReadKind::STATE && record.configurationGeneration != a.axis.generation)) {
            ESS::advanceRead(record.read, readEvent(record, ReadEventKind::CANCEL), sampled);
        } else if (sampled >= record.deadlineUs) {
            ESS::advanceRead(record.read, readEvent(record, ReadEventKind::DEADLINE), sampled);
        } else if (!a.owner.needsRecovery() && !uart.needsRecovery()) {
            admitStep(a, record, sampled); // Queue pressure defers; no admitted frame is retried.
        }
    }
}
void updateActionReservation(App& a, App::Record& record) {
    using namespace MotorControlRS;
    if (!terminal(a, record)) return;
    if (record.driverOperation) {
        driverEffects(a, record, record.driver.effects, nowUs());
        record.axisReserved = false;
        if (record.driver.kind == ESS::DriverKind::READ && record.address == a.axis.target.address &&
            record.configurationGeneration == a.axis.generation &&
            record.driver.target.generation == a.bindingGeneration && record.operationId > a.driverSettings.operationId &&
            record.operationId > a.configuration.operationId &&
            ESS::getDriver(record.driver, a.driverObserved)) {
            const auto& observed = a.driverObserved;
            uint16_t changed = 0;
            if (a.driverSettings.operationId && Probe::sameTarget(a.driverSettings.target, observed.target)) {
                for (uint8_t i = 0; i < ESS::DRIVER_FIELD_COUNT; ++i)
                    if (a.driverSettings.raw[i] != observed.raw[i]) changed |= static_cast<uint16_t>(1U << i);
                if (std::memcmp(a.driverSettings.positiveWords, observed.positiveWords, sizeof(observed.positiveWords)) ||
                    std::memcmp(a.driverSettings.negativeWords, observed.negativeWords, sizeof(observed.negativeWords)))
                    changed |= static_cast<uint16_t>(ESS::DriverField::POSITIVE_LIMIT) | static_cast<uint16_t>(ESS::DriverField::NEGATIVE_LIMIT);
            } else if (a.configuration.operationId && Probe::sameTarget(a.configuration.target, observed.target)) {
                changed |= driverConfigEffects(a.configuration.raw, observed);
            }
            if (changed) invalidateDriverAssumptions(a, record.address, changed, nowUs(), &record);
            a.driverSettings = observed;
            // The cache is reconciled to the new host interpretation generation;
            // record.driver and all raw/provenance retain their admission context.
            if (record.address == a.axis.target.address) a.driverSettings.configurationGeneration = a.axis.generation;
        }
        return;
    }
    // Arrival flags alone do not establish an exact new command coordinate.
    // Retain the move's original reference/result; invalidate dependent host
    // knowledge until an application can supply a newly qualified reference.
    if (((record.moveOperation && record.move.triggerEvidence.txAccepted) ||
        (record.velocityOperation && record.velocity.triggerEvidence.txAccepted)) &&
        record.address == a.axis.target.address && coordinateKnowledge(a)) invalidateAxis(a);
    record.axisReserved = false;
    const uint8_t mask = static_cast<uint8_t>(1U << (record.address % 8));
    if (record.velocityOperation ? record.velocity.needsStop || record.velocity.uncertain : record.moveOperation ? record.move.uncertain :
        (record.action.execution == ActionExecution::UNKNOWN ||
        (record.action.execution == ActionExecution::ACKNOWLEDGED && record.action.completion != ActionCompletion::OBSERVED)))
        a.actionConflicts[record.address / 8] |= mask;
    if ((record.velocityOperation && record.velocity.stop.completion == ActionCompletion::OBSERVED) ||
        (!record.moveOperation && !record.velocityOperation && record.action.request.kind == ActionKind::STOP && record.action.completion == ActionCompletion::OBSERVED)) {
        // A new checked stopped-state report reconciles conflicts; historical
        // interrupted outcomes and their execution uncertainty stay unchanged.
        a.actionConflicts[record.address / 8] &= static_cast<uint8_t>(~mask);
    }
}
MotorControlRS::Status advanceOperation(App::Record& record, const MotorControlRS::ActionEvent& event, uint64_t now) {
    if (record.driverOperation) return ESS::advanceDriver(record.driver, event, now);
    if (record.velocityOperation) return ESS::advanceVelocity(record.velocity, event, now);
    return record.moveOperation ? ESS::advanceMove(record.move, event, now) : ESS::advanceAction(record.action, event, now);
}
void advanceActions(App& a, uint64_t now) {
    for (auto& record : a.records) {
        if ((!record.actionOperation && !record.moveOperation && !record.velocityOperation && !record.driverOperation) || terminal(a, record)) continue;
        if (record.requestId.owner) {
            if (record.driverOperation && record.driver.kind == ESS::DriverKind::UPDATE &&
                a.owner.txAccepted(record.requestId)) {
                ESS::PreparedDriver work;
                if (ESS::nextDriver(record.driver, record.driver.servicedUs, work) && work.write)
                    for (const auto& progress : record.driver.progress)
                        if (progress.selected && progress.reg == work.reg)
                            driverEffects(a, record, static_cast<uint16_t>(progress.field), now);
            }
            if (((record.moveOperation && record.move.step == 1) ||
                (record.velocityOperation && record.velocity.phase == ESS::VelocityPhase::TRIGGER)) && record.address == a.axis.target.address &&
                a.owner.txAccepted(record.requestId)) a.coordinateReference.nativeKnown = false;
            if (!record.driverOperation && !record.effectsInvalidated && (record.velocityOperation ? record.velocity.step == 0 : record.moveOperation ? record.move.step == 0 : record.action.step == 0) && a.owner.txAccepted(record.requestId)) {
                record.effectsInvalidated = true;
                for (auto& block : a.stateCache.blocks)
                    if (block.valid && block.value.target.address == record.address) block.invalidatedUs = now;
                if (!record.moveOperation && !record.velocityOperation && (record.action.request.kind == MotorControlRS::ActionKind::RELEASE ||
                    record.action.request.kind == MotorControlRS::ActionKind::CLEAR_POSITION) &&
                    a.axis.target.address == record.address) invalidateAxis(a);
            }
            const auto* result = a.owner.result(record.requestId); if (!result) continue;
            const auto kind = result->transport.reason == Rtu::Reason::FRAME ? ReadEventKind::FRAME :
                result->transport.reason == Rtu::Reason::REQUEST_DEADLINE ||
                result->outcome == Rtu::Outcome::QUEUE_EXPIRED || result->outcome == Rtu::Outcome::DISPATCH_EXPIRED ? ReadEventKind::DEADLINE :
                result->outcome == Rtu::Outcome::CANCELLED ? ReadEventKind::CANCEL : ReadEventKind::TRANSPORT_FAILURE;
            auto event = actionEvent(record, kind);
            event.transport.target.id = result->expected.target;
            event.transport.target.address = result->expected.address;
            event.transport.target.generation = result->expected.targetGeneration;
            event.transport.txAccepted = result->transport.txAccepted;
            event.transport.executionUnknown = result->executionUnknown;
            event.transport.transportDetail = static_cast<int32_t>(result->transport.reason);
            event.transport.frame = result->transport.rxLength ? result->raw : nullptr;
            event.transport.length = result->transport.rxLength;
            event.txComplete = result->transport.txComplete;
            event.responseConfirmed = kind == ReadEventKind::FRAME &&
                (actionTimingQualified || (record.driverOperation && record.driver.kind == ESS::DriverKind::READ));
            if (kind == ReadEventKind::FRAME) {
                event.transport.qualified = result->transport.closureQualified;
                if (event.transport.qualified) {
                    event.transport.earliestUs = result->transport.closureEarliestUs;
                    event.transport.latestUs = result->transport.closureLatestUs;
                }
            }
            if (!advanceOperation(record, event, now)) {
                auto failed = actionEvent(record, ReadEventKind::TRANSPORT_FAILURE);
                failed.transport.transportDetail = -1;
                failed.transport.txAccepted = event.transport.txAccepted;
                failed.transport.executionUnknown = true;
                failed.transport.frame = event.transport.frame; failed.transport.length = event.transport.length;
                failed.txComplete = event.txComplete;
                if (!advanceOperation(record, failed, now)) continue;
            }
            observeCommunication(a, *result);
            a.owner.release(record.requestId); record.requestId = Rtu::RequestId();
        }
        if (!terminal(a, record)) {
            if (record.cancelContinuation || (record.driverOperation ? record.driver.target.generation : record.velocityOperation ? record.velocity.target.generation : record.moveOperation ? record.move.target.generation : record.action.target.generation) != a.bindingGeneration ||
                (record.moveOperation && record.move.prepared.configurationGeneration != a.axis.generation) ||
                (record.velocityOperation && record.velocity.request.configurationGeneration != a.axis.generation))
                advanceOperation(record, actionEvent(record, ReadEventKind::CANCEL), now);
            else if (record.velocityOperation) {
                // Evidence above is consumed before elapsed time. A borrowed
                // admitted token must settle before stop changes its correlation.
                ESS::serviceVelocity(record.velocity, now);
                if (!terminal(a, record) && !a.owner.needsRecovery() && !uart.needsRecovery()) {
                    ESS::PreparedVelocity work;
                    if (ESS::nextVelocity(record.velocity, now, work) && work.kind == ESS::ActionWork::TRANSACTION) {
                        const auto admitted = admitVelocityStep(a, record, work, now);
                        if (admitted != Probe::Action::OK && admitted != Probe::Action::QUEUE_FULL &&
                            admitted != Probe::Action::RESULTS_FULL && admitted != Probe::Action::RECOVERY_REQUIRED)
                            advanceOperation(record, actionEvent(record, ReadEventKind::TRANSPORT_FAILURE), now);
                    }
                }
            }
            else if (now >= record.deadlineUs)
                advanceOperation(record, actionEvent(record, ReadEventKind::DEADLINE), now);
            else if (!a.owner.needsRecovery() && !uart.needsRecovery()) {
                if (record.moveOperation) {
                    ESS::PreparedMove work;
                    const auto next = ESS::nextMove(record.move, now, work);
                    if (!next && next.detail == static_cast<int32_t>(MotorControlRS::MoveError::READINESS))
                        advanceOperation(record, actionEvent(record, ReadEventKind::DEADLINE), now);
                    else if (next && work.kind == ESS::ActionWork::TRANSACTION)
                        admitMoveStep(a, record, work, now);
                } else if (record.driverOperation) {
                    ESS::PreparedDriver work;
                    const auto next = ESS::nextDriver(record.driver, now, work);
                    if (!next) advanceOperation(record, actionEvent(record, ReadEventKind::DEADLINE), now);
                    else if (work.kind == ESS::ActionWork::TRANSACTION)
                        admitDriverStep(a, record, work, now);
                } else {
                    ESS::PreparedAction work;
                    if (ESS::nextAction(record.action, now, work) && work.kind == ESS::ActionWork::TRANSACTION)
                        admitActionStep(a, record, work, now); // Pressure defers unadmitted work, never replays a frame.
                }
            }
        }
        updateActionReservation(a, record);
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
    invalidateAxis(a);
    a.monitorState.settings.enabled = false; a.monitorState.remaining = 0;
    for (auto& record : a.records) if (record.operationId && record.typedRead &&
        record.read.state == ReadState::ACTIVE && !record.requestId.owner)
        ESS::advanceRead(record.read, readEvent(record, ReadEventKind::CANCEL), now);
    for (auto& record : a.records) if ((record.actionOperation || record.moveOperation || record.velocityOperation || record.driverOperation) && !terminal(a, record)) {
        record.cancelContinuation = true;
        if (!record.requestId.owner) {
            advanceOperation(record, actionEvent(record, ReadEventKind::CANCEL), now);
            updateActionReservation(a, record);
        }
    }
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
    if (record->driverOperation) {
        out.driverContext = &record->driver; out.pending = !terminal(a, *record); return true;
    }
    if (record->velocityOperation) {
        out.velocityContext = &record->velocity; out.pending = !terminal(a, *record);
        out.interruptedByStop = record->interruptedByStop; return true;
    }
    if (record->moveOperation) {
        out.moveContext = &record->move; out.pending = !terminal(a, *record);
        out.interruptedByStop = record->interruptedByStop; return true;
    }
    if (record->actionOperation) {
        out.actionContext = &record->action; out.pending = !terminal(a, *record);
        out.interruptedByStop = record->interruptedByStop; return true;
    }
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
    if (record->actionOperation || record->moveOperation || record->velocityOperation || record->driverOperation) {
        if (terminal(a, *record)) return Probe::Action::ALREADY_TERMINAL;
        record->cancelContinuation = true;
        if (record->requestId.owner) a.owner.cancel(record->requestId, uart.sample());
        else {
            advanceOperation(*record, actionEvent(*record, ReadEventKind::CANCEL), nowUs());
            updateActionReservation(a, *record);
        }
        return Probe::Action::OK;
    }
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
    clearRecord(*record); return Probe::Action::OK;
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
    if (requested && (a.owner.active() || a.owner.pending() || a.owner.recovering() || reading(a) || acting(a) || a.runner.transmitEnabled())) return Probe::Action::BUSY;
    const auto result = loadFixture.configure(requested, out, uart); out.ready = platformReady;
    if (requested) nextServiceUs = 0;
    return result;
}
#endif
Probe::Host host(App* a) {
    Probe::Host h; h.context = a; h.emitLine = emit; h.snapshot = snapshot;
    h.startProbe = probe; h.startCaptureRead = captureRead; h.recover = recover; h.resetStats = reset;
    h.startTypedRead = typedRead;
    h.startAction = startAction; h.startMove = startMove; h.startVelocity = startVelocity; h.startDriver = startDriver;
    h.monitor = monitor;
    h.axis = axisCommand;
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
            clearRecord(record); a.monitorState.operationId = 0;
            if (!a.monitorState.remaining) a.monitorState.settings.enabled = false;
            continue;
        }
        Probe::ResultView view; if (!lookup(&a, record.operationId, view) || view.pending) continue;
        if (record.driverOperation) {
            if (!a.console.reportDriver(record.commandId, record.operationId, record.driver)) continue;
            record.delivered = true; record.deliveredUs = nowUs(); continue;
        }
        if (record.velocityOperation) {
            if (!a.console.reportVelocity(record.commandId, record.operationId, record.velocity, record.interruptedByStop)) continue;
            record.delivered = true; record.deliveredUs = nowUs(); continue;
        }
        if (record.moveOperation) {
            if (!a.console.reportMove(record.commandId, record.operationId, record.move, record.interruptedByStop)) continue;
            record.delivered = true; record.deliveredUs = nowUs(); continue;
        }
        if (record.actionOperation) {
            if (!a.console.reportAction(record.commandId, record.operationId, record.action, record.interruptedByStop)) continue;
            record.delivered = true; record.deliveredUs = nowUs(); continue;
        }
        if (record.typedRead) {
            if (!record.observed) {
                record.observed = true;
                if (record.read.kind == ESS::ReadKind::IDENTITY && record.operationId > a.identity.operationId)
                    ESS::getIdentity(record.read, a.identity);
                if (record.read.kind == ESS::ReadKind::CONFIG && record.address == a.axis.target.address &&
                    record.operationId > a.configuration.operationId &&
                    record.operationId > a.driverSettings.operationId &&
                    record.configurationGeneration == a.axis.generation && record.read.target.generation == a.bindingGeneration) {
                    const ESS::RawConfig old = a.configuration.raw;
                    const bool same = a.configuration.operationId && Probe::sameTarget(a.configuration.target, record.read.target);
                    if (ESS::getConfig(record.read, a.configuration)) {
                        const auto& updated = a.configuration.raw;
                        const bool inputsChanged = !same || old.inputPolarity != updated.inputPolarity ||
                            std::memcmp(old.inputFunctions, updated.inputFunctions, sizeof(old.inputFunctions)) != 0;
                        if (inputsChanged) a.driverInputsQualified = false;
                        uint16_t effects = 0;
                        if (a.driverSettings.operationId && Probe::sameTarget(a.driverSettings.target, record.read.target))
                            effects = driverConfigEffects(updated, a.driverSettings);
                        const bool configChanged = same &&
                            (old.direction != updated.direction || old.subdivision != updated.subdivision ||
                             old.wordOrder != updated.wordOrder || old.algorithm != updated.algorithm ||
                             old.encoderResolution != updated.encoderResolution ||
                             old.inputPolarity != updated.inputPolarity || old.overLimitStop != updated.overLimitStop ||
                             old.softLimitEnable != updated.softLimitEnable ||
                             std::memcmp(old.inputFunctions, updated.inputFunctions, sizeof(old.inputFunctions)) != 0);
                        if (same && old.direction != updated.direction) effects |= static_cast<uint16_t>(ESS::DriverField::DIRECTION);
                        if (same && old.subdivision != updated.subdivision) effects |= static_cast<uint16_t>(ESS::DriverField::SUBDIVISION);
                        if (effects || configChanged) {
                            invalidateDriverAssumptions(a, record.address, effects, nowUs());
                            // These are the newly checked stored codes, not an older cache.
                            a.configuration.operationId = record.operationId;
                        }
                    }
                }
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
    advanceActions(a, nowUs());
    serviceCoordinates(a, nowUs());
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
