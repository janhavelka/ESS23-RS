// SPDX-License-Identifier: MIT
#include "ProbePlatform.h"
#include <esp_idf_version.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <sdkconfig.h>

// This consumer uses the reviewed UART/GPTimer SDK implementation directly;
// Arduino build macros cannot establish these native SDK prerequisites.
#if ESP_IDF_VERSION != ESP_IDF_VERSION_VAL(5, 5, 5)
#error The standalone ESP-IDF adapter requires the reviewed ESP-IDF 5.5.5 SDK
#endif
#if !CONFIG_IDF_TARGET_ESP32S3
#error The standalone ESP-IDF adapter requires an ESP32-S3 target
#endif
#if CONFIG_PM_ENABLE || CONFIG_GPTIMER_ISR_CACHE_SAFE
#error Capture does not support power management or execution with the flash cache disabled
#endif
#if !CONFIG_GPTIMER_ISR_HANDLER_IN_IRAM || !CONFIG_GPTIMER_OBJ_CACHE_SAFE
#error Capture requires the reviewed IRAM driver handler and internal timer objects
#endif
// GPTimer's force-debug option overrides the pinned SDK's global log filter.
#if CONFIG_LOG_DEFAULT_LEVEL != 0 || CONFIG_GPTIMER_ENABLE_DEBUG_LOG
#error SDK logging must remain disabled on the correlated standalone USB console
#endif
#if !CONFIG_SPIRAM || !CONFIG_SPIRAM_BOOT_INIT
#error The standalone application requires initialized PSRAM for its explicit external allocation
#endif
#if CONFIG_FREERTOS_HZ != 1000
#error The standalone service and load fixture require the reviewed 1 kHz scheduler tick
#endif
#if !CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS || !CONFIG_FREERTOS_RUN_TIME_STATS_USING_ESP_TIMER || !CONFIG_FREERTOS_RUN_TIME_COUNTER_TYPE_U32
#error Load evidence requires ESP timer runtime statistics with U32 counters
#endif
#if !CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
#error Startup failure output requires the USB Serial/JTAG ROM console
#endif
#if !CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH || !CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF || !CONFIG_ESP_COREDUMP_CHECKSUM_CRC32
#error Standalone crash diagnostics require the espcoredump component and flash ELF dumps with CRC32
#endif

namespace MotorControlRSExample { namespace Platform {
void idle(unsigned milliseconds) {
    TickType_t ticks = pdMS_TO_TICKS(milliseconds);
    if (milliseconds && !ticks) ticks = 1;
    vTaskDelay(ticks);
}

} }
