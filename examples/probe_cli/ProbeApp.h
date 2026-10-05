// SPDX-License-Identifier: MIT
#pragma once
#include "../common/Esp32S3Uart.h"

namespace MotorControlRSExample {
/** Age-only expiry is opt-in. Zero keeps checked observations until invalidated
 * by an event/configuration change; it does not prove the drive stayed powered.
 * Transaction deadlines, missing evidence and generation checks always apply. */
struct ApplicationOptions {
    uint32_t observationMaxAgeMs = 0;
};
/** Construct the shared standalone application in required PSRAM and initialize
 * its one UART/capture owner. Failure is explicit; there is no internal-RAM
 * fallback. Pins/topology are supplied by the selected example configuration.
 * Call once from the same task that subsequently calls serviceApplication().
 */
bool beginApplication(const Esp32S3Uart::Pins&, bool receiverDisabledDuringTransmit,
                      const ApplicationOptions& = ApplicationOptions());
/// One bounded cooperative console/owner service turn, including an idle yield.
void serviceApplication();
}
