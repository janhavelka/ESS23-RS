/**
 * @file BuildConfig.h
 * @brief Framework-neutral example configuration, separate from motor settings.
 */

#pragma once

#include <stdint.h>

namespace MotorControlRSExample {

/// @brief USB console rate used by both inspected CO2Control firmware builds.
/// @note This is not the RS485 motor baud rate or evidence of its settings.
constexpr uint32_t kConsoleBaud = 115200U;

}  // namespace MotorControlRSExample
