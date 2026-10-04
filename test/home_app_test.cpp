// SPDX-License-Identifier: MIT
// Actual cooperative application; all qualifications are simulated fixtures.
#include "../examples/probe_cli/main.cpp"
#include <cassert>
#include <cstdlib>
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
void step() { advanceHardware(hardware.time + 10); loop(); }
void pump(unsigned n = 1000) { while (n--) step(); }
Probe::ResultView view(uint32_t id) { Probe::ResultView v; assert(host(app).result(app, id, v)); return v; }
std::vector<uint8_t> crc(std::vector<uint8_t> v) {
    const auto c = ESS::calcCrc16(v.data(), v.size()); v.push_back(static_cast<uint8_t>(c)); v.push_back(static_cast<uint8_t>(c >> 8)); return v;
}
std::vector<uint8_t> words(uint16_t a, uint16_t b) { return crc({1,3,4,static_cast<uint8_t>(a >> 8),static_cast<uint8_t>(a),static_cast<uint8_t>(b >> 8),static_cast<uint8_t>(b)}); }
void waitTx(uint32_t id) {
    for (unsigned i = 0; i < 25000 && !app->owner.txAccepted(findRecord(*app,id)->requestId); ++i) step();
    assert(app->owner.txAccepted(findRecord(*app,id)->requestId));
}
void reply(uint32_t id, const std::vector<uint8_t>& given = {}) {
    const auto initial = view(id); const auto token = initial.homeContext ? initial.homeContext->step : initial.actionContext->step;
    waitTx(id);
    const auto response = given.empty() ? crc(std::vector<uint8_t>(hardware.tx.begin(), hardware.tx.begin()+6)) : given;
    scheduleReply(std::max(hardware.writeStarted + hardware.tx.size()*87 + 1000, hardware.time + 1000), response);
    for (unsigned i = 0; i < 25000 && view(id).pending; ++i) {
        const auto current = view(id); if ((current.homeContext ? current.homeContext->step : current.actionContext->step) != token) break; step();
    }
    const auto result = view(id);
    assert(!result.pending || (result.homeContext ? result.homeContext->step : result.actionContext->step) != token);
}
ESS::HomeRequest request(int method = 35) {
    ESS::HomeRequest r; r.method = static_cast<ESS::HomingMethod>(method); r.configurationGeneration = app->axis.generation; return r;
}
void qualify(bool oldHomed = false) {
    actionTimingQualified = true; app->knownTargets[0] |= 2;
    app->configuration.target = app->axis.target; app->configuration.operationId = 99;
    auto& p = app->homePrerequisites; p.target = app->axis.target; p.configurationGeneration = app->axis.generation;
    // Fixture authority is independent of the subsequently admitted request.
    p.qualifiedMethod = ESS::HomingMethod::METHOD_35;
    p.qualifiedSearchSpeed = 60; p.qualifiedReturnSpeed = 30; p.qualifiedRampTime = 100;
    p.methodQualified = p.nativeRatesQualified = p.nativeRampQualified = p.zeroOffsetQualified = p.auxiliaryQualified = true;
    p.inputsQualified = p.indexQualified = p.readinessQualified = p.referenceSemanticsQualified = true;
    p.observedUs = hardware.time; p.maximumAgeUs = 1000000; p.rawMotion = oldHomed ? 3 : 1;
    auto& m = app->stateCache.blocks[static_cast<uint8_t>(ESS::StateBlock::MOTION)];
    m.valid = true; m.value.target = app->axis.target; m.value.rawMotion = p.rawMotion;
    m.value.inPosition = m.value.enabled = true; m.value.homingComplete = oldHomed;
    m.observedEarliestUs = m.observedLatestUs = hardware.time;
    app->axis.originKnown = true; app->axis.originNative = 77; app->axis.originSource = ScaleSource::QUALIFIED;
}
uint32_t admit(int method = 35) {
    uint32_t id = 0; assert(host(app).startHome(app,1,1,request(method),id) == Probe::Action::OK);
    assert(id && view(id).homeContext && axisReserved(*app,1)); return id;
}
void productionAndFixtureGates() {
    fresh(); uint32_t unchanged = 77;
    assert(host(app).startHome(app,1,1,request(),unchanged) != Probe::Action::OK);
    assert(unchanged == 77 && !hardware.writes && !axisReserved(*app,1));
    qualify();
    for (unsigned parameter = 0; parameter < 4; ++parameter) {
        auto mismatch = request();
        if (parameter == 0) mismatch.method = ESS::HomingMethod::METHOD_33;
        if (parameter == 1) mismatch.searchSpeed = 61;
        if (parameter == 2) mismatch.returnSpeed = 31;
        if (parameter == 3) mismatch.rampTime = 101;
        assert(host(app).startHome(app,2,1,mismatch,unchanged) == Probe::Action::INVALID);
        assert(unchanged == 77 && !hardware.writes && !app->owner.pending() && !axisReserved(*app,1));
    }
    for (int method : {1,18,24,-1,15}) {
        assert(host(app).startHome(app,2,1,request(method),unchanged) == Probe::Action::UNSUPPORTED);
        assert(unchanged == 77 && !hardware.writes && !app->owner.pending());
    }
    app->homePrerequisites.indexQualified = false;
    assert(host(app).startHome(app,3,1,request(33),unchanged) == Probe::Action::INVALID);
    auto r = request(); r.offset = 1;
    assert(host(app).startHome(app,4,1,r,unchanged) == Probe::Action::UNSUPPORTED);
    assert(unchanged == 77 && !hardware.writes);
}
void completeAndInvalidateReference() {
    fresh(); qualify(); const uint32_t generation = app->axis.generation; const auto id = admit();
    reply(id); assert(hardware.tx.size() == 21 && hardware.tx[3] == 0x31 && hardware.tx[5] == 6);
    assert(!app->axis.originKnown && app->axis.generation == generation + 1);
    assert(view(id).homeContext->request.configurationGeneration == generation);
    reply(id); assert(hardware.tx[3] == 0x27 && hardware.tx[5] == 16);
    reply(id, words(0,3)); assert(view(id).pending && !app->coordinateReference.nativeKnown);
    reply(id, words(0,0));
    assert(!view(id).pending && view(id).homeContext->completion == ActionCompletion::OBSERVED);
    assert(!axisReserved(*app,1) && app->coordinateReference.nativeKnown && app->coordinateReference.nativePosition == 0);
    assert(app->coordinateReference.configurationGeneration == app->axis.generation && !app->axis.originKnown);
    assert(view(id).homeContext->request.configurationGeneration == generation);
}
void oldFlagsAndPartialSetup() {
    fresh(); qualify(true); auto id = admit(); reply(id); reply(id);
    reply(id,words(0,3)); assert(view(id).pending && !view(id).homeContext->homedLowObserved);
    assert(!app->coordinateReference.nativeKnown);
    assert(cancel(app,id) == Probe::Action::OK); pump();
    assert(!view(id).pending && view(id).homeContext->uncertain && axisReserved(*app,1));
    const auto writes = hardware.writes; pump(); assert(hardware.writes == writes);
    fresh(); qualify(); id = admit(); reply(id,crc({1,0x90,3}));
    assert(!view(id).pending && view(id).homeContext->setupExecution == ActionExecution::REJECTED);
    assert(view(id).homeContext->uncertain && view(id).homeContext->execution == ActionExecution::NOT_TRANSMITTED && axisReserved(*app,1));
    assert(hardware.writes == 1 && !app->axis.originKnown && !app->coordinateReference.nativeKnown);
    fresh(); qualify(); id = admit(); reply(id); waitTx(id);
    for (unsigned i = 0; i < 40000 && view(id).pending; ++i) step();
    assert(!view(id).pending && view(id).homeContext->execution == ActionExecution::UNKNOWN && app->owner.needsRecovery());
    const auto sent = hardware.writes; pump(); assert(hardware.writes == sent && axisReserved(*app,1));
    fresh(); qualify(); id = admit(); reply(id); reply(id); reply(id,words(0,4));
    assert(!view(id).pending && view(id).homeContext->outcome == ActionOutcome::REPLY_ERROR);
    assert(!app->coordinateReference.nativeKnown && view(id).homeContext->uncertain && axisReserved(*app,1));
    const auto unexpected = hardware.writes; pump(); assert(hardware.writes == unexpected);
}
void stopAtBoundariesAndUnsentExpiry() {
    for (unsigned phase = 0; phase < 5; ++phase) {
        fresh(); qualify(); const auto id = admit();
        if (phase >= 1) reply(id);
        if (phase >= 2) reply(id);
        if (phase >= 3) reply(id,words(0,3));
        if (phase == 4) waitTx(id);
        ActionRequest stop; stop.kind = ActionKind::STOP;
        stop.stop.behavior = StopBehavior::CONFIGURED_DECELERATION; uint32_t stopping = 0;
        assert(host(app).startAction(app,50,1,stop,stopping) == Probe::Action::OK);
        if (phase == 4) reply(id, words(0,0));
        pump(); assert(!view(id).pending && view(id).interruptedByStop);
        assert(!app->coordinateReference.nativeKnown);
        reply(stopping); reply(stopping,words(0,1));
        assert(!view(stopping).pending && !axisReserved(*app,1));
        assert(view(id).homeContext->completion == ActionCompletion::NOT_OBSERVED || phase == 4);
    }
    fresh(); qualify(); app->homePrerequisites.maximumAgeUs = 20000; const auto id = admit();
    const auto cap = view(id).homeContext->prerequisites.observedUs + 20000;
    advanceHardware(cap + 1000); loop(); pump();
    assert(!view(id).pending && !hardware.writes && !axisReserved(*app,1));
    assert(view(id).homeContext->status.detail == static_cast<int32_t>(ESS::HomeError::READINESS));
    fresh(); qualify(); const auto changed = admit(); reply(changed);
    invalidateAxis(*app); pump();
    assert(!view(changed).pending && view(changed).homeContext->outcome == ActionOutcome::CANCELLED && hardware.writes == 1);
    assert(!app->coordinateReference.nativeKnown && view(changed).homeContext->uncertain);
    fresh(); qualify(); app->axis.generation = UINT32_MAX;
    app->homePrerequisites.configurationGeneration = UINT32_MAX;
    const auto exhausted = admit(); reply(exhausted); pump();
    assert(!view(exhausted).pending && app->axis.generation == 0 && hardware.writes == 1);
    assert(view(exhausted).homeContext->outcome == ActionOutcome::CANCELLED);
    assert(!app->coordinateReference.nativeKnown && view(exhausted).homeContext->uncertain);
}

