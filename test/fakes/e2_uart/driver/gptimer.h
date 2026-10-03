// SPDX-License-Identifier: MIT
#pragma once
#include "../FakeEsp.h"

struct gptimer_t {};
using gptimer_handle_t = gptimer_t*;
constexpr int GPTIMER_CLK_SRC_DEFAULT = 0, GPTIMER_COUNT_UP = 0;
struct gptimer_config_t {
    int clk_src = 0, direction = 0;
    uint32_t resolution_hz = 0;
    int intr_priority = 0;
};
struct gptimer_alarm_event_data_t { uint64_t count_value = 0, alarm_value = 0; };
using gptimer_alarm_cb_t = bool (*)(gptimer_handle_t, const gptimer_alarm_event_data_t*, void*);
struct gptimer_event_callbacks_t { gptimer_alarm_cb_t on_alarm = nullptr; };
struct gptimer_alarm_config_t {
    uint64_t alarm_count = 0, reload_count = 0;
    struct { unsigned auto_reload_on_alarm = 0; } flags;
};
esp_err_t gptimer_new_timer(const gptimer_config_t*, gptimer_handle_t*);
esp_err_t gptimer_register_event_callbacks(gptimer_handle_t, const gptimer_event_callbacks_t*, void*);
esp_err_t gptimer_set_alarm_action(gptimer_handle_t, const gptimer_alarm_config_t*);
esp_err_t gptimer_enable(gptimer_handle_t);
esp_err_t gptimer_start(gptimer_handle_t);
esp_err_t gptimer_stop(gptimer_handle_t);
esp_err_t gptimer_disable(gptimer_handle_t);
esp_err_t gptimer_del_timer(gptimer_handle_t);
esp_err_t gptimer_set_raw_count(gptimer_handle_t, uint64_t);
