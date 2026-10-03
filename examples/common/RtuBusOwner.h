// SPDX-License-Identifier: MIT
#pragma once

#include "RtuRunner.h"
#include <MotorControlRS/Status.h>

namespace MotorControlRSExample { namespace Rtu {

class BusOwner;

/** Host identity, never a wire transaction ID. Valid only for the originating
 * live owner. Reuse on admission increments generation; generation never wraps.
 * All IDs expire with owner lifetime; do not reuse IDs across reconstruction. */
struct RequestId {
    const BusOwner* owner = nullptr;
    std::size_t slot = 0;
    uint64_t generation = 0;
};

/** One producer's optional sequence. No wire identity or axis reservation.
 * Tokens expire on cancellation, invalidation, replacement or recovery. */
struct SequenceId {
    const BusOwner* owner = nullptr;
    std::size_t producer = 0;
    uint64_t generation = 0;
};

/** Copied profile expectations and application target/configuration identity.
 * first/count/value are exact RTU register expectations, not decoded motion. */
struct Expectation {
    uint32_t target = 0;
    uint32_t targetGeneration = 0;
    uint16_t first = 0, count = 0, value = 0;
    uint8_t address = 0, function = 0;
};

/** Both callbacks are bounded, synchronous, non-reentrant and retain no pointers.
 * checkRequest verifies profile access, CRC, exact bytes/expectations/reply size.
 * checkReply calls the checked profile parser; EXCEPTION is a checked rejection.
 * Every other error interlocks the runner until explicit recovery. No borrowed
 * callback context is retained; all per-request expectations are copied values. */
struct Validator {
    bool (*checkRequest)(const Expectation&, const Request&) = nullptr;
    MotorControlRS::Status (*checkReply)(const Expectation&, const uint8_t*, std::size_t) = nullptr;
};

struct BusRequest {
    Request wire; ///< deadlineUs is required and remains absolute from admission.
    Expectation expected;
    Validator validator;
    std::size_t producer = 0;
    SequenceId sequence; ///< Empty for independent transactions.
    uint64_t notBeforeUs = 0; ///< Deferred ordinary work releases the bus.
    uint64_t dispatchDeadlineUs = 0; ///< Latest Runner handoff; zero uses wire deadline.
};

enum class BusAdmission : uint8_t {
    ACCEPTED, INVALID, EXPIRED, QUEUE_FULL, RESULTS_FULL, IDS_EXHAUSTED, URGENT_FULL, RECOVERING
};
enum class Outcome : uint8_t {
    QUEUE_EXPIRED, TRANSPORT, SUCCESS, DEVICE_REJECTED, INVALID_REPLY, CANCELLED, DISPATCH_EXPIRED
};
enum class Cancel : uint8_t { CANCELLED, ALREADY_TERMINAL, INVALID };
enum class Cancellation : uint8_t { NONE, REQUEST, GENERATION, RECOVERY };

/** One immutable terminal snapshot, published once and retained until release.
 * SUCCESS is checked acknowledgement/data, never proof of motion completion.
 * executionUnknown is conservative when TX was accepted without a checked reply.
 * raw[0..transport.rxLength) is the captured response, including failed prefixes.
 * endedUs in transport is phase-specific; use closure bounds for deadline evidence. */
struct Completion {
    RequestId id;
    Expectation expected;
    std::size_t producer = 0;
    SequenceId sequence;
    uint64_t admittedUs = 0, deadlineUs = 0;
    uint64_t notBeforeUs = 0, dispatchDeadlineUs = 0;
    std::size_t txLength = 0, replyLength = 0;
    uint32_t responseTimeoutUs = 0, replyGapUs = 0;
    Echo echo = Echo::NONE;
    Outcome outcome = Outcome::TRANSPORT;
    Result transport;
    MotorControlRS::Status validation = MotorControlRS::Ok(); ///< Meaningful only for FRAME.
    bool executionUnknown = false;
    bool urgent = false;
    Cancellation cancellation = Cancellation::NONE; ///< First active cause; completion evidence can win.
    uint8_t raw[MAX_FRAME] = {};
};

/** Caller supplies exclusive, non-overlapping arrays for the owner's lifetime.
 * Treat slot fields as private while owned. Pending capacity counts queued work
 * only; the one active request has already released its pending slot. Result
 * capacity counts queued + active + unread terminal work. Zero capacity is valid
 * and rejects admission explicitly. Each capacity is bounded by 65535. */
struct PendingSlot {
    BusRequest request;
    std::size_t resultSlot = 0;
    uint8_t bytes[MAX_FRAME] = {};
};
struct ResultSlot {
    Completion completion;
    uint64_t generation = 0;
    enum class State : uint8_t { FREE, RESERVED, READY } state = State::FREE;
};
struct BusStorage {
    PendingSlot* pending = nullptr;
    std::size_t pendingCapacity = 0;
    ResultSlot* results = nullptr;
    std::size_t resultCapacity = 0;
    struct ProducerSlot* producers = nullptr;
    std::size_t producerCapacity = 0; ///< 1..65535, including independent producers.
    std::size_t urgentPendingCapacity = 0; ///< Reserved part of total pending capacity.
    std::size_t urgentResultCapacity = 0; ///< Last slots, exclusively for urgent work.
};

/** Caller-owned producer state. One optional sequence per producer; fields are
 * private while owned. head is scratch rebuilt for each dispatch scan. */
struct ProducerSlot {
    uint64_t generation = 1, deadlineUs = 0;
    std::size_t head = static_cast<std::size_t>(-1);
};

enum class RecoveryAdmission : uint8_t { ACCEPTED, RESULTS_FULL, INVALID, IDS_EXHAUSTED };
enum class RecoveryOutcome : uint8_t { RECOVERED, EXPIRED, READ_ERROR, TRANSPORT_ERROR };
/** Control IDs are generations scoped to this live owner, separate from requests.
 * A failed outcome does not imply physical TX/DE has already settled. */
struct RecoveryResult {
    uint64_t id = 0, requestedUs = 0, deadlineUs = 0, finishedUs = 0;
    RequestId interrupted;
    RecoveryOutcome outcome = RecoveryOutcome::EXPIRED;
    Reason reason = Reason::NONE; ///< Read/clock evidence; NONE for a refused transport check.
};

/** Small application-owned scheduler around the real Runner. Construction performs
 * no I/O; slots are initialized. One cooperative context owns every method and
 * the Runner, port and buffers. No concurrent ingress,
 * automatic retries, clocks, allocation or framework dependencies. Do not access
 * the Runner directly while owned. All scans/copies have fixed bounds.
 * service polls at most once, uses at most 64 RX callbacks including recovery,
 * and dispatches at most once. It classifies a FRAME
 * synchronously before publishing or dispatching another request. A validator
 * must not call the owner/runner. Results are non-consuming; release is explicit.
 * Ordinary FIFO is per producer, with cyclic selection among eligible heads.
 * Urgent work uses reserved queue/result quotas and gets the next safe opportunity.
 * Recovery cancels queued work, invalidates sequences, settles physical TX and
 * drains stale traffic. No finite idle guard identifies a bit-identical late reply. */
class BusOwner {
public:
    BusOwner(Runner& runner, const BusStorage& storage) noexcept;
    BusOwner(const BusOwner&) = delete;
    BusOwner& operator=(const BusOwner&) = delete;
    /** Copy validated work and reserve a completion, without port I/O.
     * Rejection leaves output unchanged and creates no terminal result. */
    BusAdmission admit(const BusRequest& request, uint64_t nowUs, RequestId& output) noexcept;
    /** Immediate urgent work only; ordinary pressure cannot consume its quotas. */
    BusAdmission admitUrgent(const BusRequest& request, uint64_t nowUs, RequestId& output) noexcept;
    /** Begin/replace a sequence on an idle producer. Its deadline cannot be renewed. */
    bool beginSequence(std::size_t producer, uint64_t deadlineUs, uint64_t nowUs, SequenceId& output) noexcept;
    /** Cancel a sequence (including a wait with no queued work). Results survive. */
    bool invalidate(const SequenceId& sequence, uint64_t nowUs) noexcept;
    /** Configuration/rebinding boundary: cancel all producer work, invalidate its sequence. */
    bool invalidateProducer(std::size_t producer, uint64_t nowUs) noexcept;
    /** Local cancellation only; physical TX drains. CANCELLED means accepted,
     * possibly pending. Captured on-time completion or earlier expiry can win. */
    Cancel cancel(const RequestId& id, uint64_t nowUs) noexcept;
    /** One bounded service step; supplied monotonic microseconds share port epoch.
     * recoveryReady=false still polls/settles TX and enforces the recovery deadline,
     * but postpones drain/reinitialization until application adapter cleanup is safe. */
    void service(uint64_t nowUs, bool recoveryReady = true) noexcept;
    /** Non-consuming terminal view, valid until release/owner destruction;
     * nullptr for pending, active, released or foreign/stale IDs. No I/O. */
    const Completion* result(const RequestId& id) const noexcept;
    /** Release only a matching retained terminal; false otherwise. No I/O. */
    bool release(const RequestId& id) noexcept;
    /** Reserve one distinct recovery result, cancel old work and invalidate all
     * sequences. service settles TX/drains RX before recovery; no request replay.
     * Rejection leaves output/work unchanged. An unread recovery result blocks
     * another recovery, independently of ordinary/urgent result pressure. */
    RecoveryAdmission recover(uint64_t nowUs, uint64_t deadlineUs, uint64_t& output) noexcept;
    const RecoveryResult* recoveryResult(uint64_t id) const noexcept;
    bool releaseRecovery(uint64_t id) noexcept;
    bool recovering() const noexcept { return recoveryState_ == RecoveryState::PENDING; }
    bool needsRecovery() const noexcept {
        return recovering() || runner_.needsRecovery() ||
            (active_ != NONE && recovery_.id && recovery_.interrupted.owner == this &&
             recovery_.interrupted.slot == active_ &&
             recovery_.interrupted.generation == storage_.results[active_].completion.id.generation);
    }
    std::size_t pending() const noexcept { return count_; }
    bool active() const noexcept { return active_ != NONE; }
    bool valid() const noexcept { return valid_; }

private:
    static constexpr std::size_t NONE = static_cast<std::size_t>(-1);
    bool clock(uint64_t nowUs) noexcept;
    ResultSlot* find(const RequestId& id) const noexcept;
    ResultSlot* reservation(const RequestId& id) const noexcept;
    BusAdmission admit(const BusRequest&, uint64_t, RequestId&, bool urgent) noexcept;
    bool sequenceValid(const BusRequest& request) const noexcept;
    void advanceGeneration(std::size_t producer) noexcept;
    void cancelGroup(std::size_t producer, const SequenceId* sequence, uint64_t nowUs, Cancellation cause) noexcept;
    void cancelActive(uint64_t nowUs, Cancellation cause) noexcept;
    void queuedResult(PendingSlot& pending, Outcome outcome, uint64_t nowUs, Cancellation cause) noexcept;
    void remove(std::size_t index) noexcept;
    PendingSlot& queued(std::size_t index) const noexcept;
    void serviceRecovery(uint64_t nowUs, bool mayDrain) noexcept;
    void finishRecovery(RecoveryOutcome outcome, uint64_t nowUs, Reason reason = Reason::NONE) noexcept;
    void collect() noexcept;
    Runner& runner_;
    BusStorage storage_;
    Validator activeValidator_;
    std::size_t head_ = 0, count_ = 0, active_ = NONE;
    std::size_t nextProducer_ = 0;
    enum class RecoveryState : uint8_t { FREE, PENDING, READY };
    RecoveryState recoveryState_ = RecoveryState::FREE;
    RecoveryResult recovery_;
    uint64_t recoveryGeneration_ = 0, recoveryQuietUs_ = 0;
    bool valid_ = false;
};

}} // namespace MotorControlRSExample::Rtu
