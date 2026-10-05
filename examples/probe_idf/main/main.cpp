// SPDX-License-Identifier: MIT
#include "ProbeApp.h"
#include "ProbePlatform.h"
#include <esp_attr.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <sdkconfig.h>

#if !configSUPPORT_STATIC_ALLOCATION || configNUMBER_OF_CORES != 2
#error The standalone owner requires static FreeRTOS allocation and both ESP32-S3 cores
#endif

#if CONFIG_MOTORCONTROLRS_RS485_TX_PIN == 19 || CONFIG_MOTORCONTROLRS_RS485_TX_PIN == 20 || \
    CONFIG_MOTORCONTROLRS_RS485_RX_PIN == 19 || CONFIG_MOTORCONTROLRS_RS485_RX_PIN == 20 || \
    CONFIG_MOTORCONTROLRS_RS485_DE_PIN == 19 || CONFIG_MOTORCONTROLRS_RS485_DE_PIN == 20
#error GPIO19 and GPIO20 are reserved for the standalone USB Serial/JTAG console
#endif

namespace {
using namespace MotorControlRSExample;
// These driver-facing task objects stay internal even with external BSS enabled.
DRAM_ATTR StackType_t ownerStack[8192 / sizeof(StackType_t)];
DRAM_ATTR StaticTask_t ownerState;

void ownerTask(void*) {
    const Esp32S3Uart::Pins pins = {CONFIG_MOTORCONTROLRS_RS485_TX_PIN,
        CONFIG_MOTORCONTROLRS_RS485_RX_PIN, CONFIG_MOTORCONTROLRS_RS485_DE_PIN,
#if CONFIG_MOTORCONTROLRS_DE_ACTIVE_HIGH
        true
#else
        false
#endif
    };
#if CONFIG_MOTORCONTROLRS_RECEIVER_DISABLED_DURING_TX
    constexpr bool receiverDisabledDuringTransmit = true;
#else
    constexpr bool receiverDisabledDuringTransmit = false;
#endif
    beginApplication(pins, receiverDisabledDuringTransmit);
    // Initialization failure is reported by the shared application. Its cached
    // console remains available when storage exists; a missing App only idles.
    for (;;) serviceApplication();
}
}

extern "C" void app_main() {
    // ESP-IDF expresses this stack bound in bytes, unlike vanilla FreeRTOS.
    if (!xTaskCreateStaticPinnedToCore(ownerTask, "motor_owner", sizeof(ownerStack),
                                     nullptr, 1, ownerStack, &ownerState, 1)) {
        MotorControlRSExample::Platform::beginConsole();
        MotorControlRSExample::Platform::bootFailure("owner_task_allocation");
    }
}
