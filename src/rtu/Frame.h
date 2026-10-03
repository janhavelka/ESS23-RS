// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <stdint.h>

// Private RTU mechanics only. Device addresses, functions, limits and register
// policies belong to the profile. No buffers, clocks or transport are owned here.
namespace RS485Motion { namespace Rtu {

constexpr std::size_t MAX_FRAME_LEN = 256;

inline uint16_t readWord(const uint8_t* bytes) noexcept {
    return static_cast<uint16_t>((static_cast<uint16_t>(bytes[0]) << 8) | bytes[1]);
}

inline void writeWord(uint8_t* bytes, uint16_t value) noexcept {
    bytes[0] = static_cast<uint8_t>(value >> 8);
    bytes[1] = static_cast<uint8_t>(value);
}

inline uint16_t crc16(const uint8_t* bytes, std::size_t length) noexcept {
    if (length > MAX_FRAME_LEN || (!bytes && length != 0)) return 0;
    uint16_t crc = 0xFFFF;
    for (std::size_t i = 0; i < length; ++i) {
        crc ^= bytes[i];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            crc = static_cast<uint16_t>((crc >> 1) ^ ((crc & 1) ? 0xA001 : 0));
        }
    }
    return crc;
}

// Caller has already checked length is within its protocol's complete-frame bounds.
inline bool validCrc(const uint8_t* bytes, std::size_t length) noexcept {
    const uint16_t expected = static_cast<uint16_t>(bytes[length - 2]) |
        static_cast<uint16_t>(static_cast<uint16_t>(bytes[length - 1]) << 8);
    return crc16(bytes, length - 2) == expected;
}

inline std::size_t finishFrame(uint8_t* bytes, std::size_t payloadLength) noexcept {
    const uint16_t crc = crc16(bytes, payloadLength);
    bytes[payloadLength] = static_cast<uint8_t>(crc);
    bytes[payloadLength + 1] = static_cast<uint8_t>(crc >> 8);
    return payloadLength + 2;
}

inline void writeHeader(uint8_t* bytes, uint8_t address, uint8_t function,
                        uint16_t start, uint16_t value) noexcept {
    bytes[0] = address;
    bytes[1] = function;
    writeWord(bytes + 2, start);
    writeWord(bytes + 4, value);
}

// Compare byte ranges without relational comparison of unrelated pointers or
// end-address arithmetic that could overflow. Sizes are bounded by callers.
inline bool overlaps(const void* left, std::size_t leftSize,
                     const void* right, std::size_t rightSize) noexcept {
    if (leftSize == 0 || rightSize == 0) return false;
    const uintptr_t a = reinterpret_cast<uintptr_t>(left);
    const uintptr_t b = reinterpret_cast<uintptr_t>(right);
    return a <= b ? b - a < leftSize : a - b < rightSize;
}

}} // namespace RS485Motion::Rtu
