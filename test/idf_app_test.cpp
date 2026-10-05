// SPDX-License-Identifier: MIT
// Compile the actual IDF startup/USB wrapper and shared workflow with the same
// scheduled UART fake used by Arduino. No second command/operation model.
#include "fakes/esp32_uart/FakeUsb.h"
#include "../examples/probe_cli/ProbeApp.cpp"
#include "../examples/probe_idf/main/IdfPlatform.cpp"
#include "../examples/probe_idf/main/main.cpp"
#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <vector>

namespace {
const std::vector<uint8_t> MODEL_REPLY = {1, 3, 2, 0, 0x3C, 0xB8, 0x55};

void fresh() {
    if (app) { app->~App(); std::free(app); app = nullptr; }
    uart.~Esp32S3Uart(); new (&uart) Esp32S3Uart;
#if MOTORCONTROLRS_LOAD_FIXTURE
    loadFixture.~Esp32Load(); new (&loadFixture) Esp32Load;
    fixtureReady = false; nextServiceUs = 0;
#endif
    resetHardware(); resetUsbHardware();
    Platform::consoleReady = false; Platform::pendingByte = -1;
    platformReady = false;
}
void startOwner() {
    app_main();
    assert(hardware.taskCreates == 1 && hardware.createdTask);
    assert(hardware.taskCore == 1 && hardware.taskPriority == 1 && hardware.taskStackBytes == 8192);
    const auto task = hardware.createdTask;
    hardware.runningCore = hardware.taskCore;
    hardware.stopOnIdle = true;
    try { task(nullptr); assert(false); }
    catch (const FakeTaskStopped&) {}
    hardware.stopOnIdle = false;
}
void step(uint32_t us = 10) { advanceHardware(hardware.time + us); serviceApplication(); }
void pump(unsigned turns = 128) { for (unsigned i = 0; i < turns; ++i) step(); }
void contains(const char* text) {
    if (Serial.output.find(text) == std::string::npos)
        std::fprintf(stderr, "Missing [%s] in IDF output [%.*s]\n", text,
                     static_cast<int>(std::min<std::size_t>(Serial.output.size(), 3000)), Serial.output.c_str());
    assert(Serial.output.find(text) != std::string::npos);
}
void command(const char* text) {
    Serial.input = text; Serial.output.clear();
    for (unsigned i = 0; i < 1000 && (Serial.available() || Platform::pendingByte >= 0); ++i) step();
    assert(Serial.input.empty() && Platform::pendingByte < 0); pump(512);
}
Probe::ResultView result(uint32_t operation) {
    Probe::ResultView view; assert(lookup(app, operation, view)); return view;
}
std::size_t occurrences(const std::string& text, const char* token) {
    std::size_t count = 0, pos = 0;
    while ((pos = text.find(token, pos)) != std::string::npos) { ++count; pos += std::strlen(token); }
    return count;
}

void testActualStartupAndPassiveCommands() {
    fresh(); startOwner();
    assert(app && platformReady && uart.ready() && uart.stats().timer);
    assert(writeResponseConfirmed); // IDF board Kconfig supplies this topology explicitly.
#if MOTORCONTROLRS_LOAD_FIXTURE
    assert(hardware.taskCreates == 2 && hardware.taskCore == 1 && hardware.taskPriority == 2 && hardware.taskStackBytes == 4096);
#endif
    assert(hardware.writes == 0 && hardware.allocationCalls == 1);
    assert(hardware.allocationCaps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    assert(usbHardware.installs == 1 && usbHardware.config.tx_buffer_size == 1024 && usbHardware.config.rx_buffer_size == 256);
    assert(hardware.txPin == 47 && hardware.rxPin == 48 && hardware.dePin == 21);
    command("@1 version\n@2 status\n@3 health\n@4 memory\n@5 stats reset\n");
    contains("\"command\":\"version\""); contains("\"command\":\"stats\"");
    assert(hardware.writes == 0 && Serial.maxWriteSize <= 64);
}

void testExplicitRuntimeTopologyRemainsUnconfirmed() {
    fresh();
    assert(beginApplication({47, 48, 21, true}, false));
    assert(app && !writeResponseConfirmed && hardware.writes == 0);
    command("@1 caps\n"); contains("\"write_response_confirmed\":false");
    assert(hardware.writes == 0);
}

void testStartupDelimitsRetainedBootBytesBeforeCorrelatedReply() {
    fresh();
    // The real first-IDF smoke retained this incomplete bootloader segment log
    // in the USB FIFO. Keep it here instead of clearing output in command().
    const std::string boot = "I (111) esp_image: segment 5: paddr=00088e38 vaddr=50000000 size";
    Serial.output = boot;
    Serial.input = "@1 version\n";
    startOwner(); pump();
    assert(app && platformReady && hardware.writes == 0);
    assert(Serial.output.compare(0, boot.size() + 1, boot + "\n") == 0);
    const auto greeting = Serial.output.find("MotorControl-RS ", boot.size() + 1);
    assert(greeting == boot.size() + 1);
    const auto replyAt = Serial.output.find("{\"type\":\"reply\"", greeting);
    assert(replyAt != std::string::npos && Serial.output[replyAt - 1] == '\n');
    const auto welcome = Serial.output.substr(greeting, replyAt - greeting);
    assert(welcome.find("Type help or ?") != std::string::npos);
    const std::string reply = Serial.output.substr(replyAt);
    assert(reply.find("{\"type\":\"reply\"") == 0);
    assert(reply.find("\"id\":1,\"command\":\"version\",\"ok\":true") != std::string::npos);
    assert(reply.find('\n') == reply.size() - 1);
    assert(occurrences(reply, "\"command\":\"version\"") == 1);
}

void testStartupBoundaryPrecedesBootFailureAndMustBeQueued() {
    fresh(); Serial.output = "retained ROM fragment";
    hardware.allocationFails = true;
    startOwner();
    assert(!app && !platformReady && hardware.allocationCalls == 1 && hardware.writes == 0);
    assert(Serial.output.find("retained ROM fragment\n") == 0);
    const auto bootJson = Serial.output.find("{\"type\":\"boot\"");
    assert(bootJson != std::string::npos && bootJson > 0 && Serial.output[bootJson - 1] == '\n');
    contains("psram_allocation");

    fresh(); usbHardware.bootOutput = "retained ROM fragment";
    usbHardware.installResult = -1;
    startOwner();
    assert(!app && !platformReady && hardware.allocationCalls == 0 && hardware.configCalls == 0 && hardware.writes == 0);
    assert(usbHardware.bootOutput.find("retained ROM fragment\n{\"type\":\"boot\"") == 0);

    fresh(); Serial.output = "retained ROM fragment";
    Serial.writeCapacity = 0; // SDK's all-or-zero queue admission refuses the boundary.
    startOwner();
    assert(!app && !platformReady && hardware.allocationCalls == 0 && hardware.configCalls == 0);
    assert(hardware.writes == 0 && Serial.output == "retained ROM fragment");
}

void testInitializationFailuresHaveNoInternalFallbackOrMotorTraffic() {
    for (unsigned failure = 0; failure < 5; ++failure) {
        fresh();
        if (failure == 0) hardware.allocationFails = true;
        if (failure == 1) usbHardware.installResult = -1;
        if (failure == 2) hardware.configResult = -1;
        if (failure == 3) hardware.timerFailCalls.push_back(1);
        if (failure == 4) usbHardware.installed = true;
        const bool ready = beginApplication({47, 48, 21, true}, true);
        assert(!ready && !platformReady && hardware.writes == 0);
        assert(hardware.allocationCalls <= 1);
        assert(Serial.output.find("\"ok\":false") != std::string::npos || usbHardware.bootOutput.find("\"ok\":false") != std::string::npos);
        if (failure == 0) assert(!app && hardware.allocationCalls == 1);
        if (failure == 4) assert(!app && !usbHardware.installs && !hardware.allocationCalls && !hardware.configCalls);
        if (app && failure != 1) {
            command("@1 status\n@2 probe\n");
            contains("\"ready\":false"); contains("\"result\":\"unavailable\"");
            assert(hardware.writes == 0);
        }
    }
}

void testOwnerAndLoadTaskFailuresRemainVisible() {
    fresh(); hardware.taskCreationFails = true; app_main();
    assert(!app && hardware.allocationCalls == 0 && hardware.writes == 0);
    contains("owner_task_allocation");
#if MOTORCONTROLRS_LOAD_FIXTURE
    fresh(); hardware.taskCreationFails = true;
    assert(!beginApplication({47, 48, 21, true}, true));
    assert(app && !platformReady && !fixtureReady && hardware.writes == 0);
    contains("\"ok\":false");
#endif
}

void testIdfUsbByteCachingBudgetsAndPartialOutput() {
    fresh(); startOwner();
    Serial.input = "ab";
    assert(Platform::availableConsole() == 1);
    const unsigned reads = usbHardware.reads;
    assert(Platform::availableConsole() == 1 && usbHardware.reads == reads);
    assert(Platform::readConsole() == 'a' && Platform::readConsole() == 'b' && Platform::readConsole() == -1);
    Serial.input = "@1 status\n@2 health\n@3 drv\n@4 stats\n";
    // Extra defensive wrapper case; the pinned USB SDK normally returns all-or-zero.
    usbHardware.syntheticPartialWrites = true;
    Serial.output.clear(); Serial.writeLimit = 7;
    const std::size_t input = Serial.input.size();
    step(); assert(input - Serial.input.size() <= 32);
    assert(app->inputBytes <= 32 && !Serial.input.empty());
    pump(1000); assert(Serial.input.empty()); contains("\"command\":\"stats\"");
    assert(Serial.maxWriteSize <= 64 && hardware.writes == 0 && app->outputShortWrites > 0);
    uint8_t byte = 'x';
    assert(Platform::writeConsole(&byte, 65) == 0);
    usbHardware.writeError = -1; assert(Platform::writeConsole(&byte, 1) == 0);
}

void testActiveOwnerUnderUsbBackpressureRetainsOneTerminal() {
    fresh(); startOwner(); hardware.txCharacterUs = 87;
    command("@1 probe\n"); contains("\"result\":\"accepted\"");
    for (unsigned i = 0; i < 1000 && !hardware.writes; ++i) step();
    assert(hardware.writes == 1 && app->owner.active());
    Serial.output.clear(); Serial.writeCapacity = 0;
    Serial.input = "@2 status\n@3 health\n@4 drv\n";
    scheduleReply(hardware.time + 1000, MODEL_REPLY);
    for (unsigned i = 0; i < 2000 && result(1).pending; ++i) step();
    assert(!result(1).pending && app->ok && Serial.output.empty());
    const auto retained = result(1).probe.transport.endedUs;
    pump(); assert(result(1).probe.transport.endedUs == retained);
    Serial.writeCapacity = 4096; pump(2000); // Real IDF all-or-zero driver contract.
    assert(occurrences(Serial.output, "\"type\":\"probe\"") == 1);
    command("@5 result 1\n"); contains("\"raw_model\":60");
    assert(hardware.writes == 1 && Serial.maxWriteSize <= 64);
    command("@6 release 1\n"); contains("\"result\":\"done\"");
}

void testSharedCompleteInventoryAndRejectedRequestsNoTx() {
    fresh(); startOwner(); command("@1 help\n");
    for (const char* name : {"discover", "debug", "motion-profile", "host", "wiring", "communication", "persistence", "read", "profile", "driver", "io", "segment", "control", "tuning", "home", "move", "velocity", "monitor", "axis", "prepare", "result", "cancel", "recover", "load"})
        contains((std::string("\"") + name + "\"").c_str());
    command("@2 caps\n");
    for (const char* field : {"\"io_settings\":true", "\"control_settings\":true", "\"tuning\":true", "\"persistence\":true", "\"discovery\":true", "\"segment_execution\":\"external_input\""}) contains(field);
    for (const char* invalid : {"@3 io set x4 none\n", "@4 segment position 17 read\n", "@5 tuning collision set current 1\n", "@6 move relative 1.25 steps native 60 configured\n"}) {
        command(invalid); contains("\"ok\":false"); assert(hardware.writes == 0);
    }
}
}

int main() {
    testActualStartupAndPassiveCommands();
    testExplicitRuntimeTopologyRemainsUnconfirmed();
    testStartupDelimitsRetainedBootBytesBeforeCorrelatedReply();
    testStartupBoundaryPrecedesBootFailureAndMustBeQueued();
    testInitializationFailuresHaveNoInternalFallbackOrMotorTraffic();
    testOwnerAndLoadTaskFailuresRemainVisible();
    testIdfUsbByteCachingBudgetsAndPartialOutput();
    testActiveOwnerUnderUsbBackpressureRetainsOneTerminal();
    testSharedCompleteInventoryAndRejectedRequestsNoTx();
    fresh();
}
