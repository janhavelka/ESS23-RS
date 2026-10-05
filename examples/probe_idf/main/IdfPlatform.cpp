// SPDX-License-Identifier: MIT
#include "ProbePlatform.h"
#include <cstdio>
#include <driver/usb_serial_jtag.h>
#include <esp_idf_version.h>
#include <esp_rom_sys.h>
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
#if CONFIG_LOG_DEFAULT_LEVEL != 0
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

namespace MotorControlRSExample { namespace Platform {
namespace {
constexpr std::size_t kWriteChunk = 64;
bool consoleReady = false;
int pendingByte = -1;
}

bool beginConsole() {
    if (consoleReady || usb_serial_jtag_is_driver_installed()) return false;
    usb_serial_jtag_driver_config_t config = {};
    config.tx_buffer_size = 1024;
    config.rx_buffer_size = 256;
    if (usb_serial_jtag_driver_install(&config) != ESP_OK) return false;
    pendingByte = -1;
    consoleReady = true;
    // ROM/bootloader output can leave a partial line in the USB hardware FIFO
    // while the host is absent. Delimit it before the first correlated record.
    return usb_serial_jtag_write_bytes("\n", 1, 0) == 1;
}

int availableConsole() {
    if (!consoleReady) return 0;
    if (pendingByte < 0) {
        uint8_t byte = 0;
        if (usb_serial_jtag_read_bytes(&byte, 1, 0) == 1) pendingByte = byte;
    }
    return pendingByte < 0 ? 0 : 1;
}

int readConsole() {
    if (!availableConsole()) return -1;
    const int byte = pendingByte;
    pendingByte = -1;
    return byte;
}

int writableConsole() {
    // IDF offers no public free-space query. A zero-tick write attempts one
    // bounded chunk; backpressure returns zero and the application retains it.
    return consoleReady ? static_cast<int>(kWriteChunk) : 0;
}

std::size_t writeConsole(const uint8_t* data, std::size_t size) {
    if (!consoleReady || !data || !size || size > kWriteChunk) return 0;
    const int written = usb_serial_jtag_write_bytes(data, size, 0);
    return written > 0 && static_cast<std::size_t>(written) <= size
        ? static_cast<std::size_t>(written) : 0;
}

void idle(unsigned milliseconds) {
    TickType_t ticks = pdMS_TO_TICKS(milliseconds);
    if (milliseconds && !ticks) ticks = 1;
    vTaskDelay(ticks);
}

void bootFailure(const char* reason) {
    char message[128];
    const int length = std::snprintf(message, sizeof(message),
        "\n{\"type\":\"boot\",\"ok\":false,\"error\":\"%.64s\"}\n", reason ? reason : "unknown");
    if (length <= 0 || static_cast<std::size_t>(length) >= sizeof(message)) return;
    if (consoleReady) {
        // Startup failure gets one nonblocking attempt, never a flush loop.
        usb_serial_jtag_write_bytes(message, static_cast<std::size_t>(length), 0);
    } else {
        // The configured ROM console remains observable if driver setup failed.
        esp_rom_printf("%s", message);
    }
}
} }
