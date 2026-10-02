/**
 * @file BoardPins.h
 * @brief Example-only pin selection for the user-confirmed E2 HW2.0.0 bench.
 *
 * The user confirmed TX47, RX48 and DE21 on the board connected to COM13.
 * FieldCore's TunnelMonitorS3Hw200Board records the same pins, active-high
 * DE/RE and UART2. That electrical reference is not a claim that the bench
 * runs the TunnelMonitor product. See docs/reference/04_co2control_platform.md.
 * No GPIO or UART is configured by this header.
 */

#pragma once

namespace RS485MotionExample {

/** @brief ESP32-S3 E2 board revision 2.0.0 used for this example bench. */
struct E2S3Hw200Board {
  /// @brief MCU UART transmit pin, confirmed by the user for this bench.
  static constexpr int kRs485TxPin = 47;
  /// @brief MCU UART receive pin, confirmed by the user for this bench.
  static constexpr int kRs485RxPin = 48;
  /// @brief Combined transceiver direction pin, confirmed by the user.
  static constexpr int kRs485DeRePin = 21;
  /// @brief UART2, matching the FieldCore electrical/backend reference.
  static constexpr int kRs485UartIndex = 2;
  /// @brief Matching FieldCore default: high transmits, low receives.
  static constexpr bool kRs485DeReActiveHigh = true;
};

/// @brief Explicit example bench selection; not part of the reusable library.
using Board = E2S3Hw200Board;

}  // namespace RS485MotionExample
