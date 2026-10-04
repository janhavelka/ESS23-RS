/** @file Velocity.h
 * @brief Bounded ESS serial velocity staging, observation and stop. No I/O.
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "MotorControlRS/VelocityOperation.h"
#include "MotorControlRS/profiles/ess_rs/Actions.h"

namespace MotorControlRS { namespace ESS_RS {
constexpr std::size_t VELOCITY_REQUEST_BYTES = 15;
struct VelocityPrerequisites {
    ReadTarget target;
    uint32_t configurationGeneration = 0;
    bool nativeRpmVerified = false, negativeTwosComplementVerified = false;
    bool configuredRampVerified = false, serialInputsPermit = false, readinessQualified = false;
    uint16_t accelerationTime = 0, decelerationTime = 0; ///< Qualified native words, never physical acceleration.
    int16_t minimumRpm = -3000, maximumRpm = 3000; ///< Caller-qualified subset; cannot broaden signed 3000-rpm subset.
    uint16_t rawAlarm = 0, rawMotion = 0;
    uint64_t observedUs = 0, maximumAgeUs = 0;
};
enum class VelocityPhase : uint8_t { STAGING, TRIGGER, OBSERVING, STOPPING };
/** Caller-owned, read-only between calls. One axis reservation spans staging,
 * trigger, observation waits and stop. The immutable stopDueUs starts at
 * admission, including setup. Finite duration is a host service obligation;
 * ESS has no established refresh/watchdog or communication-loss stop.
 * Error-triggered cleanup preserves the original status/outcome and evidence.
 * needsStop stays true until a new checked stop observation, independently of
 * setup/start uncertainty. Cancellation is local and never yields stop. */
struct VelocityContext {
    ReadTarget target;
    uint32_t operationId = 0;
    VelocityRequest request;
    VelocityPrerequisites prerequisites;
    PreparedVelocityTarget prepared;
    ActionOptions options;
    ActionState state = ActionState::EMPTY;
    ActionOutcome outcome = ActionOutcome::NONE;
    ActionExecution setupExecution = ActionExecution::NOT_TRANSMITTED;
    ActionExecution execution = ActionExecution::NOT_TRANSMITTED;
    ActionCompletion completion = ActionCompletion::NOT_OBSERVED;
    VelocityPhase phase = VelocityPhase::STAGING;
    bool stagingApplied = false, uncertain = false, needsStop = false;
    bool runningObserved = false, observationKnown = false, serviceMissed = false;
    uint16_t words[3] = {}, rawAlarm = 0, rawMotion = 0;
    uint64_t startedUs = 0, deadlineUs = 0, stopDueUs = 0, servicedUs = 0, eligibleUs = 0;
    uint8_t step = 0, polls = 0;
    ActionEvidence stagingEvidence, triggerEvidence, activityEvidence, lastObservation, failureEvidence;
    ActionContext stop;
    Status status;
};
struct PreparedVelocity {
    ActionWork kind = ActionWork::DONE;
    ReadTarget target;
    uint32_t operationId = 0;
    uint8_t step = 0, function = 0;
    uint8_t bytes[VELOCITY_REQUEST_BYTES] = {};
    std::size_t length = 0;
    uint16_t reg = 0, count = 0, value = 0;
    bool write = false, urgent = false;
    uint64_t deadlineUs = 0, eligibleUs = 0;
};
/** Fully validates zero/sign/range, configured ramps, readiness and explicit
 * stop before any request is yielded. The target conversion uses the common
 * prepareVelocityTarget. No acceleration formula is established for ESS; an
 * ACCELERATION policy rejects as unsupported. Output unchanged on error. */
Status prepareVelocity(VelocityContext&, const AxisConfig&, uint32_t operationId,
                       const VelocityRequest&, const VelocityPrerequisites&,
                       uint64_t nowUs, uint64_t deadlineUs,
                       const ActionOptions& = ActionOptions()) noexcept;
/** Call only with no admitted transaction after processing captured evidence.
 * Advances supplied time to the finite stop boundary. Service later than
 * stopDueUs+pollIntervalUs records SERVICE_MISSED and still yields the explicit
 * stop within the original deadline; no observation is admitted after stopDueUs.
 * This bounds host work eligibility, not physical stop admission/completion.
 * The application needs a settled healthy runner, available urgent storage and
 * bounded owner service to dispatch it. Pressure/faults cannot renew deadlineUs;
 * unobserved stop settlement retains needsStop, never a watchdog guarantee.
 * Expiry cannot establish standstill. It does not retry or recover transport. */
Status serviceVelocity(VelocityContext&, uint64_t nowUs) noexcept;
Status nextVelocity(const VelocityContext&, uint64_t nowUs, PreparedVelocity&) noexcept;
/** Copied checked evidence and fresh RUNNING reports do not establish target
 * speed or acceleration. Success additionally requires the existing stop's new
 * non-running report. Wrong envelopes/correlation leave context unchanged.
 * Start/setup errors never replay writes; possible start execution enters
 * bounded stop cleanup. A local CANCEL terminates with needsStop retained. */
Status advanceVelocity(VelocityContext&, const ActionEvent&, uint64_t nowUs) noexcept;
}} // namespace MotorControlRS::ESS_RS

namespace MotorControlRS {
inline Status prepareVelocity(ESS_RS::VelocityContext& c, const AxisConfig& axis,
        uint32_t id, const VelocityRequest& request,
        const ESS_RS::VelocityPrerequisites& prerequisites, uint64_t now,
        uint64_t deadline, const ActionOptions& options = ActionOptions()) noexcept {
    return ESS_RS::prepareVelocity(c, axis, id, request, prerequisites, now, deadline, options);
}
} // namespace MotorControlRS
