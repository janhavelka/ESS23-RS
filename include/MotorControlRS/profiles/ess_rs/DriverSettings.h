/** @file DriverSettings.h
 * @brief Bounded ESS settings reads and stopped-state updates. No I/O or retries.
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "MotorControlRS/profiles/ess_rs/Actions.h"

namespace MotorControlRS { namespace ESS_RS {

constexpr uint8_t DRIVER_FIELD_COUNT = 7;
constexpr uint8_t DRIVER_READ_STEPS = 4;
constexpr uint8_t DRIVER_MAX_STEPS = 14;
constexpr std::size_t DRIVER_MAX_REPLY_BYTES = 13;
enum class DriverField : uint16_t {
    DIRECTION = 1, SUBDIVISION = 2, WORD_ORDER = 4, SOFT_LIMIT_ENABLE = 8,
    OVER_LIMIT_STOP = 16, INTERRUPTION = 32, POSITION_MODE = 64,
    POSITIVE_LIMIT = 128, NEGATIVE_LIMIT = 256
};
enum class DriverKind : uint8_t { READ, UPDATE };
enum class DriverOutcome : uint8_t {
    NONE, SUCCESS, REPLY_ERROR, TRANSPORT_ERROR, CANCELLED, DEADLINE,
    TIMING_UNQUALIFIED, UNCONFIRMED_RESPONSE, READBACK_MISMATCH
};
enum class DriverError : int32_t {
    INVALID_TARGET, INVALID_OPERATION, INVALID_DEADLINE, INVALID_STATE,
    INVALID_CANDIDATE, WRONG_CORRELATION, INVALID_EVENT, CLOCK_ERROR,
    STALE_SETTINGS, STATIONARY_REQUIRED, LIMIT_REFERENCE_REQUIRED,
    LIMIT_DEPENDENCY, PAIR_WRITE_UNSUPPORTED, DEADLINE_EXPIRED,
    TRANSPORT_FAILURE, CANCELLED, TIMING_UNQUALIFIED, UNCONFIRMED_RESPONSE,
    READBACK_MISMATCH, NOT_COMPLETE
};
/** Select fields explicitly; values in unselected fields are ignored. Pair
 * candidates are present for explicit unavailable reporting, never split writes.
 * interruption selects the external PV trigger level/edge, not serial bit 3. */
struct DriverRequest {
    uint16_t fields = 0;
    uint32_t configurationGeneration = 0;
    DefaultDirection direction = DefaultDirection::NORMAL;
    uint16_t subdivision = 400;
    WordOrder wordOrder = WordOrder::HIGH_WORD_FIRST;
    SoftLimitEnable softLimitEnable = SoftLimitEnable::LIMITS_OFF;
    OverLimitStop overLimitStop = OverLimitStop::FREE_PARKING;
    PvTriggerMode interruption = PvTriggerMode::LEVEL;
    PositionMode positionMode = PositionMode::RELATIVE;
    int64_t positiveLimit = 0, negativeLimit = 0;
};
/** Copied closure/transport and checked-parser evidence. Raw prefix retains the
 * full supplied size separately. Delivery is not observation freshness. */
struct DriverEvidence {
    uint8_t step = 0;
    uint16_t reg = 0, count = 0;
    bool write = false;
    ReadEventKind event = ReadEventKind::FRAME;
    uint8_t raw[DRIVER_MAX_REPLY_BYTES] = {};
    std::size_t length = 0, receivedLength = 0, txAccepted = 0;
    bool txComplete = false, responseConfirmed = false, qualified = false;
    bool executionUnknown = false;
    uint64_t attemptedUs = 0; ///< Step eligibility bounds observation age; closure alone does not.
    uint64_t earliestUs = 0, latestUs = 0, deliveredUs = 0;
    int32_t transportDetail = 0;
    Status status;
    FrameError frameError = FrameError::NONE;
};
/** Whole successful read, not an atomic device snapshot. raw[] order is
 * direction, subdivision, word-order, soft-enable, over-limit, PV trigger,
 * external position mode. knownFields means legal codes, never active settings.
 * Limits assemble unsigned bits only; signed meaning/native scale are unresolved. */
struct DriverObservation {
    ReadTarget target;
    uint32_t operationId = 0, configurationGeneration = 0;
    uint16_t raw[DRIVER_FIELD_COUNT] = {};
    uint16_t knownFields = 0;
    uint16_t positiveWords[2] = {}, negativeWords[2] = {};
    bool pairKnown = false;
    uint32_t positiveBits = 0, negativeBits = 0;
    DriverEvidence provenance[DRIVER_READ_STEPS];
};
/** Application-supplied qualifications bound to the exact stopped observation.
 * Enabling soft limits additionally needs qualified native bounds, homing and
 * a reference from the same generation. Raw limit reads alone prove none of it.
 * No input function is silently changed or treated as disabled. */
