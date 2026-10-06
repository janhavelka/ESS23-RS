// SPDX-License-Identifier: MIT
#pragma once
#include "Hardware.h"
inline bool esp_ptr_internal(const void*) { return hardware.captureObjectInternal; }
