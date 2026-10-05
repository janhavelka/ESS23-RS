/** @file Discovery.h
 * @brief Reviewed local discovery inventory and bounded non-changing probe. No I/O.
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "MotorControlRS/ReadOperation.h"
#include "MotorControlRS/profiles/ess_rs/Reads.h"

namespace MotorControlRS {

enum class Manufacturer : uint8_t { STEPPERONLINE };
enum class DriveProfile : uint8_t { ESS_RS };
/** Static local inventory only; names have static lifetime. A profile is a query
 * candidate, not a manufacturer/model identification established by a reply. */
struct DiscoveryProfile {
    Manufacturer manufacturer = Manufacturer::STEPPERONLINE;
    DriveProfile profile = DriveProfile::ESS_RS;
    const char* manufacturerId = "stepperonline";
    const char* manufacturerName = "STEPPERONLINE";
    const char* profileId = "ess_rs";
    const char* profileName = "ESS-RS";
};
struct DiscoveryCapabilities {
    bool probe = false, identity = false, nonChanging = false;
    bool exactModel = false, firmware = false; ///< Raw identity codes have no reviewed mapping.
    uint8_t minimumAddress = 0, maximumAddress = 0;
    uint8_t probeRequestBytes = 0, probeReplyBytes = 0, exceptionReplyBytes = 0;
    uint16_t probeFirst = 0, probeCount = 0, identityFirst = 0, identityCount = 0;
};
std::size_t discoveryProfileCount() noexcept;
/** All fallible inventory/preparation calls leave output unchanged on error. */
Status getDiscoveryProfile(std::size_t index, DiscoveryProfile& output) noexcept;
Status getDiscoveryCapabilities(DriveProfile profile, DiscoveryCapabilities& output) noexcept;

enum class ProbeOutcome : uint8_t {
    NONE, RESPONDER, EXCEPTION, MALFORMED, MISMATCH, NO_RESPONSE,
    CANCELLED, DEADLINE, TRANSPORT_ERROR, TIMING_UNQUALIFIED
};
enum class ProbeConfidence : uint8_t {
    NONE, RESPONDER_ONLY, RESPONDER_MODEL_UNRESOLVED
};
/** Caller-owned immutable request snapshot. Reusing it cannot authorize a retry;
 * the application admits/transmits at most its explicitly chosen attempt budget. */
struct PreparedProbe {
    DriveProfile profile = DriveProfile::ESS_RS;
    ReadTarget target;
    uint32_t operationId = 0;
    uint64_t startedUs = 0, deadlineUs = 0;
    ActiveSerialTuple activeSerial;
    uint8_t bytes[ESS_RS::READ_REQUEST_LEN] = {};
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
    ESS_RS::ReadResolution modelResolution = ESS_RS::ReadResolution::MODEL_MAPPING_UNRESOLVED;
    Status status;
    ESS_RS::ReadStepObservation provenance;
};
/** Prepare exactly ESS FC03 model register 0x0000/one word. Unicast target IDs
 * and operation ID are nonzero, deadline strictly follows nowUs. Unknown tuple
 * evidence is retained; no clock, settings change, retry or scan occurs. */
Status prepareProbe(PreparedProbe& output, DriveProfile profile, const ReadTarget& target,
                    uint32_t operationId, uint64_t nowUs, uint64_t deadlineUs,
                    const ActiveSerialTuple& activeSerial = ActiveSerialTuple()) noexcept;
/** FRAME requires checked closure bounds. On-time final closure may be delivered
 * later. Wrong correlation/invalid envelopes reject unchanged output; consumed
 * failures return OK with explicit outcome/status and bounded copied evidence.
 * DEADLINE without received bytes reports NO_RESPONSE; late frames report
 * DEADLINE. Transport reasons remain application-defined and are retained. */
Status checkProbe(const PreparedProbe& prepared, const ReadEvent& event,
                  uint64_t nowUs, ProbeObservation& output) noexcept;

} // namespace MotorControlRS
