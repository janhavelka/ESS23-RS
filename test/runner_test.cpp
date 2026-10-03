// SPDX-License-Identifier: MIT
#include "../examples/common/RtuRunner.h"
#include "MotorControlRS/profiles/ess_rs/Codec.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

using namespace MotorControlRSExample::Rtu;
namespace Ess = MotorControlRS::ESS_RS;

namespace {

// Independent complete wire fixtures. The model is deliberately left raw.
const uint8_t PROBE[] = {1, 3, 0, 0, 0, 1, 0x84, 0x0A};
const uint8_t REPLY[] = {1, 3, 2, 0, 0x3C, 0xB8, 0x55};
const uint8_t EXCEPTION[] = {1, 0x83, 2, 0xC0, 0xF1};
const uint8_t WRITE[] = {1, 6, 0, 0x23, 0, 0x3C, 0x78, 0x11};

struct DirectionChange {
    uint64_t at;
    bool transmit;
    DirectionChange(uint64_t time, bool enabled) : at(time), transmit(enabled) {}
};

// Allocation belongs to this native test fixture, never to the runner. RX times
// represent the actual wire, independently of when the owner services poll().
struct Fake {
    uint64_t now = 0;
    uint64_t txEnd = 0;
    uint64_t txDuration = 800;
    uint32_t txUncertainty = 0;
    bool autoRelease = false;
    uint64_t releaseAt = 0; // Zero selects TX end + 20 us in the independent fixture.
    uint32_t releaseUncertainty = 0;
    bool forceReleaseEvidence = false;
    uint64_t captureThrough = std::numeric_limits<uint64_t>::max();
    std::size_t acceptLimit = MAX_FRAME;
    std::size_t readIndex = 0;
    unsigned writes = 0, reads = 0, txChecks = 0;
    bool queued = false, writeError = false, txError = false;
    bool enableError = false, disableError = false, readError = false;
    bool holdBusy = false, capturePending = false;
    std::vector<uint8_t> written;
    std::vector<RxByte> input;
    std::vector<DirectionChange> directions;

    void bytes(const uint8_t* data, std::size_t count, uint64_t start,
               uint64_t silence = 0) {
        for (std::size_t i = 0; i < count; ++i) {
            RxByte byte;
            byte.startUs = start;
            byte.endUs = start + 100;
            byte.value = data[i];
            input.push_back(byte);
            start = byte.endUs + silence;
        }
    }

    static bool direction(void* context, bool enabled) {
        Fake& self = *static_cast<Fake*>(context);
        self.directions.push_back(DirectionChange(self.now, enabled));
        return !(enabled ? self.enableError : self.disableError);
    }

    static WriteResult write(void* context, const uint8_t* bytes, std::size_t count) {
        Fake& self = *static_cast<Fake*>(context);
        ++self.writes;
        const std::size_t accepted = std::min(count, self.acceptLimit);
        self.written.assign(bytes, bytes + accepted);
        self.queued = accepted != 0;
        self.txEnd = self.now + (accepted ? self.txDuration : 0);
        return WriteResult(accepted, self.writeError);
    }

    static TxState tx(void* context, uint64_t now, TxObservation& observation) {
        Fake& self = *static_cast<Fake*>(context);
        ++self.txChecks;
        if (self.txError) return TxState::ERROR;
        if (self.holdBusy || (self.queued && now < self.txEnd)) return TxState::BUSY;
        observation.endedUs = self.txEnd;
        observation.uncertaintyUs = self.txUncertainty;
        const uint64_t released = self.releaseAt ? self.releaseAt : self.txEnd + 20;
        if (self.autoRelease && self.queued && (now >= released || self.forceReleaseEvidence)) {
            observation.released = true;
            observation.releasedUs = released;
            observation.releaseUncertaintyUs = self.releaseUncertainty;
        }
        return TxState::IDLE;
    }

    static ReadState read(void* context, uint64_t now, RxByte& byte, uint64_t& through) {
        Fake& self = *static_cast<Fake*>(context);
        ++self.reads;
        if (self.readError) return ReadState::ERROR;
        if (self.capturePending) return ReadState::PENDING;
        if (self.readIndex < self.input.size()) {
            const RxByte& next = self.input[self.readIndex];
            if (next.endUs <= now && next.endUs <= self.captureThrough) {
                byte = next;
                ++self.readIndex;
                return ReadState::BYTE;
            }
            // A started but incomplete character must not look like silence.
            through = std::min(now, self.captureThrough);
            if (next.startUs < through) through = next.startUs;
            return ReadState::EMPTY;
        }
        through = std::min(now, self.captureThrough);
        return ReadState::EMPTY;
    }

};

Port makePort(Fake& fake) {
    Port port;
    port.context = &fake;
    port.setTransmit = Fake::direction;
    port.write = Fake::write;
    port.txState = Fake::tx;
    port.read = Fake::read;
    return port;
}

Timing makeTiming() {
    Timing timing;
    timing.gap15Us = 150;
    timing.gap35Us = 350;
    timing.setupUs = 10;
    timing.holdUs = 20;
    timing.busTimeoutUs = 5000;
    timing.txTimeoutUs = 5000;
    timing.captureTimeoutUs = 15000;
    return timing;
}

Storage makeStorage(uint8_t* tx, uint8_t* rx, Trace* trace,
                    std::size_t rxCapacity, std::size_t traceCapacity) {
    Storage storage;
    storage.tx = tx;
    storage.txCapacity = 32;
    storage.rx = rx;
    storage.rxCapacity = rxCapacity;
    storage.trace = traceCapacity ? trace : nullptr;
    storage.traceCapacity = traceCapacity;
    return storage;
}

Request request(const uint8_t* bytes = PROBE, std::size_t size = sizeof(PROBE),
                std::size_t replyLength = sizeof(REPLY), Echo echo = Echo::NONE) {
    Request result;
    result.bytes = bytes;
    result.length = size;
    result.replyLength = replyLength;
    result.responseTimeoutUs = 10000;
    result.echo = echo;
    return result;
}

struct Rig {
    Fake fake;
    uint8_t tx[32] = {};
    uint8_t rx[64] = {};
    Trace trace[64];
    Runner runner;

    explicit Rig(Timing timing = makeTiming(), std::size_t rxCapacity = 64,
                 std::size_t traceCapacity = 64)
        : runner(makePort(fake), makeStorage(tx, rx, trace, rxCapacity, traceCapacity), timing) {}

    void poll(uint64_t at, unsigned times = 1) {
        fake.now = at;
        for (unsigned i = 0; i < times; ++i) runner.poll(at);
    }

    void start(const Request& value = request(), uint64_t at = 1000) {
        fake.now = at;
        assert(runner.start(value, at) == Admission::STARTED);
        poll(at, 4);
    }

    void send() {
        poll(1349, 4);
        assert(fake.writes == 0);
        assert(!runner.transmitEnabled());
        poll(1350, 4);
        assert(runner.transmitEnabled());
        poll(1359, 4);
        assert(fake.writes == 0);
        poll(1360, 4);
        assert(fake.writes == 1);
    }

