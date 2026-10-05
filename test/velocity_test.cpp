// SPDX-License-Identifier: MIT
#include <MotorControlRS/profiles/ess_rs/Velocity.h>
#include <cassert>
#include <cstring>
#include <cmath>
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
    uint16_t value = 0xffff;
    for (uint8_t byte : b) {
        value ^= byte;
        for (unsigned bit = 0; bit < 8; ++bit) value = value & 1 ? (value >> 1) ^ 0xa001 : value >> 1;
    }
    b.push_back(static_cast<uint8_t>(value)); b.push_back(static_cast<uint8_t>(value >> 8));
}
AxisConfig axis() {
    AxisConfig a; a.target.id = 3; a.target.address = 1; a.target.generation = 4; a.generation = 7; return a;
}
VelocityRequest request(int64_t rpm = 60) {
    VelocityRequest r; r.value = Rational(rpm); r.configurationGeneration = 7;
    r.ramp = VelocityRamp::VERIFIED_CONFIGURED; r.durationUs = 1000;
    r.stop.behavior = StopBehavior::CONFIGURED_DECELERATION; return r;
}
Ess::VelocityPrerequisites prerequisites() {
    Ess::VelocityPrerequisites p; p.target = axis().target; p.configurationGeneration = 7;
    p.nativeRpmVerified = p.configuredRampVerified = p.serialInputsPermit = p.readinessQualified = true;
    p.accelerationTime = p.decelerationTime = 100;
    p.observedUs = 80; p.maximumAgeUs = 2000; p.rawMotion = 1; return p;
}
ActionOptions options() { ActionOptions o; o.pollIntervalUs = 100; o.maxPolls = 8; return o; }
Ess::VelocityContext velocity() {
    Ess::VelocityContext c;
    assert(Ess::prepareVelocity(c, axis(), 12, request(), prerequisites(), 100, 10000, options())); return c;
}
ActionEvent local(const Ess::VelocityContext& c, ReadEventKind kind) {
    ActionEvent e; e.transport.target = c.target; e.transport.operationId = c.operationId;
    e.transport.step = c.step; e.transport.kind = kind; return e;
}
std::vector<uint8_t> reply(const Ess::VelocityContext& c, uint16_t flags = 4, uint16_t alarm = 0) {
    std::vector<uint8_t> b;
    if (c.phase == Ess::VelocityPhase::STAGING) b = {1, 16, 0, 0x1d, 0, 3};
    else if (c.phase == Ess::VelocityPhase::TRIGGER) b = {1, 6, 0, 0x27, 0, 2};
    else if (c.phase == Ess::VelocityPhase::STOPPING && c.stop.step == 0)
        b = {1, 6, 0, 0x27, static_cast<uint8_t>(c.stop.value >> 8), static_cast<uint8_t>(c.stop.value)};
    else b = {1, 3, 4, static_cast<uint8_t>(alarm >> 8), static_cast<uint8_t>(alarm),
        static_cast<uint8_t>(flags >> 8), static_cast<uint8_t>(flags)};
    crc(b); return b;
}
ActionEvent frame(const Ess::VelocityContext& c, const std::vector<uint8_t>& bytes, uint64_t at) {
    auto e = local(c, ReadEventKind::FRAME); e.transport.frame = bytes.data(); e.transport.length = bytes.size();
    e.transport.txAccepted = c.phase == Ess::VelocityPhase::STAGING ? 15 : 8;
    e.transport.qualified = e.responseConfirmed = e.txComplete = true;
    e.transport.earliestUs = at; e.transport.latestUs = at + 10; return e;
}
void consume(Ess::VelocityContext& c, uint16_t flags = 4, uint16_t alarm = 0) {
    const auto bytes = reply(c, flags, alarm); const uint64_t at = c.eligibleUs + 20;
    assert(Ess::advanceVelocity(c, frame(c, bytes, at), at + 20));
}
void trigger(Ess::VelocityContext& c) { consume(c); consume(c); assert(c.phase == Ess::VelocityPhase::OBSERVING); }
void noWork(const Ess::VelocityContext& c) {
    Ess::PreparedVelocity p; assert(Ess::nextVelocity(c, c.servicedUs, p));
    assert(p.kind == Ess::ActionWork::DONE && p.length == 0);
}
void testGoldenFramesCommonParityAndFiniteStop() {
    auto c = velocity(); Ess::PreparedVelocity p;
    assert(Ess::nextVelocity(c, 100, p));
    const uint8_t golden[] = {1,16,0,0x1d,0,3,6,0,60,0,100,0,100,0x66,0xde};
    assert(p.length == sizeof(golden) && p.function == 16 && p.reg == 0x1d && p.count == 3 && p.write && !p.urgent);
    assert(std::memcmp(p.bytes, golden, sizeof(golden)) == 0);
    Ess::VelocityContext common;
    assert(MotorControlRS::prepareVelocity(common, axis(), 12, request(), prerequisites(), 100, 10000, options()));
    Ess::PreparedVelocity repeated; assert(Ess::nextVelocity(common, 100, repeated));
    assert(repeated.length == p.length && std::memcmp(p.bytes, repeated.bytes, p.length) == 0);
    consume(c); assert(c.stagingApplied && !c.needsStop);
    assert(Ess::nextVelocity(c, c.servicedUs, p) && p.value == 2 && p.reg == 0x27 && p.length == 8);
    consume(c); assert(c.execution == ActionExecution::ACKNOWLEDGED && c.needsStop);
    assert(Ess::nextVelocity(c, c.servicedUs, p) && p.kind == Ess::ActionWork::WAIT && !p.length);
    consume(c, 1); assert(!c.runningObserved && c.state == ActionState::ACTIVE); // Old arrival is not activity.
    consume(c, 0x8004); assert(c.runningObserved && c.rawMotion == 0x8004);
    const auto activity = c.activityEvidence;
    assert(Ess::serviceVelocity(c, c.stopDueUs));
    assert(c.phase == Ess::VelocityPhase::STOPPING && !c.serviceMissed);
    assert(Ess::nextVelocity(c, c.servicedUs, p) && p.urgent && p.value == 0x100 && p.count == 1 && p.deadlineUs == 10000);
    consume(c); assert(c.stop.execution == ActionExecution::ACKNOWLEDGED && c.needsStop);
    assert(Ess::nextVelocity(c, c.eligibleUs, p) && !p.write && !p.urgent && p.reg == 6 && p.count == 2);
    consume(c, 1); assert(c.state == ActionState::SUCCEEDED && !c.needsStop);
    assert(c.completion == ActionCompletion::OBSERVED && c.stop.completion == ActionCompletion::OBSERVED);
    assert(!c.uncertain && c.activityEvidence.latestUs == activity.latestUs && c.stopDueUs == 1100 && c.deadlineUs == 10000);
    noWork(c);
}
void testRejectedParametersUnchanged() {
    auto output = velocity();
    for (unsigned bad = 0; bad < 29; ++bad) {
        auto a = axis(); auto r = request(); auto p = prerequisites(); auto o = options();
        uint64_t now = 100, deadline = 10000; uint32_t id = 12;
        switch (bad) {
        case 0: a.target.address = 248; break;
        case 1: id = 0; break;
        case 2: deadline = now; break;
        case 3: r.durationUs = 0; break;
        case 4: r.durationUs = 9900; break;
        case 5: o.maxPolls = 65; break;
        case 6: o.pollIntervalUs = 0; break;
        case 7: ++p.target.generation; break;
        case 8: ++p.target.id; break;
        case 9: ++p.target.address; break;
        case 10: ++p.configurationGeneration; break;
        case 11: ++r.configurationGeneration; break;
        case 12: r.ramp = VelocityRamp::UNSPECIFIED; break;
        case 13: p.configuredRampVerified = false; break;
        case 14: p.accelerationTime = 2001; break;
        case 15: p.decelerationTime = 2001; break;
        case 16: p.nativeRpmVerified = false; break;
        case 17: p.serialInputsPermit = false; break;
        case 18: p.readinessQualified = false; break;
        case 19: p.observedUs = 101; break;
        case 20: p.maximumAgeUs = 20; break;
        case 21: p.rawAlarm = 1; break;
        case 22: p.rawMotion = 4; break;
        case 23: p.minimumRpm = -3001; break;
        case 24: p.maximumRpm = 3001; break;
        case 25: r.value = Rational(0); break;
        case 26: r.stop.behavior = StopBehavior::UNSPECIFIED; break;
        case 27: r.stop.deviceQueue = DeviceQueue::DISCARD; break;
        case 28: r.jerk = std::numeric_limits<double>::quiet_NaN(); break;
        }
        const Saved<Ess::VelocityContext> saved(output);
        assert(!Ess::prepareVelocity(output, a, id, r, p, now, deadline, o)); saved.check(output);
    }
    for (uint16_t flags : {8, 16, 32, 64}) {
        auto p = prerequisites(); p.rawMotion = flags;
        const Saved<Ess::VelocityContext> saved(output);
        assert(!Ess::prepareVelocity(output, axis(), 12, request(), p, 100, 10000)); saved.check(output);
    }
    for (unsigned policy = 0; policy < 8; ++policy) {
        auto r = request();
        if (policy < 4) {
            r.ramp = VelocityRamp::ACCELERATION; r.acceleration = r.deceleration = 1;
            r.accelerationUnit = r.decelerationUnit = policy == 0 ? AccelerationUnit(PositionUnit::STEPS) :
                policy == 1 ? AccelerationUnit(PositionUnit::DEGREES) :
                policy == 2 ? AccelerationUnit(PositionUnit::RADIANS) :
                AccelerationUnit(PositionUnit::TURNS, TimeUnit::MINUTE, TimeUnit::SECOND);
        } else if (policy == 4) r.jerk = 1;
        else if (policy == 5) r.blending = true;
        else if (policy == 6) r.liveUpdate = true;
        else r.acceleration = 1;
        const Saved<Ess::VelocityContext> saved(output);
        const auto status = Ess::prepareVelocity(output, axis(), 12, r, prerequisites(), 100, 10000);
        assert(!status && status.code == Err::UNSUPPORTED); saved.check(output);
    }
}
void testSignRangesAndLatchedSettings() {
    for (int64_t rpm : {-3000,-1,1,3000}) {
        auto p = prerequisites(); p.negativeTwosComplementVerified = true;
        auto r = request(rpm); Ess::VelocityContext c;
        assert(Ess::prepareVelocity(c, axis(), 12, r, p, 100, 10000));
        assert(c.prepared.nativeRpm == rpm && c.words[0] == static_cast<uint16_t>(rpm));
        p.accelerationTime = p.decelerationTime = 1999; p.target.address = 2;
        r.value = Rational(0); r.stop.behavior = StopBehavior::DIRECT;
        assert(c.words[1] == 100 && c.words[2] == 100 && c.prerequisites.target.address == 1);
        trigger(c); assert(Ess::serviceVelocity(c, c.stopDueUs));
        assert(c.stop.request.stop.behavior == StopBehavior::CONFIGURED_DECELERATION && c.request.value.numerator == rpm);
    }
    for (int64_t rpm : {int64_t(-3001),int64_t(3001),std::numeric_limits<int64_t>::min(),std::numeric_limits<int64_t>::max()}) {
        Ess::VelocityContext c; auto p = prerequisites(); p.negativeTwosComplementVerified = true;
        assert(!Ess::prepareVelocity(c, axis(), 12, request(rpm), p, 100, 10000));
    }
    Ess::VelocityContext c;
    auto status = Ess::prepareVelocity(c, axis(), 12, request(-1), prerequisites(), 100, 10000);
    assert(!status && status.detail == static_cast<int32_t>(VelocityError::UNRESOLVED_SIGN));
    status = Ess::prepareVelocity(c, axis(), 12, request(0), prerequisites(), 100, 10000);
    assert(!status && status.detail == static_cast<int32_t>(VelocityError::ZERO_SPEED));
    auto r = request(); r.value = Rational(12001, 4); r.rounding = Rounding::TOWARD_ZERO;
    r.maximumQuantizationErrorRpm = 1;
    assert(!Ess::prepareVelocity(c, axis(), 12, r, prerequisites(), 100, 10000)); // Requested above limit despite effective3000.
}
void testFailuresAndLocalCancellation() {
    for (unsigned phase = 0; phase < 4; ++phase) {
        auto c = velocity();
        if (phase >= 1) consume(c);
        if (phase >= 2) consume(c);
        if (phase >= 3) assert(Ess::serviceVelocity(c, c.stopDueUs));
        auto cancel = local(c, ReadEventKind::CANCEL);
        assert(Ess::advanceVelocity(c, cancel, c.servicedUs));
        assert(c.state == ActionState::FAILED && c.outcome == ActionOutcome::CANCELLED);
        assert(c.needsStop == (phase >= 2)); noWork(c);
        const Saved<Ess::VelocityContext> saved(c);
        assert(!Ess::advanceVelocity(c, cancel, c.servicedUs)); saved.check(c);
    }
    for (unsigned failPhase = 0; failPhase < 3; ++failPhase) {
        auto c = velocity(); if (failPhase >= 1) consume(c); if (failPhase >= 2) consume(c);
        auto failure = local(c, ReadEventKind::TRANSPORT_FAILURE);
        failure.transport.txAccepted = c.phase == Ess::VelocityPhase::STAGING ? 3 : 8;
        failure.transport.executionUnknown = true;
        assert(Ess::advanceVelocity(c, failure, c.servicedUs + 1));
        assert(c.uncertain && c.outcome == ActionOutcome::TRANSPORT_ERROR);
        if (failPhase == 0) { assert(c.state == ActionState::FAILED && !c.needsStop); noWork(c); }
        else {
            assert(c.phase == Ess::VelocityPhase::STOPPING && c.needsStop && c.state == ActionState::ACTIVE);
            const auto originalStatus = c.status; const auto originalFailure = c.failureEvidence;
            consume(c); consume(c, 1);
            assert(c.state == ActionState::FAILED && !c.needsStop && c.completion == ActionCompletion::OBSERVED);
            assert(c.status.detail == originalStatus.detail && c.failureEvidence.step == originalFailure.step);
            assert(c.execution == (failPhase == 1 ? ActionExecution::UNKNOWN : ActionExecution::ACKNOWLEDGED)); noWork(c);
        }
    }
    auto c = velocity(); trigger(c);
    assert(Ess::serviceVelocity(c, c.stopDueUs));
    auto failure = local(c, ReadEventKind::TRANSPORT_FAILURE); failure.transport.txAccepted = 8;
    failure.transport.executionUnknown = true;
    assert(Ess::advanceVelocity(c, failure, c.servicedUs + 1));
    assert(c.state == ActionState::FAILED && c.stop.execution == ActionExecution::UNKNOWN && c.needsStop); noWork(c);
}
void testCheckedRepliesAndEnvelopePreservation() {
    for (unsigned phase = 0; phase < 3; ++phase) {
        for (unsigned bad = 0; bad < 5; ++bad) {
            auto c = velocity(); if (phase > 0) consume(c); if (phase > 1) consume(c);
            auto b = reply(c); const uint64_t at = c.eligibleUs + 20;
            auto e = frame(c, b, at);
            if (bad == 0) b.back() ^= 1;
            else if (bad == 1) { b[0] = 2; b.resize(b.size()-2); crc(b); }
            else if (bad == 2) { b[phase == 2 ? 2 : 5] ^= 1; b.resize(b.size()-2); crc(b); }
            else if (bad == 3) e.responseConfirmed = false;
            else { e.transport.qualified = false; e.transport.earliestUs = e.transport.latestUs = 0; }
            e.transport.frame = b.data(); e.transport.length = b.size();
            assert(Ess::advanceVelocity(c, e, at + 20));
            assert(c.state == ActionState::FAILED || c.phase == Ess::VelocityPhase::STOPPING);
            assert(c.completion == ActionCompletion::NOT_OBSERVED);
        }
    }
    auto c = velocity(); auto b = reply(c); auto e = frame(c, b, 120);
    for (unsigned bad = 0; bad < 7; ++bad) {
        auto malformed = e;
        if (bad == 0) ++malformed.transport.target.generation;
        if (bad == 1) ++malformed.transport.operationId;
        if (bad == 2) ++malformed.transport.step;
        if (bad == 3) malformed.transport.txAccepted = 16;
        if (bad == 4) malformed.txComplete = false;
        if (bad == 5) malformed.transport.latestUs = 141;
        if (bad == 6) malformed.transport.frame = nullptr;
        const Saved<Ess::VelocityContext> saved(c);
        assert(!Ess::advanceVelocity(c, malformed, 140)); saved.check(c);
    }
    assert(Ess::advanceVelocity(c, e, 140)); b[0] = 99;
    assert(c.stagingEvidence.raw[0] == 1); // transient frame copied
    // A documented checked trigger exception rejects start and does not request stop.
    b = {1,0x86,2}; crc(b); e = frame(c, b, 160);
    assert(Ess::advanceVelocity(c, e, 180));
    assert(c.execution == ActionExecution::REJECTED && !c.needsStop && c.uncertain); noWork(c);
}
void testDeadlinesActivityAndDelayedService() {
    auto c = velocity(); trigger(c); consume(c, 1);
    assert(Ess::serviceVelocity(c, c.stopDueUs)); consume(c); consume(c, 1);
    assert(c.state == ActionState::FAILED && !c.needsStop && c.completion == ActionCompletion::OBSERVED);
    assert(c.status.detail == static_cast<int32_t>(VelocityError::ACTIVITY_NOT_OBSERVED));
    c = velocity(); trigger(c); consume(c);
    assert(Ess::serviceVelocity(c, c.stopDueUs + 101));
    assert(c.serviceMissed && c.status.detail == static_cast<int32_t>(VelocityError::SERVICE_MISSED));
    consume(c); consume(c, 1); assert(c.state == ActionState::FAILED && !c.needsStop && c.stopDueUs == 1100);
    c = velocity(); trigger(c);
    assert(Ess::serviceVelocity(c, c.deadlineUs));
    assert(c.state == ActionState::FAILED && c.needsStop && c.execution == ActionExecution::ACKNOWLEDGED); noWork(c);
    for (unsigned stopAcknowledged = 0; stopAcknowledged < 2; ++stopAcknowledged) {
        c = velocity(); trigger(c); assert(Ess::serviceVelocity(c, c.stopDueUs));
        if (stopAcknowledged) consume(c);
        assert(Ess::serviceVelocity(c, c.deadlineUs));
        assert(c.state == ActionState::FAILED && c.needsStop && c.stop.state == ActionState::FAILED);
        assert(c.stop.outcome == ActionOutcome::DEADLINE && c.stop.completion == ActionCompletion::NOT_OBSERVED);
        assert(c.stop.execution == (stopAcknowledged ? ActionExecution::ACKNOWLEDGED : ActionExecution::NOT_TRANSMITTED));
    }
    // Captured on-time trigger ACK delivered after duration gets stop immediately,
    // never a fresh duration or new trigger.
    c = velocity(); consume(c); auto b = reply(c); auto e = frame(c, b, 160);
    assert(Ess::advanceVelocity(c, e, 1200));
    assert(c.phase == Ess::VelocityPhase::STOPPING && c.stopDueUs == 1100 && c.needsStop);
    c = velocity(); trigger(c); b = reply(c); e = frame(c, b, c.eligibleUs + 20);
    assert(Ess::advanceVelocity(c, e, 1400));
    assert(c.runningObserved && c.serviceMissed && c.phase == Ess::VelocityPhase::STOPPING);
    assert(c.status.detail == static_cast<int32_t>(VelocityError::SERVICE_MISSED));
    c = velocity(); trigger(c); auto elapsed = local(c, ReadEventKind::DEADLINE);
    assert(Ess::advanceVelocity(c, elapsed, 1400));
    assert(c.serviceMissed && c.phase == Ess::VelocityPhase::STOPPING);
    // Last framing gap beyond the step deadline cannot establish on-time start.
    c = velocity(); consume(c); b = reply(c); e = frame(c, b, c.stopDueUs - 5);
    assert(Ess::advanceVelocity(c, e, c.stopDueUs + 10));
    assert(c.outcome == ActionOutcome::DEADLINE && c.phase == Ess::VelocityPhase::STOPPING && c.uncertain);
    // Before TX, readiness expiry produces no staging or trigger.
    auto p = prerequisites(); p.maximumAgeUs = 100; c = Ess::VelocityContext();
    assert(Ess::prepareVelocity(c, axis(), 12, request(), p, 100, 10000));
    Ess::PreparedVelocity work; assert(Ess::nextVelocity(c, 100, work) && work.deadlineUs == 180);
    assert(Ess::serviceVelocity(c, 180));
    assert(c.state == ActionState::FAILED && c.execution == ActionExecution::NOT_TRANSMITTED && !c.needsStop); noWork(c);
    // A failed new refresh leaves the last raw report immutable while stop runs.
    c = velocity(); trigger(c); consume(c, 0x8004); const auto previous = c.lastObservation;
    b = reply(c, 1); b.back() ^= 1; e = frame(c, b, c.eligibleUs + 20);
    assert(Ess::advanceVelocity(c, e, c.eligibleUs + 40));
    assert(c.rawMotion == 0x8004 && c.lastObservation.latestUs == previous.latestUs && c.needsStop);
}
void testSharedNumericPreparation() {
    auto a = axis(); auto r = request(); PreparedVelocityTarget target;
    for (int64_t v : {int64_t(-32768),int64_t(-1),int64_t(1),int64_t(32767)}) {
        r.value = Rational(v); assert(prepareVelocityTarget(r, a, target)); assert(target.nativeRpm == v);
    }
    for (int64_t v : {int64_t(-32769),int64_t(32768)}) {
        r.value = Rational(v); const Saved<PreparedVelocityTarget> saved(target);
        assert(!prepareVelocityTarget(r, a, target)); saved.check(target);
    }
    r.value = Rational(std::numeric_limits<int64_t>::max(), static_cast<uint64_t>(std::numeric_limits<int64_t>::max()));
    assert(prepareVelocityTarget(r, a, target) && target.nativeRpm == 1 && target.exactArithmetic);
    r.value = Rational(65535, 2); r.rounding = Rounding::TOWARD_ZERO; r.maximumQuantizationErrorRpm = 1;
    assert(!prepareVelocityTarget(r, a, target)); // Fractional requested32767.5 must not round into range.
    for (Rounding rounding : {Rounding::NEAREST,Rounding::TOWARD_ZERO,Rounding::FLOOR,Rounding::CEIL}) {
        for (int sign : {-1,1}) {
            r = request(); r.value = Rational(sign * 3, 2); r.rounding = rounding; r.maximumQuantizationErrorRpm = 0.5;
            assert(prepareVelocityTarget(r, a, target));
            const int expected = rounding == Rounding::NEAREST ? sign*2 :
                rounding == Rounding::TOWARD_ZERO ? sign :
                rounding == Rounding::FLOOR ? (sign < 0 ? -2 : 1) : (sign < 0 ? -1 : 2);
            assert(target.nativeRpm == expected && std::fabs(target.roundingError) == 0.5);
        }
    }
    r.value = Rational(5,2); r.rounding = Rounding::NEAREST;
    assert(prepareVelocityTarget(r, a, target) && target.nativeRpm == 2); // Ties to even.
    r.rounding = Rounding::EXACT; assert(!prepareVelocityTarget(r, a, target));
    r = request(); r.frame = CoordinateFrame::MOTOR;
    assert(prepareVelocityTarget(r, a, target) && target.nativeRpm == 60); // No C/G/origin required for motor rpm.
    r.value = Rational(360); r.unit = VelocityUnit(PositionUnit::DEGREES,TimeUnit::SECOND);
    assert(prepareVelocityTarget(r, a, target) && target.nativeRpm == 60);
    r.value = Rational(200); r.unit = VelocityUnit(PositionUnit::FULL_STEPS,TimeUnit::SECOND);
    assert(!prepareVelocityTarget(r, a, target));
    a.units.fullStepsPerMotorTurn = UnitScale(200,1,ScaleSource::DOCUMENTED);
    assert(prepareVelocityTarget(r, a, target) && target.nativeRpm == 60); // C and G still unknown.
    r.value = Rational(1000); r.unit = VelocityUnit(PositionUnit::STEPS,TimeUnit::SECOND);
    assert(!prepareVelocityTarget(r, a, target));
    a.units.commandStepsPerMotorTurn = UnitScale(1000,1,ScaleSource::QUALIFIED);
    assert(prepareVelocityTarget(r, a, target) && target.nativeRpm == 60);
    a.units.commandPolarity = -1;
    assert(prepareVelocityTarget(r, a, target) && target.nativeRpm == -60);
    a.units.commandPolarity = 1; r = request(); r.frame = CoordinateFrame::LOAD;
    assert(!prepareVelocityTarget(r, a, target));
    a.units.motorTurnsPerLoadTurn = UnitScale(3,2,ScaleSource::QUALIFIED);
    assert(prepareVelocityTarget(r, a, target) && target.nativeRpm == 90);
    r.value = Rational(10); r.unit = VelocityUnit(PositionUnit::MILLIMETRES,TimeUnit::SECOND);
    assert(!prepareVelocityTarget(r, a, target));
    a.units.millimetresPerLoadTurn = UnitScale(10,1,ScaleSource::QUALIFIED);
    assert(prepareVelocityTarget(r, a, target) && target.nativeRpm == 90);
    r.frame = CoordinateFrame::MOTOR; assert(!prepareVelocityTarget(r, a, target));
    r.value = Rational(4000); r.unit = VelocityUnit(PositionUnit::ENCODER_COUNTS,TimeUnit::SECOND);
    a.units.encoder.countsPerUnit = UnitScale(4000,1,ScaleSource::QUALIFIED);
    a.units.encoder.sourceId = 11; a.units.encoder.basis = EncoderBasis::MOTOR_TURN;
    assert(prepareVelocityTarget(r, a, target) && target.nativeRpm == 60);
    a.units.encoder.sourceId = 0; assert(!prepareVelocityTarget(r, a, target)); a.units.encoder.sourceId = 11;
    a.units.encoder.basis = EncoderBasis::MILLIMETRE;
    a.units.encoder.countsPerUnit = UnitScale(1000,1,ScaleSource::QUALIFIED);
    r.value = Rational(10000);
    assert(prepareVelocityTarget(r, a, target) && target.nativeRpm == 90);
    a = axis(); r = request(); r.frame = CoordinateFrame::MOTOR;
    r.unit = VelocityUnit(PositionUnit::RADIANS,TimeUnit::SECOND); r.value = Rational(6283185,1000000);
    assert(!prepareVelocityTarget(r, a, target));
    r.approximate = true; r.maximumApproximationErrorRpm = 0.000001;
    r.rounding = Rounding::NEAREST; r.maximumQuantizationErrorRpm = 0.001;
    assert(prepareVelocityTarget(r, a, target) && target.nativeRpm == 60 && !target.exactArithmetic);
    assert(target.approximationErrorBound > 0 && target.approximationErrorBound <= r.maximumApproximationErrorRpm);
    r.value = Rational(0);
    const auto zero = prepareVelocityTarget(r, a, target);
    assert(!zero && zero.detail == static_cast<int32_t>(VelocityError::ZERO_SPEED));
    r = request(); r.maximumQuantizationErrorRpm = std::numeric_limits<double>::quiet_NaN();
    assert(!prepareVelocityTarget(r, a, target));
    r = request(); r.maximumQuantizationErrorRpm = std::numeric_limits<double>::infinity();
    assert(!prepareVelocityTarget(r, a, target));
    r = request(); r.maximumApproximationErrorRpm = std::numeric_limits<double>::quiet_NaN();
    assert(!prepareVelocityTarget(r, a, target));
    r = request(); r.maximumApproximationErrorRpm = std::numeric_limits<double>::infinity();
    assert(!prepareVelocityTarget(r, a, target));
    r = request(); r.maximumApproximationErrorRpm = -1;
    assert(!prepareVelocityTarget(r, a, target));
    r = request(); ++r.configurationGeneration; assert(!prepareVelocityTarget(r, a, target));
    // Existing acceleration conversions remain independent from unsupported ESS
    // acceleration-to-native-time mapping.
    UnitConfig units; units.commandStepsPerMotorTurn = UnitScale(1000,1,ScaleSource::QUALIFIED);
    units.motorTurnsPerLoadTurn = UnitScale(1,1,ScaleSource::QUALIFIED);
    UnitConversion converted;
    assert(convertAcceleration(1000,AccelerationUnit(PositionUnit::STEPS),AccelerationUnit(PositionUnit::TURNS),units,converted));
    assert(converted.value == 1);
    assert(convertAcceleration(360,AccelerationUnit(PositionUnit::DEGREES),AccelerationUnit(PositionUnit::TURNS),units,converted));
    assert(converted.value == 1);
    assert(convertAcceleration(6.283185307179586,AccelerationUnit(PositionUnit::RADIANS),AccelerationUnit(PositionUnit::TURNS),units,converted));
    assert(std::fabs(converted.value-1) <= converted.absoluteErrorBound);
    assert(convertAcceleration(60,AccelerationUnit(PositionUnit::TURNS,TimeUnit::MINUTE,TimeUnit::SECOND),
        AccelerationUnit(PositionUnit::TURNS),units,converted));
    assert(converted.value == 1);
}
void testBoundedStopAndPreservedPrimaryFailure() {
    auto c = velocity(); consume(c);
    auto failedStart = local(c, ReadEventKind::TRANSPORT_FAILURE);
    failedStart.transport.txAccepted = 8; failedStart.transport.executionUnknown = true;
    assert(Ess::advanceVelocity(c, failedStart, c.servicedUs + 1));
    const auto originalFailure = c.failureEvidence; const auto originalStatus = c.status;
    auto failedStop = local(c, ReadEventKind::TRANSPORT_FAILURE);
    failedStop.transport.txAccepted = 8; failedStop.transport.executionUnknown = true;
    assert(Ess::advanceVelocity(c, failedStop, c.servicedUs + 1));
    assert(c.state == ActionState::FAILED && c.needsStop && c.execution == ActionExecution::UNKNOWN);
    assert(c.stop.execution == ActionExecution::UNKNOWN && c.stop.outcome == ActionOutcome::TRANSPORT_ERROR);
    assert(c.status.detail == originalStatus.detail && c.failureEvidence.step == originalFailure.step);
    assert(c.triggerEvidence.step != c.stop.failureEvidence.step || c.triggerEvidence.deliveredUs != c.stop.failureEvidence.deliveredUs);
    // Both observation phases remain bounded and their global tokens do not wrap.
    auto r = request(); r.durationUs = 100000; auto o = options(); o.maxPolls = Ess::ACTION_MAX_POLLS;
    auto p = prerequisites(); p.maximumAgeUs = 200000;
    assert(Ess::prepareVelocity(c, axis(), 12, r, p, 100, 300000, o)); trigger(c);
    for (unsigned i = 0; i < Ess::ACTION_MAX_POLLS; ++i) consume(c, 4);
    assert(c.phase == Ess::VelocityPhase::STOPPING && c.polls == Ess::ACTION_MAX_POLLS);
    assert(c.outcome == ActionOutcome::OBSERVATION_LIMIT && c.needsStop);
    consume(c); // Stop echo alone cannot settle velocity.
    for (unsigned i = 0; i < Ess::ACTION_MAX_POLLS; ++i) consume(c, 4);
    assert(c.state == ActionState::FAILED && c.needsStop && c.step < 255);
    assert(c.stop.polls == Ess::ACTION_MAX_POLLS && c.stop.outcome == ActionOutcome::OBSERVATION_LIMIT);
    noWork(c);
    // Cancellation after a stop ACK still cannot establish non-running.
    c = velocity(); trigger(c); assert(Ess::serviceVelocity(c, c.stopDueUs)); consume(c);
    const auto cancelled = local(c, ReadEventKind::CANCEL);
    assert(Ess::advanceVelocity(c, cancelled, c.servicedUs));
    assert(c.needsStop && c.stop.execution == ActionExecution::ACKNOWLEDGED && c.stop.completion == ActionCompletion::NOT_OBSERVED);
}
} // namespace
static void testOptionalAgePolicy() {
    const uint64_t now = 100000, deadline = 110000;
    auto p = prerequisites(); p.maximumAgeUs = 0;
    Ess::VelocityContext c;
    assert(Ess::prepareVelocity(c, axis(), 12, request(), p, now, deadline, options()));
    Ess::PreparedVelocity work;
    assert(Ess::nextVelocity(c, now, work));
    assert(work.deadlineUs == c.stopDueUs);
    const Saved<Ess::VelocityContext> saved(c);
    p.maximumAgeUs = 1000; assert(!(Ess::prepareVelocity(c, axis(), 12, request(), p, now, deadline, options()))); saved.check(c);
    p.maximumAgeUs = 0; p.target.generation++;
    assert(!(Ess::prepareVelocity(c, axis(), 12, request(), p, now, deadline, options()))); saved.check(c);
    p = prerequisites(); p.maximumAgeUs = 0; p.readinessQualified = false;
    assert(!(Ess::prepareVelocity(c, axis(), 12, request(), p, now, deadline, options()))); saved.check(c);
    p.readinessQualified = true; p.observedUs = now + 1;
    assert(!(Ess::prepareVelocity(c, axis(), 12, request(), p, now, deadline, options()))); saved.check(c);
    assert(Ess::advanceVelocity(c, local(c, ReadEventKind::DEADLINE), deadline));
    assert(c.state == ActionState::FAILED && c.outcome == ActionOutcome::DEADLINE);
    assert(Ess::nextVelocity(c, deadline, work) && work.kind == Ess::ActionWork::DONE && !work.length);
    // The finite stop obligation is independent of the optional admission age.
    p = prerequisites(); p.maximumAgeUs = 0;
    assert(Ess::prepareVelocity(c, axis(), 12, request(), p, now, deadline, options()));
    trigger(c);
    assert(Ess::serviceVelocity(c, c.stopDueUs));
    assert(c.phase == Ess::VelocityPhase::STOPPING && c.needsStop);
    assert(Ess::nextVelocity(c, c.servicedUs, work) && work.urgent && work.write);
    assert(Ess::serviceVelocity(c, deadline) && c.state == ActionState::FAILED);
    assert(c.stop.completion == ActionCompletion::NOT_OBSERVED);
}
int main() {
    testOptionalAgePolicy();
    testGoldenFramesCommonParityAndFiniteStop(); testRejectedParametersUnchanged(); testSignRangesAndLatchedSettings();
    testFailuresAndLocalCancellation(); testCheckedRepliesAndEnvelopePreservation(); testDeadlinesActivityAndDelayedService();
    testSharedNumericPreparation();
    testBoundedStopAndPreservedPrimaryFailure();
}
