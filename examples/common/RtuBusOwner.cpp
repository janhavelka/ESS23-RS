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
bool sameSequence(const SequenceId& a, const SequenceId& b) noexcept {
    return a.owner == b.owner && a.producer == b.producer && a.generation == b.generation;
}
}

BusOwner::BusOwner(Runner& runner, const BusStorage& storage) noexcept
    : runner_(runner), storage_(storage) {
    if (storage.pendingCapacity > 65535 || storage.resultCapacity > 65535 ||
        !storage.producerCapacity || storage.producerCapacity > 65535 || !storage.producers ||
        storage.urgentPendingCapacity > storage.pendingCapacity ||
        storage.urgentResultCapacity > storage.resultCapacity ||
        (storage.pendingCapacity && !storage.pending) ||
        (storage.resultCapacity && !storage.results)) return;
    const void* regions[] = {storage.pending, storage.results, storage.producers, this, &runner};
    const std::size_t sizes[] = {storage.pendingCapacity * sizeof(PendingSlot),
        storage.resultCapacity * sizeof(ResultSlot), storage.producerCapacity * sizeof(ProducerSlot),
        sizeof(*this), sizeof(runner)};
    for (std::size_t i = 0; i < 4; ++i) {
        if (runner.storageOverlaps(regions[i], sizes[i])) return;
        for (std::size_t j = i + 1; j < 5; ++j)
            if (overlap(regions[i], sizes[i], regions[j], sizes[j])) return;
    }
    if (runner.busy() || runner.needsRecovery()) return;
    for (std::size_t i = 0; i < storage.pendingCapacity; ++i) storage.pending[i] = PendingSlot();
    for (std::size_t i = 0; i < storage.resultCapacity; ++i) storage.results[i] = ResultSlot();
    for (std::size_t i = 0; i < storage.producerCapacity; ++i) storage.producers[i] = ProducerSlot();
    valid_ = true;
}

bool BusOwner::clock(uint64_t nowUs) noexcept { return runner_.checkClock(nowUs); }
PendingSlot& BusOwner::queued(std::size_t index) const noexcept {
    return storage_.pending[(head_ + index) % storage_.pendingCapacity];
}
void BusOwner::remove(std::size_t index) noexcept {
    for (std::size_t i = index; i + 1 < count_; ++i) {
        queued(i) = queued(i + 1);
        queued(i).request.wire.bytes = queued(i).bytes;
    }
    --count_;
}

bool BusOwner::sequenceValid(const BusRequest& request) const noexcept {
    if (request.producer >= storage_.producerCapacity) return false;
    const SequenceId& id = request.sequence;
    if (!id.owner && !id.generation) return true;
    const ProducerSlot& producer = storage_.producers[request.producer];
    return id.owner == this && id.producer == request.producer && id.generation &&
        id.generation == producer.generation && producer.deadlineUs &&
        request.wire.deadlineUs == producer.deadlineUs;
}

