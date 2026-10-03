// SPDX-License-Identifier: MIT
#pragma once
#include "../FakeEsp.h"
struct gpio_dev_t {};
extern gpio_dev_t GPIO;
void gpio_ll_set_level(gpio_dev_t*, unsigned, unsigned);