    void receive() {
        poll(2159, 4);
        assert(runner.transmitEnabled());
        poll(2160, 4);
        assert(runner.transmitEnabled());
        poll(2179, 4);
        assert(runner.transmitEnabled());
        poll(2180, 4);
        assert(!runner.transmitEnabled());
        assert(runner.phase() == Phase::RECEIVE);
    }
};

void testTiming() {
    struct Case { uint32_t baud; uint8_t bits; uint32_t gap15, gap35; };
    const Case cases[] = {
        {9600, 10, 1563, 3646}, {9600, 11, 1719, 4011},
        {19200, 10, 782, 1823}, {19200, 11, 860, 2006},
        {38400, 10, 750, 1750}, {115200, 11, 750, 1750}
    };
    for (const Case& value : cases) {
        Timing timing = makeTiming();
        assert(setRtuTiming(value.baud, value.bits, timing));
        assert(timing.gap15Us == value.gap15 && timing.gap35Us == value.gap35);
        assert(timing.setupUs == 10 && timing.holdUs == 20);
        assert(timing.txTimeoutUs == 5000 && timing.busTimeoutUs == 5000);
    }
    Timing timing = makeTiming();
    assert(!setRtuTiming(0, 10, timing));
    assert(!setRtuTiming(9600, 9, timing));
    assert(!setRtuTiming(9600, 12, timing));
    assert(timing.gap15Us == 150 && timing.gap35Us == 350);
}

void testProbeAndOwnership() {
    Rig rig;
    assert(rig.fake.directions.empty() && rig.fake.writes == 0 && rig.fake.reads == 0);
    uint8_t built[sizeof(PROBE)];
    assert(Ess::buildProbe(1, built, sizeof(built)) == sizeof(PROBE));
    rig.start(request(built, sizeof(built)));
    std::memset(built, 0xCC, sizeof(built)); // start owns a copy, not this pointer.
    assert(rig.runner.start(request(), 1000) == Admission::BUSY);
    rig.send();
    assert(rig.fake.written == std::vector<uint8_t>(PROBE, PROBE + sizeof(PROBE)));
    rig.receive();
    rig.fake.bytes(REPLY, sizeof(REPLY), 2510);
    rig.poll(2610); // First byte only, remaining bytes have not completed.
    assert(rig.runner.result().rxLength == 1);
    rig.poll(2910);
    assert(rig.runner.result().rxLength == 4);
    rig.poll(3210);
    assert(rig.runner.result().rxLength == sizeof(REPLY));
    assert(rig.runner.busy());
    rig.poll(3559);
    assert(rig.runner.busy());
    rig.poll(3560);
    assert(rig.runner.result().reason == Reason::FRAME);
    assert(!rig.runner.busy() && !rig.runner.needsRecovery());
    assert(rig.runner.result().txComplete && rig.runner.result().txAccepted == sizeof(PROBE));
    assert(rig.runner.result().rxLength == sizeof(REPLY));
    assert(std::memcmp(rig.rx, REPLY, sizeof(REPLY)) == 0);
    uint16_t model = 0;
    assert(Ess::parseProbe(rig.rx, rig.runner.result().rxLength, 1, model));
    assert(model == 0x003C);
    assert(rig.runner.stats().started == 1 && rig.runner.stats().frames == 1);
    assert(rig.runner.stats().failed == 0 && rig.fake.writes == 1);
    const Result saved = rig.runner.result();
    rig.poll(90000);
    assert(rig.fake.writes == 1 && rig.runner.result().endedUs == saved.endedUs);
    rig.runner.clearStats();
    assert(rig.runner.stats().started == 0 && rig.runner.result().reason == Reason::FRAME);
}

void testExceptionAndCodecFailure() {
    {
        Rig rig;
        rig.start(); rig.send(); rig.receive();
        rig.fake.bytes(EXCEPTION, sizeof(EXCEPTION), 2510);
        rig.poll(3360);
        assert(rig.runner.result().reason == Reason::FRAME);
        assert(rig.runner.result().rxLength == sizeof(EXCEPTION));
        uint16_t model = 0xABCD;
        const MotorControlRS::Status parsed = Ess::parseProbe(rig.rx, 5, 1, model);
        assert(parsed.code == MotorControlRS::Err::EXCEPTION && parsed.detail == 2);
        assert(model == 0xABCD);
    }
    {
        Rig rig;
        uint8_t corrupt[sizeof(REPLY)];
        std::memcpy(corrupt, REPLY, sizeof(corrupt));
        corrupt[sizeof(corrupt) - 1] ^= 1;
        rig.start(); rig.send(); rig.receive();
        rig.fake.bytes(corrupt, sizeof(corrupt), 2510);
        rig.poll(3560);
        assert(rig.runner.result().reason == Reason::FRAME); // Envelope, not CRC success.
        uint16_t model = 0xABCD;
        assert(Ess::parseProbe(rig.rx, sizeof(corrupt), 1, model).code == MotorControlRS::Err::CRC_ERROR);
        assert(model == 0xABCD && rig.fake.writes == 1);
    }
}

void testReplyTiming() {
    {
        Rig rig;
        rig.start(); rig.send(); rig.receive();
        rig.fake.bytes(REPLY, sizeof(REPLY), 2510, 150); // Exactly t1.5 is permitted.
        rig.poll(4460);
        assert(rig.runner.result().reason == Reason::FRAME);
    }
    {
        Rig rig;
        rig.start(); rig.send(); rig.receive();
        rig.fake.bytes(REPLY, sizeof(REPLY), 2510, 151);
        rig.poll(4470);
        assert(rig.runner.result().reason == Reason::GAP && rig.runner.needsRecovery());
        assert(rig.fake.writes == 1);
    }
    {
        Rig rig;
        rig.start(); rig.send(); rig.receive();
        rig.fake.bytes(REPLY, sizeof(REPLY), 2509); // One us short of inter-frame silence.
        rig.poll(3560);
        assert(rig.runner.result().reason == Reason::EARLY_REPLY);
    }
    {
        Rig rig;
        rig.start(); rig.send(); rig.receive();
        rig.fake.bytes(REPLY, 2, 2510);
        rig.fake.bytes(REPLY + 2, sizeof(REPLY) - 2, 3060); // t3.5: first frame is short.
        rig.poll(4000);
        assert(rig.runner.result().reason != Reason::FRAME);
        assert(rig.runner.needsRecovery());
    }
    {
        Rig rig;
        rig.start(); rig.send(); rig.receive();
        rig.fake.bytes(REPLY, 1, 2510);
        // Receive byte 2 has started but is not finished when poll runs.
        rig.fake.bytes(REPLY + 1, sizeof(REPLY) - 1, 2760);
        rig.poll(2810);
        assert(rig.runner.result().reason == Reason::NONE);
        rig.poll(3710);
        assert(rig.runner.result().reason == Reason::FRAME);
    }
}

void testLengthAndOverflow() {
    {
        Rig rig;
        rig.start(); rig.send(); rig.receive();
        rig.fake.bytes(REPLY, sizeof(REPLY), 2510);
        const uint8_t trailing = 0x99;
        rig.fake.bytes(&trailing, 1, 3210);
        rig.poll(3660);
        assert(rig.runner.result().reason == Reason::LENGTH);
        assert(rig.runner.result().rxLength == 8 && rig.rx[7] == trailing);
        assert(rig.runner.needsRecovery());
    }
    {
        Rig rig(makeTiming(), sizeof(REPLY));
        rig.start(); rig.send(); rig.receive();
        rig.fake.bytes(REPLY, sizeof(REPLY), 2510);
        const uint8_t trailing[] = {0x99, 0xAA};
        rig.fake.bytes(trailing, sizeof(trailing), 3210);
        rig.poll(3760);
        assert(rig.runner.result().reason == Reason::RX_OVERFLOW);
        assert(rig.runner.result().rxLength == sizeof(REPLY));
        assert(rig.runner.result().rxTruncated);
        assert(std::memcmp(rig.rx, REPLY, sizeof(REPLY)) == 0);
    }
    {
        Rig rig;
        rig.start(); rig.send(); rig.receive();
        rig.fake.bytes(REPLY, sizeof(REPLY) - 1, 2510);
        rig.poll(3460);
        assert(rig.runner.result().reason == Reason::LENGTH);
        assert(rig.runner.result().rxLength == sizeof(REPLY) - 1);
    }
}

void testTimeoutAndRecovery() {
    Rig rig;
    rig.start(); rig.send(); rig.receive();
    rig.poll(12159);
    assert(rig.runner.busy());
    rig.poll(12160);
    assert(rig.runner.result().reason == Reason::NO_RESPONSE);
    assert(rig.runner.needsRecovery() && rig.runner.result().txComplete);
    assert(rig.runner.stats().timeouts == 1 && rig.runner.stats().failed == 1);
    assert(rig.runner.start(request(), 12160) == Admission::RECOVERY_REQUIRED);
    rig.fake.bytes(REPLY, sizeof(REPLY), 12500); // Late reply cannot revive the request.
    rig.poll(15000);
    assert(rig.runner.result().reason == Reason::NO_RESPONSE && rig.fake.writes == 1);
    assert(rig.runner.needsRecovery());
    rig.runner.clearStats();
    assert(rig.runner.needsRecovery() && rig.runner.result().reason == Reason::NO_RESPONSE);
    rig.fake.now = 15000;
    assert(rig.runner.recover(15000)); // Explicit host recovery decision, not inferred proof.
    assert(rig.runner.result().reason == Reason::NO_RESPONSE);
    rig.start(request(), 15000);
    rig.poll(15349);
    assert(rig.fake.writes == 1);
    rig.poll(15350, 4);
    rig.poll(15360, 4);
    assert(rig.fake.writes == 2); // Only the explicit second start submitted another request.
}

void testPartialReplyDeadline() {
    Rig rig;
    Request req = request();
    req.responseTimeoutUs = 1350; // Physical TX end2160 +1350 =3510; frame settles3560.
    rig.start(req); rig.send(); rig.receive();
    rig.fake.bytes(REPLY, sizeof(REPLY), 2510);
    rig.poll(3210);
    rig.poll(3510);
    assert(rig.runner.result().reason == Reason::PARTIAL_RESPONSE);
    assert(rig.runner.result().rxLength == sizeof(REPLY));
    assert(rig.runner.needsRecovery());
}

void testCaptureLag() {
    {
        Rig rig;
        Request req = request();
        req.responseTimeoutUs = 1500; // End2160 +1500 =3660; reply is complete3560.
        rig.start(req); rig.send(); rig.receive();
        rig.fake.captureThrough = 2180;
        rig.fake.bytes(REPLY, sizeof(REPLY), 2510);
        rig.poll(3660);
        assert(rig.runner.busy() && rig.runner.result().reason == Reason::NONE);
        rig.fake.captureThrough = std::numeric_limits<uint64_t>::max();
        rig.poll(3700); // Delayed adapter capture still proves a timely wire response.
        assert(rig.runner.result().reason == Reason::FRAME);
        assert(rig.runner.result().endedUs == 3560);
    }
    {
        Timing timing = makeTiming();
        timing.captureTimeoutUs = 500;
        Rig rig(timing);
        rig.start(); rig.send(); rig.receive();
        rig.fake.captureThrough = 2180;
        rig.poll(2680); // The declared lag limit is inclusive.
        assert(rig.runner.busy());
        rig.poll(2681);
        assert(rig.runner.result().reason == Reason::CAPTURE_TIMEOUT);
        assert(rig.runner.needsRecovery() && rig.fake.writes == 1);
    }
}

void testTxDrainFailuresAndCancellation() {
    {
        Rig rig;
        rig.fake.acceptLimit = 3;
        rig.start(); rig.send();
        assert(rig.runner.busy()); // Failed enqueue still drains the accepted bytes.
        assert(rig.runner.result().txAccepted == 3);
        assert(rig.runner.transmitEnabled());
        rig.fake.now = 1500;
        assert(!rig.runner.recover(1500)); // Do not release DE over queued bytes.
        rig.poll(2160);
        assert(rig.runner.transmitEnabled());
        rig.poll(2180);
        assert(!rig.runner.transmitEnabled() && rig.runner.needsRecovery());
        assert(rig.runner.result().reason == Reason::TX_ERROR);
        assert(rig.fake.writes == 1);
    }
    {
        Timing timing = makeTiming();
        timing.txTimeoutUs = 500;
        Rig rig(timing);
        rig.start(); rig.send();
        rig.fake.holdBusy = true;
        rig.poll(1850);
        assert(rig.runner.result().reason == Reason::TX_TIMEOUT);
        assert(rig.runner.transmitEnabled() && rig.runner.needsRecovery());
        rig.fake.holdBusy = false;
        rig.poll(2180);
        assert(!rig.runner.transmitEnabled() && rig.runner.needsRecovery());
        assert(rig.fake.writes == 1);
    }
    {
        Rig rig;
        rig.start(); rig.send();
        rig.fake.now = 1500;
        rig.runner.cancel(1500);
        assert(rig.runner.busy() && rig.runner.transmitEnabled());
        rig.poll(2180);
        rig.poll(2180);
        assert(!rig.runner.transmitEnabled() && rig.fake.writes == 1);
        assert(rig.runner.result().reason == Reason::CANCELLED && rig.runner.needsRecovery());
        assert(rig.runner.result().txAccepted == sizeof(PROBE));
        assert(rig.runner.stats().cancelled == 1);
    }
    {
        Rig rig;
        rig.start();
        rig.fake.now = 1100;
        rig.runner.cancel(1100);
        assert(rig.runner.result().reason == Reason::CANCELLED);
        assert(rig.runner.result().txAccepted == 0 && rig.fake.writes == 0);
        assert(!rig.runner.needsRecovery());
    }
    {
        Timing timing = makeTiming();
        timing.txTimeoutUs = 500;
        Rig rig(timing);
        rig.fake.txDuration = 475; // TX ends1835; hold ends1855, budget expires1850.
        rig.start(); rig.send();
        rig.poll(1835);
        rig.poll(1849);
        assert(rig.runner.busy() && rig.runner.transmitEnabled());
        rig.poll(1850);
        assert(rig.runner.result().reason == Reason::TX_TIMEOUT);
        assert(rig.runner.transmitEnabled());
        rig.poll(1854);
        assert(rig.runner.transmitEnabled());
        rig.poll(1855);
        assert(!rig.runner.transmitEnabled() && rig.runner.needsRecovery());
    }
    {
        Timing timing = makeTiming();
        timing.holdUs = 0;
        Rig rig(timing);
        rig.start(); rig.send();
        rig.poll(2160); // One poll must release DE as soon as physical drain is known.
        assert(!rig.runner.transmitEnabled() && rig.runner.phase() == Phase::RECEIVE);
    }
    {
        Rig rig;
        rig.fake.acceptLimit = 3;
        rig.start(); rig.send();
        rig.fake.now = 1500;
        rig.runner.cancel(1500);
        rig.poll(2180);
        assert(rig.runner.result().reason == Reason::TX_ERROR); // Preserve the original failure.
    }
}

bool hasRxTrace(const Runner& runner, uint8_t byte, uint64_t start, uint64_t end) {
    for (std::size_t i = 0; i < runner.traceSize(); ++i) {
        const Trace* trace = runner.traceAt(i);
        if (trace->event == Event::RX && trace->byte == byte &&
            trace->wireStartUs == start && trace->atUs == end) return true;
    }
    return false;
}

void testEchoPolicies() {
    {
        Rig rig;
        rig.start(request(WRITE, sizeof(WRITE), sizeof(WRITE), Echo::NONE));
        rig.send(); rig.receive();
        rig.fake.bytes(WRITE, sizeof(WRITE), 2510);
        rig.poll(3660);
        assert(rig.runner.result().reason == Reason::FRAME);
        assert(rig.runner.result().echoBytes == 0);
        assert(Ess::parseWriteSingleRegister(rig.rx, 8, 1, 0x23, 0x3C));
    }
    {
        Rig rig;
        rig.fake.bytes(WRITE, sizeof(WRITE), 1360); // Qualified local echo while DE is high.
        rig.fake.bytes(WRITE, sizeof(WRITE), 2510); // Distinct drive acknowledgement.
        rig.start(request(WRITE, sizeof(WRITE), sizeof(WRITE), Echo::REQUIRED));
        rig.send(); rig.receive();
        rig.poll(3660);
        assert(rig.runner.result().reason == Reason::FRAME);
        assert(rig.runner.result().echoBytes == sizeof(WRITE));
        assert(rig.runner.result().rxLength == sizeof(WRITE));
        assert(Ess::parseWriteSingleRegister(rig.rx, 8, 1, 0x23, 0x3C));
    }
    {
        Rig rig;
        rig.start(request(WRITE, sizeof(WRITE), sizeof(WRITE), Echo::REQUIRED));
        rig.send();
        rig.poll(2180, 4);
        rig.poll(12160);
        assert(rig.runner.result().reason == Reason::ECHO_ERROR);
        assert(rig.runner.needsRecovery() && rig.fake.writes == 1);
    }
    {
        Rig rig;
        rig.fake.bytes(WRITE, sizeof(WRITE), 1360);
        rig.start(request(WRITE, sizeof(WRITE), sizeof(WRITE), Echo::NONE));
        rig.send();
        rig.poll(2160, 4);
        assert(rig.runner.result().reason == Reason::EARLY_REPLY);
        assert(rig.runner.needsRecovery()); // Never count a local copy as an acknowledgement.
    }
    {
        Rig rig;
        uint8_t wrong[sizeof(WRITE)];
        std::memcpy(wrong, WRITE, sizeof(wrong));
        wrong[4] ^= 1;
        rig.fake.bytes(wrong, sizeof(wrong), 1360);
        rig.start(request(WRITE, sizeof(WRITE), sizeof(WRITE), Echo::REQUIRED));
        rig.send();
        rig.poll(2180, 4);
        assert(rig.runner.result().reason == Reason::ECHO_ERROR);
        assert(rig.runner.needsRecovery());
        assert(hasRxTrace(rig.runner, wrong[4], 1760, 1860));
    }
}

void testBoundedWorkAndTrace() {
    Timing timing = makeTiming();
    timing.busTimeoutUs = 20000;
    Rig rig(timing, 64, 4);
    uint8_t noise[READ_BUDGET + 6];
    std::memset(noise, 0xCC, sizeof(noise));
    rig.fake.bytes(noise, sizeof(noise), 1000);
    rig.start();
    const unsigned before = rig.fake.reads;
    rig.poll(9000);
    assert(rig.fake.reads - before == READ_BUDGET);
    assert(rig.fake.writes == 0); // Budget exhaustion does not establish an idle bus.
    assert(rig.runner.busy() && !rig.runner.transmitEnabled());
    assert(rig.runner.traceSize() == 4 && rig.runner.stats().traceOverwritten > 0);
    for (std::size_t i = 0; i < rig.runner.traceSize(); ++i) {
        assert(rig.runner.traceAt(i) != nullptr);
        if (i) assert(rig.runner.traceAt(i - 1)->atUs <= rig.runner.traceAt(i)->atUs);
    }
    assert(rig.runner.traceAt(4) == nullptr);
    assert(rig.runner.traceAt(std::numeric_limits<std::size_t>::max()) == nullptr);
    rig.poll(9000);
    assert(rig.runner.transmitEnabled()); // Remaining queued noise is now accounted for.
    rig.poll(9010);
    assert(rig.fake.writes == 1);
}

void testEchoReadBudget() {
    for (unsigned hold = 0; hold <= 20; hold += 20) {
        Fake fake;
        // A generic FC10 frame carrying 36 words exercises the helper's 256-byte
        // storage contract; this is deliberately not an ESS register operation.
        uint8_t frame[81] = {1, 0x10, 0, 0, 0, 36, 72};
        const uint16_t crc = Ess::calcCrc16(frame, sizeof(frame) - 2);
        frame[79] = static_cast<uint8_t>(crc);
        frame[80] = static_cast<uint8_t>(crc >> 8);
        uint8_t tx[128] = {}, rx[64] = {};
        Storage storage;
        storage.tx = tx; storage.txCapacity = sizeof(tx);
        storage.rx = rx; storage.rxCapacity = sizeof(rx);
        Timing timing = makeTiming();
        timing.txTimeoutUs = 20000;
        timing.holdUs = hold;
        fake.txDuration = sizeof(frame) * 100;
        fake.bytes(frame, sizeof(frame), 1360);
        Runner runner(makePort(fake), storage, timing);
        fake.now = 1000;
        assert(runner.start(request(frame, sizeof(frame), 8, Echo::REQUIRED), 1000) == Admission::STARTED);
        runner.poll(1000);
        fake.now = 1350; runner.poll(1350);
        fake.now = 1360; runner.poll(1360);
        assert(fake.writes == 1);
        const unsigned before = fake.reads;
        fake.now = 9460; runner.poll(9460);
        assert(fake.reads - before <= READ_BUDGET);
        assert(runner.busy());
        fake.now = 9480; runner.poll(9480);
        assert(runner.result().echoBytes == sizeof(frame));
        assert(!runner.transmitEnabled());
    }
}

void testStaleBytesAndLongClock() {
    {
        Rig rig;
        rig.fake.bytes(REPLY, sizeof(REPLY), 100); // Old complete frame before start.
        for (auto& byte : rig.fake.input) byte.uncertaintyUs = 10;
        rig.start(); rig.send(); rig.receive();
        assert(rig.runner.stats().discarded == sizeof(REPLY));
        assert(rig.runner.result().rxLength == 0);
        rig.fake.bytes(REPLY, sizeof(REPLY), 2510);
        rig.poll(3560);
        assert(rig.runner.result().reason == Reason::FRAME);
        assert(rig.runner.result().firstRxStartUs == 2510);
        assert(rig.runner.result().maxRxUncertaintyUs == 0);
        assert(rig.runner.result().txEndUs == rig.fake.txEnd);
    }
    {
        Rig rig;
        const uint64_t start = static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()) + 1000;
        rig.start(request(), start);
        rig.poll(start + 350, 4);
        rig.poll(start + 360, 4);
        rig.poll(start + 1160, 4);
        rig.poll(start + 1180, 4);
        rig.fake.bytes(REPLY, sizeof(REPLY), start + 1510);
        rig.poll(start + 2560);
        assert(rig.runner.result().reason == Reason::FRAME);
        assert(rig.runner.result().startedUs == start);
        assert(rig.runner.result().endedUs == start + 2560);
    }
    {
        Rig rig;
        rig.start(); rig.send(); rig.receive();
        rig.fake.bytes(REPLY, sizeof(REPLY), 2510);
        rig.fake.input[1].startUs = rig.fake.input[0].endUs - 1; // Overlapping wire characters.
        rig.poll(3560);
        assert(rig.runner.result().reason == Reason::CLOCK_ERROR);
    }
}