BusAdmission BusOwner::admit(const BusRequest& request, uint64_t nowUs, RequestId& output) noexcept {
    return admit(request, nowUs, output, false);
}
BusAdmission BusOwner::admitUrgent(const BusRequest& request, uint64_t nowUs, RequestId& output) noexcept {
    return admit(request, nowUs, output, true);
}
BusAdmission BusOwner::admit(const BusRequest& request, uint64_t nowUs, RequestId& output, bool urgent) noexcept {
    if (configurationOwned_) return BusAdmission::CONFIGURING;
    if (!valid_ || !clock(nowUs)) return BusAdmission::INVALID;
    if (recovering()) return BusAdmission::RECOVERING;
    const uint64_t dispatch = request.dispatchDeadlineUs ? request.dispatchDeadlineUs : request.wire.deadlineUs;
    if (!request.wire.deadlineUs || !runner_.accepts(request.wire) || !sequenceValid(request) ||
        !request.validator.checkRequest || !request.validator.checkReply ||
        request.expected.address != request.wire.bytes[0] || request.expected.function != request.wire.bytes[1] ||
        dispatch > request.wire.deadlineUs || request.notBeforeUs >= dispatch ||
        (urgent && request.notBeforeUs > nowUs) ||
        !request.validator.checkRequest(request.expected, request.wire)) return BusAdmission::INVALID;
    if (nowUs >= dispatch) return BusAdmission::EXPIRED;
    std::size_t urgentCount = 0;
    for (std::size_t i = 0; i < count_; ++i)
        if (storage_.results[queued(i).resultSlot].completion.urgent) ++urgentCount;
    if (urgent ? urgentCount == storage_.urgentPendingCapacity :
        count_ - urgentCount == storage_.pendingCapacity - storage_.urgentPendingCapacity)
        return urgent ? BusAdmission::URGENT_FULL : BusAdmission::QUEUE_FULL;
    const std::size_t ordinaryResults = storage_.resultCapacity - storage_.urgentResultCapacity;
    const std::size_t first = urgent ? ordinaryResults : 0;
    const std::size_t end = urgent ? storage_.resultCapacity : ordinaryResults;
    std::size_t slot = NONE;
    bool free = false;
    for (std::size_t i = first; i < end; ++i) {
        const ResultSlot& candidate = storage_.results[i];
        if (candidate.state != ResultSlot::State::FREE) continue;
        free = true;
        if (candidate.generation != std::numeric_limits<uint64_t>::max()) { slot = i; break; }
    }
    if (slot == NONE) return free ? BusAdmission::IDS_EXHAUSTED :
        urgent ? BusAdmission::URGENT_FULL : BusAdmission::RESULTS_FULL;
    PendingSlot& pending = queued(count_);
    std::memmove(pending.bytes, request.wire.bytes, request.wire.length);
    pending.request = request;
    pending.request.wire.bytes = pending.bytes;
    pending.request.dispatchDeadlineUs = dispatch;
    pending.resultSlot = slot;
    ResultSlot& result = storage_.results[slot];
    ++result.generation;
    result.completion = Completion();
    Completion& completion = result.completion;
    completion.id.owner = this; completion.id.slot = slot; completion.id.generation = result.generation;
    completion.expected = request.expected; completion.producer = request.producer;
    completion.sequence = request.sequence; completion.notBeforeUs = request.notBeforeUs;
    completion.dispatchDeadlineUs = dispatch; completion.urgent = urgent;
    completion.admittedUs = nowUs; completion.deadlineUs = request.wire.deadlineUs;
    completion.txLength = request.wire.length; completion.replyLength = request.wire.replyLength;
    completion.responseTimeoutUs = request.wire.responseTimeoutUs; completion.replyGapUs = request.wire.replyGapUs;
    completion.echo = request.wire.echo;
    result.state = ResultSlot::State::RESERVED;
    ++count_;
    output = completion.id;
    return BusAdmission::ACCEPTED;
}

ResultSlot* BusOwner::reservation(const RequestId& id) const noexcept {
    if (!valid_ || id.owner != this || !id.generation || id.slot >= storage_.resultCapacity) return nullptr;
    ResultSlot& slot = storage_.results[id.slot];
    return slot.generation == id.generation && slot.state != ResultSlot::State::FREE ? &slot : nullptr;
}
ResultSlot* BusOwner::find(const RequestId& id) const noexcept {
    ResultSlot* slot = reservation(id);
    return slot && slot->state == ResultSlot::State::READY ? slot : nullptr;
}
const Completion* BusOwner::result(const RequestId& id) const noexcept {
    const ResultSlot* slot = find(id); return slot ? &slot->completion : nullptr;
}
bool BusOwner::release(const RequestId& id) noexcept {
    ResultSlot* slot = find(id); if (!slot) return false;
    slot->state = ResultSlot::State::FREE; return true;
}

