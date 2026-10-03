/**
 * @file BuildConfig.h
 * @brief Framework-neutral example configuration, separate from motor settings.
 */

#pragma once

#include <stdint.h>

namespace MotorControlRSExample {

/// @brief Standalone USB console rate.
/// @note This is not the RS485 motor baud rate or evidence of its settings.
constexpr uint32_t kConsoleBaud = 115200U;

}  // namespace MotorControlRSExample
