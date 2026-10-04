// SPDX-License-Identifier: MIT
// Actual application paths with simulated stopped-state/wire qualifications.
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
    resetHardware(); Serial = FakeSerial(); platformReady = actionTimingQualified = false;
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
void configReply(uint32_t operation, const std::vector<uint8_t>& supplied) {
    const auto token = view(operation).typedRead->step;
    waitTx(operation);
    scheduleReply(std::max(hardware.writeStarted + 8*87 + 1000, hardware.time + 1000), supplied);
    for (unsigned i = 0; i < 25000 && view(operation).pending && view(operation).typedRead->step == token; ++i) step();
    assert(!view(operation).pending || view(operation).typedRead->step != token);
}
uint32_t readConfig(uint16_t direction = 0, uint16_t subdivision = 1000, uint16_t input0 = 1,
                    uint16_t inputPolarity = 0, uint8_t address = 1, uint16_t algorithm = 1, uint16_t encoder = 4000) {
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

uint32_t readControl(uint16_t encoder = 4000, uint16_t algorithm = 3, uint8_t address = 1) {
    ESS::DriverRequest request; request.group = ESS::DriverGroup::CONTROL_SETTINGS;
    uint32_t id = 0;
    assert(host(app).startDriver(app,1,address,ESS::DriverKind::READ,request,id) == Probe::Action::OK);
    reply(id,words({algorithm,encoder,2200,100},address)); reply(id,words({50,100,50,1000},address));
    assert(!view(id).pending && view(id).driverContext->outcome == ESS::DriverOutcome::SUCCESS);
    if (address == 1) assert(app->controlSettings.operationId == id);
    return id;
}
void readIdentity(uint16_t model = 0x4EEA, uint16_t version = 0x0029) {
    uint32_t id = 0;
    assert(host(app).startTypedRead(app,70,1,ESS::ReadKind::IDENTITY,id) == Probe::Action::OK);
    configReply(id,words({model,version,1,0}));
    assert(app->identity.operationId == id);
}
void stationary() {
    auto& m = app->stateCache.blocks[static_cast<uint8_t>(ESS::StateBlock::MOTION)];
    m.valid = true; m.value.target = app->axis.target; m.value.rawMotion = 1;
    m.observedEarliestUs = m.observedLatestUs = hardware.time; m.invalidatedUs = 0;
    app->knownTargets[0] |= 2;
}
ESS::DriverRequest delay(uint16_t ms = 1001) {
    ESS::DriverRequest r; r.group = ESS::DriverGroup::CONTROL_SETTINGS;
    r.fields = static_cast<uint32_t>(ESS::DriverField::LOCK_DELAY);
    r.lockDelayMs = ms; r.configurationGeneration = app->axis.generation; return r;
}
uint32_t start(const ESS::DriverRequest& r) {
    uint32_t id = 0;
    assert(host(app).startDriver(app,2,1,ESS::DriverKind::UPDATE,r,id) == Probe::Action::OK); return id;
}
void safeDelayAndGuards() {
    fresh(); const auto old = readControl(); readIdentity(); stationary();
    assert(!actionTimingQualified);
    ESS::ControlObservation decoded;
    assert(ESS::getControl(*view(old).driverContext,decoded));
    assert(!decoded.algorithmKnown && decoded.raw[0] == 3 && !decoded.percentBaseKnown);
    assert(decoded.encoderResolution == 4000 && !decoded.activeSettingsKnown);
    uint32_t unchanged = 99; const auto writes = hardware.writes;
    auto invalid = delay(20001);
    assert(host(app).startDriver(app,2,1,ESS::DriverKind::UPDATE,invalid,unchanged) == Probe::Action::INVALID);
    invalid = delay(); invalid.fields |= static_cast<uint32_t>(ESS::DriverField::CONFIGURED_ENCODER); invalid.encoderResolution = 4000;
    assert(host(app).startDriver(app,2,1,ESS::DriverKind::UPDATE,invalid,unchanged) == Probe::Action::UNSUPPORTED);
    assert(unchanged == 99 && hardware.writes == writes);
    const auto generation = app->axis.generation;
    Serial.input = "@2 profile ess_rs control set lock-delay 1001\n";
    for (unsigned i = 0; i < 1000 && !Serial.input.empty(); ++i) step();
    const auto id = app->latestOperationId;
    assert(view(id).driverContext->group == ESS::DriverGroup::CONTROL_SETTINGS && axisReserved(*app,1));
    waitTx(id); assert(app->axis.generation > generation && !app->controlSettings.operationId);
    reply(id,{}); reply(id,words({1001}));
    const auto& p = view(id).driverContext->progress[7];
    assert(view(id).driverContext->outcome == ESS::DriverOutcome::SUCCESS && !view(id).driverContext->uncertain);
    assert(!p.acknowledged && p.execution == ActionExecution::UNKNOWN && p.readbackKnown && p.readback == 1001 && !p.activeKnown);
    assert(view(old).driverContext->configurationGeneration == generation && view(old).driverContext->observations[1].raw[10] == 0xE8);
    const auto settledWrites = hardware.writes; for (unsigned i = 0; i < 100; ++i) step();
    assert(hardware.writes == settledWrites);
    for (unsigned i = 0; i < 1000 && !findRecord(*app,id)->delivered; ++i) step();
    assert(findRecord(*app,id)->delivered && Serial.output.find("\"type\":\"control\"") != std::string::npos);
}
void modelAndStaleness() {
    for (unsigned fault = 0; fault < 4; ++fault) {
        fresh(); readControl(); readIdentity(fault == 0 ? 0x1234 : 0x4EEA, fault == 1 ? 0x1234 : 0x29); stationary();
        auto r = delay();
        if (fault == 2) { advanceHardware(hardware.time + 5000001); stationary(); }
        if (fault == 3) ++r.configurationGeneration;
        uint32_t unchanged = 99; const auto writes = hardware.writes;
        assert(host(app).startDriver(app,2,1,ESS::DriverKind::UPDATE,r,unchanged) == Probe::Action::INVALID);
        assert(unchanged == 99 && hardware.writes == writes);
    }
}
void changedScaleAndHistoricalContext() {
    fresh(); const auto old = readControl();
    app->axis.units.encoder.countsPerUnit = UnitScale(4000,1,ScaleSource::QUALIFIED);
    app->axis.units.encoder.sourceId = 99; app->axis.encoderOriginKnown = true; app->axis.originKnown = true;
    const auto generation = app->axis.generation; readControl(8000);
    assert(app->axis.generation > generation && !app->axis.units.encoder.countsPerUnit.numerator && !app->axis.units.encoder.sourceId);
    assert(!app->axis.encoderOriginKnown && !app->axis.originKnown);
    assert(app->controlSettings.configurationGeneration == app->axis.generation);
    assert(view(old).driverContext->observations[0].raw[5] == 0x0F && view(old).driverContext->observations[0].raw[6] == 0xA0);
    // A standard config read must apply the same encoder effects path.
    app->axis.units.encoder.countsPerUnit = UnitScale(8000,1,ScaleSource::QUALIFIED);
    app->axis.units.encoder.sourceId = 99;
    readConfig(0,1000,1,0,1,3,4000);
    assert(!app->axis.units.encoder.countsPerUnit.numerator && !app->controlSettings.operationId);
    assert(app->configuration.raw.encoderResolution == 4000);
    // Also detect changes between two config reads when no control cache exists.
    app->axis.units.encoder.countsPerUnit = UnitScale(4000,1,ScaleSource::QUALIFIED);
    readConfig(0,1000,1,0,1,3,8000);
    assert(!app->axis.units.encoder.countsPerUnit.numerator);
}
void uncertaintyAndCancel() {
    fresh(); readControl(); readIdentity(); stationary(); auto id = start(delay());
    reply(id,{}); reply(id,words({1000}));
    assert(view(id).driverContext->outcome == ESS::DriverOutcome::READBACK_MISMATCH && view(id).driverContext->uncertain);
    fresh(); readControl(); readIdentity(); stationary(); id = start(delay()); waitTx(id);
    assert(host(app).cancel(app,id) == Probe::Action::OK);
    const auto writes = hardware.writes;
    for (unsigned i = 0; i < 40000 && view(id).pending; ++i) step();
    assert(!view(id).pending && view(id).driverContext->uncertain && hardware.writes == writes);
    fresh(); readControl(); readIdentity(); stationary(); id = start(delay());
    assert(host(app).cancel(app,id) == Probe::Action::OK);
    const auto unsent = hardware.writes;
    for (unsigned i = 0; i < 100 && view(id).pending; ++i) step();
    assert(!view(id).pending && !view(id).driverContext->effects && hardware.writes == unsent);
}
void independentCachesAndPressure() {
    fresh(); const auto original = readControl();
    const auto immutable = app->controlSettings; const auto second = readControl(8000,1,2);
    assert(app->controlSettings.operationId == immutable.operationId && view(second).driverContext->target.address == 2);
    while (app->latestOperationId < REQUEST_CAPACITY) readControl();
    const auto writes = hardware.writes; uint32_t unchanged = 99;
    ESS::DriverRequest r; r.group = ESS::DriverGroup::CONTROL_SETTINGS;
    assert(host(app).startDriver(app,50,1,ESS::DriverKind::READ,r,unchanged) == Probe::Action::RESULTS_FULL);
    assert(unchanged == 99 && hardware.writes == writes && view(original).driverContext->outcome == ESS::DriverOutcome::SUCCESS);
}
void recoveryWithActiveWriteAndFullResults() {
    fresh(); const auto original = readControl(); readIdentity();
    while (app->latestOperationId < REQUEST_CAPACITY - 2) readControl();
    stationary(); const auto update = start(delay()); waitTx(update);
    assert(app->runner.transmitEnabled() && hardware.de == 1);
    uint32_t queued = 0;
    assert(host(app).startProbe(app,98,2,queued) == Probe::Action::OK);
    const auto writes = hardware.writes, generation = app->bindingGeneration;
    Serial.input = "@99 recover\n";
    for (unsigned i = 0; i < 100 && !Serial.input.empty(); ++i) step();
    assert(Serial.input.empty() && app->owner.recovering() && app->bindingGeneration == generation + 1);
    const auto recovery = app->recovery.operationId;
    assert(recovery != update && recovery != queued && view(recovery).recovery);
    for (unsigned i = 0; i < 80000 && (app->owner.recovering() || view(update).pending || view(queued).pending); ++i) step();
    assert(!app->owner.recovering() && !view(update).pending && !view(queued).pending);
    const auto& interrupted = *view(update).driverContext;
    assert(interrupted.outcome == ESS::DriverOutcome::CANCELLED && interrupted.uncertain);
    assert(interrupted.effects == static_cast<uint32_t>(ESS::DriverField::LOCK_DELAY));
    assert(interrupted.progress[7].execution == ActionExecution::UNKNOWN && !interrupted.progress[7].readbackKnown);
    assert(view(queued).probe.outcome == Rtu::Outcome::CANCELLED && view(queued).probe.transport.txAccepted == 0);
    assert(view(recovery).recoveryResult.outcome == Rtu::RecoveryOutcome::RECOVERED);
    assert(view(original).driverContext->outcome == ESS::DriverOutcome::SUCCESS && hardware.writes == writes);
    uint32_t unchanged = 99; ESS::DriverRequest r; r.group = ESS::DriverGroup::CONTROL_SETTINGS;
    assert(host(app).startDriver(app,100,1,ESS::DriverKind::READ,r,unchanged) == Probe::Action::RESULTS_FULL && unchanged == 99);
    for (unsigned i = 0; i < 100; ++i) step();
    assert(hardware.writes == writes && view(update).driverContext->uncertain && !axisReserved(*app,1));
}
void failedRefreshInvalidatesChangedScale() {
    for (bool changed : {false, true}) {
        fresh(); const auto original = readControl();
        app->axis.units.encoder.countsPerUnit = UnitScale(4000, 1, ScaleSource::QUALIFIED);
        app->axis.units.encoder.sourceId = 1;
        app->axis.originKnown = true; app->movePrerequisites.readinessQualified = true;
        const auto generation = app->axis.generation;
        uint32_t id = 0; ESS::DriverRequest request; request.group = ESS::DriverGroup::CONTROL_SETTINGS;
        assert(host(app).startDriver(app, 90, 1, ESS::DriverKind::READ, request, id) == Probe::Action::OK);
        reply(id, words({3, static_cast<uint16_t>(changed ? 8000 : 4000), 2200, 100}));
        auto malformed = words({50, 100, 50, 1000}); malformed.back() ^= 1;
        reply(id, malformed);
        assert(!view(id).pending && view(id).driverContext->state == ReadState::FAILED);
        assert(app->controlSettings.raw[1] == 4000); // Failed refresh cannot publish a partial snapshot.
        if (changed) {
            assert(!app->controlSettings.operationId && app->axis.generation > generation);
            assert(!app->axis.units.encoder.countsPerUnit.numerator && !app->axis.originKnown && !app->movePrerequisites.readinessQualified);
        } else {
            assert(app->controlSettings.operationId == original && app->axis.generation == generation);
            assert(app->axis.originKnown && app->movePrerequisites.readinessQualified);
        }
        const auto after = app->axis.generation;
        for (unsigned i = 0; i < 10; ++i) step();
        assert(app->axis.generation == after && view(original).driverContext->outcome == ESS::DriverOutcome::SUCCESS);
    }
}
} // namespace
int main() {
    safeDelayAndGuards(); modelAndStaleness(); changedScaleAndHistoricalContext(); uncertaintyAndCancel(); independentCachesAndPressure();
    recoveryWithActiveWriteAndFullResults(); failedRefreshInvalidatesChangedScale();
    if (app) { app->~App(); std::free(app); app = nullptr; }
}
