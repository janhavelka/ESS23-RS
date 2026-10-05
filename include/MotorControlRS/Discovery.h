/** @file Discovery.h
 * @brief Reviewed local discovery inventory and bounded non-changing probe. No I/O.
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "MotorControlRS/Status.h"
#include "MotorControlRS/ReadOperation.h"

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
    bool exactModel = false, firmware = false; ///< Profile-advertised identity resolution, separate from response validity.
    uint8_t minimumAddress = 0, maximumAddress = 0;
    uint8_t probeRequestBytes = 0, probeReplyBytes = 0, exceptionReplyBytes = 0;
    /** Optional register-profile query metadata; not a universal serial layout. */
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
} // namespace MotorControlRS
