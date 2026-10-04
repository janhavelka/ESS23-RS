// SPDX-License-Identifier: MIT
#pragma once

#include "../common/RtuBusOwner.h"
#include "StateCache.h"
#include "AxisConsole.h"
#include "MotorControlRS/profiles/ess_rs/Codec.h"
#include "MotorControlRS/profiles/ess_rs/Reads.h"
#include "MotorControlRS/profiles/ess_rs/Actions.h"
#include "MotorControlRS/profiles/ess_rs/Position.h"
#include "MotorControlRS/profiles/ess_rs/Velocity.h"
#include "MotorControlRS/profiles/ess_rs/DriverSettings.h"
#include "MotorControlRS/profiles/ess_rs/Homing.h"

namespace MotorControlRSExample { namespace Probe {

constexpr std::size_t LINE_CAPACITY = 128;
constexpr std::size_t OUTPUT_CAPACITY = 4608;
constexpr std::size_t OUTSTANDING_CAPACITY = 10; // Nine ordinary correlations plus one stop.
constexpr std::size_t PROBE_TX_CAPACITY = 8;
constexpr std::size_t PROBE_RX_CAPACITY = 64;
// Fixed non-consuming FC03 timing fixture; function manual physical p77.
constexpr uint16_t CAPTURE_FIRST = 0x0130, CAPTURE_WORDS = 16;

/** Host action result; admitted reads and recovery complete asynchronously. */
enum class Action : uint8_t { OK, BUSY, RECOVERY_REQUIRED, UNAVAILABLE, FAILED,
    QUEUE_FULL, RESULTS_FULL, IDS_EXHAUSTED, INVALID, ALREADY_TERMINAL,
    TIMING_UNQUALIFIED, UNSUPPORTED, AXIS_CONFLICT };

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
    uint64_t captureUs = 0, captureSamples = 0, timerCallbacks = 0;
    uint64_t ownerGapMaxUs = 0, captureGapMaxUs = 0;
    uint32_t workStackFreeBytes = 0;
    uint32_t sampleGapLimitUs = 0, captureHighWater = 0;
    bool sampleGapExceeded = false;
    bool timer = false, ready = false;
    bool cpuValid = false;
    uint8_t cpu0BusyPct = 0, cpu1BusyPct = 0; ///< Optional scheduler-derived estimate.
};

/** Finite opt-in state observation polling. No consuming fields or retries. */
struct MonitorSettings {
    bool enabled = false;
    uint32_t intervalMs = 0; ///< Enabled range 100..60000 ms.
    uint32_t count = 0; ///< Enabled finite count 1..1000.
};
struct MonitorSnapshot {
    MonitorSettings settings;
    uint32_t remaining = 0, operationId = 0;
    uint64_t nextDueUs = 0, admitted = 0, rejected = 0, cancelled = 0;
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
    bool actionsQualified = false, axisReserved = false;
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
    uint8_t probeAddress = 0; ///< Address of latest transmitted attempt, not the default target.
    bool modelKnown = false; ///< Last checked successful model is retained across failed reads.
    uint8_t modelAddress = 0; ///< Target of rawModel and its observation/delivery timestamps.
    uint32_t modelOperationId = 0; ///< Successful observation's operation ID; zero while unknown.
    uint16_t rawModel = 0;
    uint64_t ageMs = 0; ///< Conservative age from observedEarliestUs; absent bounds print null.
    uint64_t observedEarliestUs = 0, observedLatestUs = 0, deliveredUs = 0;
    uint64_t recoveryGuardUntilUs = 0;
    uint64_t deadlineUs = 0; ///< Earliest outstanding request/recovery absolute deadline.
    bool timerCapture = false;
    bool cacheOffSupported = false, sampleGapExceeded = false;
    uint32_t sampleGapLimitUs = 0;
    uint32_t readBudget = Rtu::READ_BUDGET;
    uint32_t operationId = 0;
    uint32_t cachedIdentityId = 0, cachedConfigId = 0;
    uint8_t cachedIdentityAddress = 0, cachedConfigAddress = 0;
    uint32_t cachedIdentityGeneration = 0, cachedConfigGeneration = 0, bindingGeneration = 0;
    std::size_t pending = 0, retained = 0, reserved = 0;
    std::size_t pendingCapacity = 0, resultCapacity = 0, outstandingCapacity = OUTSTANDING_CAPACITY;
    std::size_t outputQueued = 0;
    uint64_t outputBlocked = 0, outputShortWrites = 0, inputBytes = 0, inputLines = 0, inputDropped = 0;
    const StateCache* stateCache = nullptr; ///< Borrowed for synchronous snapshot formatting only.
    uint64_t nowUs = 0;
    MonitorSnapshot monitorState;
    bool communicationKnown = false;
    MotorControlRS::ReadTarget communicationTarget;
    uint64_t communicationEarliestUs = 0, communicationLatestUs = 0;
    Rtu::Stats stats;
    uint64_t maxPollGapUs = 0;
    uint32_t captureFaults = 0, rxErrors = 0;
    bool memoryValid = false;
    uint32_t internalFree = 0, internalMin = 0, internalLargest = 0;
    uint32_t psramFree = 0, psramMin = 0, psramLargest = 0, stackFreeBytes = 0;
};

struct ProbeResult {
    bool captureRead = false; ///< Fixed timing read, never a model/presence observation.
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

/** Synchronous, non-consuming lookup replaces the complete view on success.
 * Raw frame pointers are borrowed only
 * until the result callback's caller finishes formatting this view. */
struct ResultView {
    uint32_t commandId = 0, operationId = 0;
    uint8_t address = 0;
    bool pending = false, recovery = false, captureRead = false;
    const MotorControlRS::ESS_RS::ReadContext* typedRead = nullptr; ///< Borrowed only during formatting.
    const MotorControlRS::ESS_RS::ActionContext* actionContext = nullptr;
    const MotorControlRS::ESS_RS::MoveContext* moveContext = nullptr; ///< Borrowed only during formatting.
    const MotorControlRS::ESS_RS::VelocityContext* velocityContext = nullptr;
    const MotorControlRS::ESS_RS::DriverContext* driverContext = nullptr;
    const MotorControlRS::ESS_RS::HomeContext* homeContext = nullptr;
    bool interruptedByStop = false;
    ProbeResult probe;
    Rtu::RecoveryResult recoveryResult;
};

/** Single task owner, bounded callbacks. emitLine receives one complete JSON line
 * without a newline and must consume/copy it before returning. It must not call
 * back into the console. Return false without copying any bytes for backpressure.
 * The console retains one complete blocked line. With that line pending,
 * valid cancel, monitor off and one reserved stop can dispatch; other complete commands and local
 * cancel replies are discarded and counted by inputDropped(). Terminal lines
 * remain retained and are never replaced by a discarded command reply.
 * snapshot only reads cached state and must not touch the
 * motor bus. startProbe admits exactly one built ESS model read; it must not call
 * reportProbe synchronously. startTypedRead prepares the public ESS operation
 * and reports its terminal context with reportRead; borrowed result contexts
 * remain valid for the synchronous result formatter only. Successful read/recover admission publishes a
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
    /** Optional fixed CAPTURE_FIRST/CAPTURE_WORDS timing read. Same retained
     * lifetime as startProbe; reports captureRead=true, never refreshes model. */
    Action (*startCaptureRead)(void*, uint32_t commandId, uint8_t address, uint32_t& operationId) = nullptr;
    Action (*startTypedRead)(void*, uint32_t commandId, uint8_t address,
                            MotorControlRS::ESS_RS::ReadKind, uint32_t& operationId) = nullptr;
    Action (*startAction)(void*, uint32_t commandId, uint8_t address,
                         const MotorControlRS::ActionRequest&, uint32_t& operationId) = nullptr;
    /** One finite move admission through the installed preparation API; no request pointer is retained. */
    Action (*startMove)(void*, uint32_t commandId, uint8_t address,
                       const MotorControlRS::MoveRequest&, uint32_t& operationId) = nullptr;
    Action (*startVelocity)(void*, uint32_t commandId, uint8_t address,
                           const MotorControlRS::VelocityRequest&, uint32_t& operationId) = nullptr;
    /** Typed drive settings; request is copied by the host before returning.
     * READ does not change settings; UPDATE validates the complete candidate. */
    Action (*startDriver)(void*, uint32_t commandId, uint8_t address,
                         MotorControlRS::ESS_RS::DriverKind,
                         const MotorControlRS::ESS_RS::DriverRequest&, uint32_t& operationId) = nullptr;
    /** Finite homing through the public profile API; native words are copied. */
    Action (*startHome)(void*, uint32_t commandId, uint8_t address,
                       const MotorControlRS::ESS_RS::HomeRequest&, uint32_t& operationId) = nullptr;
    Action (*recover)(void*, uint32_t commandId, uint32_t& operationId) = nullptr;
    void (*resetStats)(void*) = nullptr;
    Action (*load)(void*, const LoadSettings* requested, LoadSnapshot&) = nullptr;
    /** Local finite polling control; null queries, disabled cancels local continuation. */
    Action (*monitor)(void*, const MonitorSettings* requested, MonitorSnapshot&) = nullptr;
    bool (*result)(void*, uint32_t operationId, ResultView&) = nullptr; ///< Zero selects latest.
    Action (*cancel)(void*, uint32_t operationId) = nullptr; ///< Local only; zero selects latest.
    Action (*release)(void*, uint32_t operationId) = nullptr; ///< Explicit terminal retention release.
    /** Optional synchronous host configuration/target preview. The callback
     * validates application evidence and invokes the public Axis API. It never
     * transmits, retains request pointers or changes drive settings. */
    MotorControlRS::Status (*axis)(void*, const AxisCommand&, AxisView&) = nullptr;
};

/** Fixed-capacity ESS console; no allocation, clocks or platform I/O.
 * Feed at most the application's character budget each loop. CR, LF and CRLF
 * end a line. Reject overflow/control bytes as a whole line, never execute a
 * prefix. Optional @1..4294967295 prefix supplies a host correlation id; plain
 * commands use monotonically increasing local ids (wrapping to 1), skipping
 * outstanding correlations. Nine ordinary commands and one reserved stop may be outstanding;
 * duplicate explicit IDs fail before callbacks. Operation result retention
 * belongs to the host and is released only by its explicit release hook.
 * Host callbacks and terminal reports share one task. Place this object in PSRAM if
 * desired for task-context buffers; it is never an ISR object.
 */
class Console {
public:
    explicit Console(const Host& host) noexcept : host_(host) {}
    /** Consume one character. Under output pressure local cancel, monitor off
     * and one reserved stop can dispatch. Other commands are counted and discarded. */
    void feed(char value) noexcept;
    bool serviceOutput() noexcept;
    bool outputPending() const noexcept { return outputPending_ || stopReply_.pending; }
    uint64_t inputDropped() const noexcept { return inputDropped_; }
    /** True transfers the terminal line to the console/sink; false changes nothing.
     * A blocked sink retains exactly one line. Never retry a transferred result. */
    bool reportProbe(uint32_t id, uint8_t address, uint32_t operationId, const ProbeResult& result) noexcept;
    bool reportRecovery(uint32_t id, uint32_t operationId, const Rtu::RecoveryResult& result) noexcept;
    /** Same transfer contract as reportProbe. Context is borrowed during this
     * call only; operation identity and terminal state are checked before use. */
    bool reportRead(uint32_t id, uint32_t operationId, const MotorControlRS::ESS_RS::ReadContext&) noexcept;
    bool reportAction(uint32_t id, uint32_t operationId, const MotorControlRS::ESS_RS::ActionContext&,
                      bool interruptedByStop = false) noexcept;
    bool reportMove(uint32_t id, uint32_t operationId, const MotorControlRS::ESS_RS::MoveContext&,
                    bool interruptedByStop = false) noexcept;
    bool reportVelocity(uint32_t id, uint32_t operationId, const MotorControlRS::ESS_RS::VelocityContext&,
                        bool interruptedByStop = false) noexcept;
    bool reportDriver(uint32_t id, uint32_t operationId, const MotorControlRS::ESS_RS::DriverContext&) noexcept;
    bool reportHome(uint32_t id, uint32_t operationId, const MotorControlRS::ESS_RS::HomeContext&,
                    bool interruptedByStop = false) noexcept;

private:
    void dispatch() noexcept;
    void error(uint32_t id, const char* command, const char* reason) noexcept;
    void action(uint32_t id, const char* command, Action result, uint8_t address = 0, uint32_t operationId = 0) noexcept;
    void emit(uint32_t terminalOperation = 0) noexcept;
    bool outstanding(uint32_t id) const noexcept;
    bool track(uint32_t id, uint32_t operationId, bool stop = false) noexcept;
    void untrack(uint32_t operationId) noexcept;
    bool formatProbe(uint32_t id, uint32_t commandId, uint8_t address, uint32_t operationId,
                     const ProbeResult&, bool inspection) noexcept;
    bool formatRecovery(uint32_t id, uint32_t commandId, uint32_t operationId,
                        const Rtu::RecoveryResult&, bool inspection) noexcept;
    bool formatRead(uint32_t id, uint32_t commandId, uint32_t operationId,
                    const MotorControlRS::ESS_RS::ReadContext&, bool inspection) noexcept;
    bool formatAction(uint32_t id, uint32_t commandId, uint32_t operationId,
                      const MotorControlRS::ESS_RS::ActionContext&, bool inspection, bool interruptedByStop) noexcept;
    bool formatMove(uint32_t id, uint32_t commandId, uint32_t operationId,
                    const MotorControlRS::ESS_RS::MoveContext&, bool inspection, bool interruptedByStop) noexcept;
    bool formatVelocity(uint32_t id, uint32_t commandId, uint32_t operationId,
                        const MotorControlRS::ESS_RS::VelocityContext&, bool inspection, bool interruptedByStop) noexcept;
    bool formatDriver(uint32_t id, uint32_t commandId, uint32_t operationId,
                      const MotorControlRS::ESS_RS::DriverContext&, bool inspection) noexcept;
    bool formatHome(uint32_t id, uint32_t commandId, uint32_t operationId,
                    const MotorControlRS::ESS_RS::HomeContext&, bool inspection, bool interruptedByStop) noexcept;

