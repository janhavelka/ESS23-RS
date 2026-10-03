// SPDX-License-Identifier: MIT
#include "../examples/common/RtuBusOwner.h"
#include "../examples/common/EssRtuValidator.h"
#include "MotorControlRS/profiles/ess_rs/Codec.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <limits>
#include <vector>

using namespace MotorControlRSExample::Rtu;
namespace Ess = MotorControlRS::ESS_RS;
namespace {
// Native-only fake storage. Wire timestamps are independent of service times.
struct Fake {
    uint64_t now = 0, txEnd = 0, through = std::numeric_limits<uint64_t>::max();
    uint32_t txWidth = 0, releaseWidth = 0;
    uint64_t releaseDelay = 20;
    unsigned writes = 0, reads = 0, assertions = 0, releases = 0;
    std::size_t next = 0, accept = MAX_FRAME;
    bool pending = false, txBusy = false, writeError = false, releaseError = false;
    std::vector<RxByte> input;
    std::vector<std::vector<uint8_t> > sent;
    static bool direction(void* p, bool enable) {
        Fake& f = *static_cast<Fake*>(p);
        if (enable) ++f.assertions; else ++f.releases;
        return enable || !f.releaseError;
    }
    static WriteResult write(void* p, const uint8_t* bytes, std::size_t count) {
        Fake& f = *static_cast<Fake*>(p);
        ++f.writes;
        const std::size_t n = std::min(count, f.accept);
        f.sent.push_back(std::vector<uint8_t>(bytes, bytes + n));
        f.txEnd = f.now + n * 100;
        return WriteResult(n, f.writeError);
    }
    static TxState tx(void* p, uint64_t now, TxObservation& out) {
        Fake& f = *static_cast<Fake*>(p);
        if (f.txBusy || now < f.txEnd + (f.writes ? f.releaseDelay : 0)) return TxState::BUSY;
        out.endedUs = f.txEnd;
        out.uncertaintyUs = f.txWidth;
        if (f.writes) {
            out.released = true;
            out.releasedUs = f.txEnd + f.releaseDelay;
            out.releaseUncertaintyUs = f.releaseWidth;
        }
        return TxState::IDLE;
    }
    static ReadState read(void* p, uint64_t now, RxByte& byte, uint64_t& watermark) {
        Fake& f = *static_cast<Fake*>(p);
        ++f.reads;
        if (f.pending) return ReadState::PENDING;
        watermark = std::min(now, f.through);
        if (f.next < f.input.size()) {
            const RxByte& b = f.input[f.next];
            if (b.endUs <= watermark) { byte = b; ++f.next; return ReadState::BYTE; }
            watermark = std::min(watermark, b.startUs);
        }
        return ReadState::EMPTY;
    }
    void bytes(const std::vector<uint8_t>& bytes, uint64_t at, uint32_t width = 0) {
        for (uint8_t value : bytes) {
            RxByte b;
            b.startUs = at; b.endUs = at + 100; b.value = value;
            b.uncertaintyUs = width;
            input.push_back(b); at += 100;
        }
    }
};
Port port(Fake& f) {
    Port p; p.context = &f; p.setTransmit = Fake::direction; p.write = Fake::write;
    p.txState = Fake::tx; p.read = Fake::read; return p;
}
Timing timing() {
    Timing t; t.gap15Us = 150; t.gap35Us = 350; t.setupUs = 10; t.holdUs = 20;
    t.busTimeoutUs = 100000; t.txTimeoutUs = 100000; t.captureTimeoutUs = 15000; return t;
}
Storage runnerStorage(uint8_t* tx, uint8_t* rx, std::size_t txCapacity, std::size_t rxCapacity) {
    Storage s; s.tx = tx; s.rx = rx; s.txCapacity = txCapacity; s.rxCapacity = rxCapacity; return s;
}
BusStorage busStorage(PendingSlot* p, ResultSlot* r, std::size_t pn, std::size_t rn, ProducerSlot* producers) {
    BusStorage s; s.pending = p; s.results = r; s.pendingCapacity = pn; s.resultCapacity = rn;
    s.producers = producers; s.producerCapacity = 1; return s;
}
struct Rig {
    Fake fake;
    uint8_t tx[MAX_FRAME] = {}, rx[MAX_FRAME] = {};
    PendingSlot pendingSlots[4]; ResultSlot resultSlots[6]; ProducerSlot producers[1];
    Runner runner; BusOwner owner;
    Rig(std::size_t pendingCapacity = 4, std::size_t resultCapacity = 6,
        std::size_t txCapacity = MAX_FRAME, std::size_t rxCapacity = MAX_FRAME)
        : runner(port(fake), runnerStorage(tx, rx, txCapacity, rxCapacity), timing()),
          owner(runner, busStorage(pendingSlots, resultSlots, pendingCapacity, resultCapacity, producers)) {}
    void service(uint64_t at, unsigned count = 1) {
        fake.now = at;
        for (unsigned i = 0; i < count; ++i) owner.service(at);
    }
    void send(uint64_t start = 1000) {
        service(start); service(start + 350); service(start + 360);
    }
};
// Independent bit-wise reflected CRC for reply fixtures; builders are production.
void crc(std::vector<uint8_t>& frame) {
    uint16_t sum = 0xFFFF;
    for (uint8_t value : frame) {
        sum ^= value;
        for (unsigned bit = 0; bit < 8; ++bit)
            sum = static_cast<uint16_t>((sum >> 1) ^ ((sum & 1) ? 0xA001 : 0));
    }
    frame.push_back(static_cast<uint8_t>(sum)); frame.push_back(static_cast<uint8_t>(sum >> 8));
}
std::vector<uint8_t> reply(uint8_t address = 1, unsigned count = 1) {
    std::vector<uint8_t> result = {address, 3, static_cast<uint8_t>(count * 2)};
    for (unsigned i = 0; i < count; ++i) { result.push_back(0); result.push_back(static_cast<uint8_t>(60 + i)); }
    crc(result); return result;
}
BusRequest request(uint8_t* bytes, uint64_t deadline = 20000, unsigned count = 1, uint8_t address = 1) {
    BusRequest b;
    b.wire.length = Ess::buildReadRegisters(address, count == 16 ? 0x0100 : 0, static_cast<uint16_t>(count), bytes, 32);
    assert(b.wire.length);
    b.wire.bytes = bytes; b.wire.replyLength = Ess::expectedReadRegistersLen(static_cast<uint16_t>(count));
    b.wire.responseTimeoutUs = 10000; b.wire.deadlineUs = deadline;
    b.expected.address = address; b.expected.function = 3;
    b.expected.first = count == 16 ? 0x0100 : 0; b.expected.count = static_cast<uint16_t>(count);
    b.expected.target = 123; b.expected.targetGeneration = 7;
    b.validator = essValidator(); return b;
}
RequestId admit(Rig& rig, const BusRequest& request, uint64_t at = 1000) {
    RequestId id; assert(rig.owner.admit(request, at, id) == BusAdmission::ACCEPTED); return id;
}
const Completion& done(Rig& rig, const RequestId& id) {
    const Completion* completion = rig.owner.result(id); assert(completion); return *completion;
}
void recover(Rig& rig, uint64_t at) {
    uint64_t id = 0;
    assert(rig.owner.recover(at, at + 10000, id) == RecoveryAdmission::ACCEPTED);
    rig.service(at + 350);
    assert(rig.owner.recoveryResult(id) && rig.owner.recoveryResult(id)->outcome == RecoveryOutcome::RECOVERED);
    assert(rig.owner.releaseRecovery(id));
}
void complete(Rig& rig, uint64_t start = 1000, unsigned count = 1, uint8_t address = 1) {
    rig.send(start);
    assert(rig.fake.writes);
    const uint64_t first = rig.fake.txEnd + 350;
    rig.fake.bytes(reply(address, count), first);
    rig.service(first + (5 + 2 * count) * 100 + 350);
}
void testCapacitiesAndIds() {
    uint8_t bytes[32]; const BusRequest b = request(bytes);
    for (unsigned zero = 0; zero < 2; ++zero) {
        Rig rig(zero == 0 ? 0 : 1, zero == 1 ? 0 : 1);
        assert(rig.owner.valid() && rig.fake.reads == 0 && rig.fake.writes == 0);
        RequestId id; id.generation = 99;
        assert(rig.owner.admit(b, 1000, id) == (zero == 0 ? BusAdmission::QUEUE_FULL : BusAdmission::RESULTS_FULL));
        assert(id.generation == 99 && rig.fake.reads == 0);
    }
    Rig rig(1, 2);
    RequestId a = admit(rig, b);
    RequestId rejected; rejected.generation = 99;
    assert(rig.owner.admit(b, 1000, rejected) == BusAdmission::QUEUE_FULL);
    assert(rejected.generation == 99 && !rig.owner.result(a) && !rig.owner.release(a));
    rig.service(1000); // Active no longer consumes pending storage.
    RequestId c = admit(rig, b);
    assert(rig.owner.pending() == 1 && rig.owner.active());
    complete(rig);
    assert(done(rig, a).outcome == Outcome::SUCCESS);
    assert(rig.owner.admit(b, 4000, rejected) == BusAdmission::RESULTS_FULL);
    const Completion* retained = rig.owner.result(a);
    rig.service(4000, 5);
    assert(rig.owner.result(a) == retained && retained->expected.target == 123);
    assert(rig.owner.release(a) && !rig.owner.release(a) && !rig.owner.result(a));
    RequestId reused = admit(rig, b, 4000);
    assert(reused.slot == a.slot && reused.generation != a.generation);
    assert(!rig.owner.result(a) && !rig.owner.release(a));
    Rig other;
    RequestId foreign = a; foreign.owner = &other.owner;
    assert(!rig.owner.result(foreign));
    RequestId wrong = c; ++wrong.generation;
    assert(!rig.owner.result(wrong)); wrong = c; wrong.slot = 100;
    assert(!rig.owner.result(wrong));
}
void testTransientAndReadShapes() {
    for (unsigned count : {1u, 16u}) {
        Rig rig; uint8_t bytes[32]; BusRequest b = request(bytes, 20000, count);
        const std::vector<uint8_t> expected(bytes, bytes + b.wire.length);
        RequestId id = admit(rig, b);
        std::memset(bytes, 0xCC, sizeof(bytes)); b.expected.address = 77; b.expected.count = 9; b.wire.deadlineUs = 1;
        complete(rig, 1000, count);
        const Completion& r = done(rig, id);
        assert(r.outcome == Outcome::SUCCESS && !r.executionUnknown);
        assert(r.transport.rxLength == (count == 1 ? 7 : 37));
        assert(r.transport.txAccepted == expected.size() && rig.fake.sent[0] == expected);
        assert(r.expected.address == 1 && r.expected.count == count && r.deadlineUs == 20000);
        assert(r.expected.target == 123 && r.expected.targetGeneration == 7);
        const std::vector<uint8_t> raw = reply(1, count);
        assert(std::memcmp(r.raw, raw.data(), raw.size()) == 0);
        const Completion snapshot = r;
        rig.service(30000, 20);
        assert(rig.fake.writes == 1 && done(rig, id).transport.endedUs == snapshot.transport.endedUs);
    }
}
void testWriteShapes() {
    for (bool echo : {false, true}) {
        Rig rig; uint8_t bytes[32]; BusRequest b;
        b.wire.length = Ess::buildWriteSingleRegister(1, 0x0023, 60, bytes, sizeof(bytes));
        b.wire.bytes = bytes; b.wire.replyLength = 8; b.wire.responseTimeoutUs = 10000; b.wire.deadlineUs = 20000;
        b.wire.echo = echo ? Echo::REQUIRED : Echo::NONE;
        b.expected.address = 1; b.expected.function = 6; b.expected.first = 0x0023; b.expected.count = 1; b.expected.value = 60;
        b.validator = essValidator(); assert(b.wire.length == 8);
        const std::vector<uint8_t> wire(bytes, bytes + 8);
        RequestId id = admit(rig, b); rig.send();
        if (echo) rig.fake.bytes(wire, 1360);
        rig.fake.bytes(wire, rig.fake.txEnd + 350);
        rig.service(4000);
        const Completion& result = done(rig, id);
        assert(result.outcome == Outcome::SUCCESS && result.transport.rxLength == 8);
        assert(result.transport.echoBytes == (echo ? 8 : 0));
        assert(std::memcmp(result.raw, wire.data(), 8) == 0 && rig.fake.writes == 1);
    }
    struct Window { uint16_t first, count; };
    for (Window window : {Window{0x0024, 2}, Window{0x0021, 5}, Window{0x001D, 3}, Window{0x0031, 6}}) {
        Rig rig; uint8_t bytes[32]; uint16_t words[6] = {1, 2, 3, 4, 5, 6}; BusRequest b;
        b.wire.length = Ess::buildWriteMultipleRegisters(1, window.first, words, window.count, bytes, sizeof(bytes));
        b.wire.bytes = bytes; b.wire.replyLength = 8; b.wire.responseTimeoutUs = 10000; b.wire.deadlineUs = 20000;
        b.expected.address = 1; b.expected.function = 0x10; b.expected.first = window.first; b.expected.count = window.count;
        b.validator = essValidator(); assert(b.wire.length == 9u + 2u * window.count);
        RequestId id = admit(rig, b); rig.send();
        std::vector<uint8_t> response(bytes, bytes + 6); crc(response);
        rig.fake.bytes(response, rig.fake.txEnd + 350); rig.service(rig.fake.txEnd + 1500);
        const Completion& result = done(rig, id);
        assert(result.outcome == Outcome::SUCCESS && result.txLength == b.wire.length);
        assert(result.transport.rxLength == 8 && result.transport.txAccepted == b.wire.length);
    }
}
void testStandaloneValidatorBounds() {
    // The validator callback is callable outside BusOwner's wire-shape guards.
    // Unsupported FC10 sizes must reject before indexing even a zero-length frame.
    const uint8_t oneByte = 0xAA;
    Request wire; wire.bytes = &oneByte; wire.length = 0; wire.replyLength = 8;
    Expectation expected; expected.address = 1; expected.function = 0x10; expected.first = 0x0024;
    for (uint16_t count : {uint16_t(0), uint16_t(1), uint16_t(4), uint16_t(7)}) {
        expected.count = count;
        assert(!essValidator().checkRequest(expected, wire));
        assert(oneByte == 0xAA);
    }
    // Reviewed counts also require the complete byte span before decoding words.
    for (uint16_t count : {uint16_t(2), uint16_t(3), uint16_t(5), uint16_t(6)}) {
        expected.count = count;
        assert(!essValidator().checkRequest(expected, wire));
        wire.length = 1; assert(!essValidator().checkRequest(expected, wire)); wire.length = 0;
    }
}
void testInvalidRequestsAndStorage() {
    for (unsigned variant = 0; variant < 12; ++variant) {
        Rig rig; uint8_t bytes[32]; BusRequest b = request(bytes);
        switch (variant) {
        case 0: b.wire.bytes = nullptr; break;
        case 1: b.wire.length = 0; break;
        case 2: b.wire.length = MAX_FRAME + 1; break;
        case 3: b.wire.replyLength = 4; break;
        case 4: b.wire.responseTimeoutUs = 0; break;
        case 5: b.wire.deadlineUs = 0; break;
        case 6: b.validator.checkRequest = nullptr; break;
        case 7: b.validator.checkReply = nullptr; break;
        case 8: ++b.expected.address; break;
        case 9: ++b.expected.count; break;
        case 10: bytes[7] ^= 1; break;
        case 11: b.wire.echo = static_cast<Echo>(99); break;
        }
        RequestId id; id.generation = 99;
        assert(rig.owner.admit(b, 1000, id) == BusAdmission::INVALID);
        assert(id.generation == 99 && rig.owner.pending() == 0 && rig.fake.writes == 0 && rig.fake.reads == 0);
    }
    for (unsigned small = 0; small < 2; ++small) {
        Rig rig(4, 6, small ? MAX_FRAME : 7, small ? 6 : MAX_FRAME);
        uint8_t bytes[32]; RequestId id;
        assert(rig.owner.admit(request(bytes), 1000, id) == BusAdmission::INVALID);
    }
    Rig exact(1, 1, 8, 7); uint8_t bytes[32]; RequestId id = admit(exact, request(bytes));
    complete(exact); assert(done(exact, id).outcome == Outcome::SUCCESS);
    Rig exactSixteen(1, 1, 8, 37); RequestId sixteenId = admit(exactSixteen, request(bytes, 20000, 16));
    complete(exactSixteen, 1000, 16); assert(done(exactSixteen, sixteenId).outcome == Outcome::SUCCESS);
    for (std::size_t capacity : {std::size_t(20), std::size_t(21)}) {
        Rig longest(1, 1, capacity, 8); uint16_t words[6] = {1, 2, 3, 4, 5, 6}; BusRequest write;
        write.wire.length = Ess::buildWriteMultipleRegisters(1, 0x0031, words, 6, bytes, sizeof(bytes));
        write.wire.bytes = bytes; write.wire.replyLength = 8; write.wire.responseTimeoutUs = 10000; write.wire.deadlineUs = 20000;
        write.expected.address = 1; write.expected.function = 0x10; write.expected.first = 0x0031; write.expected.count = 6;
        write.validator = essValidator(); RequestId writeId;
        assert(longest.owner.admit(write, 1000, writeId) == (capacity == 20 ? BusAdmission::INVALID : BusAdmission::ACCEPTED));
        if (capacity == 21) {
            longest.send(); std::vector<uint8_t> response(bytes, bytes + 6); crc(response);
            longest.fake.bytes(response, longest.fake.txEnd + 350); longest.service(longest.fake.txEnd + 1500);
            assert(done(longest, writeId).outcome == Outcome::SUCCESS);
        } else assert(longest.fake.writes == 0);
    }
    Rig sixteen(1, 1, 8, 36); RequestId denied;
    assert(sixteen.owner.admit(request(bytes, 20000, 16), 1000, denied) == BusAdmission::INVALID);
    Rig backing;
    BusStorage invalid = busStorage(backing.pendingSlots, backing.resultSlots, 65536, 1, backing.producers);
    BusOwner huge(backing.runner, invalid); assert(!huge.valid());
    invalid = busStorage(nullptr, backing.resultSlots, 1, 1, backing.producers);
    BusOwner null(backing.runner, invalid); assert(!null.valid());
    invalid = busStorage(backing.pendingSlots, reinterpret_cast<ResultSlot*>(backing.pendingSlots), 1, 1, backing.producers);
    BusOwner overlap(backing.runner, invalid); assert(!overlap.valid());
    invalid = busStorage(reinterpret_cast<PendingSlot*>(backing.tx), backing.resultSlots, 1, 1, backing.producers);
    BusOwner wireOverlap(backing.runner, invalid); assert(!wireOverlap.valid());
    // Mutating caller slots is prohibited; this fixture reaches generation exhaustion without 2^64 admissions.
    Rig exhausted(1, 1); exhausted.resultSlots[0].generation = std::numeric_limits<uint64_t>::max();
    assert(exhausted.owner.admit(request(bytes), 1000, denied) == BusAdmission::IDS_EXHAUSTED);
}
void testClassificationAndRecovery() {
    for (unsigned failure = 0; failure < 4; ++failure) {
        Rig rig; uint8_t bytes[32]; BusRequest b = request(bytes);
        RequestId a = admit(rig, b); rig.service(1000); RequestId queued = admit(rig, b);
        rig.send(); std::vector<uint8_t> response;
        if (failure == 0) response = reply(2); // Valid CRC, foreign address.
        if (failure == 1) { response = reply(); response.back() ^= 1; }
        if (failure >= 2) { response = {1, 0x83, static_cast<uint8_t>(failure == 2 ? 2 : 0xF7)}; crc(response); }
        rig.fake.bytes(response, 2510); rig.service(4000);
        const Completion& result = done(rig, a);
        assert(result.transport.reason == Reason::FRAME);
        assert(std::memcmp(result.raw, response.data(), response.size()) == 0);
        if (failure < 2) {
            assert(result.outcome == Outcome::INVALID_REPLY && result.executionUnknown);
            assert(result.validation.code == (failure == 1 ? MotorControlRS::Err::CRC_ERROR : MotorControlRS::Err::FRAME_ERROR));
            assert(rig.owner.needsRecovery() && !rig.owner.active() && rig.owner.pending() == 1);
            rig.service(5000, 4); assert(rig.fake.writes == 1 && !rig.owner.result(queued));

            rig.service(20000);
            assert(done(rig, queued).outcome == Outcome::QUEUE_EXPIRED);
            recover(rig, 20000);
            assert(!rig.owner.active() && !rig.owner.pending());
        } else {
            assert(result.outcome == Outcome::DEVICE_REJECTED && !result.executionUnknown);
            assert(result.validation.code == MotorControlRS::Err::EXCEPTION);
            assert(result.validation.detail == (failure == 2 ? 2 : 0xF7));
            assert(!rig.owner.needsRecovery() && rig.owner.active());
        }
        assert(done(rig, a).transport.reason == Reason::FRAME && rig.fake.writes == 1);
    }
    Rig shortWrite; uint8_t bytes[32]; RequestId id = admit(shortWrite, request(bytes));
    shortWrite.fake.accept = 3; shortWrite.send(); shortWrite.service(2000);
    assert(done(shortWrite, id).outcome == Outcome::TRANSPORT);
    assert(done(shortWrite, id).transport.txAccepted == 3 && done(shortWrite, id).executionUnknown);
    assert(shortWrite.owner.needsRecovery()); shortWrite.service(3000, 5); assert(shortWrite.fake.writes == 1);
}
void testWriteEchoAndOverflow() {
    Rig rig; uint8_t bytes[32]; BusRequest b;
    b.wire.length = Ess::buildWriteSingleRegister(1, 0x0023, 60, bytes, sizeof(bytes));
    b.wire.bytes = bytes; b.wire.replyLength = 8; b.wire.responseTimeoutUs = 10000; b.wire.deadlineUs = 20000;
    b.expected.address = 1; b.expected.function = 6; b.expected.first = 0x0023; b.expected.count = 1; b.expected.value = 60;
    b.validator = essValidator();
    RequestId id = admit(rig, b); rig.send();
    std::vector<uint8_t> wrong(bytes, bytes + 6); wrong[5] = 61; crc(wrong);
    rig.fake.bytes(wrong, 2510); rig.service(4000);
    assert(done(rig, id).outcome == Outcome::INVALID_REPLY && done(rig, id).executionUnknown);
    assert(done(rig, id).validation.code == MotorControlRS::Err::FRAME_ERROR && rig.owner.needsRecovery());
    assert(done(rig, id).transport.rxLength == 8 && std::memcmp(done(rig, id).raw, wrong.data(), 8) == 0);

    Rig overflow(1, 1, 8, 7); BusRequest read = request(bytes); RequestId over = admit(overflow, read);
    overflow.send(); std::vector<uint8_t> oversized = reply(); oversized.push_back(0xEE);
    overflow.fake.bytes(oversized, 2510); overflow.service(4000);
    const Completion& r = done(overflow, over);
    assert(r.transport.reason == Reason::RX_OVERFLOW && r.transport.rxTruncated && r.transport.rxLength == 7);
    assert(r.outcome == Outcome::TRANSPORT && r.executionUnknown && overflow.owner.needsRecovery());
    assert(std::memcmp(r.raw, oversized.data(), 7) == 0);
}
void testWrappedCompactionAndReuse() {
    Rig rig(3, 6); uint8_t bytes[32];
    RequestId first = admit(rig, request(bytes)); complete(rig); assert(rig.owner.release(first));
    // The next three requests occupy indices 1,2,0. Removing index 2 compacts index 0 over it.
    RequestId a = admit(rig, request(bytes, 30000, 1, 1), 4000);
    RequestId expired = admit(rig, request(bytes, 4200, 1, 2), 4000);
    RequestId c = admit(rig, request(bytes, 30000, 1, 3), 4000);
    complete(rig, 4000); assert(done(rig, expired).outcome == Outcome::QUEUE_EXPIRED);
    const Completion saved = done(rig, a);
    complete(rig, 8000, 1, 3); assert(done(rig, c).outcome == Outcome::SUCCESS);
    RequestId d = admit(rig, request(bytes, 30000, 1, 4), 12000); complete(rig, 12000, 1, 4);
    assert(done(rig, d).outcome == Outcome::SUCCESS);
    assert(done(rig, a).expected.address == saved.expected.address && done(rig, a).transport.endedUs == saved.transport.endedUs);
    assert(std::memcmp(done(rig, a).raw, saved.raw, saved.transport.rxLength) == 0);
    assert(rig.fake.sent.size() == 4 && rig.fake.sent[2][0] == 3 && rig.fake.sent[3][0] == 4);
}
void testFifoExpiryAndBudget() {
    Rig rig; uint8_t aBytes[32], bBytes[32], cBytes[32], dBytes[32];
    RequestId a = admit(rig, request(aBytes, 30000, 1, 1));
    RequestId b = admit(rig, request(bBytes, 2000, 1, 2));
    RequestId c = admit(rig, request(cBytes, 30000, 1, 3));
    RequestId d = admit(rig, request(dBytes, 30000, 1, 4));
    complete(rig);
    assert(done(rig, b).outcome == Outcome::QUEUE_EXPIRED && done(rig, b).transport.txAccepted == 0);
    assert(done(rig, b).deadlineUs == 2000 && !done(rig, b).executionUnknown);
    assert(done(rig, a).outcome == Outcome::SUCCESS);
    complete(rig, 4000, 1, 3); complete(rig, 8000, 1, 4);
    assert(done(rig, c).outcome == Outcome::SUCCESS && done(rig, d).outcome == Outcome::SUCCESS);
    assert(rig.fake.sent.size() == 3 && rig.fake.sent[0][0] == 1 && rig.fake.sent[1][0] == 3 && rig.fake.sent[2][0] == 4);
    // Queue compaction wraps around the ring while preserving owned byte pointers.
    assert(rig.owner.release(a) && rig.owner.release(b));
    RequestId again = admit(rig, request(aBytes, 30000, 1, 1), 12000);
    complete(rig, 12000); assert(done(rig, again).outcome == Outcome::SUCCESS);
    Rig budget; uint8_t bytes[32]; RequestId id = admit(budget, request(bytes));
    std::vector<uint8_t> old(READ_BUDGET + 1, 0x42); budget.fake.bytes(old, 0);
    budget.service(1000); unsigned before = budget.fake.reads; budget.service(7000);
    assert(budget.fake.reads - before == READ_BUDGET && budget.fake.assertions == 0);
    budget.service(7000); assert(budget.fake.assertions == 1);
    budget.service(7010); assert(budget.fake.writes == 1 && !budget.owner.result(id));
}
// Test instrumentation observes the classification barrier without mutating owner/runner.
Rig* observedRig = nullptr;
unsigned expectedStarts = 0, classifications = 0;
MotorControlRS::Status observeReply(const Expectation& expected, const uint8_t* bytes, std::size_t count) {
    assert(observedRig && observedRig->owner.active());
    assert(observedRig->runner.stats().started == expectedStarts);
    ++classifications;
    return essValidator().checkReply(expected, bytes, count);
}
bool largeEchoRequest(const Expectation& expected, const Request& wire) {
    return expected.address == 1 && expected.function == 3 && wire.length == MAX_FRAME &&
        wire.replyLength == 5 && wire.echo == Echo::REQUIRED;
}
void testClassificationBarrierAndLateBudget() {
    for (bool corrupt : {false, true}) {
        Rig rig; uint8_t bytes[32]; BusRequest b = request(bytes); b.validator.checkReply = observeReply;
        RequestId a = admit(rig, b); rig.service(1000); RequestId queued = admit(rig, b);
        (void)queued; observedRig = &rig; expectedStarts = 1; classifications = 0;
        rig.send(); std::vector<uint8_t> response = reply(); if (corrupt) response.back() ^= 1;
        rig.fake.bytes(response, 2510); rig.service(4000);
        assert(classifications == 1 && done(rig, a).outcome == (corrupt ? Outcome::INVALID_REPLY : Outcome::SUCCESS));
        assert(rig.runner.stats().started == (corrupt ? 1u : 2u)); observedRig = nullptr;
    }
    Rig rig; uint8_t bytes[MAX_FRAME]; std::memset(bytes, 0x42, sizeof(bytes)); bytes[0] = 1; bytes[1] = 3;
    BusRequest b; b.wire.bytes = bytes; b.wire.length = sizeof(bytes); b.wire.replyLength = 5;
    b.wire.responseTimeoutUs = 50000; b.wire.deadlineUs = 29000; b.wire.echo = Echo::REQUIRED;
    b.expected.address = 1; b.expected.function = 3; b.expected.count = 1;
    b.validator.checkRequest = largeEchoRequest; b.validator.checkReply = essValidator().checkReply;
    RequestId id = admit(rig, b); rig.send();
    rig.fake.bytes(std::vector<uint8_t>(bytes, bytes + sizeof(bytes)), 1360);
    std::vector<uint8_t> exception = {1, 0x83, 2}; crc(exception);
    rig.fake.bytes(exception, rig.fake.txEnd + 350);
    for (unsigned pass = 0; pass < 4; ++pass) {
        unsigned before = rig.fake.reads; rig.service(40000);
        assert(rig.fake.reads - before == READ_BUDGET && !rig.owner.result(id));
    }
    rig.service(40000);
    const Completion& result = done(rig, id);
    assert(result.outcome == Outcome::DEVICE_REJECTED && result.transport.reason == Reason::FRAME);
    assert(result.transport.echoBytes == MAX_FRAME && result.transport.rxLength == 5);
    assert(result.transport.closureQualified && result.transport.closureLatestUs == 28160 && result.deadlineUs == 29000);
    assert(rig.fake.writes == 1 && !rig.owner.needsRecovery());
}
void testClockRegression() {
    uint8_t bytes[32];
    {
        Rig rig; RequestId id = admit(rig, request(bytes), 100);
        RequestId rejected;
        assert(rig.owner.admit(request(bytes), 90, rejected) == BusAdmission::INVALID);
        assert(rig.owner.needsRecovery());
        rig.service(100); assert(rig.fake.writes == 0 && !rig.owner.active());

        rig.service(20000); assert(done(rig, id).outcome == Outcome::QUEUE_EXPIRED);
        recover(rig, 20000);
    }
    {
        Rig rig; RequestId id = admit(rig, request(bytes), 100);
        rig.service(100); RequestId pending = admit(rig, request(bytes), 200);
        rig.service(150);
        assert(rig.owner.needsRecovery() && rig.fake.writes == 0);
        rig.service(200);
        assert(done(rig, id).transport.reason == Reason::CLOCK_ERROR);
        assert(!done(rig, id).executionUnknown && !rig.owner.result(pending));
        rig.service(20000);
        assert(done(rig, id).transport.reason == Reason::CLOCK_ERROR);
        assert(done(rig, pending).outcome == Outcome::QUEUE_EXPIRED);
        recover(rig, 20000);
        assert(done(rig, id).transport.reason == Reason::CLOCK_ERROR);
    }
}
void testAbsoluteDeadline() {
    uint8_t bytes[32];
    {
        Rig rig; BusRequest b = request(bytes, 1000); RequestId id; id.generation = 99;
        assert(rig.owner.admit(b, 1000, id) == BusAdmission::EXPIRED && id.generation == 99);
    }
    {
        Rig rig; RequestId id = admit(rig, request(bytes, 1200)); rig.service(1000); rig.service(1200);
        const Completion& r = done(rig, id);
        assert(r.outcome == Outcome::TRANSPORT && r.transport.reason == Reason::REQUEST_DEADLINE);
        assert(rig.fake.writes == 0 && rig.fake.assertions == 0 && !rig.owner.needsRecovery());
    }
    {
        Rig rig; RequestId id = admit(rig, request(bytes, 1360)); rig.service(1000); rig.service(1350); rig.service(1360);
        assert(done(rig, id).transport.reason == Reason::REQUEST_DEADLINE);
        assert(rig.fake.assertions == 1 && rig.fake.writes == 0 && rig.fake.releases == 1 && !rig.owner.needsRecovery());
    }
    {
        Rig rig; RequestId id = admit(rig, request(bytes, 2000)); rig.send(); rig.service(2000);
        assert(done(rig, id).transport.reason == Reason::REQUEST_DEADLINE);
        assert(done(rig, id).executionUnknown && rig.owner.needsRecovery() && rig.runner.transmitEnabled());
        rig.service(3000); assert(!rig.runner.transmitEnabled() && rig.fake.writes == 1);
    }
    // Closure is 3560, not the last byte's 3210. Exact boundary is accepted.
    for (uint64_t deadline : {uint64_t(3559), uint64_t(3560), uint64_t(4000)}) {
        Rig rig; RequestId id = admit(rig, request(bytes, deadline)); rig.send();
        rig.fake.bytes(reply(), 2510); rig.service(100000);
        const Completion& r = done(rig, id);
        assert(r.transport.closureQualified && r.transport.closureEarliestUs == 3560 && r.transport.closureLatestUs == 3560);
        assert(r.transport.reason == (deadline < 3560 ? Reason::REQUEST_DEADLINE : Reason::FRAME));
        assert(r.outcome == (deadline < 3560 ? Outcome::TRANSPORT : Outcome::SUCCESS));
        assert(r.deadlineUs == deadline && rig.fake.writes == 1);
    }
    {
        Rig rig; RequestId id = admit(rig, request(bytes, 3550)); rig.send();
        rig.fake.bytes(reply(), 2510); rig.fake.input.back().uncertaintyUs = 20; rig.service(3550);
        const Completion& r = done(rig, id);
        assert(r.transport.reason == Reason::TIMING_UNCERTAIN && !r.transport.closureQualified);
        assert(r.transport.closureEarliestUs == 3540 && r.transport.closureLatestUs == 3560);
        assert(r.executionUnknown && rig.owner.needsRecovery());
    }
    {
        Rig rig; RequestId id = admit(rig, request(bytes, 3550)); rig.send();
        rig.fake.bytes(reply(), 2510); rig.fake.input.back().uncertaintyUs = 20; rig.service(4000);
        assert(done(rig, id).transport.reason == Reason::TIMING_UNCERTAIN && done(rig, id).transport.closureQualified);
    }
    {
        Rig rig; RequestId id = admit(rig, request(bytes, 4000)); rig.send();
        rig.fake.bytes(reply(), 2510); rig.fake.through = 3560; rig.service(100000);
        assert(done(rig, id).outcome == Outcome::SUCCESS); // Historical watermark qualifies closure despite service lag.
    }
    {
        Rig rig; RequestId id = admit(rig, request(bytes, 4000)); rig.send(); rig.service(2180);
        rig.fake.bytes(reply(), 2510); rig.service(3210); rig.fake.pending = true; rig.service(5000);
        assert(!rig.owner.result(id) && rig.owner.active());
        rig.fake.pending = false; rig.service(5010);
        assert(done(rig, id).outcome == Outcome::SUCCESS);
    }
    {
        Rig rig; RequestId id = admit(rig, request(bytes, 4000)); rig.send();
        rig.fake.through = 3999; rig.service(5000);
        assert(!rig.owner.result(id)); // Host time cannot replace the capture watermark.
        rig.fake.through = 4000; rig.service(5010);
        assert(done(rig, id).transport.reason == Reason::REQUEST_DEADLINE && !done(rig, id).transport.closureQualified);
    }
    {
        Rig rig; RequestId id = admit(rig, request(bytes, 4000)); rig.send();
        rig.fake.bytes(reply(), 2510); rig.fake.bytes(reply(), 4000); rig.service(100000);
        const Completion& r = done(rig, id);
        assert(r.outcome == Outcome::SUCCESS && r.transport.closureQualified && r.transport.closureLatestUs == 3560);
        assert(r.transport.rxLength == 7); // A new-frame byte proves the earlier t3.5 gap, independently of task wakeup.
    }
    {
        Rig rig; RequestId id = admit(rig, request(bytes, 2700)); rig.send();
        std::vector<uint8_t> prefix = {1, 3}; rig.fake.bytes(prefix, 2510);
        rig.fake.input.back().uncertaintyUs = 20; rig.service(3000);
        const Completion& r = done(rig, id);
        assert(r.transport.reason == Reason::REQUEST_DEADLINE && !r.transport.closureQualified);
        assert(r.transport.closureEarliestUs == 3040 && r.transport.closureLatestUs == 3060);
    }
    {
        Rig rig; BusRequest b = request(bytes, 20000); b.wire.responseTimeoutUs = 1300;
        RequestId id = admit(rig, b); rig.send(); rig.fake.bytes(reply(), 2510); rig.service(4000);
        assert(done(rig, id).transport.reason == Reason::PARTIAL_RESPONSE && done(rig, id).deadlineUs == 20000);
    }
    {
        Rig rig; RequestId id = admit(rig, request(bytes, 4000)); rig.send(); rig.service(2180);
        rig.fake.pending = true; rig.service(17181);
        assert(done(rig, id).transport.reason == Reason::CAPTURE_TIMEOUT);
    }
}
void testEffectiveClosureBudget() {
    // TX ends at 2160. The final stop is [3190,3210], so closure is
    // [3540,3560]. The earlier applicable budget must decide expiry even
    // when the later budget straddles the closure interval.
    struct Scenario {
        uint64_t deadline;
        uint32_t responseTimeout;
        uint64_t serviceAt;
        Reason reason;
        bool response, qualified;
    };
    const Scenario scenarios[] = {
        {3550, 1300, 4000, Reason::PARTIAL_RESPONSE, true, true},
        {3550, 1300, 3550, Reason::PARTIAL_RESPONSE, true, false},
        {3550, 1300, 4000, Reason::NO_RESPONSE, false, false},
        {3550, 2000, 4000, Reason::TIMING_UNCERTAIN, true, true},
        {3550, 2000, 3550, Reason::TIMING_UNCERTAIN, true, false},
        {3550, 2000, 5000, Reason::REQUEST_DEADLINE, false, false},
        {3500, 1300, 4000, Reason::PARTIAL_RESPONSE, true, true},
        {3400, 1300, 4000, Reason::REQUEST_DEADLINE, true, true}
    };
    for (const Scenario& scenario : scenarios) {
        Rig rig; uint8_t bytes[32]; BusRequest b = request(bytes, scenario.deadline);
        b.wire.responseTimeoutUs = scenario.responseTimeout;
        RequestId id = admit(rig, b); rig.send();
        const std::vector<uint8_t> response = reply();
        if (scenario.response) {
            rig.fake.bytes(response, 2510);
            rig.fake.input.back().uncertaintyUs = 20;
        }
        rig.service(scenario.serviceAt);
        const Completion& r = done(rig, id);
        assert(r.outcome == Outcome::TRANSPORT && r.transport.reason == scenario.reason);
        assert(r.deadlineUs == scenario.deadline && r.responseTimeoutUs == scenario.responseTimeout);
        assert(r.transport.txAccepted == 8 && r.executionUnknown && rig.owner.needsRecovery());
        assert(r.transport.closureQualified == scenario.qualified);
        if (scenario.response) {
            assert(r.transport.rxLength == response.size());
            assert(std::memcmp(r.raw, response.data(), response.size()) == 0);
            assert(r.transport.closureEarliestUs == 3540 && r.transport.closureLatestUs == 3560);
        } else {
            assert(r.transport.rxLength == 0 && r.transport.closureEarliestUs == 0 && r.transport.closureLatestUs == 0);
        }
        const Completion retained = r;
        rig.service(scenario.serviceAt + 1000, 3);
        assert(done(rig, id).transport.reason == retained.transport.reason && rig.fake.writes == 1);
        assert(done(rig, id).transport.endedUs == retained.transport.endedUs);
    }
    {
        Rig rig; uint8_t bytes[32]; BusRequest b = request(bytes);
        b.wire.responseTimeoutUs = 730; // Relative cutoff2890 straddles the fourth byte's stop.
        RequestId id = admit(rig, b); rig.send();
        const std::vector<uint8_t> response = reply();
        rig.fake.bytes(response, 2510); rig.fake.input[3].uncertaintyUs = 20;
        rig.service(3000);
        const Completion& r = done(rig, id);
        // The candidate closure [3240,3260] is definitely late even though
        // the stop-bit interval [2890,2910] includes the relative cutoff.
        assert(r.transport.reason == Reason::PARTIAL_RESPONSE && r.outcome == Outcome::TRANSPORT);
        assert(r.transport.rxLength == 3 && std::memcmp(r.raw, response.data(), 3) == 0);
        assert(r.transport.closureEarliestUs == 3240 && r.transport.closureLatestUs == 3260);
        assert(!r.transport.closureQualified && r.executionUnknown && rig.owner.needsRecovery());
        assert(r.deadlineUs == 20000 && r.transport.txAccepted == 8 && rig.fake.writes == 1);
    }
}
} // namespace
int main() {
    testCapacitiesAndIds(); testTransientAndReadShapes(); testWriteShapes();
    testStandaloneValidatorBounds(); testInvalidRequestsAndStorage(); testClassificationAndRecovery();
    testWriteEchoAndOverflow(); testWrappedCompactionAndReuse(); testFifoExpiryAndBudget(); testClassificationBarrierAndLateBudget(); testClockRegression(); testAbsoluteDeadline(); testEffectiveClosureBudget();
    return 0;
}
