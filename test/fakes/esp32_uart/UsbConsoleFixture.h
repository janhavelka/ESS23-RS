// SPDX-License-Identifier: MIT
#pragma once
#include "FakeUsb.h"
#include "../../../examples/probe_cli/Esp32UsbConsole.cpp"

inline void resetUsbConsole() {
    resetUsbHardware();
    MotorControlRSExample::Platform::consoleReady = false;
    MotorControlRSExample::Platform::pendingByte = -1;
}