void BusOwner::collect() noexcept {
    ResultSlot& slot = storage_.results[active_];
    Completion& completion = slot.completion;
    completion.transport = runner_.result();
    std::memcpy(completion.raw, runner_.received(), completion.transport.rxLength);
    if (completion.transport.reason == Reason::FRAME) {
        completion.validation = activeValidator_.checkReply(completion.expected, completion.raw,
                                                            completion.transport.rxLength);
        if (completion.validation) completion.outcome = Outcome::SUCCESS;
        else if (completion.validation.code == MotorControlRS::Err::EXCEPTION)
            completion.outcome = Outcome::DEVICE_REJECTED;
        else { completion.outcome = Outcome::INVALID_REPLY; runner_.rejectFrame(); }
    } else completion.outcome = completion.transport.reason == Reason::CANCELLED ? Outcome::CANCELLED : Outcome::TRANSPORT;
    completion.executionUnknown = completion.transport.txAccepted != 0 &&
        completion.outcome != Outcome::SUCCESS && completion.outcome != Outcome::DEVICE_REJECTED;
    // Recovery remains explicit even if a historical reply completes after its
    // recovery attempt expired. Preserve that reply, but keep the bus interlocked.
    if (recovery_.id && recovery_.interrupted.owner == this &&
        recovery_.interrupted.slot == active_ &&
        recovery_.interrupted.generation == completion.id.generation)
        runner_.requireRecovery();
    slot.state = ResultSlot::State::READY;
    active_ = NONE;
    activeValidator_ = Validator();
}
void BusOwner::queuedResult(PendingSlot& pending, Outcome outcome, uint64_t nowUs, Cancellation cause) noexcept {
    if (nowUs >= pending.request.wire.deadlineUs) outcome = Outcome::QUEUE_EXPIRED;
    else if (nowUs >= pending.request.dispatchDeadlineUs) outcome = Outcome::DISPATCH_EXPIRED;
    ResultSlot& slot = storage_.results[pending.resultSlot];
    slot.completion.outcome = outcome; slot.completion.cancellation = cause;
    slot.completion.transport.reason = outcome == Outcome::CANCELLED ? Reason::CANCELLED : Reason::REQUEST_DEADLINE;
    slot.completion.transport.endedUs = nowUs; slot.state = ResultSlot::State::READY;
}
void BusOwner::cancelActive(uint64_t nowUs, Cancellation cause) noexcept {
    if (active_ == NONE) return;
    const Phase phase = runner_.phase();
    const Completion& completion = storage_.results[active_].completion;
    // Do not poll an on-time unsent transaction just to cancel it: that could enqueue TX.
    if ((phase != Phase::WAIT_BUS && phase != Phase::SETUP) || nowUs >= completion.deadlineUs)
        runner_.poll(nowUs);
    if (runner_.busy()) {
        if (storage_.results[active_].completion.cancellation == Cancellation::NONE)
            storage_.results[active_].completion.cancellation = cause;
        // Expired requests must finish examining captured history across read budgets.
        // A qualified on-time closure takes precedence over a late cancellation.
        if (nowUs < completion.deadlineUs) runner_.cancelCaptured(nowUs);
    }
    if (!runner_.busy()) collect();
}
void BusOwner::advanceGeneration(std::size_t index) noexcept {
    ProducerSlot& producer = storage_.producers[index];
    if (producer.generation) producer.generation = producer.generation == std::numeric_limits<uint64_t>::max() ?
        0 : producer.generation + 1;
    producer.deadlineUs = 0;
}
void BusOwner::cancelGroup(std::size_t producer, const SequenceId* sequence, uint64_t nowUs, Cancellation cause) noexcept {
    const std::size_t before = count_;
    std::size_t kept = 0;
    for (std::size_t i = 0; i < before; ++i) {
        PendingSlot& pending = queued(i);
        if (pending.request.producer == producer && (!sequence || sameSequence(pending.request.sequence, *sequence))) {
            queuedResult(pending, Outcome::CANCELLED, nowUs, cause);
        } else {
            PendingSlot& destination = queued(kept);
            if (&destination != &pending) destination = pending;
            destination.request.wire.bytes = destination.bytes;
            ++kept;
        }
    }
    count_ = kept;
    if (active_ != NONE) {
        const Completion& completion = storage_.results[active_].completion;
        if (completion.producer == producer && (!sequence || sameSequence(completion.sequence, *sequence)))
            cancelActive(nowUs, cause);
    }
}
bool BusOwner::beginConfiguration(uint64_t nowUs) noexcept {
    if (!valid_ || configurationOwned_ || active() || pending() || needsRecovery() ||
        runner_.busy() || runner_.transmitEnabled() || !clock(nowUs)) return false;
    configurationOwned_ = true;
    return true;
}
bool BusOwner::finishConfiguration(const Timing& timing, uint64_t nowUs) noexcept {
    if (!configurationOwned_ || !runner_.configureTiming(timing, nowUs)) return false;
    configurationOwned_ = false;
    return true;
}

