// SPDX-License-Identifier: MIT
#pragma once
#include <MotorControlRS/profiles/ess_rs/Discovery.h>
#include "../common/HostSerial.h"

namespace MotorControlRSExample { namespace Probe {
constexpr uint8_t DISCOVERY_MAX_TUPLES = 4, DISCOVERY_MAX_RESULTS = 8;
enum class DiscoveryCommandKind : uint8_t { BEGIN, CANCEL, RESTORE, FINISH };
struct DiscoverySettings {
    MotorControlRS::DriveProfile profile = MotorControlRS::DriveProfile::ESS_RS;
    uint8_t first = 0, last = 0; ///< Zero/zero selects the bound endpoint.
    HostTuple tuples[DISCOVERY_MAX_TUPLES];
    uint8_t tupleCount = 0; ///< Zero selects the actual current host tuple.
    uint32_t queryMs = 500, overallMs = 5000;
    uint16_t requestLimit = 16;
    uint8_t resultLimit = DISCOVERY_MAX_RESULTS;
    bool identity = false;
};
struct DiscoveryCommand {
    DiscoveryCommandKind kind = DiscoveryCommandKind::BEGIN;
    DiscoverySettings settings;
};
enum class DiscoveryPhase : uint8_t { EMPTY, PROBE, IDENTITY, RESTORING, INTERLOCK, TERMINAL };
enum class DiscoveryOutcome : uint8_t {
    NONE, COMPLETE, CANCELLED, DEADLINE, REQUEST_LIMIT, RESULT_LIMIT,
    TRANSPORT_FAULT, RESTORE_FAILED, PREEMPTED, SETUP_FAILED
};
struct DiscoveryFinding {
    MotorControlRS::ESS_RS::PreparedProbe request;
    MotorControlRS::ESS_RS::ProbeObservation probe;
    MotorControlRS::ESS_RS::IdentityObservation identity;
    MotorControlRS::ESS_RS::ReadStepObservation identityEvidence;
    uint32_t identityOperationId = 0;
    uint64_t identityDeadlineUs = 0;
    bool identityAttempted = false, identityKnown = false, identityAmbiguous = false;
    bool collisionExcluded = false; ///< Never infer uniqueness from a valid reply.
};
/** One cooperative application-owned scan and bounded retained results. These
 * types are outside the installed core; one UART/task executes all traffic. */
struct DiscoveryScan {
    DiscoverySettings settings;
    MotorControlRS::ReadTarget originalTarget;
    HostTuple originalTuple;
    uint32_t operationId = 0, originalSerialGeneration = 0;
    uint64_t startedUs = 0, deadlineUs = 0, finishedUs = 0;
    DiscoveryPhase phase = DiscoveryPhase::EMPTY;
    DiscoveryOutcome outcome = DiscoveryOutcome::NONE;
    uint16_t requests = 0;
    uint8_t count = 0, tupleIndex = 0, address = 0;
    bool owned = false, restored = false, released = false, cancelRequested = false;
    DiscoveryFinding findings[DISCOVERY_MAX_RESULTS];
};
struct DiscoveryView {
    const DiscoveryScan* scan = nullptr; ///< Synchronous formatting only.
};
}} // namespace MotorControlRSExample::Probe
