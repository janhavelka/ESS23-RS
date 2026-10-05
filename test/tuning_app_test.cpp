// SPDX-License-Identifier: MIT
// Actual application and console paths; supplied SDK evidence is simulated.
#include "../examples/probe_cli/ProbeApp.cpp"
#include "../examples/probe_cli/ArduinoPlatform.cpp"
#include "../examples/probe_cli/main.cpp"
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
    assert(beginApplication({Board::kRs485TxPin, Board::kRs485RxPin, Board::kRs485DeRePin,
        Board::kRs485DeReActiveHigh}, false)); // Explicit alternate/unknown-echo topology.
    assert(app && !hardware.writes && !writeResponseConfirmed);
    hardware.txCharacterUs = 87; assert(uart.startCapture(20, timing().holdUs));
}
void step() { advanceHardware(hardware.time + 10); loop(); }
Probe::ResultView view(uint32_t id) {
    Probe::ResultView v; assert(host(app).result(app, id, v)); return v;
}
std::vector<uint8_t> words(const std::vector<uint16_t>& values, uint8_t address = 1) {
    std::vector<uint8_t> bytes = {address, 3, static_cast<uint8_t>(values.size() * 2)};
    for (auto value : values) { bytes.push_back(static_cast<uint8_t>(value >> 8)); bytes.push_back(static_cast<uint8_t>(value)); }
    const uint16_t crc = ESS::calcCrc16(bytes.data(), bytes.size());
    bytes.push_back(static_cast<uint8_t>(crc)); bytes.push_back(static_cast<uint8_t>(crc >> 8)); return bytes;
}
void waitTx(uint32_t id) {
    for (unsigned i = 0; i < 25000 && !app->owner.txAccepted(findRecord(*app, id)->requestId); ++i) step();
    assert(app->owner.txAccepted(findRecord(*app, id)->requestId));
}
void reply(uint32_t id, const std::vector<uint8_t>& supplied) {
    const bool typed = view(id).typedRead != nullptr;
    const auto token = typed ? view(id).typedRead->step : view(id).driverContext->step;
    waitTx(id);
    scheduleReply(std::max(hardware.writeStarted + 8 * 87 + 1000, hardware.time + 1000), supplied.empty() ? hardware.tx : supplied);
    for (unsigned i = 0; i < 25000 && view(id).pending &&
         (typed ? view(id).typedRead->step : view(id).driverContext->step) == token; ++i) step();
    assert(!view(id).pending || (typed ? view(id).typedRead->step : view(id).driverContext->step) != token);
}
uint32_t read(ESS::DriverGroup group, const std::vector<uint16_t>& raw, uint8_t address = 1) {
    ESS::DriverRequest request; request.group = group;
    uint32_t id = 0;
    assert(host(app).startDriver(app, 1, address, ESS::DriverKind::READ, request, id) == Probe::Action::OK);
    for (std::size_t offset = 0; offset < raw.size(); offset += 4)
        reply(id, words(std::vector<uint16_t>(raw.begin() + offset, raw.begin() + std::min(raw.size(), offset + 4)), address));
    assert(view(id).driverContext->outcome == ESS::DriverOutcome::SUCCESS);
    return id;
}
void identity() {
    uint32_t id = 0;
    assert(host(app).startTypedRead(app, 2, 1, ESS::ReadKind::IDENTITY, id) == Probe::Action::OK);
    reply(id, words({0x4EEA, 0x29, 1, 0})); assert(app->identity.operationId == id);
}
void ioBaseline() {
    ESS::DriverRequest request; request.group = ESS::DriverGroup::IO;
    uint32_t id = 0;
    assert(host(app).startDriver(app, 3, 1, ESS::DriverKind::READ, request, id) == Probe::Action::OK);
    reply(id, words({0, 1, 2, 3, 0})); reply(id, words({0, 0, 0})); reply(id, words({0}));
    assert(view(id).driverContext->outcome == ESS::DriverOutcome::SUCCESS);
}
void stationary() {
    for (auto kind : {ESS::StateBlock::MOTION, ESS::StateBlock::IO}) {
        auto& block = app->stateCache.blocks[static_cast<uint8_t>(kind)];
        block.valid = true; block.value.target = app->axis.target; block.value.rawMotion = 1;
        block.observedEarliestUs = block.observedLatestUs = hardware.time; block.invalidatedUs = 0;
    }
    app->knownTargets[0] |= 2;
}
ESS::DriverRequest filter(uint16_t value = 3) {
    ESS::DriverRequest request;
    assert(ESS::prepareTuningValue(request, ESS::TuningParameter::INPUT_FILTER, value));
    request.configurationGeneration = app->axis.generation; return request;
}
void baseline() {
    fresh(); read(ESS::DriverGroup::FILTERS, {2, 5, 4000, 5, 10, 512}); identity(); ioBaseline(); stationary();
}
void readsAndChangedCompletion() {
    fresh(); const auto old = read(ESS::DriverGroup::FILTERS, {2, 5, 4000, 5, 10, 512});
    app->axis.units.commandStepsPerMotorTurn = UnitScale(1000, 1, ScaleSource::QUALIFIED);
    app->axis.originKnown = true; app->movePrerequisites.readinessQualified = true;
    const auto generation = app->axis.generation;
    read(ESS::DriverGroup::CURRENT_LOOP, {4096, 1024, 28, 1228});
    assert(app->axis.generation == generation && app->axis.originKnown);
    read(ESS::DriverGroup::FILTERS, {2, 5, 4000, 6, 10, 512});
    assert(app->axis.generation > generation && !app->axis.originKnown && !app->movePrerequisites.readinessQualified);
    assert(app->axis.units.commandStepsPerMotorTurn.numerator == 1000 && app->commandPolarityKnown);
    assert(view(old).driverContext->configurationGeneration == generation && view(old).driverContext->observations[0].raw[10] == 5);
    const auto stable = app->axis.generation;
    read(ESS::DriverGroup::CURRENT_LOOP, {4096, 1024, 28, 1228});
    read(ESS::DriverGroup::LA, {10, 32, 320, 15, 33, 320, 20, 35});
    read(ESS::DriverGroup::COLLISION, {200, 50});
    assert(app->axis.generation == stable); // Different group baselines cannot be compared as geometry.
    const auto cached = tuningCache(*app, ESS::DriverGroup::COLLISION);
    read(ESS::DriverGroup::COLLISION, {201, 51}, 2);
    assert(tuningCache(*app, ESS::DriverGroup::COLLISION).operationId == cached.operationId &&
           tuningCache(*app, ESS::DriverGroup::COLLISION).raw[0] == 200);
    assert(hardware.writes == 10); // Only the bounded read windows above.
}
void safeFilterAndGuards() {
    baseline(); const auto writes = hardware.writes;
    ESS::DriverRequest request;
    assert(ESS::prepareTuningValue(request, ESS::TuningParameter::CURRENT_LOOP_KP, 1024));
    uint32_t untouched = 99;
    assert(host(app).startDriver(app, 4, 1, ESS::DriverKind::UPDATE, request, untouched) != Probe::Action::OK);
    app->inputWiring[0] = InputWiring::UNKNOWN; request = filter();
    assert(host(app).startDriver(app, 4, 1, ESS::DriverKind::UPDATE, request, untouched) == Probe::Action::INVALID);
    assert(untouched == 99 && hardware.writes == writes);
    app->inputWiring[0] = InputWiring::UNCONNECTED;
    app->axis.units.commandStepsPerMotorTurn = UnitScale(1000, 1, ScaleSource::QUALIFIED);
    const auto generation = app->axis.generation;
    Serial.input = "@5 profile ess_rs tuning filters set input-filter 3\n";
    for (unsigned i = 0; i < 100 && !Serial.input.empty(); ++i) step();
    const auto id = app->latestOperationId; waitTx(id);
    assert(app->axis.generation > generation && !tuningCache(*app, ESS::DriverGroup::FILTERS).operationId && axisReserved(*app, 1));
    assert(app->axis.units.commandStepsPerMotorTurn.numerator == 1000 && app->commandPolarityKnown);
    reply(id, {}); reply(id, words({3}));
    const auto& context = *view(id).driverContext;
    assert(context.outcome == ESS::DriverOutcome::SUCCESS && !context.uncertain);
    assert(!context.progress[0].acknowledged && context.progress[0].execution == ActionExecution::UNKNOWN && context.progress[0].readbackKnown);
    for (unsigned i = 0; i < 100; ++i) step();
    assert(Serial.output.find("\"type\":\"tuning\"") != std::string::npos && hardware.writes == writes + 2);
}
void failuresAndRecovery() {
    baseline(); auto request = filter(); uint32_t id = 0;
    assert(host(app).startDriver(app, 6, 1, ESS::DriverKind::UPDATE, request, id) == Probe::Action::OK);
    reply(id, {}); reply(id, words({2}));
    assert(view(id).driverContext->outcome == ESS::DriverOutcome::READBACK_MISMATCH && view(id).driverContext->uncertain);
    const auto writes = hardware.writes;
    for (unsigned i = 0; i < 100; ++i) step();
    assert(hardware.writes == writes);
    baseline(); request = filter();
    assert(host(app).startDriver(app, 7, 1, ESS::DriverKind::UPDATE, request, id) == Probe::Action::OK); waitTx(id);
    const auto acceptedWrites = hardware.writes;
    Serial.input = "@8 recover\n";
    for (unsigned i = 0; i < 100 && !Serial.input.empty(); ++i) step();
    for (unsigned i = 0; i < 80000 && (app->owner.recovering() || view(id).pending); ++i) step();
    assert(view(id).driverContext->outcome == ESS::DriverOutcome::CANCELLED && view(id).driverContext->uncertain);
    assert(view(id).driverContext->effects == 1 && !view(id).driverContext->progress[0].readbackKnown);
    assert(hardware.writes == acceptedWrites && !axisReserved(*app, 1));
}
void failedRefreshPreservesBaseline() {
    fresh(); const auto old = read(ESS::DriverGroup::FILTERS, {2, 5, 4000, 5, 10, 512});
    const auto cached = tuningCache(*app, ESS::DriverGroup::FILTERS);
    app->axis.originKnown = true; app->movePrerequisites.readinessQualified = true;
    const auto generation = app->axis.generation;
    ESS::DriverRequest request; request.group = ESS::DriverGroup::FILTERS;
    uint32_t id = 0;
    assert(host(app).startDriver(app, 9, 1, ESS::DriverKind::READ, request, id) == Probe::Action::OK);
    reply(id, words({2, 5, 4000, 6}));
    auto malformed = words({10, 512}); malformed.back() ^= 1;
    reply(id, malformed);
    assert(!view(id).pending && view(id).driverContext->outcome != ESS::DriverOutcome::SUCCESS);
    assert(!tuningCache(*app, ESS::DriverGroup::FILTERS).operationId &&
           tuningCache(*app, ESS::DriverGroup::FILTERS).raw[3] == cached.raw[3]);
    assert(app->axis.generation > generation && !app->axis.originKnown && !app->movePrerequisites.readinessQualified);
    const auto invalidated = app->axis.generation;
    for (unsigned i = 0; i < 10; ++i) step();
    assert(app->axis.generation == invalidated); // Repeated service does not repeat invalidation.
    assert(view(old).driverContext->outcome == ESS::DriverOutcome::SUCCESS);
}
} // namespace
int main() {
    readsAndChangedCompletion(); safeFilterAndGuards(); failuresAndRecovery(); failedRefreshPreservesBaseline();
    if (app) { app->~App(); std::free(app); app = nullptr; }
}
