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
};

enum class BusAdmission : uint8_t { ACCEPTED, INVALID, EXPIRED, QUEUE_FULL, RESULTS_FULL, IDS_EXHAUSTED };
enum class Outcome : uint8_t { QUEUE_EXPIRED, TRANSPORT, SUCCESS, DEVICE_REJECTED, INVALID_REPLY };

/** One immutable terminal snapshot, published once and retained until release.
 * SUCCESS is checked acknowledgement/data, never proof of motion completion.
 * executionUnknown is conservative when TX was accepted without a checked reply.
 * raw[0..transport.rxLength) is the captured response, including failed prefixes.
 * endedUs in transport is phase-specific; use closure bounds for deadline evidence. */
struct Completion {
    RequestId id;
    Expectation expected;
    uint64_t admittedUs = 0, deadlineUs = 0;
    std::size_t txLength = 0, replyLength = 0;
    uint32_t responseTimeoutUs = 0, replyGapUs = 0;
    Echo echo = Echo::NONE;
    Outcome outcome = Outcome::TRANSPORT;
    Result transport;
    MotorControlRS::Status validation = MotorControlRS::Ok(); ///< Meaningful only for FRAME.
    bool executionUnknown = false;
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
};

/** Small application-owned FIFO around the real Runner. Construction performs
 * no I/O; slots are initialized. One cooperative context owns admit/service/
 * result/release/recover and the Runner, port and buffers. No concurrent ingress,
 * automatic retries, clocks, allocation or framework dependencies. Do not access
 * the Runner directly while owned. All scans/copies have fixed bounds.
 * service polls at most once and dispatches at most once. It classifies a FRAME
 * synchronously before publishing or dispatching another request. A validator
 * must not call the owner/runner. Results are non-consuming; release is explicit.
 * Recovery requires application settlement of possible late responses and an
 * empty pending queue (service can expire it). It preserves unread results and
 * never resumes queued writes. Cancellation/disposition is added in prompt 02. */
class BusOwner {
public:
    BusOwner(Runner& runner, const BusStorage& storage) noexcept;
    BusOwner(const BusOwner&) = delete;
    BusOwner& operator=(const BusOwner&) = delete;
    /** Copy validated work and reserve a completion, without port I/O.
     * Rejection leaves output unchanged and creates no terminal result. */
    BusAdmission admit(const BusRequest& request, uint64_t nowUs, RequestId& output) noexcept;
    /** One bounded service step; supplied monotonic microseconds share port epoch. */
    void service(uint64_t nowUs) noexcept;
    /** Non-consuming terminal view, valid until release/owner destruction;
     * nullptr for pending, active, released or foreign/stale IDs. No I/O. */
    const Completion* result(const RequestId& id) const noexcept;
    /** Release only a matching retained terminal; false otherwise. No I/O. */
    bool release(const RequestId& id) noexcept;
    /** Empty-queue/idle-only host recovery after caller settles late traffic.
     * Physical TX/direction checks may refuse it; no request is replayed. */
    bool recover(uint64_t nowUs) noexcept;
    bool needsRecovery() const noexcept { return runner_.needsRecovery(); }
    std::size_t pending() const noexcept { return count_; }
    bool active() const noexcept { return active_ != NONE; }
    bool valid() const noexcept { return valid_; }

private:
    static constexpr std::size_t NONE = static_cast<std::size_t>(-1);
    bool clock(uint64_t nowUs) noexcept;
    ResultSlot* find(const RequestId& id) const noexcept;
    void collect() noexcept;
    Runner& runner_;
    BusStorage storage_;
    Validator activeValidator_;
    std::size_t head_ = 0, count_ = 0, active_ = NONE;
    bool valid_ = false;
};

}} // namespace MotorControlRSExample::Rtu
