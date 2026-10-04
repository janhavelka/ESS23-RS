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
    p.observedUs = 80; p.maximumAgeUs = 1000; p.rawMotion = 1; return p;
}
ActionOptions options() { ActionOptions o; o.pollIntervalUs = 100; o.maxPolls = 4; return o; }
Ess::MoveContext move(uint64_t deadline = 10000) {
    Ess::MoveContext c;
    assert(Ess::prepareMoveRelative(c, axis(), nullptr, 12, request(), prerequisites(), 100, deadline, options()));
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
    else if (c.step == 1) b = {1, 6, 0, 0x27, 0, 1};
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
void testAllPreparationGatesLeaveOutputUnchanged() {
    auto c = move();
    for (unsigned fault = 0; fault < 28; ++fault) {
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
} // namespace
int main() {
    testExactStageTriggerAndCommonParity(); testAllPreparationGatesLeaveOutputUnchanged();
    testSignWordOrderAndRange(); testAdmittedPrerequisitesAreCopied(); testCancelFailureAndDeadlineAtEveryBoundary();
    testBadRepliesAndNoAutomaticReplay(); testEnvelopeCorrelationAndCopiedEvidence();
    testFreshCompletionLimitsAndDelayedService(); testDriveFaultAndPreviousObservationRetention();
}
