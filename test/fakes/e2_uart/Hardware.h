// SPDX-License-Identifier: MIT
#pragma once
#include "FakeEsp.h"
#include <deque>
#include <vector>

struct Hardware {
    uint64_t time = 1000;
    uint64_t writeStarted = 0;
    unsigned clockStep = 1, criticalDepth = 0, writes = 0, rxResets = 0;
    bool driverInstalled = false;
    unsigned rxChecks = 0, publishOnCheck = 0;
    int levelResult = ESP_OK, directionResult = ESP_OK, configResult = ESP_OK;
    int pinResult = ESP_OK, de = -1, txPin = -1, rxPin = -1;
    uart_config_t config = {};
    std::deque<uint8_t> rx;
    std::vector<uint8_t> tx;
};
extern Hardware hardware;
void resetHardware();
