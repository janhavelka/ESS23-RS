// SPDX-License-Identifier: MIT
#include "ProbePlatform.h"
#include <cstdio>
#include <driver/usb_serial_jtag.h>
#include <hal/usb_serial_jtag_ll.h>
#include <esp_rom_sys.h>

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
    // CPU resets can retain HWCDC's BUS_RESET enable. This SDK driver only
    // services RX/TX: mask inherited sources before it installs its own ISR.
    // Keep pending status/FIFO contents, including TX-empty for the first write.
    usb_serial_jtag_ll_disable_intr_mask(USB_SERIAL_JTAG_LL_INTR_MASK);
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
