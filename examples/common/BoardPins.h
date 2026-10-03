/**
 * @file BoardPins.h
 * @brief Example-only pin selection for the available ESP32-S3 motor bench.
 *
 * The user confirmed TX47, RX48 and DE21 on the board connected to COM13.
 * These are bench wiring facts, not a motor-library or product requirement.
 * See docs/hardware_bench.md for wiring provenance and qualification.
 * No GPIO or UART is configured by this header.
 */

#pragma once

namespace MotorControlRSExample {

/** @brief Selected ESP32-S3 bench wiring; replace for another application. */
struct Esp32S3BenchBoard {
  /// @brief MCU UART transmit pin, confirmed by the user for this bench.
  static constexpr int kRs485TxPin = 47;
  /// @brief MCU UART receive pin, confirmed by the user for this bench.
  static constexpr int kRs485RxPin = 48;
  /// @brief Combined transceiver direction pin, confirmed by the user.
  static constexpr int kRs485DeRePin = 21;
  /// @brief Bench transceiver wiring: high transmits, low receives.
  static constexpr bool kRs485DeReActiveHigh = true;
};

/// @brief Explicit example bench selection; not part of the reusable library.
using Board = Esp32S3BenchBoard;

}  // namespace MotorControlRSExample
