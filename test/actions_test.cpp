// SPDX-License-Identifier: MIT
#include <MotorControlRS/profiles/ess_rs/Actions.h>
#include <cassert>
#include <cstring>
#include <limits>
#include <vector>

using namespace MotorControlRS;
namespace Ess = MotorControlRS::ESS_RS;
namespace {
const uint8_t ENABLE[] = {1, 6, 0, 0x2D, 0, 0x12, 0x99, 0xCE};
const uint8_t RELEASE[] = {1, 6, 0, 0x2D, 0, 0x11, 0xD9, 0xCF};
const uint8_t CLEAR[] = {1, 6, 0, 0x2D, 0, 0x21, 0xD9, 0xDB};
const uint8_t NORMAL[] = {1, 6, 0, 0x27, 1, 0, 0x38, 0x51};
const uint8_t DIRECT[] = {1, 6, 0, 0x27, 2, 0, 0x38, 0xA1};
ReadTarget target() { ReadTarget t; t.id = 4; t.address = 1; t.generation = 7; return t; }
template<class T> struct Saved {
    unsigned char bytes[sizeof(T)];
    explicit Saved(const T& value) { std::memcpy(bytes, &value, sizeof(value)); }
    void check(const T& value) const { assert(std::memcmp(bytes, &value, sizeof(value)) == 0); }
};
void crc(std::vector<uint8_t>& bytes) {
    uint16_t value = 0xFFFF;
    for (uint8_t byte : bytes) {
        value ^= byte;
        for (unsigned bit = 0; bit < 8; ++bit) value = (value & 1) ? (value >> 1) ^ 0xA001 : value >> 1;
    }
    bytes.push_back(static_cast<uint8_t>(value)); bytes.push_back(static_cast<uint8_t>(value >> 8));
}
std::vector<uint8_t> motion(uint16_t alarm, uint16_t flags) {
    std::vector<uint8_t> b = {1, 3, 4, static_cast<uint8_t>(alarm >> 8), static_cast<uint8_t>(alarm),
        static_cast<uint8_t>(flags >> 8), static_cast<uint8_t>(flags)};
    crc(b); return b;
}
Ess::ActionContext action(ActionKind kind = ActionKind::STOP, uint64_t deadline = 10000, bool allowUnconfirmed = false) {
    Ess::ActionContext c; ActionRequest request; request.kind = kind;
    if (kind == ActionKind::STOP) request.stop.behavior = StopBehavior::CONFIGURED_DECELERATION;
    ActionOptions options; options.pollIntervalUs = 100; options.maxPolls = 3;
    options.allowUnconfirmedWriteObservation = allowUnconfirmed;
    assert(Ess::prepareAction(c, target(), 12, request, 100, deadline, options)); return c;
}
ActionEvent event(const Ess::ActionContext& c, ReadEventKind kind) {
    ActionEvent e; e.transport.target = c.target; e.transport.operationId = c.operationId;
    e.transport.step = c.step; e.transport.kind = kind; return e;
}
ActionEvent frame(const Ess::ActionContext& c, const uint8_t* bytes, std::size_t length, uint64_t at) {
    ActionEvent e = event(c, ReadEventKind::FRAME);
    e.transport.frame = bytes; e.transport.length = length; e.transport.txAccepted = 8;
    e.transport.qualified = e.responseConfirmed = e.txComplete = true;
    e.transport.earliestUs = at; e.transport.latestUs = at + 10; return e;
}
void acknowledge(Ess::ActionContext& c) {
    Ess::PreparedAction prepared; assert(Ess::nextAction(c, 100, prepared));
    const ActionEvent e = frame(c, prepared.bytes, prepared.length, 200);
    assert(Ess::advanceAction(c, e, 220));
    assert(c.state == ActionState::ACTIVE && c.execution == ActionExecution::ACKNOWLEDGED);
    assert(c.completion == ActionCompletion::NOT_OBSERVED && c.step == 1);
}
void observe(Ess::ActionContext& c, uint16_t alarm, uint16_t flags) {
    const auto bytes = motion(alarm, flags);
    const uint64_t eligible = c.eligibleUs;
    assert(Ess::advanceAction(c, frame(c, bytes.data(), bytes.size(), eligible), eligible + 20));
}
void testExactCommandsAndCommonNativeParity() {
    using Prepare = Status (*)(Ess::ActionContext&, const ReadTarget&, uint32_t, uint64_t, uint64_t, const ActionOptions&) noexcept;
    const Prepare native[] = {Ess::prepareEnable, Ess::prepareRelease, Ess::prepareClearAlarm,
        Ess::prepareNormalStop, Ess::prepareEmergencyStop};
    const uint8_t* expected[] = {ENABLE, RELEASE, CLEAR, NORMAL, DIRECT};
    for (unsigned i = 0; i < 5; ++i) {
        Ess::ActionContext c; assert(native[i](c, target(), 12, 100, 100000, ActionOptions()));
        Ess::PreparedAction p; assert(Ess::nextAction(c, 100, p));
        assert(p.kind == Ess::ActionWork::TRANSACTION && p.write && p.length == 8 && p.step == 0);
        assert(std::memcmp(p.bytes, expected[i], 8) == 0);
        Ess::ActionContext common;
        if (i == 0) assert(MotorControlRS::prepareEnable(common, target(), 12, 100, 100000));
        else if (i == 1) assert(MotorControlRS::prepareRelease(common, target(), 12, 100, 100000));
        else if (i == 2) assert(MotorControlRS::prepareClearAlarm(common, target(), 12, 100, 100000));
        else { StopPolicy policy; policy.behavior = i == 3 ? StopBehavior::CONFIGURED_DECELERATION : StopBehavior::DIRECT;
            assert(MotorControlRS::prepareStop(common, target(), 12, policy, 100, 100000)); }
        Ess::PreparedAction other; assert(Ess::nextAction(common, 100, other));
        assert(other.reg == p.reg && other.value == p.value && std::memcmp(other.bytes, p.bytes, 8) == 0);
    }
}
void testPolicyRejectionPreservesPreparedOperation() {
    auto c = action(); const Saved<Ess::ActionContext> saved(c);
    ActionRequest request; request.kind = ActionKind::STOP;
    assert(!Ess::prepareAction(c, target(), 12, request, 100, 1000)); saved.check(c);
    request.stop.behavior = StopBehavior::DIRECT;
    for (DeviceQueue queue : {DeviceQueue::PRESERVE, DeviceQueue::DISCARD}) {
        request.stop.deviceQueue = queue;
        assert(Ess::prepareAction(c, target(), 12, request, 100, 1000).code == Err::UNSUPPORTED); saved.check(c);
    }
    request.stop.deviceQueue = DeviceQueue::UNSPECIFIED; request.stop.customDeceleration = true;
    assert(Ess::prepareAction(c, target(), 12, request, 100, 1000).code == Err::UNSUPPORTED); saved.check(c);
    request.stop.customDeceleration = false;
    for (unsigned field = 0; field < 3; ++field) {
        ReadTarget bad = target(); if (field == 0) bad.id = 0; else if (field == 1) bad.address = 248; else bad.generation = 0;
        assert(!Ess::prepareAction(c, bad, 12, request, 100, 1000)); saved.check(c);
    }
    assert(!Ess::prepareAction(c, target(), 0, request, 100, 1000)); saved.check(c);
    assert(!Ess::prepareAction(c, target(), 12, request, 100, 100)); saved.check(c);
    ActionOptions options; options.maxPolls = 65;
    assert(!Ess::prepareAction(c, target(), 12, request, 100, 1000, options)); saved.check(c);
    options.maxPolls = 1; options.pollIntervalUs = 0;
    assert(!Ess::prepareAction(c, target(), 12, request, 100, 1000, options)); saved.check(c);
    request.kind = ActionKind::ENABLE;
    assert(!Ess::prepareAction(c, target(), 12, request, 100, 1000)); saved.check(c);
}
void testEchoAcknowledgementAndReportedCompletionAreSeparate() {
    auto echo = action(ActionKind::ENABLE);
    auto e = frame(echo, ENABLE, sizeof(ENABLE), 200); e.responseConfirmed = false;
    assert(Ess::advanceAction(echo, e, 220));
    assert(echo.state == ActionState::FAILED && echo.outcome == ActionOutcome::UNCONFIRMED_RESPONSE);
    assert(echo.execution == ActionExecution::UNKNOWN && echo.completion == ActionCompletion::NOT_OBSERVED);
    assert(echo.writeEvidence.txComplete && echo.writeEvidence.txAccepted == 8);
    for (ActionKind kind : {ActionKind::ENABLE, ActionKind::RELEASE, ActionKind::CLEAR_ALARM, ActionKind::STOP}) {
        auto c = action(kind); acknowledge(c);
        Ess::PreparedAction p; assert(Ess::nextAction(c, 220, p));
        assert(p.kind == Ess::ActionWork::WAIT && p.length == 0 && p.eligibleUs == 320);
        assert(Ess::nextAction(c, 320, p) && p.kind == Ess::ActionWork::TRANSACTION && !p.write);
        assert(p.reg == 6 && p.count == 2 && p.length == 8 && p.step == 1);
        const uint16_t unmet = kind == ActionKind::ENABLE ? 0x10 : kind == ActionKind::RELEASE ? 0 :
            kind == ActionKind::CLEAR_ALARM ? 8 : 4;
        observe(c, 5, unmet);
        assert(c.state == ActionState::ACTIVE && c.polls == 1 && c.step == 2);
        const uint16_t met = kind == ActionKind::RELEASE ? 0x10 : kind == ActionKind::STOP ? 0x8008 : 0;
        observe(c, kind == ActionKind::STOP ? 5 : 0, met);
        assert(c.state == ActionState::SUCCEEDED && c.completion == ActionCompletion::OBSERVED);
        assert(c.execution == ActionExecution::ACKNOWLEDGED && c.rawMotion == met);
        assert(Ess::nextAction(c, c.servicedUs, p) && p.kind == Ess::ActionWork::DONE && p.length == 0);
    }
}
void testOptedInObservationRetainsUnknownExecution() {
    for (ActionKind kind : {ActionKind::ENABLE, ActionKind::RELEASE, ActionKind::CLEAR_ALARM, ActionKind::STOP}) {
        auto c = action(kind, 10000, true);
        Ess::PreparedAction work; assert(Ess::nextAction(c, 100, work));
        auto e = frame(c, work.bytes, work.length, 200); e.responseConfirmed = false;
        assert(Ess::advanceAction(c, e, 220));
        assert(c.state == ActionState::ACTIVE && c.execution == ActionExecution::UNKNOWN && c.step == 1);
        const Saved<Ess::ActionEvidence> original(c.writeEvidence);
        assert(Ess::nextAction(c, c.eligibleUs, work));
        assert(!work.write && work.bytes[1] == 3);
        observe(c, 0, kind == ActionKind::RELEASE ? 16 : 0);
        assert(c.state == ActionState::SUCCEEDED && c.completion == ActionCompletion::OBSERVED);
        assert(c.execution == ActionExecution::UNKNOWN && !c.writeEvidence.responseConfirmed);
        assert(c.lastObservation.responseConfirmed); original.check(c.writeEvidence);
        assert(Ess::nextAction(c, c.servicedUs, work) && work.kind == Ess::ActionWork::DONE);
    }
    // An unconfirmed FC03 cannot turn a possible local echo into an observation.
    auto c = action(ActionKind::STOP, 10000, true);
    auto e = frame(c, NORMAL, sizeof(NORMAL), 200); e.responseConfirmed = false;
    assert(Ess::advanceAction(c, e, 220));
    const auto bytes = motion(0, 0);
    e = frame(c, bytes.data(), bytes.size(), c.eligibleUs); e.responseConfirmed = false;
    assert(Ess::advanceAction(c, e, c.eligibleUs + 20));
    assert(c.outcome == ActionOutcome::UNCONFIRMED_RESPONSE && !c.observationKnown);
    assert(c.execution == ActionExecution::UNKNOWN && c.completion == ActionCompletion::NOT_OBSERVED);
}
void testOptedInObservationPreservesWriteFailures() {
    for (unsigned fault = 0; fault < 8; ++fault) {
        auto c = action(ActionKind::STOP, 1000, true);
        std::vector<uint8_t> bytes(NORMAL, NORMAL + sizeof(NORMAL));
        if (fault == 0) bytes.back() ^= 1;
        if (fault == 1) bytes.assign(ENABLE, ENABLE + sizeof(ENABLE));
        if (fault == 2) { bytes = {1, 0x86, 2}; crc(bytes); }
        auto e = frame(c, bytes.data(), bytes.size(), fault == 4 ? 995 : 200);
        e.responseConfirmed = false;
        uint64_t now = fault == 4 ? 1020 : 220;
        if (fault == 3) { e.transport.qualified = false; e.transport.earliestUs = e.transport.latestUs = 0; }
        if (fault == 5) { e = event(c, ReadEventKind::TRANSPORT_FAILURE); e.transport.txAccepted = 8; e.txComplete = true; }
        if (fault == 6) e.txComplete = false;
        if (fault == 7) { e.transport.txAccepted = 7; e.txComplete = false; }
        if (fault >= 6) {
            const Saved<Ess::ActionContext> saved(c);
            assert(!Ess::advanceAction(c, e, now)); saved.check(c); continue;
        }
        assert(Ess::advanceAction(c, e, now));
        const ActionOutcome expected = fault < 3 ? ActionOutcome::REPLY_ERROR : fault == 3 ?
            ActionOutcome::TIMING_UNQUALIFIED : fault == 4 ? ActionOutcome::DEADLINE : ActionOutcome::TRANSPORT_ERROR;
        assert(c.state == ActionState::FAILED && c.outcome == expected && c.execution == ActionExecution::UNKNOWN);
        assert(!c.observationKnown && c.completion == ActionCompletion::NOT_OBSERVED);
        Ess::PreparedAction work; assert(Ess::nextAction(c, now, work));
        assert(work.kind == Ess::ActionWork::DONE && work.length == 0);
    }
}
void testInvalidEnvelopesAndDuplicatesDoNotMutate() {
    auto c = action();
    const ActionEvent good = frame(c, NORMAL, sizeof(NORMAL), 200);
    for (unsigned fault = 0; fault < 13; ++fault) {
        auto e = good;
        switch (fault) {
        case 0: ++e.transport.target.id; break;
        case 1: ++e.transport.target.generation; break;
        case 2: ++e.transport.target.address; break;
        case 3: ++e.transport.operationId; break;
        case 4: ++e.transport.step; break;
        case 5: e.transport.frame = nullptr; break;
        case 6: e.transport.txAccepted = 9; break;
        case 7: e.txComplete = false; break;
        case 8: e.transport.earliestUs = 99; break;
        case 9: e.transport.latestUs = 1000; break;
        case 10: e.transport.qualified = false; break;
        case 11: e.transport.txAccepted = 7; break;
        default: e.transport.kind = static_cast<ReadEventKind>(99); break;
        }
        const Saved<Ess::ActionContext> saved(c);
        assert(!Ess::advanceAction(c, e, 220)); saved.check(c);
    }
    assert(Ess::advanceAction(c, good, 220));
    const Saved<Ess::ActionContext> saved(c);
    assert(!Ess::advanceAction(c, good, 220)); saved.check(c);
    auto state = motion(0, 0);
    assert(!Ess::advanceAction(c, frame(c, state.data(), state.size(), c.eligibleUs - 1), c.eligibleUs + 20)); saved.check(c);
    auto local = event(c, ReadEventKind::DEADLINE);
    assert(!Ess::advanceAction(c, local, 500)); saved.check(c);
    local.transport.kind = ReadEventKind::CANCEL; local.responseConfirmed = true;
    assert(!Ess::advanceAction(c, local, 500)); saved.check(c);
    assert(!Ess::advanceAction(c, event(c, ReadEventKind::CANCEL), 219)); saved.check(c);
}
void testFailuresRetainUncertaintyAndNeverReplay() {
    for (ReadEventKind kind : {ReadEventKind::TRANSPORT_FAILURE, ReadEventKind::CANCEL, ReadEventKind::DEADLINE}) {
        for (std::size_t accepted : {std::size_t(0), std::size_t(3), std::size_t(8)}) {
            auto c = action(); auto e = event(c, kind);
            e.transport.txAccepted = accepted; e.txComplete = accepted == 8;
            e.transport.transportDetail = 123;
            assert(Ess::advanceAction(c, e, kind == ReadEventKind::DEADLINE ? c.deadlineUs : 200));
            assert(c.state == ActionState::FAILED && c.completion == ActionCompletion::NOT_OBSERVED);
            assert(c.execution == (accepted ? ActionExecution::UNKNOWN : ActionExecution::NOT_TRANSMITTED));
            assert(c.writeEvidence.txAccepted == accepted && c.failureEvidence.transportDetail == 123);
            Ess::PreparedAction p; assert(Ess::nextAction(c, c.servicedUs, p));
            assert(p.kind == Ess::ActionWork::DONE && p.length == 0);
            const Saved<Ess::ActionContext> saved(c); assert(!Ess::advanceAction(c, e, c.servicedUs)); saved.check(c);
        }
        auto c = action(); acknowledge(c);
        assert(Ess::advanceAction(c, event(c, kind), kind == ReadEventKind::DEADLINE ? c.deadlineUs : 230));
        assert(c.state == ActionState::FAILED && c.execution == ActionExecution::ACKNOWLEDGED);
        assert(c.completion == ActionCompletion::NOT_OBSERVED && c.writeEvidence.status);
    }
}
void testBadRepliesAndExceptionPreserveEvidence() {
    for (unsigned fault = 0; fault < 4; ++fault) {
        auto c = action(); std::vector<uint8_t> bad(NORMAL, NORMAL + sizeof(NORMAL));
        if (fault == 0) bad[7] ^= 1;
        else if (fault == 1) bad.assign(ENABLE, ENABLE + sizeof(ENABLE));
        else if (fault == 2) bad.resize(40, 0xA5);
        else { bad = {1, 0x86, 7}; crc(bad); }
        auto received = frame(c, bad.data(), bad.size(), 200);
        // The owner can identify a drive frame while its bad CRC/echo leaves
        // command execution unknown. This is a correlated failure to consume.
        received.transport.executionUnknown = fault != 3;
        assert(Ess::advanceAction(c, received, 220));
        assert(c.state == ActionState::FAILED && c.outcome == ActionOutcome::REPLY_ERROR);
        assert(c.execution == (fault == 3 ? ActionExecution::REJECTED : ActionExecution::UNKNOWN));
        assert(c.failureEvidence.receivedLength == bad.size() && c.failureEvidence.length <= Ess::ACTION_MAX_REPLY_BYTES);
        assert(c.failureEvidence.executionUnknown == (fault != 3));
        if (fault == 3) assert(c.status.code == Err::EXCEPTION && c.status.detail == 7);
        else assert(!c.status && c.completion == ActionCompletion::NOT_OBSERVED);
    }
    auto confirmed = action();
    auto acknowledged = frame(confirmed, NORMAL, sizeof(NORMAL), 200);
    acknowledged.transport.executionUnknown = true;
    assert(Ess::advanceAction(confirmed, acknowledged, 220));
    assert(confirmed.execution == ActionExecution::ACKNOWLEDGED && confirmed.writeEvidence.executionUnknown);
    auto c = action(); acknowledge(c); observe(c, 0x55, 0x8004);
    const auto last = c.lastObservation; auto bad = motion(0, 0); bad.back() ^= 1;
    assert(Ess::advanceAction(c, frame(c, bad.data(), bad.size(), c.eligibleUs), c.eligibleUs + 20));
    assert(c.state == ActionState::FAILED && c.status.code == Err::CRC_ERROR && c.execution == ActionExecution::ACKNOWLEDGED);
    assert(c.rawAlarm == 0x55 && c.rawMotion == 0x8004 && c.observationKnown);
    assert(c.lastObservation.step == last.step && std::memcmp(c.lastObservation.raw, last.raw, last.length) == 0);
}
void testExceptionExecutionUsesOnlyDocumentedRejections() {
    for (uint8_t code : {0, 1, 2, 3, 4, 5, 6, 7, 8, 0xE7, 0xFF}) {
        auto c = action();
        std::vector<uint8_t> bytes = {1, 0x86, code}; crc(bytes);
        assert(Ess::advanceAction(c, frame(c, bytes.data(), bytes.size(), 200), 220));
        assert(c.state == ActionState::FAILED && c.outcome == ActionOutcome::REPLY_ERROR);
        assert(c.status.code == Err::EXCEPTION && c.status.detail == code);
        const bool documentedRejection = code >= 1 && code <= 7;
        assert(c.execution == (documentedRejection ? ActionExecution::REJECTED : ActionExecution::UNKNOWN));
        assert(c.completion == ActionCompletion::NOT_OBSERVED && !c.observationKnown);
        assert(c.writeEvidence.status.code == Err::EXCEPTION && c.writeEvidence.status.detail == code);
        assert(c.writeEvidence.frameError == Ess::FrameError::EXCEPTION);
        assert(c.writeEvidence.txAccepted == 8 && c.writeEvidence.txComplete && c.writeEvidence.responseConfirmed);
        assert(c.writeEvidence.length == bytes.size() &&
            std::memcmp(c.writeEvidence.raw, bytes.data(), bytes.size()) == 0);
        assert(c.failureEvidence.status.detail == code &&
            std::memcmp(c.failureEvidence.raw, bytes.data(), bytes.size()) == 0);
        Ess::PreparedAction p; assert(Ess::nextAction(c, 220, p));
        assert(p.kind == Ess::ActionWork::DONE && p.length == 0);
        const Saved<Ess::ActionContext> saved(c);
        assert(!Ess::advanceAction(c, frame(c, bytes.data(), bytes.size(), 200), 220)); saved.check(c);
    }
}
void testObservationLimitsAndLateDelivery() {
    auto c = action(); acknowledge(c);
    for (unsigned i = 0; i < 3; ++i) observe(c, 0, 4);
    assert(c.state == ActionState::FAILED && c.outcome == ActionOutcome::OBSERVATION_LIMIT && c.polls == 3);
    assert(c.execution == ActionExecution::ACKNOWLEDGED && c.rawMotion == 4);
    auto lateAck = action(ActionKind::STOP, 215);
    assert(Ess::advanceAction(lateAck, frame(lateAck, NORMAL, sizeof(NORMAL), 200), 250));
    assert(lateAck.outcome == ActionOutcome::DEADLINE && lateAck.execution == ActionExecution::ACKNOWLEDGED);
    auto lateDelivery = action(ActionKind::STOP, 400); acknowledge(lateDelivery);
    auto bytes = motion(5, 8); // Stop observation does not require alarm-free readiness.
    assert(Ess::advanceAction(lateDelivery, frame(lateDelivery, bytes.data(), bytes.size(), 380), 500));
    assert(lateDelivery.state == ActionState::SUCCEEDED && lateDelivery.rawAlarm == 5);
    auto lateClosure = action(ActionKind::STOP, 400); acknowledge(lateClosure);
    assert(Ess::advanceAction(lateClosure, frame(lateClosure, bytes.data(), bytes.size(), 395), 500));
    assert(lateClosure.outcome == ActionOutcome::DEADLINE && !lateClosure.observationKnown);
    auto unqualified = action(); auto e = frame(unqualified, NORMAL, sizeof(NORMAL), 200);
    e.transport.qualified = false; e.transport.earliestUs = e.transport.latestUs = 0;
    assert(Ess::advanceAction(unqualified, e, 220));
    assert(unqualified.outcome == ActionOutcome::TIMING_UNQUALIFIED && unqualified.execution == ActionExecution::UNKNOWN);
    const uint64_t maximum = std::numeric_limits<uint64_t>::max();
    Ess::ActionContext boundary;
    assert(Ess::prepareEmergencyStop(boundary, target(), 12, maximum - 100, maximum - 1));
    assert(Ess::advanceAction(boundary, frame(boundary, DIRECT, sizeof(DIRECT), maximum - 90), maximum - 70));
    assert(boundary.eligibleUs == maximum - 1);
    Ess::PreparedAction p; assert(Ess::nextAction(boundary, maximum - 2, p) && p.kind == Ess::ActionWork::WAIT);
    const Saved<Ess::PreparedAction> saved(p);
    assert(!Ess::nextAction(boundary, maximum - 1, p)); saved.check(p);
    assert(Ess::advanceAction(boundary, event(boundary, ReadEventKind::DEADLINE), maximum - 1));
    assert(boundary.execution == ActionExecution::ACKNOWLEDGED && boundary.outcome == ActionOutcome::DEADLINE);
}
void testZeroOnlyDeviceClearNeedsQualifiedFreshReadback() {
    Ess::ActionContext c = action(); const Saved<Ess::ActionContext> saved(c);
    for (int64_t nonzero : {INT64_MIN, INT64_C(-1), INT64_C(1), INT64_MAX}) {
        assert(Ess::prepareSetDevicePosition(c, target(), 12, nonzero, true, 100, 10000).code == Err::UNSUPPORTED);
        saved.check(c);
    }
    assert(!Ess::prepareSetDevicePosition(c, target(), 12, 0, false, 100, 10000)); saved.check(c);
    ActionOptions options; options.pollIntervalUs = 100; options.maxPolls = 3;
    assert(MotorControlRS::prepareSetDevicePosition(c, target(), 12, 0, true, 100, 10000, options));
    Ess::PreparedAction work; assert(Ess::nextAction(c, 100, work));
    // The manual's auxiliary value is decimal49, not a position-register write.
    std::vector<uint8_t> expected = {1, 6, 0, 0x2D, 0, 0x31}; crc(expected);
    assert(work.reg == 0x2D && work.value == 49 && work.length == expected.size());
    assert(std::memcmp(work.bytes, expected.data(), expected.size()) == 0);
    acknowledge(c); assert(!c.observationKnown && c.completion == ActionCompletion::NOT_OBSERVED);
    assert(Ess::nextAction(c, c.eligibleUs, work) && !work.write && work.reg == 0xA && work.count == 2);
    observe(c, 0, 1); assert(c.state == ActionState::ACTIVE && c.rawPosition == 1);
    assert(c.rawAlarm == 0 && c.rawMotion == 0); // These words are position, not motor status.
    observe(c, 1, 0); assert(c.state == ActionState::ACTIVE && c.rawPosition == 0x10000);
    observe(c, 0, 0); assert(c.state == ActionState::SUCCEEDED && c.rawPosition == 0);
    assert(c.completion == ActionCompletion::OBSERVED && c.execution == ActionExecution::ACKNOWLEDGED);
    assert(c.lastObservation.step > c.writeEvidence.step && c.lastObservation.earliestUs > c.writeEvidence.latestUs);
    const Saved<Ess::ActionContext> completed(c);
    assert(!Ess::advanceAction(c, event(c, ReadEventKind::CANCEL), c.servicedUs)); completed.check(c);

    assert(Ess::prepareSetDevicePosition(c, target(), 13, 0, true, 100, 10000, options));
    auto lost = event(c, ReadEventKind::DEADLINE); lost.transport.txAccepted = 8; lost.txComplete = true;
    assert(Ess::advanceAction(c, lost, 10000));
    assert(c.execution == ActionExecution::UNKNOWN && !c.observationKnown && c.outcome == ActionOutcome::DEADLINE);
    assert(Ess::nextAction(c, c.servicedUs, work) && work.kind == Ess::ActionWork::DONE && !work.length);
    assert(Ess::prepareSetDevicePosition(c, target(), 14, 0, true, 100, 10000, options)); acknowledge(c);
    auto zero = motion(0, 0); auto stale = frame(c, zero.data(), zero.size(), c.writeEvidence.latestUs);
    const Saved<Ess::ActionContext> acknowledged(c);
    assert(!Ess::advanceAction(c, stale, c.eligibleUs + 20)); acknowledged.check(c);
    zero.back() ^= 1;
    assert(Ess::advanceAction(c, frame(c, zero.data(), zero.size(), c.eligibleUs), c.eligibleUs + 20));
    assert(c.state == ActionState::FAILED && c.status.code == Err::CRC_ERROR && !c.observationKnown);
    assert(c.execution == ActionExecution::ACKNOWLEDGED && c.completion == ActionCompletion::NOT_OBSERVED);
}
} // namespace
int main() {
    testOptedInObservationRetainsUnknownExecution(); testOptedInObservationPreservesWriteFailures();
    testExactCommandsAndCommonNativeParity(); testPolicyRejectionPreservesPreparedOperation();
    testEchoAcknowledgementAndReportedCompletionAreSeparate(); testInvalidEnvelopesAndDuplicatesDoNotMutate();
    testFailuresRetainUncertaintyAndNeverReplay(); testBadRepliesAndExceptionPreserveEvidence();
    testExceptionExecutionUsesOnlyDocumentedRejections();
    testObservationLimitsAndLateDelivery();
    testZeroOnlyDeviceClearNeedsQualifiedFreshReadback();
}
