/**
 * @file Status.h
 * @brief Allocation-free validation results shared by the library.
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdint.h>

namespace RS485Motion {

/** @brief Pure-call errors; separate from drive alarms and application health. */
enum class Err : uint8_t {
  OK = 0,          ///< The requested calculation/validation succeeded.
  INVALID_CONFIG,  ///< Invalid configuration, enum, scale or argument.
  ILLEGAL_VALUE,   ///< Value cannot be represented within the declared bounds.
  UNSUPPORTED,    ///< Required conversion or reviewed capability is unavailable.
  CRC_ERROR,      ///< Frame checksum does not match.
  FRAME_ERROR,    ///< Reply shape or request expectation does not match.
  EXCEPTION       ///< Valid device exception; detail preserves its raw byte.
};

/**
 * @brief Result of a pure library call, distinct from motor/application health.
 * @note msg points to a static string; detail is a documented numeric reason.
 * No allocation, logging, clock access or transport operation is performed.
 */
struct Status {
  Err code;
  int32_t detail;
  const char* msg;

  constexpr Status() : code(Err::OK), detail(0), msg("") {}
  constexpr Status(Err value, int32_t reason, const char* message)
      : code(value), detail(reason), msg(message) {}
  constexpr bool isOk() const { return code == Err::OK; }
  constexpr explicit operator bool() const { return isOk(); }
};

/** @brief Create a successful pure-call result. */
constexpr Status Ok() { return Status(); }

/** @brief Return a static label without allocating or retaining caller data. */
inline const char* errToString(Err code) {
  switch (code) {
    case Err::OK: return "OK";
    case Err::INVALID_CONFIG: return "INVALID_CONFIG";
    case Err::ILLEGAL_VALUE: return "ILLEGAL_VALUE";
    case Err::UNSUPPORTED: return "UNSUPPORTED";
    case Err::CRC_ERROR: return "CRC_ERROR";
    case Err::FRAME_ERROR: return "FRAME_ERROR";
    case Err::EXCEPTION: return "EXCEPTION";
  }
  return "UNKNOWN";
}

}  // namespace RS485Motion
