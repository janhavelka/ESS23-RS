// SPDX-License-Identifier: MIT
#include "Hardware.h"
#include <cassert>

uart_dev_t fakeUart;
Hardware hardware;
void resetHardware() { hardware = Hardware(); fakeUart = uart_dev_t(); }

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
unsigned uart_ll_get_rxfifo_len(uart_dev_t*) {
    const unsigned count = static_cast<unsigned>(hardware.rx.size());
    if (++hardware.rxChecks == hardware.publishOnCheck) {
        // Finish a character immediately after the FIFO count was sampled.
        hardware.rx.push_back(0x5A);
        fakeUart.fsm_status.st_urx_out = 0;
        fakeUart.status.rxd = 1;
    }
    return count;
}
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