void testBusyWireAdmission() {
    {
        Rig rig;
        rig.fake.bytes(REPLY, 1, 1351);
        rig.start();
        rig.poll(1400); // Historical idle was long enough, but a character is now in flight.
        assert(!rig.runner.transmitEnabled() && rig.fake.writes == 0);
        rig.poll(1451);
        assert(rig.runner.stats().discarded == 1);
        rig.poll(1800);
        assert(!rig.runner.transmitEnabled());
        rig.poll(1801);
        assert(rig.runner.transmitEnabled());
        rig.poll(1811);
        assert(rig.fake.writes == 1);
    }
    {
        Rig rig;
        rig.fake.bytes(REPLY, 1, 1355);
        rig.start();
        rig.poll(1350);
        assert(rig.runner.transmitEnabled());
        rig.poll(1360); // External activity began during DE setup; no TX can be enqueued.
        assert(rig.fake.writes == 0);
        rig.poll(1455);
        assert(rig.runner.result().reason == Reason::EARLY_REPLY);
        assert(rig.runner.needsRecovery() && rig.fake.writes == 0);
        assert(!rig.runner.transmitEnabled());
    }
}

void testRejectedByteEvidence() {
    {
        Rig rig;
        rig.start(); rig.send(); rig.receive();
        rig.fake.bytes(REPLY, sizeof(REPLY), 2509);
        rig.poll(3560);
        assert(rig.runner.result().reason == Reason::EARLY_REPLY);
        assert(hasRxTrace(rig.runner, REPLY[0], 2509, 2609));
    }
    {
        Rig rig;
        rig.start(); rig.send(); rig.receive();
        rig.fake.bytes(REPLY, sizeof(REPLY), 2510, 151);
        rig.poll(4470);
        assert(rig.runner.result().reason == Reason::GAP);
        assert(hasRxTrace(rig.runner, REPLY[1], 2761, 2861));
    }
    {
        Rig rig(makeTiming(), 64, 0);
        rig.start(); rig.send(); rig.receive();
        rig.fake.bytes(REPLY, sizeof(REPLY), 2510);
        rig.poll(3560);
        assert(rig.runner.result().reason == Reason::FRAME);
        assert(rig.runner.traceSize() == 0 && rig.runner.traceAt(0) == nullptr);
    }
}

