// SPDX-License-Identifier: MIT
#pragma once
#include <cassert>
#include "Hardware.h"
constexpr esp_err_t ESP_ERR_NOT_FOUND = 0x105, ESP_ERR_INVALID_STATE = 0x103;
struct esp_task_wdt_config_t { uint32_t timeout_ms, idle_core_mask; bool trigger_panic; };
inline esp_err_t esp_task_wdt_status(void*) {
    return !hardware.watchdogInitialized ? ESP_ERR_INVALID_STATE :
        hardware.watchdogSubscribed ? ESP_OK : ESP_ERR_NOT_FOUND;
}
inline esp_err_t esp_task_wdt_init(const esp_task_wdt_config_t* config) {
    hardware.watchdogTimeoutMs = config->timeout_ms;
    hardware.watchdogIdleMask = config->idle_core_mask;
    hardware.watchdogPanic = config->trigger_panic;
    if (!hardware.watchdogSetupResult) hardware.watchdogInitialized = true;
    return hardware.watchdogSetupResult;
}
inline esp_err_t esp_task_wdt_reconfigure(const esp_task_wdt_config_t* config) { return esp_task_wdt_init(config); }
inline esp_err_t esp_task_wdt_add(void*) {
    if (!hardware.watchdogAddResult) hardware.watchdogSubscribed = true;
    return hardware.watchdogAddResult;
}
inline esp_err_t esp_task_wdt_reset() {
    assert(hardware.watchdogSubscribed);
    ++hardware.watchdogFeeds; hardware.watchdogFeedAt = hardware.time;
    return hardware.watchdogFeedResult;
}
