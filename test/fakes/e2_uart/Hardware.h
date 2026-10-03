// SPDX-License-Identifier: MIT
#pragma once
#include "FakeEsp.h"
#include "driver/gptimer.h"
#include <deque>
#include <vector>

enum class WireEventKind { RX_START, RX_END, TX_FIFO_EMPTY, TX_IDLE };
struct WireEvent {
    uint64_t at;
    WireEventKind kind;
    uint8_t value;
    WireEvent(uint64_t time, WireEventKind event, uint8_t byte = 0)
        : at(time), kind(event), value(byte) {}
};

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
    // Optional wire schedule. Events arrive even when neither the adapter nor
    // runner is serviced. Old tests may still manipulate registers directly.
    std::deque<WireEvent> events;
    unsigned fifoCapacity = 128, droppedBytes = 0, txCharacterUs = 0;
    gptimer_alarm_cb_t timerCallback = nullptr;
    void* timerContext = nullptr;
    uint64_t timerPeriod = 0, timerNext = 0;
    bool timerCreated = false, timerEnabled = false, timerRunning = false, inTimer = false;
    unsigned timerCalls = 0, timerCallbacks = 0, timerDeletes = 0;
    std::vector<unsigned> timerFailCalls;
};
extern Hardware hardware;
void resetHardware();
void advanceHardware(uint64_t at);
void scheduleWire(const WireEvent& event);
void scheduleReply(uint64_t start, const std::vector<uint8_t>& bytes,
                   uint32_t characterUs = 87, uint32_t gapUs = 0);