    Host host_;
    char line_[LINE_CAPACITY] = {};
    char output_[OUTPUT_CAPACITY] = {};
    char pendingOutput_[OUTPUT_CAPACITY] = {};
    // Checked decoder outputs stay in caller-owned task storage, never on a
    // service stack. Only the selected kind is used during a bounded format.
    MotorControlRS::ESS_RS::IdentityObservation identityView_;
    MotorControlRS::ESS_RS::ConfigObservation configView_;
    MotorControlRS::ESS_RS::StateObservation stateView_;
    MotorControlRS::ESS_RS::DriverObservation driverView_;
    std::size_t length_ = 0;
    uint32_t nextId_ = 1;
    struct Outstanding { uint32_t commandId = 0, operationId = 0; bool transferred = false; } outstanding_[OUTSTANDING_CAPACITY];
    uint32_t pendingTerminalOperation_ = 0;
    // Retain one stop admission under output pressure without a second line buffer.
    struct StopReply {
        bool pending = false;
        uint32_t id = 0, operationId = 0;
        uint8_t address = 0;
        Action result = Action::FAILED;
    } stopReply_;
    uint64_t inputDropped_ = 0;
    bool outputPending_ = false;
    bool overflow_ = false;
    bool invalid_ = false;
    bool afterCr_ = false;
};

}} // namespace MotorControlRSExample::Probe