bool BusOwner::beginSequence(std::size_t producer, uint64_t deadline, uint64_t nowUs, SequenceId& output) noexcept {
    if (configurationOwned_) return false;
    if (!valid_ || !clock(nowUs) || recovering() || producer >= storage_.producerCapacity || deadline <= nowUs ||
        !storage_.producers[producer].generation ||
        storage_.producers[producer].generation == std::numeric_limits<uint64_t>::max()) return false;
    if (active_ != NONE && storage_.results[active_].completion.producer == producer) return false;
    for (std::size_t i = 0; i < count_; ++i) if (queued(i).request.producer == producer) return false;
    advanceGeneration(producer);
    ProducerSlot& slot = storage_.producers[producer];
    slot.deadlineUs = deadline; output.owner = this; output.producer = producer; output.generation = slot.generation;
    return true;
}
bool BusOwner::invalidate(const SequenceId& id, uint64_t nowUs) noexcept {
    if (!valid_ || !clock(nowUs) || id.owner != this || id.producer >= storage_.producerCapacity || !id.generation ||
        id.generation != storage_.producers[id.producer].generation || !storage_.producers[id.producer].deadlineUs) return false;
    advanceGeneration(id.producer);
    cancelGroup(id.producer, &id, nowUs, Cancellation::GENERATION);
    return true;
}
bool BusOwner::invalidateProducer(std::size_t producer, uint64_t nowUs) noexcept {
    if (!valid_ || !clock(nowUs) || producer >= storage_.producerCapacity) return false;
    advanceGeneration(producer);
    cancelGroup(producer, nullptr, nowUs, Cancellation::GENERATION);
    return true;
}
Cancel BusOwner::cancel(const RequestId& id, uint64_t nowUs) noexcept {
    if (!valid_ || !clock(nowUs)) return Cancel::INVALID;
    ResultSlot* slot = reservation(id); if (!slot) return Cancel::INVALID;
    if (slot->state == ResultSlot::State::READY) return Cancel::ALREADY_TERMINAL;
    const SequenceId sequence = slot->completion.sequence;
    if (sequence.owner) {
        if (storage_.producers[sequence.producer].generation == sequence.generation)
            advanceGeneration(sequence.producer);
        cancelGroup(sequence.producer, &sequence, nowUs, Cancellation::REQUEST);
    } else if (active_ == id.slot) cancelActive(nowUs, Cancellation::REQUEST);
    else {
        for (std::size_t i = 0; i < count_; ++i) if (queued(i).resultSlot == id.slot) {
            queuedResult(queued(i), Outcome::CANCELLED, nowUs, Cancellation::REQUEST); remove(i); break;
        }
    }
    return slot->state == ResultSlot::State::READY && slot->completion.outcome != Outcome::CANCELLED ?
        Cancel::ALREADY_TERMINAL : Cancel::CANCELLED;
}

bool BusOwner::cancelUnsent(const RequestId& id, uint64_t nowUs) noexcept {
    if (!valid_ || !clock(nowUs)) return false;
    ResultSlot* slot = reservation(id);
    if (!slot || slot->state == ResultSlot::State::READY) return false;
    if (active_ == id.slot) {
        if ((runner_.phase() != Phase::WAIT_BUS && runner_.phase() != Phase::SETUP) ||
            runner_.result().txAccepted) return false;
        cancelActive(nowUs, Cancellation::REQUEST);
        return true;
    }
    for (std::size_t i = 0; i < count_; ++i) if (queued(i).resultSlot == id.slot) {
        queuedResult(queued(i), Outcome::CANCELLED, nowUs, Cancellation::REQUEST);
        remove(i); return true;
    }
    return false;
}
std::size_t BusOwner::txAccepted(const RequestId& id) const noexcept {
    const ResultSlot* slot = reservation(id);
    if (!slot) return 0;
    return active_ == id.slot ? runner_.result().txAccepted : slot->completion.transport.txAccepted;
}

