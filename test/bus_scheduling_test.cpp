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
uint16_t checksum(const uint8_t* bytes, std::size_t count) {
    uint16_t crc = 0xFFFF;
    for (std::size_t i = 0; i < count; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = static_cast<uint16_t>((crc >> 1) ^ ((crc & 1) ? 0xA001 : 0));
    }
    return crc;
}
void appendCrc(std::vector<uint8_t>& bytes) {
    const uint16_t crc = checksum(bytes.data(), bytes.size());
    bytes.push_back(static_cast<uint8_t>(crc)); bytes.push_back(static_cast<uint8_t>(crc >> 8));
}
std::vector<uint8_t> reply(uint8_t address = 1, uint8_t function = 3) {
    std::vector<uint8_t> bytes = {address, function, 2, 0x12, 0x34}; appendCrc(bytes); return bytes;
}
// This fixture supplies transport evidence, never scheduling or cancellation.
struct Fake {
    uint64_t now = 0, txEnd = 0;
    uint64_t through = std::numeric_limits<uint64_t>::max();
    unsigned assertions = 0, releases = 0, reads = 0;
    bool de = false, automaticRelease = true, automaticReply = false;
    bool busy = false, pending = false, readError = false, releaseError = false;
    std::size_t next = 0;
    std::vector<RxByte> input;
    std::vector<std::vector<uint8_t> > sent;
    static bool direction(void* context, bool enabled) {
        Fake& f = *static_cast<Fake*>(context);
        if (enabled) ++f.assertions; else ++f.releases;
        if (!enabled && f.releaseError) return false;
        f.de = enabled; return true;
    }
    static WriteResult write(void* context, const uint8_t* bytes, std::size_t count) {
        Fake& f = *static_cast<Fake*>(context); assert(f.de);
        f.sent.push_back(std::vector<uint8_t>(bytes, bytes + count));
        f.txEnd = f.now + count * 100;
        if (f.automaticReply)
            f.bytes(bytes[1] == 6 && count == 8 ? f.sent.back() : reply(bytes[0], bytes[1]), f.txEnd + 350);
        return WriteResult(count);
    }
    static TxState tx(void* context, uint64_t at, TxObservation& observation) {
        Fake& f = *static_cast<Fake*>(context);
        const uint64_t release = f.txEnd + (f.sent.empty() ? 0 : 20);
        if (f.busy || at < (f.automaticRelease ? release : f.txEnd)) return TxState::BUSY;
        observation.endedUs = f.txEnd;
        if (f.automaticRelease && !f.sent.empty()) {
            observation.released = true; observation.releasedUs = release; f.de = false;
        }
        return TxState::IDLE;
    }
    static ReadState read(void* context, uint64_t at, RxByte& byte, uint64_t& through) {
        Fake& f = *static_cast<Fake*>(context); ++f.reads;
        if (f.readError) return ReadState::ERROR;
        if (f.pending) return ReadState::PENDING;
        through = std::min(at, f.through);
        if (f.next < f.input.size()) {
            const RxByte& candidate = f.input[f.next];
            if (candidate.endUs <= through) { byte = candidate; ++f.next; return ReadState::BYTE; }
            through = std::min(through, candidate.startUs);
        }
        return ReadState::EMPTY;
    }
    void bytes(const std::vector<uint8_t>& values, uint64_t at) {
        for (uint8_t value : values) {
            RxByte byte; byte.startUs = at; byte.endUs = at + 100; byte.value = value;
            input.push_back(byte); at += 100;
        }
    }
};
Port makePort(Fake& fake) {
    Port port; port.context = &fake; port.setTransmit = Fake::direction;
    port.write = Fake::write; port.txState = Fake::tx; port.read = Fake::read; return port;
}
Timing makeTiming() {
    Timing timing; timing.gap15Us = 150; timing.gap35Us = 350; timing.setupUs = 10;
    timing.holdUs = 20; timing.busTimeoutUs = 100000; timing.txTimeoutUs = 100000;
    timing.captureTimeoutUs = 15000; return timing;
}
Storage makeRunnerStorage(uint8_t* tx, uint8_t* rx) {
    Storage storage; storage.tx = tx; storage.rx = rx;
    storage.txCapacity = storage.rxCapacity = MAX_FRAME; return storage;
}
BusStorage makeBusStorage(PendingSlot* pending, ResultSlot* results, ProducerSlot* producers,
                          std::size_t pn, std::size_t rn, std::size_t un, std::size_t ur) {
    BusStorage storage; storage.pending = pending; storage.results = results;
    storage.pendingCapacity = pn; storage.resultCapacity = rn;
    storage.producers = producers; storage.producerCapacity = 3;
    storage.urgentPendingCapacity = un; storage.urgentResultCapacity = ur; return storage;
}
struct Rig {
    Fake fake;
    uint8_t tx[MAX_FRAME] = {}, rx[MAX_FRAME] = {};
    PendingSlot pending[8]; ResultSlot results[12]; ProducerSlot producers[3];
    Runner runner; BusOwner owner;
    Rig(std::size_t pn = 8, std::size_t rn = 12, std::size_t un = 2, std::size_t ur = 2)
        : runner(makePort(fake), makeRunnerStorage(tx, rx), makeTiming()),
          owner(runner, makeBusStorage(pending, results, producers, pn, rn, un, ur)) {
        assert(owner.valid());
    }
    void service(uint64_t at) { fake.now = at; owner.service(at); }
    void send() { service(1000); service(1350); service(1360); assert(fake.sent.size() == 1); }
    void until(const RequestId& id, uint64_t limit = 100000) {
        for (uint64_t at = fake.now; at <= limit && !owner.result(id); at += 10) service(at);
        assert(owner.result(id));
    }
    void recovered(uint64_t id, uint64_t limit = 100000) {
        for (uint64_t at = fake.now; at <= limit && !owner.recoveryResult(id); at += 10) service(at);
        assert(owner.recoveryResult(id));
    }
};
bool syntheticRequest(const Expectation& expected, const Request& wire) {
    return wire.bytes && wire.length == 8 && wire.replyLength == 7 &&
        expected.function == 4 && expected.first == 1 && expected.count == 1 &&
        wire.bytes[0] == expected.address && wire.bytes[1] == 4 && wire.bytes[2] == 0 &&
        wire.bytes[3] == 1 && wire.bytes[4] == 0 && wire.bytes[5] == 1 &&
        checksum(wire.bytes, wire.length) == 0;
}
MotorControlRS::Status syntheticReply(const Expectation& expected, const uint8_t* bytes, std::size_t count) {
    if (count != 7 || bytes[0] != expected.address || bytes[1] != 4 || bytes[2] != 2)
        return MotorControlRS::Status(MotorControlRS::Err::FRAME_ERROR, 0, "synthetic reply");
    if (checksum(bytes, count)) return MotorControlRS::Status(MotorControlRS::Err::CRC_ERROR, 0, "synthetic CRC");
    return MotorControlRS::Ok();
}
bool checkLongRequest(const Expectation& expected, const Request& wire) {
    return wire.bytes && wire.length == 81 && wire.replyLength == 8 && expected.function == 0x10 &&
        expected.first == 0 && expected.count == 36 && wire.bytes[0] == expected.address &&
        wire.bytes[1] == 0x10 && wire.bytes[2] == 0 && wire.bytes[3] == 0 &&
        wire.bytes[4] == 0 && wire.bytes[5] == 36 && wire.bytes[6] == 72 && checksum(wire.bytes, 81) == 0;
}
MotorControlRS::Status checkLongReply(const Expectation& expected, const uint8_t* bytes, std::size_t count) {
    if (count != 8 || bytes[0] != expected.address || bytes[1] != 0x10 ||
        bytes[2] != 0 || bytes[3] != 0 || bytes[4] != 0 || bytes[5] != expected.count)
        return MotorControlRS::Status(MotorControlRS::Err::FRAME_ERROR, 0, "synthetic long reply");
    if (checksum(bytes, count)) return MotorControlRS::Status(MotorControlRS::Err::CRC_ERROR, 0, "synthetic long CRC");
    return MotorControlRS::Ok();
}
// Test-only control register/value. This is a scheduling shape, not an ESS stop.
bool checkStopShape(const Expectation& expected, const Request& wire) {
    return wire.bytes && wire.length == 8 && wire.replyLength == 8 && expected.function == 6 &&
        expected.first == 0xFF00 && expected.count == 1 && expected.value == 0 &&
        wire.bytes[0] == expected.address && wire.bytes[1] == 6 && wire.bytes[2] == 0xFF &&
        wire.bytes[3] == 0 && wire.bytes[4] == 0 && wire.bytes[5] == 0 && checksum(wire.bytes, 8) == 0;
}
MotorControlRS::Status checkStopAck(const Expectation& expected, const uint8_t* bytes, std::size_t count) {
    if (count != 8 || bytes[0] != expected.address || bytes[1] != 6 || bytes[2] != 0xFF ||
        bytes[3] != 0 || bytes[4] != 0 || bytes[5] != expected.value)
        return MotorControlRS::Status(MotorControlRS::Err::FRAME_ERROR, 0, "synthetic stop echo");
    if (checksum(bytes, count)) return MotorControlRS::Status(MotorControlRS::Err::CRC_ERROR, 0, "synthetic stop CRC");
    return MotorControlRS::Ok();
}
BusRequest request(uint8_t* bytes, std::size_t producer = 0, uint64_t deadline = 100000,
                   bool synthetic = false) {
    BusRequest request; request.producer = producer;
    request.expected.target = static_cast<uint32_t>(100 + producer); request.expected.targetGeneration = 9;
    request.expected.address = static_cast<uint8_t>(producer + 1); request.expected.count = 1;
    request.wire.bytes = bytes; request.wire.replyLength = 7;
    request.wire.responseTimeoutUs = 10000; request.wire.deadlineUs = deadline;
    if (synthetic) {
        const std::vector<uint8_t> prefix = {request.expected.address, 4, 0, 1, 0, 1};
        std::memcpy(bytes, prefix.data(), prefix.size());
        const uint16_t crc = checksum(bytes, prefix.size()); bytes[6] = static_cast<uint8_t>(crc); bytes[7] = static_cast<uint8_t>(crc >> 8);
        request.wire.length = 8; request.expected.function = 4; request.expected.first = 1;
        request.validator.checkRequest = syntheticRequest; request.validator.checkReply = syntheticReply;
    } else {
        request.wire.length = Ess::buildReadRegisters(request.expected.address, 0, 1, bytes, 32);
        request.expected.function = 3; request.validator = essValidator();
    }
    assert(request.wire.length == 8); return request;
}
RequestId admit(Rig& rig, const BusRequest& request, uint64_t at = 1000, bool urgent = false) {
    RequestId id; rig.fake.now = at;
    assert((urgent ? rig.owner.admitUrgent(request, at, id) : rig.owner.admit(request, at, id)) == BusAdmission::ACCEPTED);
    return id;
}
const Completion& done(Rig& rig, const RequestId& id) {
    const Completion* result = rig.owner.result(id); assert(result); return *result;
}
void testScheduleBounds() {
    Rig backing;
    for (unsigned variant = 0; variant < 6; ++variant) {
        BusStorage storage = makeBusStorage(backing.pending, backing.results, backing.producers, 8, 12, 2, 2);
        switch (variant) {
        case 0: storage.producerCapacity = 0; break;
        case 1: storage.producerCapacity = 65536; break;
        case 2: storage.producers = nullptr; break;
        case 3: storage.producers = reinterpret_cast<ProducerSlot*>(backing.pending); break;
        case 4: storage.urgentPendingCapacity = 9; break;
        case 5: storage.urgentResultCapacity = 13; break;
        }
        BusOwner invalid(backing.runner, storage); assert(!invalid.valid());
    }
    assert(backing.fake.sent.empty() && backing.fake.reads == 0 && backing.fake.assertions == 0);
    for (unsigned variant = 0; variant < 4; ++variant) {
        Rig rig; uint8_t bytes[32]; BusRequest b = request(bytes);
        switch (variant) {
        case 0: b.producer = 3; break;
        case 1: b.dispatchDeadlineUs = b.wire.deadlineUs + 1; break;
        case 2: b.notBeforeUs = b.wire.deadlineUs; break;
        case 3: b.sequence.owner = &backing.owner; b.sequence.generation = 1; break;
        }
        RequestId rejected; rejected.generation = 99;
        assert(rig.owner.admit(b, 1000, rejected) == BusAdmission::INVALID);
        assert(rejected.generation == 99 && rig.owner.pending() == 0 && rig.fake.reads == 0 && rig.fake.sent.empty());
    }
    Rig exhausted;
    // Fixture-only mutation reaches exhaustion without 2^64 sequence starts.
    exhausted.producers[0].generation = std::numeric_limits<uint64_t>::max();
    SequenceId unchanged; unchanged.generation = 99;
    assert(!exhausted.owner.beginSequence(0, 10000, 1000, unchanged) && unchanged.generation == 99);
    assert(!exhausted.owner.beginSequence(0, 10000, 1000, unchanged));
    assert(exhausted.fake.sent.empty());
}
void testCancellationPhases() {
    for (Phase phase : {Phase::IDLE, Phase::WAIT_BUS, Phase::SETUP, Phase::DRAIN, Phase::HOLD, Phase::RECEIVE}) {
        Rig rig; uint8_t bytes[32]; RequestId id = admit(rig, request(bytes));
        if (phase != Phase::IDLE) rig.service(1000);
        if (phase == Phase::SETUP || phase == Phase::DRAIN || phase == Phase::HOLD || phase == Phase::RECEIVE) rig.service(1350);
        if (phase == Phase::DRAIN || phase == Phase::HOLD || phase == Phase::RECEIVE) rig.service(1360);
        if (phase == Phase::HOLD) { rig.fake.automaticRelease = false; rig.service(2160); }
        if (phase == Phase::RECEIVE) rig.service(2180);
        assert(rig.runner.phase() == phase);
        const uint64_t cancelAt = rig.fake.now ? rig.fake.now + 1 : 1000;
        rig.fake.now = cancelAt; assert(rig.owner.cancel(id, cancelAt) == Cancel::CANCELLED);
        const bool transmitted = phase == Phase::DRAIN || phase == Phase::HOLD || phase == Phase::RECEIVE;
        if (phase == Phase::DRAIN || phase == Phase::HOLD) {
            assert(rig.owner.active() && !rig.owner.result(id) && rig.runner.transmitEnabled());
            assert(rig.owner.cancel(id, cancelAt) == Cancel::CANCELLED);
            rig.service(2179); assert(!rig.owner.result(id) && rig.runner.transmitEnabled());
            rig.service(2180);
        }
        const Completion& r = done(rig, id);
        assert(r.outcome == Outcome::CANCELLED && r.cancellation == Cancellation::REQUEST);
        assert(r.transport.reason == Reason::CANCELLED && r.transport.txAccepted == (transmitted ? 8 : 0));
        assert(r.executionUnknown == transmitted && rig.owner.needsRecovery() == transmitted);
        assert(!rig.runner.transmitEnabled() && rig.fake.sent.size() == (transmitted ? 1 : 0));
        const Completion retained = r;
        assert(rig.owner.cancel(id, 3000) == Cancel::ALREADY_TERMINAL);
        rig.service(4000);
        assert(done(rig, id).transport.reason == retained.transport.reason && done(rig, id).transport.endedUs == retained.transport.endedUs);
        assert(rig.owner.release(id)); assert(rig.owner.cancel(id, 4000) == Cancel::INVALID);
    }
    Rig completed; uint8_t bytes[32]; completed.fake.automaticReply = true;
    RequestId id = admit(completed, request(bytes)); completed.until(id);
    assert(completed.runner.phase() == Phase::DONE && done(completed, id).outcome == Outcome::SUCCESS);
    assert(completed.owner.cancel(id, completed.fake.now) == Cancel::ALREADY_TERMINAL);
    assert(done(completed, id).outcome == Outcome::SUCCESS && !done(completed, id).executionUnknown);
    RequestId foreign = id; Rig other; foreign.owner = &other.owner;
    assert(completed.owner.cancel(foreign, completed.fake.now) == Cancel::INVALID);
}
void testDeadlineCancellation() {
    for (uint64_t deadline : {uint64_t(1200), uint64_t(1360), uint64_t(2000)}) {
        Rig rig; uint8_t bytes[32]; RequestId id = admit(rig, request(bytes, 0, deadline));
        rig.service(1000);
        if (deadline >= 1360) rig.service(1350);
        if (deadline == 2000) rig.service(1360);
        rig.fake.now = deadline; rig.owner.cancel(id, deadline); rig.service(deadline);
        const Completion& r = done(rig, id);
        assert(r.transport.reason == Reason::REQUEST_DEADLINE && r.deadlineUs == deadline);
        assert(r.executionUnknown == (deadline == 2000));
        assert(rig.fake.sent.size() == (deadline == 2000 ? 1 : 0));
        if (deadline == 2000) { assert(rig.runner.transmitEnabled()); rig.service(3000); }
        assert(!rig.runner.transmitEnabled());
        assert(rig.owner.cancel(id, 3000) == Cancel::ALREADY_TERMINAL);
        assert(done(rig, id).transport.reason == Reason::REQUEST_DEADLINE);
    }
    for (uint64_t deadline : {uint64_t(3500), uint64_t(4000)}) {
        Rig rig; uint8_t bytes[32]; RequestId id = admit(rig, request(bytes, 0, deadline)); rig.send();
        rig.fake.bytes(reply(), 2510); rig.fake.now = 4000;
        assert(rig.owner.cancel(id, 4000) == Cancel::ALREADY_TERMINAL);
        const Completion& r = done(rig, id);
        assert(r.transport.closureQualified && r.transport.closureLatestUs == 3560 && r.deadlineUs == deadline);
        assert(r.outcome == (deadline == 4000 ? Outcome::SUCCESS : Outcome::TRANSPORT));
        assert(r.transport.reason == (deadline == 4000 ? Reason::FRAME : Reason::REQUEST_DEADLINE));
        assert(r.executionUnknown == (deadline == 3500) && rig.fake.sent.size() == 1);
    }
    for (bool dispatchOnly : {false, true}) {
        Rig rig; uint8_t bytes[32]; BusRequest b = request(bytes, 0, dispatchOnly ? 4000 : 1200);
        if (dispatchOnly) b.dispatchDeadlineUs = 1200;
        RequestId id = admit(rig, b);
        assert(rig.owner.cancel(id, 1300) == Cancel::ALREADY_TERMINAL);
        assert(done(rig, id).outcome == (dispatchOnly ? Outcome::DISPATCH_EXPIRED : Outcome::QUEUE_EXPIRED));
        assert(done(rig, id).transport.reason == Reason::REQUEST_DEADLINE && !done(rig, id).executionUnknown);
        assert(rig.fake.sent.empty());
    }
}
void testLateCancelOverReadBudget() {
    for (bool onTime : {false, true}) {
        Rig rig; std::vector<uint8_t> frame(79, 0);
        frame[0] = 1; frame[1] = 0x10; frame[5] = 36; frame[6] = 72; appendCrc(frame);
        BusRequest b; b.wire.bytes = frame.data(); b.wire.length = frame.size(); b.wire.replyLength = 8;
        b.wire.responseTimeoutUs = 10000; b.wire.deadlineUs = 12000; b.wire.echo = Echo::REQUIRED;
        b.expected.address = 1; b.expected.function = 0x10; b.expected.count = 36;
        b.validator.checkRequest = checkLongRequest; b.validator.checkReply = checkLongReply;
        RequestId id = admit(rig, b); rig.send(); rig.fake.bytes(frame, 1360);
        std::vector<uint8_t> response = {1, 0x10, 0, 0, 0, 36}; appendCrc(response);
        rig.fake.bytes(response, onTime ? 9810 : 12000); rig.fake.now = 20000;
        const unsigned before = rig.fake.reads; rig.owner.cancel(id, 20000);
        assert(rig.fake.reads - before == READ_BUDGET && !rig.owner.result(id) && rig.owner.active());
        rig.service(20000);
        const Completion& r = done(rig, id);
        assert(r.deadlineUs == 12000 && r.transport.txAccepted == 81 && r.transport.echoBytes == 81);
        assert(r.transport.reason == (onTime ? Reason::FRAME : Reason::REQUEST_DEADLINE));
        assert(r.outcome == (onTime ? Outcome::SUCCESS : Outcome::TRANSPORT) && r.executionUnknown != onTime);
        assert(rig.fake.sent.size() == 1);
        if (onTime) {
            assert(r.transport.closureQualified && r.transport.closureLatestUs == 10960);
            assert(r.transport.rxLength == response.size() && std::memcmp(r.raw, response.data(), response.size()) == 0);
        }
    }
    struct CutoffCase { uint64_t responseStart; uint32_t width; Reason reason; Outcome outcome; };
    const CutoffCase cases[] = {
        {9810, 0, Reason::FRAME, Outcome::SUCCESS},
        {9890, 0, Reason::CANCELLED, Outcome::CANCELLED},
        {9860, 20, Reason::TIMING_UNCERTAIN, Outcome::TRANSPORT}
    };
    for (const CutoffCase& scenario : cases) {
        Rig rig; std::vector<uint8_t> frame(79, 0);
        frame[0] = 1; frame[1] = 0x10; frame[5] = 36; frame[6] = 72; appendCrc(frame);
        BusRequest b; b.wire.bytes = frame.data(); b.wire.length = frame.size(); b.wire.replyLength = 8;
        b.wire.responseTimeoutUs = 10000; b.wire.deadlineUs = 12000; b.wire.echo = Echo::REQUIRED;
        b.expected.address = 1; b.expected.function = 0x10; b.expected.count = 36;
        b.validator.checkRequest = checkLongRequest; b.validator.checkReply = checkLongReply;
        RequestId id = admit(rig, b); rig.send(); rig.fake.bytes(frame, 1360);
        std::vector<uint8_t> response = {1, 0x10, 0, 0, 0, 36}; appendCrc(response);
        rig.fake.bytes(response, scenario.responseStart); rig.fake.input.back().uncertaintyUs = scenario.width;
        rig.fake.now = 11000; const unsigned before = rig.fake.reads;
        assert(rig.owner.cancel(id, 11000) == Cancel::CANCELLED);
        assert(rig.fake.reads - before == READ_BUDGET && !rig.owner.result(id));
        // Repeating cancellation must preserve the first cutoff. At12000 all
        // three replies would otherwise fit the original absolute deadline.
        rig.fake.now = 12000; rig.owner.cancel(id, 12000);
        const Completion& r = done(rig, id);
        assert(r.transport.reason == scenario.reason && r.outcome == scenario.outcome);
        assert(r.cancellation == Cancellation::REQUEST && r.deadlineUs == 12000);
        assert(r.transport.closureQualified && r.transport.closureLatestUs == scenario.responseStart + 1150);
        assert(r.transport.closureEarliestUs == r.transport.closureLatestUs - scenario.width);
        assert(r.transport.rxLength == response.size() && std::memcmp(r.raw, response.data(), response.size()) == 0);
        assert(r.executionUnknown == (scenario.outcome != Outcome::SUCCESS) && rig.fake.sent.size() == 1);
    }
}
void testCancellationPrecedesLaterExpiry() {
    // Capture may legitimately remain PENDING at cancellation. Delayed service
    // must compare the immutable cutoff with deadlines, rather than today's time.
    struct Case {
        uint64_t deadline, cancelAt, serviceAt;
        uint32_t responseTimeout;
        Reason reason;
    };
    const Case silence[] = {
        {3500, 3000, 4000, 10000, Reason::CANCELLED},
        {100000, 3000, 4000, 1000, Reason::CANCELLED},
        {3500, 4000, 4500, 10000, Reason::REQUEST_DEADLINE},
        {100000, 3000, 4000, 700, Reason::NO_RESPONSE},
        {3500, 3500, 4000, 10000, Reason::REQUEST_DEADLINE},
        {100000, 3160, 4000, 1000, Reason::NO_RESPONSE}
    };
    for (const Case& scenario : silence) {
        Rig rig; uint8_t bytes[32]; BusRequest b = request(bytes, 0, scenario.deadline);
        b.wire.responseTimeoutUs = scenario.responseTimeout;
        RequestId id = admit(rig, b); rig.send(); rig.service(2180);
        rig.fake.pending = true; rig.fake.now = scenario.cancelAt;
        assert(rig.owner.cancel(id, scenario.cancelAt) == Cancel::CANCELLED);
        assert(!rig.owner.result(id) && rig.owner.active());
        rig.fake.pending = false; rig.service(scenario.serviceAt);
        const Completion& r = done(rig, id);
        assert(r.transport.reason == scenario.reason);
        assert(r.outcome == (scenario.reason == Reason::CANCELLED ? Outcome::CANCELLED : Outcome::TRANSPORT));
        assert(r.cancellation == Cancellation::REQUEST && r.executionUnknown);
        assert(r.deadlineUs == scenario.deadline && r.responseTimeoutUs == scenario.responseTimeout);
        assert(r.transport.rxLength == 0 && !r.transport.closureQualified && rig.fake.sent.size() == 1);
        const Completion retained = r;
        assert(rig.owner.cancel(id, scenario.serviceAt) == Cancel::ALREADY_TERMINAL);
        rig.service(scenario.serviceAt + 1000);
        assert(done(rig, id).transport.reason == retained.transport.reason &&
               done(rig, id).transport.endedUs == retained.transport.endedUs && rig.fake.sent.size() == 1);
    }
    // A complete response ending at3210 closes at3560. All bytes precede the
    // cancellation cutoff; the completed-frame path must select the earliest
    // limit, including uncertain closure and historical success.
    struct FrameCase {
        uint64_t deadline, cancelAt;
        uint32_t responseTimeout, width;
        Reason reason;
    };
    const FrameCase frames[] = {
        {3520, 3500, 10000, 0, Reason::CANCELLED},
        {100000, 3500, 1360, 0, Reason::CANCELLED},
        {3520, 3540, 10000, 0, Reason::REQUEST_DEADLINE},
        {100000, 3540, 1360, 0, Reason::PARTIAL_RESPONSE},
        {3600, 3550, 10000, 20, Reason::TIMING_UNCERTAIN},
        {100000, 3550, 1440, 20, Reason::TIMING_UNCERTAIN},
        {3600, 3570, 10000, 0, Reason::FRAME}
    };
    for (const FrameCase& scenario : frames) {
        Rig rig; uint8_t bytes[32]; BusRequest b = request(bytes, 0, scenario.deadline);
        b.wire.responseTimeoutUs = scenario.responseTimeout;
        RequestId id = admit(rig, b); rig.send(); rig.service(2180);
        const std::vector<uint8_t> response = reply(); rig.fake.bytes(response, 2510);
        rig.fake.input.back().uncertaintyUs = scenario.width;
        rig.fake.pending = true; rig.fake.now = scenario.cancelAt;
        assert(rig.owner.cancel(id, scenario.cancelAt) == Cancel::CANCELLED);
        assert(!rig.owner.result(id));
        rig.fake.pending = false; rig.service(5000);
        const Completion& r = done(rig, id);
        assert(r.transport.reason == scenario.reason && r.cancellation == Cancellation::REQUEST);
        assert(r.transport.closureQualified && r.transport.closureLatestUs == 3560 &&
               r.transport.closureEarliestUs == 3560 - scenario.width);
        assert(r.transport.rxLength == response.size() && std::memcmp(r.raw, response.data(), response.size()) == 0);
        assert(r.outcome == (scenario.reason == Reason::FRAME ? Outcome::SUCCESS :
                            scenario.reason == Reason::CANCELLED ? Outcome::CANCELLED : Outcome::TRANSPORT));
        assert(r.executionUnknown == (scenario.reason != Reason::FRAME) && rig.fake.sent.size() == 1);
    }
    // The first late byte crosses both limits. It cannot replace a prior cancel
    // with the later absolute/relative expiry, nor mask a deadline that was first.
    const Case lateBytes[] = {
        {3500, 3000, 5000, 10000, Reason::CANCELLED},
        {100000, 3000, 5000, 1340, Reason::CANCELLED},
        {3500, 4000, 5000, 10000, Reason::REQUEST_DEADLINE},
        {100000, 4000, 5000, 1340, Reason::NO_RESPONSE}
    };
    for (const Case& scenario : lateBytes) {
        Rig rig; uint8_t bytes[32]; BusRequest b = request(bytes, 0, scenario.deadline);
        b.wire.responseTimeoutUs = scenario.responseTimeout;
        RequestId id = admit(rig, b); rig.send(); rig.service(2180);
        rig.fake.bytes(reply(), 3500);
        rig.fake.pending = true; rig.fake.now = scenario.cancelAt;
        assert(rig.owner.cancel(id, scenario.cancelAt) == Cancel::CANCELLED);
        rig.fake.pending = false; rig.service(scenario.serviceAt);
        const Completion& r = done(rig, id);
        assert(r.transport.reason == scenario.reason && r.cancellation == Cancellation::REQUEST);
        assert(r.transport.rxLength == 0 && !r.transport.closureQualified);
        assert(r.transport.closureEarliestUs == 3950 && r.transport.closureLatestUs == 3950);
        assert(r.executionUnknown && rig.fake.sent.size() == 1 && rig.owner.needsRecovery());
    }
}
void testFairnessAndIsolation() {
    Rig rig; rig.fake.automaticReply = true; uint8_t bytes[32]; std::vector<RequestId> ids;
    for (unsigned n = 0; n < 3; ++n) ids.push_back(admit(rig, request(bytes, 0)));
    for (unsigned n = 0; n < 2; ++n) ids.push_back(admit(rig, request(bytes, 1, 100000, true)));
    ids.push_back(admit(rig, request(bytes, 2)));
    rig.until(ids[2]); rig.until(ids[4]); rig.until(ids[5]);
    const uint8_t order[] = {1, 2, 3, 1, 2, 1};
    assert(rig.fake.sent.size() == sizeof(order));
    for (std::size_t n = 0; n < sizeof(order); ++n) assert(rig.fake.sent[n][0] == order[n]);
    for (const RequestId& id : ids) {
        const Completion& r = done(rig, id);
        assert(r.outcome == Outcome::SUCCESS && r.expected.target == 100 + r.producer);
        assert(r.expected.targetGeneration == 9 && r.raw[0] == r.producer + 1);
        assert(r.raw[1] == (r.producer == 1 ? 4 : 3) && r.transport.rxLength == 7);
        assert(r.deadlineUs == 100000 && !r.executionUnknown);
    }
    for (bool badCrc : {false, true}) {
        Rig rejected; uint8_t bytes[32];
        RequestId motor = admit(rejected, request(bytes));
        RequestId other = admit(rejected, request(bytes, 1, 100000, true));
        rejected.send(); rejected.fake.bytes(reply(), 2510); rejected.service(4000);
        const Completion retained = done(rejected, motor);
        rejected.service(4350); rejected.service(4360);
        assert(rejected.fake.sent.size() == 2 && rejected.fake.sent[1][1] == 4);
        std::vector<uint8_t> response = reply(badCrc ? 2 : 1, 4);
        if (badCrc) response.back() ^= 1;
        rejected.fake.bytes(response, rejected.fake.txEnd + 350); rejected.service(7000);
        const Completion& r = done(rejected, other);
        assert(r.outcome == Outcome::INVALID_REPLY && r.executionUnknown && rejected.owner.needsRecovery());
        assert(r.validation.code == (badCrc ? MotorControlRS::Err::CRC_ERROR : MotorControlRS::Err::FRAME_ERROR));
        assert(r.producer == 1 && r.expected.function == 4 && std::memcmp(r.raw, response.data(), response.size()) == 0);
        assert(done(rejected, motor).outcome == Outcome::SUCCESS && !done(rejected, motor).executionUnknown);
        assert(done(rejected, motor).transport.endedUs == retained.transport.endedUs && done(rejected, motor).raw[1] == 3);
    }
}
void testUrgentReservationAndOrdering() {
    Rig rig(3, 3, 1, 1); uint8_t bytes[32]; rig.fake.automaticReply = true;
    RequestId a = admit(rig, request(bytes, 0)); rig.service(1000);
    RequestId b = admit(rig, request(bytes, 1));
    RequestId rejected; rejected.generation = 99;
    assert(rig.owner.admit(request(bytes, 2), 1000, rejected) == BusAdmission::RESULTS_FULL);
    RequestId urgent = admit(rig, request(bytes, 2), 1000, true);
    assert(rig.owner.admitUrgent(request(bytes), 1000, rejected) == BusAdmission::URGENT_FULL);
    assert(rejected.generation == 99 && rig.fake.sent.empty());
    rig.until(urgent); assert(rig.fake.sent.size() == 2 && rig.fake.sent[0][0] == 1 && rig.fake.sent[1][0] == 3);
    assert(done(rig, urgent).urgent && !done(rig, a).urgent);
    assert(rig.owner.admitUrgent(request(bytes), rig.fake.now, rejected) == BusAdmission::URGENT_FULL);
    rig.until(b); assert(rig.fake.sent.size() == 3 && rig.fake.sent.back()[0] == 2);
    assert(done(rig, a).outcome == Outcome::SUCCESS && done(rig, urgent).outcome == Outcome::SUCCESS);
    assert(rig.owner.release(urgent));
    RequestId next = admit(rig, request(bytes, 2), rig.fake.now, true); rig.until(next);
    assert(next.generation != urgent.generation && !rig.owner.result(urgent));
    Rig deferred; BusRequest wait = request(bytes); wait.notBeforeUs = 2000;
    assert(deferred.owner.admitUrgent(wait, 1000, rejected) != BusAdmission::ACCEPTED);
    assert(deferred.fake.sent.empty() && deferred.owner.pending() == 0);
    Rig noReserve(2, 2, 0, 0);
    assert(noReserve.owner.admitUrgent(request(bytes), 1000, rejected) == BusAdmission::URGENT_FULL);
    Rig queuePressure(3, 6, 1, 1); queuePressure.fake.automaticReply = true;
    RequestId ordinaryA = admit(queuePressure, request(bytes, 0));
    RequestId ordinaryB = admit(queuePressure, request(bytes, 1));
    assert(queuePressure.owner.admit(request(bytes), 1000, rejected) == BusAdmission::QUEUE_FULL);
    RequestId reserved = admit(queuePressure, request(bytes, 2), 1000, true);
    queuePressure.until(reserved); queuePressure.until(ordinaryA); queuePressure.until(ordinaryB);
    assert(queuePressure.fake.sent.size() == 3 && queuePressure.fake.sent.front()[0] == 3);
    Rig urgentFifo; urgentFifo.fake.automaticReply = true;
    RequestId normal = admit(urgentFifo, request(bytes));
    RequestId urgentA = admit(urgentFifo, request(bytes, 2), 1000, true);
    RequestId urgentB = admit(urgentFifo, request(bytes, 1), 1000, true);
    urgentFifo.until(normal);
    assert(done(urgentFifo, urgentA).outcome == Outcome::SUCCESS && done(urgentFifo, urgentB).outcome == Outcome::SUCCESS);
    assert(urgentFifo.fake.sent.size() == 3 && urgentFifo.fake.sent[0][0] == 3 && urgentFifo.fake.sent[1][0] == 2);
}
void testStopShapedUrgentScheduling() {
    Rig rig; rig.fake.automaticReply = true; uint8_t bytes[32];
    RequestId active = admit(rig, request(bytes, 0, 50000)); rig.service(1000);
    BusRequest wait = request(bytes, 1, 50000, true); wait.notBeforeUs = 15000;
    RequestId deferred = admit(rig, wait);
    RequestId ordinary = admit(rig, request(bytes, 2, 50000));
    std::vector<uint8_t> stopWire = {3, 6, 0xFF, 0, 0, 0}; appendCrc(stopWire);
    BusRequest stop; stop.producer = 2; stop.wire.bytes = stopWire.data(); stop.wire.length = stopWire.size();
    stop.wire.replyLength = 8; stop.wire.responseTimeoutUs = 10000; stop.wire.deadlineUs = 50000;
    stop.expected.address = 3; stop.expected.function = 6; stop.expected.first = 0xFF00; stop.expected.count = 1;
    stop.expected.target = 777; stop.expected.targetGeneration = 13;
    stop.validator.checkRequest = checkStopShape; stop.validator.checkReply = checkStopAck;
    RequestId urgent = admit(rig, stop, 1000, true); rig.until(urgent); rig.until(ordinary);
    assert(rig.fake.sent.size() == 3 && rig.fake.sent[0][1] == 3 && rig.fake.sent[1] == stopWire && rig.fake.sent[2][1] == 3);
    const Completion& result = done(rig, urgent);
    assert(result.outcome == Outcome::SUCCESS && result.urgent && result.transport.rxLength == 8 && result.transport.echoBytes == 0);
    assert(std::memcmp(result.raw, stopWire.data(), stopWire.size()) == 0);
    assert(result.expected.target == 777 && result.expected.targetGeneration == 13 && result.expected.first == 0xFF00);
    assert(done(rig, ordinary).expected.target == 102 && done(rig, ordinary).raw[1] == 3);
    assert(done(rig, active).expected.target == 100 && !rig.owner.result(deferred));
    rig.service(14999); assert(rig.fake.sent.size() == 3 && !rig.owner.active());
    rig.until(deferred, 50000); assert(done(rig, deferred).outcome == Outcome::SUCCESS && done(rig, deferred).raw[1] == 4);
    assert(rig.fake.sent.size() == 4 && done(rig, urgent).transport.rxLength == 8);
}
void testDeferredAndDispatchExpiry() {
    Rig rig; rig.fake.automaticReply = true; uint8_t bytes[32];
    BusRequest wait = request(bytes, 0, 30000); wait.notBeforeUs = 15000;
    RequestId deferred = admit(rig, wait);
    RequestId sameProducer = admit(rig, request(bytes, 0, 30000));
    RequestId otherProducer = admit(rig, request(bytes, 1, 30000, true));
    rig.until(otherProducer); assert(rig.fake.sent.size() == 1 && rig.fake.sent[0][0] == 2);
    rig.service(14999); assert(!rig.owner.active() && !rig.owner.result(deferred) && rig.fake.sent.size() == 1);
    rig.until(sameProducer, 30000); assert(done(rig, deferred).outcome == Outcome::SUCCESS);
    assert(done(rig, deferred).notBeforeUs == 15000 && done(rig, deferred).deadlineUs == 30000);
    assert(rig.fake.sent.size() == 3 && rig.fake.sent[1][0] == 1 && rig.fake.sent[2][0] == 1);
    Rig expiry; wait = request(bytes, 0, 20000); wait.notBeforeUs = 10000; wait.dispatchDeadlineUs = 12000;
    RequestId early = admit(expiry, wait); expiry.service(12000);
    assert(done(expiry, early).outcome == Outcome::DISPATCH_EXPIRED && done(expiry, early).deadlineUs == 20000);
    assert(done(expiry, early).dispatchDeadlineUs == 12000 && !done(expiry, early).executionUnknown && expiry.fake.sent.empty());
    Rig absolute; wait = request(bytes, 0, 15000); wait.notBeforeUs = 10000;
    RequestId late = admit(absolute, wait); absolute.service(15000);
    assert(done(absolute, late).outcome == Outcome::QUEUE_EXPIRED && done(absolute, late).deadlineUs == 15000);
    assert(absolute.fake.sent.empty());
    Rig cancelled; wait = request(bytes); wait.notBeforeUs = 20000;
    RequestId cancel = admit(cancelled, wait); assert(cancelled.owner.cancel(cancel, 1001) == Cancel::CANCELLED);
    cancelled.service(30000); assert(done(cancelled, cancel).outcome == Outcome::CANCELLED && cancelled.fake.sent.empty());
}
void testSequenceInvalidation() {
    Rig rig; uint8_t bytes[32]; SequenceId sequence;
    assert(rig.owner.beginSequence(0, 30000, 1000, sequence));
    BusRequest step = request(bytes, 0, 30000); step.sequence = sequence; step.notBeforeUs = 15000;
    RequestId first = admit(rig, step); RequestId second = admit(rig, step);
    RequestId independent = admit(rig, request(bytes, 0, 30000));
    assert(rig.owner.cancel(first, 1001) == Cancel::CANCELLED);
    assert(done(rig, first).outcome == Outcome::CANCELLED && done(rig, second).outcome == Outcome::CANCELLED);
    assert(done(rig, first).cancellation == Cancellation::REQUEST);
    assert(done(rig, second).cancellation == Cancellation::REQUEST);
    assert(!rig.owner.result(independent) && rig.owner.pending() == 1);
    RequestId rejected; rejected.generation = 99;
    assert(rig.owner.admit(step, 1001, rejected) != BusAdmission::ACCEPTED && rejected.generation == 99);
    assert(!rig.owner.invalidate(sequence, 1001));
    assert(rig.owner.cancel(independent, 1001) == Cancel::CANCELLED);
    SequenceId fresh; assert(rig.owner.beginSequence(0, 30000, 1001, fresh));
    assert(fresh.generation != sequence.generation);
    step.sequence = fresh; step.wire.deadlineUs = 31000;
    assert(rig.owner.admit(step, 1001, rejected) != BusAdmission::ACCEPTED); // No continuation deadline renewal.
    step.wire.deadlineUs = 30000; RequestId next = admit(rig, step, 1001);
    RequestId standalone = admit(rig, request(bytes, 0, 30000), 1001);
    assert(rig.owner.invalidateProducer(0, 1002));
    assert(done(rig, next).cancellation == Cancellation::GENERATION);
    assert(done(rig, standalone).cancellation == Cancellation::GENERATION);
    assert(done(rig, independent).cancellation == Cancellation::REQUEST);
    rig.service(40000); assert(rig.fake.sent.empty());
    assert(done(rig, first).deadlineUs == 30000 && done(rig, first).sequence.generation == sequence.generation);
    Rig settling; SequenceId initial;
    assert(settling.owner.beginSequence(0, 30000, 1000, initial));
    step = request(bytes, 0, 30000); step.sequence = initial;
    RequestId active = admit(settling, step); settling.send();
    assert(settling.owner.cancel(active, 1400) == Cancel::CANCELLED);
    assert(settling.owner.cancel(active, 1500) == Cancel::CANCELLED);
    settling.service(2180); assert(done(settling, active).cancellation == Cancellation::REQUEST);
    SequenceId replacement;
    assert(settling.owner.beginSequence(0, 30000, 2180, replacement));
    assert(replacement.generation == initial.generation + 2); // Cancel once, begin once.
}
void testRecoveryPressureAndSettlement() {
    Rig rig(3, 3, 1, 1); uint8_t bytes[32];
    SequenceId sequence; assert(rig.owner.beginSequence(0, 30000, 1000, sequence));
    BusRequest sequenced = request(bytes, 0, 30000); sequenced.sequence = sequence;
    RequestId active = admit(rig, sequenced); rig.send();
    RequestId queued = admit(rig, request(bytes, 1), 1360);
    RequestId urgent = admit(rig, request(bytes, 2), 1360, true);
    uint64_t recovery = 0; assert(rig.owner.recover(1400, 8000, recovery) == RecoveryAdmission::ACCEPTED);
    assert(rig.owner.recovering() && !rig.owner.recoveryResult(recovery));
    assert(done(rig, queued).cancellation == Cancellation::RECOVERY && done(rig, urgent).cancellation == Cancellation::RECOVERY);
    assert(!rig.owner.result(active) && rig.runner.transmitEnabled());
    assert(rig.owner.cancel(active, 1500) == Cancel::CANCELLED);
    assert(rig.owner.invalidateProducer(0, 1600));
    uint64_t rejected = 99;
    assert(rig.owner.recover(1600, 9000, rejected) == RecoveryAdmission::RESULTS_FULL && rejected == 99);
    rig.service(1800); assert(rig.runner.transmitEnabled() && !rig.owner.recoveryResult(recovery));
    // Stale captured traffic must be boundedly discarded before recovery opens admission.
    rig.fake.bytes(reply(), 2510); rig.service(5000); rig.recovered(recovery);
    const RecoveryResult& recovered = *rig.owner.recoveryResult(recovery);
    assert(recovered.outcome == RecoveryOutcome::RECOVERED && recovered.id == recovery && recovered.deadlineUs == 8000);
    assert(recovered.interrupted.generation == active.generation && recovered.interrupted.owner == active.owner);
    assert(done(rig, active).outcome == Outcome::CANCELLED && done(rig, active).executionUnknown);
    assert(done(rig, active).cancellation == Cancellation::RECOVERY);
    assert(!rig.owner.recovering() && !rig.owner.needsRecovery() && !rig.runner.transmitEnabled());
    assert(rig.fake.next == rig.fake.input.size() && rig.fake.sent.size() == 1);
    const Completion retained = done(rig, active); const RecoveryResult snapshot = recovered;
    rig.service(10000); assert(rig.fake.sent.size() == 1 && done(rig, active).transport.endedUs == retained.transport.endedUs);
    assert(rig.owner.recoveryResult(recovery)->finishedUs == snapshot.finishedUs);
    assert(rig.owner.recover(10000, 20000, rejected) == RecoveryAdmission::RESULTS_FULL);
    assert(rig.owner.releaseRecovery(recovery) && !rig.owner.releaseRecovery(recovery));
    assert(!rig.owner.recoveryResult(recovery));
    RequestId stale; assert(rig.owner.admit(sequenced, 10000, stale) != BusAdmission::ACCEPTED);
    assert(rig.owner.release(queued)); rig.fake.automaticReply = true;
    RequestId fresh = admit(rig, request(bytes, 1), 10000); rig.until(fresh);
    assert(done(rig, fresh).outcome == Outcome::SUCCESS && rig.fake.sent.size() == 2);
    assert(done(rig, active).executionUnknown && done(rig, active).deadlineUs == 30000);
}
void testRecoveryFailuresAndExpiry() {
    {
        Rig rig; uint64_t id = 0; rig.fake.readError = true;
        assert(rig.owner.recover(1000, 10000, id) == RecoveryAdmission::ACCEPTED); rig.service(1000);
        assert(rig.owner.recoveryResult(id)->outcome == RecoveryOutcome::READ_ERROR && rig.owner.needsRecovery());
        const RecoveryResult snapshot = *rig.owner.recoveryResult(id); rig.fake.readError = false; rig.service(2000);
        assert(rig.owner.recoveryResult(id)->outcome == snapshot.outcome && rig.owner.needsRecovery());
        assert(rig.owner.releaseRecovery(id)); uint64_t next = 0;
        assert(rig.owner.recover(2000, 10000, next) == RecoveryAdmission::ACCEPTED); rig.recovered(next);
        assert(next != id && rig.owner.recoveryResult(next)->outcome == RecoveryOutcome::RECOVERED);
    }
    {
        Rig rig; rig.fake.automaticRelease = false; rig.fake.releaseError = true; uint64_t id = 0;
        assert(rig.owner.recover(1000, 10000, id) == RecoveryAdmission::ACCEPTED); rig.service(1000); rig.recovered(id);
        assert(rig.owner.recoveryResult(id)->outcome == RecoveryOutcome::TRANSPORT_ERROR && rig.owner.needsRecovery());
        rig.fake.releaseError = false; rig.service(2000); assert(rig.owner.needsRecovery());
    }
    {
        Rig rig; uint8_t bytes[32]; RequestId active = admit(rig, request(bytes)); rig.send(); rig.fake.busy = true;
        uint64_t id = 0; assert(rig.owner.recover(1400, 1800, id) == RecoveryAdmission::ACCEPTED);
        rig.service(1800); assert(rig.owner.recoveryResult(id)->outcome == RecoveryOutcome::EXPIRED);
        assert(rig.owner.needsRecovery() && rig.runner.transmitEnabled() && rig.fake.sent.size() == 1);
        rig.fake.busy = false; rig.service(3000); assert(!rig.runner.transmitEnabled());
        assert(done(rig, active).executionUnknown && rig.owner.recoveryResult(id)->outcome == RecoveryOutcome::EXPIRED);
    }
    {
        Rig rig; uint64_t id = 0;
        for (unsigned i = 0; i <= READ_BUDGET; ++i) {
            RxByte byte; byte.startUs = i; byte.endUs = i + 1; byte.value = 0xA5; rig.fake.input.push_back(byte);
        }
        assert(rig.owner.recover(1000, 10000, id) == RecoveryAdmission::ACCEPTED);
        const unsigned before = rig.fake.reads; rig.service(1000);
        assert(rig.fake.reads - before == READ_BUDGET && !rig.owner.recoveryResult(id));
        const unsigned after = rig.fake.reads; rig.service(1000);
        assert(rig.fake.reads - after <= READ_BUDGET);
        rig.recovered(id);
        assert(rig.owner.recoveryResult(id)->outcome == RecoveryOutcome::RECOVERED && rig.fake.next == READ_BUDGET + 1);
    }
}
void testRecoveryRejectsContradictoryDrainEvidence() {
    for (bool regressedByte : {false, true}) {
        Rig rig;
        // A read-budget boundary must not reset the last accepted wire time.
        for (unsigned i = 0; i < READ_BUDGET; ++i) {
            RxByte byte; byte.startUs = 500 + i * 2; byte.endUs = byte.startUs + 1;
            byte.value = 0xA5; rig.fake.input.push_back(byte);
        }
        if (regressedByte) {
            RxByte byte; byte.startUs = 1; byte.endUs = 2; byte.value = 0xEE;
            rig.fake.input.push_back(byte);
        }
        uint64_t id = 0;
        assert(rig.owner.recover(1000, 10000, id) == RecoveryAdmission::ACCEPTED);
        const unsigned before = rig.fake.reads; rig.service(1000);
        assert(rig.fake.reads - before == READ_BUDGET && !rig.owner.recoveryResult(id));
        if (!regressedByte) rig.fake.through = 626; // Last accepted byte ended at627.
        const unsigned second = rig.fake.reads; rig.service(1350);
        const RecoveryResult* r = rig.owner.recoveryResult(id); assert(r);
        assert(r->outcome == RecoveryOutcome::READ_ERROR && r->reason == Reason::CLOCK_ERROR);
        assert(rig.fake.reads - second <= READ_BUDGET && rig.owner.needsRecovery() && rig.fake.sent.empty());
        const RecoveryResult retained = *r;
        rig.fake.through = std::numeric_limits<uint64_t>::max(); rig.service(2000);
        assert(rig.owner.recoveryResult(id)->outcome == retained.outcome &&
               rig.owner.recoveryResult(id)->finishedUs == retained.finishedUs && rig.owner.needsRecovery());
    }
    {
        Rig rig; uint64_t id = 0;
        assert(rig.owner.recover(1000, 10000, id) == RecoveryAdmission::ACCEPTED);
        rig.fake.through = 1200; rig.service(1200);
        assert(!rig.owner.recoveryResult(id)); // Recovery quiet guard has not elapsed.
        rig.fake.through = 1100; rig.service(1250); // Contradicts the prior EMPTY watermark.
        const RecoveryResult* r = rig.owner.recoveryResult(id); assert(r);
        assert(r->outcome == RecoveryOutcome::READ_ERROR && r->reason == Reason::CLOCK_ERROR);
        assert(rig.owner.needsRecovery() && rig.fake.sent.empty());
    }
}
void testRequestCancellationSurvivesRecoveryExpiry() {
    for (bool releaseResult : {false, true}) {
        Rig rig; uint8_t bytes[32]; RequestId active = admit(rig, request(bytes)); rig.send();
        rig.fake.busy = true; rig.fake.now = 1400;
        assert(rig.owner.cancel(active, 1400) == Cancel::CANCELLED);
        assert(!rig.owner.result(active) && rig.runner.transmitEnabled());
        uint64_t recovery = 0;
        assert(rig.owner.recover(1500, 1600, recovery) == RecoveryAdmission::ACCEPTED);
        rig.service(1600);
        const RecoveryResult* r = rig.owner.recoveryResult(recovery); assert(r);
        assert(r->outcome == RecoveryOutcome::EXPIRED && r->interrupted.generation == active.generation);
        assert(rig.owner.needsRecovery() && rig.runner.transmitEnabled() && !rig.owner.result(active));
        const RecoveryResult retained = *r;
        if (releaseResult) assert(rig.owner.releaseRecovery(recovery));
        RequestId urgent = admit(rig, request(bytes, 2), 1600, true);
        rig.fake.busy = false; rig.service(2180);
        const Completion& completed = done(rig, active);
        assert(completed.outcome == Outcome::CANCELLED && completed.transport.reason == Reason::CANCELLED);
        assert(completed.cancellation == Cancellation::REQUEST && completed.executionUnknown);
        assert(completed.deadlineUs == 100000 && completed.transport.txAccepted == 8);
        assert(rig.owner.needsRecovery() && !rig.runner.transmitEnabled() && !rig.owner.result(urgent));
        assert(rig.owner.cancel(active, 3000) == Cancel::ALREADY_TERMINAL); rig.service(3000);
        assert(done(rig, active).cancellation == Cancellation::REQUEST && done(rig, active).executionUnknown);
        assert(rig.fake.sent.size() == 1 && !rig.owner.result(urgent));
        if (!releaseResult) assert(rig.owner.recoveryResult(recovery)->outcome == retained.outcome &&
                                  rig.owner.recoveryResult(recovery)->finishedUs == retained.finishedUs);
    }
}
void testUrgentCannotBypassRecovery() {
    Rig rig; uint8_t bytes[32]; RequestId expired = admit(rig, request(bytes, 0, 2000)); rig.send();
    RequestId urgent = admit(rig, request(bytes, 2), 1360, true);
    rig.service(2000); rig.service(3000);
    assert(done(rig, expired).transport.reason == Reason::REQUEST_DEADLINE && done(rig, expired).executionUnknown);
    assert(!rig.owner.result(urgent) && rig.owner.pending() == 1 && rig.owner.needsRecovery() && rig.fake.sent.size() == 1);
    uint64_t recovery = 0; assert(rig.owner.recover(3000, 10000, recovery) == RecoveryAdmission::ACCEPTED);
    rig.recovered(recovery);
    assert(done(rig, urgent).cancellation == Cancellation::RECOVERY && !done(rig, urgent).executionUnknown);
    assert(rig.owner.recoveryResult(recovery)->outcome == RecoveryOutcome::RECOVERED && rig.fake.sent.size() == 1);
}
void testExpiredRecoveryCannotReopenThroughHistoricalSuccess() {
    for (bool releaseResult : {false, true}) {
        Rig rig; std::vector<uint8_t> frame(79, 0);
        frame[0] = 1; frame[1] = 0x10; frame[5] = 36; frame[6] = 72; appendCrc(frame);
        BusRequest b; b.wire.bytes = frame.data(); b.wire.length = frame.size(); b.wire.replyLength = 8;
        b.wire.responseTimeoutUs = 10000; b.wire.deadlineUs = 12000; b.wire.echo = Echo::REQUIRED;
        b.expected.address = 1; b.expected.function = 0x10; b.expected.count = 36;
        b.validator.checkRequest = checkLongRequest; b.validator.checkReply = checkLongReply;
        RequestId old = admit(rig, b); rig.send(); rig.fake.bytes(frame, 1360);
        std::vector<uint8_t> response = {1, 0x10, 0, 0, 0, 36}; appendCrc(response);
        rig.fake.bytes(response, 9810); rig.fake.now = 20000;
        uint64_t recovery = 0;
        assert(rig.owner.recover(20000, 20001, recovery) == RecoveryAdmission::ACCEPTED);
        assert(rig.owner.active() && !rig.owner.result(old));
        rig.fake.pending = true; rig.service(20001);
        assert(rig.owner.recoveryResult(recovery)->outcome == RecoveryOutcome::EXPIRED && rig.owner.needsRecovery());
        if (releaseResult) assert(rig.owner.releaseRecovery(recovery));
        uint8_t bytes[32]; RequestId urgent = admit(rig, request(bytes, 2), 20001, true);
        rig.fake.pending = false; rig.service(20002);
        // Historical success retains its original context; it cannot complete
        // a failed recovery or reopen dispatch after the recovery result is released.
        assert(done(rig, old).outcome == Outcome::SUCCESS && done(rig, old).transport.closureLatestUs == 10960);
        assert(!done(rig, old).executionUnknown && done(rig, old).deadlineUs == 12000);
        assert(rig.owner.needsRecovery() && !rig.owner.active() && !rig.owner.result(urgent));
        rig.service(21000); assert(rig.fake.sent.size() == 1 && rig.owner.pending() == 1);
        if (!releaseResult) assert(rig.owner.recoveryResult(recovery)->outcome == RecoveryOutcome::EXPIRED);
    }
}
void testRecoveryDoesNotDoubleReadBudgetAfterCompletion() {
    Rig rig; std::vector<uint8_t> frame(79, 0);
    frame[0] = 1; frame[1] = 0x10; frame[5] = 36; frame[6] = 72; appendCrc(frame);
    BusRequest b; b.wire.bytes = frame.data(); b.wire.length = frame.size(); b.wire.replyLength = 8;
    b.wire.responseTimeoutUs = 10000; b.wire.deadlineUs = 12000; b.wire.echo = Echo::REQUIRED;
    b.expected.address = 1; b.expected.function = 0x10; b.expected.count = 36;
    b.validator.checkRequest = checkLongRequest; b.validator.checkReply = checkLongReply;
    RequestId old = admit(rig, b); rig.send(); rig.fake.bytes(frame, 1360);
    std::vector<uint8_t> response = {1, 0x10, 0, 0, 0, 36}; appendCrc(response);
    rig.fake.bytes(response, 9810); rig.fake.bytes(std::vector<uint8_t>(READ_BUDGET + 1, 0xEE), 12000);
    uint64_t recovery = 0; rig.fake.now = 20000;
    assert(rig.owner.recover(20000, 30000, recovery) == RecoveryAdmission::ACCEPTED);
    const unsigned before = rig.fake.reads; rig.service(20000);
    assert(rig.fake.reads - before == 26); // 17 echo + 8 reply + next-frame boundary.
    assert(done(rig, old).outcome == Outcome::SUCCESS && !rig.owner.recoveryResult(recovery));
    const unsigned after = rig.fake.reads; rig.service(20000);
    assert(rig.fake.reads - after == READ_BUDGET && !rig.owner.recoveryResult(recovery));
    rig.recovered(recovery, 30000);
    assert(rig.owner.recoveryResult(recovery)->outcome == RecoveryOutcome::RECOVERED);
    assert(rig.fake.next == rig.fake.input.size() && rig.fake.sent.size() == 1);
}
void testLateIdenticalReplyLimitation() {
    Rig rig; uint8_t bytes[32]; RequestId old = admit(rig, request(bytes)); rig.send();
    assert(rig.owner.cancel(old, 1400) == Cancel::CANCELLED); rig.service(2180);
    assert(done(rig, old).executionUnknown);
    rig.fake.bytes(reply(), 2510); uint64_t recovery = 0;
    rig.fake.now = 4000;
    assert(rig.owner.recover(4000, 10000, recovery) == RecoveryAdmission::ACCEPTED); rig.service(4000); rig.recovered(recovery);
    assert(rig.fake.next == rig.fake.input.size());
    const uint64_t at = rig.fake.now; RequestId fresh = admit(rig, request(bytes), at);
    rig.service(at); rig.service(at + 350); rig.service(at + 360);
    assert(rig.fake.sent.size() == 2);
    // RTU supplies no wire ID: this could be the old drive response, and the
    // checked parser cannot distinguish it once it falls in the new interval.
    rig.fake.bytes(reply(), rig.fake.txEnd + 350); rig.until(fresh);
    assert(done(rig, fresh).outcome == Outcome::SUCCESS && done(rig, old).executionUnknown);
    assert(done(rig, old).outcome == Outcome::CANCELLED && rig.fake.sent.size() == 2);
}
} // namespace
int main() {
    testScheduleBounds(); testCancellationPhases(); testDeadlineCancellation(); testLateCancelOverReadBudget();
    testCancellationPrecedesLaterExpiry(); testFairnessAndIsolation();
    testUrgentReservationAndOrdering(); testStopShapedUrgentScheduling(); testDeferredAndDispatchExpiry(); testSequenceInvalidation();
    testRecoveryPressureAndSettlement(); testRecoveryFailuresAndExpiry();
    testRecoveryRejectsContradictoryDrainEvidence(); testRequestCancellationSurvivesRecoveryExpiry(); testUrgentCannotBypassRecovery();
    testExpiredRecoveryCannotReopenThroughHistoricalSuccess(); testRecoveryDoesNotDoubleReadBudgetAfterCompletion();
    testLateIdenticalReplyLimitation();
    return 0;
}
