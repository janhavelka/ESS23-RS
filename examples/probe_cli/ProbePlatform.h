// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <cstdint>

namespace MotorControlRSExample { namespace Platform {
// Example-only console/task boundary. Exactly one cooperative application task
// calls these functions; motor and command semantics stay in ProbeApp.cpp.
bool beginConsole();
int availableConsole();
int readConsole();
int writableConsole();
std::size_t writeConsole(const uint8_t*, std::size_t);
void idle(unsigned milliseconds);
void bootFailure(const char* reason);
} }
