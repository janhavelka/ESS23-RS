// SPDX-License-Identifier: MIT
#include "MotorControlRS/Discovery.h"
#include "MotorControlRS/profiles/ess_rs/Discovery.h"

namespace MotorControlRS {
std::size_t discoveryProfileCount() noexcept { return 1; }
Status getDiscoveryProfile(std::size_t index, DiscoveryProfile& output) noexcept {
    if (index >= discoveryProfileCount())
        return Status(Err::UNSUPPORTED, 0, "profile is not in the reviewed inventory");
    output = DiscoveryProfile();
    return Ok();
}
Status getDiscoveryCapabilities(DriveProfile profile, DiscoveryCapabilities& output) noexcept {
    if (profile != DriveProfile::ESS_RS)
        return Status(Err::UNSUPPORTED, 0, "discovery profile is not implemented");
    output = ESS_RS::probeCapabilities();
    return Ok();
}
} // namespace MotorControlRS
