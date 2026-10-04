/** @file Actions.h
 * @brief Finite ESS enable, release, alarm/position clear and stop sequences. No I/O.
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "MotorControlRS/ActionOperation.h"
#include "MotorControlRS/profiles/ess_rs/Codec.h"

namespace MotorControlRS { namespace ESS_RS {

constexpr uint8_t ACTION_MAX_POLLS = 64;
constexpr std::size_t ACTION_MAX_REPLY_BYTES = 9;
enum class ActionWork : uint8_t { TRANSACTION, WAIT, DONE };
/** One copied transaction observation. receivedLength retains the full supplied
 * size when raw[] holds only a prefix. Invalid frames never update decoded data. */
struct ActionEvidence {
    uint8_t step = 0;
    ReadEventKind event = ReadEventKind::FRAME;
    uint8_t raw[ACTION_MAX_REPLY_BYTES] = {};
    std::size_t length = 0, receivedLength = 0, txAccepted = 0;
    bool txComplete = false, responseConfirmed = false, qualified = false;
    bool executionUnknown = false;
    uint64_t earliestUs = 0, latestUs = 0, deliveredUs = 0;
    int32_t transportDetail = 0;
    Status status;
    FrameError frameError = FrameError::NONE;
};
/** Caller-owned state; treat fields as read-only between calls. One command is
 * followed by bounded non-consuming observations: 0x000A/2 for position clear,
 * otherwise 0x0006/2. No command replay or
 * parameter staging occurs. step=0 is the write; positive tokens identify each
 * distinct observation, including waits before admission. */
struct ActionContext {
    ReadTarget target;
    uint32_t operationId = 0;
    ActionRequest request;
    ActionOptions options;
    ActionState state = ActionState::EMPTY;
    ActionOutcome outcome = ActionOutcome::NONE;
    ActionExecution execution = ActionExecution::NOT_TRANSMITTED;
    ActionCompletion completion = ActionCompletion::NOT_OBSERVED;
    uint64_t startedUs = 0, deadlineUs = 0, servicedUs = 0, eligibleUs = 0;
    uint8_t step = 0, polls = 0;
    uint16_t reg = 0, value = 0;
    bool observationKnown = false;
    uint16_t rawAlarm = 0, rawMotion = 0; ///< Last valid report, including unknown bits.
    uint32_t rawPosition = 0; ///< CLEAR_POSITION only: first wire word <<16 | second; zero check needs no word order. No signed-coordinate/origin proof.
    ActionEvidence writeEvidence, lastObservation, failureEvidence;
    Status status;
};
/** Repeated nextAction returns the same token. Application admission must avoid
 * duplicate TX and retain ordinary/urgent reservations and transaction storage.
 * WAIT owns no bus transaction. DONE yields no bytes even after failure. */
struct PreparedAction {
    ActionWork kind = ActionWork::DONE;
    ReadTarget target;
    uint32_t operationId = 0;
    uint8_t step = 0;
    uint8_t bytes[READ_REQUEST_LEN] = {};
    std::size_t length = 0;
    bool write = false;
    uint16_t reg = 0, value = 0, count = 0;
    uint64_t deadlineUs = 0, eligibleUs = 0;
};

/** Rejection leaves output unchanged. Stop has no position/origin/scale, idle,
 * alarm-free or home-input prerequisite. Normal stop uses the ramp established
 * before motion; direct stop is the documented ESS emergency command, never
 * release. No device queued-command guarantee is documented. Application policy
 * owns hardware qualification, target binding and external-input arbitration. */
Status prepareAction(ActionContext&, const ReadTarget&, uint32_t operationId,
                     const ActionRequest&, uint64_t nowUs, uint64_t deadlineUs,
                     const ActionOptions& = ActionOptions()) noexcept;
Status prepareEnable(ActionContext&, const ReadTarget&, uint32_t operationId,
                     uint64_t nowUs, uint64_t deadlineUs, const ActionOptions& = ActionOptions()) noexcept;
