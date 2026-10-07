// SPDX-License-Identifier: MIT
#include "ProbePlatform.h"
#include <Arduino.h>

#if ARDUINO_USB_CDC_ON_BOOT
#error The standalone console owns the IDF USB driver; Arduino CDC auto-start must be disabled
#endif

namespace MotorControlRSExample { namespace Platform {
void idle(unsigned milliseconds) { delay(milliseconds); }
} }
