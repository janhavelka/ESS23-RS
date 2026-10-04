// SPDX-License-Identifier: MIT
#include "../examples/common/RtuBusOwner.h"
#include "../examples/common/EssRtuValidator.h"
#include "MotorControlRS/profiles/ess_rs/Codec.h"
#include <cassert>

using namespace MotorControlRSExample::Rtu;
namespace Ess = MotorControlRS::ESS_RS;
namespace {
struct Rig {
    uint64_t now = 0, txEnd = 0;
    unsigned writes = 0;
    uint8_t tx[MAX_FRAME] = {}, rx[MAX_FRAME] = {}, bytes[8] = {};
    PendingSlot pending[3]; ResultSlot results[4]; ProducerSlot producers[2];
    Runner runner; BusOwner owner;
    static bool direction(void*, bool) { return true; }
    static WriteResult write(void* context, const uint8_t*, std::size_t size) {
        auto& r = *static_cast<Rig*>(context); ++r.writes;
        r.txEnd = r.now + size * 100; return WriteResult(size);
    }
    static TxState txState(void* context, uint64_t at, TxObservation& out) {
        auto& r = *static_cast<Rig*>(context);
        if (at < r.txEnd + (r.writes ? 20 : 0)) return TxState::BUSY;
        out.endedUs = r.txEnd;
        if (r.writes) { out.released = true; out.releasedUs = r.txEnd + 20; }
        return TxState::IDLE;
    }
    static ReadState read(void*, uint64_t at, RxByte&, uint64_t& through) {
        through = at; return ReadState::EMPTY;
    }
    Port port() {
        Port p; p.context = this; p.setTransmit = direction; p.write = write;
        p.txState = txState; p.read = read; return p;
    }
    Storage storage() {
        Storage s; s.tx = tx; s.rx = rx; s.txCapacity = s.rxCapacity = MAX_FRAME; return s;
    }
    static Timing timing() {
        Timing t; t.gap15Us = 150; t.gap35Us = 350; t.setupUs = 10; t.holdUs = 20;
        t.busTimeoutUs = t.txTimeoutUs = 10000; t.captureTimeoutUs = 10000; return t;
    }
    BusStorage busStorage() {
        BusStorage s; s.pending = pending; s.pendingCapacity = 3; s.results = results;
        s.resultCapacity = 4; s.producers = producers; s.producerCapacity = 2;
        s.urgentPendingCapacity = s.urgentResultCapacity = 1; return s;
    }
    Rig() : runner(port(), storage(), timing()), owner(runner, busStorage()) { assert(owner.valid()); }
    BusRequest request() {
        BusRequest b; b.wire.bytes = bytes;
        b.wire.length = Ess::buildReadRegisters(1, 0, 1, bytes, sizeof(bytes));
        b.wire.replyLength = 7; b.wire.responseTimeoutUs = 1000; b.wire.deadlineUs = 20000;
        b.expected.address = 1; b.expected.function = 3; b.expected.count = 1;
        b.validator = essValidator(); return b;
    }
    void service(uint64_t at) { now = at; owner.service(at); }
};

void exclusionAndStaleTokens() {
    Rig r; auto request = r.request();
    SequenceId previous;
    assert(r.owner.beginSequence(1, 20000, 1000, previous));
    uint64_t token = 0; assert(r.owner.beginCommissioning(1000, token) && token);
    uint64_t unchanged = 123; assert(!r.owner.beginCommissioning(1000, unchanged) && unchanged == 123);
    RequestId id; id.generation = 99;
    assert(r.owner.admit(request, 1000, id) == BusAdmission::CONFIGURING);
    assert(r.owner.admitUrgent(request, 1000, id) == BusAdmission::CONFIGURING);
    assert(id.generation == 99 && !r.writes && !r.owner.pending());
    SequenceId blocked; assert(!r.owner.beginSequence(0, 20000, 1000, blocked));
    assert(r.owner.admitCommissioning(request, 0, 1000, id) == BusAdmission::INVALID);
    request.producer = 1; request.sequence = previous;
    assert(r.owner.admitCommissioning(request, token, 1000, id) == BusAdmission::INVALID);
    request.sequence = SequenceId();
    assert(r.owner.admitCommissioning(request, token, 1000, id) == BusAdmission::ACCEPTED);
    assert(!r.owner.endCommissioning(token, 1000));
    assert(r.owner.cancel(id, 1000) == Cancel::CANCELLED);
    const auto* retained = r.owner.result(id); assert(retained && retained->outcome == Outcome::CANCELLED);
    assert(r.owner.endCommissioning(token, 1000) && !r.owner.commissioningOwned());
    assert(r.owner.result(id) == retained && !r.writes);
    assert(!r.owner.endCommissioning(token, 1000));
    assert(r.owner.admitCommissioning(request, token, 1000, id) == BusAdmission::INVALID);
    uint64_t next = 0; assert(r.owner.beginCommissioning(1000, next) && next != token);
    assert(!r.owner.endCommissioning(token, 1000));
    assert(r.owner.admitCommissioning(request, token, 1000, id) == BusAdmission::INVALID);
    assert(r.owner.endCommissioning(next, 1000));
    request.sequence = previous;
    assert(r.owner.admit(request, 1000, id) == BusAdmission::INVALID);
    request.sequence = SequenceId();
    assert(r.owner.admit(request, 1000, id) == BusAdmission::ACCEPTED);
}

void busyEntryPreservesWork() {
    Rig r; auto request = r.request(); RequestId id;
    assert(r.owner.admit(request, 1000, id) == BusAdmission::ACCEPTED);
    uint64_t token = 77;
    assert(!r.owner.beginCommissioning(1000, token) && token == 77);
    assert(r.owner.pending() == 1 && !r.owner.result(id));
    r.service(1000);
    assert(r.owner.active() && !r.owner.beginCommissioning(1000, token));
    for (uint64_t at = 1050; at <= 1400; at += 50) r.service(at);
    assert(r.writes == 1 && r.runner.transmitEnabled());
    assert(!r.owner.beginCommissioning(1400, token) && token == 77);
}

void configurationKeepsLease() {
    Rig r; uint64_t token = 0; RequestId id; auto request = r.request();
    assert(r.owner.beginCommissioning(1000, token));
    assert(r.owner.beginConfiguration(1000));
    auto invalid = Rig::timing(); invalid.busTimeoutUs = 1;
    assert(!r.owner.finishConfiguration(invalid, 1000));
    assert(r.owner.configurationOwned() && r.owner.commissioningOwned());
    assert(!r.owner.endCommissioning(token, 1000));
    assert(r.owner.admitCommissioning(request, token, 1000, id) == BusAdmission::CONFIGURING);
    assert(r.owner.finishConfiguration(Rig::timing(), 1000));
    assert(!r.owner.configurationOwned() && r.owner.commissioningOwned());
    assert(r.owner.admit(request, 1000, id) == BusAdmission::CONFIGURING);
    assert(r.owner.admitCommissioning(request, token, 1000, id) == BusAdmission::ACCEPTED);
    assert(r.owner.cancel(id, 1000) == Cancel::CANCELLED);
    assert(r.owner.endCommissioning(token, 1000));
}

void recoveryKeepsUncertaintyAndLease() {
    Rig r; uint64_t token = 0; RequestId id; auto request = r.request();
    assert(r.owner.beginCommissioning(1000, token));
    assert(r.owner.admitCommissioning(request, token, 1000, id) == BusAdmission::ACCEPTED);
    for (uint64_t at = 1000; at <= 10000; at += 50) r.service(at);
    const auto* result = r.owner.result(id);
    assert(result && result->executionUnknown && result->transport.txAccepted == 8);
    assert(r.writes == 1 && r.owner.needsRecovery());
    assert(!r.owner.endCommissioning(token, 10000));
    uint64_t recovery = 0;
    assert(r.owner.recover(10000, 15000, recovery) == RecoveryAdmission::ACCEPTED);
    assert(!r.owner.endCommissioning(token, 10000) && r.owner.commissioningOwned());
    r.service(10350);
    assert(r.owner.recoveryResult(recovery)->outcome == RecoveryOutcome::RECOVERED);
    assert(!r.owner.needsRecovery() && r.owner.commissioningOwned());
    RequestId rejected;
    assert(r.owner.admit(request, 10350, rejected) == BusAdmission::CONFIGURING);
    assert(r.owner.result(id) == result && result->executionUnknown && r.writes == 1);
    assert(r.owner.endCommissioning(token, 10350));
    r.service(11000); assert(r.writes == 1);
}
}
int main() {
    exclusionAndStaleTokens(); busyEntryPreservesWork(); configurationKeepsLease();
    recoveryKeepsUncertaintyAndLease();
}
