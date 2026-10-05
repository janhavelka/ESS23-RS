// SPDX-License-Identifier: MIT
#include "FakeUsb.h"
#include <cstdarg>
#include <cstdio>
#include <cassert>

FakeSerial Serial;
UsbHardware usbHardware;
void resetUsbHardware() { usbHardware = UsbHardware(); Serial = FakeSerial(); }
bool usb_serial_jtag_is_driver_installed() { return usbHardware.installed; }
esp_err_t usb_serial_jtag_driver_install(const usb_serial_jtag_driver_config_t* config) {
    assert(config && !usbHardware.installed);
    ++usbHardware.installs; usbHardware.config = *config;
    if (usbHardware.installResult == ESP_OK) usbHardware.installed = true;
    return usbHardware.installResult;
}
int usb_serial_jtag_read_bytes(void* data, uint32_t size, TickType_t ticks) {
    assert(ticks == 0 && size <= 1); ++usbHardware.reads;
    if (usbHardware.readError) return usbHardware.readError;
    if (!Serial.available()) return 0;
    *static_cast<uint8_t*>(data) = static_cast<uint8_t>(Serial.read()); return 1;
}
int usb_serial_jtag_write_bytes(const void* data, std::size_t size, TickType_t ticks) {
    assert(ticks == 0 && size <= 256); ++usbHardware.writes;
    if (usbHardware.writeError) return usbHardware.writeError;
    if (!usbHardware.syntheticPartialWrites &&
        (Serial.writeCapacity < size || Serial.writeLimit < size)) return 0;
    return static_cast<int>(Serial.write(static_cast<const uint8_t*>(data), size));
}
int esp_rom_printf(const char* format, ...) {
    char text[256]; va_list args; va_start(args, format);
    const int size = std::vsnprintf(text, sizeof(text), format, args); va_end(args);
    if (size > 0) usbHardware.bootOutput.append(text, std::min<std::size_t>(size, sizeof(text) - 1));
    return size;
}
