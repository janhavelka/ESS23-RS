// SPDX-License-Identifier: MIT
#include <MotorControlRS/profiles/ess_rs/DriverSettings.h>
#include <MotorControlRS/profiles/ess_rs/Registers.h>
#include <cassert>
#include <cstring>
#include <limits>
#include <vector>

using namespace MotorControlRS;
namespace Ess = MotorControlRS::ESS_RS;
namespace {
ReadTarget target() { ReadTarget t; t.id = 4; t.address = 1; t.generation = 7; return t; }
uint16_t field(Ess::DriverField f) { return static_cast<uint16_t>(f); }
template<class T> struct Saved {
    unsigned char bytes[sizeof(T)];
    explicit Saved(const T& value) { std::memcpy(bytes, &value, sizeof(value)); }
    void check(const T& value) const { assert(std::memcmp(bytes, &value, sizeof(value)) == 0); }
};
void crc(std::vector<uint8_t>& bytes) {
    uint16_t value = 0xFFFF;
    for (uint8_t b : bytes) {
        value ^= b;
        for (unsigned i = 0; i < 8; ++i) value = (value & 1) ? (value >> 1) ^ 0xA001 : value >> 1;
    }
    bytes.push_back(static_cast<uint8_t>(value)); bytes.push_back(static_cast<uint8_t>(value >> 8));
}
std::vector<uint8_t> reply(std::initializer_list<uint16_t> words) {
    std::vector<uint8_t> bytes = {1, 3, static_cast<uint8_t>(words.size() * 2)};
    for (uint16_t value : words) { bytes.push_back(static_cast<uint8_t>(value >> 8)); bytes.push_back(static_cast<uint8_t>(value)); }
    crc(bytes); return bytes;
}
ActionEvent local(const Ess::DriverContext& c, ReadEventKind kind) {
    ActionEvent e; e.transport.target = c.target; e.transport.operationId = c.operationId;
    e.transport.step = c.step; e.transport.kind = kind; return e;
}
ActionEvent frame(const Ess::DriverContext& c, const uint8_t* bytes, std::size_t length, uint64_t at) {
    ActionEvent e = local(c, ReadEventKind::FRAME); e.transport.frame = bytes; e.transport.length = length;
    e.transport.txAccepted = 8; e.txComplete = e.responseConfirmed = e.transport.qualified = true;
    e.transport.earliestUs = at; e.transport.latestUs = at + 1; return e;
}
void supply(Ess::DriverContext& c, const std::vector<uint8_t>& bytes, uint64_t at = 0) {
    if (!at) at = c.servicedUs + 2;
    assert(Ess::advanceDriver(c, frame(c, bytes.data(), bytes.size(), at), at + 2));
}
Ess::DriverObservation snapshot(uint16_t order = 0, uint16_t enable = 0, uint16_t direction = 0) {
    Ess::DriverContext c; assert(Ess::prepareDriverRead(c, target(), 11, 17, 100, 10000));
    supply(c, reply({direction, 1600})); supply(c, reply({0, enable, order}));
    supply(c, reply({0x1234, 0x5678, 0x9ABC, 0xDEF0})); supply(c, reply({0, 1}));
    Ess::DriverObservation value; assert(Ess::getDriver(c, value)); return value;
}
Ess::DriverPrerequisites prerequisites(uint16_t enable = 0) {
    Ess::DriverPrerequisites p; p.configurationGeneration = 17; p.previous = snapshot(0, enable);
    p.stationaryQualified = p.inputsPermit = true; p.stationaryTarget = target();
    p.stationaryEarliestUs = 110; p.stationaryLatestUs = 120; p.maxAgeUs = 10000;
    return p;
}
Ess::DriverRequest request(uint16_t mask = 1) {
    Ess::DriverRequest r; r.configurationGeneration = 17; r.fields = mask;
    r.direction = Ess::DefaultDirection::REVERSED; r.subdivision = 3200; return r;
}
Ess::DriverContext update(uint16_t mask = 1, uint64_t deadline = 9000) {
    Ess::DriverContext c; assert(Ess::prepareDriverSettings(c, target(), 12, request(mask), prerequisites(), 200, deadline));
    return c;
}
Ess::PreparedDriver work(const Ess::DriverContext& c, uint64_t at = 0) {
    Ess::PreparedDriver w; assert(Ess::nextDriver(c, at ? at : c.servicedUs, w)); return w;
}
void ack(Ess::DriverContext& c, uint64_t at = 0) {
    const Ess::PreparedDriver w = work(c); assert(w.write);
    supply(c, std::vector<uint8_t>(w.bytes, w.bytes + w.length), at);
}
void checkUnchangedReject(Ess::DriverRequest r, Ess::DriverPrerequisites p, Err expected = Err::INVALID_CONFIG) {
    Ess::DriverContext c; c.operationId = 999; const Saved<Ess::DriverContext> saved(c);
    const Status s = Ess::prepareDriverSettings(c, target(), 12, r, p, 200, 9000);
    assert(!s && s.code == expected); saved.check(c);
}
void testReadAndPairOrder() {
    const auto high = snapshot(); assert(high.knownFields == 127 && high.pairKnown);
    assert(high.positiveBits == 0x12345678 && high.negativeBits == 0x9ABCDEF0);
    const auto low = snapshot(1); assert(low.positiveBits == 0x56781234 && low.negativeBits == 0xDEF09ABC);
    const auto unknown = snapshot(5, 7, 8);
    assert(!unknown.pairKnown && unknown.positiveBits == 0 && unknown.positiveWords[0] == 0x1234);
    assert(!(unknown.knownFields & (1 | 4 | 8)) && unknown.raw[0] == 8 && unknown.raw[2] == 5);
    Ess::DriverContext c; assert(Ess::prepareDriverRead(c, target(), 3, 17, 100, 500));
    const Saved<Ess::DriverObservation> immutable(high);
    const uint16_t regs[] = {0x10, 0x17, 0x37, 0x50}, counts[] = {2, 3, 4, 2};
    const auto w = work(c); assert(w.reg == regs[0] && w.count == counts[0] && !w.write);
    const auto again = work(c); assert(!std::memcmp(w.bytes, again.bytes, 8));
    supply(c, reply({0, 1600})); supply(c, reply({0, 0, 0}));
    auto corrupt = reply({1, 2, 3, 4}); corrupt.back() ^= 1; supply(c, corrupt);
    assert(c.state == ReadState::FAILED && c.outcome == Ess::DriverOutcome::REPLY_ERROR && c.completedSteps == 2);
    auto previous = high; assert(!Ess::getDriver(c, previous)); immutable.check(previous);
    Ess::PreparedDriver done; assert(Ess::nextDriver(c, c.servicedUs, done) && done.kind == Ess::ActionWork::DONE && !done.length);
    Ess::DriverContext d; assert(Ess::prepareDriverRead(d, target(), 3, 17, 100, 130));
    supply(d, reply({0, 1600})); supply(d, reply({0, 0, 0})); supply(d, reply({1, 2, 3, 4}));
    const auto bytes = reply({0, 1}); auto e = frame(d, bytes.data(), bytes.size(), 125);
    assert(Ess::advanceDriver(d, e, 1000) && d.state == ReadState::SUCCEEDED);
    for (unsigned i = 0; i < 4; ++i) {
        Ess::DriverContext n; assert(Ess::prepareDriverRead(n, target(), 3, 17, 100, 1000));
        while (n.step < i) {
            if (n.step == 0) supply(n, reply({0, 1600}));
            else if (n.step == 1) supply(n, reply({0, 0, 0}));
            else supply(n, reply({1, 2, 3, 4}));
        }
        const auto p = work(n); assert(p.reg == regs[i] && p.count == counts[i] && p.count <= 16);
    }
}
void testWholeCandidateAndPrerequisites() {
    auto r = request(); auto p = prerequisites();
    r.fields = 0; checkUnchangedReject(r, p);
    r = request(0x8000); checkUnchangedReject(r, p);
    for (unsigned i = 0; i < 7; ++i) {
        r = request(static_cast<uint16_t>(1u << i));
        switch (i) {
        case 0: r.direction = static_cast<Ess::DefaultDirection>(2); break;
        case 1: r.subdivision = 399; break;
        case 2: r.wordOrder = static_cast<Ess::WordOrder>(2); break;
        case 3: r.softLimitEnable = static_cast<Ess::SoftLimitEnable>(2); break;
        case 4: r.overLimitStop = static_cast<Ess::OverLimitStop>(2); break;
        case 5: r.interruption = static_cast<Ess::PvTriggerMode>(2); break;
        default: r.positionMode = static_cast<Ess::PositionMode>(2); break;
        }
        checkUnchangedReject(r, p);
    }
    r = request(3); r.subdivision = 51201; checkUnchangedReject(r, p);
    for (uint16_t native : {uint16_t(400), uint16_t(51200)}) {
        r = request(2); r.subdivision = native; Ess::DriverContext c;
        assert(Ess::prepareDriverSettings(c, target(), 3, r, p, 200, 1000) && work(c).value == native);
    }
    r = request(1 | 128 | 256); r.positiveLimit = std::numeric_limits<int64_t>::max();
    r.negativeLimit = std::numeric_limits<int64_t>::min(); checkUnchangedReject(r, p, Err::UNSUPPORTED);
    r = request(); p.previous.target.address = 2; checkUnchangedReject(r, p);
    p = prerequisites(); p.previous.configurationGeneration++; checkUnchangedReject(r, p);
    p = prerequisites(); p.configurationGeneration++; checkUnchangedReject(r, p);
    p = prerequisites(); p.previous.raw[0] = 1; checkUnchangedReject(r, p);
    p = prerequisites(); p.maxAgeUs = 90; checkUnchangedReject(r, p);
    p = prerequisites(); p.maxAgeUs = 100;
    // Closure happened after 100, but the first read was eligible at 100.
    // It cannot become newly fresh by waiting until the final reply arrived.
    p.stationaryEarliestUs = p.stationaryLatestUs = 199; checkUnchangedReject(r, p);
    p = prerequisites(); p.inputsPermit = false; checkUnchangedReject(r, p);
    p = prerequisites(); p.allowEchoReadback = true; checkUnchangedReject(r, p);
    p = prerequisites(); p.stationaryQualified = false; checkUnchangedReject(r, p);
    p = prerequisites(); p.stationaryTarget.generation++; checkUnchangedReject(r, p);
    p = prerequisites(); p.stationaryLatestUs = 201; checkUnchangedReject(r, p);
    for (uint16_t invalid : {uint16_t(4), uint16_t(8), uint16_t(0x8000)}) {
        p = prerequisites(); p.rawMotion = invalid; checkUnchangedReject(r, p);
    }
    p = prerequisites(); p.rawAlarm = 1; checkUnchangedReject(r, p);
    // Released is a state flag, not evidence of running; settings still need the explicit stationary/input qualification.
    p = prerequisites(); p.rawMotion = 16; Ess::DriverContext c;
    assert(Ess::prepareDriverSettings(c, target(), 3, r, p, 200, 1000));
    const Saved<Ess::DriverContext> saved(c);
    assert(!Ess::prepareDriverSettings(c, target(), 4, c.request, c.prerequisites, 200, 1000)); saved.check(c);
}
void testLimitDependencies() {
    auto r = request(1); auto p = prerequisites(1); checkUnchangedReject(r, p);
    r.fields |= field(Ess::DriverField::SOFT_LIMIT_ENABLE); r.softLimitEnable = Ess::SoftLimitEnable::LIMITS_OFF;
    Ess::DriverContext c; assert(Ess::prepareDriverSettings(c, target(), 3, r, p, 200, 9000));
    assert(work(c).reg == 0x18 && work(c).value == 0); ack(c); supply(c, reply({0}));
    assert(work(c).reg == 0x10); assert(c.prerequisites.previous.raw[3] == 1);
    r = request(8); r.softLimitEnable = Ess::SoftLimitEnable::AFTER_HOMING; p = prerequisites();
    checkUnchangedReject(r, p);
    p.limitSemanticsQualified = p.homedReferenceQualified = true; p.referenceTarget = target();
    p.referenceConfigurationGeneration = 17; p.rawMotion = 2;
    std::memcpy(p.qualifiedPositiveWords, p.previous.positiveWords, sizeof(p.qualifiedPositiveWords));
    std::memcpy(p.qualifiedNegativeWords, p.previous.negativeWords, sizeof(p.qualifiedNegativeWords));
    p.positiveLimit = 100; p.negativeLimit = -100;
    assert(Ess::prepareDriverSettings(c, target(), 3, r, p, 200, 9000));
    p.qualifiedPositiveWords[0]++; checkUnchangedReject(r, p); p.qualifiedPositiveWords[0]--;
    p.qualifiedNegativeWords[1]++; checkUnchangedReject(r, p); p.qualifiedNegativeWords[1]--;
    p.referenceConfigurationGeneration++; checkUnchangedReject(r, p); p.referenceConfigurationGeneration--;
    p.rawMotion = 0; checkUnchangedReject(r, p); p.rawMotion = 2;
    p.rawMotion = 0x12; checkUnchangedReject(r, p); p.rawMotion = 2;
    p.positiveLimit = p.negativeLimit; checkUnchangedReject(r, p);
    p.positiveLimit = 0x10000000; checkUnchangedReject(r, p);
    p.positiveLimit = 100; p.negativeLimit = -0x10000000; checkUnchangedReject(r, p);
    p.negativeLimit = -100; p.referenceTarget.generation++; checkUnchangedReject(r, p);
    p.referenceTarget = target(); r.fields |= 2; checkUnchangedReject(r, p);
}
void testProgressNoRollbackAndCopiedStorage() {
    auto p = prerequisites(); auto r = request(127); r.wordOrder = Ess::WordOrder::LOW_WORD_FIRST;
    Ess::DriverContext c; assert(Ess::prepareDriverSettings(c, target(), 12, r, p, 200, 9000));
    p.previous.raw[0] = 99; r.direction = Ess::DefaultDirection::NORMAL;
    assert(c.prerequisites.previous.raw[0] == 0 && c.request.direction == Ess::DefaultDirection::REVERSED);
    for (unsigned i = 0; i < 7; ++i) {
        const auto w = work(c); const unsigned index = c.order[c.step / 2];
        assert(w.write && w.reg == c.progress[index].reg && w.count == 1);
        ack(c); assert(c.progress[index].acknowledged && c.uncertain);
        const auto rd = work(c); assert(!rd.write && rd.reg == w.reg && rd.count == 1);
        supply(c, reply({w.value})); assert(!c.uncertain && !c.progress[index].activeKnown);
    }
    assert(c.state == ReadState::SUCCEEDED && c.completedSteps == 14 && c.effects == 127);
    assert(!Ess::advanceDriver(c, local(c, ReadEventKind::CANCEL), c.servicedUs));
    Ess::DriverContext partial = update(3); ack(partial); supply(partial, reply({1}));
    ack(partial); supply(partial, reply({3199}));
    assert(partial.outcome == Ess::DriverOutcome::READBACK_MISMATCH && partial.effects == 3 && partial.uncertain);
    assert(partial.progress[0].readbackKnown && partial.progress[0].readback == 1);
    assert(partial.progress[1].acknowledged && partial.progress[1].readback == 3199);
    assert(partial.prerequisites.previous.raw[1] == 1600 && partial.request.subdivision == 3200);
    assert(work(partial).kind == Ess::ActionWork::DONE);
    auto rejected = update(3); ack(rejected); supply(rejected, reply({1}));
    std::vector<uint8_t> exception = {1, 0x86, 3}; crc(exception); supply(rejected, exception);
    assert(rejected.outcome == Ess::DriverOutcome::REPLY_ERROR && rejected.progress[1].execution == ActionExecution::REJECTED);
    assert(rejected.effects == 3 && !rejected.uncertain && rejected.progress[0].readbackKnown);
    auto unknownException = update(); exception = {1, 0x86, 0x7F}; crc(exception); supply(unknownException, exception);
    assert(unknownException.uncertain && unknownException.progress[0].execution == ActionExecution::UNKNOWN);
}
void testFaultsCancellationAndExpiry() {
    for (unsigned step = 0; step < 4; ++step) {
        auto c = update(3);
        while (c.step < step) { if ((c.step & 1) == 0) ack(c); else supply(c, reply({1})); }
        auto e = local(c, ReadEventKind::CANCEL);
        if ((step & 1) == 0) { e.transport.txAccepted = 3; e.transport.executionUnknown = true; }
        assert(Ess::advanceDriver(c, e, c.servicedUs + 1));
        assert(c.outcome == Ess::DriverOutcome::CANCELLED);
        assert(c.uncertain);
        const Saved<Ess::DriverContext> retained(c);
        assert(!Ess::advanceDriver(c, e, c.servicedUs + 1)); retained.check(c);
    }
    auto unsent = update(); assert(Ess::advanceDriver(unsent, local(unsent, ReadEventKind::CANCEL), 201));
    assert(!unsent.effects && !unsent.uncertain);
    auto settledPartial = update(3); ack(settledPartial); supply(settledPartial, reply({1}));
    assert(Ess::advanceDriver(settledPartial, local(settledPartial, ReadEventKind::CANCEL), settledPartial.servicedUs + 1));
    assert(settledPartial.effects == 1 && !settledPartial.uncertain && !settledPartial.progress[1].acknowledged);
    auto lost = update(); auto timeout = local(lost, ReadEventKind::TRANSPORT_FAILURE);
    timeout.transport.txAccepted = 8; timeout.txComplete = true;
    assert(Ess::advanceDriver(lost, timeout, 300)); assert(lost.uncertain && lost.effects == 1);
    auto wrong = update(); const auto w = work(wrong); auto bytes = std::vector<uint8_t>(w.bytes, w.bytes + w.length);
    bytes[5] ^= 1; bytes.resize(6); crc(bytes); supply(wrong, bytes);
    assert(wrong.outcome == Ess::DriverOutcome::REPLY_ERROR && wrong.uncertain);
    auto echo = update(); const auto ew = work(echo); auto e = frame(echo, ew.bytes, 8, 220); e.responseConfirmed = false;
    assert(Ess::advanceDriver(echo, e, 222) && echo.outcome == Ess::DriverOutcome::UNCONFIRMED_RESPONSE && echo.uncertain);
    auto crcBad = update(); const auto cw = work(crcBad); bytes.assign(cw.bytes, cw.bytes + 8); bytes[7] ^= 1;
    supply(crcBad, bytes); assert(crcBad.status.code == Err::CRC_ERROR && crcBad.uncertain);
    auto foreign = update(); bytes.assign(cw.bytes, cw.bytes + 6); bytes[0] = 2; crc(bytes);
    supply(foreign, bytes); assert(foreign.status.code == Err::FRAME_ERROR && foreign.uncertain);
    auto invalid = update(); const Saved<Ess::DriverContext> saved(invalid);
    e = local(invalid, ReadEventKind::CANCEL); e.transport.target.generation++;
    assert(!Ess::advanceDriver(invalid, e, 201)); saved.check(invalid);
    e = local(invalid, ReadEventKind::CANCEL); e.transport.operationId++;
    assert(!Ess::advanceDriver(invalid, e, 201)); saved.check(invalid);
    e = local(invalid, ReadEventKind::CANCEL); e.transport.step++;
    assert(!Ess::advanceDriver(invalid, e, 201)); saved.check(invalid);
    e = local(invalid, ReadEventKind::CANCEL); e.transport.txAccepted = 9;
    assert(!Ess::advanceDriver(invalid, e, 201)); saved.check(invalid);
    e = local(invalid, ReadEventKind::DEADLINE); assert(!Ess::advanceDriver(invalid, e, 201)); saved.check(invalid);
    auto boundary = update(1, 300); ack(boundary); bytes = reply({1}); e = frame(boundary, bytes.data(), bytes.size(), 298);
    assert(Ess::advanceDriver(boundary, e, 9000) && boundary.state == ReadState::SUCCEEDED && !boundary.uncertain);
    auto late = update(1, 300); ack(late); e = frame(late, bytes.data(), bytes.size(), 300);
    assert(Ess::advanceDriver(late, e, 9000) && late.outcome == Ess::DriverOutcome::DEADLINE && late.uncertain);
    auto partialLate = update(3, 300); ack(partialLate); e = frame(partialLate, bytes.data(), bytes.size(), 298);
    assert(Ess::advanceDriver(partialLate, e, 9000) && partialLate.outcome == Ess::DriverOutcome::DEADLINE);
    assert(!partialLate.uncertain && partialLate.effects == 1 && work(partialLate).kind == Ess::ActionWork::DONE);
    auto p = prerequisites(); p.maxAgeUs = 200; p.stationaryEarliestUs = 95; Ess::DriverContext capped;
    assert(Ess::prepareDriverSettings(capped, target(), 3, request(), p, 200, 9000));
    assert(work(capped).deadlineUs == 295 && capped.deadlineUs == 9000);
    Ess::PreparedDriver pending; assert(!Ess::nextDriver(capped, 295, pending));
    assert(Ess::advanceDriver(capped, local(capped, ReadEventKind::DEADLINE), 295));
    assert(capped.outcome == Ess::DriverOutcome::DEADLINE && !capped.effects);
    assert(Ess::prepareDriverSettings(capped, target(), 3, request(), p, 200, 9000));
    e = local(capped, ReadEventKind::DEADLINE); e.transport.txAccepted = 8; e.txComplete = true;
    assert(Ess::advanceDriver(capped, e, 400) && capped.uncertain && capped.effects == 1);
    assert(capped.deadlineUs == 9000);
    assert(Ess::prepareDriverSettings(capped, target(), 3, request(), p, 200, 9000));
    const auto capWork = work(capped); e = frame(capped, capWork.bytes, 8, 310);
    assert(Ess::advanceDriver(capped, e, 400) && capped.outcome == Ess::DriverOutcome::DEADLINE);
    assert(!capped.progress[0].acknowledged && capped.progress[0].execution == ActionExecution::UNKNOWN && capped.uncertain && !capped.progress[0].activeKnown);
}
void testUnconfirmedReadEvidence() {
    const auto historical = snapshot();
    const Saved<Ess::DriverObservation> retained(historical);
    for (uint8_t rejectedStep = 0; rejectedStep < Ess::DRIVER_READ_STEPS; ++rejectedStep) {
        Ess::DriverContext c; assert(Ess::prepareDriverRead(c, target(), 3, 17, 100, 1000));
        const std::vector<uint8_t> frames[] = {
            reply({0, 1600}), reply({0, 0, 0}), reply({1, 2, 3, 4}), reply({0, 1})
        };
        for (uint8_t i = 0; i < rejectedStep; ++i) supply(c, frames[i]);
        const auto& bytes = frames[rejectedStep];
        auto e = frame(c, bytes.data(), bytes.size(), c.servicedUs + 2);
        e.responseConfirmed = false;
        assert(Ess::advanceDriver(c, e, c.servicedUs + 4));
        assert(c.outcome == Ess::DriverOutcome::UNCONFIRMED_RESPONSE && c.state == ReadState::FAILED);
        assert(c.completedSteps == rejectedStep && !c.observations[rejectedStep].responseConfirmed);
        assert(work(c).kind == Ess::ActionWork::DONE);
        auto previous = historical;
        assert(!Ess::getDriver(c, previous)); retained.check(previous);
    }
    // A retained observation from a formerly permissive reader must not become
    // valid preparation evidence merely because its CRC and decoded values match.
    for (uint8_t i = 0; i < Ess::DRIVER_READ_STEPS; ++i) {
        auto p = prerequisites(); p.previous.provenance[i].responseConfirmed = false;
        checkUnchangedReject(request(), p);
    }
    for (uint16_t mask : {uint16_t(1), uint16_t(3)}) {
        auto c = update(mask); const Saved<Ess::DriverObservation> previous(c.prerequisites.previous); ack(c);
        const auto bytes = reply({1}); auto e = frame(c, bytes.data(), bytes.size(), c.servicedUs + 2);
        e.responseConfirmed = false;
        assert(Ess::advanceDriver(c, e, c.servicedUs + 4));
        assert(c.outcome == Ess::DriverOutcome::UNCONFIRMED_RESPONSE && c.state == ReadState::FAILED);
        assert(c.completedSteps == 1 && c.effects == 1 && c.uncertain);
        assert(c.progress[0].acknowledged && !c.progress[0].readbackKnown);
        assert(!c.progress[1].acknowledged && work(c).kind == Ess::ActionWork::DONE);
        previous.check(c.prerequisites.previous);
    }
    // Parser errors retain precedence over the source-confirmation failure.
    auto c = update(); ack(c); auto bytes = reply({1}); bytes.back() ^= 1;
    auto e = frame(c, bytes.data(), bytes.size(), c.servicedUs + 2); e.responseConfirmed = false;
    assert(Ess::advanceDriver(c, e, c.servicedUs + 4));
    assert(c.outcome == Ess::DriverOutcome::REPLY_ERROR && c.status.code == Err::CRC_ERROR && c.uncertain);
}
} // namespace
int main() {
    testReadAndPairOrder(); testWholeCandidateAndPrerequisites(); testLimitDependencies();
    testProgressNoRollbackAndCopiedStorage(); testFaultsCancellationAndExpiry();
    testUnconfirmedReadEvidence();
}
