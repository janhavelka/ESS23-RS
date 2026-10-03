// SPDX-License-Identifier: MIT
#include "RtuBusOwner.h"
#include <cstring>
#include <limits>

namespace MotorControlRSExample { namespace Rtu {
namespace {
bool overlap(const void* a, std::size_t sizeA, const void* b, std::size_t sizeB) noexcept {
    if (!sizeA || !sizeB) return false;
    const uintptr_t x = reinterpret_cast<uintptr_t>(a), y = reinterpret_cast<uintptr_t>(b);
    return x <= y ? y - x < sizeA : x - y < sizeB;
}
}

BusOwner::BusOwner(Runner& runner, const BusStorage& storage) noexcept
    : runner_(runner), storage_(storage) {
    if (storage.pendingCapacity > 65535 || storage.resultCapacity > 65535 ||
        (storage.pendingCapacity && !storage.pending) ||
        (storage.resultCapacity && !storage.results)) return;
    const std::size_t pendingBytes = storage.pendingCapacity * sizeof(PendingSlot);
    const std::size_t resultBytes = storage.resultCapacity * sizeof(ResultSlot);
    if (overlap(storage.pending, pendingBytes, storage.results, resultBytes) ||
        runner.storageOverlaps(storage.pending, pendingBytes) ||
        runner.storageOverlaps(storage.results, resultBytes) ||
        runner.storageOverlaps(this, sizeof(*this)) ||
        overlap(this, sizeof(*this), storage.pending, pendingBytes) ||
        overlap(this, sizeof(*this), storage.results, resultBytes) ||
        overlap(&runner, sizeof(runner), storage.pending, pendingBytes) ||
        overlap(&runner, sizeof(runner), storage.results, resultBytes) ||
        runner.busy() || runner.needsRecovery()) return;
    for (std::size_t i = 0; i < storage.pendingCapacity; ++i) storage.pending[i] = PendingSlot();
    for (std::size_t i = 0; i < storage.resultCapacity; ++i) storage.results[i] = ResultSlot();
    valid_ = true;
}

bool BusOwner::clock(uint64_t nowUs) noexcept {
    return runner_.checkClock(nowUs);
}

BusAdmission BusOwner::admit(const BusRequest& request, uint64_t nowUs, RequestId& output) noexcept {
    if (!valid_ || !clock(nowUs) || !request.wire.deadlineUs ||
        !runner_.accepts(request.wire) || !request.validator.checkRequest ||
        !request.validator.checkReply || request.expected.address != request.wire.bytes[0] ||
        request.expected.function != request.wire.bytes[1] ||
        !request.validator.checkRequest(request.expected, request.wire)) return BusAdmission::INVALID;
    if (nowUs >= request.wire.deadlineUs) return BusAdmission::EXPIRED;
    if (count_ == storage_.pendingCapacity) return BusAdmission::QUEUE_FULL;
    std::size_t slot = NONE;
    bool free = false;
    for (std::size_t i = 0; i < storage_.resultCapacity; ++i) {
        const ResultSlot& candidate = storage_.results[i];
        if (candidate.state != ResultSlot::State::FREE) continue;
        free = true;
        if (candidate.generation != std::numeric_limits<uint64_t>::max()) { slot = i; break; }
    }
    if (slot == NONE) return free ? BusAdmission::IDS_EXHAUSTED : BusAdmission::RESULTS_FULL;
    // Copy first: admission may use a previously retained raw buffer as input.
    PendingSlot& pending = storage_.pending[(head_ + count_) % storage_.pendingCapacity];
    std::memmove(pending.bytes, request.wire.bytes, request.wire.length);
    pending.request = request;
    pending.request.wire.bytes = pending.bytes;
    pending.resultSlot = slot;
    ResultSlot& result = storage_.results[slot];
    ++result.generation;
    result.completion = Completion();
    Completion& completion = result.completion;
    completion.id.owner = this;
    completion.id.slot = slot;
    completion.id.generation = result.generation;
    completion.expected = request.expected;
    completion.admittedUs = nowUs;
    completion.deadlineUs = request.wire.deadlineUs;
    completion.txLength = request.wire.length;
    completion.replyLength = request.wire.replyLength;
    completion.responseTimeoutUs = request.wire.responseTimeoutUs;
    completion.replyGapUs = request.wire.replyGapUs;
    completion.echo = request.wire.echo;
    result.state = ResultSlot::State::RESERVED;
    ++count_;
    output = completion.id;
    return BusAdmission::ACCEPTED;
}

ResultSlot* BusOwner::find(const RequestId& id) const noexcept {
    if (!valid_ || id.owner != this || !id.generation || id.slot >= storage_.resultCapacity) return nullptr;
    ResultSlot& slot = storage_.results[id.slot];
    return slot.generation == id.generation && slot.state == ResultSlot::State::READY ? &slot : nullptr;
}

const Completion* BusOwner::result(const RequestId& id) const noexcept {
    const ResultSlot* slot = find(id);
    return slot ? &slot->completion : nullptr;
}

bool BusOwner::release(const RequestId& id) noexcept {
    ResultSlot* slot = find(id);
    if (!slot) return false;
    slot->state = ResultSlot::State::FREE;
    return true;
}

void BusOwner::collect() noexcept {
    ResultSlot& slot = storage_.results[active_];
    Completion& completion = slot.completion;
    completion.transport = runner_.result();
    std::memcpy(completion.raw, runner_.received(), completion.transport.rxLength);
    if (completion.transport.reason == Reason::FRAME) {
        // The active reservation and dispatch barrier remain until parser returns.
        completion.validation = activeValidator_.checkReply(completion.expected, completion.raw,
                                                             completion.transport.rxLength);
        if (completion.validation) completion.outcome = Outcome::SUCCESS;
        else if (completion.validation.code == MotorControlRS::Err::EXCEPTION)
            completion.outcome = Outcome::DEVICE_REJECTED;
        else {
            completion.outcome = Outcome::INVALID_REPLY;
            runner_.rejectFrame();
        }
    } else completion.outcome = Outcome::TRANSPORT;
    completion.executionUnknown = completion.transport.txAccepted != 0 &&
        completion.outcome != Outcome::SUCCESS && completion.outcome != Outcome::DEVICE_REJECTED;
    slot.state = ResultSlot::State::READY;
    active_ = NONE;
    activeValidator_ = Validator();
}

void BusOwner::service(uint64_t nowUs) noexcept {
    if (!valid_ || !clock(nowUs)) return;
    runner_.poll(nowUs); // Also permits fault DE cleanup; never clears recovery.
    if (active_ != NONE && !runner_.busy()) collect();

    // Expire any queued request, even behind a live request or recovery interlock.
    // Compact this bounded FIFO in place; admitted frames do not borrow its slots.
    const std::size_t before = count_;
    std::size_t kept = 0;
    for (std::size_t i = 0; i < before; ++i) {
        PendingSlot& pending = storage_.pending[(head_ + i) % storage_.pendingCapacity];
        if (nowUs >= pending.request.wire.deadlineUs) {
            ResultSlot& slot = storage_.results[pending.resultSlot];
            slot.completion.outcome = Outcome::QUEUE_EXPIRED;
            slot.completion.transport.reason = Reason::REQUEST_DEADLINE;
            slot.completion.transport.endedUs = nowUs;
            slot.state = ResultSlot::State::READY;
        } else {
            PendingSlot& destination = storage_.pending[(head_ + kept) % storage_.pendingCapacity];
            if (&destination != &pending) destination = pending;
            destination.request.wire.bytes = destination.bytes;
            ++kept;
        }
    }
    count_ = kept;
    if (active_ != NONE || runner_.needsRecovery() || !count_) return;
    PendingSlot& pending = storage_.pending[head_];
    const Admission admission = runner_.start(pending.request.wire, nowUs);
    if (admission != Admission::STARTED) return; // No slot/result loss on refusal.
    active_ = pending.resultSlot;
    activeValidator_ = pending.request.validator;
    head_ = (head_ + 1) % storage_.pendingCapacity;
    --count_;
}

bool BusOwner::recover(uint64_t nowUs) noexcept {
    if (!valid_ || !clock(nowUs) || active_ != NONE || count_ != 0) return false;
    return runner_.recover(nowUs);
}

}} // namespace MotorControlRSExample::Rtu
