// SPDX-License-Identifier: MIT
#include "Hardware.h"
#include "hal/gpio_ll.h"
#include <algorithm>
#include <cassert>

uart_dev_t fakeUart;
gpio_dev_t GPIO;
gptimer_t fakeTimer;
Hardware hardware;
void resetHardware() { hardware = Hardware(); fakeUart = uart_dev_t(); }

void scheduleWire(const WireEvent& event) {
    assert(event.at >= hardware.time);
    const auto after = std::upper_bound(hardware.events.begin(), hardware.events.end(), event.at,
        [](uint64_t at, const WireEvent& queued) { return at < queued.at; });
    hardware.events.insert(after, event);
}
void scheduleReply(uint64_t start, const std::vector<uint8_t>& bytes,
                   uint32_t characterUs, uint32_t gapUs) {
    assert(characterUs != 0);
    for (uint8_t value : bytes) {
        scheduleWire(WireEvent(start, WireEventKind::RX_START));
        scheduleWire(WireEvent(start + characterUs, WireEventKind::RX_END, value));
        start += characterUs + gapUs;
    }
}
namespace {
void applyWire(uint64_t at) {
    while (!hardware.events.empty() && hardware.events.front().at <= at) {
        const WireEvent event = hardware.events.front();
        hardware.events.pop_front();
        switch (event.kind) {
        case WireEventKind::RX_START:
            fakeUart.fsm_status.st_urx_out = 1; fakeUart.status.rxd = 0;
            break;
        case WireEventKind::RX_END:
            if (hardware.rx.size() < hardware.fifoCapacity) hardware.rx.push_back(event.value);
            else { ++hardware.droppedBytes; fakeUart.int_raw.val |= 1U << 4; }
            fakeUart.fsm_status.st_urx_out = 0; fakeUart.status.rxd = 1;
            break;
        case WireEventKind::TX_FIFO_EMPTY: fakeUart.status.txfifo_cnt = 0; break;
        case WireEventKind::TX_IDLE: fakeUart.fsm_status.st_utx_out = 0; break;
        }
    }
}
bool timerCall() {
    ++hardware.timerCalls;
    return std::find(hardware.timerFailCalls.begin(), hardware.timerFailCalls.end(),
                     hardware.timerCalls) == hardware.timerFailCalls.end();
}
}
void advanceHardware(uint64_t at) {
    assert(at >= hardware.time);
    // Hardware bytes progress through critical sections. A masked timer fires
    // once after unmasking; it cannot replay snapshots that were never taken.
    while (hardware.timerRunning && !hardware.inTimer && hardware.criticalDepth == 0 &&
           hardware.timerNext <= at) {
        const uint64_t fire = std::max(hardware.time, hardware.timerNext);
        applyWire(fire);
        hardware.time = fire;
        hardware.timerNext = fire + hardware.timerPeriod;
        hardware.inTimer = true;
        ++hardware.timerCallbacks;
        const gptimer_alarm_event_data_t event;
        hardware.timerCallback(&fakeTimer, &event, hardware.timerContext);
        hardware.inTimer = false;
        // Avoid an artificial callback storm if its work exceeds one period.
        if (hardware.timerNext < hardware.time) hardware.timerNext = hardware.time + hardware.timerPeriod;
    }
    applyWire(std::max(at, hardware.time));
    hardware.time = std::max(at, hardware.time);
}

