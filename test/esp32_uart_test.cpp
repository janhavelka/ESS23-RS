// SPDX-License-Identifier: MIT
#include "../examples/common/Esp32S3Uart.h"
#include "fakes/esp32_uart/Hardware.h"
#include <cassert>
#include <deque>
#include <vector>

using MotorControlRSExample::Esp32S3Uart;
namespace Rtu = MotorControlRSExample::Rtu;
namespace {
constexpr Esp32S3Uart::Pins PINS = {47, 48, 21, true};
uint64_t sample(Esp32S3Uart& uart, uint64_t at) {
    hardware.time = at;
    const uint64_t result = uart.sample();
    assert(hardware.criticalDepth == 0);
    return result;
}
Rtu::ReadState read(Rtu::Port& port, uint64_t at, Rtu::RxByte& byte, uint64_t& through) {
    return port.read(port.context, at, byte, through);
}
void testInitialization() {
    resetHardware();
    Esp32S3Uart uart;
    Rtu::Port port = uart.port();
    uint64_t through = 777;
    Rtu::TxObservation tx;
    Rtu::RxByte byte;
    assert(port.txState(port.context, 1000, tx) == Rtu::TxState::ERROR);
    assert(read(port, 1000, byte, through) == Rtu::ReadState::ERROR);
    assert(!uart.clear());
    assert(!uart.begin(PINS, 9600));
    hardware.driverInstalled = true;
    assert(!uart.begin(PINS));
    assert(hardware.de == -1); // Refuse another driver's peripheral before GPIO changes.
    hardware.driverInstalled = false;
    assert(uart.begin(PINS));
    assert(uart.ready() && hardware.de == 0);
    assert(hardware.txPin == 47 && hardware.rxPin == 48);
    assert(hardware.config.baud_rate == 115200 && hardware.config.data_bits == UART_DATA_8_BITS);
    assert(hardware.config.parity == UART_PARITY_DISABLE && hardware.config.stop_bits == UART_STOP_BITS_1);
    assert(!uart.begin(PINS));
    for (bool activeHigh : {false, true}) {
        const Esp32S3Uart::Pins pins = {4, 5, 6, activeHigh};
        for (unsigned failure = 0; failure < 4; ++failure) {
            resetHardware();
            Esp32S3Uart failing;
            if (failure == 0) hardware.levelResult = -1;
            if (failure == 1) hardware.directionResult = -1;
            if (failure == 2) hardware.configResult = -1;
            if (failure == 3) hardware.pinResult = -1;
            assert(!failing.begin(pins) && !failing.ready());
            assert(hardware.writes == 0 && !failing.startCapture());
            if (failure != 0) assert(hardware.de == (activeHigh ? 0 : 1));
            hardware.levelResult = hardware.directionResult = hardware.configResult = hardware.pinResult = ESP_OK;
            assert(failing.begin(pins)); // Partial initialization remains retryable.
            assert(hardware.de == (activeHigh ? 0 : 1) && hardware.writes == 0);
        }
    }
}
void testPhysicalTx() {
    resetHardware();
    Esp32S3Uart uart;
    assert(uart.begin(PINS));
    Rtu::Port port = uart.port();
    const uint8_t frame[] = {1, 3, 0, 0, 0, 1, 0x84, 0x0A};
    assert(port.write(port.context, frame, sizeof(frame)).error);
    assert(hardware.writes == 0);
    assert(port.setTransmit(port.context, true));
    const Rtu::WriteResult sent = port.write(port.context, frame, sizeof(frame));
    assert(!sent.error && sent.accepted == sizeof(frame));
    assert(hardware.writes == 1 && hardware.tx == std::vector<uint8_t>(frame, frame + sizeof(frame)));
    Rtu::TxObservation tx;
    assert(port.txState(port.context, 1100, tx) == Rtu::TxState::BUSY);
    fakeUart.status.txfifo_cnt = 0; // Last byte left FIFO; shifter still sends its stop bit.
    fakeUart.fsm_status.st_utx_out = 1;
    sample(uart, 1100);
    assert(port.txState(port.context, 1100, tx) == Rtu::TxState::BUSY);
    assert(!port.setTransmit(port.context, false) && hardware.de == 1);
    assert(!uart.clear());
    fakeUart.fsm_status.st_utx_out = 0;
    const uint64_t at = sample(uart, 1110);
    assert(port.txState(port.context, at, tx) == Rtu::TxState::BUSY); // Snapshot upper bound is later.
    assert(port.txState(port.context, 1112, tx) == Rtu::TxState::IDLE);
    assert(tx.endedUs == 1112 && tx.endedUs - tx.uncertaintyUs == 1100);
    assert(port.setTransmit(port.context, false) && hardware.de == 0);
    assert(uart.clear());
    assert(hardware.writes == 1); // Recovery never repeats the write.
}
void testPinValidation() {
    const Esp32S3Uart::Pins invalid[] = {
        {-1, 48, 21, true}, {47, -1, 21, true}, {47, 48, -1, true},
        {49, 48, 21, true}, {47, 64, 21, true}, {47, 48, 1000, true},
        {22, 48, 21, true}, {47, 25, 21, true}, {47, 48, 24, true},
        {47, 47, 21, true}, {47, 48, 47, true}, {47, 48, 48, true}
    };
    for (const auto& pins : invalid) {
        resetHardware();
        Esp32S3Uart uart;
        assert(!uart.begin(pins) && !uart.ready());
        assert(hardware.dePin == -1 && hardware.txPin == -1 && hardware.rxPin == -1);
        assert(hardware.writes == 0 && hardware.config.baud_rate == 0);
    }
}
void testAlternatePinsAndPolarity() {
    // An unrelated application supplies its own pins and an active-low driver.
    const Esp32S3Uart::Pins pins = {4, 5, 6, false};
    resetHardware();
    hardware.txCharacterUs = 87;
    hardware.transmitLevel = 0;
    Esp32S3Uart uart;
    assert(uart.begin(pins));
    assert(hardware.txPin == 4 && hardware.rxPin == 5 && hardware.dePin == 6);
    assert(hardware.de == 1); // Receive is high for this transceiver.
    Rtu::Port port = uart.port();
    assert(port.setTransmit(port.context, true) && hardware.de == 0);
    assert(port.setTransmit(port.context, false) && hardware.de == 1);
    assert(uart.startCapture());
    const uint8_t bytes[] = {1, 3, 0, 0};
    assert(port.setTransmit(port.context, true) && hardware.de == 0);
    assert(!port.write(port.context, bytes, sizeof(bytes)).error);
    advanceHardware(hardware.time + 1000);
    assert(hardware.dePin == 6 && hardware.de == 1 && hardware.writes == 1);
    Rtu::TxObservation tx;
    assert(port.txState(port.context, hardware.time, tx) == Rtu::TxState::IDLE);
    assert(tx.released);
}
void testReceiveCapture() {
    resetHardware();
    Esp32S3Uart uart;
    assert(uart.begin(PINS));
    Rtu::Port port = uart.port();
    Rtu::RxByte byte;
    byte.value = 0xAA;
    uint64_t through = 77;
    sample(uart, 1100);
    assert(read(port, 1100, byte, through) == Rtu::ReadState::EMPTY && through == 1100);
    fakeUart.fsm_status.st_urx_out = 1;
    fakeUart.status.rxd = 0;
    sample(uart, 1150);
    assert(read(port, 1150, byte, through) == Rtu::ReadState::PENDING);
    assert(through == 1100 && byte.value == 0xAA);
    hardware.rx.push_back(0x5A);
    fakeUart.fsm_status.st_urx_out = 0;
    fakeUart.status.rxd = 1;
    sample(uart, 1200);
    assert(read(port, 1200, byte, through) == Rtu::ReadState::PENDING);
    assert(byte.value == 0xAA); // Stop-bit upper bound is not yet in the supplied past.
    assert(read(port, 1213, byte, through) == Rtu::ReadState::BYTE);
    assert(byte.value == 0x5A && byte.startUs >= 1100 && byte.endUs == 1213);
    assert(byte.uncertaintyUs > 0 && byte.uncertaintyUs == uart.maxRxWidthUs());
    assert(byte.endUs - byte.uncertaintyUs <= 1200);
    assert(read(port, 1213, byte, through) == Rtu::ReadState::PENDING);
    sample(uart, 1220);
    assert(read(port, 1220, byte, through) == Rtu::ReadState::EMPTY && through == 1220);
}
void testIdleSnapshotRace() {
    resetHardware();
    Esp32S3Uart uart;
    assert(uart.begin(PINS));
    Rtu::Port port = uart.port();
    Rtu::RxByte byte;
    uint64_t through = 0;
    sample(uart, 1100);
    assert(read(port, 1100, byte, through) == Rtu::ReadState::EMPTY && through == 1100);
    fakeUart.fsm_status.st_urx_out = 1;
    hardware.publishOnCheck = hardware.rxChecks + 2;
    sample(uart, 1200); // Byte finishes between the final FIFO and RX idle observations.
    assert(read(port, 1200, byte, through) == Rtu::ReadState::PENDING && through == 1100);
    sample(uart, 1210);
    assert(read(port, 1230, byte, through) == Rtu::ReadState::BYTE);
    assert(byte.value == 0x5A && byte.startUs < 1200); // Do not clamp to a false idle watermark.
    sample(uart, 1240);
    assert(read(port, 1240, byte, through) == Rtu::ReadState::EMPTY);
    assert(uart.clear());
    assert(read(port, 1250, byte, through) == Rtu::ReadState::PENDING); // Fresh evidence required.
}
void testSlowSnapshotFails() {
    resetHardware();
    Esp32S3Uart uart;
    assert(uart.begin(PINS));
    hardware.clockStep = 50;
    sample(uart, 1200);
    assert(uart.needsRecovery() && uart.captureFaults() == 1);
}
void testCaptureFaultsAndRecovery() {
    resetHardware();
    Esp32S3Uart uart;
    assert(uart.begin(PINS));
    Rtu::Port port = uart.port();
    Rtu::RxByte byte;
    uint64_t through = 0;
    hardware.rx.push_back(1); hardware.rx.push_back(3);
    sample(uart, 1200);
    assert(read(port, 1300, byte, through) == Rtu::ReadState::ERROR);
    assert(uart.needsRecovery()); // A fault during idle must be visible before a new probe.
    assert(uart.captureFaults() == 1 && uart.rxErrors() == 0);
    sample(uart, 1300);
    assert(uart.captureFaults() == 1); // Sticky failure counts once.
    assert(!port.setTransmit(port.context, true) && hardware.de == 0);
    const uint8_t frame[] = {1, 3, 0, 0};
    assert(port.write(port.context, frame, sizeof(frame)).error && hardware.writes == 0);
    assert(port.setTransmit(port.context, false));
    fakeUart.fsm_status.st_urx_out = 1;
    assert(!uart.clear()); // Do not flush an in-progress character as if the bus were idle.
    fakeUart.fsm_status.st_urx_out = 0;
    assert(uart.clear());
    sample(uart, 1400);
    assert(read(port, 1400, byte, through) == Rtu::ReadState::EMPTY);
    for (unsigned bit : {2U, 3U, 4U, 7U}) {
        fakeUart.int_raw.val = 1U << bit;
        sample(uart, hardware.time + 100);
        assert(read(port, hardware.time, byte, through) == Rtu::ReadState::ERROR);
        assert(uart.clear());
    }
    assert(uart.captureFaults() == 5 && uart.rxErrors() == 4);
    assert(uart.maxPollGapUs() > 0);
    uart.resetStats();
    assert(!uart.captureFaults() && !uart.rxErrors() && !uart.maxPollGapUs());
}
void testPendingCapacity() {
    resetHardware();
    Esp32S3Uart uart;
    assert(uart.begin(PINS));
    for (unsigned i = 0; i < Esp32S3Uart::CAPTURE_CAPACITY + 1; ++i) {
        hardware.rx.push_back(static_cast<uint8_t>(i));
        sample(uart, 1200 + i * 100);
    }
    Rtu::Port port = uart.port();
    Rtu::RxByte byte;
    uint64_t through = 0;
    assert(read(port, hardware.time, byte, through) == Rtu::ReadState::ERROR);
    assert(uart.captureFaults() == 1);
}
void testCaptureTimerLifecycle() {
    resetHardware();
    unsigned startupCalls = 0;
    {
        Esp32S3Uart uart;
        assert(!uart.startCapture());
        assert(uart.begin(PINS));
        assert(!uart.startCapture(0) && !uart.startCapture(1000));
        assert(uart.startCapture() && uart.stats().timer);
        assert(!uart.startCapture());
        startupCalls = hardware.timerCalls;
        advanceHardware(hardware.time + 1000);
        assert(hardware.timerCallbacks > 40 && uart.stats().samples > 40);
        assert(uart.stopCapture() && !hardware.timerRunning && !uart.stats().timer);
        const unsigned callbacks = hardware.timerCallbacks;
        advanceHardware(hardware.time + 1000);
        assert(hardware.timerCallbacks == callbacks && hardware.writes == 0);
        assert(uart.startCapture());
    }
    assert(!hardware.timerRunning && !hardware.timerCreated); // Destructor owns its timer.
    for (unsigned call = 1; call <= startupCalls; ++call) {
        resetHardware();
        Esp32S3Uart uart;
        assert(uart.begin(PINS));
        hardware.timerFailCalls = {call};
        assert(!uart.startCapture());
        assert(!hardware.timerCreated && !hardware.timerRunning && !uart.stats().timer);
        hardware.timerFailCalls.clear();
        assert(uart.startCapture()); // A failed start does not leak the timer allocation.
    }
}
void testTimerIdleStarvationIsSticky() {
    resetHardware();
    Esp32S3Uart uart;
    assert(uart.begin(PINS));
    advanceHardware(hardware.time + 5000); // Setup before capture is outside its epoch.
    assert(uart.startCapture());
    advanceHardware(hardware.time + 1000);
    auto stats = uart.stats();
    assert(!stats.failed && !stats.sampleGapExceeded && stats.sampleGapLimitUs == 85);
    assert(stats.timerCallbacks == hardware.timerCallbacks && stats.timerCallbacks > 40);
    const uint64_t timerSamples = stats.timerCallbacks;
    uart.sample();
    stats = uart.stats();
    assert(stats.samples > stats.timerCallbacks && stats.timerCallbacks >= timerSamples);
    fakeEnterCritical();
    advanceHardware(hardware.time + 1000); // Empty FIFO cannot prove uninterrupted capture.
    fakeExitCritical();
    stats = uart.stats();
    assert(stats.failed && stats.sampleGapExceeded && stats.faults == 1);
    assert(stats.maxGapUs >= stats.sampleGapLimitUs && hardware.rx.empty());
    Rtu::Port port = uart.port();
    const uint8_t bytes[] = {1, 3, 0, 0};
    assert(!port.setTransmit(port.context, true));
    assert(port.write(port.context, bytes, sizeof(bytes)).error && hardware.writes == 0);
    uart.resetStats();
    stats = uart.stats();
    assert(stats.failed && stats.sampleGapExceeded && stats.faults == 0);
    assert(!port.setTransmit(port.context, true) && hardware.writes == 0);
    assert(uart.clear()); // Only explicit host recovery can clear the restriction failure.
    stats = uart.stats();
    assert(!stats.failed && !stats.sampleGapExceeded);
    advanceHardware(hardware.time + 100);
    assert(!uart.needsRecovery());
    assert(port.setTransmit(port.context, true));
    assert(port.setTransmit(port.context, false));
    assert(uart.stopCapture());
    advanceHardware(hardware.time + 5000);
    assert(uart.startCapture()); // Stopped intervals cannot poison the next capture epoch.
    advanceHardware(hardware.time + 100);
    assert(!uart.needsRecovery() && !uart.stats().sampleGapExceeded);
}
void testPollIdleHasNoTimerGapRestriction() {
    resetHardware();
    Esp32S3Uart uart;
    assert(uart.begin(PINS));
    advanceHardware(hardware.time + 5000);
    uart.sample();
    const auto stats = uart.stats();
    assert(!stats.timer && !stats.failed && !stats.sampleGapExceeded);
    assert(stats.timerCallbacks == 0 && stats.maxGapUs > stats.sampleGapLimitUs);
    Rtu::Port port = uart.port();
    assert(port.setTransmit(port.context, true));
    assert(port.setTransmit(port.context, false));
}
void testTimerSamplingGapBoundary() {
    for (const uint32_t gap : {84U, 85U}) {
        resetHardware();
        // Hold clock reads at the current wire time so the requested gap is
        // exactly the callback's before-to-before interval, not gap + fake work.
        hardware.clockStep = 0;
        Esp32S3Uart uart;
        assert(uart.begin(PINS) && uart.startCapture());
        const uint64_t epoch = hardware.time;
        fakeEnterCritical();
        advanceHardware(epoch + gap);
        fakeExitCritical(); // The first overdue callback observes this exact gap.
        const auto stats = uart.stats();
        const bool rejected = gap == 85;
        assert(stats.timerCallbacks == 1 && stats.samples == 1);
        assert(stats.maxGapUs == gap && stats.sampleGapLimitUs == 85);
        assert(stats.failed == rejected && stats.sampleGapExceeded == rejected);
        assert(stats.faults == (rejected ? 1U : 0U));
        Rtu::Port port = uart.port();
        assert(port.setTransmit(port.context, true) == !rejected);
        assert(port.setTransmit(port.context, false));
        assert(hardware.writes == 0);
    }
}
void testCaptureTimerStopsOnlyWhenIdle() {
    resetHardware();
    hardware.txCharacterUs = 87;
    Esp32S3Uart uart;
    assert(uart.begin(PINS) && uart.startCapture());
    Rtu::Port port = uart.port();
    const uint8_t frame[] = {1, 3, 0, 0};
    assert(port.setTransmit(port.context, true));
    assert(!port.write(port.context, frame, sizeof(frame)).error);
    assert(!uart.stopCapture() && hardware.timerRunning && hardware.de == 1);
    advanceHardware(hardware.time + 1000);
    assert(hardware.de == 0); // Background capture releases after physical TX and hold.
    hardware.timerFailCalls = {hardware.timerCalls + 1};
    assert(!uart.stopCapture() && hardware.timerRunning && uart.stats().timer);
    hardware.timerFailCalls.clear();
    assert(uart.stopCapture());
}
void testPartialTimerCleanupCanResume() {
    for (unsigned stage : {2U, 3U}) {
        resetHardware();
        Esp32S3Uart uart;
        assert(uart.begin(PINS) && uart.startCapture());
        advanceHardware(hardware.time + 1000);
        hardware.timerFailCalls = {hardware.timerCalls + stage};
        assert(!uart.stopCapture());
        assert(!hardware.timerRunning && hardware.timerCreated);
        assert(hardware.timerEnabled == (stage == 2));
        assert(!uart.stats().timer && uart.needsRecovery());
        hardware.timerFailCalls.clear();
        assert(uart.stopCapture());
        assert(!hardware.timerCreated && !hardware.timerEnabled);
        assert(uart.clear() && uart.startCapture());
    }
}
void testStartupCleanupFailureKeepsOwnership() {
    resetHardware();
    Esp32S3Uart uart;
    assert(uart.begin(PINS));
    // Current startup: create, callbacks, alarm, enable, start. Fail start and
    // the subsequent disable, then require staged cleanup of the retained timer.
    hardware.timerFailCalls = {5, 6};
    assert(!uart.startCapture());
    assert(hardware.timerCreated && hardware.timerEnabled && !hardware.timerRunning);
    assert(uart.needsRecovery() && !uart.stats().timer);
    assert(!uart.startCapture());
    hardware.timerFailCalls.clear();
    assert(uart.stopCapture());
    assert(!hardware.timerCreated && uart.clear() && uart.startCapture());
}
void testTimerStopChecksCurrentReceiveState() {
    resetHardware();
    Esp32S3Uart uart;
    assert(uart.begin(PINS) && uart.startCapture());
    advanceHardware(hardware.time + 1000);
    fakeUart.fsm_status.st_urx_out = 1;
    assert(!uart.stopCapture() && hardware.timerRunning);
    fakeUart.fsm_status.st_urx_out = 0;
    hardware.rx.push_back(0x55);
    assert(!uart.stopCapture() && hardware.timerRunning);
    assert(uart.clear());
    advanceHardware(hardware.time + 100);
    assert(uart.stopCapture());
}
void testUnrepresentableTimingFails() {
    {
        resetHardware();
        Esp32S3Uart uart;
        assert(uart.begin(PINS));
        sample(uart, 1100);
        hardware.rx.push_back(0x55);
        sample(uart, uint64_t(UINT32_MAX) + 2000);
        Rtu::Port port = uart.port();
        Rtu::RxByte byte;
        uint64_t through = 0;
        assert(uart.needsRecovery());
        assert(read(port, hardware.time, byte, through) == Rtu::ReadState::ERROR);
    }
    {
        resetHardware();
        Esp32S3Uart uart;
        assert(uart.begin(PINS));
        Rtu::Port port = uart.port();
        const uint8_t bytes[] = {1, 3, 0, 0};
        assert(port.setTransmit(port.context, true));
        assert(!port.write(port.context, bytes, sizeof(bytes)).error);
        fakeUart.status.txfifo_cnt = 0; fakeUart.fsm_status.st_utx_out = 0;
        sample(uart, uint64_t(UINT32_MAX) + 2000);
        assert(uart.needsRecovery());
        assert(port.setTransmit(port.context, false)); // Fault still permits physical cleanup.
    }
}
void testTimerTxObservationWaitsForRelease() {
    resetHardware();
    hardware.txCharacterUs = 87;
    Esp32S3Uart uart;
    assert(uart.begin(PINS) && uart.startCapture(20, 1000));
    Rtu::Port port = uart.port();
    const uint8_t bytes[] = {1, 3, 0, 0};
    assert(port.setTransmit(port.context, true));
    assert(!port.write(port.context, bytes, sizeof(bytes)).error);
    advanceHardware(hardware.time + 500);
    assert(fakeUart.fsm_status.st_utx_out == 0 && hardware.de == 1);
    Rtu::TxObservation tx;
    assert(port.txState(port.context, hardware.time, tx) == Rtu::TxState::BUSY);
    advanceHardware(hardware.time + 1000);
    assert(port.txState(port.context, hardware.time, tx) == Rtu::TxState::IDLE);
    assert(tx.released && hardware.de == 0);
    assert(tx.releasedUs - tx.releaseUncertaintyUs >= tx.endedUs + 1000);
    const uint64_t released = tx.releasedUs;
    assert(port.txState(port.context, released - 1, tx) == Rtu::TxState::BUSY);
    // An earlier release belongs only to that transfer. Starting another request
    // cannot inherit it and release DE before the new stop bit and hold interval.
    assert(port.setTransmit(port.context, true));
    assert(!port.write(port.context, bytes, sizeof(bytes)).error);
    advanceHardware(hardware.time + 50);
    assert(port.txState(port.context, hardware.time, tx) == Rtu::TxState::BUSY && hardware.de == 1);
    advanceHardware(hardware.time + 1500);
    assert(port.txState(port.context, hardware.time, tx) == Rtu::TxState::IDLE);
    assert(tx.releasedUs > released && tx.released && hardware.de == 0 && hardware.writes == 2);
}
void testRunnerProbe() {
    resetHardware();
    Esp32S3Uart uart;
    assert(uart.begin(PINS));
    uint8_t tx[32] = {}, rx[64] = {};
    Rtu::Storage storage;
    storage.tx = tx; storage.txCapacity = sizeof(tx);
    storage.rx = rx; storage.rxCapacity = sizeof(rx);
    Rtu::Timing timing;
    assert(Rtu::setRtuTiming(115200, 10, timing));
    timing.setupUs = 20; timing.holdUs = 0;
    timing.busTimeoutUs = 10000; timing.txTimeoutUs = 5000; timing.captureTimeoutUs = 5000;
    Rtu::Runner runner(uart.port(), storage, timing);
    const uint8_t request[] = {1, 3, 0, 0, 0, 1, 0x84, 0x0A};
    const uint8_t reply[] = {1, 3, 2, 0, 0x3C, 0xB8, 0x55};
    Rtu::Request command;
    command.bytes = request; command.length = sizeof(request);
    command.replyLength = sizeof(reply); command.responseTimeoutUs = 10000;
    assert(runner.start(command, hardware.time) == Rtu::Admission::STARTED);
    unsigned sent = 0;
    for (unsigned iteration = 0; iteration < 2000 && runner.busy(); ++iteration) {
        hardware.time += 10;
        if (hardware.writes) {
            const uint64_t end = hardware.writeStarted + 696;
            if (hardware.time >= end - 80) fakeUart.status.txfifo_cnt = 0;
            if (hardware.time >= end) fakeUart.fsm_status.st_utx_out = 0;
            const uint64_t first = end + 2000;
            if (sent < sizeof(reply)) {
                if (hardware.time >= first + sent * 87 + 87) {
                    hardware.rx.push_back(reply[sent++]);
                }
                const bool receiving = sent < sizeof(reply) && hardware.time >= first + sent * 87;
                fakeUart.fsm_status.st_urx_out = receiving ? 1 : 0;
            } else fakeUart.fsm_status.st_urx_out = 0;
        }
        runner.poll(uart.sample());
    }
    assert(runner.result().reason == Rtu::Reason::FRAME);
    assert(runner.result().rxLength == sizeof(reply) && runner.result().txComplete);
    assert(std::vector<uint8_t>(rx, rx + sizeof(reply)) == std::vector<uint8_t>(reply, reply + sizeof(reply)));
    assert(hardware.writes == 1 && hardware.de == 0 && uart.captureFaults() == 0);
}
} // namespace

int main() {
    testInitialization(); testPhysicalTx(); testReceiveCapture();
    testPinValidation(); testAlternatePinsAndPolarity();
    testIdleSnapshotRace(); testSlowSnapshotFails();
    testCaptureFaultsAndRecovery(); testPendingCapacity();
    testCaptureTimerLifecycle(); testCaptureTimerStopsOnlyWhenIdle();
    testTimerIdleStarvationIsSticky(); testPollIdleHasNoTimerGapRestriction();
    testTimerSamplingGapBoundary();
    testPartialTimerCleanupCanResume(); testStartupCleanupFailureKeepsOwnership();
    testTimerStopChecksCurrentReceiveState(); testUnrepresentableTimingFails();
    testTimerTxObservationWaitsForRelease();
    testRunnerProbe();
    return 0;
}
