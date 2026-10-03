// SPDX-License-Identifier: MIT
#include "../examples/common/E2Uart.h"
#include "fakes/e2_uart/Hardware.h"
#include <cassert>
#include <deque>
#include <vector>

using MotorControlRSExample::E2Uart;
namespace Rtu = MotorControlRSExample::Rtu;
namespace {
uint64_t sample(E2Uart& uart, uint64_t at) {
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
    E2Uart uart;
    Rtu::Port port = uart.port();
    uint64_t through = 777;
    Rtu::TxObservation tx;
    Rtu::RxByte byte;
    assert(port.txState(port.context, 1000, tx) == Rtu::TxState::ERROR);
    assert(read(port, 1000, byte, through) == Rtu::ReadState::ERROR);
    assert(!uart.clear());
    assert(!uart.begin(9600));
    hardware.driverInstalled = true;
    assert(!uart.begin());
    assert(hardware.de == -1); // Refuse another driver's peripheral before GPIO changes.
    hardware.driverInstalled = false;
    assert(uart.begin());
    assert(uart.ready() && hardware.de == 0);
    assert(hardware.txPin == 47 && hardware.rxPin == 48);
    assert(hardware.config.baud_rate == 115200 && hardware.config.data_bits == UART_DATA_8_BITS);
    assert(hardware.config.parity == UART_PARITY_DISABLE && hardware.config.stop_bits == UART_STOP_BITS_1);
    assert(!uart.begin());
    for (unsigned failure = 0; failure < 4; ++failure) {
        resetHardware();
        E2Uart failing;
        if (failure == 0) hardware.levelResult = -1;
        if (failure == 1) hardware.directionResult = -1;
        if (failure == 2) hardware.configResult = -1;
        if (failure == 3) hardware.pinResult = -1;
        assert(!failing.begin() && !failing.ready());
    }
}
void testPhysicalTx() {
    resetHardware();
    E2Uart uart;
    assert(uart.begin());
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
void testReceiveCapture() {
    resetHardware();
    E2Uart uart;
    assert(uart.begin());
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
    E2Uart uart;
    assert(uart.begin());
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
    E2Uart uart;
    assert(uart.begin());
    hardware.clockStep = 50;
    sample(uart, 1200);
    assert(uart.needsRecovery() && uart.captureFaults() == 1);
}
void testCaptureFaultsAndRecovery() {
    resetHardware();
    E2Uart uart;
    assert(uart.begin());
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
    E2Uart uart;
    assert(uart.begin());
    for (unsigned i = 0; i < E2Uart::CAPTURE_CAPACITY + 1; ++i) {
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
        E2Uart uart;
        assert(!uart.startCapture());
        assert(uart.begin());
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
        E2Uart uart;
        assert(uart.begin());
        hardware.timerFailCalls = {call};
        assert(!uart.startCapture());
        assert(!hardware.timerCreated && !hardware.timerRunning && !uart.stats().timer);
        hardware.timerFailCalls.clear();
        assert(uart.startCapture()); // A failed start does not leak the timer allocation.
    }
}
void testCaptureTimerStopsOnlyWhenIdle() {
    resetHardware();
    hardware.txCharacterUs = 87;
    E2Uart uart;
    assert(uart.begin() && uart.startCapture());
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
        E2Uart uart;
        assert(uart.begin() && uart.startCapture());
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
    E2Uart uart;
    assert(uart.begin());
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
    E2Uart uart;
    assert(uart.begin() && uart.startCapture());
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
        E2Uart uart;
        assert(uart.begin());
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
        E2Uart uart;
        assert(uart.begin());
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
    E2Uart uart;
    assert(uart.begin() && uart.startCapture(20, 1000));
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
    E2Uart uart;
    assert(uart.begin());
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
    testIdleSnapshotRace(); testSlowSnapshotFails();
    testCaptureFaultsAndRecovery(); testPendingCapacity();
    testCaptureTimerLifecycle(); testCaptureTimerStopsOnlyWhenIdle();
    testPartialTimerCleanupCanResume(); testStartupCleanupFailureKeepsOwnership();
    testTimerStopChecksCurrentReceiveState(); testUnrepresentableTimingFails();
    testTimerTxObservationWaitsForRelease();
    testRunnerProbe();
    return 0;
}
