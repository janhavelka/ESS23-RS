// SPDX-License-Identifier: MIT
#pragma once
#include "Hardware.h"
#include <algorithm>
#include <limits>
#include <string>

struct FakeSerial {
    std::string input, output;
    std::size_t writeCapacity = 4096;
    std::size_t writeLimit = std::numeric_limits<std::size_t>::max();
    std::size_t writeCalls = 0, writeBytes = 0, maxWriteSize = 0;
    uint32_t writeTimeUs = 0, txTimeoutMs = 1000;
    bool consumeCapacity = false;
    void begin(unsigned) {}
    std::size_t setTxBufferSize(std::size_t size) { writeCapacity = size; return size; }
    void setTxTimeoutMs(uint32_t timeout) { txTimeoutMs = timeout; }
    int availableForWrite() const { return static_cast<int>(writeCapacity); }
    int available() const { return static_cast<int>(input.size()); }
    int read() { const char c = input.front(); input.erase(0, 1); return c; }
    std::size_t write(const uint8_t* data, std::size_t size) {
        ++writeCalls;
        maxWriteSize = std::max(maxWriteSize, size);
        const std::size_t accepted = std::min(size, std::min(writeCapacity, writeLimit));
        output.append(reinterpret_cast<const char*>(data), accepted);
        writeBytes += accepted;
        if (consumeCapacity) writeCapacity -= accepted;
        advanceHardware(hardware.time + writeTimeUs);
        return accepted;
    }
    std::size_t write(char c) { return write(reinterpret_cast<const uint8_t*>(&c), 1); }
    void println(const char* text) { write(reinterpret_cast<const uint8_t*>(text), std::char_traits<char>::length(text)); write('\n'); }
};
extern FakeSerial Serial;
inline void delay(unsigned ms) { advanceHardware(hardware.time + static_cast<uint64_t>(ms) * 1000); }
#ifndef MOTORCONTROLRS_FAKE_STACK_WATERMARK
#define MOTORCONTROLRS_FAKE_STACK_WATERMARK
inline unsigned uxTaskGetStackHighWaterMark(void*) { return 4096; }
#endif
