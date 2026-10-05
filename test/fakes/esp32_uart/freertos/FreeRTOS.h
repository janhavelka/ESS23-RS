// SPDX-License-Identifier: MIT
#pragma once
#include "../FakeEsp.h"
using TickType_t = uint32_t;
using StackType_t = uint32_t;
using BaseType_t = int;
constexpr BaseType_t pdTRUE = 1, pdFALSE = 0;
constexpr TickType_t portMAX_DELAY = UINT32_MAX;
#define pdMS_TO_TICKS(ms) (ms)
#define configSUPPORT_STATIC_ALLOCATION 1
#define configNUMBER_OF_CORES 2
