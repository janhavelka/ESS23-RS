// SPDX-License-Identifier: MIT
#pragma once
#include "../FakeUsb.h"
constexpr uint32_t USB_SERIAL_JTAG_LL_INTR_MASK = 0x7FF;
inline void usb_serial_jtag_ll_disable_intr_mask(uint32_t mask) {
    usbHardware.interruptEnable &= ~mask;
    ++usbHardware.maskCalls;
}
