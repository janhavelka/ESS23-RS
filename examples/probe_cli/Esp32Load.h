// SPDX-License-Identifier: MIT
#pragma once
#include "ProbeConsole.h"
#include "../common/Esp32S3Uart.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

namespace MotorControlRSExample {

/** Optional ESP32 test fixture, outside the library. One competing task on the
 * owner's core, fixed internal stack, and serialized USB diagnostic lines.
 * This object has application lifetime and must remain in internal RAM.
 */
class Esp32Load {
public:
    Esp32Load() = default;
    Esp32Load(const Esp32Load&) = delete;
    Esp32Load& operator=(const Esp32Load&) = delete;
    bool begin();
    Probe::Action configure(const Probe::LoadSettings*, Probe::LoadSnapshot&, Esp32S3Uart&);
    uint32_t ownerDelayUs();
    void resetStats(Esp32S3Uart&); ///< One measurement window; preserves workload and faults.
    void serviced(uint64_t atUs, bool active);
    void writeLine(const char*, std::size_t);
private:
    static void task(void*);
    void run();
    portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
    StaticTask_t taskState_;
    StackType_t stack_[4096 / sizeof(StackType_t)];
    StaticSemaphore_t outputState_;
    SemaphoreHandle_t output_ = nullptr;
    TaskHandle_t worker_ = nullptr;
    Probe::LoadSettings settings_;
    uint64_t epochUs_ = 0, workUs_ = 0, iterations_ = 0, lines_ = 0, dropped_ = 0;
    uint64_t ownerAt_ = 0, ownerGap_ = 0;
    uint32_t generation_ = 0, idleStart_[2] = {};
};
} // namespace MotorControlRSExample
