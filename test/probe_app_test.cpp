// SPDX-License-Identifier: MIT
// Compile the actual setup/loop and host callbacks against the same SDK fake as
// the adapter tests. No application admission, completion or recovery is copied.
#include "../examples/probe_cli/main.cpp"
#include <cassert>
#include <cstdlib>
#include <string>
#include <vector>

FakeSerial Serial;
namespace {
void fresh() {
    if (app) { app->~App(); std::free(app); app = nullptr; }
    uart.~Esp32S3Uart(); new (&uart) Esp32S3Uart;
    resetHardware(); Serial = FakeSerial();
    setup();
    assert(app && uart.ready() && hardware.writes == 0);
}
void contains(const char* text) { assert(Serial.output.find(text) != std::string::npos); }
void command(const char* text) {
    Serial.input = text; Serial.output.clear();
    for (unsigned i = 0; i < 20 && !Serial.input.empty(); ++i) loop();
    assert(Serial.input.empty());
}
void reply(const std::vector<uint8_t>& bytes) {
    unsigned sent = 0;
    for (unsigned i = 0; i < 25000 && app->active; ++i) {
        hardware.time += 10;
        if (hardware.writes) {
            const uint64_t end = hardware.writeStarted + 696;
            if (hardware.time >= end - 80) fakeUart.status.txfifo_cnt = 0;
            if (hardware.time >= end) fakeUart.fsm_status.st_utx_out = 0;
            const uint64_t first = end + 1000;
            if (sent < bytes.size() && hardware.time >= first + sent * 87 + 87)
                hardware.rx.push_back(bytes[sent++]);
            fakeUart.fsm_status.st_urx_out = sent < bytes.size() && hardware.time >= first + sent * 87 ? 1 : 0;
        }
        loop();
    }
    assert(!app->active);
}
void testSuccessfulProbeAndReset() {
    fresh();
    command("@1 probe 1\n"); contains("\"result\":\"accepted\"");
    reply({1, 3, 2, 0, 0x3C, 0xB8, 0x55});
    contains("\"type\":\"probe\""); contains("\"raw_model\":60");
    assert(app->known && app->ok && !app->parserFault && hardware.writes == 1);
    command("@2 reset\n"); contains("\"result\":\"done\"");
    assert(app->runner.stats().started == 0 && app->ok);
    command("@3 status\n"); contains("\"codec\":\"OK\"");
    contains("\"transmit_enabled\":false");
    assert(hardware.writes == 1); // Cached commands never touch the motor bus.
}
void testCheckedExceptionNeedsNoHostRecovery() {
    fresh(); command("@1 probe\n");
    reply({1, 0x83, 2, 0xC0, 0xF1});
    contains("\"codec\":\"EXCEPTION\""); contains("\"detail\":2");
    assert(app->known && !app->ok && !app->parserFault && !app->runner.needsRecovery());
    command("@2 status\n"); contains("\"codec\":\"EXCEPTION\"");
    contains("\"recovery_required\":false");
    command("@3 probe\n"); contains("\"result\":\"accepted\"");
    assert(app->active);
}
void testBadCrcRetainedAndInterlocked() {
    fresh(); command("@1 probe\n");
    reply({1, 3, 2, 0, 0x3C, 0xB8, 0x54});
    assert(!app->ok && app->parserFault);
    command("@2 status\n"); contains("\"codec\":\"CRC_ERROR\"");
    command("@3 reset\n"); assert(app->parserFault);
    command("@4 probe\n"); contains("\"result\":\"recovery_required\"");
    assert(hardware.writes == 1);
    hardware.time += RECOVER_US;
    command("@5 recover\n"); contains("\"result\":\"done\"");
    assert(!app->known && !app->parserFault && hardware.writes == 1);
    command("@6 status\n"); contains("\"codec\":\"CRC_ERROR\""); // Historical cause retained.
}
void testFaultWhileTransmitterStaysBusy() {
    fresh(); command("@1 probe\n");
    for (unsigned i = 0; i < 1000 && !hardware.writes; ++i) { hardware.time += 10; loop(); }
    assert(hardware.writes == 1 && app->active && hardware.de == 1);
    hardware.time += 30000; loop(); // Physical TX idle never arrives.
    assert(!app->active && app->runner.transmitEnabled());
    contains("\"transport\":\"TX_TIMEOUT\"");
    command("@2 status\n"); contains("\"transmit_enabled\":true"); contains("\"busy\":false");
    command("@3 recover\n"); contains("\"result\":\"busy\"");
    command("@4 probe\n"); contains("\"result\":\"recovery_required\"");
    assert(hardware.writes == 1 && hardware.de == 1);
    fakeUart.status.txfifo_cnt = 0; fakeUart.fsm_status.st_utx_out = 0;
    for (unsigned i = 0; i < 5; ++i) { hardware.time += 100; loop(); }
    assert(!app->runner.transmitEnabled() && hardware.de == 0);
    command("@5 status\n"); contains("\"transport\":\"TX_TIMEOUT\"");
    contains("\"transmit_enabled\":false");
}
void testNewCaptureFaultRejectsAdmission() {
    fresh();
    hardware.rx.push_back(1); hardware.rx.push_back(3);
    assert(probe(app, 1, 1) == Probe::Action::RECOVERY_REQUIRED);
    assert(hardware.writes == 0 && !app->active);
    command("@2 health\n"); contains("\"communication\":\"failed\"");
}
}
int main() {
    testSuccessfulProbeAndReset(); testCheckedExceptionNeedsNoHostRecovery();
    testBadCrcRetainedAndInterlocked(); testFaultWhileTransmitterStaysBusy();
    testNewCaptureFaultRejectsAdmission();
    app->~App(); std::free(app); app = nullptr;
}
