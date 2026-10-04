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

uint32_t readIo(uint16_t x0 = 1, uint16_t inputPolarity = 0, uint16_t y0 = 0, uint16_t custom = 0,
                uint8_t address = 1) {
    ESS::DriverRequest request; request.group = ESS::DriverGroup::IO;
    uint32_t id = 0;
    assert(host(app).startDriver(app, 1, address, ESS::DriverKind::READ, request, id) == Probe::Action::OK);
    reply(id, words({inputPolarity,x0,2,3,0},address));
    reply(id, words({0,y0,0},address)); reply(id, words({custom},address));
    assert(!view(id).pending && view(id).driverContext->outcome == ESS::DriverOutcome::SUCCESS);
    if (address == 1) assert(app->ioSettings.operationId == id);
    return id;
}
void stateEvidence(uint16_t inputs = 0, uint16_t outputs = 0) {
    for (const auto index : {ESS::StateBlock::MOTION, ESS::StateBlock::IO}) {
        auto& block = app->stateCache.blocks[static_cast<uint8_t>(index)];
        block.valid = true; block.value.target = app->axis.target;
        block.value.rawMotion = 1; block.value.enabled = block.value.inPosition = true;
        block.value.rawInputs = inputs; block.value.rawOutputs = outputs;
        block.observedEarliestUs = block.observedLatestUs = hardware.time;
        block.invalidatedUs = 0;
    }
    app->knownTargets[0] |= 2;
}
ESS::DriverRequest disable(uint8_t terminal = 0) {
    ESS::DriverRequest r;
    assert(ESS::prepareInputFunction(r,terminal,ESS::InputFunction::UNDEFINED));
    r.configurationGeneration = app->axis.generation; return r;
}
uint32_t update(const ESS::DriverRequest& request) {
    uint32_t id = 0;
    assert(host(app).startDriver(app,2,1,ESS::DriverKind::UPDATE,request,id) == Probe::Action::OK);
    return id;
}
void safeDisableAndReadback() {
    fresh(); assert(!actionTimingQualified); const auto old = readIo(); stateEvidence();
    app->axis.originKnown = true; app->coordinateReference.nativeKnown = true;
    app->coordinateReference.target = app->axis.target;
    app->coordinateReference.configurationGeneration = app->axis.generation;
    app->coordinateReference.observedUs = hardware.time; app->coordinateReference.maximumAgeUs = 5000000;
    const auto generation = app->axis.generation; const auto id = update(disable());
    waitTx(id);
    assert(app->axis.generation > generation && !app->axis.originKnown && !app->coordinateReference.nativeKnown);
    assert(!app->ioSettings.operationId && !app->configuration.operationId);
    assert(view(id).driverContext->prerequisites.previous.raw[8] == 1);
    reply(id,{}); // An indistinguishable FC06 echo is not promoted to source proof.
    const auto& progress = view(id).driverContext->progress[8];
    assert(!progress.acknowledged && progress.execution == ActionExecution::UNKNOWN);
    assert(view(id).driverContext->observations[0].responseConfirmed == false);
    reply(id,words({0}));
    assert(view(id).driverContext->outcome == ESS::DriverOutcome::SUCCESS && !view(id).driverContext->uncertain);
    assert(progress.readbackKnown && progress.readback == 0 && !progress.activeKnown && !progress.acknowledged);
    assert(view(old).driverContext->configurationGeneration == generation && view(old).driverContext->observations[0].raw[6] == 1);
    const auto writes = hardware.writes;
    for (unsigned i = 0; i < 100; ++i) step();
    assert(hardware.writes == writes && !axisReserved(*app,1));
    assert(host(app).cancel(app,id) == Probe::Action::ALREADY_TERMINAL);
}
void scopedGates() {
    fresh(); readIo(); stateEvidence(); const auto writes = hardware.writes; uint32_t unchanged = 99;
    app->inputWiring[0] = InputWiring::UNKNOWN;
    assert(host(app).startDriver(app,2,1,ESS::DriverKind::UPDATE,disable(),unchanged) == Probe::Action::INVALID);
    app->inputWiring[0] = InputWiring::CONNECTED;
    assert(host(app).startDriver(app,2,1,ESS::DriverKind::UPDATE,disable(),unchanged) == Probe::Action::INVALID);
    app->inputWiring[0] = InputWiring::UNCONNECTED; app->inputWiring[3] = InputWiring::UNKNOWN;
    auto request = disable(); request.inputFunctions[0] = ESS::InputFunction::RELEASE_MOTOR;
    assert(host(app).startDriver(app,2,1,ESS::DriverKind::UPDATE,request,unchanged) == Probe::Action::INVALID);
    request = disable(); request.fields |= static_cast<uint32_t>(ESS::DriverField::INPUT_POLARITY); request.inputPolarity = 1;
    assert(host(app).startDriver(app,2,1,ESS::DriverKind::UPDATE,request,unchanged) == Probe::Action::INVALID);
    request = disable(); request.fields |= static_cast<uint32_t>(ESS::DriverField::OUTPUT_Y0);
    request.outputFunctions[0] = static_cast<ESS::OutputFunction>(6);
    assert(host(app).startDriver(app,2,1,ESS::DriverKind::UPDATE,request,unchanged) == Probe::Action::INVALID);
    assert(unchanged == 99 && hardware.writes == writes);
    const auto id = update(disable()); // Unknown unrelated X3 does not make X0 unavailable.
    assert(host(app).cancel(app,id) == Probe::Action::OK);
    for (unsigned i = 0; i < 100 && view(id).pending; ++i) step();
    assert(!view(id).pending && hardware.writes == writes);
    fresh(); readIo(); stateEvidence();
    app->stateCache.blocks[1].value.rawInputs = 0x10;
    assert(host(app).startDriver(app,2,1,ESS::DriverKind::UPDATE,disable(),unchanged) == Probe::Action::INVALID);
}
void outputsAndFailures() {
    fresh(); readIo(1,0,9); stateEvidence();
    ESS::DriverRequest request; assert(ESS::prepareOutputFunction(request,0,ESS::OutputFunction::UNDEFINED));
    request.configurationGeneration = app->axis.generation;
    auto id = update(request); reply(id,{}); reply(id,words({0}));
    assert(view(id).driverContext->outcome == ESS::DriverOutcome::SUCCESS && view(id).driverContext->progress[13].readback == 0);
    fresh(); readIo(); stateEvidence(); request = disable();
    assert(ESS::prepareOutputFunction(request,1,ESS::OutputFunction::ALARM));
    id = update(request); reply(id,{}); reply(id,words({0})); waitTx(id);
    const auto writes = hardware.writes;
    for (unsigned i = 0; i < 40000 && view(id).pending; ++i) step();
    assert(!view(id).pending && view(id).driverContext->uncertain && hardware.writes == writes);
    assert(view(id).driverContext->progress[8].readbackKnown && !view(id).driverContext->progress[14].readbackKnown);
    assert(!app->ioSettings.operationId && app->owner.needsRecovery());
    fresh(); readIo(); stateEvidence(); id = update(disable()); reply(id,{}); reply(id,words({1}));
    assert(view(id).driverContext->outcome == ESS::DriverOutcome::READBACK_MISMATCH && view(id).driverContext->uncertain);
    fresh(); readIo(); stateEvidence(); id = update(disable()); waitTx(id);
    assert(host(app).cancel(app,id) == Probe::Action::OK);
    for (unsigned i = 0; i < 40000 && view(id).pending; ++i) step();
    assert(!view(id).pending && view(id).driverContext->uncertain && view(id).driverContext->effects);
}
void conflictAndPassiveReads() {
    fresh(); readIo(); stateEvidence();
    Rtu::BusRequest forbidden; forbidden.expected.address = forbidden.expected.target = 1;
    forbidden.expected.targetGeneration = app->bindingGeneration; forbidden.expected.function = 6;
    forbidden.expected.first = ESS::Registers::AUXILIARY_COMMAND; forbidden.expected.count = 1;
    assert(checkIoWrite(*app,forbidden) == Probe::Action::INVALID); // Scoped path never admits motor actions.
    const auto id = update(disable());
    uint32_t unchanged = 99;
    assert(host(app).startDriver(app,3,1,ESS::DriverKind::UPDATE,disable(),unchanged) == Probe::Action::AXIS_CONFLICT);
    assert(unchanged == 99); invalidateAxis(*app);
    const auto writes = hardware.writes;
    for (unsigned i = 0; i < 100 && view(id).pending; ++i) step();
    assert(!view(id).pending && hardware.writes == writes && !view(id).driverContext->effects);
    fresh(); const auto first = readIo(); stateEvidence();
    app->axis.originKnown = true; const auto generation = app->axis.generation;
    readIo(0); // External changed assignment invalidates dependent confidence.
    assert(app->axis.generation > generation && !app->axis.originKnown && !app->driverInputsQualified);
    assert(app->ioSettings.raw[8] == 0 && app->ioSettings.configurationGeneration == app->axis.generation);
    assert(view(first).driverContext->observations[0].raw[6] == 1);
    const auto baseline = app->ioSettings.operationId;
    readIo(17,0,0,0,2); assert(app->ioSettings.operationId == baseline);
}
void polarityRequiresPriorDisableAndCustomMapping() {
    fresh(); readIo(0); stateEvidence();
    ESS::DriverRequest request; request.group = ESS::DriverGroup::IO;
    request.fields = static_cast<uint32_t>(ESS::DriverField::INPUT_POLARITY);
    request.configurationGeneration = app->axis.generation; request.inputPolarity = 1;
    const auto id = update(request); reply(id,{}); reply(id,words({1}));
    assert(view(id).driverContext->outcome == ESS::DriverOutcome::SUCCESS);
    assert(view(id).driverContext->progress[7].readback == 1 && !view(id).driverContext->progress[7].activeKnown);
    fresh(); readIo(1); stateEvidence(); request = disable(); request.inputPolarity = 1;
    request.fields |= static_cast<uint32_t>(ESS::DriverField::INPUT_POLARITY);
    uint32_t unchanged = 77; const auto writes = hardware.writes;
    assert(host(app).startDriver(app,2,1,ESS::DriverKind::UPDATE,request,unchanged) == Probe::Action::INVALID);
    assert(unchanged == 77 && hardware.writes == writes); // A later disable cannot justify an earlier inversion.
    fresh(); readIo(1,0,10); stateEvidence();
    request = ESS::DriverRequest(); request.group = ESS::DriverGroup::IO;
    request.configurationGeneration = app->axis.generation; request.fields = static_cast<uint32_t>(ESS::DriverField::CUSTOM_OUTPUT);
    request.customOutput = 1;
    const auto before = hardware.writes;
    assert(host(app).startDriver(app,2,1,ESS::DriverKind::UPDATE,request,unchanged) != Probe::Action::OK);
    assert(unchanged == 77 && hardware.writes == before); // Crossmapped custom selector lacks reviewed actuation semantics.
    fresh(); readIo(1,0,9); stateEvidence(); request.configurationGeneration = app->axis.generation;
    const auto custom = update(request); reply(custom,{}); reply(custom,words({1}));
    assert(view(custom).driverContext->outcome == ESS::DriverOutcome::SUCCESS && view(custom).driverContext->progress[15].readback == 1);
}

