// SPDX-License-Identifier: MIT
#include <MotorControlRS/profiles/ess_rs/Position.h>
#include <cassert>
#include <cstring>
#include <limits>
#include <vector>

using namespace MotorControlRS;
namespace Ess = MotorControlRS::ESS_RS;
namespace {
template<class T> struct Saved {
    unsigned char bytes[sizeof(T)];
    explicit Saved(const T& value) { std::memcpy(bytes, &value, sizeof(value)); }
    void check(const T& value) const { assert(std::memcmp(bytes, &value, sizeof(value)) == 0); }
};
void crc(std::vector<uint8_t>& b) {
    uint16_t value = 0xFFFF;
    for (uint8_t byte : b) {
        value ^= byte;
        for (unsigned bit = 0; bit < 8; ++bit) value = (value & 1) ? (value >> 1) ^ 0xA001 : value >> 1;
    }
    b.push_back(static_cast<uint8_t>(value)); b.push_back(static_cast<uint8_t>(value >> 8));
}
AxisConfig axis() {
    AxisConfig a; a.target.id = 3; a.target.address = 1; a.target.generation = 4;
    a.generation = 7; a.supportedRelativeBases = 1;
    a.nativeMinimum = std::numeric_limits<int32_t>::min();
    a.nativeMaximum = std::numeric_limits<int32_t>::max();
    return a;
}
MoveRequest request(int64_t native = 25) {
    MoveRequest r; r.position.value = Rational(native); r.position.configurationGeneration = 7;
    r.speedRpm = 60; r.ramp = MoveRamp::VERIFIED_CONFIGURED; return r;
}
Ess::MovePrerequisites prerequisites() {
    Ess::MovePrerequisites p; p.target = axis().target; p.configurationGeneration = 7;
    p.commandUnitsVerified = p.relativeBasisVerified = p.configuredRampVerified = true;
    p.serialInputsPermit = p.readinessQualified = p.wordOrderKnown = p.startSpeedKnown = true;
    p.accelerationTime = 100; p.decelerationTime = 120; p.startSpeed = 30;
    p.subdivision = 1000; p.observedUs = 80; p.maximumAgeUs = 1000; p.rawMotion = 1; return p;
}
ActionOptions options() { ActionOptions o; o.pollIntervalUs = 100; o.maxPolls = 4; return o; }
Ess::MoveContext move(uint64_t deadline = 10000) {
    Ess::MoveContext c;
    auto o = options();
    assert(Ess::prepareMoveRelative(c, axis(), nullptr, 12, request(), prerequisites(), 100, deadline, o));
    return c;
}
ActionEvent local(const Ess::MoveContext& c, ReadEventKind kind) {
    ActionEvent e; e.transport.target = c.target; e.transport.operationId = c.operationId;
    e.transport.step = c.step; e.transport.kind = kind; return e;
}
ActionEvent frame(const Ess::MoveContext& c, const std::vector<uint8_t>& bytes, uint64_t at) {
    auto e = local(c, ReadEventKind::FRAME);
    e.transport.frame = bytes.data(); e.transport.length = bytes.size();
    e.transport.txAccepted = c.step == 0 ? 19 : 8;
    e.transport.qualified = e.responseConfirmed = e.txComplete = true;
    e.transport.earliestUs = at; e.transport.latestUs = at + 10; return e;
}
std::vector<uint8_t> reply(const Ess::MoveContext& c, uint16_t flags = 1, uint16_t alarm = 0) {
    std::vector<uint8_t> b;
    if (c.step == 0) b = {1, 16, 0, 0x21, 0, 5};
    else if (c.step == 1) b = {1, 6, 0, 0x27, 0, static_cast<uint8_t>(c.request.position.relative ? 1 : 5)};
    else b = {1, 3, 4, static_cast<uint8_t>(alarm >> 8), static_cast<uint8_t>(alarm),
        static_cast<uint8_t>(flags >> 8), static_cast<uint8_t>(flags)};
    crc(b); return b;
}
void consume(Ess::MoveContext& c, uint16_t flags = 1, uint16_t alarm = 0) {
    const auto bytes = reply(c, flags, alarm);
    const uint64_t at = c.eligibleUs + 20;
    assert(Ess::advanceMove(c, frame(c, bytes, at), at + 20));
}
void trigger(Ess::MoveContext& c) { consume(c); consume(c); assert(c.step == 2); }
void noMoreWork(const Ess::MoveContext& c) {
    Ess::PreparedMove p; assert(Ess::nextMove(c, c.servicedUs, p));
    assert(p.kind == Ess::ActionWork::DONE && p.length == 0);
}
void testExactStageTriggerAndCommonParity() {
    auto c = move(); Ess::PreparedMove p;
    assert(Ess::nextMove(c, 100, p) && p.kind == Ess::ActionWork::TRANSACTION);
    const uint8_t expected[] = {1, 16, 0, 0x21, 0, 5, 10, 0, 100, 0, 120, 0, 60, 0, 0, 0, 25};
    assert(p.length == 19 && p.function == 16 && p.count == 5 && p.write);
    assert(std::memcmp(p.bytes, expected, sizeof(expected)) == 0);
    Ess::PreparedMove repeated; assert(Ess::nextMove(c, 100, repeated));
    assert(repeated.step == p.step && repeated.length == p.length && std::memcmp(p.bytes, repeated.bytes, p.length) == 0);
    Ess::MoveContext common;
    assert(MotorControlRS::prepareMoveRelative(common, axis(), nullptr, 12, request(), prerequisites(), 100, 10000, options()));
    assert(Ess::nextMove(common, 100, repeated) && std::memcmp(p.bytes, repeated.bytes, p.length) == 0);
    // Native relative work has no unrelated origin, gear, lead or encoder metadata.
    assert(!axis().originKnown && !c.prepared.endpointKnown && c.prepared.displacementNative == 25);
    consume(c); assert(c.stagingApplied && c.setupExecution == ActionExecution::ACKNOWLEDGED);
    assert(c.execution == ActionExecution::NOT_TRANSMITTED && c.completion == ActionCompletion::NOT_OBSERVED);
    assert(Ess::nextMove(c, c.servicedUs, p));
    assert(p.function == 6 && p.reg == 0x27 && p.value == 1 && p.length == 8 && p.step == 1);
    consume(c); assert(c.execution == ActionExecution::ACKNOWLEDGED);
    assert(Ess::nextMove(c, c.servicedUs, p) && p.kind == Ess::ActionWork::WAIT && p.length == 0);
    assert(Ess::nextMove(c, c.eligibleUs, p) && p.function == 3 && p.reg == 6 && p.count == 2);
    consume(c, 1); // Old arrival does not complete a new operation.
    assert(c.state == ActionState::ACTIVE && !c.runningObserved);
    consume(c, 0x8004); assert(c.runningObserved && c.rawMotion == 0x8004);
    const auto activity = c.activityEvidence;
    consume(c, 1); assert(c.state == ActionState::SUCCEEDED && c.completion == ActionCompletion::OBSERVED);
    assert(!c.uncertain && c.activityEvidence.step == activity.step && c.activityEvidence.latestUs < c.lastObservation.earliestUs);
    noMoreWork(c);
}
void testTriggerObservationKeepsUnknownExecution() {
    {
        auto c = move(10000); consume(c);
        const auto bytes = reply(c);
        auto e = frame(c, bytes, c.eligibleUs + 20); e.responseConfirmed = false;
        assert(Ess::advanceMove(c, e, c.eligibleUs + 40));
        assert(c.execution == ActionExecution::UNKNOWN && !c.triggerEvidence.responseConfirmed);
        assert(c.state == ActionState::ACTIVE && c.step == 2);
        const Saved<Ess::ActionEvidence> original(c.triggerEvidence);
        Ess::PreparedMove work; assert(Ess::nextMove(c, c.eligibleUs, work));
        assert(!work.write && work.function == 3);
        consume(c, 1); // Preexisting ARRIVED is insufficient.
        assert(c.state == ActionState::ACTIVE && !c.runningObserved);
        consume(c, 4); assert(c.runningObserved && c.activityEvidence.responseConfirmed);
        consume(c, 1);
        assert(c.state == ActionState::SUCCEEDED && c.completion == ActionCompletion::OBSERVED);
        assert(c.execution == ActionExecution::UNKNOWN && c.lastObservation.responseConfirmed);
        original.check(c.triggerEvidence); noMoreWork(c);
    }
    // FC10 staging still needs a confirmed source, before any trigger.
    auto stage = move(10000); const auto staged = reply(stage);
    auto e = frame(stage, staged, 120); e.responseConfirmed = false;
    assert(Ess::advanceMove(stage, e, 140));
    assert(stage.outcome == ActionOutcome::UNCONFIRMED_RESPONSE && stage.step == 0);
    assert(!stage.stagingApplied && stage.setupExecution == ActionExecution::UNKNOWN);
    noMoreWork(stage);
    // Unconfirmed status cannot establish activity or completion.
    auto c = move(10000); consume(c);
    auto bytes = reply(c); e = frame(c, bytes, c.eligibleUs + 20); e.responseConfirmed = false;
    assert(Ess::advanceMove(c, e, c.eligibleUs + 40));
    bytes = reply(c, 4); e = frame(c, bytes, c.eligibleUs); e.responseConfirmed = false;
    assert(Ess::advanceMove(c, e, c.eligibleUs + 20));
    assert(c.outcome == ActionOutcome::UNCONFIRMED_RESPONSE && !c.runningObserved && !c.observationKnown);
    assert(c.execution == ActionExecution::UNKNOWN); noMoreWork(c);
    // A finite move that is missed between polls stays unobserved.
    c = move(10000); consume(c);
    bytes = reply(c); e = frame(c, bytes, c.eligibleUs + 20); e.responseConfirmed = false;
    assert(Ess::advanceMove(c, e, c.eligibleUs + 40));
    for (unsigned i = 0; i < c.options.maxPolls; ++i) consume(c, 1);
    assert(c.outcome == ActionOutcome::OBSERVATION_LIMIT && !c.runningObserved);
    assert(c.execution == ActionExecution::UNKNOWN && c.completion == ActionCompletion::NOT_OBSERVED);
    noMoreWork(c);
}
void testTriggerPreservesFailures() {
    for (unsigned fault = 0; fault < 8; ++fault) {
        auto c = move(1000); consume(c);
        auto bytes = reply(c);
        if (fault == 0) bytes.back() ^= 1;
        if (fault == 1) { bytes = {1, 6, 0, 0x27, 0, 5}; crc(bytes); }
        if (fault == 2) { bytes = {1, 0x86, 2}; crc(bytes); }
        auto e = frame(c, bytes, fault == 4 ? 995 : 200); e.responseConfirmed = false;
        const uint64_t now = fault == 4 ? 1020 : 220;
        if (fault == 3) { e.transport.qualified = false; e.transport.earliestUs = e.transport.latestUs = 0; }
        if (fault == 5) { e = local(c, ReadEventKind::TRANSPORT_FAILURE); e.transport.txAccepted = 8; e.txComplete = true; }
        if (fault == 6) e.txComplete = false;
        if (fault == 7) { e.transport.txAccepted = 7; e.txComplete = false; }
        if (fault >= 6) {
            const Saved<Ess::MoveContext> saved(c);
            assert(!Ess::advanceMove(c, e, now)); saved.check(c); continue;
        }
        assert(Ess::advanceMove(c, e, now));
        const ActionOutcome expected = fault < 3 ? ActionOutcome::REPLY_ERROR : fault == 3 ?
            ActionOutcome::TIMING_UNQUALIFIED : fault == 4 ? ActionOutcome::DEADLINE : ActionOutcome::TRANSPORT_ERROR;
        assert(c.state == ActionState::FAILED && c.outcome == expected && c.execution == ActionExecution::UNKNOWN);
        assert(c.uncertain && !c.observationKnown && c.completion == ActionCompletion::NOT_OBSERVED);
        noMoreWork(c);
    }
}
void testAllPreparationGatesLeaveOutputUnchanged() {
    auto c = move();
    for (unsigned fault = 0; fault < 29; ++fault) {
        auto a = axis(); auto r = request(); auto p = prerequisites(); auto o = options();
        uint32_t id = 12; uint64_t now = 100, deadline = 10000;
        switch (fault) {
        case 0: a.target.address = 248; break;
        case 1: id = 0; break;
        case 2: deadline = now; break;
        case 3: o.maxPolls = 65; break;
        case 4: o.pollIntervalUs = 0; break;
        case 5: r.position.relative = false; break;
        case 6: r.position.basis = RelativeBasis::COMMANDED; break;
        case 7: ++p.target.id; break;
        case 8: ++p.target.address; break;
        case 9: ++p.target.generation; break;
        case 10: ++p.configurationGeneration; break;
        case 11: p.commandUnitsVerified = false; break;
        case 12: p.relativeBasisVerified = false; break;
        case 13: p.configuredRampVerified = false; break;
        case 14: r.ramp = MoveRamp::UNSPECIFIED; break;
        case 15: p.accelerationTime = 2001; break;
        case 16: p.decelerationTime = 2001; break;
        case 17: r.speedRpm = 0; break;
        case 18: r.speedRpm = 3001; break;
        case 19: p.startSpeedKnown = false; break;
        case 20: p.startSpeed = 61; break;
        case 21: p.wordOrderKnown = false; break;
        case 22: p.wordOrder = static_cast<Ess::WordOrder>(99); break;
        case 23: p.serialInputsPermit = false; break;
        case 24: p.readinessQualified = false; break;
        case 25: p.observedUs = 101; break;
        case 26: p.maximumAgeUs = 1; break;
        case 27: r.position.value.numerator = 0; break;
        case 28: p.maximumAgeUs = now - p.observedUs; break;
        }
        const Saved<Ess::MoveContext> saved(c);
        assert(!Ess::prepareMoveRelative(c, a, nullptr, id, r, p, now, deadline, o)); saved.check(c);
    }
    for (uint16_t badFlags : {4, 8, 16, 32, 64}) {
        auto p = prerequisites(); p.rawMotion = badFlags;
        const Saved<Ess::MoveContext> saved(c);
        assert(!Ess::prepareMoveRelative(c, axis(), nullptr, 12, request(), p, 100, 10000)); saved.check(c);
    }
    auto p = prerequisites(); p.rawAlarm = 1;
    const Saved<Ess::MoveContext> saved(c);
    assert(!Ess::prepareMoveRelative(c, axis(), nullptr, 12, request(), p, 100, 10000)); saved.check(c);
    p = prerequisites(); auto r = request(-1);
    assert(!Ess::prepareMoveRelative(c, axis(), nullptr, 12, r, p, 100, 10000)); saved.check(c);
    auto a = axis(); a.nativeMaximum = 20;
    assert(!Ess::prepareMoveRelative(c, a, nullptr, 12, request(), p, 100, 10000)); saved.check(c);
    r = request(); ++r.position.configurationGeneration;
    assert(!Ess::prepareMoveRelative(c, axis(), nullptr, 12, r, p, 100, 10000)); saved.check(c);
    a = axis(); a.softLimitsKnown = true; a.softMinimum = -100; a.softMaximum = 100;
    assert(!Ess::prepareMoveRelative(c, a, nullptr, 12, request(), p, 100, 10000)); saved.check(c);
}
void testSignWordOrderAndRange() {
    for (int64_t target : {int64_t(std::numeric_limits<int32_t>::min()), int64_t(-25), int64_t(25),
                          int64_t(std::numeric_limits<int32_t>::max())}) {
        for (Ess::WordOrder order : {Ess::WordOrder::HIGH_WORD_FIRST, Ess::WordOrder::LOW_WORD_FIRST}) {
            auto p = prerequisites(); p.negativeTwosComplementVerified = true; p.wordOrder = order;
            Ess::MoveContext c; assert(Ess::prepareMoveRelative(c, axis(), nullptr, 1, request(target), p, 100, 10000));
            const uint32_t raw = static_cast<uint32_t>(target);
            const unsigned high = order == Ess::WordOrder::HIGH_WORD_FIRST ? 3 : 4;
            assert(c.words[high] == raw >> 16 && c.words[high == 3 ? 4 : 3] == (raw & 0xFFFF));
        }
    }
    auto a = axis(); a.nativeMinimum = std::numeric_limits<int64_t>::min(); a.nativeMaximum = std::numeric_limits<int64_t>::max();
    Ess::MoveContext c;
    assert(!Ess::prepareMoveRelative(c, a, nullptr, 1, request(int64_t(std::numeric_limits<int32_t>::max()) + 1), prerequisites(), 100, 10000));
}
void testAdmittedPrerequisitesAreCopied() {
    auto p = prerequisites(); Ess::MoveContext c;
    assert(Ess::prepareMoveRelative(c, axis(), nullptr, 12, request(), p, 100, 10000));
    const auto admitted = c.prerequisites;
    p.target.address = 2; p.configurationGeneration = 8;
    p.commandUnitsVerified = p.configuredRampVerified = p.readinessQualified = false;
    p.accelerationTime = 2000; p.observedUs = 9999;
    assert(c.prerequisites.target.address == admitted.target.address &&
        c.prerequisites.configurationGeneration == admitted.configurationGeneration &&
        c.prerequisites.commandUnitsVerified && c.prerequisites.configuredRampVerified && c.prerequisites.readinessQualified &&
        c.prerequisites.observedUs == 80 && c.prerequisites.accelerationTime == 100 && c.words[0] == 100);
}
void testCancelFailureAndDeadlineAtEveryBoundary() {
    for (unsigned phase = 0; phase < 4; ++phase) {
        for (ReadEventKind kind : {ReadEventKind::CANCEL, ReadEventKind::TRANSPORT_FAILURE, ReadEventKind::DEADLINE}) {
            for (unsigned prefix = 0; prefix < 3; ++prefix) {
                auto c = move();
                if (phase >= 1) consume(c);
                if (phase >= 2) consume(c);
                if (phase >= 3) consume(c, 4);
                auto e = local(c, kind);
                const std::size_t length = c.step == 0 ? 19 : 8;
                e.transport.txAccepted = prefix == 0 ? 0 : prefix == 1 ? 3 : length;
                e.txComplete = prefix == 2;
                e.transport.transportDetail = 117;
                assert(Ess::advanceMove(c, e, kind == ReadEventKind::DEADLINE ? c.deadlineUs : c.servicedUs + 20));
                assert(c.state == ActionState::FAILED && c.completion == ActionCompletion::NOT_OBSERVED);
                assert(c.uncertain == (phase != 0 || prefix != 0));
                if (phase == 0) assert(c.execution == ActionExecution::NOT_TRANSMITTED);
                if (phase == 1) assert(c.execution == (prefix ? ActionExecution::UNKNOWN : ActionExecution::NOT_TRANSMITTED));
                if (phase >= 2) assert(c.execution == ActionExecution::ACKNOWLEDGED);
                assert(c.failureEvidence.transportDetail == 117 && c.deadlineUs == 10000);
                noMoreWork(c);
                const Saved<Ess::MoveContext> saved(c);
                assert(!Ess::advanceMove(c, e, c.servicedUs)); saved.check(c);
            }
        }
    }
}
void testBadRepliesAndNoAutomaticReplay() {
    for (unsigned phase = 0; phase < 3; ++phase) {
        for (unsigned fault = 0; fault < 7; ++fault) {
            auto c = move(); for (unsigned i = 0; i < phase; ++i) consume(c);
            auto bytes = reply(c);
            if (fault == 0) bytes.back() ^= 1;
            else if (fault == 1) { bytes[0] = 2; bytes.resize(bytes.size() - 2); crc(bytes); }
            else if (fault == 2) { bytes[1] = 4; bytes.resize(bytes.size() - 2); crc(bytes); }
            else if (fault == 3) { bytes[3] ^= 1; bytes.resize(bytes.size() - 2); crc(bytes); }
            else if (fault == 4) { bytes = {1, static_cast<uint8_t>(phase == 0 ? 0x90 : phase == 1 ? 0x86 : 0x83), 3}; crc(bytes); }
            else if (fault == 5) { bytes = {1, static_cast<uint8_t>(phase == 0 ? 0x90 : phase == 1 ? 0x86 : 0x83), 0xE7}; crc(bytes); }
            else bytes.resize(40, 0x55);
            const uint64_t at = c.eligibleUs + 20;
            auto e = frame(c, bytes, at);
            // FC03 fault3 modifies payload into a nonzero alarm, rather than echo.
            assert(Ess::advanceMove(c, e, at + 20));
            assert(c.state == ActionState::FAILED && c.uncertain && c.completion == ActionCompletion::NOT_OBSERVED);
            assert(c.failureEvidence.receivedLength == bytes.size() && c.failureEvidence.length <= Ess::ACTION_MAX_REPLY_BYTES);
            if (phase == 0 && fault == 4) assert(c.setupExecution == ActionExecution::REJECTED);
            if (phase == 0 && fault == 5) assert(c.setupExecution == ActionExecution::UNKNOWN);
            if (phase == 1 && fault == 4) assert(c.execution == ActionExecution::REJECTED);
            if (phase == 1 && fault == 5) assert(c.execution == ActionExecution::UNKNOWN);
            noMoreWork(c);
        }
    }
    auto c = move(); const auto bytes = reply(c); auto e = frame(c, bytes, 120); e.responseConfirmed = false;
    assert(Ess::advanceMove(c, e, 140));
    assert(c.outcome == ActionOutcome::UNCONFIRMED_RESPONSE && c.setupExecution == ActionExecution::UNKNOWN && c.uncertain);
}
void testEnvelopeCorrelationAndCopiedEvidence() {
    auto c = move(); auto bytes = reply(c); const auto good = frame(c, bytes, 120);
    for (unsigned fault = 0; fault < 13; ++fault) {
        auto e = good;
        switch (fault) {
        case 0: ++e.transport.operationId; break;
        case 1: ++e.transport.step; break;
        case 2: ++e.transport.target.id; break;
        case 3: ++e.transport.target.address; break;
        case 4: ++e.transport.target.generation; break;
        case 5: e.transport.frame = nullptr; break;
        case 6: e.transport.txAccepted = 20; break;
        case 7: e.transport.txAccepted = 18; break;
        case 8: e.txComplete = false; break;
        case 9: e.transport.earliestUs = 99; break;
        case 10: e.transport.latestUs = 200; break;
        case 11: e.transport.qualified = false; break;
        case 12: e.transport.kind = static_cast<ReadEventKind>(99); break;
        }
        const Saved<Ess::MoveContext> saved(c);
        assert(!Ess::advanceMove(c, e, 140)); saved.check(c);
    }
    const Saved<Ess::MoveContext> saved(c);
    assert(!Ess::advanceMove(c, local(c, ReadEventKind::DEADLINE), 140)); saved.check(c);
    assert(Ess::advanceMove(c, good, 140));
    const auto evidence = c.stagingEvidence; std::memset(bytes.data(), 0xAA, bytes.size());
    assert(c.stagingEvidence.length == evidence.length && std::memcmp(c.stagingEvidence.raw, evidence.raw, evidence.length) == 0);
    const Saved<Ess::MoveContext> next(c);
    assert(!Ess::advanceMove(c, good, 140)); next.check(c);
    assert(!Ess::advanceMove(c, local(c, ReadEventKind::CANCEL), 139)); next.check(c);
}
void testFreshCompletionLimitsAndDelayedService() {
    auto c = move(); trigger(c);
    for (unsigned i = 0; i < 4; ++i) consume(c, 1);
    assert(c.state == ActionState::FAILED && c.outcome == ActionOutcome::OBSERVATION_LIMIT && !c.runningObserved && c.uncertain);
    auto late = move(1000); trigger(late); consume(late, 4);
    auto stopped = reply(late, 1);
    assert(Ess::advanceMove(late, frame(late, stopped, 980), 1200));
    assert(late.state == ActionState::SUCCEEDED && late.deadlineUs == 1000 && late.lastObservation.deliveredUs == 1200);
    auto straddle = move(1000); trigger(straddle); consume(straddle, 4);
    stopped = reply(straddle, 1);
    assert(Ess::advanceMove(straddle, frame(straddle, stopped, 995), 1200));
    assert(straddle.state == ActionState::FAILED && straddle.outcome == ActionOutcome::DEADLINE && straddle.uncertain);
    assert(straddle.lastObservation.step == 2 && straddle.rawMotion == 4);
    auto lateSetup = move(150); const auto stage = reply(lateSetup);
    assert(Ess::advanceMove(lateSetup, frame(lateSetup, stage, 120), 200));
    assert(lateSetup.stagingApplied && lateSetup.execution == ActionExecution::NOT_TRANSMITTED && lateSetup.uncertain);
    noMoreWork(lateSetup);
    auto stale = move(); trigger(stale);
    const Saved<Ess::MoveContext> saved(stale);
    auto staleEvent = frame(stale, stopped, stale.triggerEvidence.latestUs);
    assert(!Ess::advanceMove(stale, staleEvent, stale.eligibleUs + 20)); saved.check(stale);
    auto unqualified = move(); auto e = frame(unqualified, stage, 120);
    e.transport.qualified = false; e.transport.earliestUs = e.transport.latestUs = 0;
    assert(Ess::advanceMove(unqualified, e, 140));
    assert(unqualified.outcome == ActionOutcome::TIMING_UNQUALIFIED && unqualified.uncertain);
}
void testDriveFaultAndPreviousObservationRetention() {
    for (uint16_t flags : {uint16_t(8), uint16_t(16), uint16_t(32), uint16_t(64)}) {
        auto c = move(); trigger(c); consume(c, flags);
        assert(c.state == ActionState::FAILED && c.status.detail == static_cast<int32_t>(MoveError::DRIVE_FAULT));
        assert(c.observationKnown && c.rawMotion == flags && c.uncertain);
    }
    auto c = move(); trigger(c); consume(c, 4);
    const auto previous = c.lastObservation; auto bytes = reply(c, 1); bytes.back() ^= 1;
    assert(Ess::advanceMove(c, frame(c, bytes, c.eligibleUs), c.eligibleUs + 20));
    assert(c.rawMotion == 4 && c.lastObservation.step == previous.step && c.runningObserved);
    assert(c.execution == ActionExecution::ACKNOWLEDGED && c.completion == ActionCompletion::NOT_OBSERVED);
}
void testWriteDeadlinesRetainReadinessAndOperationBudgets() {
    auto c = move();
    const uint64_t readinessEnd = c.prerequisites.observedUs + c.prerequisites.maximumAgeUs;
    Ess::PreparedMove work;
    assert(Ess::nextMove(c, readinessEnd - 1, work));
    assert(work.length == 19 && work.deadlineUs == readinessEnd && c.deadlineUs == 10000);
    const Saved<Ess::PreparedMove> savedWork(work);
    const Saved<Ess::MoveContext> savedContext(c);
    assert(!Ess::nextMove(c, readinessEnd, work)); savedWork.check(work); savedContext.check(c);
    assert(!Ess::advanceMove(c, local(c, ReadEventKind::DEADLINE), readinessEnd - 1)); savedContext.check(c);
    assert(Ess::advanceMove(c, local(c, ReadEventKind::DEADLINE), readinessEnd));
    assert(c.outcome == ActionOutcome::DEADLINE && c.status.detail == static_cast<int32_t>(MoveError::READINESS));
    assert(!c.uncertain && c.execution == ActionExecution::NOT_TRANSMITTED && c.deadlineUs == 10000);
    noMoreWork(c);

    // A timely staging reply cannot authorize a trigger once its immutable
    // readiness has expired during delayed application delivery.
    auto delayedStage = move(); auto bytes = reply(delayedStage);
    assert(Ess::advanceMove(delayedStage, frame(delayedStage, bytes, readinessEnd - 20), readinessEnd + 20));
    assert(delayedStage.stagingApplied && delayedStage.uncertain && delayedStage.step == 0);
    assert(delayedStage.execution == ActionExecution::NOT_TRANSMITTED && delayedStage.deadlineUs == 10000);
    assert(delayedStage.status.detail == static_cast<int32_t>(MoveError::READINESS));
    noMoreWork(delayedStage);

    auto queuedTrigger = move(); consume(queuedTrigger);
    assert(Ess::nextMove(queuedTrigger, queuedTrigger.servicedUs, work));
    assert(work.length == 8 && work.deadlineUs == readinessEnd);
    auto expired = local(queuedTrigger, ReadEventKind::DEADLINE);
    assert(Ess::advanceMove(queuedTrigger, expired, readinessEnd));
    assert(queuedTrigger.stagingApplied && queuedTrigger.uncertain);
    assert(queuedTrigger.execution == ActionExecution::NOT_TRANSMITTED);
    assert(queuedTrigger.status.detail == static_cast<int32_t>(MoveError::READINESS));
    noMoreWork(queuedTrigger);

    for (bool triggering : {false, true}) {
        for (std::size_t prefix : {std::size_t(3), std::size_t(triggering ? 8 : 19)}) {
            auto interrupted = move(); if (triggering) consume(interrupted);
            auto expiry = local(interrupted, ReadEventKind::DEADLINE);
            expiry.transport.txAccepted = prefix;
            expiry.txComplete = prefix == (triggering ? 8 : 19);
            assert(Ess::advanceMove(interrupted, expiry, readinessEnd));
            assert(interrupted.uncertain && interrupted.status.detail == static_cast<int32_t>(MoveError::READINESS));
            assert((triggering ? interrupted.execution : interrupted.setupExecution) == ActionExecution::UNKNOWN);
            assert(interrupted.deadlineUs == 10000); noMoreWork(interrupted);
        }
    }

    // Physical trigger closure was timely; delayed delivery does not revoke it
    // or shorten the budget for observing the autonomous finite motion.
    auto delayedTrigger = move(); consume(delayedTrigger); bytes = reply(delayedTrigger);
    assert(Ess::advanceMove(delayedTrigger, frame(delayedTrigger, bytes, readinessEnd - 10), readinessEnd + 200));
    assert(delayedTrigger.execution == ActionExecution::ACKNOWLEDGED && delayedTrigger.state == ActionState::ACTIVE);
    assert(Ess::nextMove(delayedTrigger, delayedTrigger.eligibleUs, work));
    assert(work.function == 3 && work.deadlineUs == delayedTrigger.deadlineUs);
    consume(delayedTrigger, 4); consume(delayedTrigger, 1);
    assert(delayedTrigger.state == ActionState::SUCCEEDED && !delayedTrigger.uncertain);

    for (bool triggerReply : {false, true}) {
        auto late = move(); if (triggerReply) consume(late);
        bytes = reply(late);
        assert(Ess::advanceMove(late, frame(late, bytes, readinessEnd - 9), readinessEnd + 20));
        assert(late.state == ActionState::FAILED && late.uncertain && late.deadlineUs == 10000);
        assert(late.status.detail == static_cast<int32_t>(MoveError::READINESS));
        noMoreWork(late);
    }

    // Saturating the immutable age bound must neither wrap into an old deadline
    // nor renew the operation's earlier absolute deadline.
    const uint64_t maximum = std::numeric_limits<uint64_t>::max();
    auto p = prerequisites(); p.observedUs = maximum - 1000; p.maximumAgeUs = 2000;
    Ess::MoveContext huge;
    assert(Ess::prepareMoveRelative(huge, axis(), nullptr, 12, request(), p, maximum - 500, maximum - 1, options()));
    assert(Ess::nextMove(huge, maximum - 500, work));
    assert(work.deadlineUs == maximum - 1 && huge.deadlineUs == maximum - 1);
    assert(Ess::advanceMove(huge, local(huge, ReadEventKind::DEADLINE), maximum - 1));
    assert(huge.status.detail == static_cast<int32_t>(MoveError::DEADLINE_EXPIRED) && !huge.uncertain);
}
void testConsumedReferenceUsesOperationTimeAndCoversWrites() {
    auto a = axis(); a.softLimitsKnown = true; a.softMinimum = -100; a.softMaximum = 100;
    AxisReference reference; reference.target = a.target; reference.configurationGeneration = a.generation;
    reference.nativeKnown = true; reference.source = ScaleSource::QUALIFIED;
    reference.observedUs = 80; reference.nowUs = 100; reference.maximumAgeUs = 1000;
    auto p = prerequisites(); auto c = move();
    const Saved<AxisReference> savedReference(reference);
    assert(Ess::prepareMoveRelative(c, a, &reference, 12, request(), p, 100, 10000));
    assert(c.prepared.endpointKnown && c.prepared.endpointNative == 25);
    savedReference.check(reference);
    const Saved<Ess::MoveContext> saved(c);
    // A cached reference clock cannot hide a stale coordinate/limit witness.
    p.observedUs = 9990;
    assert(!Ess::prepareMoveRelative(c, a, &reference, 12, request(), p, 10000, 20000));
    saved.check(c); savedReference.check(reference);

    // A currently fresh endpoint can still expire while writes wait. Reject its
    // insufficient budget instead of retaining another overlapping clock field.
    p = prerequisites(); reference.maximumAgeUs = 30;
    const Saved<AxisReference> shortReference(reference);
    const auto shortBudget = Ess::prepareMoveRelative(c, a, &reference, 12, request(), p, 100, 10000);
    assert(!shortBudget && shortBudget.detail == static_cast<int32_t>(MoveError::READINESS));
    saved.check(c); shortReference.check(reference);
    p.maximumAgeUs = 30;
    assert(Ess::prepareMoveRelative(c, a, &reference, 12, request(), p, 100, 10000));
    Ess::PreparedMove work; assert(Ess::nextMove(c, 100, work));
    assert(work.deadlineUs == 110 && c.deadlineUs == 10000);
    p = prerequisites();
    assert(Ess::prepareMoveRelative(c, a, &reference, 12, request(), p, 100, 110));
    assert(Ess::nextMove(c, 100, work) && work.deadlineUs == 110);

    // Reference age uses the same saturating arithmetic as readiness.
    reference.maximumAgeUs = std::numeric_limits<uint64_t>::max();
    assert(Ess::prepareMoveRelative(c, a, &reference, 12, request(), p, 100, 10000));
    assert(c.prepared.endpointKnown);

    // Unestablished optional feedback remains irrelevant to an unbounded native
    // relative displacement, even with unrelated stale correlation metadata.
    reference.nativeKnown = false; reference.target.id = 999;
    reference.nowUs = 1; reference.observedUs = 0; reference.maximumAgeUs = 0;
    assert(Ess::prepareMoveRelative(c, axis(), &reference, 12, request(), p, 100, 10000));
    assert(!c.prepared.endpointKnown && c.prepared.effectiveNative == 25);
}
void testAbsoluteAndAngleReuseSequenceAndRetainReference() {
    auto a = axis(); a.originKnown = true; a.originSource = ScaleSource::QUALIFIED;
    a.units.commandStepsPerMotorTurn = UnitScale(1000, 1, ScaleSource::QUALIFIED);
    AxisReference ref; ref.target = a.target; ref.configurationGeneration = a.generation;
    ref.nativeKnown = ref.stationary = true; ref.source = ScaleSource::QUALIFIED;
    ref.nativePosition = 1990; ref.observedUs = 80; ref.maximumAgeUs = 1000; ref.nowUs = 1;
    auto r = request(); r.position.relative = false; r.position.frame = CoordinateFrame::MOTOR;
    r.position.unit = PositionUnit::DEGREES; r.position.value = Rational(720);
    auto p = prerequisites(); p.relativeBasisVerified = false; // Absolute has no relative-basis dependency.
    Ess::MoveContext c, common;
    assert(Ess::prepareMoveAbsolute(c, a, &ref, 12, r, p, 100, 10000, options()));
    assert(MotorControlRS::prepareMoveAbsolute(common, a, &ref, 12, r, p, 100, 10000, options()));
    assert(c.prepared.effectiveNative == 2000 && c.prepared.displacementNative == 10);
    assert(c.words[3] == 0 && c.words[4] == 2000 && common.words[4] == c.words[4]);
    ref.nativePosition = -999; ref.maximumAgeUs = 0; // Admitted context owns its reference.
    assert(c.reference.nativePosition == 1990 && c.reference.nowUs == 100);
    consume(c); Ess::PreparedMove work; assert(Ess::nextMove(c, c.servicedUs, work));
    assert(work.value == 5 && work.bytes[5] == 5 && work.deadlineUs == 1080);
    consume(c); consume(c, 1); assert(c.state == ActionState::ACTIVE); // A stale arrival is not completion.
    consume(c, 4); consume(c, 1); assert(c.state == ActionState::SUCCEEDED);

    ref = common.reference; ref.nativePosition = 990;
    r.position.wrapped = true; r.position.path = AnglePath::POSITIVE; r.position.tie = HalfTurnTie::REJECT;
    r.position.value = Rational(0);
    assert(MotorControlRS::prepareMoveAngle(c, a, &ref, 12, r, p, 100, 10000, options()));
    assert(c.prepared.effectiveNative == 1000 && c.prepared.displacementNative == 10);
    consume(c); assert(Ess::nextMove(c, c.servicedUs, work) && work.value == 5);
    // A response echoing relative start cannot authorize an absolute start.
    std::vector<uint8_t> echo = {1, 6, 0, 0x27, 0, 1}; crc(echo);
    assert(Ess::advanceMove(c, frame(c, echo, c.eligibleUs + 20), c.eligibleUs + 40));
    assert(c.state == ActionState::FAILED && c.execution == ActionExecution::UNKNOWN && c.uncertain);
    noMoreWork(c);
}
void testNativeAbsoluteDoesNotInventDisplacement() {
    auto a = axis(); auto r = request(0); r.position.relative = false;
    auto p = prerequisites(); auto o = options();
    Ess::MoveContext c;
    assert(Ess::prepareMoveAbsolute(c, a, nullptr, 12, r, p, 100, 10000, o));
    assert(c.prepared.endpointKnown && c.prepared.endpointNative == 0 && c.prepared.effectiveNative == 0);
    assert(!c.prepared.displacementKnown && !c.prepared.zeroDisplacement && !c.reference.nativeKnown);
    assert(c.words[3] == 0 && c.words[4] == 0);
    consume(c);
    Ess::PreparedMove work; assert(Ess::nextMove(c, c.eligibleUs, work));
    assert(work.function == 6 && work.value == 5);
    const auto bytes = reply(c);
    auto e = frame(c, bytes, c.eligibleUs + 20); e.responseConfirmed = false;
    assert(Ess::advanceMove(c, e, c.eligibleUs + 40));
    consume(c, 1); assert(c.state == ActionState::ACTIVE && !c.runningObserved);
    consume(c, 4); consume(c, 1);
    assert(c.state == ActionState::SUCCEEDED && c.execution == ActionExecution::UNKNOWN);
    assert(!c.prepared.displacementKnown && !c.triggerEvidence.responseConfirmed);
}
void testTypedPositionProfile() {
    for (auto order : {Ess::WordOrder::HIGH_WORD_FIRST,Ess::WordOrder::LOW_WORD_FIRST}) {
        uint8_t frame[32]; std::memset(frame,0xA5,sizeof(frame));
        Ess::PositionProfile profile; profile.startSpeed=30; profile.accelerationTime=100;
        profile.decelerationTime=200; profile.speed=60; profile.targetBits=0xFEDC1234;
        assert(Ess::buildReadPositionProfile(1,frame,sizeof(frame))==8 && frame[3]==0x20 && frame[5]==6);
        assert(Ess::buildWritePositionProfile(1,profile,order,frame,sizeof(frame))==19);
        assert(frame[1]==16 && frame[3]==0x21 && frame[5]==5 && frame[6]==10);
        uint16_t target[2]; assert(Ess::encodeUint32(profile.targetBits,order,target,2));
        std::vector<uint8_t> bytes={1,3,12,0,30,0,100,0,200,0,60,
            uint8_t(target[0]>>8),uint8_t(target[0]),uint8_t(target[1]>>8),uint8_t(target[1])}; crc(bytes);
        Ess::PositionProfile parsed; assert(Ess::parsePositionProfile(bytes.data(),bytes.size(),1,order,parsed));
        assert(parsed.startSpeed==30 && parsed.accelerationTime==100 && parsed.decelerationTime==200 &&
            parsed.speed==60 && parsed.targetBits==profile.targetBits);
        const Saved<Ess::PositionProfile> saved(parsed);
        bytes.back()^=1; assert(!Ess::parsePositionProfile(bytes.data(),bytes.size(),1,order,parsed)); saved.check(parsed);
        bytes.back()^=1; assert(!Ess::parsePositionProfile(bytes.data(),bytes.size(),2,order,parsed)); saved.check(parsed);
        assert(!Ess::parsePositionProfile(bytes.data(),bytes.size(),1,static_cast<Ess::WordOrder>(2),parsed)); saved.check(parsed);
        for (unsigned fault=0;fault<5;++fault) {
            auto bad=profile; auto wordOrder=order; std::size_t capacity=sizeof(frame);
            if(fault==0) bad.accelerationTime=2001;
            if(fault==1) bad.decelerationTime=2001;
            if(fault==2) bad.speed=3001;
            if(fault==3) wordOrder=static_cast<Ess::WordOrder>(2);
            if(fault==4) capacity=18;
            uint8_t unchanged[32]; std::memcpy(unchanged,frame,sizeof(frame));
            assert(!Ess::buildWritePositionProfile(1,bad,wordOrder,frame,capacity));
            assert(!std::memcmp(unchanged,frame,sizeof(frame)));
        }
    }
}
void testNativeAbsoluteTargetsAndReadiness() {
    for (int64_t target : {int64_t(0), int64_t(1), int64_t(2147483647)}) {
        auto a=axis(); auto r=request(target); r.position.relative=false;
        Ess::MoveContext c;
        assert(Ess::prepareMoveAbsolute(c,a,nullptr,12,r,prerequisites(),100,10000));
        assert(c.prepared.endpointKnown && c.prepared.endpointNative==target && !c.prepared.displacementKnown);
    }
    auto c=move(); const Saved<Ess::MoveContext> saved(c);
    for (unsigned fault=0;fault<8;++fault) {
        auto a=axis(); auto r=request(0); r.position.relative=false; auto p=prerequisites();
        switch(fault) {
        case 0:r.position.value.denominator=0;break;
        case 1:p.readinessQualified=false;break;
        case 2:p.commandUnitsVerified=false;break;
        case 3:p.configuredRampVerified=false;break;
        case 4:p.maximumAgeUs=1;break;
        case 5:++r.position.configurationGeneration;break;
        case 6:a.nativeMinimum=1;break;
        case 7:p.serialInputsPermit=false;break;
        }
        assert(!Ess::prepareMoveAbsolute(c,a,nullptr,12,r,p,100,10000)); saved.check(c);
    }
    auto a=axis(); a.softLimitsKnown=true; a.softMinimum=0; a.softMaximum=100;
    auto r=request(101); r.position.relative=false;
    assert(!Ess::prepareMoveAbsolute(c,a,nullptr,12,r,prerequisites(),100,10000)); saved.check(c);
}
void testAbsoluteReferenceAndZeroGatesPreserveOutput() {
    auto a = axis(); a.originKnown = true; a.originSource = ScaleSource::QUALIFIED;
    AxisReference ref; ref.target = a.target; ref.configurationGeneration = a.generation;
    ref.nativeKnown = ref.stationary = true; ref.source = ScaleSource::QUALIFIED;
    ref.observedUs = 80; ref.maximumAgeUs = 1000;
    auto r = request(); r.position.relative = false; auto c = move(); const Saved<Ess::MoveContext> saved(c);
    for (unsigned fault = 0; fault < 11; ++fault) {
        auto bad = ref; auto config = a; auto req = r;
        switch (fault) {
        case 0: bad.nativeKnown = false; break;
        case 1: bad.stationary = false; break;
        case 2: ++bad.target.id; break;
        case 3: ++bad.target.generation; break;
        case 4: ++bad.configurationGeneration; break;
        case 5: bad.observedUs = 101; break;
        case 6: bad.maximumAgeUs = 1; break;
        case 7: bad.basis = RelativeBasis::COMMANDED; break;
        case 8: config.originKnown = false; req.position.frame = CoordinateFrame::MOTOR; break;
        case 9: req.position.value = Rational(0); break;
        case 10: req.position.value = Rational(1, 2); req.position.rounding = Rounding::NEAREST;
            req.position.maximumQuantizationError = 1; break; // Ties-to-even gives zero displacement.
        }
        assert(!Ess::prepareMoveAbsolute(c, config, &bad, 12, req, prerequisites(), 100, 10000, options())); saved.check(c);
    }
    assert(Ess::prepareMoveAbsolute(c, a, nullptr, 12, r, prerequisites(), 100, 10000));
    assert(!c.prepared.displacementKnown);
}
} // namespace
static void testOptionalAgePolicy() {
    const uint64_t now = 100000, deadline = 110000;
    auto p = prerequisites(); p.maximumAgeUs = 0;
    Ess::MoveContext c;
    assert(Ess::prepareMoveRelative(c, axis(), nullptr, 12, request(), p, now, deadline, options()));
    Ess::PreparedMove work;
    assert(Ess::nextMove(c, now, work));
    assert(work.deadlineUs == deadline);
    const Saved<Ess::MoveContext> saved(c);
    p.maximumAgeUs = 1000; assert(!(Ess::prepareMoveRelative(c, axis(), nullptr, 12, request(), p, now, deadline, options()))); saved.check(c);
    p.maximumAgeUs = 0; p.target.generation++;
    assert(!(Ess::prepareMoveRelative(c, axis(), nullptr, 12, request(), p, now, deadline, options()))); saved.check(c);
    p = prerequisites(); p.maximumAgeUs = 0; p.readinessQualified = false;
    assert(!(Ess::prepareMoveRelative(c, axis(), nullptr, 12, request(), p, now, deadline, options()))); saved.check(c);
    p.readinessQualified = true; p.observedUs = now + 1;
    assert(!(Ess::prepareMoveRelative(c, axis(), nullptr, 12, request(), p, now, deadline, options()))); saved.check(c);
    assert(Ess::advanceMove(c, local(c, ReadEventKind::DEADLINE), deadline));
    assert(c.state == ActionState::FAILED && c.outcome == ActionOutcome::DEADLINE);
    assert(Ess::nextMove(c, deadline, work) && work.kind == Ess::ActionWork::DONE && !work.length);
}
static void testShortMoveEndpointEvidence() {
    auto prepare = [](Ess::WordOrder order = Ess::WordOrder::HIGH_WORD_FIRST, int32_t baseline = 100) {
        auto p = prerequisites(); p.positionFeedbackMatchesCommand = true; p.wordOrder = order;
        AxisReference ref; ref.target = axis().target; ref.configurationGeneration = axis().generation;
        ref.nativeKnown = ref.stationary = true; ref.nativePosition = baseline;
        ref.basis = RelativeBasis::ACTUAL; ref.source = ScaleSource::READBACK;
        ref.observedUs = 80; ref.nowUs = 100; ref.maximumAgeUs = 1000;
        Ess::MoveContext c; auto o = options(); o.maxPolls = 10;
        assert(Ess::prepareMoveRelative(c, axis(), &ref, 12, request(), p, 100, 10000, o));
        trigger(c); return c;
    };
    auto observe = [](Ess::MoveContext& c, int32_t position, uint16_t flags = 1, uint16_t speed = 0, bool corrupt = false) {
        Ess::PreparedMove work; assert(Ess::nextMove(c, c.eligibleUs, work));
        assert(work.reg == 6 && work.count == 7 && !work.write);
        uint16_t pair[2]; assert(Ess::encodeInt32(position, c.prerequisites.wordOrder, pair, 2));
        std::vector<uint8_t> b = {1, 3, 14, 0, 0, uint8_t(flags >> 8), uint8_t(flags), 0, 0, 0, 0,
            uint8_t(pair[0] >> 8), uint8_t(pair[0]), uint8_t(pair[1] >> 8), uint8_t(pair[1]), uint8_t(speed >> 8), uint8_t(speed)};
        crc(b); if (corrupt) b.back() ^= 1;
        const auto at = c.eligibleUs + 20;
        assert(Ess::advanceMove(c, frame(c, b, at), at + 20));
    };
    for (auto order : {Ess::WordOrder::HIGH_WORD_FIRST, Ess::WordOrder::LOW_WORD_FIRST}) {
        auto c = prepare(order);
        observe(c, 100); assert(c.state == ActionState::ACTIVE && !c.positionMatchEvidence.length); // Old arrival.
        observe(c, 110); assert(c.state == ActionState::ACTIVE); // Arbitrary movement is insufficient.
        observe(c, 125); assert(c.state == ActionState::ACTIVE && c.positionMatchEvidence.length == 19);
        observe(c, 125, 1, 1); assert(!c.positionMatchEvidence.length); // Nonzero speed resets the witness.
        observe(c, 125); assert(c.state == ActionState::ACTIVE);
        observe(c, 125); assert(c.positionConfirmed && !c.runningObserved && !c.uncertain);
        assert(c.state == ActionState::SUCCEEDED && c.lastObservation.length == 19);
        assert(c.positionMatchEvidence.step + 1 == c.lastObservation.step); noMoreWork(c);
    }
    auto negative = prepare(Ess::WordOrder::LOW_WORD_FIRST, -100);
    observe(negative, -75); observe(negative, -75); assert(negative.positionConfirmed);
    auto wrong = prepare(); observe(wrong, 125); observe(wrong, 125, 1, 0, true);
    assert(wrong.state == ActionState::FAILED && !wrong.positionConfirmed && wrong.status.code == Err::CRC_ERROR);
    auto alarm = prepare(); observe(alarm, 125); observe(alarm, 125, 9);
    assert(alarm.state == ActionState::FAILED && !alarm.positionConfirmed);
    auto running = prepare(); observe(running, 125, 4); observe(running, 125, 4);
    assert(running.state == ActionState::ACTIVE && !running.positionConfirmed); // Cannot mask the high-speed discrepancy.
    auto unknown = prepare(); unknown.execution = ActionExecution::UNKNOWN;
    observe(unknown, 125); observe(unknown, 125); assert(unknown.state == ActionState::ACTIVE && !unknown.positionConfirmed);
    auto cancelled = prepare(); observe(cancelled, 125);
    assert(Ess::advanceMove(cancelled, local(cancelled, ReadEventKind::CANCEL), cancelled.servicedUs + 1));
    assert(cancelled.state == ActionState::FAILED && !cancelled.positionConfirmed);
    auto p = prerequisites(); p.positionFeedbackMatchesCommand = true;
    Ess::MoveContext untouched; const Saved<Ess::MoveContext> saved(untouched);
    assert(!Ess::prepareMoveRelative(untouched, axis(), nullptr, 12, request(), p, 100, 10000)); saved.check(untouched);
}
void testOperatingLimitsCommonParity() {
    auto p=prerequisites(); p.subdivision=51200;
    auto r=request(); r.speedRpm=234;
    Ess::MoveContext c;
    assert(Ess::prepareMoveRelative(c,axis(),nullptr,1,r,p,100,10000));
    const Saved<Ess::MoveContext> before(c);
    r.speedRpm=235;
    auto status=MotorControlRS::prepareMoveRelative(c,axis(),nullptr,1,r,p,100,10000);
    assert(!status && status.detail==static_cast<int>(MoveError::OPERATING_LIMIT)); before.check(c);
    p.subdivision=4000; r.speedRpm=2001;
    assert(!Ess::prepareMoveRelative(c,axis(),nullptr,1,r,p,100,10000)); before.check(c);
    r.speedRpm=2000; p.decelerationTime=99;
    assert(!Ess::prepareMoveRelative(c,axis(),nullptr,1,r,p,100,10000)); before.check(c);
    p.decelerationTime=100; p.subdivision=0;
    assert(!Ess::prepareMoveRelative(c,axis(),nullptr,1,r,p,100,10000)); before.check(c);
    p.subdivision=4000;
    assert(Ess::prepareMoveRelative(c,axis(),nullptr,1,r,p,100,10000));
    // Native displacement still needs no host origin, gear or travel scale.
    assert(!axis().originKnown && !axis().units.commandStepsPerMotorTurn.numerator);
}
int main() {
    testOperatingLimitsCommonParity();
    testShortMoveEndpointEvidence();
    testOptionalAgePolicy();
    testTypedPositionProfile(); testNativeAbsoluteDoesNotInventDisplacement(); testNativeAbsoluteTargetsAndReadiness();
    testTriggerObservationKeepsUnknownExecution(); testTriggerPreservesFailures();
    testExactStageTriggerAndCommonParity(); testAllPreparationGatesLeaveOutputUnchanged();
    testSignWordOrderAndRange(); testAdmittedPrerequisitesAreCopied(); testCancelFailureAndDeadlineAtEveryBoundary();
    testBadRepliesAndNoAutomaticReplay(); testEnvelopeCorrelationAndCopiedEvidence();
    testFreshCompletionLimitsAndDelayedService(); testDriveFaultAndPreviousObservationRetention();
    testWriteDeadlinesRetainReadinessAndOperationBudgets();
    testConsumedReferenceUsesOperationTimeAndCoversWrites();
    testAbsoluteAndAngleReuseSequenceAndRetainReference(); testAbsoluteReferenceAndZeroGatesPreserveOutput();
}
