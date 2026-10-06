// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <esp_task_wdt.h>
#include <esp_system.h>
#include <sdkconfig.h>

namespace MotorControlRSExample { namespace OwnerWatchdog {
// Example task supervision only. A controller reset cannot stop the drive,
// confirm an interrupted operation, or authorize replay of a motor command.
constexpr uint32_t TIMEOUT_MS = 5000;
static bool subscribed = false;
static esp_err_t error = ESP_OK;
static uint32_t resetReason = 0;
static uint64_t lastFeedUs = 0, completedLoops = 0;

inline bool begin() {
    subscribed = false; error = ESP_OK; lastFeedUs = completedLoops = 0;
    resetReason = static_cast<uint32_t>(esp_reset_reason());
    esp_task_wdt_config_t config = {};
    config.timeout_ms = TIMEOUT_MS;
    config.trigger_panic = true;
    // Preserve the SDK's selected idle-task checks as well as watching our owner.
#if CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0
    config.idle_core_mask |= 1;
#endif
#if CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1
    config.idle_core_mask |= 2;
#endif
    const esp_err_t status = esp_task_wdt_status(nullptr);
    if (status != ESP_OK && status != ESP_ERR_NOT_FOUND && status != ESP_ERR_INVALID_STATE) {
        error = status; return false;
    }
    error = status == ESP_ERR_INVALID_STATE ? esp_task_wdt_init(&config) : esp_task_wdt_reconfigure(&config);
    if (error != ESP_OK) return false;
    error = status == ESP_OK ? ESP_OK : esp_task_wdt_add(nullptr);
    if (error != ESP_OK) return false;
    subscribed = true;
    error = esp_task_wdt_reset();
    return error == ESP_OK;
}

inline bool completed(uint64_t sampledUs) {
    ++completedLoops;
    if (!subscribed || error != ESP_OK) return false;
    // Feed only after all owner/operation/console work returned. USB output
    // pressure must return to the loop; a separate timer must not hide a stall.
    if (!lastFeedUs || sampledUs < lastFeedUs || sampledUs - lastFeedUs >= 100000) {
        error = esp_task_wdt_reset();
        if (error == ESP_OK) lastFeedUs = sampledUs;
    }
    return error == ESP_OK;
}
} }