void activePreviousAssignmentsNeedExplicitQualification() {
    for (uint16_t previous : {uint16_t(4),uint16_t(5),uint16_t(6),uint16_t(8),uint16_t(9),uint16_t(10)}) {
        fresh(); readIo(previous); stateEvidence();
        const auto before = hardware.writes; uint32_t unchanged = 77;
        assert(host(app).startDriver(app,2,1,ESS::DriverKind::UPDATE,disable(),unchanged) == Probe::Action::INVALID);
        assert(unchanged == 77 && hardware.writes == before && !app->owner.pending());
    }
}
void consoleAdmissionAndTerminalCorrelation() {
    fresh(); readIo();
    for (unsigned i = 0; i < 1000 && app->outputCount; ++i) step();
    assert(!app->outputCount); stateEvidence(); Serial.output.clear();
    Serial.input = "@55 profile ess_rs io set x0 none\n";
    for (unsigned i = 0; i < 100 && !Serial.input.empty(); ++i) step();
    assert(Serial.input.empty());
    for (unsigned i = 0; i < 100; ++i) step();
    assert(Serial.output.find("\"command\":\"io\"") != std::string::npos);
    assert(Serial.output.find("\"result\":\"accepted\"") != std::string::npos);
    const auto operation = app->latestOperationId;
    const auto* record = findRecord(*app,operation);
    assert(record && record->commandId == 55 && record->driverOperation);
    assert(record->driver.request.inputFunctions[0] == ESS::InputFunction::UNDEFINED);
    reply(operation,{}); reply(operation,words({0}));
    for (unsigned i = 0; i < 1000 && app->outputCount; ++i) step();
    assert(findRecord(*app,operation)->delivered);
    const std::string correlation = "\"command_id\":55,\"operation_id\":" + std::to_string(operation);
    assert(Serial.output.find("\"type\":\"io\"") != std::string::npos && Serial.output.find(correlation) != std::string::npos);
    assert(view(operation).driverContext->outcome == ESS::DriverOutcome::SUCCESS && !view(operation).driverContext->uncertain);
}

