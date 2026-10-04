/** @file Traffic.h
 * @brief Non-consuming, caller-owned copies of serial traffic. No I/O or allocation.
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <cstddef>
#include <stdint.h>

namespace MotorControlRS {
constexpr std::size_t TRAFFIC_MAX_BYTES = 256;
enum class TrafficKind : uint8_t { TX, RX, TX_END, DIRECTION, END };

/** Diagnostic copy, never transaction/admission evidence. TX is accepted enqueue
 * data, not proof that bytes reached the wire. TX_END separately reports physical
 * completion. RX bounds retain the adapter's uncertainty; software observation is
 * not an independent electrical measurement. code is an application-defined reason
 * for END or 0/1 for DIRECTION. complete means a full enqueue / qualified RX envelope.
 */
struct TrafficRecord {
    uint64_t sequence = 0, transaction = 0, atUs = 0, startUs = 0, endUs = 0;
    uint32_t uncertaintyUs = 0;
    uint16_t length = 0, code = 0;
    TrafficKind kind = TrafficKind::RX;
    bool complete = false;
    uint8_t bytes[TRAFFIC_MAX_BYTES] = {};
};

/** Single-owner optional recorder. Storage must remain exclusive and separate from
 * live transport buffers. Overwrites oldest records rather than delaying traffic;
 * overwritten() is saturating. Reading copies never consumes bytes or records.
 * No callback, UART access, clock, heap, locks or formatting occurs here.
 * Disabled by default. Disabling publishes any diagnostic partial RX as incomplete.
 */
class TrafficCapture {
public:
    TrafficCapture(TrafficRecord* records, std::size_t capacity) noexcept;
    TrafficCapture(const TrafficCapture&) = delete;
    TrafficCapture& operator=(const TrafficCapture&) = delete;
    void setEnabled(bool enabled) noexcept;
    bool enabled() const noexcept { return enabled_; }
    std::size_t size() const noexcept { return size_; }
    uint32_t overwritten() const noexcept { return overwritten_; }
    uint32_t dropped() const noexcept { return dropped_; } ///< Invalid/oversize supplied records.
    /** Oldest retained sequence newer than cursor; copies and advances only cursor.
     * Independent readers may use independent cursors. False leaves both unchanged.
     * Output/cursor overlap with capture storage or each other is rejected.
     */
    bool copyAfter(uint64_t& cursor, TrafficRecord& output) const noexcept;
    void clear() noexcept; ///< Clears diagnostics only; sequence remains monotonic.

    void begin(uint64_t atUs) noexcept;
    /** Copies 0..256 accepted bytes; invalid/oversize inputs increment dropped(). */
    void transmitted(const uint8_t* bytes, std::size_t accepted, bool full,
                     uint64_t atUs) noexcept;
    void received(uint8_t byte, uint64_t startUs, uint64_t endUs,
                  uint32_t uncertaintyUs, uint64_t atUs) noexcept;
    void flushReceived(bool complete, uint64_t atUs, std::size_t expectedLength = 0) noexcept;
    void event(TrafficKind kind, uint64_t atUs, uint16_t code = 0,
               uint32_t uncertaintyUs = 0) noexcept;
private:
    void append(TrafficRecord& record) noexcept;
    TrafficRecord* records_;
    std::size_t capacity_, head_ = 0, size_ = 0;
    uint64_t sequence_ = 0, transaction_ = 0;
    uint32_t overwritten_ = 0, dropped_ = 0;
    bool enabled_ = false, splitRx_ = false;
    TrafficRecord pending_;
};
const char* trafficKindName(TrafficKind kind) noexcept;
} // namespace MotorControlRS
