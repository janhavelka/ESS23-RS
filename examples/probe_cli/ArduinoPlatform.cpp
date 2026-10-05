// SPDX-License-Identifier: MIT
#include "ProbePlatform.h"
#include "../common/BuildConfig.h"
#include <Arduino.h>
#include <cstdio>

namespace MotorControlRSExample { namespace Platform {
bool beginConsole() {
    const bool ready = Serial.setTxBufferSize(1024) == 1024;
    Serial.begin(kConsoleBaud);
    Serial.setTxTimeoutMs(0);
    return ready;
}
int availableConsole() { return Serial.available(); }
int readConsole() { return Serial.read(); }
int writableConsole() { return Serial.availableForWrite(); }
std::size_t writeConsole(const uint8_t* bytes, std::size_t size) { return Serial.write(bytes, size); }
void idle(unsigned milliseconds) { delay(milliseconds); }
void bootFailure(const char* reason) {
    char line[96];
    const int size = std::snprintf(line, sizeof(line), "{\"type\":\"boot\",\"ok\":false,\"error\":\"%s\"}\n", reason);
    if (size > 0 && size < static_cast<int>(sizeof(line)))
        writeConsole(reinterpret_cast<const uint8_t*>(line), static_cast<std::size_t>(size));
}
} }