struct DriverPrerequisites {
    uint32_t configurationGeneration = 0;
    DriverObservation previous;
    bool stationaryQualified = false, inputsPermit = false;
    ReadTarget stationaryTarget;
    uint16_t rawAlarm = 0, rawMotion = 0;
    uint64_t stationaryEarliestUs = 0, stationaryLatestUs = 0, maxAgeUs = 0;
    bool limitSemanticsQualified = false, homedReferenceQualified = false;
    ReadTarget referenceTarget;
    uint32_t referenceConfigurationGeneration = 0;
    /** Caller-qualified decoding must map these exact retained raw words to
     * the native signed bounds below. Neither a cast nor a host origin is proof. */
    uint16_t qualifiedPositiveWords[2] = {}, qualifiedNegativeWords[2] = {};
    int64_t positiveLimit = 0, negativeLimit = 0;
};
/** Each selected field's exact progress. Active/persistence remain unknown:
 * checked echo and matching readback do not document activation or rollback. */
struct DriverProgress {
    DriverField field = DriverField::DIRECTION;
    uint16_t reg = 0, previous = 0, requested = 0, readback = 0;
    bool selected = false, acknowledged = false, readbackKnown = false;
    bool activeKnown = false;
    uint16_t active = 0;
    ActionExecution execution = ActionExecution::NOT_TRANSMITTED;
};
/** Caller-owned bounded state; treat fields as read-only between calls. There
 * are at most seven writes and seven reads. Accepted writes retain effects and
 * uncertainty even when cancellation, expiry or a later read fails. Never retry
 * or rollback automatically. Historical previous/evidence keep original binding. */
struct DriverContext {
    ReadTarget target;
    uint32_t operationId = 0, configurationGeneration = 0;
    DriverKind kind = DriverKind::READ;
    ReadState state = ReadState::EMPTY;
    DriverOutcome outcome = DriverOutcome::NONE;
    DriverRequest request;
    DriverPrerequisites prerequisites;
    uint64_t startedUs = 0, deadlineUs = 0, servicedUs = 0, eligibleUs = 0;
    uint8_t step = 0, completedSteps = 0, fieldCount = 0;
    uint8_t order[DRIVER_FIELD_COUNT] = {};
    DriverProgress progress[DRIVER_FIELD_COUNT];
    DriverEvidence observations[DRIVER_MAX_STEPS];
    uint16_t effects = 0; ///< Possibly changed field mask; never cleared on failure.
    bool uncertain = false; ///< A write lacks confirmed matching readback.
    Status status;
};
struct PreparedDriver {
    ActionWork kind = ActionWork::DONE;
    ReadTarget target;
    uint32_t operationId = 0;
    uint8_t step = 0;
    bool write = false;
    uint16_t reg = 0, value = 0, count = 0;
    uint8_t bytes[READ_REQUEST_LEN] = {};
    std::size_t length = 0;
    uint64_t deadlineUs = 0;
};

/** All rejected preparations leave output unchanged, and yield no traffic.
 * Request/prerequisite inputs must not alias the output's embedded members;
 * this is checked before mutation and avoids a full-context stack copy. */
Status prepareDriverRead(DriverContext&, const ReadTarget&, uint32_t operationId,
                         uint32_t configurationGeneration, uint64_t nowUs, uint64_t deadlineUs) noexcept;
Status prepareDriverSettings(DriverContext&, const ReadTarget&, uint32_t operationId,
                             const DriverRequest&, const DriverPrerequisites&,
                             uint64_t nowUs, uint64_t deadlineUs) noexcept;
/** Repeated calls yield the same token; admission/axis reservation are owned by
 * the application. Absolute deadline and readiness bounds are never renewed. */
Status nextDriver(const DriverContext&, uint64_t nowUs, PreparedDriver&) noexcept;
/** Invalid correlations/envelopes leave state unchanged. Qualified on-time final
 * completion can succeed when delivered later. A nonfinal late delivery cannot
 * start another transaction. CANCEL is local and never restores settings. */
Status advanceDriver(DriverContext&, const ActionEvent&, uint64_t nowUs) noexcept;
/** Only a complete READ publishes, leaving output unchanged on all failures. */
Status getDriver(const DriverContext&, DriverObservation&) noexcept;

}} // namespace MotorControlRS::ESS_RS
