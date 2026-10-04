// SPDX-License-Identifier: MIT
#pragma once
#include "RtuRunner.h"

namespace MotorControlRSExample {
/** Reviewed ESS host formats; application support, outside the installed core. */
enum class HostFormat : uint8_t { N8_1, N8_2, E8_1, O8_1 };
struct HostTuple { uint32_t baud = 115200; HostFormat format = HostFormat::N8_1; };
inline bool sameTuple(const HostTuple& a, const HostTuple& b) noexcept {
    return a.baud == b.baud && a.format == b.format;
}
inline uint8_t bitsPerCharacter(HostFormat format) noexcept {
    switch (format) {
    case HostFormat::N8_1: return 10;
    case HostFormat::N8_2: case HostFormat::E8_1: case HostFormat::O8_1: return 11;
    }
    return 0;
}
inline const char* formatName(HostFormat format) noexcept {
    switch (format) {
    case HostFormat::N8_1: return "8N1";
    case HostFormat::N8_2: return "8N2";
    case HostFormat::E8_1: return "8E1";
    case HostFormat::O8_1: return "8O1";
    }
    return "unsupported";
}
/** Finite SDK/ESS intersection; support does not imply electrical qualification. */
inline bool reviewedHostTuple(const HostTuple& tuple) noexcept {
    return bitsPerCharacter(tuple.format) && (tuple.baud == 9600 || tuple.baud == 19200 ||
        tuple.baud == 38400 || tuple.baud == 115200);
}
struct HostTiming {
    Rtu::Timing runner;
    uint32_t characterMinUs = 0, characterMaxUs = 0, stopGuardUs = 0;
    uint32_t capturePeriodUs = 20, replyGapUs = 0;
    uint32_t responseTimeoutUs = 200000, requestTimeoutUs = 500000, recoveryGuardUs = 500000;
};
/** Host policy using actual 10/11-bit length and a provisional 2% baud tolerance.
 * Output stays unchanged on rejection. Only the recorded default bench tuple
 * uses its 304-us first-reply exception; all other tuples use ordinary t3.5.
 * TX budget covers the application's 32-byte buffer plus capture/setup margin.
 */
inline bool hostTiming(const HostTuple& tuple, HostTiming& output) noexcept {
    if (!reviewedHostTuple(tuple)) return false;
    HostTiming value;
    const uint32_t bits = bitsPerCharacter(tuple.format), baud = tuple.baud;
    if (!Rtu::setRtuTiming(baud, static_cast<uint8_t>(bits), value.runner)) return false;
    // Baud tolerance applies to frequency: slowest characters use baud * .98,
    // rather than the slightly shorter approximation duration * 1.02.
    value.characterMinUs = static_cast<uint32_t>(uint64_t(bits) * 100000000U / (uint64_t(baud) * 102));
    value.characterMaxUs = static_cast<uint32_t>((uint64_t(bits) * 100000000U + uint64_t(baud) * 98 - 1) /
        (uint64_t(baud) * 98));
    // FIFO visibility is not documented as final-stop completion. Cover both
    // stop bits for 8N2; retain the provisional one-stop sampling assumption
    // for the other reviewed formats, pending independent electrical capture.
    const uint32_t stops = tuple.format == HostFormat::N8_2 ? 2 : 1;
    value.stopGuardUs = static_cast<uint32_t>((uint64_t(stops) * 100000000U + uint64_t(baud) * 98 - 1) /
        (uint64_t(baud) * 98)) + 2;
    value.runner.setupUs = value.runner.holdUs = 20;
    value.runner.busTimeoutUs = 100000;
    value.runner.captureTimeoutUs = 10000;
    const uint32_t txBudget = 32 * value.characterMaxUs + 10000 + 40;
    value.runner.txTimeoutUs = txBudget > 20000 ? txBudget : 20000;
    value.replyGapUs = baud == 115200 && tuple.format == HostFormat::N8_1 ? 304 : value.runner.gap35Us;
    output = value;
    return true;
}
} // namespace MotorControlRSExample