void testAdmissionAndFaults() {
    {
        Rig rig;
        Request invalid = request();
        invalid.bytes = nullptr;
        assert(rig.runner.start(invalid, 1000) == Admission::INVALID);
        invalid = request(); invalid.length = 0;
        assert(rig.runner.start(invalid, 1000) == Admission::INVALID);
        invalid = request(); invalid.length = 33;
        assert(rig.runner.start(invalid, 1000) == Admission::INVALID);
        invalid = request(); invalid.replyLength = 65;
        assert(rig.runner.start(invalid, 1000) == Admission::INVALID);
        invalid = request(); invalid.responseTimeoutUs = 0;
        assert(rig.runner.start(invalid, 1000) == Admission::INVALID);
        invalid = request(); invalid.echo = static_cast<Echo>(255);
        assert(rig.runner.start(invalid, 1000) == Admission::INVALID);
        assert(rig.fake.writes == 0 && rig.fake.directions.empty());
    }
    {
        Rig rig;
        rig.fake.enableError = true;
        rig.start();
        rig.poll(1350);
        assert(rig.runner.result().reason == Reason::DIRECTION_ERROR);
        assert(rig.fake.writes == 0);
    }
    {
        Rig rig;
        rig.start(); rig.send();
        rig.fake.disableError = true;
        rig.poll(2180, 4);
        assert(rig.runner.result().reason == Reason::DIRECTION_ERROR);
        assert(rig.runner.needsRecovery());
        rig.fake.now = 2200;
        assert(!rig.runner.recover(2200));
        rig.fake.disableError = false;
        rig.fake.now = 2300;
        assert(rig.runner.recover(2300));
    }
    {
        Rig rig;
        rig.start(); rig.send(); rig.receive();
        rig.fake.readError = true;
        rig.poll(2200);
        assert(rig.runner.result().reason == Reason::RX_ERROR);
        assert(rig.runner.needsRecovery() && rig.fake.writes == 1);
    }
    {
        Rig rig;
        rig.start(); rig.send();
        rig.poll(1359); // Owner supplied a regressing clock while TX may be active.
        assert(rig.runner.result().reason == Reason::CLOCK_ERROR);
        assert(rig.runner.needsRecovery() && rig.runner.transmitEnabled());
    }
    {
        Rig rig;
        rig.start(); rig.send(); rig.receive();
        rig.fake.bytes(REPLY, sizeof(REPLY), 2510);
        rig.poll(3560);
        assert(rig.runner.result().reason == Reason::FRAME);
        rig.poll(3559); // A later invalid call must preserve the completed transaction.
        assert(rig.runner.needsRecovery());
        assert(rig.runner.result().reason == Reason::FRAME);
        assert(rig.runner.result().endedUs == 3560);
        assert(rig.runner.result().rxLength == sizeof(REPLY));
        assert(std::memcmp(rig.rx, REPLY, sizeof(REPLY)) == 0);
        assert(rig.runner.stats().frames == 1 && rig.runner.stats().failed == 0);
        const Trace* last = rig.runner.traceAt(rig.runner.traceSize() - 1);
        assert(last && last->event == Event::CLOCK_ERROR);
        assert(last->reason == Reason::CLOCK_ERROR && last->atUs == 3560);
        assert(rig.runner.start(request(), 4000) == Admission::RECOVERY_REQUIRED);
        rig.fake.now = 4000;
        assert(rig.runner.recover(4000));
        assert(rig.runner.result().reason == Reason::FRAME);
        assert(rig.fake.writes == 1);
    }
    {
        Rig rig;
        rig.start(); rig.send(); rig.receive();
        rig.fake.bytes(REPLY, sizeof(REPLY), 2510);
        rig.poll(3560);
        assert(rig.runner.result().reason == Reason::FRAME);
        rig.fake.disableError = true;
        rig.fake.now = 4000;
        assert(!rig.runner.recover(4000));
        assert(rig.runner.needsRecovery()); // Failed direction cleanup must interlock even after DONE.
        assert(rig.runner.start(request(), 4000) == Admission::RECOVERY_REQUIRED);
    }
}

