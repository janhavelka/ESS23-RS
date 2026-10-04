// SPDX-License-Identifier: MIT
#include "Esp32S3Uart.h"
#include <cstdlib>
#include <cstring>
#include <driver/uart.h>
#include <driver/gpio.h>
#include <driver/gptimer.h>
#include <esp_timer.h>
#include <esp_rom_sys.h>
#include <freertos/FreeRTOS.h>
#include <hal/uart_ll.h>
#include <hal/gpio_ll.h>

#if !CONFIG_IDF_TARGET_ESP32S3
#error Esp32S3Uart supports ESP32-S3 builds only
#endif

namespace MotorControlRSExample {
namespace {
uart_dev_t* const hw = UART_LL_GET_HW(UART_NUM_2);
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
uint64_t now() { return static_cast<uint64_t>(esp_timer_get_time()); }
uint32_t bounded(uint64_t v) { return v > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(v); }
void increment(uint32_t& v) { if (v != UINT32_MAX) ++v; }
uint32_t maximum(uint32_t a, uint32_t b) { return a > b ? a : b; }
bool validPin(int pin, bool output) {
    // IDF's validity macros shift by pin; bound it before evaluating either.
    return pin >= 0 && pin < GPIO_NUM_MAX &&
        (output ? GPIO_IS_VALID_OUTPUT_GPIO(pin) : GPIO_IS_VALID_GPIO(pin));
}
// One peripheral, one lock, shared by the owner and the capture interrupt.
struct Guard {
    Guard() { portENTER_CRITICAL_SAFE(&mux); }
    ~Guard() { portEXIT_CRITICAL_SAFE(&mux); }
};
}

Esp32S3Uart::~Esp32S3Uart() {
    // A live callback must never outlast its object. Applications should call
    // stopCapture() while idle and handle its result before ending the lifetime.
    if (!stopTimer()) std::abort(); // Do not silently leak a timer or leave a dangling callback.
}

bool Esp32S3Uart::startCapture(uint32_t periodUs, uint32_t holdUs) noexcept {
    {
        Guard guard;
        if (!ready_ || configurationBlocked_ || failed_ || timer_ || transmitting_ || periodUs < 10 || periodUs > 40 ||
            holdUs > 1000) return false;
    }
    gptimer_config_t config = {};
    config.clk_src = GPTIMER_CLK_SRC_DEFAULT;
    config.direction = GPTIMER_COUNT_UP;
    config.resolution_hz = 1000000;
    config.intr_priority = 2;
    gptimer_handle_t timer = nullptr;
    if (gptimer_new_timer(&config, &timer) != ESP_OK) return false;
    timer_ = timer; // Preserve ownership even when an initialization cleanup fails.
    gptimer_event_callbacks_t callbacks = {};
    callbacks.on_alarm = [](gptimer_handle_t, const gptimer_alarm_event_data_t*, void* context) {
        static_cast<Esp32S3Uart*>(context)->captureSample(true);
        return false;
    };
    gptimer_alarm_config_t alarm = {};
    alarm.alarm_count = periodUs;
    alarm.flags.auto_reload_on_alarm = true;
    if (gptimer_register_event_callbacks(timer, &callbacks, this) != ESP_OK ||
        gptimer_set_alarm_action(timer, &alarm) != ESP_OK || gptimer_enable(timer) != ESP_OK) {
        stopTimer();
        return false;
    }
    timerEnabled_ = true;
    {
        Guard guard;
        holdUs_ = holdUs;
        periodUs_ = periodUs;
        // Timer allocation/setup and an earlier stopped interval do not belong
        // to this sampling epoch. Do not manufacture a fresh RX idle watermark.
        sampled_ = now();
        timerRunning_ = true; // Publish capture policy before its first interrupt.
    }
    if (gptimer_start(timer) == ESP_OK) { captureWanted_ = true; return true; }
    { Guard guard; timerRunning_ = false; }
    stopTimer();
    return false;
}

bool Esp32S3Uart::stopTimer() noexcept {
    if (!timer_) return true;
    auto timer = static_cast<gptimer_handle_t>(timer_);
    if (timerRunning_) {
        if (gptimer_stop(timer) != ESP_OK) { Guard guard; fault(false); return false; }
        Guard guard;
        timerRunning_ = false;
    }
    if (timerEnabled_) {
        if (gptimer_disable(timer) != ESP_OK) { Guard guard; fault(false); return false; }
        timerEnabled_ = false;
    }
    if (gptimer_del_timer(timer) != ESP_OK) { Guard guard; fault(false); return false; }
    timer_ = nullptr;
    return true;
}

bool Esp32S3Uart::stopCapture() noexcept {
    if (!timer_) { captureWanted_ = false; return true; }
    {
        Guard guard;
        if (timerRunning_ && (transmitting_ || txPending_ || count_ || !rxIdle_ ||
            !uart_ll_is_tx_idle(hw) || uart_ll_get_rxfifo_len(hw) != 0 ||
            hw->fsm_status.st_urx_out != 0 || !hw->status.rxd)) return false;
    }
    if (!stopTimer()) return false;
    captureWanted_ = false;
    return true;
}

bool Esp32S3Uart::begin(const Pins& pins, uint32_t baud) noexcept {
    HostTuple tuple; tuple.baud = baud;
    return begin(pins, tuple);
}

bool Esp32S3Uart::begin(const Pins& pins, const HostTuple& tuple) noexcept {
    if (ready_ || !supports(tuple) || !validPin(pins.tx, true) ||
        !validPin(pins.rx, false) || !validPin(pins.de, true) ||
        pins.tx == pins.rx || pins.tx == pins.de || pins.rx == pins.de ||
        uart_is_driver_installed(UART_NUM_2)) return false;
    pins_ = pins;
    const auto de = static_cast<gpio_num_t>(pins_.de);
    if (gpio_set_level(de, pins_.activeHigh ? 0 : 1) != ESP_OK) return false;
    if (gpio_set_direction(de, GPIO_MODE_OUTPUT) != ESP_OK)
        return false;
    if (!configure(tuple) || uart_set_pin(UART_NUM_2, pins_.tx, pins_.rx,
                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) return false;
    newEpoch();
    ready_ = true;
    return true;
}

bool Esp32S3Uart::configure(const HostTuple& tuple) noexcept {
    uart_config_t config = {};
    config.baud_rate = static_cast<int>(tuple.baud);
    config.data_bits = UART_DATA_8_BITS;
    config.parity = UART_PARITY_DISABLE;
    config.stop_bits = UART_STOP_BITS_1;
    switch (tuple.format) {
    case HostFormat::N8_1: break;
    case HostFormat::N8_2: config.stop_bits = UART_STOP_BITS_2; break;
    case HostFormat::E8_1: config.parity = UART_PARITY_EVEN; break;
    case HostFormat::O8_1: config.parity = UART_PARITY_ODD; break;
    }
    config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    config.source_clk = UART_SCLK_XTAL;
    // UART setup does not install its driver or ISR. The optional GPTimer samples
    // raw FIFO/state/error registers under the same exclusive owner.
    uint32_t actual = 0;
    if (uart_param_config(UART_NUM_2, &config) != ESP_OK ||
        uart_get_baudrate(UART_NUM_2, &actual) != ESP_OK ||
        uint64_t(actual) * 100 < uint64_t(tuple.baud) * 98 ||
        uint64_t(actual) * 100 > uint64_t(tuple.baud) * 102) return false;
    tuple_ = tuple;
    actualBaud_ = actual;
    // Bounds cover the selected nominal baud's provisional 2% envelope,
    // including the verified divider; external wire/clock proof remains open.
    HostTiming timing;
    if (!hostTiming(tuple, timing)) return false;
    charMin_ = timing.characterMinUs;
    charMax_ = timing.characterMaxUs;
    stopGuard_ = timing.stopGuardUs;
    return true;
}

void Esp32S3Uart::newEpoch() noexcept {
    Guard guard;
    hw->int_ena.val = 0;
    uart_ll_set_tx_idle_num(hw, 0);
    uart_ll_txfifo_rst(hw);
    uart_ll_rxfifo_rst(hw);
    hw->int_clr.val = UINT32_MAX;
    head_ = count_ = 0;
    txBusyAt_ = txEnd_ = directionAt_ = releasedAt_ = 0;
    txWidth_ = releaseWidth_ = 0;
    txPending_ = transmitting_ = rxIdle_ = false;
    txIdle_ = true;
    failed_ = sampleGapExceeded_ = false;
    sampled_ = emptySince_ = idleThrough_ = now();
}

bool Esp32S3Uart::reconfigure(const HostTuple& tuple) noexcept {
    if (!supports(tuple)) return false; // No GPIO/UART/timer mutation on invalid input.
    bool repair = false;
    {
        Guard guard;
        repair = configurationBlocked_;
        if (!ready_ || transmitting_ || txPending_ || (!repair && count_) ||
            !uart_ll_is_tx_idle(hw) || (!repair && uart_ll_get_rxfifo_len(hw)) ||
            hw->fsm_status.st_urx_out != 0 || !hw->status.rxd ||
            uart_is_driver_installed(UART_NUM_2)) return false;
        // Retain timer intent across partial cleanup/configuration failures.
        captureWanted_ = captureWanted_ || timerRunning_;
        configurationBlocked_ = true;
    }
    if (!stopTimer()) return false;
    {
        Guard guard;
        if ((!repair && uart_ll_get_rxfifo_len(hw)) || hw->fsm_status.st_urx_out != 0 || !hw->status.rxd)
            return false;
    }
    if (!configure(tuple)) return false;
    // Explicit repair discards settled old/unknown-tuple FIFO and captured bytes.
    // They cannot be read while blocked; requiring them empty would make a
    // stopped/failed capture permanently unrepairable. Physical RX must be idle.
    newEpoch();
    { Guard guard; configurationBlocked_ = false; }
    if (captureWanted_ && !startCapture(periodUs_, holdUs_)) {
        Guard guard; configurationBlocked_ = true; fault(false); return false;
    }
    return true;
}

HostTuple Esp32S3Uart::tuple() const noexcept { Guard guard; return tuple_; }
bool Esp32S3Uart::configurationBlocked() const noexcept { Guard guard; return configurationBlocked_; }

void Esp32S3Uart::fault(bool uartError) noexcept {
    if (!failed_) { increment(captureFaults_); if (uartError) increment(rxErrors_); }
    failed_ = true;
}

uint64_t Esp32S3Uart::sample() noexcept {
    return captureSample(false);
}

uint64_t Esp32S3Uart::captureSample(bool timerCallback) noexcept {
    Guard guard;
    if (!ready_) return now();
    uint8_t value = 0;
    const uint64_t before = now();
    if (timerCallback) ++timerCallbacks_;
    const uint32_t errors = hw->int_raw.val & ((1U << 2) | (1U << 3) | (1U << 4) | (1U << 7));
    const bool idleRxBefore = hw->fsm_status.st_urx_out == 0 && hw->status.rxd;
    const unsigned available = uart_ll_get_rxfifo_len(hw);
    const bool idleTx = uart_ll_is_tx_idle(hw); // FIFO empty AND TX state machine idle.
    if (available == 1) uart_ll_read_rxfifo(hw, &value, 1);
    const uint64_t emptyCheck = now();
    const bool empty = uart_ll_get_rxfifo_len(hw) == 0;
    const bool idleRx = hw->fsm_status.st_urx_out == 0 && hw->status.rxd;
    const uint64_t after = now();
    const uint64_t gap = before - sampled_;
    maxPollGap_ = maximum(maxPollGap_, bounded(gap));
    sampled_ = before;
    if (errors) fault(true);
    if (timerRunning_ && gap >= charMin_) {
        // A masked interrupt or cache-off interval is outside the operating
        // envelope even if the FIFO happens to be empty when capture resumes.
        sampleGapExceeded_ = true;
        fault(false);
    }
    if (after - before >= charMin_) fault(false); // A whole byte could cross this snapshot unseen.
    if (available > 1) fault(false); // Batch contains unknowable inter-byte gaps.
    if (available == 1 && !failed_) {
        if (count_ == CAPTURE_CAPACITY || emptySince_ > before) fault(false);
        else {
            Rtu::RxByte& byte = pending_[(head_ + count_) % CAPTURE_CAPACITY];
            const uint64_t lowEnd = emptySince_;
            const uint64_t highEnd = after + stopGuard_;
            // Same uncertainty width for both intervals; widen the end interval
            // by the character duration tolerance instead of inventing precision.
            byte.startUs = lowEnd > charMax_ ? lowEnd - charMax_ : 0;
            if (byte.startUs < idleThrough_) byte.startUs = idleThrough_;
            byte.endUs = highEnd;
            const uint64_t highStart = highEnd > charMin_ ? highEnd - charMin_ : 0;
            const uint64_t startWidth = highStart > byte.startUs ? highStart - byte.startUs : 0;
            if (highEnd - lowEnd > UINT32_MAX || startWidth > UINT32_MAX) fault(false);
            byte.uncertaintyUs = maximum(bounded(highEnd - lowEnd), bounded(startWidth));
            byte.value = value;
            if (!transmitting_ && txEnd_ && byte.startUs < directionAt_) fault(false);
            maxRxWidth_ = maximum(maxRxWidth_, byte.uncertaintyUs);
            ++count_;
            highWater_ = maximum(highWater_, count_);
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
            if (after - txBusyAt_ > UINT32_MAX) fault(false);
            txWidth_ = bounded(after - txBusyAt_);
            txPending_ = false;
        }
    }
    if (timerRunning_ && transmitting_ && !txPending_ && txEnd_ && before >= txEnd_ &&
        before - txEnd_ >= holdUs_) {
        // Release locally even if the owner is asleep. GPIO write timing is
        // bracketed; electrical DE timing still needs an independent capture.
        const uint64_t low = now();
        gpio_ll_set_level(&GPIO, pins_.de, pins_.activeHigh ? 0 : 1);
        releasedAt_ = directionAt_ = now();
        releaseWidth_ = bounded(releasedAt_ - low);
        transmitting_ = false;
    }
    ++samples_;
    busyUs_ += now() - before;
    return before;
}

bool Esp32S3Uart::direction(void* ctx, bool enabled) {
    Esp32S3Uart& self = *static_cast<Esp32S3Uart*>(ctx);
    Guard guard;
    if (!self.ready_ || !uart_ll_is_tx_idle(hw) ||
        (enabled && (self.failed_ || self.configurationBlocked_))) return false;
    if (!enabled && !self.transmitting_) return true;
    const uint64_t before = now();
    const int level = enabled == self.pins_.activeHigh ? 1 : 0;
    if (gpio_set_level(static_cast<gpio_num_t>(self.pins_.de), level) != ESP_OK)
        return false;
    self.transmitting_ = enabled;
    self.directionAt_ = now();
    if (enabled) {
        self.releasedAt_ = 0;
        self.releaseWidth_ = 0;
        self.txEnd_ = 0; // Previous transaction must not trigger an early release.
        self.txWidth_ = 0;
    } else {
        self.releasedAt_ = self.directionAt_;
        self.releaseWidth_ = bounded(self.releasedAt_ - before);
    }
    // A finite board setup guard starts at the actual GPIO write, independently
    // of the runner's earlier snapshot time. This is not an unbounded drain wait.
    if (enabled) esp_rom_delay_us(20);
    return true;
}

Rtu::WriteResult Esp32S3Uart::write(void* ctx, const uint8_t* bytes, std::size_t length) {
    Esp32S3Uart& self = *static_cast<Esp32S3Uart*>(ctx);
    if (!bytes || !length || length > 64)
        return Rtu::WriteResult(0, true);
    uint8_t local[64]; // Internal stack: no PSRAM reads in the short FIFO fill.
    std::memcpy(local, bytes, length);
    Guard guard;
    if (!self.ready_ || self.failed_ || self.configurationBlocked_ || !self.transmitting_)
        return Rtu::WriteResult(0, true);
    const uint64_t before = now();
    if (!uart_ll_is_tx_idle(hw) || uart_ll_get_txfifo_len(hw) < length) {
        return Rtu::WriteResult(0, true);
    }
    uart_ll_write_txfifo(hw, local, length);
    const uint64_t after = now();
    self.txBusyAt_ = before;
    self.txPending_ = true;
    self.txIdle_ = false;
    self.txEnd_ = 0;
    self.txWidth_ = 0;
    return Rtu::WriteResult(length, after - before >= self.charMin_);
}

Rtu::TxState Esp32S3Uart::txState(void* ctx, uint64_t at, Rtu::TxObservation& observation) {
    Esp32S3Uart& self = *static_cast<Esp32S3Uart*>(ctx);
    Guard guard;
    if (!self.ready_ || self.configurationBlocked_) return Rtu::TxState::ERROR;
    // In timer mode report completion and DE release together. Otherwise a
    // release between this snapshot and a later task direction callback could
    // be mistaken for a release at the earlier poll timestamp.
    if (!self.txIdle_ || self.txPending_ || self.txEnd_ > at || self.releasedAt_ > at ||
        (self.timerRunning_ && self.transmitting_ && self.txEnd_)) return Rtu::TxState::BUSY;
    observation.endedUs = self.txEnd_;
    observation.uncertaintyUs = self.txWidth_;
    observation.released = self.releasedAt_ && self.releasedAt_ <= at;
    observation.releasedUs = observation.released ? self.releasedAt_ : 0;
    observation.releaseUncertaintyUs = observation.released ? self.releaseWidth_ : 0;
    return Rtu::TxState::IDLE;
}

Rtu::ReadState Esp32S3Uart::read(void* ctx, uint64_t at, Rtu::RxByte& byte, uint64_t& through) {
    Esp32S3Uart& self = *static_cast<Esp32S3Uart*>(ctx);
    Guard guard;
    if (!self.ready_ || self.failed_ || self.configurationBlocked_) return Rtu::ReadState::ERROR;
    if (self.count_) {
        if (self.pending_[self.head_].endUs > at) return Rtu::ReadState::PENDING;
        byte = self.pending_[self.head_];
        self.head_ = (self.head_ + 1) % CAPTURE_CAPACITY;
        --self.count_;
        return Rtu::ReadState::BYTE;
    }
    if (!self.rxIdle_) return Rtu::ReadState::PENDING;
    through = self.idleThrough_ < at ? self.idleThrough_ : at;
    return Rtu::ReadState::EMPTY;
}

bool Esp32S3Uart::clear() noexcept {
    Guard guard;
    if (!ready_ || configurationBlocked_ || transmitting_ || !uart_ll_is_tx_idle(hw) ||
        hw->fsm_status.st_urx_out != 0 || !hw->status.rxd) return false;
    uart_ll_rxfifo_rst(hw);
    hw->int_clr.val = UINT32_MAX;
    head_ = count_ = 0;
    failed_ = false;
    sampleGapExceeded_ = false;
    rxIdle_ = false; // Recovery is not fresh silence evidence; sample again first.
    emptySince_ = idleThrough_ = sampled_ = now();
    return true;
}
void Esp32S3Uart::resetStats() noexcept {
    Guard guard;
    captureFaults_ = rxErrors_ = maxPollGap_ = maxRxWidth_ = 0;
    samples_ = timerCallbacks_ = busyUs_ = 0;
    highWater_ = count_;
}
Esp32S3Uart::CaptureStats Esp32S3Uart::stats() const noexcept {
    Guard guard;
    CaptureStats s;
    s.samples = samples_; s.timerCallbacks = timerCallbacks_; s.busyUs = busyUs_; s.txEndUs = txEnd_;
    s.maxGapUs = maxPollGap_; s.faults = captureFaults_; s.rxErrors = rxErrors_;
    s.txWidthUs = txWidth_; s.maxRxWidthUs = maxRxWidth_; s.highWater = highWater_;
    s.ready = ready_ && !configurationBlocked_; s.failed = failed_ || configurationBlocked_; s.timer = timerRunning_;
    s.actualBaud = actualBaud_;
    s.sampleGapLimitUs = charMin_; s.sampleGapExceeded = sampleGapExceeded_;
    return s;
}
Rtu::Port Esp32S3Uart::port() noexcept {
    Rtu::Port value;
    value.context = this;
    value.setTransmit = direction;
    value.write = write;
    value.txState = txState;
    value.read = read;
    return value;
}
} // namespace MotorControlRSExample
