// SPDX-License-Identifier: MIT
#pragma once
#include "Arduino.h" // Reuse the existing bounded serial fake; not production.
#include "driver/usb_serial_jtag.h"

struct UsbHardware {
    bool installed = false;
    int installResult = ESP_OK, readError = 0, writeError = 0;
    unsigned installs = 0, reads = 0, writes = 0;
    usb_serial_jtag_driver_config_t config = {};
    std::string bootOutput;
    bool syntheticPartialWrites = false; ///< Explicit robustness injection; SDK normally returns all-or-zero.
};
extern UsbHardware usbHardware;
void resetUsbHardware();