void testRepeatedTransactions() {
    Rig rig(makeTiming(), 64, 8);
    const unsigned transactions = 1000;
    const uint64_t origin = static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()) - 2000;
    uint32_t received = 0;
    uint32_t overwritten = 0;
    for (unsigned iteration = 0; iteration < transactions; ++iteration) {
        // Reuse the same runner, buffers and trace ring. Clearing consumed fake
        // input keeps the test harness bounded too; its vector capacity is reused.
        rig.fake.input.clear();
        rig.fake.readIndex = 0;
        rig.fake.directions.clear();
        const uint64_t start = origin + static_cast<uint64_t>(iteration) * 5000;
        const bool exception = iteration % 7 == 0;
        const uint8_t* reply = exception ? EXCEPTION : REPLY;
        const std::size_t length = exception ? sizeof(EXCEPTION) : sizeof(REPLY);
        const uint64_t silence = (iteration % 5) * 25;
        const uint64_t firstStart = start + 1510;
        const uint64_t lastEnd = firstStart + length * 100 + (length - 1) * silence;
        const uint64_t complete = lastEnd + 350;

        rig.start(request(), start);
        rig.poll(start + 350);
        rig.poll(start + 360);
        rig.poll(start + 1160);
        rig.poll(start + 1180);
        assert(!rig.runner.transmitEnabled());
        assert(rig.fake.writes == iteration + 1);
        assert(rig.fake.directions.size() == 2);
        rig.fake.bytes(reply, length, firstStart, silence);

        switch (iteration % 3) {
            case 0:
                rig.poll(complete); // All bytes arrive in one service batch.
                break;
            case 1:
                for (std::size_t i = 0; i < length; ++i) {
                    rig.poll(firstStart + i * (100 + silence) + 100);
                    assert(rig.runner.busy());
                }
                rig.poll(complete - 1);
                assert(rig.runner.busy());
                rig.poll(complete);
                break;
            default:
                rig.poll(firstStart + 200 + silence); // Two-byte initial fragment.
                assert(rig.runner.result().rxLength == 2);
                rig.poll(lastEnd + 175);
                assert(rig.runner.busy());
                rig.poll(complete + 123); // Delayed service retains actual completion time.
                break;
        }

        assert(rig.runner.result().reason == Reason::FRAME);
        assert(rig.runner.result().startedUs == start && rig.runner.result().endedUs == complete);
        assert(rig.runner.result().rxLength == length && rig.runner.result().txComplete);
        assert(!rig.runner.needsRecovery() && rig.fake.readIndex == length);
        assert(std::memcmp(rig.rx, reply, length) == 0);
        uint16_t model = 0xABCD;
        const MotorControlRS::Status parsed = Ess::parseProbe(rig.rx, length, 1, model);
        if (exception) {
            assert(parsed.code == MotorControlRS::Err::EXCEPTION && parsed.detail == 2);
            assert(model == 0xABCD);
        } else {
            assert(parsed && model == 0x003C);
        }

        received += static_cast<uint32_t>(length);
        assert(rig.runner.stats().started == iteration + 1);
        assert(rig.runner.stats().frames == iteration + 1);
        assert(rig.runner.stats().rxBytes == received && rig.runner.stats().failed == 0);
        assert(rig.runner.traceSize() == 8);
        assert(rig.runner.stats().traceOverwritten > overwritten);
        overwritten = rig.runner.stats().traceOverwritten;
        assert(rig.runner.traceAt(7)->event == Event::END);
        rig.poll(start + 4000, 3);
        assert(rig.fake.writes == iteration + 1); // Idle polling never repeats the request.
    }
    assert(rig.runner.stats().timeouts == 0 && rig.runner.stats().cancelled == 0);
}

