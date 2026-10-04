// SPDX-License-Identifier: MIT
#include "MotorControlRS/Traffic.h"
#include "MotorControlRS/profiles/ess_rs/Traffic.h"
#include "../examples/common/RtuRunner.h"
#include <cassert>
#include <cstring>
#include <vector>

using namespace MotorControlRS;
namespace Ess = MotorControlRS::ESS_RS;
namespace Rtu = MotorControlRSExample::Rtu;
namespace {
const uint8_t PROBE[] = {1,3,0,0,0,1,0x84,0x0A};
const uint8_t REPLY[] = {1,3,2,0,0x3C,0xB8,0x55};
const uint8_t EXCEPTION[] = {1,0x83,2,0xC0,0xF1};
TrafficRecord record(TrafficKind kind, const uint8_t* bytes, std::size_t length) {
    TrafficRecord r; r.kind = kind; r.complete = true; r.transaction = 1;
    r.sequence = kind == TrafficKind::TX ? 1 : 2;
    r.length = static_cast<uint16_t>(length); std::memcpy(r.bytes, bytes, length); return r;
}
void storage() {
    TrafficRecord records[2]; TrafficCapture capture(records, 2);
    capture.transmitted(PROBE, sizeof(PROBE), true, 1); assert(capture.size() == 0);
    capture.setEnabled(true); capture.begin(1);
    capture.transmitted(PROBE, sizeof(PROBE), true, 2);
    uint64_t a = 0, b = 0; TrafficRecord first, second;
    assert(capture.copyAfter(a, first) && capture.copyAfter(b, second));
    assert(first.sequence == second.sequence && first.transaction == 1);
    assert(first.length == sizeof(PROBE) && std::memcmp(first.bytes, PROBE, sizeof(PROBE)) == 0);
    uint64_t aliasCursor = 0;
    assert(!capture.copyAfter(aliasCursor, records[0]) && aliasCursor == 0);
    assert(!capture.copyAfter(records[0].sequence, second) && records[0].sequence == 1);
    assert(!capture.copyAfter(second.sequence, second));
    assert(capture.size() == 1 && !capture.copyAfter(a, second));
    capture.event(TrafficKind::TX_END, 20, 8, 3);
    capture.transmitted(PROBE, 3, false, 30);
    assert(capture.overwritten() == 1 && capture.size() == 2);
    a = 0; assert(capture.copyAfter(a, second) && second.kind == TrafficKind::TX_END);
    assert(second.uncertaintyUs == 3 && second.endUs == 20);
    assert(capture.copyAfter(a, second) && second.length == 3 && !second.complete);
    capture.transmitted(PROBE, 257, true, 31);
    capture.transmitted(nullptr, 1, true, 32);
    assert(capture.dropped() == 2);
    capture.clear(); assert(capture.size() == 0 && capture.overwritten() == 0 && capture.dropped() == 0);
    capture.event(TrafficKind::END, 40); assert(capture.copyAfter(a, second));
    assert(second.sequence > first.sequence);
    capture.clear(); capture.begin(50);
    for (unsigned i = 0; i < 257; ++i) capture.received(static_cast<uint8_t>(i), i, i+1, 0, i+1);
    capture.flushReceived(true, 300);
    a = 0; assert(capture.copyAfter(a, first) && first.length == 256 && !first.complete);
    assert(capture.copyAfter(a, second) && second.length == 1 && !second.complete);
    capture.setEnabled(false); capture.received(1,1,2,0,2); assert(capture.size() == 2);
    TrafficCapture invalid(nullptr, 1); invalid.setEnabled(true); assert(!invalid.enabled());
}
void decode() {
    TrafficRecord tx = record(TrafficKind::TX, PROBE, sizeof(PROBE));
    TrafficRecord rx = record(TrafficKind::RX, REPLY, sizeof(REPLY));
    Ess::TrafficDecoded decoded; Ess::FrameError error;
    assert(Ess::decodeTraffic(tx, nullptr, decoded, &error));
    assert(decoded.address == 1 && decoded.function == 3 && decoded.start == 0 && decoded.count == 1);
    assert(Ess::decodeTraffic(rx, &tx, decoded, &error));
    assert(decoded.wordCount == 1 && decoded.words[0] == 60);
    decoded.start = 1234;
    rx.bytes[4] ^= 1;
    assert(Ess::decodeTraffic(rx, &tx, decoded, &error).code == Err::CRC_ERROR);
    assert(decoded.start == 1234 && error == Ess::FrameError::CRC);
    rx.bytes[4] ^= 1; rx.transaction = 2;
    assert(!Ess::decodeTraffic(rx, &tx, decoded) && decoded.start == 1234);
    rx.transaction = 1; assert(!Ess::decodeTraffic(rx, nullptr, decoded));
    rx.sequence = tx.sequence; assert(!Ess::decodeTraffic(rx, &tx, decoded)); rx.sequence = 2;
    rx.length = 257; assert(!Ess::decodeTraffic(rx, &tx, decoded)); rx.length = sizeof(REPLY);
    rx.complete = false; assert(!Ess::decodeTraffic(rx, &tx, decoded));
    rx = record(TrafficKind::RX, EXCEPTION, sizeof(EXCEPTION));
    assert(Ess::decodeTraffic(rx, &tx, decoded).code == Err::EXCEPTION);
    assert(decoded.isException && decoded.exceptionCode == 2 && decoded.wordCount == 0);
    tx.length = static_cast<uint16_t>(Ess::buildWriteSingleRegister(1, 0x23, 60, tx.bytes, sizeof(tx.bytes)));
    assert(tx.length == 8); rx = tx; rx.kind = TrafficKind::RX; rx.sequence = 2;
    assert(Ess::decodeTraffic(rx, &tx, decoded));
    assert(decoded.function == 6 && decoded.start == 0x23 && decoded.words[0] == 60);
    rx.bytes[5] ^= 1;
    const uint16_t crc = Ess::calcCrc16(rx.bytes, 6); rx.bytes[6] = static_cast<uint8_t>(crc); rx.bytes[7] = static_cast<uint8_t>(crc >> 8);
    assert(!Ess::decodeTraffic(rx, &tx, decoded, &error) && error == Ess::FrameError::ECHO);
    const uint16_t words[] = {0,100};
    tx.length = static_cast<uint16_t>(Ess::buildWriteMultipleRegisters(1,0x24,words,2,tx.bytes,sizeof(tx.bytes)));
    assert(Ess::decodeTraffic(tx, nullptr, decoded) && decoded.wordCount == 2 && decoded.words[1] == 100);
    rx = tx; rx.kind = TrafficKind::RX; rx.length = 8; rx.sequence = 2;
    const uint16_t crc2 = Ess::calcCrc16(rx.bytes, 6); rx.bytes[6] = static_cast<uint8_t>(crc2); rx.bytes[7] = static_cast<uint8_t>(crc2 >> 8);
    assert(Ess::decodeTraffic(rx, &tx, decoded) && decoded.wordCount == 0);
    tx.complete = false; assert(!Ess::decodeTraffic(tx, nullptr, decoded));
    rx.kind = TrafficKind::END; assert(Ess::decodeTraffic(rx, &tx, decoded).code == Err::UNSUPPORTED);
}
struct Fake {
    uint64_t now = 0, txEnd = 0;
    unsigned reads = 0, writes = 0; bool partial = false, corrupt = false;
    std::vector<Rtu::RxByte> input; std::size_t next = 0;
    static bool direction(void*, bool) { return true; }
    static Rtu::WriteResult write(void* p, const uint8_t*, std::size_t length) {
        Fake& f = *static_cast<Fake*>(p); ++f.writes; f.txEnd = f.now + 1000;
        if (!f.partial) for (std::size_t i = 0; i < sizeof(REPLY); ++i) {
            Rtu::RxByte b; b.startUs = f.txEnd + 1750 + i * 100; b.endUs = b.startUs + 100;
            b.value = static_cast<uint8_t>(REPLY[i] ^ (f.corrupt && i == sizeof(REPLY)-1 ? 1 : 0)); f.input.push_back(b);
        }
        return Rtu::WriteResult(f.partial ? 3 : length);
    }
    static Rtu::TxState tx(void* p, uint64_t now, Rtu::TxObservation& out) {
        Fake& f = *static_cast<Fake*>(p); if (now < f.txEnd) return Rtu::TxState::BUSY;
        out.endedUs = f.txEnd; return Rtu::TxState::IDLE;
    }
    static Rtu::ReadState read(void* p, uint64_t now, Rtu::RxByte& byte, uint64_t& through) {
        Fake& f = *static_cast<Fake*>(p); ++f.reads;
        if (f.next < f.input.size() && f.input[f.next].endUs <= now) { byte = f.input[f.next++]; return Rtu::ReadState::BYTE; }
        // A partial character holds back the completeness watermark.
        through = f.next < f.input.size() && f.input[f.next].startUs < now ? f.input[f.next].startUs : now;
        return Rtu::ReadState::EMPTY;
    }
};
struct RunResult { Rtu::Result result; unsigned reads, writes; };
RunResult run(bool observe, bool partial, std::size_t capacity, bool corrupt = false) {
    Fake fake; fake.partial = partial; fake.corrupt = corrupt;
    Rtu::Port port; port.context=&fake; port.setTransmit=Fake::direction; port.write=Fake::write; port.txState=Fake::tx; port.read=Fake::read;
    uint8_t tx[256],rx[256]; Rtu::Storage storage; storage.tx=tx; storage.txCapacity=sizeof(tx); storage.rx=rx; storage.rxCapacity=sizeof(rx);
    Rtu::Timing timing; Rtu::setRtuTiming(115200,10,timing); timing.busTimeoutUs=10000; timing.txTimeoutUs=10000; timing.captureTimeoutUs=10000;
    Rtu::Runner runner(port,storage,timing);
    TrafficRecord records[16]; TrafficCapture capture(records,capacity); capture.setEnabled(true);
    if (observe) assert(runner.setTrafficCapture(&capture));
    Rtu::Request request; request.bytes=PROBE; request.length=sizeof(PROBE); request.replyLength=sizeof(REPLY); request.responseTimeoutUs=10000;
    assert(runner.start(request,0)==Rtu::Admission::STARTED);
    assert(!runner.setTrafficCapture(nullptr));
    for (fake.now=0;fake.now<20000 && runner.busy();fake.now+=50) runner.poll(fake.now);
    assert(runner.result().reason == (partial ? Rtu::Reason::TX_ERROR : Rtu::Reason::FRAME));
    if (!partial) {
        assert(runner.result().rxLength==sizeof(REPLY));
        for (std::size_t i=0;i<sizeof(REPLY);++i) assert(runner.received()[i]==fake.input[i].value);
    }
    if (observe && capacity==16) {
        uint64_t cursor=0; TrafficRecord item,expected; bool foundTx=false,foundRx=false,foundEnd=false;
        while(capture.copyAfter(cursor,item)) {
            if(item.kind==TrafficKind::TX) { foundTx=true; expected=item; assert(item.length==(partial ? 3 : sizeof(PROBE))); }
            if(item.kind==TrafficKind::RX) {
                foundRx=true; Ess::TrafficDecoded value;
                assert(std::memcmp(item.bytes,runner.received(),sizeof(REPLY))==0);
                const Status parsed=Ess::decodeTraffic(item,&expected,value);
                if(corrupt) assert(parsed.code==Err::CRC_ERROR && value.wordCount==0);
                else assert(parsed && value.words[0]==60);
            }
            if(item.kind==TrafficKind::TX_END) foundEnd=true;
        }
        assert(foundTx && foundEnd && foundRx==!partial);
        if(partial) {
            Rtu::RxByte byte; byte.value=0xFF; byte.startUs=fake.now; byte.endUs=fake.now+100; fake.input.push_back(byte);
            fake.now+=100; const Rtu::Result before=runner.result(); runner.discard(fake.now);
            assert(runner.result().reason==before.reason && runner.result().txAccepted==before.txAccepted);
            bool discarded=false;
            while(capture.copyAfter(cursor,item)) if(item.kind==TrafficKind::RX) { discarded=true; assert(item.length==1 && item.bytes[0]==0xFF && !item.complete); }
            assert(discarded);
        }
    }
    RunResult out; out.result=runner.result(); out.reads=fake.reads; out.writes=fake.writes; return out;
}
void integration() {
    const RunResult plain=run(false,false,16),observed=run(true,false,16),overflow=run(true,false,1);
    assert(plain.reads==observed.reads && plain.reads==overflow.reads && plain.writes==observed.writes);
    assert(plain.result.reason==observed.result.reason && plain.result.endedUs==observed.result.endedUs);
    assert(plain.result.txAccepted==overflow.result.txAccepted && plain.result.rxLength==overflow.result.rxLength);
    run(true,true,16);
    const RunResult badPlain=run(false,false,16,true),badObserved=run(true,false,16,true);
    assert(badPlain.reads==badObserved.reads && badPlain.result.reason==badObserved.result.reason);
}
} // namespace
int main() { storage(); decode(); integration(); }
