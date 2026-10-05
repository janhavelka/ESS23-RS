// SPDX-License-Identifier: MIT
// Exercise the actual cooperative application with simulated SDK/wire evidence.
// Qualification below belongs solely to this fake fixture, never the real bench.
#include "../examples/probe_cli/ProbeApp.cpp"
#include "../examples/probe_cli/ArduinoPlatform.cpp"
#include "../examples/probe_cli/main.cpp"
#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <vector>

FakeSerial Serial;
namespace {
using namespace MotorControlRS;
void fresh() {
    if (app) { app->~App(); std::free(app); app = nullptr; }
    uart.~Esp32S3Uart(); new (&uart) Esp32S3Uart;
    resetHardware(); Serial = FakeSerial(); platformReady = false; writeResponseConfirmed = false;
    setup();
    assert(app && uart.ready() && app->owner.valid() && hardware.writes == 0);
    hardware.txCharacterUs = 87;
    assert(uart.startCapture(20, timing().holdUs));
}
void step(uint32_t us = 10) { advanceHardware(hardware.time + us); loop(); }
void pump(unsigned count = 64) { for (unsigned i = 0; i < count; ++i) step(); }
Probe::ResultView view(uint32_t operation) {
    Probe::ResultView result;
    assert(host(app).result(app, operation, result)); return result;
}
void command(const std::string& text) {
    Serial.input = text; Serial.output.clear();
    for (unsigned i = 0; i < 500 && !Serial.input.empty(); ++i) step();
    assert(Serial.input.empty()); pump();
}
std::vector<uint8_t> crc(std::vector<uint8_t> bytes) {
    const uint16_t value = ESS::calcCrc16(bytes.data(), bytes.size());
    bytes.push_back(static_cast<uint8_t>(value)); bytes.push_back(static_cast<uint8_t>(value >> 8));
    return bytes;
}
std::vector<uint8_t> registers(uint8_t address, std::initializer_list<uint16_t> words) {
    std::vector<uint8_t> bytes = {address, 3, static_cast<uint8_t>(words.size() * 2)};
    for (const auto word : words) { bytes.push_back(static_cast<uint8_t>(word >> 8)); bytes.push_back(static_cast<uint8_t>(word)); }
    return crc(bytes);
}
void waitTx(uint32_t operation) {
    for (unsigned i = 0; i < 25000 && !app->owner.txAccepted(findRecord(*app, operation)->requestId); ++i) step();
    if (!app->owner.txAccepted(findRecord(*app, operation)->requestId))
        std::fprintf(stderr,"No TX operation %u pending %d phase %u step %u outcome %u needsRecovery %d writes %u\n",
            operation,view(operation).pending,static_cast<unsigned>(findRecord(*app,operation)->velocity.phase),
            findRecord(*app,operation)->velocity.step,static_cast<unsigned>(findRecord(*app,operation)->velocity.outcome),
            app->owner.needsRecovery(),hardware.writes);
    assert(app->owner.txAccepted(findRecord(*app, operation)->requestId));
}
std::vector<uint8_t> acknowledgement() {
    assert(hardware.tx.size() >= 8);
    if (hardware.tx[1] == 6) return hardware.tx;
    assert(hardware.tx[1] == 16);
    return crc(std::vector<uint8_t>(hardware.tx.begin(), hardware.tx.begin() + 6));
}
void velocityStep(uint32_t operation, const std::vector<uint8_t>& supplied = {}) {
    const uint8_t token = view(operation).velocityContext->step;
    waitTx(operation);
    const auto response = supplied.empty() ? acknowledgement() : supplied;
    scheduleReply(std::max(hardware.writeStarted + hardware.tx.size() * 87 + 1000, hardware.time + 1000), response);
    for (unsigned i = 0; i < 25000 && view(operation).pending && view(operation).velocityContext->step == token; ++i) step();
    assert(!view(operation).pending || view(operation).velocityContext->step != token);
}
void actionStep(uint32_t operation, const std::vector<uint8_t>& supplied = {}) {
    const uint8_t token = view(operation).actionContext->step;
    waitTx(operation);
    const auto response = supplied.empty() ? acknowledgement() : supplied;
    scheduleReply(std::max(hardware.writeStarted + 8 * 87 + 1000, hardware.time + 1000), response);
    for (unsigned i = 0; i < 25000 && view(operation).pending && view(operation).actionContext->step == token; ++i) step();
    assert(!view(operation).pending || view(operation).actionContext->step != token);
}
VelocityRequest request() {
    VelocityRequest r; r.value = Rational(30); r.configurationGeneration = app->axis.generation;
    r.ramp = VelocityRamp::VERIFIED_CONFIGURED; r.durationUs = 200000;
    r.stop.behavior = StopBehavior::CONFIGURED_DECELERATION; return r;
}
void qualify() {
    // Deliberately simulated qualifications. Production has no corresponding
    // console control and starts with every verification flag false.
    writeResponseConfirmed = true;
    app->knownTargets[0] |= 2;
    app->axis.nativeMinimum = -1000; app->axis.nativeMaximum = 1000;
    app->axis.supportedRelativeBases = 1;
    auto& p = app->velocityPrerequisites;
    p.target = app->axis.target; p.configurationGeneration = app->axis.generation;
    p.nativeRpmVerified = p.negativeTwosComplementVerified = true;
    p.configuredRampVerified = p.serialInputsPermit = p.readinessQualified = true;
    p.accelerationTime = p.decelerationTime = 100;
    p.rawMotion = 1; p.observedUs = hardware.time; p.maximumAgeUs = 1000000;
    app->configuration.target = app->axis.target;
    app->configuration.operationId = 99;
    app->configuration.wordOrderKnown = true;
    app->configuration.raw.subdivision = 1000;
    app->configuration.raw.algorithm = 2; app->configuration.raw.encoderResolution = 4000;
    app->configuration.raw.inputFunctions[0] = 1;
    app->configuration.raw.inputFunctions[1] = 2; app->configuration.raw.inputFunctions[2] = 3;
    auto& motion = app->stateCache.blocks[static_cast<uint8_t>(ESS::StateBlock::MOTION)];
    motion.valid = true; motion.value.target = app->axis.target;
    motion.value.rawMotion = 1; motion.value.inPosition = motion.value.enabled = true;
    motion.observedEarliestUs = motion.observedLatestUs = hardware.time;
}
uint32_t admitVelocity(uint32_t commandId = 1) {
    uint32_t id = 0;
    assert(host(app).startVelocity(app, commandId, 1, request(), id) == Probe::Action::OK);
    assert(id && view(id).velocityContext && axisReserved(*app, 1)); return id;
}
uint32_t admitStop(uint32_t commandId = 50) {
    ActionRequest stop; stop.kind = ActionKind::STOP; stop.stop.behavior = StopBehavior::CONFIGURED_DECELERATION;
    uint32_t id = 0;
    assert(host(app).startAction(app, commandId, 1, stop, id) == Probe::Action::OK); return id;
}
void gatesAndParity() {
    fresh(); uint32_t untouched = 77;
    assert(host(app).startVelocity(app, 1, 1, request(), untouched) != Probe::Action::OK);
    assert(untouched == 77 && !hardware.writes);
    command("@1 velocity 30 rpm native 200 configured normal\n");
    assert(!hardware.writes);
    for (bool profile : {false, true}) {
        fresh(); qualify(); app->axis.units.commandStepsPerMotorTurn = UnitScale(1000,1,ScaleSource::ASSUMED);
        command(profile ? "@1 velocity 30 rpm native 200 configured normal\n" :
            "@1 velocity 500 steps/s motor 200 configured normal\n");
        if (Serial.output.find("\"result\":\"accepted\"") == std::string::npos)
            std::fprintf(stderr,"Velocity CLI: %s\n",Serial.output.c_str());
        assert(Serial.output.find("\"result\":\"accepted\"") != std::string::npos);
        const auto id = view(0).operationId;
        assert(view(id).velocityContext->prepared.nativeRpm == 30);
        velocityStep(id); assert(hardware.tx.size() == 15 && hardware.tx[3] == 0x1D);
        uint8_t bytes[8]; Rtu::RequestId other;
        Rtu::BusRequest write; write.expected.address = write.expected.target = 1;
        write.expected.targetGeneration = app->bindingGeneration;
        write.expected.function = 6; write.expected.first = 0x1D; write.expected.count = 1;
        write.wire.bytes = bytes; write.wire.length = ESS::buildWriteSingleRegister(1,0x1D,50,bytes,sizeof(bytes));
        write.wire.replyLength = 8; write.wire.responseTimeoutUs = app->serial.timing.responseTimeoutUs;
        write.wire.deadlineUs = nowUs() + REQUEST_US; write.validator = Rtu::essValidator();
        assert(admitAxisWrite(*app,write,nowUs(),other) == Probe::Action::AXIS_CONFLICT);
        velocityStep(id); assert(hardware.tx[1] == 6 && hardware.tx[5] == 2);
        velocityStep(id, registers(1,{0,4})); assert(view(id).velocityContext->runningObserved);
        const auto due = view(id).velocityContext->stopDueUs;
        // A settled wait yields stop on the immutable finite boundary.
        assert(!findRecord(*app,id)->requestId.owner);
        advanceHardware(due); loop();
        assert(nowUs() >= due && view(id).velocityContext->deadlineUs > due);
        velocityStep(id); assert(hardware.tx[5] == 0 && hardware.tx[4] == 1);
        velocityStep(id, registers(1,{0,1}));
        assert(!view(id).pending && !view(id).velocityContext->needsStop);
        assert(view(id).velocityContext->completion == ActionCompletion::OBSERVED && !axisReserved(*app,1));
        assert(host(app).cancel(app,id) == Probe::Action::ALREADY_TERMINAL);
    }
}
void externalStopAndLocalCancel() {
    fresh(); qualify(); const auto id = admitVelocity();
    velocityStep(id); velocityStep(id);
    velocityStep(id,registers(1,{0,4}));
    assert(view(id).velocityContext->runningObserved);
    const auto stop = admitStop();
    for (unsigned i=0;i<25000 && view(id).pending;++i) step();
    assert(!view(id).pending && view(id).interruptedByStop && view(id).velocityContext->needsStop);
    actionStep(stop); actionStep(stop,registers(1,{0,1}));
    assert(!axisReserved(*app,1) && view(id).velocityContext->needsStop);
    // Original uncertainty survives a later independent checked stop.
    assert(view(id).velocityContext->outcome == ActionOutcome::CANCELLED);
    fresh(); qualify(); const auto cancelled = admitVelocity();
    assert(host(app).cancel(app,cancelled) == Probe::Action::OK);
    pump(1000); assert(!view(cancelled).pending && !hardware.writes);
}
void pressureAndPhysicalCancellation() {
    fresh(); qualify(); uint32_t retained[7] = {};
    for (unsigned i=0;i<7;++i) {
        assert(probe(app,10+i,2,retained[i]) == Probe::Action::OK);
        waitTx(retained[i]); scheduleReply(hardware.time+2000,registers(2,{0x4EEA}));
        for (unsigned j=0;j<25000 && view(retained[i]).pending;++j) step();
        assert(!view(retained[i]).pending);
    }
    const auto id=admitVelocity(); waitTx(id);
    const auto stopping=admitStop(); velocityStep(id);
    assert(!view(id).pending && view(id).velocityContext->execution == ActionExecution::NOT_TRANSMITTED);
    assert(view(id).velocityContext->uncertain && view(id).interruptedByStop);
    actionStep(stopping); actionStep(stopping,registers(1,{0,1}));
    assert(!axisReserved(*app,1));
    for (auto kept:retained) assert(view(kept).probe.outcome == Rtu::Outcome::SUCCESS);
    // Local cancellation settles accepted physical staging; it cannot truncate TX.
    fresh(); qualify(); const auto cancelled=admitVelocity(); waitTx(cancelled);
    const uint64_t physicalEnd=hardware.writeStarted+hardware.tx.size()*87;
    assert(cancel(app,cancelled) == Probe::Action::OK);
    for (unsigned i=0;i<25000 && view(cancelled).pending;++i) step();
    assert(!view(cancelled).pending && hardware.deReleasedAt >= physicalEnd && hardware.writes == 1);
    assert(view(cancelled).velocityContext->uncertain && axisReserved(*app,1));
}
void delayedEvidenceAndRecovery() {
    fresh(); qualify(); const auto id=admitVelocity(); velocityStep(id); velocityStep(id); waitTx(id);
    const auto due=view(id).velocityContext->stopDueUs;
    scheduleReply(hardware.writeStarted+8*87+1000,registers(1,{0,4}));
    advanceHardware(due+30000); loop();
    const auto& context=*view(id).velocityContext;
    assert(context.runningObserved && context.serviceMissed && context.needsStop);
    assert(context.phase == ESS::VelocityPhase::STOPPING && context.stopDueUs == due);
    velocityStep(id); velocityStep(id,registers(1,{0,1}));
    assert(!view(id).pending && !view(id).velocityContext->needsStop);
    assert(view(id).velocityContext->outcome == ActionOutcome::DEADLINE);
    // Explicit recovery invalidates a still-active old-generation continuation.
    fresh(); qualify(); const auto recovering=admitVelocity(); velocityStep(recovering); waitTx(recovering);
    const uint32_t oldGeneration=app->bindingGeneration;
    uint32_t recovery=0; assert(recover(app,99,recovery) == Probe::Action::OK);
    for (unsigned i=0;i<250000 && (view(recovering).pending || view(recovery).pending);++i) step();
    assert(!view(recovering).pending && !view(recovery).pending && app->bindingGeneration == oldGeneration+1);
    assert(view(recovering).velocityContext->needsStop && axisReserved(*app,1));
    assert(view(recovery).recovery && hardware.writes == 2);
}
void urgentPressureDefersWithinOriginalDeadline() {
    for (bool releaseReserved : {false,true}) {
        fresh(); qualify(); const auto id=admitVelocity(); velocityStep(id); velocityStep(id);
        velocityStep(id,registers(1,{0,4}));
        uint8_t bytes[8]; Rtu::BusRequest r; r.wire.bytes=bytes;
        r.wire.length=ESS::buildProbe(2,bytes,sizeof(bytes)); r.wire.replyLength=7;
        r.wire.responseTimeoutUs=app->serial.timing.responseTimeoutUs; r.wire.replyGapUs=app->serial.timing.replyGapUs;
        r.wire.deadlineUs=nowUs()+REQUEST_US;
        r.expected.address=r.expected.target=2; r.expected.targetGeneration=app->bindingGeneration;
        r.expected.function=3; r.expected.count=1; r.validator=Rtu::essValidator(); Rtu::RequestId reserved;
        assert(app->owner.admitUrgent(r,nowUs(),reserved) == Rtu::BusAdmission::ACCEPTED);
        for (unsigned i=0;i<25000 && !app->owner.txAccepted(reserved);++i) step();
        scheduleReply(hardware.time+2000,registers(2,{0x4EEA}));
        for (unsigned i=0;i<25000 && !app->owner.result(reserved);++i) step();
        assert(app->owner.result(reserved));
        const uint64_t deadline=view(id).velocityContext->deadlineUs;
        advanceHardware(view(id).velocityContext->stopDueUs); loop();
        assert(view(id).pending && view(id).velocityContext->phase == ESS::VelocityPhase::STOPPING);
        assert(!findRecord(*app,id)->requestId.owner && !app->owner.needsRecovery());
        if (releaseReserved) {
            assert(app->owner.release(reserved)); loop();
            velocityStep(id); velocityStep(id,registers(1,{0,1}));
            assert(!view(id).pending && !view(id).velocityContext->needsStop);
        } else {
            advanceHardware(deadline); loop();
            assert(!view(id).pending && view(id).velocityContext->needsStop && hardware.writes == 4);
            assert(view(id).velocityContext->stop.outcome == ActionOutcome::DEADLINE);
        }
        assert(view(id).velocityContext->deadlineUs == deadline);
    }
}
void lostStartNeverReplays() {
    fresh(); qualify(); command("@1 velocity 30 rpm native 200 configured normal\n");
    const auto id = view(0).operationId;
    velocityStep(id); waitTx(id);
    for (unsigned i=0;i<400000 && view(id).pending;++i) step();
    assert(!view(id).pending && view(id).velocityContext->execution == ActionExecution::UNKNOWN);
    assert(view(id).velocityContext->needsStop && axisReserved(*app,1));
    assert(app->owner.needsRecovery() && hardware.writes == 2);
    pump(2000); assert(findRecord(*app,id)->delivered);
    assert(host(app).release(app,id) == Probe::Action::OK && axisReserved(*app,1));
}
} // namespace
int main() { gatesAndParity(); externalStopAndLocalCancel(); pressureAndPhysicalCancellation(); delayedEvidenceAndRecovery(); urgentPressureDefersWithinOriginalDeadline(); lostStartNeverReplays(); }
