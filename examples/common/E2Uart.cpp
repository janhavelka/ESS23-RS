// SPDX-License-Identifier: MIT
#include "E2Uart.h"
#include "BoardPins.h"
#include <cstring>
#include <driver/uart.h>
#include <driver/gpio.h>
#include <esp_timer.h>
#include <esp_rom_sys.h>
#include <freertos/FreeRTOS.h>
#include <hal/uart_ll.h>

#if !CONFIG_IDF_TARGET_ESP32S3
#error E2Uart supports ESP32-S3 builds only
#endif

namespace MotorControlRSExample {
namespace {
uart_dev_t* const hw = UART_LL_GET_HW(UART_NUM_2);
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
uint64_t now() { return static_cast<uint64_t>(esp_timer_get_time()); }
uint32_t bounded(uint64_t v) { return v > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(v); }
void increment(uint32_t& v) { if (v != UINT32_MAX) ++v; }
uint32_t maximum(uint32_t a, uint32_t b) { return a > b ? a : b; }
}

bool E2Uart::begin(uint32_t baud) noexcept {
    if (ready_ || uart_is_driver_installed(UART_NUM_2) || baud != 115200) return false;
    // Initial slice intentionally fixes the documented default tuple: 115200 8N1.
    if (gpio_set_level(static_cast<gpio_num_t>(Board::kRs485DeRePin), 0) != ESP_OK) return false;
    if (gpio_set_direction(static_cast<gpio_num_t>(Board::kRs485DeRePin), GPIO_MODE_OUTPUT) != ESP_OK)
        return false;
    uart_config_t config = {};
    config.baud_rate = static_cast<int>(baud);
    config.data_bits = UART_DATA_8_BITS;
    config.parity = UART_PARITY_DISABLE;
    config.stop_bits = UART_STOP_BITS_1;
    config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    config.source_clk = UART_SCLK_XTAL;
    // Parameter/pin setup is independent of driver installation. No ISR/ring buffer
    // is installed: raw FIFO/state/error registers remain under this one owner.
    if (uart_param_config(UART_NUM_2, &config) != ESP_OK ||
        uart_set_pin(UART_NUM_2, Board::kRs485TxPin, Board::kRs485RxPin,
                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) return false;
    hw->int_ena.val = 0;
    uart_ll_set_tx_idle_num(hw, 0);
    uart_ll_txfifo_rst(hw);
    uart_ll_rxfifo_rst(hw);
    hw->int_clr.val = UINT32_MAX;
    // 2% baud tolerance and one bit + 2 us publish/sampling guard are engineering
    // assumptions, exposed as unqualified in the CLI until external trace checks.
    charMin_ = 9800000U / baud;
    charMax_ = (10200000U + baud - 1) / baud;
    stopGuard_ = (1000000U + baud - 1) / baud + 2;
    sampled_ = emptySince_ = idleThrough_ = now();
    ready_ = true;
    return true;
}

void E2Uart::fault(bool uartError) noexcept {
    if (!failed_) { increment(captureFaults_); if (uartError) increment(rxErrors_); }
    failed_ = true;
}

uint64_t E2Uart::sample() noexcept {
    if (!ready_) return now();
    uint8_t value = 0;
    portENTER_CRITICAL(&mux);
    const uint64_t before = now();
    const uint32_t errors = hw->int_raw.val & ((1U << 2) | (1U << 3) | (1U << 4) | (1U << 7));
    const bool idleRxBefore = hw->fsm_status.st_urx_out == 0 && hw->status.rxd;
    const unsigned available = uart_ll_get_rxfifo_len(hw);
    const bool idleTx = uart_ll_is_tx_idle(hw); // FIFO empty AND TX state machine idle.
    if (available == 1) uart_ll_read_rxfifo(hw, &value, 1);
    const uint64_t emptyCheck = now();
    const bool empty = uart_ll_get_rxfifo_len(hw) == 0;
    const bool idleRx = hw->fsm_status.st_urx_out == 0 && hw->status.rxd;
    const uint64_t after = now();
    portEXIT_CRITICAL(&mux);
    maxPollGap_ = maximum(maxPollGap_, bounded(before - sampled_));
    sampled_ = before;
    if (errors) fault(true);
    if (after - before >= charMin_) fault(false); // A whole byte could cross this snapshot unseen.
    if (available > 1) fault(false); // Batch contains unknowable inter-byte gaps.
    if (available == 1 && !failed_) {
        if (count_ == 4 || emptySince_ > before) fault(false);
        else {
            Rtu::RxByte& byte = pending_[(head_ + count_) % 4];
            const uint64_t lowEnd = emptySince_;
            const uint64_t highEnd = after + stopGuard_;
            // Same uncertainty width for both intervals; widen the end interval
            // by the character duration tolerance instead of inventing precision.
            byte.startUs = lowEnd > charMax_ ? lowEnd - charMax_ : 0;
            if (byte.startUs < idleThrough_) byte.startUs = idleThrough_;
            byte.endUs = highEnd;
            const uint64_t highStart = highEnd > charMin_ ? highEnd - charMin_ : 0;
            byte.uncertaintyUs = maximum(bounded(highEnd - lowEnd),
                                        bounded(highStart > byte.startUs ? highStart - byte.startUs : 0));
            byte.value = value;
            if (!transmitting_ && txEnd_ && byte.startUs < directionAt_) fault(false);
            maxRxWidth_ = maximum(maxRxWidth_, byte.uncertaintyUs);
            ++count_;
        }
    }
    if (empty) emptySince_ = emptyCheck;
    // A byte may finish between the FIFO and state-machine reads. Require idle
    // on both sides and no byte consumed in this sample before advancing silence.
    // RX synchronizer/FIFO publication latency still needs external qualification.
    rxIdle_ = idleRxBefore && available == 0 && empty && idleRx;
    if (rxIdle_) idleThrough_ = before;
    txIdle_ = idleTx;
    if (txPending_) {
        if (!idleTx) txBusyAt_ = before;
        else {
            txEnd_ = after;
            txWidth_ = bounded(after - txBusyAt_);
            txPending_ = false;
        }
    }
    return before;
}

bool E2Uart::direction(void* ctx, bool enabled) {
    E2Uart& self = *static_cast<E2Uart*>(ctx);
    if (!self.ready_ || (!enabled && !uart_ll_is_tx_idle(hw))) return false;
    if (gpio_set_level(static_cast<gpio_num_t>(Board::kRs485DeRePin), enabled ? 1 : 0) != ESP_OK)
        return false;
    self.transmitting_ = enabled;
    self.directionAt_ = now();
    // A finite board setup guard starts at the actual GPIO write, independently
    // of the runner's earlier snapshot time. This is not an unbounded drain wait.
    if (enabled) esp_rom_delay_us(20);
    return true;
}

Rtu::WriteResult E2Uart::write(void* ctx, const uint8_t* bytes, std::size_t length) {
    E2Uart& self = *static_cast<E2Uart*>(ctx);
    if (!self.ready_ || self.failed_ || !self.transmitting_ || !bytes || !length || length > 64)
        return Rtu::WriteResult(0, true);
    uint8_t local[64]; // Internal stack: no PSRAM reads in the short FIFO fill.
    std::memcpy(local, bytes, length);
    portENTER_CRITICAL(&mux);
    const uint64_t before = now();
    if (!uart_ll_is_tx_idle(hw) || uart_ll_get_txfifo_len(hw) < length) {
        portEXIT_CRITICAL(&mux);
        return Rtu::WriteResult(0, true);
    }
    uart_ll_write_txfifo(hw, local, length);
    const uint64_t after = now();
    portEXIT_CRITICAL(&mux);
    self.txBusyAt_ = before;
    self.txPending_ = true;
    self.txIdle_ = false;
    self.txEnd_ = 0;
    self.txWidth_ = 0;
    return Rtu::WriteResult(length, after - before >= self.charMin_);
}

Rtu::TxState E2Uart::txState(void* ctx, uint64_t at, uint64_t& ended) {
    E2Uart& self = *static_cast<E2Uart*>(ctx);
    if (!self.ready_) return Rtu::TxState::ERROR;
    if (!self.txIdle_ || self.txPending_ || self.txEnd_ > at) return Rtu::TxState::BUSY;
    ended = self.txEnd_;
    return Rtu::TxState::IDLE;
}
uint32_t E2Uart::txWidth(void* ctx) { return static_cast<E2Uart*>(ctx)->txWidth_; }

Rtu::ReadState E2Uart::read(void* ctx, uint64_t at, Rtu::RxByte& byte, uint64_t& through) {
    E2Uart& self = *static_cast<E2Uart*>(ctx);
    if (!self.ready_ || self.failed_) return Rtu::ReadState::ERROR;
    if (self.count_) {
        if (self.pending_[self.head_].endUs > at) return Rtu::ReadState::PENDING;
        byte = self.pending_[self.head_];
        self.head_ = (self.head_ + 1) % 4;
        --self.count_;
        return Rtu::ReadState::BYTE;
    }
    if (!self.rxIdle_) return Rtu::ReadState::PENDING;
    through = self.idleThrough_;
    return Rtu::ReadState::EMPTY;
}

bool E2Uart::clear() noexcept {
    if (!ready_ || transmitting_ || !uart_ll_is_tx_idle(hw) ||
        hw->fsm_status.st_urx_out != 0 || !hw->status.rxd) return false;
    uart_ll_rxfifo_rst(hw);
    hw->int_clr.val = UINT32_MAX;
    head_ = count_ = 0;
    failed_ = false;
    rxIdle_ = false; // Recovery is not fresh silence evidence; sample again first.
    emptySince_ = idleThrough_ = sampled_ = now();
    return true;
}
void E2Uart::resetStats() noexcept {
    captureFaults_ = rxErrors_ = maxPollGap_ = maxRxWidth_ = 0;
}
Rtu::Port E2Uart::port() noexcept {
    Rtu::Port value;
    value.context = this;
    value.setTransmit = direction;
    value.write = write;
    value.txState = txState;
    value.read = read;
    value.txUncertaintyUs = txWidth;
    return value;
}
} // namespace MotorControlRSExample
