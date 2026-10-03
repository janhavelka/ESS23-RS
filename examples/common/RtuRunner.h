// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <stdint.h>

namespace MotorControlRSExample { namespace Rtu {

constexpr std::size_t MAX_FRAME = 256;
constexpr unsigned READ_BUDGET = 64; ///< Maximum receive callbacks per poll.

enum class Phase : uint8_t { IDLE, WAIT_BUS, SETUP, DRAIN, HOLD, RECEIVE, DONE, FAULT };
enum class Reason : uint8_t {
    NONE, FRAME, NO_RESPONSE, PARTIAL_RESPONSE, LENGTH, GAP, EARLY_REPLY,
    RX_OVERFLOW, RX_ERROR, TX_ERROR, TX_TIMEOUT, BUS_TIMEOUT, DIRECTION_ERROR,
    ECHO_ERROR, CANCELLED, CLOCK_ERROR, CAPTURE_TIMEOUT, TIMING_UNCERTAIN,
    REQUEST_DEADLINE
};
enum class Admission : uint8_t { STARTED, BUSY, RECOVERY_REQUIRED, INVALID };
enum class Echo : uint8_t { NONE, REQUIRED }; ///< REQUIRED means one qualified local copy.
enum class ReadState : uint8_t { BYTE, EMPTY, ERROR, PENDING };
enum class TxState : uint8_t { IDLE, BUSY, ERROR }; ///< BUSY also covers pending capture evidence.

/** Conservative wire-time intervals on the same microsecond clock as poll().
 * Start is in [startUs, startUs + uncertaintyUs]; stop-bit end is in
 * [endUs - uncertaintyUs, endUs]. Zero uncertainty means exact timestamps.
 * Bounds include capture delay, receiver sampling and baud uncertainty.
 */
struct RxByte {
    uint64_t startUs = 0;
    uint64_t endUs = 0;
    uint8_t value = 0;
    uint32_t uncertaintyUs = 0;
};

struct WriteResult {
    std::size_t accepted;
    bool error;
    WriteResult(std::size_t count = 0, bool failed = false) : accepted(count), error(failed) {}
};

/** One atomic observation of physical TX and an optional adapter-owned DE release.
 * TX end is in [endedUs - uncertaintyUs, endedUs]. If released is true, receive
 * mode was established in [releasedUs - releaseUncertaintyUs, releasedUs].
 * Release evidence belongs to the current transmission, never an earlier one.
 * An autonomous adapter must use the same hold policy as the runner and retain
 * these bounds until the next enqueue. Otherwise leave released false and let
 * the runner control DE in task context.
 */
struct TxObservation {
    uint64_t endedUs = 0;
    uint32_t uncertaintyUs = 0;
    bool released = false;
    uint64_t releasedUs = 0;
    uint32_t releaseUncertaintyUs = 0;
};

/** Bounded, nonblocking adapter callbacks; caller configured an idle UART in receive mode.
 * write() is called once per request. It must enqueue a contiguous whole RTU frame;
 * a short enqueue is a failed transaction, never resumed or retried here.
 * txState(IDLE) supplies one atomic TxObservation. IDLE means physical TX has
 * ended, independently of when the task reads that evidence. Adapter-owned DE
 * release permits delayed task service without holding the bus over a reply.
 * Such adapters return BUSY until completion and release can be reported together;
 * the task must not race their autonomous direction action.
 * read() reports ordered wire events at/before nowUs. EMPTY supplies a monotonic
 * observedThroughUs watermark: all wire activity through that time was reported.
 * An in-progress character or delayed capture must hold the watermark back.
 * PENDING means capture is incomplete; no empty/silence proof is supplied.
 * Poll time / Serial.available() alone cannot satisfy this timing contract.
 * Errors must not hide accepted TX bytes or queued activity. Calls share one owner.
 */
struct Port {
    void* context = nullptr;
    bool (*setTransmit)(void*, bool) = nullptr;
    WriteResult (*write)(void*, const uint8_t*, std::size_t) = nullptr;
    TxState (*txState)(void*, uint64_t nowUs, TxObservation&) = nullptr;
    ReadState (*read)(void*, uint64_t nowUs, RxByte&, uint64_t& observedThroughUs) = nullptr;
};

struct Timing {
    uint32_t gap15Us = 0;
    uint32_t gap35Us = 0;
    uint32_t setupUs = 0;       ///< Transceiver DE setup; adapter/board requirement.
    uint32_t holdUs = 0;        ///< DE hold after physical TX end.
    uint32_t busTimeoutUs = 0;  ///< Application admission deadline, not a drive fact.
    uint32_t txTimeoutUs = 0;   ///< Transaction deadline from DE assertion; stalled TX
                              ///< may keep DE asserted until physical idle is established.
                              ///< Retained on-time TX/release evidence survives delayed polling.
    uint32_t captureTimeoutUs = 0; ///< Maximum adapter observation lag, not a drive timeout.
};

/** Set only the two gap fields; ceil-rounded us, 10/11-bit formats, no I/O.
 * <=19200 uses character timing; above it uses 750/1750 us. 8N1 (10 bits) is
 * vendor framing, not the standard's 11-bit format. Unchanged on invalid input.
 */
bool setRtuTiming(uint32_t baud, uint8_t bitsPerCharacter, Timing& output) noexcept;

struct Request {
    const uint8_t* bytes = nullptr; ///< Already validated/built request; copied at start.
    std::size_t length = 0;
    std::size_t replyLength = 0;    ///< Normal reply, including CRC; exception is five bytes.
    uint32_t responseTimeoutUs = 0; ///< From physical TX end, including final framing gap.
    uint32_t replyGapUs = 0;        ///< Explicit device turnaround; zero uses gap35Us.
    Echo echo = Echo::NONE;
    uint64_t deadlineUs = 0;        ///< Absolute closure deadline; zero disables it.
                                  ///< New DE assertion/enqueue requires now < deadline.
};

/** FRAME means a complete envelope was collected, not a valid CRC or motor action.
 * Pass the retained RX bytes to the existing checked profile parser. No retry occurs.
 * Any accepted TX byte may have reached the drive; even txComplete is not an ack.
 */
struct Result {
    Reason reason = Reason::NONE;
    uint64_t startedUs = 0;
    uint64_t endedUs = 0;
    uint16_t txAccepted = 0;
    uint16_t rxLength = 0;
    uint16_t echoBytes = 0;
    bool txComplete = false;
    bool rxTruncated = false;
    uint64_t closureEarliestUs = 0; ///< Last stop lower bound plus final t3.5.
    uint64_t closureLatestUs = 0;   ///< Last stop upper bound plus final t3.5.
    bool closureQualified = false; ///< Watermark/next frame proves the entire idle gap.
    uint64_t txEndUs = 0, firstRxStartUs = 0; ///< Retained wire evidence; zero when absent.
    uint32_t txUncertaintyUs = 0, maxRxUncertaintyUs = 0;
};

enum class Event : uint8_t { START, PHASE, TX, TX_DONE, DIRECTION, RX, ECHO, DISCARD, END, RECOVER, CLOCK_ERROR };
struct Trace {
    uint64_t atUs = 0;
    uint64_t wireStartUs = 0; ///< RX earliest start; atUs is latest stop-bit end.
    Event event = Event::START;
    Phase phase = Phase::IDLE;
    Reason reason = Reason::NONE;
    uint8_t byte = 0;
    uint16_t count = 0;
    uint32_t uncertaintyUs = 0; ///< RX or TX_DONE interval width; zero otherwise.
};

struct Stats {
    // Saturating counters; clearStats() leaves transaction evidence intact.
    uint32_t started = 0, frames = 0, failed = 0, timeouts = 0, cancelled = 0;
    uint32_t rxBytes = 0, discarded = 0, echoBytes = 0, traceOverwritten = 0;
};

struct DrainResult {
    ReadState state = ReadState::PENDING;
    Reason reason = Reason::NONE;
    uint64_t throughUs = 0, lastByteUs = 0;
};

/** Caller-owned storage, alive and exclusive for the runner lifetime. TX/RX must
 * not overlap each other or trace storage. 1..256 bytes each; actual request/reply
 * bounds are checked at start. A larger RX buffer can retain overlong-frame evidence.
 * Traces are optional (nullptr,0), fixed capacity <=65535; overwritten oldest-first.
 * Task-context storage can later use platform-allocated PSRAM. No allocation here.
 */
struct Storage {
    uint8_t* tx = nullptr;
    std::size_t txCapacity = 0;
    uint8_t* rx = nullptr;
    std::size_t rxCapacity = 0;
    Trace* trace = nullptr;
    std::size_t traceCapacity = 0;
};

/** Application helper, not part of the installed reusable library.
 * Single owner, no threads, heap, clocks, logging or platform headers. Constructors
 * perform no I/O. The supplied uint64 clock must be monotonic; callbacks are bounded.
 * Results/raw bytes remain until the next accepted start; inspect/copy before then.
 * Check FRAME with the profile parser before reuse. If parsing rejects it, settle
 * the possible real/late reply before explicit recover(); a collected foreign or
 * corrupt frame does not prove the expected transaction ended.
 * Faults after TX require explicit recovery. recover() requires physical idle and
 * successful DE release; its caller must first settle possible late responses using
 * a qualified bound or explicit host recovery policy. Silence alone cannot prove it.
 * poll() in FAULT can release DE when TX finally ends, but never clears the interlock.
 */
class Runner {
public:
    Runner(const Port& port, const Storage& storage, const Timing& timing) noexcept;
    Runner(const Runner&) = delete;
    Runner& operator=(const Runner&) = delete;

