// SPDX-License-Identifier: MIT
#pragma once
#include "ProbeConsole.h"

namespace MotorControlRSExample { namespace Probe {
/** Application-owned diagnostics for the ordinary runner and console. No second
 * transport, operation queue or execution policy belongs to this session. */
struct DebugSession {
    static constexpr std::size_t CAPACITY = 16;
    MotorControlRS::TrafficRecord records[CAPACITY], request, scratch;
    MotorControlRS::TrafficCapture capture{records, CAPACITY};
    DebugSnapshot state;
    uint64_t cursor = 0;
    bool requestKnown = false;
};
}} // namespace MotorControlRSExample::Probe
