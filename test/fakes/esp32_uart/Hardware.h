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
    uint64_t deReleasedAt = 0; ///< Physical receive-mode GPIO write, independent of owner service.
    unsigned clockStep = 1, criticalDepth = 0, writes = 0, rxResets = 0;
    bool driverInstalled = false;
    unsigned rxChecks = 0, publishOnCheck = 0;
    int levelResult = ESP_OK, directionResult = ESP_OK, configResult = ESP_OK;
    int baudResult = ESP_OK;
    uint32_t actualBaud = 0; ///< Zero derives the real SDK XTAL divider result.
    unsigned configCalls = 0, baudCalls = 0;
    uint32_t configDelayUs = 0; ///< SDK setup interval outside capture's next epoch.
    std::vector<unsigned> configFailCalls;
    int pinResult = ESP_OK, de = -1, dePin = -1, txPin = -1, rxPin = -1;
    int transmitLevel = 1; // Physical fake transceiver polarity, independent of adapter settings.
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
    // Startup fault injection shared by the real Arduino/IDF example paths.
    bool allocationFails = false, taskCreationFails = false;
    unsigned allocationCalls = 0, allocationCaps = 0, taskCreates = 0;
    unsigned taskStackBytes = 0, taskPriority = 0;
    int taskCore = -1;
    int runningCore = 0;
    void (*createdTask)(void*) = nullptr;
    void* createdTaskContext = nullptr;
    bool stopOnIdle = false;
};
struct FakeTaskStopped {};
extern Hardware hardware;
void resetHardware();
void advanceHardware(uint64_t at);
void scheduleWire(const WireEvent& event);
void scheduleReply(uint64_t start, const std::vector<uint8_t>& bytes,
                   uint32_t characterUs = 87, uint32_t gapUs = 0);