void optionalInputsAreNotGlobalPrerequisites() {
    fresh(); stateEvidence(); actionTimingQualified = true;
    app->axis.nativeMinimum = -1000; app->axis.nativeMaximum = 1000; app->axis.supportedRelativeBases = 1;
    app->configuration.target = app->axis.target; app->configuration.operationId = 99;
    app->configuration.wordOrderKnown = true;
    app->configuration.raw.inputFunctions[0] = 1; app->configuration.raw.inputFunctions[1] = 2;
    app->configuration.raw.inputFunctions[2] = 3;
    auto& p = app->movePrerequisites; p.target = app->axis.target; p.configurationGeneration = app->axis.generation;
    p.commandUnitsVerified = p.relativeBasisVerified = p.negativeTwosComplementVerified = true;
    p.configuredRampVerified = p.serialInputsPermit = p.readinessQualified = true;
    p.accelerationTime = p.decelerationTime = 100; p.wordOrderKnown = p.startSpeedKnown = true; p.startSpeed = 10;
    p.rawMotion = 1; p.observedUs = hardware.time; p.maximumAgeUs = 1000000;
    for (const auto wiring : app->inputWiring) assert(wiring == InputWiring::UNCONNECTED);
    MoveRequest move; move.position.value = Rational(20); move.position.configurationGeneration = app->axis.generation;
    move.speedRpm = 30; move.ramp = MoveRamp::VERIFIED_CONFIGURED;
    uint32_t id = 0;
    assert(host(app).startMove(app,10,1,move,id) == Probe::Action::OK);
    assert(view(id).moveContext && !hardware.writes); // Admission does not alter optional assignments.
    assert(host(app).cancel(app,id) == Probe::Action::OK);
    for (unsigned i = 0; i < 100 && view(id).pending; ++i) step();
    assert(!hardware.writes && app->configuration.raw.inputFunctions[0] == 1);
    fresh(); stateEvidence(); actionTimingQualified = true;
    app->configuration.target = app->axis.target; app->configuration.operationId = 99;
    ESS::HomeRequest home; home.method = ESS::HomingMethod::METHOD_24; home.configurationGeneration = app->axis.generation;
    uint32_t unchanged = 77;
    assert(host(app).startHome(app,11,1,home,unchanged) == Probe::Action::UNSUPPORTED);
    assert(unchanged == 77 && !hardware.writes); // Setters do not implement missing switch transitions.
    home.method = ESS::HomingMethod::METHOD_35;
    auto& hp = app->homePrerequisites; hp.target = app->axis.target; hp.configurationGeneration = app->axis.generation;
    hp.qualifiedMethod = ESS::HomingMethod::METHOD_35;
    hp.qualifiedSearchSpeed = 60; hp.qualifiedReturnSpeed = 30; hp.qualifiedRampTime = 100;
    hp.methodQualified = hp.nativeRatesQualified = hp.nativeRampQualified = hp.zeroOffsetQualified = hp.auxiliaryQualified = true;
    hp.inputsQualified = hp.readinessQualified = true; hp.observedUs = hardware.time; hp.maximumAgeUs = 1000000;
    // No terminal/index requirement for current-position origin method35.
    assert(host(app).startHome(app,12,1,home,id) == Probe::Action::OK);
    assert(view(id).homeContext && !hardware.writes);
    assert(host(app).cancel(app,id) == Probe::Action::OK);
    for (unsigned i = 0; i < 100 && view(id).pending; ++i) step();
    assert(!hardware.writes);
}

} // namespace
int main() {
    safeDisableAndReadback(); scopedGates(); outputsAndFailures(); conflictAndPassiveReads(); polarityRequiresPriorDisableAndCustomMapping(); activePreviousAssignmentsNeedExplicitQualification();
    consoleAdmissionAndTerminalCorrelation(); optionalInputsAreNotGlobalPrerequisites();
    fresh(); readIo(); stateEvidence(); app->actionConflicts[0] |= 2;
    uint32_t unchanged = 77; const auto writes = hardware.writes;
    assert(host(app).startDriver(app,99,1,ESS::DriverKind::UPDATE,disable(),unchanged) == Probe::Action::AXIS_CONFLICT);
    assert(unchanged == 77 && hardware.writes == writes); // Settings cannot clear an uncertain axis.

    if (app) { app->~App(); std::free(app); app = nullptr; }
    return 0;
}