FakeClearRegister::Value& FakeClearRegister::Value::operator=(uint32_t bits) {
    fakeUart.int_raw.val &= ~bits;
    return *this;
}
void fakeEnterCritical() { assert(hardware.criticalDepth++ == 0); }
void fakeExitCritical() {
    assert(hardware.criticalDepth-- == 1);
    advanceHardware(hardware.time);
}
bool uart_is_driver_installed(uart_port_t port) { assert(port == UART_NUM_2); return hardware.driverInstalled; }
esp_err_t uart_param_config(uart_port_t port, const uart_config_t* config) {
    assert(port == UART_NUM_2); hardware.config = *config; return hardware.configResult;
}
esp_err_t uart_set_pin(uart_port_t port, int tx, int rx, int rts, int cts) {
    assert(port == UART_NUM_2 && rts == UART_PIN_NO_CHANGE && cts == UART_PIN_NO_CHANGE);
    hardware.txPin = tx; hardware.rxPin = rx; return hardware.pinResult;
}
esp_err_t gpio_set_level(gpio_num_t pin, int value) {
    assert(GPIO_IS_VALID_OUTPUT_GPIO(pin));
    hardware.dePin = pin;
    if (hardware.levelResult == ESP_OK) {
        hardware.de = value;
        if (value != hardware.transmitLevel) hardware.deReleasedAt = hardware.time;
    }
    return hardware.levelResult;
}
esp_err_t gpio_set_direction(gpio_num_t pin, int mode) {
    assert(GPIO_IS_VALID_OUTPUT_GPIO(pin) && pin == hardware.dePin && mode == GPIO_MODE_OUTPUT);
    return hardware.directionResult;
}
int64_t esp_timer_get_time() {
    const uint64_t at = hardware.time;
    advanceHardware(at + hardware.clockStep);
    return static_cast<int64_t>(at);
}
void gpio_ll_set_level(gpio_dev_t* gpio, unsigned pin, unsigned level) {
    assert(gpio == &GPIO && static_cast<int>(pin) == hardware.dePin && level <= 1);
    hardware.de = static_cast<int>(level);
    if (hardware.de != hardware.transmitLevel) hardware.deReleasedAt = hardware.time;
}
void esp_rom_delay_us(uint32_t us) { advanceHardware(hardware.time + us); }
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
    assert(hardware.criticalDepth == 1 && hardware.de == hardware.transmitLevel &&
           count <= 128 - hw->status.txfifo_cnt);
    ++hardware.writes; hardware.tx.assign(bytes, bytes + count);
    hardware.writeStarted = hardware.time;
    hw->status.txfifo_cnt += static_cast<unsigned>(count); hw->fsm_status.st_utx_out = 1;
    if (hardware.txCharacterUs) {
        scheduleWire(WireEvent(hardware.time + (count - 1) * hardware.txCharacterUs,
                               WireEventKind::TX_FIFO_EMPTY));
        scheduleWire(WireEvent(hardware.time + count * hardware.txCharacterUs,
                               WireEventKind::TX_IDLE));
    }
}
esp_err_t gptimer_new_timer(const gptimer_config_t* config, gptimer_handle_t* output) {
    assert(config->resolution_hz == 1000000 && !hardware.timerCreated);
    if (!timerCall()) return -1;
    hardware.timerCreated = true; *output = &fakeTimer; return ESP_OK;
}
esp_err_t gptimer_register_event_callbacks(gptimer_handle_t timer,
                                         const gptimer_event_callbacks_t* callbacks, void* context) {
    assert(timer == &fakeTimer && hardware.timerCreated && !hardware.timerRunning);
    if (!timerCall()) return -1;
    hardware.timerCallback = callbacks->on_alarm; hardware.timerContext = context; return ESP_OK;
}
esp_err_t gptimer_set_alarm_action(gptimer_handle_t timer, const gptimer_alarm_config_t* config) {
    assert(timer == &fakeTimer && config->flags.auto_reload_on_alarm &&
           config->alarm_count > config->reload_count);
    if (!timerCall()) return -1;
    hardware.timerPeriod = config->alarm_count - config->reload_count; return ESP_OK;
}
esp_err_t gptimer_enable(gptimer_handle_t timer) {
    assert(timer == &fakeTimer && hardware.timerCreated && !hardware.timerEnabled);
    if (!timerCall()) return -1;
    hardware.timerEnabled = true; return ESP_OK;
}
esp_err_t gptimer_start(gptimer_handle_t timer) {
    assert(timer == &fakeTimer && hardware.timerEnabled && !hardware.timerRunning && hardware.timerCallback);
    if (!timerCall()) return -1;
    hardware.timerRunning = true; hardware.timerNext = hardware.time + hardware.timerPeriod; return ESP_OK;
}
esp_err_t gptimer_stop(gptimer_handle_t timer) {
    assert(timer == &fakeTimer);
    if (!hardware.timerRunning) return -2;
    if (!timerCall()) return -1;
    hardware.timerRunning = false; return ESP_OK;
}
esp_err_t gptimer_disable(gptimer_handle_t timer) {
    assert(timer == &fakeTimer);
    if (!hardware.timerEnabled || hardware.timerRunning) return -2;
    if (!timerCall()) return -1;
    hardware.timerEnabled = false; return ESP_OK;
}
esp_err_t gptimer_del_timer(gptimer_handle_t timer) {
    assert(timer == &fakeTimer);
    if (!hardware.timerCreated || hardware.timerEnabled) return -2;
    if (!timerCall()) return -1;
    hardware.timerCreated = false; hardware.timerCallback = nullptr; hardware.timerContext = nullptr;
    ++hardware.timerDeletes; return ESP_OK;
}
esp_err_t gptimer_set_raw_count(gptimer_handle_t timer, uint64_t value) {
    assert(timer == &fakeTimer && value == 0);
    return timerCall() ? ESP_OK : -1;
}
