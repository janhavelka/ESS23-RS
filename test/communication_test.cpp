// SPDX-License-Identifier: MIT
#include <MotorControlRS/profiles/ess_rs/Communication.h>
#include <MotorControlRS/profiles/ess_rs/Registers.h>
#include <cassert>
#include <cstring>
#include <vector>

using namespace MotorControlRS;
namespace Ess = MotorControlRS::ESS_RS;
namespace {
template<class T> struct Saved {
    unsigned char bytes[sizeof(T)];
    explicit Saved(const T& value) { std::memcpy(bytes, &value, sizeof(value)); }
    void check(const T& value) const { assert(std::memcmp(bytes, &value, sizeof(value)) == 0); }
};
void crc(std::vector<uint8_t>& bytes) {
    uint16_t n = 0xFFFF;
    for (uint8_t b : bytes) { n ^= b; for (int i = 0; i < 8; ++i) n = (n & 1) ? (n >> 1) ^ 0xA001 : n >> 1; }
    bytes.push_back(static_cast<uint8_t>(n)); bytes.push_back(static_cast<uint8_t>(n >> 8));
}
std::vector<uint8_t> reply(uint8_t address, uint16_t word) {
    std::vector<uint8_t> b = {address, 3, 2, static_cast<uint8_t>(word >> 8), static_cast<uint8_t>(word)};
    crc(b); return b;
}
ReadTarget target() { ReadTarget t; t.id = 4; t.address = 1; t.generation = 7; return t; }
ActiveSerialTuple serial() {
    ActiveSerialTuple t; t.known = true; t.baud = 115200; t.dataBits = 8; t.parity = SerialParity::NONE; t.stopBits = 1; return t;
}
Ess::CommunicationRequest request(Ess::CommunicationField field = Ess::CommunicationField::BAUD) {
    Ess::CommunicationRequest r; r.field = field; r.address = 2; r.baud = Ess::BaudRateCode::BAUD_38400;
    r.format = Ess::SerialFormatCode::FORMAT_8E1; return r;
}
Ess::CommunicationPrerequisites prerequisites(const Ess::CommunicationRequest& r) {
    Ess::CommunicationPrerequisites p; p.previous.target = target(); p.previous.operationId = 23;
    p.beforeSerial = p.previous.activeSerial = serial(); p.maxAgeUs = 10000;
    p.stationaryQualified = p.effectsQualified = p.routeBackQualified = p.addressDipOffQualified = true;
    p.qualifiedRequest = r;
    std::vector<uint8_t> b = {1, 3, 6, 0, 0, 0, 0, 0, 0}; crc(b);
    auto& e = p.previous.provenance[1]; e.first = Ess::Registers::CUSTOM_NODE; e.count = 3;
    e.length = e.receivedLength = b.size(); e.txAccepted = 8;
    e.qualified = true; e.attemptedUs = 10; e.earliestUs = 20; e.latestUs = 30; e.deliveredUs = 40;
    std::memcpy(e.raw, b.data(), b.size());
    return p;
}
Ess::CommunicationContext operation(Ess::CommunicationField field = Ess::CommunicationField::BAUD) {
    Ess::CommunicationContext c; const auto r = request(field); const auto p = prerequisites(r);
    assert(Ess::prepareCommunication(c, target(), 9, r, p, 100, 1000)); return c;
}
ActionEvent event(const Ess::CommunicationContext& c, ReadEventKind kind) {
    ActionEvent e; e.transport.kind = kind; e.transport.target = c.step == 0 ? c.beforeTarget : c.confirmationTarget;
    e.transport.operationId = c.operationId; e.transport.step = c.step; return e;
}
ActionEvent frame(const Ess::CommunicationContext& c, const uint8_t* b, std::size_t n, uint64_t at = 200) {
    ActionEvent e = event(c, ReadEventKind::FRAME); e.transport.frame = b; e.transport.length = n;
    e.transport.txAccepted = 8; e.txComplete = e.responseConfirmed = e.transport.qualified = true;
    e.transport.earliestUs = at; e.transport.latestUs = at + 10; return e;
}
void ack(Ess::CommunicationContext& c, uint64_t delivered = 220) {
    Ess::PreparedCommunication work; assert(Ess::nextCommunication(c, 100, work));
    assert(Ess::advanceCommunication(c, frame(c, work.bytes, work.length), delivered));
}
void confirm(Ess::CommunicationContext& c, bool requested, uint16_t value) {
    assert(Ess::prepareCommunicationConfirmation(c, requested ? c.requestedTarget : c.beforeTarget,
        requested ? c.requestedSerial : c.beforeSerial, 1200, 2000));
    Ess::PreparedCommunication work; assert(Ess::nextCommunication(c, 1200, work));
    assert(!work.write && work.count == 1 && work.bytes[1] == 3);
    const auto b = reply(work.target.address, value);
    assert(Ess::advanceCommunication(c, frame(c, b.data(), b.size(), 1300), 1320));
}
void preparation() {
    for (auto f : {Ess::CommunicationField::ADDRESS, Ess::CommunicationField::BAUD, Ess::CommunicationField::FORMAT}) {
        const auto c = operation(f); Ess::PreparedCommunication work;
        assert(Ess::nextCommunication(c, 100, work));
        assert(work.write && work.reg == 0x13 + static_cast<unsigned>(f) && work.bytes[1] == 6);
        assert(work.target.address == 1 && work.serial.baud == 115200);
        if (f == Ess::CommunicationField::ADDRESS) {
            assert(c.requestedTarget.address == 2 && c.save == Ess::CommunicationRequirement::REQUIRED);
            assert(c.restart == Ess::CommunicationRequirement::UNRESOLVED);
        } else {
            assert(c.restart == Ess::CommunicationRequirement::REQUIRED && c.save == Ess::CommunicationRequirement::UNRESOLVED);
            if (f == Ess::CommunicationField::BAUD) assert(c.requestedSerial.baud == 38400);
            else assert(c.requestedSerial.parity == SerialParity::EVEN && c.requestedSerial.stopBits == 1);
        }
        assert(c.activationUnknown && !c.observedActiveKnown && !c.effects);
    }
    auto c = operation(); const Saved<Ess::CommunicationContext> saved(c);
    auto r = request(); auto p = prerequisites(r);
    p.routeBackQualified = false;
    assert(!Ess::prepareCommunication(c, target(), 10, r, p, 100, 1000)); saved.check(c);
    p = prerequisites(r); p.previous.raw.baud = 2;
    assert(!Ess::prepareCommunication(c, target(), 10, r, p, 100, 1000)); saved.check(c);
    p = prerequisites(r); p.previous.target.generation++;
    assert(!Ess::prepareCommunication(c, target(), 10, r, p, 100, 1000)); saved.check(c);
    p = prerequisites(r); p.previous.provenance[1].deliveredUs = 29;
    assert(!Ess::prepareCommunication(c, target(), 10, r, p, 100, 1000)); saved.check(c);
    p = prerequisites(r); p.beforeSerial.baud = 38400;
    assert(!Ess::prepareCommunication(c, target(), 10, r, p, 100, 1000)); saved.check(c);
    for (unsigned pending : {1u, 2u}) {
        p = prerequisites(r);
        std::vector<uint8_t> baseline = {1, 3, 6, 0, 0, 0, 0, 0, 0};
        baseline[pending == 1 ? 6 : 8] = 1; crc(baseline);
        std::memcpy(p.previous.provenance[1].raw, baseline.data(), baseline.size());
        if (pending == 1) p.previous.raw.baud = 1; else p.previous.raw.format = 1;
        const Status status = Ess::prepareCommunication(c, target(), 10, r, p, 100, 1000);
        assert(!status && status.detail == static_cast<int32_t>(Ess::CommunicationError::PENDING_BASELINE)); saved.check(c);
    }
    p = prerequisites(r);
    assert(!Ess::prepareCommunication(c, target(), 10, r, p, 10010, 11000)); saved.check(c);
    r = request(Ess::CommunicationField::ADDRESS); p = prerequisites(r); p.addressDipOffQualified = false;
    assert(!Ess::prepareCommunication(c, target(), 10, r, p, 100, 1000)); saved.check(c);
    for (uint16_t address : {uint16_t(0), uint16_t(248), uint16_t(255), uint16_t(256)}) {
        r.address = address; p = prerequisites(r);
        assert(!Ess::prepareCommunication(c, target(), 10, r, p, 100, 1000)); saved.check(c);
    }
    r = request(); p = prerequisites(r); p.maxAgeUs = 500;
    assert(Ess::prepareCommunication(c, target(), 10, r, p, 100, 1000)); assert(c.deadlineUs == 510);
}
void oldAndNewContexts() {
    auto c = operation(); ack(c);
    assert(c.execution == ActionExecution::ACKNOWLEDGED && c.effects && c.uncertain);
    assert(c.writeOutcome == Ess::CommunicationOutcome::ACKNOWLEDGED);
    confirm(c, false, 1);
    assert(c.outcome == Ess::CommunicationOutcome::CONFIRMED && !c.uncertain && c.readbackKnown && c.readback == 1);
    assert(c.observedActiveKnown && c.observedActiveSerial.baud == 115200 && c.requestedSerial.baud == 38400);
    assert(c.activationUnknown && c.restart == Ess::CommunicationRequirement::REQUIRED);
    assert(c.writeOutcome == Ess::CommunicationOutcome::ACKNOWLEDGED);
    c = operation(Ess::CommunicationField::ADDRESS); ack(c);
    assert(c.execution == ActionExecution::UNKNOWN && c.outcome == Ess::CommunicationOutcome::ADDRESS_ACK_UNRESOLVED);
    confirm(c, true, 2);
    assert(c.observedActiveTarget.address == 2 && c.beforeTarget.address == 1 && c.activationUnknown);
    assert(c.execution == ActionExecution::UNKNOWN && c.save == Ess::CommunicationRequirement::REQUIRED);
    assert(c.writeEvidence.wire.status && c.writeEvidence.target.address == 1);
    c = operation(Ess::CommunicationField::ADDRESS);
    Ess::PreparedCommunication work; assert(Ess::nextCommunication(c, 100, work));
    std::vector<uint8_t> b(work.bytes, work.bytes + 6); b[0] = 2; crc(b);
    assert(Ess::advanceCommunication(c, frame(c, b.data(), b.size()), 220));
    assert(c.outcome == Ess::CommunicationOutcome::REPLY_ERROR && c.execution == ActionExecution::UNKNOWN);
    assert(!c.observedActiveKnown && c.effects && c.uncertain);
}
void lostAndDelayed() {
    auto c = operation(); auto e = event(c, ReadEventKind::TRANSPORT_FAILURE);
    e.transport.txAccepted = 8; e.txComplete = true; e.transport.transportDetail = 73;
    assert(Ess::advanceCommunication(c, e, 1000));
    assert(c.execution == ActionExecution::UNKNOWN && c.effects && c.uncertain);
    const Saved<Ess::CommunicationEvidence> evidence(c.writeEvidence);
    assert(Ess::prepareCommunicationConfirmation(c, c.beforeTarget, c.beforeSerial, 1100, 1500));
    e = event(c, ReadEventKind::TRANSPORT_FAILURE); e.transport.txAccepted = 8; e.txComplete = true;
    assert(Ess::advanceCommunication(c, e, 1500));
    assert(c.uncertain && !c.observedActiveKnown && c.confirmations == 1);
    assert(Ess::prepareCommunicationConfirmation(c, c.requestedTarget, c.requestedSerial, 1600, 2000));
    auto b = reply(1, 1);
    assert(Ess::advanceCommunication(c, frame(c, b.data(), b.size(), 1700), 2100));
    assert(c.outcome == Ess::CommunicationOutcome::CONFIRMED && c.observedActiveSerial.baud == 38400);
    evidence.check(c.writeEvidence);
    assert(c.confirmationEvidence[0].wire.event == ReadEventKind::TRANSPORT_FAILURE);
    assert(c.confirmationEvidence[1].readbackKnown);
    assert(c.writeEvidence.deadlineUs == 1000 && c.confirmationEvidence[0].deadlineUs == 1500 &&
        c.confirmationEvidence[1].deadlineUs == 2000);
    const Saved<Ess::CommunicationContext> saved(c);
    assert(!Ess::prepareCommunicationConfirmation(c, c.beforeTarget, c.beforeSerial, 2200, 2500)); saved.check(c);
    c = operation(); ack(c, 1200);
    assert(c.outcome == Ess::CommunicationOutcome::ACKNOWLEDGED); // On-time closure, delayed delivery.
    Ess::PreparedCommunication done; assert(Ess::nextCommunication(c, 1200, done)); assert(done.kind == Ess::ActionWork::DONE);
    c = operation(); Ess::PreparedCommunication work; assert(Ess::nextCommunication(c, 100, work));
    assert(Ess::advanceCommunication(c, frame(c, work.bytes, work.length, 1001), 1020));
    assert(c.outcome == Ess::CommunicationOutcome::DEADLINE && c.effects);
    assert(c.execution == ActionExecution::UNKNOWN);
    c = operation(); b = {1, 0x86, 3}; crc(b);
    assert(Ess::advanceCommunication(c, frame(c, b.data(), b.size(), 1001), 1020));
    assert(c.outcome == Ess::CommunicationOutcome::DEADLINE && c.effects && c.uncertain);
    assert(c.execution == ActionExecution::UNKNOWN);
}
void rejectionAndUncertainty() {
    auto c = operation(); std::vector<uint8_t> b = {1, 0x86, 3}; crc(b);
    assert(Ess::advanceCommunication(c, frame(c, b.data(), b.size()), 220));
    assert(c.execution == ActionExecution::REJECTED && c.effects && !c.uncertain);
    c = operation(); b = {1, 0x86, 0x80}; crc(b);
    assert(Ess::advanceCommunication(c, frame(c, b.data(), b.size()), 220));
    assert(c.execution == ActionExecution::UNKNOWN && c.effects && c.uncertain);
    c = operation(); b = {1, 0x86, 5}; crc(b);
    assert(Ess::advanceCommunication(c, frame(c, b.data(), b.size()), 220));
    assert(c.execution == ActionExecution::UNKNOWN && c.uncertain); // Read-count error cannot confirm FC06 rejection.
    c = operation(); Ess::PreparedCommunication work; assert(Ess::nextCommunication(c, 100, work));
    auto e = frame(c, work.bytes, work.length); e.responseConfirmed = false;
    assert(Ess::advanceCommunication(c, e, 220));
    assert(c.outcome == Ess::CommunicationOutcome::UNCONFIRMED_RESPONSE && c.execution == ActionExecution::UNKNOWN);
    confirm(c, false, 0);
    assert(c.outcome == Ess::CommunicationOutcome::READBACK_MISMATCH && c.uncertain && c.observedActiveKnown);
    c = operation(); assert(Ess::nextCommunication(c, 100, work));
    e = frame(c, work.bytes, work.length); e.transport.executionUnknown = true;
    assert(Ess::advanceCommunication(c, e, 220));
    assert(c.execution == ActionExecution::UNKNOWN && c.outcome == Ess::CommunicationOutcome::UNCONFIRMED_RESPONSE);
    assert(Ess::prepareCommunicationConfirmation(c, c.beforeTarget, c.beforeSerial, 300, 500));
    b = reply(1, 1); e = frame(c, b.data(), b.size(), 350); e.transport.executionUnknown = true;
    assert(Ess::advanceCommunication(c, e, 370));
    assert(c.outcome == Ess::CommunicationOutcome::UNCONFIRMED_RESPONSE && !c.readbackKnown && !c.observedActiveKnown && c.uncertain);
    c = operation(); e = event(c, ReadEventKind::CANCEL); e.transport.txAccepted = 3;
    assert(Ess::advanceCommunication(c, e, 120)); assert(c.effects && c.execution == ActionExecution::UNKNOWN);
    c = operation(); e = event(c, ReadEventKind::CANCEL);
    assert(Ess::advanceCommunication(c, e, 120)); assert(!c.effects && c.execution == ActionExecution::NOT_TRANSMITTED);
}
void immutableEnvelopes() {
    auto c = operation(); const Saved<Ess::CommunicationContext> saved(c);
    Ess::PreparedCommunication work; assert(Ess::nextCommunication(c, 100, work));
    auto e = frame(c, work.bytes, work.length); e.transport.target.generation++;
    assert(!Ess::advanceCommunication(c, e, 220)); saved.check(c);
    e = frame(c, work.bytes, work.length); e.transport.step++;
    assert(!Ess::advanceCommunication(c, e, 220)); saved.check(c);
    e = frame(c, work.bytes, work.length); e.txComplete = false;
    assert(!Ess::advanceCommunication(c, e, 220)); saved.check(c);
    e = frame(c, work.bytes, work.length); e.transport.latestUs = 230;
    assert(!Ess::advanceCommunication(c, e, 220)); saved.check(c);
    e = event(c, ReadEventKind::DEADLINE);
    assert(!Ess::advanceCommunication(c, e, 999)); saved.check(c);
    ack(c); const Saved<Ess::CommunicationContext> terminal(c);
    ReadTarget wrong = c.beforeTarget; wrong.generation++;
    assert(!Ess::prepareCommunicationConfirmation(c, wrong, c.beforeSerial, 300, 500)); terminal.check(c);
    auto tuple = c.beforeSerial; tuple.parity = SerialParity::ODD;
    assert(!Ess::prepareCommunicationConfirmation(c, c.beforeTarget, tuple, 300, 500)); terminal.check(c);
    assert(Ess::prepareCommunicationConfirmation(c, c.beforeTarget, c.beforeSerial, 300, 500));
    const Saved<Ess::CommunicationContext> pending(c);
    e = frame(c, work.bytes, work.length, 350); e.transport.step = 0;
    assert(!Ess::advanceCommunication(c, e, 370)); pending.check(c);
}
}
int main() {
    preparation(); oldAndNewContexts(); lostAndDelayed(); rejectionAndUncertainty(); immutableEnvelopes();
}