void BusOwner::finishRecovery(RecoveryOutcome outcome, uint64_t nowUs, Reason reason) noexcept {
    recovery_.outcome = outcome; recovery_.finishedUs = nowUs; recovery_.reason = reason;
    recoveryState_ = RecoveryState::READY;
}
RecoveryAdmission BusOwner::recover(uint64_t nowUs, uint64_t deadline, uint64_t& output) noexcept {
    if (configurationOwned_) return RecoveryAdmission::INVALID;
    if (!valid_ || !clock(nowUs) || deadline <= nowUs) return RecoveryAdmission::INVALID;
    if (recoveryState_ != RecoveryState::FREE) return RecoveryAdmission::RESULTS_FULL;
    if (recoveryGeneration_ == std::numeric_limits<uint64_t>::max()) return RecoveryAdmission::IDS_EXHAUSTED;
    recovery_ = RecoveryResult();
    recovery_.id = ++recoveryGeneration_;
    recovery_.requestedUs = nowUs;
    recovery_.deadlineUs = deadline;
    recoveryQuietUs_ = nowUs;
    if (active_ != NONE) recovery_.interrupted = storage_.results[active_].completion.id;
    recoveryState_ = RecoveryState::PENDING;
    output = recovery_.id;
    for (std::size_t i = 0; i < storage_.producerCapacity; ++i) advanceGeneration(i);
    for (std::size_t i = 0; i < count_; ++i) queuedResult(queued(i), Outcome::CANCELLED, nowUs, Cancellation::RECOVERY);
    count_ = 0;
    cancelActive(nowUs, Cancellation::RECOVERY);
    if (active_ == NONE) runner_.requireRecovery();
    return RecoveryAdmission::ACCEPTED;
}
const RecoveryResult* BusOwner::recoveryResult(uint64_t id) const noexcept {
    return recoveryState_ == RecoveryState::READY && id && id == recovery_.id ? &recovery_ : nullptr;
}
bool BusOwner::releaseRecovery(uint64_t id) noexcept {
    if (!recoveryResult(id)) return false;
    recoveryState_ = RecoveryState::FREE; return true;
}
void BusOwner::serviceRecovery(uint64_t nowUs, bool mayDrain) noexcept {
    if (active_ == NONE) runner_.requireRecovery();
    if (nowUs >= recovery_.deadlineUs) {
        finishRecovery(RecoveryOutcome::EXPIRED, nowUs, Reason::REQUEST_DEADLINE);
        return;
    }
    if (active_ != NONE || runner_.transmitEnabled() || !mayDrain) return;
    const DrainResult drained = runner_.discard(nowUs);
    if (drained.lastByteUs > recoveryQuietUs_) recoveryQuietUs_ = drained.lastByteUs;
    if (drained.state == ReadState::ERROR) {
        finishRecovery(RecoveryOutcome::READ_ERROR, nowUs, drained.reason);
        return;
    }
    if (drained.state != ReadState::EMPTY || drained.throughUs != nowUs ||
        nowUs - recoveryQuietUs_ < runner_.idleGapUs()) return;
    finishRecovery(runner_.recover(nowUs) ? RecoveryOutcome::RECOVERED : RecoveryOutcome::TRANSPORT_ERROR, nowUs);
}

void BusOwner::service(uint64_t nowUs, bool recoveryReady) noexcept {
    if (configurationOwned_) return;
    if (!valid_ || !clock(nowUs)) return;
    const bool hadActive = active_ != NONE;
    runner_.poll(nowUs);
    if (active_ != NONE && !runner_.busy()) collect();
    if (recovering()) { serviceRecovery(nowUs, !hadActive && recoveryReady); return; }
    const std::size_t before = count_; std::size_t kept = 0;
    for (std::size_t i = 0; i < before; ++i) {
        PendingSlot& pending = queued(i);
        if (nowUs >= pending.request.wire.deadlineUs) queuedResult(pending, Outcome::QUEUE_EXPIRED, nowUs, Cancellation::NONE);
        else if (nowUs >= pending.request.dispatchDeadlineUs) queuedResult(pending, Outcome::DISPATCH_EXPIRED, nowUs, Cancellation::NONE);
        else if (!sequenceValid(pending.request)) queuedResult(pending, Outcome::CANCELLED, nowUs, Cancellation::GENERATION);
        else {
            PendingSlot& destination = queued(kept);
            if (&destination != &pending) destination = pending;
            destination.request.wire.bytes = destination.bytes; ++kept;
        }
    }
    count_ = kept;
    if (active_ != NONE || runner_.needsRecovery() || !count_) return;
    for (std::size_t p = 0; p < storage_.producerCapacity; ++p) storage_.producers[p].head = NONE;
    std::size_t selected = NONE;
    for (std::size_t i = 0; i < count_; ++i) {
        PendingSlot& pending = queued(i);
        if (storage_.results[pending.resultSlot].completion.urgent) { selected = i; break; }
        ProducerSlot& producer = storage_.producers[pending.request.producer];
        if (producer.head == NONE) producer.head = i;
    }
    if (selected == NONE) for (std::size_t offset = 0; offset < storage_.producerCapacity; ++offset) {
        const std::size_t p = (nextProducer_ + offset) % storage_.producerCapacity;
        const std::size_t i = storage_.producers[p].head;
        if (i != NONE && queued(i).request.notBeforeUs <= nowUs) { selected = i; break; }
    }
    if (selected == NONE) return;
    PendingSlot& pending = queued(selected);
    if (runner_.start(pending.request.wire, nowUs) != Admission::STARTED) return;
    active_ = pending.resultSlot; activeValidator_ = pending.request.validator;
    if (!storage_.results[active_].completion.urgent)
        nextProducer_ = (pending.request.producer + 1) % storage_.producerCapacity;
    if (selected == 0) { head_ = (head_ + 1) % storage_.pendingCapacity; --count_; }
    else remove(selected);
}

}} // namespace MotorControlRSExample::Rtu