    Admission start(const Request& request, uint64_t nowUs) noexcept;
    bool accepts(const Request& request) const noexcept; ///< Shape/storage check, no I/O.
    bool checkClock(uint64_t nowUs) noexcept; ///< Observe owner time without I/O; regression interlocks.
    bool storageOverlaps(const void* data, std::size_t bytes) const noexcept; ///< Storage check, no I/O.
    const uint8_t* received() const noexcept { return storage_.rx; } ///< Borrowed until next accepted start.
    /** Checked parser rejected FRAME. Interlock before any subsequent start;
     * caller must settle possible late/foreign traffic before explicit recover(). */
    bool rejectFrame() noexcept;
    bool requireRecovery() noexcept; ///< Idle-only interlock; preserve terminal evidence, no I/O.
    DrainResult discard(uint64_t nowUs) noexcept; ///< Fault/DE-released only, <=64 reads; preserves Result.
    uint32_t idleGapUs() const noexcept { return timing_.gap35Us; }
    void poll(uint64_t nowUs) noexcept;
    void cancel(uint64_t nowUs) noexcept; ///< Local cancellation, never a motor stop.
    /** Cancel RECEIVE after examining retained evidence through this immutable
     * cutoff. A qualified closure at/before the cutoff wins, even across read
     * budgets; later replies cannot succeed. Other phases use cancel(). No reads. */
    void cancelCaptured(uint64_t nowUs) noexcept;
    bool recover(uint64_t nowUs) noexcept;
    bool busy() const noexcept;
    bool needsRecovery() const noexcept { return phase_ == Phase::FAULT; }
    bool transmitEnabled() const noexcept { return de_; } ///< True also if DE is uncertain.
    Phase phase() const noexcept { return phase_; }
    const Result& result() const noexcept { return result_; }
    const Stats& stats() const noexcept { return stats_; }
    void clearStats() noexcept { stats_ = Stats(); } ///< Does not clear outcome/interlock.
    std::size_t traceSize() const noexcept { return traceSize_; }
    const Trace* traceAt(std::size_t index) const noexcept; ///< Oldest first; no I/O.

private:
    bool valid() const noexcept;
    bool clock(uint64_t nowUs) noexcept;
    bool direction(bool enabled, uint64_t nowUs) noexcept;
    void phase(Phase next, uint64_t nowUs) noexcept;
    void record(Event event, uint64_t atUs, uint8_t byte = 0, uint16_t count = 0,
                uint64_t wireStartUs = 0, uint32_t uncertaintyUs = 0) noexcept;
    void finish(Reason reason, uint64_t nowUs, bool fault) noexcept;
    bool receive(uint64_t nowUs) noexcept; // true only when adapter reaches EMPTY.
    bool onByte(const RxByte& byte, uint64_t nowUs) noexcept;
    void frame(uint64_t nowUs) noexcept;
    void completedFrame(uint64_t latestUs, uint32_t uncertaintyUs) noexcept;
    void closure(uint64_t latestUs, uint32_t uncertaintyUs, bool qualified) noexcept;
    bool requestDeadlineFirst() const noexcept;
    bool cancellationFirst() const noexcept;
    Reason closureDeadline(uint64_t latestUs, uint32_t uncertaintyUs) const noexcept;
    bool expired(uint64_t nowUs) const noexcept;
    void releaseFault(uint64_t nowUs) noexcept;

    Port port_;
    Storage storage_;
    Timing timing_;
    Result result_;
    Stats stats_;
    Phase phase_ = Phase::IDLE;
    Echo echo_ = Echo::NONE;
    Reason pending_ = Reason::NONE;
    std::size_t txLength_ = 0, replyLength_ = 0;
    std::size_t traceHead_ = 0, traceSize_ = 0;
    uint32_t responseTimeoutUs_ = 0, replyGapUs_ = 0;
    uint32_t txUncertaintyUs_ = 0, rxUncertaintyUs_ = 0, releaseUncertaintyUs_ = 0;
    uint64_t now_ = 0, quietSince_ = 0, assertedUs_ = 0, queuedUs_ = 0;
    uint64_t txEndUs_ = 0, releasedUs_ = 0, lastRxEndUs_ = 0, observedUs_ = 0;
    uint64_t deadlineUs_ = 0, cancellationUs_ = 0;
    bool clockSet_ = false, observed_ = false, haveRx_ = false, de_ = false;
};

const char* phaseName(Phase phase) noexcept;
const char* reasonName(Reason reason) noexcept;

}} // namespace MotorControlRSExample::Rtu
