// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <esp_attr.h>

// Diagnostic progress only, never persisted motor coordinates or replay state.
// RTC RAM survives a controller reset; loss of board power may erase it.
namespace MotorControlRSExample { namespace RuntimeWitness {
enum Stage : uint32_t { STARTUP = 1, OWNER, OPERATIONS, CONSOLE, OUTPUT_SPACE,
    OUTPUT_WRITE, IDLE, SNAPSHOT, CAPTURE_STATS, HEAP_STATS, STACK_STATS };
struct Record {
    uint32_t magic, stage, uptimeMs, loops, inputLines, outputQueued, outputBlocked;
};
constexpr uint32_t MAGIC = 0x4D435231;
static RTC_NOINIT_ATTR volatile Record retained;
static Record previous = {};
inline void begin() {
    if (retained.magic == MAGIC && retained.stage >= STARTUP && retained.stage <= STACK_STATS)
        previous = {MAGIC, retained.stage, retained.uptimeMs, retained.loops,
                    retained.inputLines, retained.outputQueued, retained.outputBlocked};
    else previous = {};
    retained.magic = 0;
    retained.stage = STARTUP; retained.uptimeMs = retained.loops = 0;
    retained.inputLines = retained.outputQueued = retained.outputBlocked = 0;
    retained.magic = MAGIC;
}
inline void mark(Stage stage) { retained.stage = stage; }
inline void service(uint32_t ms, uint32_t lines, uint32_t queued, uint32_t blocked) {
    retained.uptimeMs = ms; retained.inputLines = lines;
    retained.outputQueued = queued; retained.outputBlocked = blocked;
    retained.loops = retained.loops + 1;
}
} }
