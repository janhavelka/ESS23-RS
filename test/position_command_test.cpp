// SPDX-License-Identifier: MIT
#include <MotorControlRS/profiles/ess_rs/Position.h>
#include <cassert>
#include <cstring>
#include <vector>
using namespace MotorControlRS;
namespace E = MotorControlRS::ESS_RS;
namespace {
E::PositionCommand motor() {
    E::PositionCommand m;
    m.target.id = 1; m.target.address = 1; m.target.generation = 2;
    m.speedRpm = 60; m.accelerationTime = m.decelerationTime = 100;
    return m;
}
std::vector<uint8_t> crc(std::vector<uint8_t> bytes) {
    const auto c = E::calcCrc16(bytes.data(), bytes.size());
    bytes.push_back(static_cast<uint8_t>(c)); bytes.push_back(static_cast<uint8_t>(c >> 8));
    return bytes;
}
void reply(E::MoveContext& c, uint16_t flags = 4, bool wrongStage = false,
           bool confirmed = true, uint64_t closure = 0, uint64_t delivered = 0) {
    std::vector<uint8_t> b;
    if (c.step == 0) b = {1, 16, 0, 0x21, 0, static_cast<uint8_t>(wrongStage ? 6 : 5)};
    else if (c.step == 1) b = {1, 6, 0, 0x27, 0, static_cast<uint8_t>(c.request.position.relative ? 1 : 5)};
    else b = {1, 3, 4, 0, 0, static_cast<uint8_t>(flags >> 8), static_cast<uint8_t>(flags)};
    b = crc(b);
    ActionEvent e; auto& t = e.transport;
    t.target = c.target; t.operationId = c.operationId; t.step = c.step;
    t.kind = ReadEventKind::FRAME; t.frame = b.data(); t.length = b.size();
    t.txAccepted = c.step == 0 ? 19 : 8; t.qualified = e.txComplete = e.responseConfirmed = true;
    e.responseConfirmed = confirmed;
    t.earliestUs = c.eligibleUs + 1; t.latestUs = c.eligibleUs + 2;
    if (closure) t.earliestUs = t.latestUs = closure;
    assert(E::advanceMove(c, e, delivered ? delivered : t.latestUs + 1));
}
void intentSnapshotAndNormalSequence() {
    auto m = motor(); E::MoveContext c;
    assert(m.prepareRelative(c, 9, 100, 1000000000, 1001000000));
    assert(c.admission == E::MoveAdmission::NATIVE_INTENT);
    assert(!c.prerequisites.readinessQualified && !c.reference.nativeKnown && !c.prepared.displacementKnown);
    m.speedRpm = 50; m.accelerationTime = 200;
    E::PreparedMove work;
    assert(E::nextMove(c, c.startedUs, work));
    const uint8_t expected[] = {1,16,0,0x21,0,5,10,0,100,0,100,0,60,0,0,0,100,0xCE,0x33};
    assert(work.length == sizeof(expected) && !std::memcmp(work.bytes, expected, sizeof(expected)));
    assert(work.deadlineUs == c.deadlineUs);
    reply(c); assert(c.stagingApplied && c.step == 1);
    assert(E::nextMove(c, c.servicedUs, work) && work.function == 6 && work.value == 1);
    reply(c); assert(c.execution == ActionExecution::ACKNOWLEDGED && c.completion == ActionCompletion::NOT_OBSERVED);
    assert(c.state == ActionState::SUCCEEDED && c.outcome == ActionOutcome::ACKNOWLEDGED);
    assert(!c.runningObserved && !c.observationKnown);
    assert(E::nextMove(c, c.servicedUs, work) && work.kind == E::ActionWork::DONE);
    ActionEvent unrelated; unrelated.transport.target = c.target;
    unrelated.transport.operationId = c.operationId; unrelated.transport.step = c.step;
    assert(!E::advanceMove(c, unrelated, c.servicedUs + 1));
    assert(c.completion == ActionCompletion::NOT_OBSERVED);
    E::MoveContext next;
    assert(m.prepareRelative(next, 10, 200, c.servicedUs, c.servicedUs + 1000000));
    assert(E::nextMove(next, next.startedUs, work) && work.function == 16);
    assert(next.words[0] == 200 && next.words[2] == 50 && next.words[4] == 200);
    assert(c.words[0] == 100 && c.words[2] == 60 && c.words[4] == 100);
}
void rejectionAndRawAccess() {
    auto m = motor(); E::MoveContext out;
    for (unsigned fault = 0; fault < 8; ++fault) {
        auto bad = m;
        if (fault == 0) bad.target.address = 0;
        if (fault == 1) bad.target.generation = 0;
        if (fault == 2) bad.speedRpm = 0;
        if (fault == 3) bad.speedRpm = 3001;
        if (fault == 4) bad.accelerationTime = 2001;
        if (fault == 5) bad.decelerationTime = 2001;
        if (fault == 6) bad.wordOrder = static_cast<E::WordOrder>(2);
        unsigned char before[sizeof(out)]; std::memcpy(before, &out, sizeof(out));
        assert(!bad.prepareRelative(out, 1, fault == 7 ? 0 : 100, 10, 100));
        assert(!std::memcmp(before, &out, sizeof(out)));
    }
    assert(!m.prepareRelative(out, 0, 100, 10, 100));
    assert(!m.prepareRelative(out, 1, 100, 10, 10));
    for (auto order : {E::WordOrder::HIGH_WORD_FIRST, E::WordOrder::LOW_WORD_FIRST}) {
        m.wordOrder = order;
        assert(m.prepareAbsolute(out, 1, 0x12345678, 10, 1000000));
        assert(!out.prepared.displacementKnown && !out.reference.nativeKnown);
        assert(out.words[3] == (order == E::WordOrder::HIGH_WORD_FIRST ? 0x1234 : 0x5678));
        reply(out); E::PreparedMove work;
        assert(E::nextMove(out, out.servicedUs, work) && work.value == 5);
    }
    assert(m.prepareAbsolute(out, 1, 0, 10, 1000000));
    assert(m.prepareAbsolute(out, 1, UINT32_MAX, 10, 1000000)); // Raw bits, not negative-motion qualification.
    uint8_t bytes[8]; std::memset(bytes, 0xAA, sizeof(bytes));
    assert(!E::buildStartPosition(1, true, bytes, 7) && bytes[0] == 0xAA);
    assert(E::buildStartPosition(1, true, bytes, sizeof(bytes)) == 8);
    const uint8_t start[] = {1,6,0,0x27,0,1,0xF8,1};
    assert(!std::memcmp(bytes, start, sizeof(start)));
}
void failuresNeverStartOrReplay() {
    auto m = motor(); E::MoveContext c; E::PreparedMove work;
    assert(m.prepareRelative(c, 1, 100, 10, 1000000)); reply(c, 4, true);
    assert(c.state == ActionState::FAILED && c.uncertain && c.execution == ActionExecution::NOT_TRANSMITTED);
    assert(E::nextMove(c, c.servicedUs, work) && work.kind == E::ActionWork::DONE);
    for (unsigned phase = 0; phase < 2; ++phase) {
        assert(m.prepareRelative(c, 1, 100, 10, 1000000));
        if (phase) reply(c);
        ActionEvent e; e.transport.target = c.target; e.transport.operationId = c.operationId;
        e.transport.step = c.step; e.transport.kind = ReadEventKind::TRANSPORT_FAILURE;
        e.transport.txAccepted = phase ? 8 : 3;
        assert(E::advanceMove(c, e, c.servicedUs + 10));
        assert(c.state == ActionState::FAILED && c.uncertain);
        assert(E::nextMove(c, c.servicedUs, work) && work.kind == E::ActionWork::DONE);
    }
    assert(m.prepareRelative(c, 1, 100, 10, 100));
    assert(!E::nextMove(c, 100, work));
    ActionEvent expired; expired.transport.target = c.target;
    expired.transport.operationId = c.operationId; expired.transport.step = c.step;
    expired.transport.kind = ReadEventKind::DEADLINE;
    assert(E::advanceMove(c, expired, 100));
    assert(c.outcome == ActionOutcome::DEADLINE);
    assert(E::nextMove(c, 100, work) && work.kind == E::ActionWork::DONE);
}
void acknowledgementEvidence() {
    auto m = motor(); E::MoveContext c; E::PreparedMove work;
    assert(m.prepareRelative(c, 1, 100, 10, 1000)); reply(c);
    reply(c, 4, false, false);
    assert(c.state == ActionState::FAILED && c.uncertain);
    assert(c.outcome == ActionOutcome::UNCONFIRMED_RESPONSE && c.execution == ActionExecution::UNKNOWN);
    assert(c.completion == ActionCompletion::NOT_OBSERVED);
    assert(E::nextMove(c, c.servicedUs, work) && work.kind == E::ActionWork::DONE);
    assert(m.prepareRelative(c, 2, 100, 10, 1000)); reply(c);
    reply(c, 4, false, true, 999, 1100); // On-time closure, late task delivery of the final ACK.
    assert(c.outcome == ActionOutcome::ACKNOWLEDGED && c.completion == ActionCompletion::NOT_OBSERVED);
    assert(m.prepareRelative(c, 3, 100, 10, 1000)); reply(c);
    reply(c, 4, false, true, 1001, 1100);
    assert(c.outcome == ActionOutcome::DEADLINE && c.uncertain);
}
}
int main() { intentSnapshotAndNormalSequence(); rejectionAndRawAccess(); failuresNeverStartOrReplay(); acknowledgementEvidence(); }
