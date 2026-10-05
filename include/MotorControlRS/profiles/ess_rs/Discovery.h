/** @file Discovery.h
 * @brief ESS non-changing probe request, interpretation and retained evidence. No I/O.
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "MotorControlRS/Discovery.h"
#include "MotorControlRS/profiles/ess_rs/Reads.h"

namespace MotorControlRS { namespace ESS_RS {
/** Reviewed ESS query capabilities; register and frame metadata belong to this profile. */
DiscoveryCapabilities probeCapabilities() noexcept;

/** Caller-owned immutable request snapshot. Reusing it cannot authorize a retry;
 * the application admits/transmits at most its explicitly chosen attempt budget. */
struct PreparedProbe {
    DriveProfile profile = DriveProfile::ESS_RS;
    ReadTarget target;
    uint32_t operationId = 0;
    uint64_t startedUs = 0, deadlineUs = 0;
    ActiveSerialTuple activeSerial;
    uint8_t bytes[READ_REQUEST_LEN] = {};
    std::size_t length = 0;
};
/** Copied evidence from one attempt. No pointer to caller frame storage remains.
 * A checked response/exception proves responsiveness only: manufacturer, exact
 * model, readiness and address uniqueness remain unestablished. */
struct ProbeObservation {
    DriveProfile profile = DriveProfile::ESS_RS;
    ReadTarget target;
    uint32_t operationId = 0;
    ActiveSerialTuple activeSerial;
    ProbeOutcome outcome = ProbeOutcome::NONE;
    ProbeConfidence confidence = ProbeConfidence::NONE;
    bool rawModelKnown = false;
    uint16_t rawModel = 0;
    ReadResolution modelResolution = ReadResolution::MODEL_MAPPING_UNRESOLVED;
    Status status;
    ReadStepObservation provenance;
};
/** Prepare exactly ESS FC03 model register 0x0000/one word. Unicast target IDs
 * and operation ID are nonzero, deadline strictly follows nowUs. Unknown tuple
 * evidence is retained; no clock, settings change, retry or scan occurs. */
Status prepareProbe(PreparedProbe& output, const ReadTarget& target,
                    uint32_t operationId, uint64_t nowUs, uint64_t deadlineUs,
                    const ActiveSerialTuple& activeSerial = ActiveSerialTuple()) noexcept;
/** FRAME requires checked closure bounds. On-time final closure may be delivered
 * later. Wrong correlation/invalid envelopes reject unchanged output; consumed
 * failures return OK with explicit outcome/status and bounded copied evidence.
 * DEADLINE without received bytes reports NO_RESPONSE; late frames report
 * DEADLINE. Transport reasons remain application-defined and are retained. */
Status checkProbe(const PreparedProbe& prepared, const ReadEvent& event,
                  uint64_t nowUs, ProbeObservation& output) noexcept;

}} // namespace MotorControlRS::ESS_RS

namespace MotorControlRS {
/** Common finite-profile route to the concrete ESS probe; rejects unsupported
 * profiles before touching output. Include this profile header to use its storage. */
inline Status prepareProbe(ESS_RS::PreparedProbe& output, DriveProfile profile,
        const ReadTarget& target, uint32_t operationId, uint64_t nowUs, uint64_t deadlineUs,
        const ActiveSerialTuple& activeSerial = ActiveSerialTuple()) noexcept {
    DiscoveryCapabilities capabilities;
    const Status supported = getDiscoveryCapabilities(profile, capabilities);
    return supported ? ESS_RS::prepareProbe(output, target, operationId, nowUs, deadlineUs, activeSerial) : supported;
}
/** Common route using the selected profile's concrete request and evidence. */
inline Status checkProbe(const ESS_RS::PreparedProbe& prepared, const ReadEvent& event,
        uint64_t nowUs, ESS_RS::ProbeObservation& output) noexcept {
    return ESS_RS::checkProbe(prepared, event, nowUs, output);
}
} // namespace MotorControlRS
