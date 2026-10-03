// SPDX-License-Identifier: MIT
#pragma once
#include "Hardware.h"
#include <string>

struct FakeSerial {
    std::string input, output;
    void begin(unsigned) {}
    int available() const { return static_cast<int>(input.size()); }
    int read() { const char c = input.front(); input.erase(0, 1); return c; }
    std::size_t write(const uint8_t* data, std::size_t size) {
        output.append(reinterpret_cast<const char*>(data), size); return size;
    }
    std::size_t write(char c) { output += c; return 1; }
    void println(const char* text) { output += text; output += '\n'; }
};
extern FakeSerial Serial;
inline void delay(unsigned ms) { hardware.time += static_cast<uint64_t>(ms) * 1000; }
inline unsigned uxTaskGetStackHighWaterMark(void*) { return 4096; }
