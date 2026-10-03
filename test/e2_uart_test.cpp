// SPDX-License-Identifier: MIT
#include "../examples/common/E2Uart.h"
#include "fakes/e2_uart/FakeEsp.h"
#include <cassert>
#include <deque>
#include <vector>

using MotorControlRSExample::E2Uart;
namespace Rtu = MotorControlRSExample::Rtu;
uart_dev_t fakeUart;
namespace {
struct Hardware {
    uint64_t time = 1000;
    uint64_t writeStarted = 0;
    unsigned clockStep = 1, criticalDepth = 0, writes = 0, rxResets = 0;
    bool driverInstalled = false;
    int levelResult = ESP_OK, directionResult = ESP_OK, configResult = ESP_OK;
    int pinResult = ESP_OK, de = -1, txPin = -1, rxPin = -1;
    uart_config_t config = {};
    std::deque<uint8_t> rx;
    std::vector<uint8_t> tx;
} hardware;
void reset() { hardware = Hardware(); fakeUart = uart_dev_t(); }
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
    reset();
    E2Uart uart;
    Rtu::Port port = uart.port();
    uint64_t ended = 777, through = 777;
    Rtu::RxByte byte;
    assert(port.txState(port.context, 1000, ended) == Rtu::TxState::ERROR);
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
        reset();
        E2Uart failing;
        if (failure == 0) hardware.levelResult = -1;
        if (failure == 1) hardware.directionResult = -1;
        if (failure == 2) hardware.configResult = -1;
        if (failure == 3) hardware.pinResult = -1;
        assert(!failing.begin() && !failing.ready());
    }
}
void testPhysicalTx() {
    reset();
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
    uint64_t ended = 999;
    assert(port.txState(port.context, 1100, ended) == Rtu::TxState::BUSY);
    fakeUart.status.txfifo_cnt = 0; // Last byte left FIFO; shifter still sends its stop bit.
    fakeUart.fsm_status.st_utx_out = 1;
    sample(uart, 1100);
    assert(port.txState(port.context, 1100, ended) == Rtu::TxState::BUSY);
    assert(!port.setTransmit(port.context, false) && hardware.de == 1);
    assert(!uart.clear());
    fakeUart.fsm_status.st_utx_out = 0;
    const uint64_t at = sample(uart, 1110);
    assert(port.txState(port.context, at, ended) == Rtu::TxState::BUSY); // Snapshot upper bound is later.
    assert(port.txState(port.context, 1112, ended) == Rtu::TxState::IDLE);
    const uint32_t width = port.txUncertaintyUs(port.context);
    assert(ended == 1112 && ended - width == 1100);
    assert(port.setTransmit(port.context, false) && hardware.de == 0);
    assert(uart.clear());
    assert(hardware.writes == 1); // Recovery never repeats the write.
}
void testReceiveCapture() {
    reset();
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
    assert(read(port, 1213, byte, through) == Rtu::ReadState::EMPTY);
    assert(through <= 1213);
}
void testCaptureFaultsAndRecovery() {
    reset();
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
    assert(port.setTransmit(port.context, true));
    const uint8_t frame[] = {1, 3, 0, 0};
    assert(port.write(port.context, frame, sizeof(frame)).error && hardware.writes == 0);
    assert(!uart.clear());
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
    reset();
    E2Uart uart;
    assert(uart.begin());
    for (unsigned i = 0; i < 5; ++i) {
        hardware.rx.push_back(static_cast<uint8_t>(i));
        sample(uart, 1200 + i * 100);
    }
    Rtu::Port port = uart.port();
    Rtu::RxByte byte;
    uint64_t through = 0;
    assert(read(port, 1800, byte, through) == Rtu::ReadState::ERROR);
    assert(uart.captureFaults() == 1);
}
void testRunnerProbe() {
    reset();
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

FakeClearRegister::Value& FakeClearRegister::Value::operator=(uint32_t bits) {
    fakeUart.int_raw.val &= ~bits;
    return *this;
}
void fakeEnterCritical() { assert(hardware.criticalDepth++ == 0); }
void fakeExitCritical() { assert(hardware.criticalDepth-- == 1); }
bool uart_is_driver_installed(uart_port_t port) { assert(port == UART_NUM_2); return hardware.driverInstalled; }
esp_err_t uart_param_config(uart_port_t port, const uart_config_t* config) {
    assert(port == UART_NUM_2); hardware.config = *config; return hardware.configResult;
}
esp_err_t uart_set_pin(uart_port_t port, int tx, int rx, int rts, int cts) {
    assert(port == UART_NUM_2 && rts == UART_PIN_NO_CHANGE && cts == UART_PIN_NO_CHANGE);
    hardware.txPin = tx; hardware.rxPin = rx; return hardware.pinResult;
}
esp_err_t gpio_set_level(gpio_num_t pin, int value) {
    assert(pin == 21); if (hardware.levelResult == ESP_OK) hardware.de = value; return hardware.levelResult;
}
esp_err_t gpio_set_direction(gpio_num_t pin, int mode) {
    assert(pin == 21 && mode == GPIO_MODE_OUTPUT); return hardware.directionResult;
}
int64_t esp_timer_get_time() { const uint64_t at = hardware.time; hardware.time += hardware.clockStep; return at; }
void esp_rom_delay_us(uint32_t us) { hardware.time += us; }
void uart_ll_set_tx_idle_num(uart_dev_t* hw, unsigned idle) { assert(hw == &fakeUart && idle == 0); }
void uart_ll_txfifo_rst(uart_dev_t* hw) { hw->status.txfifo_cnt = 0; hw->fsm_status.st_utx_out = 0; }
void uart_ll_rxfifo_rst(uart_dev_t*) { hardware.rx.clear(); ++hardware.rxResets; }
unsigned uart_ll_get_rxfifo_len(uart_dev_t*) { return static_cast<unsigned>(hardware.rx.size()); }
unsigned uart_ll_get_txfifo_len(uart_dev_t* hw) { return 128 - hw->status.txfifo_cnt; }
bool uart_ll_is_tx_idle(uart_dev_t* hw) { return hw->status.txfifo_cnt == 0 && hw->fsm_status.st_utx_out == 0; }
void uart_ll_read_rxfifo(uart_dev_t*, uint8_t* bytes, std::size_t count) {
    assert(count <= hardware.rx.size());
    while (count--) { *bytes++ = hardware.rx.front(); hardware.rx.pop_front(); }
}
void uart_ll_write_txfifo(uart_dev_t* hw, const uint8_t* bytes, std::size_t count) {
    assert(hardware.criticalDepth == 1 && hardware.de == 1 && count <= 128 - hw->status.txfifo_cnt);
    ++hardware.writes; hardware.tx.assign(bytes, bytes + count);
    hardware.writeStarted = hardware.time;
    hw->status.txfifo_cnt += static_cast<unsigned>(count); hw->fsm_status.st_utx_out = 1;
}
int main() {
    testInitialization(); testPhysicalTx(); testReceiveCapture();
    testCaptureFaultsAndRecovery(); testPendingCapacity();
    testRunnerProbe();
    return 0;
}
