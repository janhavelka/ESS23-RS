// SPDX-License-Identifier: MIT
#pragma once

#include "../common/RtuBusOwner.h"
#include "MotorControlRS/profiles/ess_rs/Codec.h"

namespace MotorControlRSExample { namespace Probe {

constexpr std::size_t LINE_CAPACITY = 96;
constexpr std::size_t OUTPUT_CAPACITY = 1536;
constexpr std::size_t OUTSTANDING_CAPACITY = 9;
constexpr std::size_t PROBE_TX_CAPACITY = 8;
constexpr std::size_t PROBE_RX_CAPACITY = 64;

/** Host action result; admitted probe and recovery complete asynchronously. */
enum class Action : uint8_t { OK, BUSY, RECOVERY_REQUIRED, UNAVAILABLE, FAILED,
    QUEUE_FULL, RESULTS_FULL, IDS_EXHAUSTED, INVALID, ALREADY_TERMINAL };

/** Host-only qualification workload. Changes never configure the motor. */
struct LoadSettings {
    uint32_t workUs = 0;       ///< Competing work per 10 ms period, at most 5000 us.
    uint32_t ownerDelayUs = 0; ///< Active runner service delay, at most 20000 us.
    uint32_t consoleBytes = 0; ///< Competing console bytes per period, at most 256.
};

/** Retained fixture measurements. Section durations are not total CPU use. */
struct LoadSnapshot {
    LoadSettings settings;
    uint64_t elapsedUs = 0, workUs = 0, workIterations = 0;
    uint64_t consoleLines = 0, consoleDropped = 0;
    uint64_t captureUs = 0, captureSamples = 0;
    uint64_t ownerGapMaxUs = 0, captureGapMaxUs = 0;
    uint32_t workStackFreeBytes = 0;
    bool timer = false, ready = false;
    bool cpuValid = false;
    uint8_t cpu0BusyPct = 0, cpu1BusyPct = 0; ///< Optional scheduler-derived estimate.
};

/** Task-context cached host observations. No probe establishes motor readiness. */
struct Snapshot {
    uint8_t address = 1;
    uint32_t baud = 0;
    uint32_t responseTimeoutUs = 0;
    uint32_t replyGapUs = 0, gap15Us = 0, gap35Us = 0;
    uint32_t staleAfterMs = 5000;
    uint64_t uptimeMs = 0;
    bool ready = false;
    bool timingQualified = false;
    bool busy = false;
    bool transmitEnabled = false; ///< Asserted or uncertain DE, including fault cleanup.
    bool recoveryRequired = false;
    Rtu::Phase phase = Rtu::Phase::IDLE;
    Rtu::Reason transport = Rtu::Reason::NONE;
    bool codecChecked = false;
    MotorControlRS::Status codec;
    MotorControlRS::ESS_RS::FrameError frameError = MotorControlRS::ESS_RS::FrameError::NONE;
    bool probeKnown = false;
    bool probeOk = false;
    uint8_t probeAddress = 0; ///< Address of retained probe evidence, not the default target.
    uint16_t rawModel = 0;
    uint64_t ageMs = 0; ///< Conservative age from observedEarliestUs; absent bounds print null.
    uint64_t observedEarliestUs = 0, observedLatestUs = 0, deliveredUs = 0;
    uint64_t recoveryGuardUntilUs = 0;
    uint64_t deadlineUs = 0; ///< Earliest outstanding request/recovery absolute deadline.
    bool timerCapture = false;
    uint32_t readBudget = Rtu::READ_BUDGET;
    uint32_t operationId = 0;
    std::size_t pending = 0, retained = 0, reserved = 0;
    std::size_t pendingCapacity = 0, resultCapacity = 0, outstandingCapacity = OUTSTANDING_CAPACITY;
    std::size_t outputQueued = 0;
    uint64_t outputBlocked = 0, outputShortWrites = 0, inputBytes = 0, inputLines = 0, inputDropped = 0;
    Rtu::Stats stats;
    uint64_t maxPollGapUs = 0;
    uint32_t captureFaults = 0, rxErrors = 0;
    bool memoryValid = false;
    uint32_t internalFree = 0, internalMin = 0, internalLargest = 0;
    uint32_t psramFree = 0, psramMin = 0, psramLargest = 0, stackFreeBytes = 0;
};

struct ProbeResult {
    Rtu::Result transport;
    bool codecChecked = false;
    MotorControlRS::Status codec;
    MotorControlRS::ESS_RS::FrameError frameError = MotorControlRS::ESS_RS::FrameError::NONE;
    uint16_t rawModel = 0; ///< Published only for FRAME plus a successful checked parser.
    const uint8_t* tx = nullptr; ///< Consumed during reportProbe only; nullptr requires zero length.
    std::size_t txLength = 0;
    const uint8_t* rx = nullptr;
    std::size_t rxLength = 0;
    bool timingValid = false;
    uint32_t txUncertaintyUs = 0, maxRxUncertaintyUs = 0;
    uint64_t txEndUs = 0, firstRxStartUs = 0;
    Rtu::Outcome outcome = Rtu::Outcome::TRANSPORT;
    bool executionUnknown = false;
    Rtu::Cancellation cancellation = Rtu::Cancellation::NONE;
    uint64_t observedEarliestUs = 0, observedLatestUs = 0, deliveredUs = 0;
};

/** Synchronous, non-consuming lookup. Raw frame pointers are borrowed only
 * until the result callback's caller finishes formatting this view. */
struct ResultView {
    uint32_t commandId = 0, operationId = 0;
    uint8_t address = 0;
    bool pending = false, recovery = false;
    ProbeResult probe;
    Rtu::RecoveryResult recoveryResult;
};

/** Single task owner, bounded callbacks. emitLine receives one complete JSON line
 * without a newline and must consume/copy it before returning. It must not call
 * back into the console. Return false without copying any bytes for backpressure.
 * The console retains one complete blocked line. With that line pending,
 * only a valid cancel command dispatches; other complete commands and local
 * cancel replies are discarded and counted by inputDropped(). Terminal lines
 * remain retained and are never replaced by a discarded command reply.
 * snapshot only reads cached state and must not touch the
 * motor bus. startProbe admits exactly one built ESS model read; it must not call
 * reportProbe synchronously. Successful probe/recover admission publishes a
 * unique nonzero operationId, distinct from command correlation, and exactly
 * one later terminal callback. recover affects the host only; resetStats clears
 * only local counters. The optional load callback changes/reads host fixture
 * settings only; a null request means query. It must copy the request before
 * returning and publish the applied settings in the snapshot. result, cancel
 * and release are optional; help excludes absent hooks. The other callbacks
 * are required for this console build.
 */
struct Host {
    void* context = nullptr;
    bool (*emitLine)(void*, const char*, std::size_t) = nullptr;
    void (*snapshot)(void*, Snapshot&) = nullptr;
    Action (*startProbe)(void*, uint32_t commandId, uint8_t address, uint32_t& operationId) = nullptr;
    Action (*recover)(void*, uint32_t commandId, uint32_t& operationId) = nullptr;
    void (*resetStats)(void*) = nullptr;
    Action (*load)(void*, const LoadSettings* requested, LoadSnapshot&) = nullptr;
    bool (*result)(void*, uint32_t operationId, ResultView&) = nullptr; ///< Zero selects latest.
    Action (*cancel)(void*, uint32_t operationId) = nullptr; ///< Local only; zero selects latest.
    Action (*release)(void*, uint32_t operationId) = nullptr; ///< Explicit terminal retention release.
};

/** Fixed-capacity read-only ESS console; no allocation, clocks or platform I/O.
 * Feed at most the application's character budget each loop. CR, LF and CRLF
 * end a line. Reject overflow/control bytes as a whole line, never execute a
 * prefix. Optional @1..4294967295 prefix supplies a host correlation id; plain
 * commands use monotonically increasing local ids (wrapping to 1), skipping
 * outstanding correlations. Nine asynchronous commands may be outstanding;
 * duplicate explicit IDs fail before callbacks. Operation result retention
 * belongs to the host and is released only by its explicit release hook.
 * Host callbacks and terminal reports share one task. Place this object in PSRAM if
 * desired for task-context buffers; it is never an ISR object.
 */
class Console {
public:
    explicit Console(const Host& host) noexcept : host_(host) {}
    /** Consume one character. Under output pressure only valid local cancel
     * commands dispatch; other completed commands are counted and discarded. */
    void feed(char value) noexcept;
    bool serviceOutput() noexcept;
    bool outputPending() const noexcept { return outputPending_; }
    uint64_t inputDropped() const noexcept { return inputDropped_; }
    /** True transfers the terminal line to the console/sink; false changes nothing.
     * A blocked sink retains exactly one line. Never retry a transferred result. */
    bool reportProbe(uint32_t id, uint8_t address, uint32_t operationId, const ProbeResult& result) noexcept;
    bool reportRecovery(uint32_t id, uint32_t operationId, const Rtu::RecoveryResult& result) noexcept;

private:
    void dispatch() noexcept;
    void error(uint32_t id, const char* command, const char* reason) noexcept;
    void action(uint32_t id, const char* command, Action result, uint8_t address = 0, uint32_t operationId = 0) noexcept;
    void emit(uint32_t terminalOperation = 0) noexcept;
    bool outstanding(uint32_t id) const noexcept;
    bool track(uint32_t id, uint32_t operationId) noexcept;
    void untrack(uint32_t operationId) noexcept;
    bool formatProbe(uint32_t id, uint32_t commandId, uint8_t address, uint32_t operationId,
                     const ProbeResult&, bool inspection) noexcept;
    bool formatRecovery(uint32_t id, uint32_t commandId, uint32_t operationId,
                        const Rtu::RecoveryResult&, bool inspection) noexcept;

    Host host_;
    char line_[LINE_CAPACITY] = {};
    char output_[OUTPUT_CAPACITY] = {};
    char pendingOutput_[OUTPUT_CAPACITY] = {};
    std::size_t length_ = 0;
    uint32_t nextId_ = 1;
    struct Outstanding { uint32_t commandId = 0, operationId = 0; bool transferred = false; } outstanding_[OUTSTANDING_CAPACITY];
    uint32_t pendingTerminalOperation_ = 0;
    uint64_t inputDropped_ = 0;
    bool outputPending_ = false;
    bool overflow_ = false;
    bool invalid_ = false;
    bool afterCr_ = false;
};

}} // namespace MotorControlRSExample::Probe