Status prepareRelease(ActionContext&, const ReadTarget&, uint32_t operationId,
                      uint64_t nowUs, uint64_t deadlineUs, const ActionOptions& = ActionOptions()) noexcept;
Status prepareClearAlarm(ActionContext&, const ReadTarget&, uint32_t operationId,
                         uint64_t nowUs, uint64_t deadlineUs, const ActionOptions& = ActionOptions()) noexcept;
/** Explicit ESS zero-only counter clear (auxiliary 0x0031 at 0x002D).
 * Requires caller-qualified semantics/stopped-state policy before preparation.
 * Nonzero positions reject without yielding work. The ACK is followed by new
 * checked current-position pair observations; zero establishes neither homing,
 * host origin, signed encoding nor feedback-to-command coordinate mapping. */
Status prepareSetDevicePosition(ActionContext&, const ReadTarget&, uint32_t operationId,
                                int64_t nativePosition, bool qualified,
                                uint64_t nowUs, uint64_t deadlineUs,
                                const ActionOptions& = ActionOptions()) noexcept;
Status prepareStop(ActionContext&, const ReadTarget&, uint32_t operationId, const StopPolicy&,
                   uint64_t nowUs, uint64_t deadlineUs, const ActionOptions& = ActionOptions()) noexcept;
Status prepareNormalStop(ActionContext&, const ReadTarget&, uint32_t operationId,
                         uint64_t nowUs, uint64_t deadlineUs, const ActionOptions& = ActionOptions()) noexcept;
Status prepareEmergencyStop(ActionContext&, const ReadTarget&, uint32_t operationId,
                            uint64_t nowUs, uint64_t deadlineUs, const ActionOptions& = ActionOptions()) noexcept;
Status nextAction(const ActionContext&, uint64_t nowUs, PreparedAction&) noexcept;
/** Invalid envelopes/correlation leave the context unchanged. OK consumes an
 * event and can produce failure/unknown execution. A confirmed checked echo is
 * acknowledgement only; later checked status establishes reported completion.
 * Local cancellation never transmits a stop or clears command uncertainty. */
Status advanceAction(ActionContext&, const ActionEvent&, uint64_t nowUs) noexcept;

}} // namespace MotorControlRS::ESS_RS

namespace MotorControlRS {
// Common and native routes share the same concrete ESS operation and policies.
// Other profiles require their own implementation; no registry or transport API.
inline Status prepareEnable(ESS_RS::ActionContext& c, const ReadTarget& t, uint32_t id,
                            uint64_t now, uint64_t deadline, const ActionOptions& options = ActionOptions()) noexcept {
    return ESS_RS::prepareEnable(c, t, id, now, deadline, options);
}
inline Status prepareRelease(ESS_RS::ActionContext& c, const ReadTarget& t, uint32_t id,
                             uint64_t now, uint64_t deadline, const ActionOptions& options = ActionOptions()) noexcept {
    return ESS_RS::prepareRelease(c, t, id, now, deadline, options);
}
inline Status prepareClearAlarm(ESS_RS::ActionContext& c, const ReadTarget& t, uint32_t id,
                                uint64_t now, uint64_t deadline, const ActionOptions& options = ActionOptions()) noexcept {
    return ESS_RS::prepareClearAlarm(c, t, id, now, deadline, options);
}
inline Status prepareSetDevicePosition(ESS_RS::ActionContext& c, const ReadTarget& t, uint32_t id,
        int64_t nativePosition, bool qualified, uint64_t now, uint64_t deadline,
        const ActionOptions& options = ActionOptions()) noexcept {
    return ESS_RS::prepareSetDevicePosition(c, t, id, nativePosition, qualified, now, deadline, options);
}
inline Status prepareStop(ESS_RS::ActionContext& c, const ReadTarget& t, uint32_t id,
                          const StopPolicy& policy, uint64_t now, uint64_t deadline,
                          const ActionOptions& options = ActionOptions()) noexcept {
    return ESS_RS::prepareStop(c, t, id, policy, now, deadline, options);
}
} // namespace MotorControlRS
