// SPDX-License-Identifier: MIT
#include <MotorControlRS/profiles/ess_rs/Persistence.h>
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
ReadTarget target() { ReadTarget t; t.id = 4; t.address = 1; t.generation = 7; return t; }
ActiveSerialTuple serial() {
    ActiveSerialTuple t; t.known = true; t.baud = 115200; t.dataBits = 8;
    t.parity = SerialParity::NONE; t.stopBits = 1; return t;
}
void crc(std::vector<uint8_t>& bytes) {
    uint16_t n = 0xFFFF;
    for (uint8_t b : bytes) { n ^= b; for (int i = 0; i < 8; ++i) n = (n & 1) ? (n >> 1) ^ 0xA001 : n >> 1; }
    bytes.push_back(static_cast<uint8_t>(n)); bytes.push_back(static_cast<uint8_t>(n >> 8));
}
std::vector<uint8_t> reply(uint8_t address, const uint16_t* words, std::size_t count) {
    std::vector<uint8_t> b = {address, 3, static_cast<uint8_t>(2 * count)};
    for (std::size_t i = 0; i < count; ++i) { b.push_back(static_cast<uint8_t>(words[i] >> 8)); b.push_back(static_cast<uint8_t>(words[i])); }
    crc(b); return b;
}
void readStep(Ess::ReadContext& c, const uint16_t* words, uint64_t& now) {
    Ess::PreparedRead work; assert(Ess::nextRead(c, now, work));
    const auto b = reply(work.target.address, words, work.count);
    ReadEvent e; e.kind = ReadEventKind::FRAME; e.target = work.target; e.operationId = work.operationId; e.step = work.step;
    e.frame = b.data(); e.length = b.size(); e.txAccepted = 8; e.qualified = true;
    e.earliestUs = now + 2; e.latestUs = now + 5; now += 10;
    assert(Ess::advanceRead(c, e, now));
}
Ess::PersistencePrerequisites prerequisites(uint64_t start = 10, uint16_t motion = 1, uint16_t direction = 0) {
    Ess::PersistencePrerequisites p; p.beforeSerial = serial();
    p.configurationGeneration = 9; p.maxAgeUs = 10000; p.stationaryQualified = p.effectsQualified = true;
    const uint16_t identity[] = {0x0305, 0x0102, 1, 0};
    const uint16_t words[] = {direction, 400, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 4000};
    Ess::ReadContext c; uint64_t now = start;
    assert(Ess::prepareIdentity(c, target(), 21, now, now + 1000, serial()));
    readStep(c, identity, now); assert(Ess::getIdentity(c, p.beforeIdentity));
    assert(Ess::prepareConfig(c, target(), 22, now, now + 1000, serial()));
    uint8_t offset = 0;
    for (uint8_t count : {uint8_t(2), uint8_t(3), uint8_t(3), uint8_t(5), uint8_t(2)}) {
        readStep(c, words + offset, now); offset = static_cast<uint8_t>(offset + count);
    }
    assert(Ess::getConfig(c, p.beforeConfig));
    assert(Ess::prepareState(c, target(), 23, now, now + 1000, serial(), &p.beforeConfig));
    const uint16_t state[] = {0, motion}; readStep(c, state, now);
    assert(Ess::getStateBlock(c, 0, p.stationary)); // Production partial-state publication, no fabricated observation.
    return p;
}
Ess::PersistenceContext operation(Ess::PersistenceKind kind = Ess::PersistenceKind::SAVE) {
    Ess::PersistenceContext c; auto p = prerequisites(); p.qualifiedKind = kind;
    p.completeBackupQualified = p.routeBackQualified = true; p.backupSourceId = 77;
    const auto s = kind == Ess::PersistenceKind::SAVE ? Ess::prepareSave(c, target(), 9, p, 1000, 2000) :
        Ess::prepareFactoryRestore(c, target(), 9, p, 1000, 2000);
    assert(s); return c;
}
ActionEvent event(const Ess::PersistenceContext& c, ReadEventKind kind) {
    ActionEvent e; e.transport.kind = kind; e.transport.target = c.target; e.transport.operationId = c.operationId; return e;
}
ActionEvent frame(const Ess::PersistenceContext& c, const uint8_t* bytes, std::size_t size, uint64_t at = 1100) {
    auto e = event(c, ReadEventKind::FRAME); e.transport.frame = bytes; e.transport.length = size;
    e.transport.txAccepted = 8; e.transport.qualified = e.txComplete = e.responseConfirmed = true;
    e.transport.earliestUs = at; e.transport.latestUs = at + 10; return e;
}
void ack(Ess::PersistenceContext& c, bool confirmed = true, uint64_t delivered = 1120) {
    Ess::PreparedPersistence work; assert(Ess::nextPersistence(c, 1000, work));
    auto e = frame(c, work.bytes, work.length); e.responseConfirmed = confirmed;
    assert(Ess::advancePersistence(c, e, delivered));
}
Ess::PersistenceVerification verification(uint64_t start = 1200, uint16_t direction = 0) {
    const auto p = prerequisites(start, 1, direction);
    Ess::PersistenceVerification v; v.config = p.beforeConfig; v.identity = p.beforeIdentity;
    v.stationary = p.stationary; v.serial = p.beforeSerial;
    v.configurationGeneration = 10; v.maxAgeUs = p.maxAgeUs; return v;
}
void preparation() {
    for (auto kind : {Ess::PersistenceKind::SAVE, Ess::PersistenceKind::FACTORY_RESTORE}) {
        const auto c = operation(kind); Ess::PreparedPersistence work;
        assert(Ess::nextPersistence(c, 1000, work));
        const uint8_t save[] = {1, 6, 0, 0x2D, 0, 0x42, 0x99, 0xF2};
        const uint8_t restore[] = {1, 6, 0, 0x2D, 0, 0x41, 0xD9, 0xF3};
        assert(work.length == 8 && std::memcmp(work.bytes, kind == Ess::PersistenceKind::SAVE ? save : restore, 8) == 0);
        assert(work.write && work.reg == 0x2D && work.step == 0 && work.serial.known);
        assert(c.persistence == Ess::PersistenceKnowledge::UNVERIFIED && c.restartRequiredForProof);
        assert(!c.configurationSnapshotComplete && !c.liveReadbackKnown && !c.effects);
        assert(c.fields[0].sourceAccess && c.fields[0].before == 0 && c.fields[14].reg == 0x101);
        Ess::PreparedPersistence again; assert(Ess::nextPersistence(c, 1000, again));
        assert(std::memcmp(work.bytes, again.bytes, 8) == 0 && again.operationId == work.operationId);
    }
    auto c = operation(); const Saved<Ess::PersistenceContext> saved(c); auto p = prerequisites();
    for (uint16_t motion : {uint16_t(4), uint16_t(8), uint16_t(0x80)}) {
        p = prerequisites(10, motion);
        assert(!Ess::prepareSave(c, target(), 10, p, 1000, 2000)); saved.check(c);
    }
    p = prerequisites(); p.stationaryQualified = false;
    assert(!Ess::prepareSave(c, target(), 10, p, 1000, 2000)); saved.check(c);
    p = prerequisites(); p.beforeConfig.raw.baud = 1;
    assert(!Ess::prepareSave(c, target(), 10, p, 1000, 2000)); saved.check(c);
    p = prerequisites(); p.beforeConfig.provenance[4].raw[3] ^= 1;
    assert(!Ess::prepareSave(c, target(), 10, p, 1000, 2000)); saved.check(c);
    p = prerequisites(); p.beforeIdentity.target.generation++;
    assert(!Ess::prepareSave(c, target(), 10, p, 1000, 2000)); saved.check(c);
    p = prerequisites(); p.beforeSerial.known = false;
    assert(!Ess::prepareSave(c, target(), 10, p, 1000, 2000)); saved.check(c);
    p = prerequisites(); p.maxAgeUs = 100;
    assert(!Ess::prepareSave(c, target(), 10, p, 1000, 2000)); saved.check(c);
    p = prerequisites(); p.qualifiedKind = Ess::PersistenceKind::FACTORY_RESTORE;
    auto s = Ess::prepareFactoryRestore(c, target(), 10, p, 1000, 2000);
    assert(!s && s.detail == static_cast<int32_t>(Ess::PersistenceError::BACKUP_REQUIRED)); saved.check(c);
    p.completeBackupQualified = true; p.backupSourceId = 2;
    s = Ess::prepareFactoryRestore(c, target(), 10, p, 1000, 2000);
    assert(!s && s.detail == static_cast<int32_t>(Ess::PersistenceError::ROUTE_BACK_REQUIRED)); saved.check(c);
    assert(!Ess::prepareSave(c, target(), 10, c.before, 1000, 2000)); saved.check(c);
    p = prerequisites(10, 1, 0xFFFF); assert(Ess::prepareSave(c, target(), 10, p, 1000, 2000));
    assert(c.before.beforeConfig.raw.direction == 0xFFFF && !c.before.beforeConfig.directionKnown); // Partial semantic knowledge allowed.
    p = prerequisites(); p.maxAgeUs = 1500;
    assert(Ess::prepareSave(c, target(), 10, p, 1000, 3000)); assert(c.deadlineUs == 1510);
    Ess::PreparedPersistence noTraffic; const Saved<Ess::PreparedPersistence> untouched(noTraffic);
    assert(!Ess::nextPersistence(c, 1510, noTraffic)); untouched.check(noTraffic);
}
void acknowledgementsAndLostReplies() {
    auto c = operation(); ack(c);
    assert(c.execution == ActionExecution::ACKNOWLEDGED && c.outcome == Ess::PersistenceOutcome::ACKNOWLEDGED);
    assert(c.effects && !c.uncertain && c.persistence == Ess::PersistenceKnowledge::UNVERIFIED);
    assert(c.configurationInvalidated && c.restartRequiredForProof);
    Ess::PreparedPersistence done; assert(Ess::nextPersistence(c, 2200, done));
    assert(done.kind == Ess::ActionWork::DONE && !done.length && !done.write);
    const Saved<Ess::PersistenceContext> saved(c);
    assert(!Ess::advancePersistence(c, event(c, ReadEventKind::DEADLINE), 2200)); saved.check(c);
    c = operation(); ack(c, false);
    assert(c.execution == ActionExecution::UNKNOWN && c.uncertain && c.outcome == Ess::PersistenceOutcome::UNCONFIRMED_RESPONSE);
    c = operation(Ess::PersistenceKind::FACTORY_RESTORE);
    auto e = event(c, ReadEventKind::TRANSPORT_FAILURE); e.transport.txAccepted = 8; e.txComplete = true; e.transport.transportDetail = 73;
    assert(Ess::advancePersistence(c, e, 2000));
    assert(c.uncertain && c.effects && c.manualInterventionRequired && c.configurationInvalidated);
    assert(c.writeEvidence.transportDetail == 73);
    assert(Ess::nextPersistence(c, 2000, done) && done.kind == Ess::ActionWork::DONE);
    c = operation(); e = event(c, ReadEventKind::CANCEL); assert(Ess::advancePersistence(c, e, 1010));
    assert(!c.effects && !c.uncertain && c.execution == ActionExecution::NOT_TRANSMITTED);
    c = operation(); e = event(c, ReadEventKind::CANCEL); e.transport.txAccepted = 3;
    assert(Ess::advancePersistence(c, e, 1010)); assert(c.effects && c.uncertain);
    c = operation(); ack(c, true, 2300); // On-time closure can be delivered late.
    assert(c.execution == ActionExecution::ACKNOWLEDGED);
    c = operation(); Ess::PreparedPersistence work; assert(Ess::nextPersistence(c, 1000, work));
    assert(Ess::advancePersistence(c, frame(c, work.bytes, work.length, 2001), 2020));
    assert(c.outcome == Ess::PersistenceOutcome::DEADLINE && c.uncertain);
    for (uint8_t code : {uint8_t(3), uint8_t(5), uint8_t(0x80)}) {
        c = operation(); std::vector<uint8_t> b = {1, 0x86, code}; crc(b);
        assert(Ess::advancePersistence(c, frame(c, b.data(), b.size()), 1120));
        assert(c.execution == (code == 3 ? ActionExecution::REJECTED : ActionExecution::UNKNOWN));
        assert(c.effects && c.persistence == Ess::PersistenceKnowledge::UNVERIFIED);
    }
}
void invalidEnvelopes() {
    auto c = operation(); Ess::PreparedPersistence w; assert(Ess::nextPersistence(c, 1000, w));
    const Saved<Ess::PersistenceContext> saved(c);
    auto e = frame(c, w.bytes, w.length); e.transport.operationId++;
    assert(!Ess::advancePersistence(c, e, 1120)); saved.check(c);
    e = frame(c, w.bytes, w.length); e.transport.target.generation++;
    assert(!Ess::advancePersistence(c, e, 1120)); saved.check(c);
    e = frame(c, w.bytes, w.length); e.transport.step = 1;
    assert(!Ess::advancePersistence(c, e, 1120)); saved.check(c);
    e = frame(c, w.bytes, w.length); e.txComplete = false;
    assert(!Ess::advancePersistence(c, e, 1120)); saved.check(c);
    e = frame(c, w.bytes, w.length); e.transport.earliestUs = 999;
    assert(!Ess::advancePersistence(c, e, 1120)); saved.check(c);
    e = event(c, ReadEventKind::DEADLINE);
    assert(!Ess::advancePersistence(c, e, 1100)); saved.check(c);
    e = event(c, ReadEventKind::CANCEL); e.responseConfirmed = true;
    assert(!Ess::advancePersistence(c, e, 1100)); saved.check(c);
    std::vector<uint8_t> wrong(w.bytes, w.bytes + 6); wrong[5] = 0x41; crc(wrong);
    assert(Ess::advancePersistence(c, frame(c, wrong.data(), wrong.size()), 1120));
    assert(c.uncertain && c.outcome == Ess::PersistenceOutcome::REPLY_ERROR);
}
void liveAndRestartVerification() {
    auto c = operation(); ack(c); const Saved<Ess::ActionEvidence> write(c.writeEvidence);
    auto v = verification(); assert(Ess::verifyPersistence(c, v, 1500));
    assert(c.liveReadbackKnown && c.matchingFields == 0x7FFF && !c.verifiedFields);
    assert(c.persistence == Ess::PersistenceKnowledge::UNVERIFIED && c.restartRequiredForProof);
    assert(c.outcome == Ess::PersistenceOutcome::ACKNOWLEDGED); write.check(c.writeEvidence);
    v = verification(1600, 1); v.restartObserved = true; v.restartUs = 1550; v.restartSourceId = 48;
    assert(Ess::verifyPersistence(c, v, 1800));
    assert(c.persistence == Ess::PersistenceKnowledge::VERIFIED_FIELDS && c.verifiedFields == 0x7FFE);
    assert(!c.fields[0].survivedRestart && c.fields[1].survivedRestart && !c.configurationSnapshotComplete);
    assert(!c.restartRequiredForProof && c.verificationCount == 2); write.check(c.writeEvidence);
    const Saved<Ess::PersistenceContext> saved(c);
    assert(!Ess::verifyPersistence(c, v, 1900)); saved.check(c);
    c = operation(); ack(c, false); v = verification();
    assert(Ess::verifyPersistence(c, v, 1500));
    assert(c.uncertain && c.execution == ActionExecution::UNKNOWN && c.liveReadbackKnown && !c.verifiedFields);
    c = operation(Ess::PersistenceKind::FACTORY_RESTORE); ack(c); v = verification();
    assert(Ess::verifyPersistence(c, v, 1500));
    assert(c.manualInterventionRequired && c.persistence == Ess::PersistenceKnowledge::UNVERIFIED);
    c = operation(); ack(c); v = verification();
    // Stored settings can change while the same responding interface remains.
    v.config.raw.baud = 1;
    auto& p = v.config.provenance[1]; const uint16_t comm[] = {0, 1, 0}; const auto b = reply(1, comm, 3);
    std::memcpy(p.raw, b.data(), b.size());
    assert(Ess::verifyPersistence(c, v, 1500)); assert(c.communicationChanged && !(c.matchingFields & (1u << 3)));
}
void invalidVerification() {
    auto c = operation(); ack(c); const Saved<Ess::PersistenceContext> saved(c);
    auto v = verification(); v.identity.target.address = 2;
    assert(!Ess::verifyPersistence(c, v, 1500)); saved.check(c);
    v = verification(); v.configurationGeneration = 8;
    assert(!Ess::verifyPersistence(c, v, 1500)); saved.check(c);
    v = verification(); v.serial.baud = 38400;
    assert(!Ess::verifyPersistence(c, v, 1500)); saved.check(c);
    v = verification(10);
    assert(!Ess::verifyPersistence(c, v, 1500)); saved.check(c); // Pre-write observations cannot settle an action.
    v = verification(); v.restartObserved = true;
    assert(!Ess::verifyPersistence(c, v, 1500)); saved.check(c);
    v.restartUs = 1150; v.restartSourceId = 17;
    assert(Ess::verifyPersistence(c, v, 1500));
    c = operation(); ack(c); const Saved<Ess::PersistenceContext> second(c);
    v = verification(); v.restartUs = 1150; v.restartSourceId = 17;
    assert(!Ess::verifyPersistence(c, v, 1500)); second.check(c);
    v = verification(); v.restartObserved = true; v.restartUs = 1300; v.restartSourceId = 17;
    assert(!Ess::verifyPersistence(c, v, 1500)); second.check(c); // Every source must follow actual restart.
    v = verification(); v.stationary = prerequisites(1200, 4).stationary;
    assert(!Ess::verifyPersistence(c, v, 1500)); second.check(c);
    assert(!Ess::verifyPersistence(c, c.verification, 1500)); second.check(c);
}
} // namespace
static void testOptionalAgePolicy() {
    const uint64_t now = 100000, deadline = 110000;
    auto p = prerequisites(); p.maxAgeUs = 0;
    Ess::PersistenceContext c; assert(Ess::prepareSave(c, target(), 9, p, now, deadline));
    Ess::PreparedPersistence work; assert(Ess::nextPersistence(c, now, work) && work.deadlineUs == deadline);
    const Saved<Ess::PersistenceContext> saved(c);
    p.maxAgeUs = 10000; assert(!(Ess::prepareSave(c, target(), 9, p, now, deadline))); saved.check(c);
    p.maxAgeUs = 0; p.beforeIdentity.target.generation++;
    assert(!(Ess::prepareSave(c, target(), 9, p, now, deadline))); saved.check(c); p.beforeIdentity.target.generation--;
    const auto id = p.beforeIdentity.operationId; p.beforeIdentity.operationId = 0;
    assert(!(Ess::prepareSave(c, target(), 9, p, now, deadline))); saved.check(c); p.beforeIdentity.operationId = id;
    p.beforeIdentity.provenance.deliveredUs = now + 1;
    assert(!(Ess::prepareSave(c, target(), 9, p, now, deadline))); saved.check(c);
    assert(Ess::advancePersistence(c, event(c, ReadEventKind::DEADLINE), deadline));
    assert(c.state == ReadState::FAILED && c.outcome == Ess::PersistenceOutcome::DEADLINE);
    assert(Ess::nextPersistence(c, deadline, work) && work.kind == Ess::ActionWork::DONE && !work.length);
    // Disabling age does not allow pre-write observations to prove persistence.
    c = operation(); ack(c);
    auto v = verification(10); v.maxAgeUs = 0;
    const Saved<Ess::PersistenceContext> terminal(c);
    assert(!Ess::verifyPersistence(c, v, now)); terminal.check(c);
    v = verification(); v.maxAgeUs = 0;
    assert(Ess::verifyPersistence(c, v, now) && c.liveReadbackKnown && !c.verifiedFields);
}
int main() {
    testOptionalAgePolicy();
    preparation(); acknowledgementsAndLostReplies(); invalidEnvelopes();
    liveAndRestartVerification(); invalidVerification(); return 0;
}
