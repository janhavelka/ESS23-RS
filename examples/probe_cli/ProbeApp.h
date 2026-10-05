// SPDX-License-Identifier: MIT
#pragma once
#include "../common/Esp32S3Uart.h"

namespace MotorControlRSExample {
/** Construct the shared standalone application in required PSRAM and initialize
 * its one UART/capture owner. Failure is explicit; there is no internal-RAM
 * fallback. Pins/topology are supplied by the selected example configuration.
 * Call once from the same task that subsequently calls serviceApplication().
 */
bool beginApplication(const Esp32S3Uart::Pins&, bool receiverDisabledDuringTransmit);
/// One bounded cooperative console/owner service turn, including an idle yield.
void serviceApplication();
}
