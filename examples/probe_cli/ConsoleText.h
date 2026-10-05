// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>

namespace MotorControlRSExample { namespace Probe {
/** Render an existing console response for a person. This presentation adapter
 * reads only the bounded, self-generated JSON report; it performs no protocol
 * parsing, I/O, admission or state changes. Input and output must not overlap.
 * Invalid input or an outcome summary that cannot fit returns false with empty
 * output. Valid reports may omit details with an explicit truncation notice.
 * The caller retains ordinary reply/result ownership in either case. */
bool renderHuman(const char* json, char* output, std::size_t capacity) noexcept;
}} // namespace MotorControlRSExample::Probe
