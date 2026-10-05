/** @file Position.h
 * @brief Bounded ESS positioning: checked staging, trigger and fresh observations. No I/O.
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "MotorControlRS/MoveOperation.h"
#include "MotorControlRS/profiles/ess_rs/Actions.h"

namespace MotorControlRS { namespace ESS_RS {
constexpr std::size_t MOVE_REQUEST_BYTES = 19;
/** Stored native finite-position profile, read from 0x0020/6. Ramp words are
 * device encodings, not physical acceleration. targetBits preserves the full
 * existing pair for explicit restoration without interpreting signed motion. */
struct PositionProfile {
    uint16_t startSpeed = 0, accelerationTime = 0, decelerationTime = 0, speed = 0;
    uint32_t targetBits = 0;
};
/** Fixed non-consuming read; no operation, trigger or persistence side effects. */
std::size_t buildReadPositionProfile(uint8_t address, uint8_t*, std::size_t) noexcept;
/** Checked response; output remains unchanged on every failure. */
Status parsePositionProfile(const uint8_t*, std::size_t, uint8_t address,
                            WordOrder, PositionProfile&) noexcept;
/** Stage only the reviewed 0x0021/5 profile. Does not change start speed or
 * trigger motion. Caller owns stationary admission, saved values and readback.
 * Returns zero without changing the buffer for invalid values/order/capacity. */
std::size_t buildWritePositionProfile(uint8_t address, const PositionProfile&,
                                     WordOrder, uint8_t*, std::size_t) noexcept;
/** Raw finite-position start only; uses the drive's already stored target and
 * profile. No cached-state admission, setup, observation or retry is implied. */
std::size_t buildStartPosition(uint8_t address, bool relative,
                               uint8_t*, std::size_t) noexcept;
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
    uint64_t observedUs = 0, maximumAgeUs = 0; ///< Zero disables age expiry; otherwise admission/new writes require age below this immutable budget. Operation deadlines still apply.
};
enum class MoveAdmission : uint8_t { OBSERVED_STATE, NATIVE_INTENT };
/** Caller-owned, read-only between API calls. step0 stages 0x0021/5; step1
 * triggers 0x0001 relative or 0x0005 absolute; later tokens read alarm/motion words.
 * One axis reservation must survive every staging/trigger/wait boundary.
 * uncertain includes a possibly/definitely applied setup on terminal failure;
 * execution separately describes the trigger, never physical completion. */
struct MoveContext {
    MoveAdmission admission = MoveAdmission::OBSERVED_STATE;
    ReadTarget target;
    uint32_t operationId = 0;
    MoveRequest request;
    MovePrerequisites prerequisites; ///< Immutable admitted qualification/baseline snapshot.
    AxisReference reference; ///< Consumed command-coordinate witness, copied at admission; nativeKnown=false when not needed.
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
    uint64_t deadlineUs = 0, eligibleUs = 0; ///< Optional readiness expiry caps writes; the operation deadline always applies.
};
/** Remembered caller intent, not a motor-state cache. Each preparation copies
 * these settings into an independent MoveContext and always stages 0x0021/5
 * before start. Use nextMove/advanceMove and the application's existing owner,
 * reservation and priority-stop path; no I/O, clock, allocation or retries.
 *
 * This native route requires no prior motor observations. It does not establish
 * enable, input/limit state, origin, calibration or 0x0020 starting-speed
 * compatibility. It neither enables nor saves/clears anything implicitly.
 * targetBits are the exact device-native 32-bit target encoding. In particular,
 * supplying two's-complement bits is not qualification of negative motion.
 * Generic units/limits and observation-aware admission use prepareMove* instead.
 * The context retains NATIVE_INTENT; its prerequisites/reference/PreparedTarget
 * do not pretend that observations or coordinate conversion took place.
 * Success means setup and start were acknowledged: outcome ACKNOWLEDGED and
 * completion NOT_OBSERVED. No status polling can attribute an already running
 * motion to this noninterrupting start without a prior stationary observation.
 * Use typed state reads for feedback, or prepareMove* for observed completion.
 */
struct PositionCommand {
    ReadTarget target;
    WordOrder wordOrder = WordOrder::HIGH_WORD_FIRST;
    uint16_t accelerationTime = 0, decelerationTime = 0, speedRpm = 0;
    Status prepareRelative(MoveContext&, uint32_t operationId, uint32_t targetBits,
                           uint64_t nowUs, uint64_t deadlineUs) const noexcept;
    Status prepareAbsolute(MoveContext&, uint32_t operationId, uint32_t targetBits,
                           uint64_t nowUs, uint64_t deadlineUs) const noexcept;
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
/** Preserve a multi-turn absolute target without normalization. An unwrapped
 * native target does not require a current-position reference; its displacement
 * remains unknown. Conversions, wrapped paths and applicable limits retain their
 * required metadata. A supplied reference must be fresh and stationary and cover
 * both write budgets. Unsupported preparation leaves output unchanged. */
Status prepareMoveAbsolute(MoveContext&, const AxisConfig&, const AxisReference*,
                           uint32_t operationId, const MoveRequest&, const MovePrerequisites&,
                           uint64_t nowUs, uint64_t deadlineUs,
                           const ActionOptions& = ActionOptions()) noexcept;
/** Resolve an explicitly wrapped angular path through preparePosition, then use
 * the same native absolute sequence. Same orientation/quantized zero rejects
 * without a write. Limits never cause an alternative revolution to be chosen. */
Status prepareMoveAngle(MoveContext&, const AxisConfig&, const AxisReference*,
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
inline Status prepareMoveAbsolute(ESS_RS::MoveContext& c, const AxisConfig& axis,
        const AxisReference* reference, uint32_t id, const MoveRequest& request,
        const ESS_RS::MovePrerequisites& prerequisites, uint64_t now, uint64_t deadline,
        const ActionOptions& options = ActionOptions()) noexcept {
    return ESS_RS::prepareMoveAbsolute(c, axis, reference, id, request, prerequisites, now, deadline, options);
}
inline Status prepareMoveAngle(ESS_RS::MoveContext& c, const AxisConfig& axis,
        const AxisReference* reference, uint32_t id, const MoveRequest& request,
        const ESS_RS::MovePrerequisites& prerequisites, uint64_t now, uint64_t deadline,
        const ActionOptions& options = ActionOptions()) noexcept {
    return ESS_RS::prepareMoveAngle(c, axis, reference, id, request, prerequisites, now, deadline, options);
}
} // namespace MotorControlRS
