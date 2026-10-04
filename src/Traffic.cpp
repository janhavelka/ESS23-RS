// SPDX-License-Identifier: MIT
#include "MotorControlRS/Traffic.h"
#include <cstring>
#include <limits>

namespace MotorControlRS {
namespace {
bool overlaps(const void* a, std::size_t as, const void* b, std::size_t bs) noexcept {
    if (!as || !bs) return false;
    const uintptr_t x = reinterpret_cast<uintptr_t>(a), y = reinterpret_cast<uintptr_t>(b);
    return x <= y ? y - x < as : x - y < bs;
}
}
TrafficCapture::TrafficCapture(TrafficRecord* records, std::size_t capacity) noexcept
    : records_(records), capacity_(records && capacity <= 65535 ? capacity : 0) {}

void TrafficCapture::setEnabled(bool enabled) noexcept {
    if (enabled_ == enabled) return;
    if (enabled_) flushReceived(false, pending_.atUs);
    pending_ = TrafficRecord();
    splitRx_ = false;
    enabled_ = enabled && capacity_ != 0;
}
void TrafficCapture::clear() noexcept {
    head_ = size_ = 0;
    overwritten_ = dropped_ = 0;
    pending_ = TrafficRecord();
    splitRx_ = false;
}
void TrafficCapture::append(TrafficRecord& record) noexcept {
    if (!enabled_) return;
    // Exhaustion cannot wrap correlation IDs and silently join different traffic.
    if (sequence_ == std::numeric_limits<uint64_t>::max()) { enabled_ = false; return; }
    record.sequence = ++sequence_;
    record.transaction = transaction_;
    records_[head_] = record;
    head_ = (head_ + 1) % capacity_;
    if (size_ < capacity_) ++size_;
    else if (overwritten_ != std::numeric_limits<uint32_t>::max()) ++overwritten_;
}
bool TrafficCapture::copyAfter(uint64_t& cursor, TrafficRecord& output) const noexcept {
    if (overlaps(&output, sizeof(output), records_, capacity_ * sizeof(TrafficRecord)) ||
        overlaps(&cursor, sizeof(cursor), records_, capacity_ * sizeof(TrafficRecord)) ||
        overlaps(&cursor, sizeof(cursor), &output, sizeof(output))) return false;
    const std::size_t first = capacity_ ? (head_ + capacity_ - size_) % capacity_ : 0;
    for (std::size_t i = 0; i < size_; ++i) {
        const TrafficRecord& record = records_[(first + i) % capacity_];
        if (record.sequence > cursor) { output = record; cursor = record.sequence; return true; }
    }
    return false;
}
void TrafficCapture::begin(uint64_t atUs) noexcept {
    flushReceived(false, atUs);
    if (transaction_ == std::numeric_limits<uint64_t>::max()) { enabled_ = false; return; }
    ++transaction_;
    splitRx_ = false;
}
void TrafficCapture::transmitted(const uint8_t* bytes, std::size_t accepted, bool full,
                                uint64_t atUs) noexcept {
    if (!enabled_) return;
    flushReceived(false, atUs);
    if ((!bytes && accepted) || accepted > TRAFFIC_MAX_BYTES) {
        if (dropped_ != std::numeric_limits<uint32_t>::max()) ++dropped_;
        return;
    }
    TrafficRecord record;
    record.kind = TrafficKind::TX;
    record.atUs = atUs;
    record.length = static_cast<uint16_t>(accepted);
    record.complete = full;
    if (accepted) std::memcpy(record.bytes, bytes, accepted);
    append(record);
}
void TrafficCapture::received(uint8_t byte, uint64_t startUs, uint64_t endUs,
                             uint32_t uncertaintyUs, uint64_t atUs) noexcept {
    if (!enabled_) return;
    if (pending_.length == TRAFFIC_MAX_BYTES) {
        flushReceived(false, atUs);
        splitRx_ = true;
    }
    if (!pending_.length) pending_.startUs = startUs;
    pending_.endUs = endUs;
    pending_.atUs = atUs;
    if (uncertaintyUs > pending_.uncertaintyUs) pending_.uncertaintyUs = uncertaintyUs;
    pending_.bytes[pending_.length++] = byte;
}
void TrafficCapture::flushReceived(bool complete, uint64_t atUs, std::size_t expectedLength) noexcept {
    if (!enabled_ || !pending_.length) return;
    pending_.kind = TrafficKind::RX;
    pending_.atUs = atUs;
    pending_.complete = complete && !splitRx_ && (!expectedLength || expectedLength == pending_.length);
    append(pending_);
    pending_ = TrafficRecord();
}
void TrafficCapture::event(TrafficKind kind, uint64_t atUs, uint16_t code,
                          uint32_t uncertaintyUs) noexcept {
    if (!enabled_) return;
    TrafficRecord record;
    record.kind = kind;
    record.atUs = atUs;
    record.endUs = kind == TrafficKind::TX_END ? atUs : 0;
    record.code = code;
    record.uncertaintyUs = uncertaintyUs;
    append(record);
}
const char* trafficKindName(TrafficKind kind) noexcept {
    switch (kind) {
    case TrafficKind::TX: return "TX";
    case TrafficKind::RX: return "RX";
    case TrafficKind::TX_END: return "TX_END";
    case TrafficKind::DIRECTION: return "DIRECTION";
    case TrafficKind::END: return "END";
    }
    return "UNKNOWN";
}
} // namespace MotorControlRS
