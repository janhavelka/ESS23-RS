// SPDX-License-Identifier: MIT
#pragma once
#include "Hardware.h"
inline uint32_t esp_reset_reason() { return hardware.resetReason; }
