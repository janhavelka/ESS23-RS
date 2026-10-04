/** @file Position.h
 * @brief Bounded ESS relative positioning: checked staging, trigger and fresh observations. No I/O.
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "MotorControlRS/MoveOperation.h"
#include "MotorControlRS/profiles/ess_rs/Actions.h"

namespace MotorControlRS { namespace ESS_RS {
constexpr std::size_t MOVE_REQUEST_BYTES = 19;
/** Exact target/configuration binding and caller-qualified prerequisites.
 * Configured ramp words are preserved verbatim in the reviewed FC10 window;
 * their names do not claim a physical acceleration. Qualification must include
 * active input assignments/levels and device limits; unwired is not disabled.
 * No encoder/origin is required by an otherwise valid native relative request. */
struct MovePrerequisites {
    ReadTarget target;
    uint32_t configurationGeneration = 0;
    bool commandUnitsVerified = false, relativeBasisVerified = false;
    bool negativeTwosComplementVerified = false;
    bool configuredRampVerified = false, serialInputsPermit = false;
    bool readinessQualified = false;
    uint16_t accelerationTime = 0, decelerationTime = 0;
    bool wordOrderKnown = false;
    bool startSpeedKnown = false;
    uint16_t startSpeed = 0; ///< Qualified native-RPM relation from existing 0x0020; plain ambiguous readback is insufficient. Never rewritten.
    WordOrder wordOrder = WordOrder::HIGH_WORD_FIRST;
    uint16_t rawAlarm = 0, rawMotion = 0;
    uint64_t observedUs = 0, maximumAgeUs = 0; ///< Admission/new writes require age below this immutable budget; qualified closure may equal its deadline.
};
/** Caller-owned, read-only between API calls. step0 stages 0x0021/5; step1
 * triggers exactly 0x0001; later tokens read non-consuming alarm/motion words.
 * One axis reservation must survive every staging/trigger/wait boundary.
 * uncertain includes a possibly/definitely applied setup on terminal failure;
 * execution separately describes the trigger, never physical completion. */
struct MoveContext {
    ReadTarget target;
    uint32_t operationId = 0;
    MoveRequest request;
    MovePrerequisites prerequisites; ///< Immutable admitted qualification/baseline snapshot.
    PreparedTarget prepared;
    ActionOptions options;
    ActionState state = ActionState::EMPTY;
    ActionOutcome outcome = ActionOutcome::NONE;
    ActionExecution setupExecution = ActionExecution::NOT_TRANSMITTED;
    ActionExecution execution = ActionExecution::NOT_TRANSMITTED;
    ActionCompletion completion = ActionCompletion::NOT_OBSERVED;
    bool stagingApplied = false, uncertain = false, runningObserved = false, observationKnown = false;
    uint16_t words[5] = {}, rawAlarm = 0, rawMotion = 0;
    uint64_t startedUs = 0, deadlineUs = 0, servicedUs = 0, eligibleUs = 0;
    uint8_t step = 0, polls = 0;
    ActionEvidence stagingEvidence, triggerEvidence, activityEvidence, lastObservation, failureEvidence;
    Status status;
};
struct PreparedMove {
    ActionWork kind = ActionWork::DONE;
    ReadTarget target;
    uint32_t operationId = 0;
    uint8_t step = 0, function = 0;
    uint8_t bytes[MOVE_REQUEST_BYTES] = {};
    std::size_t length = 0;
    uint16_t reg = 0, count = 0, value = 0;
    bool write = false;
    uint64_t deadlineUs = 0, eligibleUs = 0; ///< Writes are capped by immutable readiness age; observations retain the operation deadline.
};
/** Validate all parameters and prerequisites before publishing any work.
 * Unsupported bases, unresolved negative encoding/ramp/units and stale readiness
 * reject with output unchanged. Zero effective displacement rejects explicitly.
 * Fixed trigger flags request finite relative, noninterrupting positioning.
 * A consumed native reference is checked at nowUs, not its cached nowUs. Its
 * freshness must cover the capped write deadline; refresh it or reduce the
 * readiness budget/operation deadline before preparation. No reference is needed
 * for an otherwise valid native displacement without endpoint limits. */
Status prepareMoveRelative(MoveContext&, const AxisConfig&, const AxisReference*,
                           uint32_t operationId, const MoveRequest&, const MovePrerequisites&,
                           uint64_t nowUs, uint64_t deadlineUs,
                           const ActionOptions& = ActionOptions()) noexcept;
Status nextMove(const MoveContext&, uint64_t nowUs, PreparedMove&) noexcept;
/** Copies bounded evidence. Confirmed echoes acknowledge only; completion needs
 * a fresh post-trigger RUNNING report followed by arrived and stopped. A short
 * move missed between polls stays unobserved. Cancellation never sends stop,
 * truncates TX or retries setup/trigger. Wrong envelopes leave state unchanged.
 * A write deadline may expire before the retained operation deadline, reporting
 * READINESS. The caller must honor yielded deadlines through queue/setup/TX.
 * On-time trigger evidence may arrive after readiness expires; observations can
 * continue, but no new staging or trigger is yielded from stale readiness. */
Status advanceMove(MoveContext&, const ActionEvent&, uint64_t nowUs) noexcept;
}} // namespace MotorControlRS::ESS_RS

namespace MotorControlRS {
inline Status prepareMoveRelative(ESS_RS::MoveContext& c, const AxisConfig& axis,
        const AxisReference* reference, uint32_t id, const MoveRequest& request,
        const ESS_RS::MovePrerequisites& prerequisites, uint64_t now, uint64_t deadline,
        const ActionOptions& options = ActionOptions()) noexcept {
    return ESS_RS::prepareMoveRelative(c, axis, reference, id, request, prerequisites, now, deadline, options);
}
} // namespace MotorControlRS
