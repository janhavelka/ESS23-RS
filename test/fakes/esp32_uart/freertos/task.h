// SPDX-License-Identifier: MIT
#pragma once
#include "FreeRTOS.h"
#include "../Hardware.h"

struct StaticTask_t {};
using TaskHandle_t = StaticTask_t*;
inline TaskHandle_t xTaskCreateStaticPinnedToCore(void (*)(void*), const char*,
    unsigned, void*, unsigned, StackType_t*, StaticTask_t* state, int) {
    // Creation only: native application tests exercise the owner/configuration
    // paths. This fake deliberately does not simulate scheduler/CPU workload.
    return state;
}
inline int xPortGetCoreID() { return 0; }
inline uint32_t ulTaskGetIdleRunTimeCounterForCore(unsigned) { return 0; }
inline TickType_t xTaskGetTickCount() { return static_cast<TickType_t>(hardware.time / 1000); }
inline void vTaskDelayUntil(TickType_t* wake, TickType_t period) {
    *wake += period; advanceHardware(hardware.time + static_cast<uint64_t>(period) * 1000);
}