void testTimingIntervals() {
    {
        Rig rig;
        rig.fake.txUncertainty = 20;
        rig.start(); rig.send(); rig.receive();
        rig.fake.bytes(REPLY, sizeof(REPLY), 2570);
        for (RxByte& byte : rig.fake.input) {
            byte.startUs -= 10;
            byte.endUs += 10;
            byte.uncertaintyUs = 20;
        }
        rig.poll(3629);
        assert(rig.runner.busy());
        rig.poll(3630);
        assert(rig.runner.result().reason == Reason::FRAME);
        assert(rig.runner.result().endedUs == 3630);
        bool txSeen = false, rxSeen = false;
        for (std::size_t i = 0; i < rig.runner.traceSize(); ++i) {
            const Trace& item = *rig.runner.traceAt(i);
            if (item.event == Event::TX_DONE) {
                txSeen = true;
                assert(item.atUs == 2160 && item.uncertaintyUs == 20);
            }
            if (item.event == Event::RX) {
                rxSeen = true;
                assert(item.uncertaintyUs == 20);
            }
        }
        assert(txSeen && rxSeen && rig.fake.writes == 1);
    }
    {
        Rig rig;
        rig.start(); rig.send(); rig.receive();
        rig.fake.bytes(REPLY, 1, 2500);
        rig.fake.input[0].uncertaintyUs = 20; // Start straddles TX end + t3.5.
        rig.poll(2700);
        assert(rig.runner.result().reason == Reason::TIMING_UNCERTAIN);
        assert(rig.runner.needsRecovery());
    }
    {
        Rig rig;
        rig.fake.txUncertainty = 20;
        rig.start(); rig.send(); rig.receive();
        rig.fake.bytes(REPLY, 1, 2500); // Exact RX, uncertain TX end straddles t3.5.
        rig.poll(2700);
        assert(rig.runner.result().reason == Reason::TIMING_UNCERTAIN);
    }
    for (unsigned difference = 0; difference < 2; ++difference) {
        Rig rig;
        rig.start(); rig.send(); rig.receive();
        rig.fake.bytes(REPLY, 1, 2510);
        rig.fake.bytes(REPLY + 1, 1, 2760 + difference * 10);
        rig.fake.input[1].uncertaintyUs = 20;
        rig.poll(3000);
        assert(rig.runner.result().reason ==
               (difference ? Reason::GAP : Reason::TIMING_UNCERTAIN));
    }
    {
        Rig rig;
        rig.start(); rig.send(); rig.receive();
        rig.fake.bytes(REPLY, sizeof(REPLY), 2510);
        rig.fake.bytes(REPLY, 1, 3550);
        rig.fake.input.back().uncertaintyUs = 20; // Gap may or may not be t3.5.
        rig.poll(3800);
        assert(rig.runner.result().reason == Reason::TIMING_UNCERTAIN);
        assert(rig.runner.result().rxLength == sizeof(REPLY));
    }
    {
        Rig rig;
        rig.fake.txUncertainty = 20;
        Request value = request();
        value.responseTimeoutUs = 1400; // Deadline lies in [3540,3560].
        rig.start(value); rig.send(); rig.receive();
        rig.fake.bytes(REPLY, sizeof(REPLY), 2510);
        rig.poll(3560);
        assert(rig.runner.result().reason == Reason::TIMING_UNCERTAIN);
    }
    {
        Rig rig;
        Request value = request();
        value.responseTimeoutUs = 1390; // 3550; final gap lies in [3540,3560].
        rig.start(value); rig.send(); rig.receive();
        rig.fake.bytes(REPLY, sizeof(REPLY), 2510);
        rig.fake.input.back().uncertaintyUs = 20;
        rig.poll(3550);
        assert(rig.runner.result().reason == Reason::TIMING_UNCERTAIN);
    }
    {
        Rig rig;
        rig.fake.txUncertainty = 20;
        Request value = request();
        value.responseTimeoutUs = 1400;
        rig.start(value); rig.send(); rig.receive();
        rig.poll(3540);
        assert(rig.runner.busy()); // Earliest possible deadline cannot prove timeout.
        rig.poll(3560);
        assert(rig.runner.result().reason == Reason::NO_RESPONSE);
    }
    {
        Rig rig;
        rig.start(); rig.send(); rig.receive();
        rig.fake.bytes(REPLY, 1, 2510);
        rig.fake.input[0].uncertaintyUs = 3000;
        rig.poll(3000);
        assert(rig.runner.result().reason == Reason::CLOCK_ERROR);
    }
    {
        Rig rig;
        rig.fake.txUncertainty = 1000; // Earliest completion predates enqueue.
        rig.start(); rig.send();
        rig.poll(2160);
        assert(rig.runner.result().reason == Reason::CLOCK_ERROR);
    }
}