void orderedSharedMotionCachePreservesNewHomeReference() {
    // Each cache value represents a separately checked shared read. Exercise
    // the actual application loop's ordering against correlated home evidence.
    for (unsigned scenario = 0; scenario < 5; ++scenario) {
        fresh(); qualify(); app->homePrerequisites.qualifiedMethod = ESS::HomingMethod::METHOD_33;
        const auto id = admit(33); reply(id); reply(id); reply(id,words(0,4));
        auto& motion = app->stateCache.blocks[static_cast<uint8_t>(ESS::StateBlock::MOTION)];
        motion.valid = true; motion.value.target = app->axis.target; motion.value.rawAlarm = 0;
        const bool released = scenario >= 3;
        motion.value.rawMotion = released ? 16 : 4;
        motion.value.running = !released; motion.value.released = released;
        motion.value.enabled = !released; motion.value.alarmFlag = motion.value.inPosition = false;
        motion.observedEarliestUs = motion.observedLatestUs = hardware.time;
        const uint64_t oldLatest = motion.observedLatestUs;
        reply(id,words(0,3));
        const uint64_t stationary = view(id).homeContext->completionObservedUs;
        assert(oldLatest < stationary);
        // Equal bounds overlap the new conservative witness; newer values
        // remain evidence of external activity/release after the reference.
        if (scenario == 1 || scenario == 4)
            motion.observedEarliestUs = motion.observedLatestUs = stationary;
        if (scenario == 2)
            motion.observedEarliestUs = motion.observedLatestUs = hardware.time;
        reply(id,words(0,0));
        assert(!view(id).pending && view(id).homeContext->state == ActionState::SUCCEEDED);
        const bool definitelyOlder = scenario == 0 || scenario == 3;
        assert(app->coordinateReference.nativeKnown == definitelyOlder);
        assert(!app->axis.originKnown);
        if (definitelyOlder) {
            assert(app->coordinateReference.observedUs == stationary);
            const auto reference = axisReference(*app);
            assert(reference.nativeKnown && !reference.stationary);
            const unsigned writes = hardware.writes;
            assert(!setAxisOrigin(app->axis,0,reference));
            assert(!app->axis.originKnown && hardware.writes == writes);
        }
    }
    // An origin without a newer native witness still uses the cached external
    // activity rule. Old timestamps alone do not grant coordinate authority.
    fresh(); qualify(); assert(app->axis.originKnown && !app->coordinateReference.nativeKnown);
    auto& motion = app->stateCache.blocks[static_cast<uint8_t>(ESS::StateBlock::MOTION)];
    motion.value.running = true; motion.value.rawMotion = 4;
    step(); assert(!app->axis.originKnown && !app->coordinateReference.nativeKnown);
}
void refreshState(uint16_t position) {
    uint32_t id = 0;
    assert(startTypedRead(app,88,1,ESS::ReadKind::STATE,id,false) == Probe::Action::OK);
    const std::vector<uint8_t> responses[] = {
        words(0,3), words(0,0), crc({1,3,6,0,0,static_cast<uint8_t>(position >> 8),static_cast<uint8_t>(position),0,0})
    };
    for (const auto& response : responses) {
        const auto token = findRecord(*app,id)->read.step;
        waitTx(id);
        scheduleReply(std::max(hardware.writeStarted + hardware.tx.size()*87 + 1000, hardware.time + 1000), response);
        for (unsigned i = 0; i < 25000 && view(id).pending && findRecord(*app,id)->read.step == token; ++i) step();
        assert(!view(id).pending || findRecord(*app,id)->read.step != token);
    }
    assert(!view(id).pending);
}
void feedbackAfterHomeUsesTheNewZeroWitness() {
    for (unsigned scenario = 0; scenario < 5; ++scenario) {
        fresh(); qualify();
        app->configuration.algorithmKnown = true;
        app->configuration.wordOrderKnown = scenario < 3;
        app->configuration.wordOrder = ESS::WordOrder::HIGH_WORD_FIRST;
        app->configuration.algorithm = ESS::ControlAlgorithm::ALGORITHM_1;
        auto& historical = app->stateCache.blocks[2];
        historical.valid = true; historical.value.target = app->axis.target;
        historical.value.pairKnown = true; historical.value.rawPosition = 123;
        historical.value.rawPositionWords[1] = 123;
        historical.value.configOperationId = app->configuration.operationId;
        historical.observedEarliestUs = historical.observedLatestUs = hardware.time;
        const auto id = admit(); reply(id); reply(id); reply(id,words(0,3)); reply(id,words(0,0));
        assert(app->coordinateReference.nativeKnown && historical.invalidatedUs);
        const auto generation = app->axis.generation;
        // A new zero matches the correlated home witness even though the older,
        // invalidated feedback reports a different position. A first new nonzero
        // or a later change from fresh zero must invalidate that witness.
        const bool firstNonzero = scenario == 2 || scenario == 4;
        refreshState(firstNonzero ? 1 : 0);
        assert(app->coordinateReference.nativeKnown == !firstNonzero);
        assert(app->axis.generation == generation + (firstNonzero ? 1U : 0U));
        assert(app->stateCache.blocks[2].value.pairKnown == (scenario < 3));
        if (scenario == 1 || scenario == 3) {
            refreshState(1);
            assert(!app->coordinateReference.nativeKnown && app->axis.generation == generation + 1);
        }
        assert(!app->axis.originKnown);
    }
}
}
int main() {
    productionAndFixtureGates(); completeAndInvalidateReference(); oldFlagsAndPartialSetup();
    stopAtBoundariesAndUnsentExpiry(); orderedSharedMotionCachePreservesNewHomeReference();
    feedbackAfterHomeUsesTheNewZeroWitness();
}
