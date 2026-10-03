// SPDX-License-Identifier: MIT
#pragma once

#include "../common/RtuRunner.h"
#include "MotorControlRS/profiles/ess_rs/Codec.h"

namespace MotorControlRSExample { namespace Probe {

constexpr std::size_t LINE_CAPACITY = 96;
constexpr std::size_t OUTPUT_CAPACITY = 1024;
constexpr std::size_t PROBE_TX_CAPACITY = 8;
constexpr std::size_t PROBE_RX_CAPACITY = 64;

/** Host action result; only a probe completes asynchronously. */
enum class Action : uint8_t { OK, BUSY, RECOVERY_REQUIRED, UNAVAILABLE, FAILED };

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
    uint64_t ageMs = 0; ///< Age of the last completed probe; meaningful when known.
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
};

/** Single task owner, bounded callbacks. emitLine receives one complete JSON line
 * without a newline and must consume/copy it before returning. It must not call
 * back into the console. snapshot only reads cached state and must not touch the
 * motor bus. startProbe admits exactly one built ESS model read; it must not call
 * reportProbe synchronously. recover affects the host only; resetStats clears
 * only local counters. All callbacks are required for this console build.
 */
struct Host {
    void* context = nullptr;
    void (*emitLine)(void*, const char*, std::size_t) = nullptr;
    void (*snapshot)(void*, Snapshot&) = nullptr;
    Action (*startProbe)(void*, uint32_t id, uint8_t address) = nullptr;
    Action (*recover)(void*) = nullptr;
    void (*resetStats)(void*) = nullptr;
};

/** Fixed-capacity read-only ESS console; no allocation, clocks or platform I/O.
 * Feed at most the application's character budget each loop. CR, LF and CRLF
 * end a line. Reject overflow/control bytes as a whole line, never execute a
 * prefix. Optional @1..4294967295 prefix supplies a host correlation id; plain
 * commands use monotonically increasing local ids (wrapping to 1).
 * Host callbacks and reportProbe share one task. Place this object in PSRAM if
 * desired for task-context buffers; it is never an ISR object.
 */
class Console {
public:
    explicit Console(const Host& host) noexcept : host_(host) {}
    void feed(char value) noexcept;
    void reportProbe(uint32_t id, uint8_t address, const ProbeResult& result) noexcept;

private:
    void dispatch() noexcept;
    void error(uint32_t id, const char* command, const char* reason) noexcept;
    void action(uint32_t id, const char* command, Action result, uint8_t address = 0) noexcept;
    void emit() noexcept;

    Host host_;
    char line_[LINE_CAPACITY] = {};
    char output_[OUTPUT_CAPACITY] = {};
    std::size_t length_ = 0;
    uint32_t nextId_ = 1;
    bool overflow_ = false;
    bool invalid_ = false;
    bool afterCr_ = false;
};

}} // namespace MotorControlRSExample::Probe
