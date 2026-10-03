// SPDX-License-Identifier: MIT
#include "Esp32Load.h"
#include <Arduino.h>
#include <esp_timer.h>
#include <cstring>

namespace MotorControlRSExample {
namespace {
uint64_t clockUs() { return static_cast<uint64_t>(esp_timer_get_time()); }
constexpr uint32_t PERIOD_MS = 10;
}

bool Esp32Load::begin() {
    if (worker_) return false;
    output_ = xSemaphoreCreateMutexStatic(&outputState_);
    if (!output_) return false;
    epochUs_ = clockUs();
    idleStart_[0] = ulTaskGetIdleRunTimeCounterForCore(0);
    idleStart_[1] = ulTaskGetIdleRunTimeCounterForCore(1);
    worker_ = xTaskCreateStaticPinnedToCore(task, "probe_load", sizeof(stack_), this,
        2, stack_, &taskState_, xPortGetCoreID());
    return worker_ != nullptr;
}

void Esp32Load::writeLine(const char* text, std::size_t size) {
    if (output_) xSemaphoreTake(output_, portMAX_DELAY);
    Serial.write('\n'); // Resynchronize after a short diagnostic write/disconnect.
    Serial.write(reinterpret_cast<const uint8_t*>(text), size);
    Serial.write('\n');
    if (output_) xSemaphoreGive(output_);
}

uint32_t Esp32Load::ownerDelayUs() {
    portENTER_CRITICAL(&mux_);
    const uint32_t value = settings_.ownerDelayUs;
    portEXIT_CRITICAL(&mux_);
    return value;
}

void Esp32Load::serviced(uint64_t atUs, bool active) {
    // Called only by the owner; stats queries/configuration use that same task.
    if (active && ownerAt_ && atUs - ownerAt_ > ownerGap_) ownerGap_ = atUs - ownerAt_;
    ownerAt_ = active ? atUs : 0;
}

void Esp32Load::resetStats(Esp32S3Uart& uart) {
    const uint32_t idle0 = ulTaskGetIdleRunTimeCounterForCore(0);
    const uint32_t idle1 = ulTaskGetIdleRunTimeCounterForCore(1);
    portENTER_CRITICAL(&mux_);
    ++generation_;
    epochUs_ = clockUs();
    workUs_ = iterations_ = lines_ = dropped_ = 0;
    idleStart_[0] = idle0; idleStart_[1] = idle1;
    portEXIT_CRITICAL(&mux_);
    ownerAt_ = ownerGap_ = 0;
    uart.resetStats(); // Never clears transport or capture faults.
}

Probe::Action Esp32Load::configure(const Probe::LoadSettings* requested,
                              Probe::LoadSnapshot& out, Esp32S3Uart& uart) {
    if (!worker_) return Probe::Action::UNAVAILABLE;
    if (requested && (requested->workUs > 5000 || requested->ownerDelayUs > 20000 ||
                      requested->consoleBytes > 256)) return Probe::Action::FAILED;
    if (requested) {
        portENTER_CRITICAL(&mux_);
        settings_ = *requested;
        portEXIT_CRITICAL(&mux_);
        resetStats(uart);
    }
    portENTER_CRITICAL(&mux_);
    out.settings = settings_;
    out.elapsedUs = clockUs() - epochUs_;
    out.workUs = workUs_; out.workIterations = iterations_;
    out.consoleLines = lines_; out.consoleDropped = dropped_;
    portEXIT_CRITICAL(&mux_);
    out.ownerGapMaxUs = ownerGap_;
    const auto capture = uart.stats();
    out.captureUs = capture.busyUs; out.captureSamples = capture.samples;
    out.captureGapMaxUs = capture.maxGapUs; out.timer = capture.timer;
    out.workStackFreeBytes = uxTaskGetStackHighWaterMark(worker_);
    out.ready = capture.ready;
    // Scheduler accounting is a CPU-load estimate: interrupts can be charged
    // to the interrupted task, including idle. Capture section time is separate.
    // U32 deltas are valid only within one counter revolution (~71 minutes).
    out.cpuValid = out.elapsedUs >= 100000 && out.elapsedUs < UINT32_MAX;
    if (out.cpuValid) {
        uint8_t* percentages[] = {&out.cpu0BusyPct, &out.cpu1BusyPct};
        for (unsigned core = 0; core < 2; ++core) {
            const uint32_t idle = ulTaskGetIdleRunTimeCounterForCore(core) - idleStart_[core];
            const uint64_t idlePercent = static_cast<uint64_t>(idle) * 100 / out.elapsedUs;
            *percentages[core] = idlePercent >= 100 ? 0 : static_cast<uint8_t>(100 - idlePercent);
        }
    }
    return Probe::Action::OK;
}

void Esp32Load::task(void* context) { static_cast<Esp32Load*>(context)->run(); }

void Esp32Load::run() {
    TickType_t wake = xTaskGetTickCount();
    volatile uint32_t value = 1;
    for (;;) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(PERIOD_MS));
        portENTER_CRITICAL(&mux_);
        const Probe::LoadSettings settings = settings_;
        const uint32_t generation = generation_;
        portEXIT_CRITICAL(&mux_);
        const uint64_t started = clockUs();
        while (clockUs() - started < settings.workUs) value = value * 1664525U + 1013904223U;
        const uint64_t elapsed = clockUs() - started;
        bool written = false;
        if (settings.consoleBytes && xSemaphoreTake(output_, 0) == pdTRUE) {
            // One bounded whole line. Drop load text when USB is back-pressured;
            // never intentionally corrupt an in-progress JSON response.
            char line[265];
            std::memcpy(line, "\n# load ", 8);
            std::memset(line + 8, '.', settings.consoleBytes);
            line[8 + settings.consoleBytes] = '\n';
            const std::size_t size = 9 + settings.consoleBytes;
            if (Serial.availableForWrite() >= static_cast<int>(size))
                written = Serial.write(reinterpret_cast<const uint8_t*>(line), size) == size;
            xSemaphoreGive(output_);
        }
        portENTER_CRITICAL(&mux_);
        if (generation == generation_) {
            workUs_ += elapsed;
            ++iterations_;
            if (written) ++lines_;
            else if (settings.consoleBytes) ++dropped_;
        }
        portEXIT_CRITICAL(&mux_);
    }
}
} // namespace MotorControlRSExample