void testPendingCapture() {
    {
        Rig rig;
        rig.start();
        rig.fake.capturePending = true;
        rig.poll(1350);
        assert(!rig.runner.transmitEnabled() && rig.fake.writes == 0);
        rig.fake.capturePending = false;
        rig.poll(1351);
        assert(rig.runner.transmitEnabled());
    }
    {
        Timing timing = makeTiming();
        timing.captureTimeoutUs = 1000;
        Rig rig(timing);
        rig.start(); rig.send(); rig.receive();
        rig.fake.capturePending = true;
        rig.poll(3180);
        assert(rig.runner.busy());
        rig.poll(3181);
        assert(rig.runner.result().reason == Reason::CAPTURE_TIMEOUT);
    }
    {
        Rig rig;
        rig.start(); rig.send(); rig.receive();
        rig.fake.bytes(REPLY, sizeof(REPLY), 2510);
        rig.poll(3210);
        rig.fake.capturePending = true;
        rig.poll(3560);
        assert(rig.runner.busy()); // No fabricated final gap from a pending capture.
        rig.fake.capturePending = false;
        rig.poll(3570);
        assert(rig.runner.result().reason == Reason::FRAME);
    }
}

void testReplyGapPolicy() {
    {
        Rig rig;
        Request value = request();
        value.replyGapUs = 50;
        rig.start(value); rig.send(); rig.receive();
        rig.fake.bytes(REPLY, sizeof(REPLY), 2210); // TX ends 2160; explicit reply gap is 50.
        rig.poll(3259);
        assert(rig.runner.busy()); // Final t3.5 remains 350, independent of turnaround.
        rig.poll(3260);
        assert(rig.runner.result().reason == Reason::FRAME);
        assert(rig.runner.result().endedUs == 3260);
    }
    {
        Rig rig;
        Request value = request();
        value.replyGapUs = 50;
        rig.start(value); rig.send(); rig.receive();
        rig.fake.bytes(REPLY, 1, 2209);
        rig.poll(2400);
        assert(rig.runner.result().reason == Reason::EARLY_REPLY);
        assert(rig.runner.needsRecovery());
    }
    {
        Rig rig;
        Request value = request();
        value.replyGapUs = 50;
        rig.start(value); rig.send(); rig.receive();
        rig.fake.bytes(REPLY, 1, 2200);
        rig.fake.input[0].uncertaintyUs = 20;
        rig.poll(2400);
        assert(rig.runner.result().reason == Reason::TIMING_UNCERTAIN);
    }
    {
        Rig rig;
        rig.start(); rig.send(); rig.receive();
        rig.fake.bytes(REPLY, 1, 2210); // Zero/default retains the original t3.5 requirement.
        rig.poll(2400);
        assert(rig.runner.result().reason == Reason::EARLY_REPLY);
    }
}

