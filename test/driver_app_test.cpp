// SPDX-License-Identifier: MIT
// Actual application paths with simulated stopped-state/wire qualifications.
#include "../examples/probe_cli/ProbeApp.cpp"
#include "../examples/probe_cli/ArduinoPlatform.cpp"
#include "../examples/probe_cli/main.cpp"
#include <MotorControlRS/profiles/ess_rs/Registers.h>
#include <cassert>
#include <cstdlib>
#include <string>
#include <vector>
FakeSerial Serial;
namespace {
using namespace MotorControlRS;
void fresh() {
    if (app) { app->~App(); std::free(app); app = nullptr; }
    uart.~Esp32S3Uart(); new (&uart) Esp32S3Uart;
    resetHardware(); Serial = FakeSerial(); platformReady = writeResponseConfirmed = false;
    setup(); assert(app && !hardware.writes);
    hardware.txCharacterUs = 87; assert(uart.startCapture(20, timing().holdUs));
}
void step(uint32_t us = 10) { advanceHardware(hardware.time + us); loop(); }
Probe::ResultView view(uint32_t operation) {
    Probe::ResultView v; assert(host(app).result(app, operation, v)); return v;
}
std::vector<uint8_t> crc(std::vector<uint8_t> bytes) {
    const uint16_t c = ESS::calcCrc16(bytes.data(), bytes.size());
    bytes.push_back(static_cast<uint8_t>(c)); bytes.push_back(static_cast<uint8_t>(c >> 8)); return bytes;
}
std::vector<uint8_t> words(std::initializer_list<uint16_t> values, uint8_t address = 1) {
    std::vector<uint8_t> bytes = {address,3,static_cast<uint8_t>(values.size()*2)};
    for (const auto value : values) { bytes.push_back(static_cast<uint8_t>(value >> 8)); bytes.push_back(static_cast<uint8_t>(value)); }
    return crc(bytes);
}
void waitTx(uint32_t operation) {
    for (unsigned i = 0; i < 25000 && !app->owner.txAccepted(findRecord(*app,operation)->requestId); ++i) step();
    assert(app->owner.txAccepted(findRecord(*app,operation)->requestId));
}
void reply(uint32_t operation, const std::vector<uint8_t>& supplied) {
    const auto token = view(operation).driverContext->step;
    waitTx(operation);
    const auto response = supplied.empty() ? hardware.tx : supplied;
    scheduleReply(std::max(hardware.writeStarted + 8*87 + 1000, hardware.time + 1000), response);
    for (unsigned i = 0; i < 25000 && view(operation).pending && view(operation).driverContext->step == token; ++i) step();
    assert(!view(operation).pending || view(operation).driverContext->step != token);
}
uint32_t readSettings(uint16_t order = 0, uint16_t direction = 0, uint16_t subdivision = 1000,
                      uint8_t address = 1, uint16_t positionMode = 0, uint16_t interruption = 1) {
    uint32_t id = 0;
    assert(host(app).startDriver(app,1,address,ESS::DriverKind::READ,ESS::DriverRequest(),id) == Probe::Action::OK);
    reply(id,words({direction,subdivision},address)); reply(id,words({1,0,order},address));
    reply(id,words({0x1234,0x5678,0xABCD,0xEF01},address)); reply(id,words({positionMode,interruption},address));
    assert(!view(id).pending && view(id).driverContext->outcome == ESS::DriverOutcome::SUCCESS);
    if (address == app->axis.target.address) assert(app->driverSettings.operationId == id);
    return id;
}
void configReply(uint32_t operation, const std::vector<uint8_t>& supplied) {
    const auto token = view(operation).typedRead->step;
    waitTx(operation);
    scheduleReply(std::max(hardware.writeStarted + 8*87 + 1000, hardware.time + 1000), supplied);
    for (unsigned i = 0; i < 25000 && view(operation).pending && view(operation).typedRead->step == token; ++i) step();
    assert(!view(operation).pending || view(operation).typedRead->step != token);
}
uint32_t readConfig(uint16_t direction = 0, uint16_t subdivision = 1000, uint16_t input0 = 1,
                    uint16_t inputPolarity = 0, uint8_t address = 1,
                    uint16_t algorithm = 1, uint16_t encoder = 4000) {
    uint32_t id = 0;
    assert(host(app).startTypedRead(app,70,address,ESS::ReadKind::CONFIG,id) == Probe::Action::OK);
    configReply(id,words({direction,subdivision},address)); configReply(id,words({0,0,0},address));
    configReply(id,words({1,0,0},address)); configReply(id,words({inputPolarity,input0,2,3,0},address));
    configReply(id,words({algorithm,encoder},address));
    for (unsigned i = 0; i < 100; ++i) step();
    assert(!view(id).pending && view(id).typedRead->state == ReadState::SUCCEEDED);
    if (address == app->axis.target.address) assert(app->configuration.operationId == id);
    return id;
}
void qualify() {
    // This fixture's qualification never propagates to production or COM13.
    writeResponseConfirmed = true; app->driverInputsQualified = true; app->knownTargets[0] |= 2;
    auto& motion = app->stateCache.blocks[static_cast<uint8_t>(ESS::StateBlock::MOTION)];
    motion.valid = true; motion.value.target = app->axis.target; motion.value.rawMotion = 1;
    motion.value.enabled = motion.value.inPosition = true;
    motion.observedEarliestUs = motion.observedLatestUs = hardware.time;
    app->axis.units.commandStepsPerMotorTurn = UnitScale(1000,1,ScaleSource::QUALIFIED);
    app->axis.originKnown = app->axis.softLimitsKnown = true;
    app->coordinateReference.target = app->axis.target; app->coordinateReference.nativeKnown = true;
    app->coordinateReference.configurationGeneration = app->axis.generation;
    app->coordinateReference.observedUs = hardware.time; app->coordinateReference.maximumAgeUs = 5000000;
}
ESS::DriverRequest update() {
    ESS::DriverRequest r; r.fields = static_cast<uint16_t>(ESS::DriverField::DIRECTION) |
        static_cast<uint16_t>(ESS::DriverField::SUBDIVISION);
    r.direction = ESS::DefaultDirection::REVERSED; r.subdivision = 1200;
    r.configurationGeneration = app->axis.generation; return r;
}
uint32_t admit(const ESS::DriverRequest& request) {
    Serial.input = "@2 profile ess_rs driver set direction 1 subdivision 1200\n";
    for (unsigned i = 0; i < 1000 && !Serial.input.empty(); ++i) step();
    const uint32_t id = app->latestOperationId;
    assert(view(id).driverContext->request.fields == request.fields);
    assert(view(id).driverContext && axisReserved(*app,1)); return id;
}
void gatesAndReadContext() {
    fresh(); const auto read = readSettings(1);
    assert(app->driverSettings.positiveBits == 0x56781234);
    const auto immutable = app->driverSettings;
    assert(app->driverSettings.negativeBits == 0xEF01ABCD);
    uint32_t untouched = 99;
    auto request = update(); request.fields |= static_cast<uint16_t>(ESS::DriverField::POSITIVE_LIMIT);
    const auto writes = hardware.writes;
    assert(host(app).startDriver(app,2,1,ESS::DriverKind::UPDATE,request,untouched) == Probe::Action::UNSUPPORTED);
    assert(untouched == 99 && hardware.writes == writes);
    qualify(); request = update(); request.subdivision = 399;
    assert(host(app).startDriver(app,2,1,ESS::DriverKind::UPDATE,request,untouched) == Probe::Action::INVALID);
    request = update(); writeResponseConfirmed = false;
    assert(host(app).startDriver(app,2,1,ESS::DriverKind::UPDATE,request,untouched) == Probe::Action::OK);
    assert(hardware.writes == writes);
    assert(view(read).driverContext->configurationGeneration == immutable.configurationGeneration);
}
void successAndEffects() {
    fresh(); readSettings(); qualify();
    app->configuration.operationId = 99; app->configuration.target = app->axis.target;
    app->configuration.wordOrderKnown = true;
    const auto generation = app->axis.generation; const auto id = admit(update());
    waitTx(id);
    assert(app->axis.generation > generation && !app->axis.originKnown && !app->coordinateReference.nativeKnown);
    assert(!app->axis.units.commandStepsPerMotorTurn.numerator && !app->commandPolarityKnown);
    assert(!app->configuration.operationId && !app->driverSettings.operationId);
    assert(view(id).driverContext->prerequisites.previous.raw[0] == 0);
    uint8_t bytes[8]; Rtu::BusRequest competing; competing.expected.address = competing.expected.target = 1;
    competing.expected.targetGeneration = app->bindingGeneration; competing.expected.function = 6;
    competing.expected.first = ESS::Registers::SUBDIVISION; competing.expected.count = 1;
    competing.expected.value = 800;
    competing.wire.bytes = bytes; competing.wire.length = ESS::buildWriteSingleRegister(1,0x11,800,bytes,8);
    assert(checkAxisWrite(*app,competing) == Probe::Action::AXIS_CONFLICT);
    reply(id,{}); reply(id,words({1})); reply(id,{}); reply(id,words({1200}));
    const auto& result = *view(id).driverContext;
    assert(result.outcome == ESS::DriverOutcome::SUCCESS && !result.uncertain && !axisReserved(*app,1));
    assert(result.progress[0].acknowledged && result.progress[0].readbackKnown && !result.progress[0].activeKnown);
    assert(result.progress[1].readback == 1200 && result.configurationGeneration == generation);
    const auto writes = hardware.writes;
    for (unsigned i = 0; i < 100; ++i) step();
    assert(hardware.writes == writes && result.progress[1].readback == 1200);
    assert(host(app).cancel(app,id) == Probe::Action::ALREADY_TERMINAL);
    for (unsigned i = 0; i < 2000 && !findRecord(*app,id)->delivered; ++i) step();
    assert(findRecord(*app,id)->delivered);
    assert(host(app).release(app,id) == Probe::Action::OK);
}
void failuresAndCancellation() {
    fresh(); readSettings(); qualify(); auto id = admit(update());
    reply(id,{}); reply(id,words({0}));
    assert(view(id).driverContext->outcome == ESS::DriverOutcome::READBACK_MISMATCH);
    assert(view(id).driverContext->progress[0].acknowledged && view(id).driverContext->uncertain);
    assert(!view(id).driverContext->progress[1].acknowledged && !app->configuration.operationId);
    fresh(); readSettings(); qualify(); id = admit(update());
    reply(id,{}); reply(id,words({1})); waitTx(id);
    const auto writes = hardware.writes;
    for (unsigned i = 0; i < 40000 && view(id).pending; ++i) step();
    assert(!view(id).pending && view(id).driverContext->uncertain && hardware.writes == writes);
    assert(view(id).driverContext->progress[0].readbackKnown && !view(id).driverContext->progress[1].acknowledged);
    assert(app->owner.needsRecovery());
    fresh(); readSettings(); qualify(); id = admit(update());
    const auto generation = app->axis.generation;
    assert(host(app).cancel(app,id) == Probe::Action::OK);
    for (unsigned i = 0; i < 100 && view(id).pending; ++i) step();
    assert(!view(id).pending && !view(id).driverContext->effects && app->axis.generation == generation);
    fresh(); readSettings(); qualify(); id = admit(update()); waitTx(id);
    assert(host(app).cancel(app,id) == Probe::Action::OK);
    for (unsigned i = 0; i < 40000 && view(id).pending; ++i) step();
    assert(!view(id).pending && view(id).driverContext->uncertain && view(id).driverContext->effects);
}
void staleContinuation() {
    fresh(); readSettings(); qualify(); const auto id = admit(update());
    const auto writes = hardware.writes;
    invalidateAxis(*app);
    for (unsigned i = 0; i < 100 && view(id).pending; ++i) step();
    assert(!view(id).pending && hardware.writes == writes && !view(id).driverContext->effects);
    fresh(); uint32_t state = 0;
    assert(host(app).startTypedRead(app,1,1,ESS::ReadKind::STATE,state) == Probe::Action::OK);
    // Simulate configuration invalidation while a state read is queued.
    invalidateAxis(*app);
    for (unsigned i = 0; i < 100; ++i) step();
    assert(!view(state).pending && !app->stateCache.blocks[0].valid && !hardware.writes);
}
void externalChangesAndRecovery() {
    fresh(); const auto original = readSettings(); qualify();
    const auto generation = app->axis.generation;
    readSettings(1,1,1600);
    assert(app->axis.generation > generation && !app->commandPolarityKnown && !app->axis.originKnown);
    assert(!app->axis.units.commandStepsPerMotorTurn.numerator);
    assert(app->driverSettings.configurationGeneration == app->axis.generation);
    assert(app->driverSettings.raw[0] == 1 && app->driverSettings.positiveBits == 0x56781234);
    assert(view(original).driverContext->observations[0].raw[3] == 0); // Historical direction remains zero.
    fresh(); readSettings(); qualify(); const auto id = admit(update()); waitTx(id);
    const auto writes = hardware.writes; uint32_t recovery = 0;
    assert(host(app).recover(app,50,recovery) == Probe::Action::OK);
    for (unsigned i = 0; i < 40000 && view(id).pending; ++i) step();
    assert(!view(id).pending && view(id).driverContext->uncertain && hardware.writes == writes);
    assert(view(id).driverContext->prerequisites.previous.raw[0] == 0 && view(recovery).recovery);
    fresh(); readSettings(); qualify(); const auto interrupted = admit(update());
    ActionRequest stop; stop.kind = ActionKind::STOP; stop.stop.behavior = StopBehavior::CONFIGURED_DECELERATION;
    uint32_t stopping = 0;
    assert(host(app).startAction(app,60,1,stop,stopping) == Probe::Action::OK);
    for (unsigned i = 0; i < 1000 && view(interrupted).pending; ++i) step();
    assert(!view(interrupted).pending && !view(interrupted).driverContext->effects);
    assert(findRecord(*app,interrupted)->interruptedByStop && axisReserved(*app,1));
}
void configurationReconcilesDriverBaseline() {
    fresh(); const auto original = readSettings(); qualify();
    const auto generation = app->axis.generation;
    assert(!app->configuration.operationId);
    readConfig(1,1600);
    assert(app->axis.generation > generation && !app->axis.originKnown && !app->coordinateReference.nativeKnown);
    assert(!app->commandPolarityKnown && !app->axis.units.commandStepsPerMotorTurn.numerator);
    assert(!app->driverSettings.operationId && !app->driverInputsQualified);
    assert(app->configuration.raw.direction == 1 && app->configuration.raw.subdivision == 1600);
    assert(view(original).driverContext->configurationGeneration == generation);
    assert(view(original).driverContext->observations[0].raw[3] == 0);
}
uint32_t readSharedGroup(ESS::DriverGroup group, bool changed) {
    if (group == ESS::DriverGroup::DRIVE) return readSettings(0,changed ? 1 : 0,changed ? 1600 : 1000);
    ESS::DriverRequest request; request.group = group;
    uint32_t id = 0;
    assert(host(app).startDriver(app,1,1,ESS::DriverKind::READ,request,id) == Probe::Action::OK);
    if (group == ESS::DriverGroup::CONTROL_SETTINGS) {
        reply(id,words({static_cast<uint16_t>(changed ? 3 : 1),static_cast<uint16_t>(changed ? 8000 : 4000),2200,100}));
        reply(id,words({50,100,50,1000}));
        assert(app->controlSettings.operationId == id);
    } else {
        assert(group == ESS::DriverGroup::IO);
        reply(id,words({static_cast<uint16_t>(changed ? 1 : 0),static_cast<uint16_t>(changed ? 17 : 1),2,3,0}));
        reply(id,words({0,0,0})); reply(id,words({0}));
        assert(app->ioSettings.operationId == id);
    }
    assert(!view(id).pending && view(id).driverContext->outcome == ESS::DriverOutcome::SUCCESS);
    return id;
}
std::vector<uint8_t> retainedBytes(uint32_t operation) {
    const auto result = view(operation);
    std::vector<uint8_t> bytes;
    const uint8_t steps = result.typedRead ? result.typedRead->completedSteps : result.driverContext->completedSteps;
    for (uint8_t i = 0; i < steps; ++i) {
        const uint8_t* raw = result.typedRead ? result.typedRead->observations[i].raw : result.driverContext->observations[i].raw;
        const std::size_t length = result.typedRead ? result.typedRead->observations[i].length : result.driverContext->observations[i].length;
        bytes.insert(bytes.end(),raw,raw + length);
    }
    return bytes;
}
void hostChangesPreserveCrossReadBaselines() {
    // Freshness expires with the host session; retained raw settings remain a
    // comparison baseline when the next refresh uses the other public API.
    for (const auto group : {ESS::DriverGroup::DRIVE,ESS::DriverGroup::CONTROL_SETTINGS,ESS::DriverGroup::IO})
        for (const bool configFirst : {false,true})
            for (const bool changed : {true,false}) {
                fresh();
                const uint32_t original = configFirst ? readConfig() : readSharedGroup(group,false);
                const auto historical = retainedBytes(original);
                const auto originalSerial = findRecord(*app,original)->serialGeneration;
                qualify();
                app->axis.units.encoder.countsPerUnit = UnitScale(4000,1,ScaleSource::QUALIFIED);
                app->axis.units.encoder.sourceId = 1;
                app->axis.originSource = ScaleSource::QUALIFIED;
                app->axis.supportedRelativeBases = 1;
                app->axis.softLimitsKnown = false;
                const auto generation = app->axis.generation, binding = app->bindingGeneration;
                PositionRequest intent; intent.value = Rational(10); intent.configurationGeneration = generation;
                PreparedTarget prepared; assert(preparePosition(intent,app->axis,nullptr,prepared));
                Probe::HostRequest change; change.tuple.baud = 9600;
                Probe::HostSnapshot serial;
                assert(host(app).hostSerial(app,&change,serial) == Probe::Action::OK);
                Probe::HostRequest restore; restore.restore = true;
                assert(host(app).hostSerial(app,&restore,serial) == Probe::Action::OK);
                assert(app->axis.generation == generation && app->bindingGeneration == binding);
                assert(app->axis.originKnown && !app->driverInputsQualified && writeResponseConfirmed);
                assert(!app->configuration.operationId && !app->driverSettings.operationId &&
                       !app->controlSettings.operationId && !app->ioSettings.operationId);
                if (configFirst) readSharedGroup(group,changed);
                else readConfig(changed && group == ESS::DriverGroup::DRIVE ? 1 : 0,
                                changed && group == ESS::DriverGroup::DRIVE ? 1600 : 1000,
                                changed && group == ESS::DriverGroup::IO ? 17 : 1,
                                changed && group == ESS::DriverGroup::IO ? 1 : 0,1,
                                changed && group == ESS::DriverGroup::CONTROL_SETTINGS ? 3 : 1,
                                changed && group == ESS::DriverGroup::CONTROL_SETTINGS ? 8000 : 4000);
                assert(retainedBytes(original) == historical);
                assert(findRecord(*app,original)->serialGeneration == originalSerial && serial.generation > originalSerial);
                assert(app->bindingGeneration == binding);
                if (changed) {
                    assert(app->axis.generation > generation && !app->axis.originKnown && !app->coordinateReference.nativeKnown);
                    assert(prepared.configurationGeneration != app->axis.generation);
                } else {
                    assert(app->axis.generation == generation && app->axis.originKnown && app->coordinateReference.nativeKnown);
                    assert(prepared.configurationGeneration == app->axis.generation);
                }
                assert(app->commandPolarityKnown == !(changed && group == ESS::DriverGroup::DRIVE));
                assert(app->axis.units.commandStepsPerMotorTurn.numerator == (changed && group == ESS::DriverGroup::DRIVE ? 0u : 1000u));
                assert(app->axis.units.encoder.countsPerUnit.numerator == (changed && group == ESS::DriverGroup::CONTROL_SETTINGS ? 0u : 4000u));
            }
}
void changedInputsInvalidateQualification() {
    for (unsigned change = 0; change < 2; ++change) {
        fresh(); readConfig(); qualify();
        const auto generation = app->axis.generation;
        readConfig(0,1000,change == 0 ? 17 : 1,change == 1 ? 1 : 0);
        assert(app->axis.generation > generation && !app->driverInputsQualified);
        assert(!app->axis.originKnown && !app->coordinateReference.nativeKnown);
        readSettings();
        const auto writes = hardware.writes; uint32_t unchanged = 99;
        assert(host(app).startDriver(app,71,1,ESS::DriverKind::UPDATE,update(),unchanged) == Probe::Action::INVALID);
        assert(unchanged == 99 && hardware.writes == writes);
    }
}
void secondaryReadsPreserveAxisBaseline() {
    for (unsigned change = 0; change < 2; ++change) {
        fresh(); const auto original = readSettings(); qualify();
        const auto generation = app->axis.generation;
        const auto secondary = readSettings(0,0,1000,2);
        assert(app->driverSettings.operationId == original && app->driverSettings.target.address == 1);
        assert(view(secondary).driverContext->target.address == 2);
        readSettings(0,0,1000,1,change == 0 ? 1 : 0,change == 1 ? 0 : 1);
        assert(app->axis.generation > generation && !app->axis.originKnown && !app->driverInputsQualified);
        assert(view(original).driverContext->observations[3].raw[4] == 0);
    }
    fresh(); const auto original = readConfig(); qualify();
    const auto generation = app->axis.generation;
    const auto secondary = readConfig(1,1600,17,1,2);
    assert(app->configuration.operationId == original && app->configuration.target.address == 1);
    assert(app->axis.generation == generation && app->axis.originKnown && app->driverInputsQualified);
    assert(view(secondary).typedRead->target.address == 2);
    readConfig(0,1000,17);
    assert(app->axis.generation > generation && !app->driverInputsQualified);
}
void olderConfigCannotReplaceNewerDriver() {
    fresh(); qualify(); uint32_t config = 0, driver = 0;
    assert(host(app).startTypedRead(app,70,1,ESS::ReadKind::CONFIG,config) == Probe::Action::OK);
    assert(host(app).startDriver(app,71,1,ESS::DriverKind::READ,ESS::DriverRequest(),driver) == Probe::Action::OK);
    // Interleaved real owner transactions: CONFIG's shared fields were read
    // before DRIVER's, but CONFIG has one extra window and finishes later.
    configReply(config,words({0,1000})); reply(driver,words({1,1600}));
    configReply(config,words({0,0,0})); reply(driver,words({1,0,0}));
    configReply(config,words({1,0,0})); reply(driver,words({0x1234,0x5678,0xABCD,0xEF01}));
    configReply(config,words({0,1,2,3,0})); reply(driver,words({0,1}));
    assert(app->driverSettings.operationId == driver);
    const auto generation = app->axis.generation;
    configReply(config,words({1,4000}));
    for (unsigned i = 0; i < 100; ++i) step();
    assert(!app->configuration.operationId && app->driverSettings.operationId == driver);
    assert(app->axis.generation == generation);
    ESS::ConfigObservation historical;
    assert(ESS::getConfig(*view(config).typedRead,historical));
    assert(historical.raw.direction == 0 && historical.raw.subdivision == 1000);
    assert(app->driverSettings.raw[0] == 1 && app->driverSettings.raw[1] == 1600);
}
void retentionPressure() {
    fresh();
    uint32_t ids[REQUEST_CAPACITY];
    for (std::size_t i = 0; i < REQUEST_CAPACITY; ++i) ids[i] = readSettings();
    const auto writes = hardware.writes; uint32_t untouched = 99;
    assert(host(app).startDriver(app,50,1,ESS::DriverKind::READ,ESS::DriverRequest(),untouched) == Probe::Action::RESULTS_FULL);
    assert(untouched == 99 && hardware.writes == writes);
    for (const auto id : ids) {
        const auto& retained = *view(id).driverContext;
        assert(retained.outcome == ESS::DriverOutcome::SUCCESS && retained.observations[2].receivedLength == 13);
    }
}
void backpressureAndPolarityParity() {
    fresh(); readSettings(); qualify(); const auto id = admit(update());
    Serial.writeCapacity = 0;
    reply(id,{}); reply(id,words({1})); reply(id,{}); reply(id,words({1200}));
    const auto writes = hardware.writes;
    for (unsigned i = 0; i < 1000; ++i) step();
    assert(!view(id).pending && hardware.writes == writes && app->outputCount);
    assert(view(id).driverContext->progress[1].readback == 1200);
    Serial.writeCapacity = 4096;
    for (unsigned i = 0; i < 1000; ++i) step();
    assert(findRecord(*app,id)->delivered && !app->outputCount);
    assert(Serial.output.find("\"driver_kind\":\"update\"") != std::string::npos);
    // Re-entering a scale cannot silently reconcile a possibly changed sign.
    app->axis.units.commandStepsPerMotorTurn = UnitScale(1000,1,ScaleSource::ASSUMED);
    MoveRequest move; move.position.frame = CoordinateFrame::MOTOR; move.position.value = Rational(1);
    move.speedRpm = 30; move.ramp = MoveRamp::VERIFIED_CONFIGURED;
    VelocityRequest velocity; velocity.frame = CoordinateFrame::MOTOR; velocity.durationUs = 100000;
    uint32_t untouched = 99;
    assert(host(app).startMove(app,60,1,move,untouched) == Probe::Action::UNAVAILABLE);
    assert(host(app).startVelocity(app,61,1,velocity,untouched) == Probe::Action::UNAVAILABLE);
    assert(untouched == 99 && hardware.writes == writes);
}
} // namespace
int main() {
    gatesAndReadContext(); successAndEffects(); failuresAndCancellation(); staleContinuation(); externalChangesAndRecovery();
    configurationReconcilesDriverBaseline(); changedInputsInvalidateQualification(); secondaryReadsPreserveAxisBaseline();
    hostChangesPreserveCrossReadBaselines();
    olderConfigCannotReplaceNewerDriver();
    retentionPressure();
    backpressureAndPolarityParity();
    if (app) { app->~App(); std::free(app); app = nullptr; }
    return 0;
}
