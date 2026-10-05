// SPDX-License-Identifier: MIT
#pragma once
#include "../FakeEsp.h"
#include "../freertos/FreeRTOS.h"

struct usb_serial_jtag_driver_config_t { unsigned tx_buffer_size, rx_buffer_size; };
bool usb_serial_jtag_is_driver_installed();
esp_err_t usb_serial_jtag_driver_install(const usb_serial_jtag_driver_config_t*);
int usb_serial_jtag_read_bytes(void*, uint32_t, TickType_t);
int usb_serial_jtag_write_bytes(const void*, std::size_t, TickType_t);