void testDeferredTransmitEvidence() {
    for (bool observedHold : {false, true}) {
        Rig rig;
        rig.fake.autoRelease = true;
        rig.start(); rig.send();
        if (observedHold) {
            rig.poll(2160);
            assert(rig.runner.phase() == Phase::HOLD);
        }
        rig.fake.bytes(REPLY, sizeof(REPLY), 2510);
        rig.poll(20000); // Delivery is late; the wire frame and DE release were on time.
        assert(rig.runner.result().reason == Reason::FRAME);
        assert(rig.runner.result().endedUs == 3560);
        assert(!rig.runner.transmitEnabled());
        assert(rig.fake.directions.size() == 1); // Only DE assertion needed task service.
        assert(std::memcmp(rig.rx, REPLY, sizeof(REPLY)) == 0);
        unsigned completions = 0;
        for (std::size_t i = 0; i < rig.runner.traceSize(); ++i) {
            const Trace& trace = *rig.runner.traceAt(i);
            if (trace.event == Event::TX_DONE) ++completions;
            if (trace.event == Event::DIRECTION && !trace.byte) assert(trace.atUs == 2180);
        }
        assert(completions == 1);
        rig.poll(30000, 5);
        assert(rig.fake.writes == 1 && rig.runner.result().reason == Reason::FRAME);
    }
    {
        Rig rig;
        rig.fake.autoRelease = true;
        rig.start(); rig.send(); rig.poll(2160);
        assert(rig.runner.phase() == Phase::HOLD);
        rig.fake.holdBusy = true; // A newer release observation is not publishable yet.
        rig.poll(2180);
        assert(rig.runner.phase() == Phase::HOLD && rig.runner.transmitEnabled());
        rig.fake.holdBusy = false;
        rig.fake.bytes(REPLY, sizeof(REPLY), 2510);
        rig.poll(9000);
        assert(rig.runner.result().reason == Reason::FRAME && rig.fake.writes == 1);
    }
    {
        Rig rig;
        rig.fake.autoRelease = true;
        rig.start(request(WRITE, sizeof(WRITE), sizeof(WRITE), Echo::REQUIRED));
        rig.send();
        rig.fake.bytes(WRITE, sizeof(WRITE), 1360);
        rig.fake.bytes(WRITE, sizeof(WRITE), 2510);
        rig.poll(9000);
        assert(rig.runner.result().reason == Reason::FRAME);
        assert(rig.runner.result().echoBytes == sizeof(WRITE));
        assert(rig.runner.result().rxLength == sizeof(WRITE));
        assert(rig.fake.writes == 1); // Buffered echo never causes replay.
    }
    {
        Rig rig;
        rig.fake.autoRelease = true;
        rig.start(); rig.send();
        rig.fake.bytes(REPLY, sizeof(REPLY), 13000); // The response itself missed its deadline.
        rig.poll(15000);
        assert(rig.runner.result().reason == Reason::NO_RESPONSE);
        assert(rig.runner.needsRecovery());
        rig.poll(20000, 5);
        assert(rig.fake.writes == 1);
    }
    {
        Rig rig;
        rig.fake.autoRelease = true;
        rig.start(); rig.send();
        rig.runner.cancel(1500);
        rig.fake.bytes(REPLY, sizeof(REPLY), 2510);
        rig.poll(9000);
        assert(rig.runner.result().reason == Reason::CANCELLED);
        assert(!rig.runner.transmitEnabled());
        assert(rig.fake.readIndex == 0 && rig.fake.writes == 1);
        assert(rig.runner.needsRecovery());
    }
}

void testReleaseBoundsAndDeadlines() {
    struct Invalid { uint64_t at; uint32_t width; uint64_t poll; Reason reason; };
    const Invalid invalid[] = {
        {3000, 0, 2500, Reason::CLOCK_ERROR}, // Future release evidence.
        {2180, 2181, 4000, Reason::CLOCK_ERROR},
        {2180, 1000, 4000, Reason::CLOCK_ERROR}, // Earliest release precedes assertion.
        {2170, 0, 4000, Reason::DIRECTION_ERROR}, // Definite hold violation.
        {2190, 20, 4000, Reason::TIMING_UNCERTAIN}, // Release range straddles required hold.
    };
    for (const Invalid& value : invalid) {
        Rig rig;
        rig.fake.autoRelease = rig.fake.forceReleaseEvidence = true;
        rig.fake.releaseAt = value.at;
        rig.fake.releaseUncertainty = value.width;
        rig.start(); rig.send(); rig.poll(value.poll);
        assert(rig.runner.result().reason == value.reason);
        assert(rig.runner.needsRecovery());
        assert(rig.runner.start(request(), value.poll) == Admission::RECOVERY_REQUIRED);
        // Current idle + an explicit direction action can recover without rewriting
        // the historical bad release evidence or replaying the original command.
        rig.fake.now = 5000;
        assert(rig.runner.recover(5000));
        assert(rig.runner.result().reason == value.reason && rig.fake.writes == 1);
    }
    struct Deadline { uint64_t at; uint32_t width; Reason reason; };
    const Deadline deadlines[] = {
        {6349, 0, Reason::FRAME},
        {6350, 0, Reason::TX_TIMEOUT},
        {6355, 10, Reason::TIMING_UNCERTAIN},
    };
    for (const Deadline& value : deadlines) {
        Rig rig;
        rig.fake.autoRelease = true;
        rig.fake.releaseAt = value.at;
        rig.fake.releaseUncertainty = value.width;
        rig.start(); rig.send();
        rig.fake.bytes(REPLY, sizeof(REPLY), 6500);
        rig.poll(9000);
        assert(rig.runner.result().reason == value.reason);
        assert(!rig.runner.transmitEnabled() && rig.fake.writes == 1);
    }
    {
        Rig rig;
        rig.fake.autoRelease = true;
        rig.start(); rig.send();
        rig.fake.holdBusy = true;
        rig.poll(9000);
        assert(rig.runner.result().reason == Reason::TX_TIMEOUT);
        assert(rig.runner.transmitEnabled());
        rig.fake.holdBusy = false;
        rig.poll(10000);
        assert(!rig.runner.transmitEnabled());
        assert(rig.runner.result().reason == Reason::TX_TIMEOUT && rig.fake.writes == 1);
    }
    for (uint64_t start : {uint64_t(2300), uint64_t(2340), uint64_t(2350)}) {
        Rig rig;
        rig.fake.autoRelease = true;
        rig.fake.releaseAt = 2350;
        rig.fake.releaseUncertainty = 20;
        Request value = request();
        value.replyGapUs = 50;
        rig.start(value); rig.send();
        rig.fake.bytes(REPLY, sizeof(REPLY), start);
        for (RxByte& byte : rig.fake.input) byte.uncertaintyUs = 20;
        rig.poll(9000);
        assert(rig.runner.result().reason == (start == 2300 ? Reason::EARLY_REPLY :
            start == 2340 ? Reason::TIMING_UNCERTAIN : Reason::FRAME));
    }
}

} // namespace

int main() {
    testTiming();
    testProbeAndOwnership();
    testExceptionAndCodecFailure();
    testReplyTiming();
    testLengthAndOverflow();
    testTimeoutAndRecovery();
    testPartialReplyDeadline();
    testCaptureLag();
    testTxDrainFailuresAndCancellation();
    testEchoPolicies();
    testBoundedWorkAndTrace();
    testEchoReadBudget();
    testStaleBytesAndLongClock();
    testBusyWireAdmission();
    testRejectedByteEvidence();
    testAdmissionAndFaults();
    testRepeatedTransactions();
    testTimingIntervals();
    testPendingCapture();
    testReplyGapPolicy();
    testDeferredTransmitEvidence();
    testReleaseBoundsAndDeadlines();
    return 0;
}
