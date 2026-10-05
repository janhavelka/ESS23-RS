// SPDX-License-Identifier: MIT
#pragma once
#include "FreeRTOS.h"
#include "../Hardware.h"

struct StaticTask_t {};
using TaskHandle_t = StaticTask_t*;
inline TaskHandle_t xTaskCreateStaticPinnedToCore(void (*task)(void*), const char*,
    unsigned bytes, void* context, unsigned priority, StackType_t*, StaticTask_t* state, int core) {
    // Creation only: native application tests exercise the owner/configuration
    // paths. This fake deliberately does not simulate scheduler/CPU workload.
    ++hardware.taskCreates; hardware.taskStackBytes = bytes;
    hardware.taskPriority = priority; hardware.taskCore = core;
    hardware.createdTask = task; hardware.createdTaskContext = context;
    return hardware.taskCreationFails ? nullptr : state;
}
inline int xPortGetCoreID() { return hardware.runningCore; }
inline uint32_t ulTaskGetIdleRunTimeCounterForCore(unsigned) { return 0; }
inline TickType_t xTaskGetTickCount() { return static_cast<TickType_t>(hardware.time / 1000); }
inline void vTaskDelayUntil(TickType_t* wake, TickType_t period) {
    *wake += period; advanceHardware(hardware.time + static_cast<uint64_t>(period) * 1000);
}
inline void vTaskDelay(TickType_t ticks) {
    advanceHardware(hardware.time + static_cast<uint64_t>(ticks) * 1000);
    if (hardware.stopOnIdle) throw FakeTaskStopped();
}
inline void vTaskDelete(TaskHandle_t) {}
#ifndef MOTORCONTROLRS_FAKE_STACK_WATERMARK
#define MOTORCONTROLRS_FAKE_STACK_WATERMARK
inline unsigned uxTaskGetStackHighWaterMark(void*) { return 4096; }
#endif
