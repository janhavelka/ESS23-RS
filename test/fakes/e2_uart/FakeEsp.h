// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <cstdint>

// Minimal fake of the exact SDK surface used by E2Uart.cpp. Implementations
// live in e2_uart_test.cpp; no adapter logic is duplicated here.
using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0;
using uart_port_t = int;
constexpr uart_port_t UART_NUM_2 = 2;
constexpr int UART_DATA_8_BITS = 8, UART_PARITY_DISABLE = 0;
constexpr int UART_STOP_BITS_1 = 1, UART_HW_FLOWCTRL_DISABLE = 0;
constexpr int UART_SCLK_XTAL = 1, UART_PIN_NO_CHANGE = -1;
struct uart_config_t {
    int baud_rate, data_bits, parity, stop_bits, flow_ctrl, source_clk;
};
using gpio_num_t = int;
constexpr int GPIO_MODE_OUTPUT = 1;
using portMUX_TYPE = int;
#define portMUX_INITIALIZER_UNLOCKED 0
void fakeEnterCritical();
void fakeExitCritical();
#define portENTER_CRITICAL(mux) ((void)(mux), fakeEnterCritical())
#define portEXIT_CRITICAL(mux) ((void)(mux), fakeExitCritical())

struct FakeRegister { uint32_t val = 0; };
struct FakeClearRegister {
    struct Value { Value& operator=(uint32_t); } val;
};
struct uart_dev_t {
    FakeRegister int_ena, int_raw;
    FakeClearRegister int_clr;
    struct { unsigned st_urx_out = 0, st_utx_out = 0; } fsm_status;
    struct { unsigned rxd = 1, txfifo_cnt = 0; } status;
};
extern uart_dev_t fakeUart;
#define UART_LL_GET_HW(port) ((void)(port), &fakeUart)

bool uart_is_driver_installed(uart_port_t);
esp_err_t uart_param_config(uart_port_t, const uart_config_t*);
esp_err_t uart_set_pin(uart_port_t, int, int, int, int);
esp_err_t gpio_set_level(gpio_num_t, int);
esp_err_t gpio_set_direction(gpio_num_t, int);
int64_t esp_timer_get_time();
void esp_rom_delay_us(uint32_t);
void uart_ll_set_tx_idle_num(uart_dev_t*, unsigned);
void uart_ll_txfifo_rst(uart_dev_t*);
void uart_ll_rxfifo_rst(uart_dev_t*);
unsigned uart_ll_get_rxfifo_len(uart_dev_t*);
unsigned uart_ll_get_txfifo_len(uart_dev_t*);
bool uart_ll_is_tx_idle(uart_dev_t*);
void uart_ll_read_rxfifo(uart_dev_t*, uint8_t*, std::size_t);
void uart_ll_write_txfifo(uart_dev_t*, const uint8_t*, std::size_t);
