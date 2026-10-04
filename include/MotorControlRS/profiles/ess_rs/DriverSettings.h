/** @file DriverSettings.h
 * @brief Bounded ESS settings reads and stopped-state updates. No I/O or retries.
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "MotorControlRS/profiles/ess_rs/Actions.h"

namespace MotorControlRS { namespace ESS_RS {

constexpr uint8_t DRIVER_FIELD_COUNT = 16;
constexpr uint8_t DRIVER_READ_STEPS = 4;
constexpr uint8_t DRIVER_MAX_STEPS = 18;
constexpr std::size_t DRIVER_MAX_REPLY_BYTES = 15;
enum class DriverGroup : uint8_t { DRIVE, IO, POSITION_SEGMENT, SPEED_SEGMENT, SEGMENT_START_SPEED };
enum class DriverField : uint32_t {
    DIRECTION = 1, SUBDIVISION = 2, WORD_ORDER = 4, SOFT_LIMIT_ENABLE = 8,
    OVER_LIMIT_STOP = 16, INTERRUPTION = 32, POSITION_MODE = 64,
    POSITIVE_LIMIT = 128, NEGATIVE_LIMIT = 256,
    INPUT_POLARITY = 1u << 9, INPUT_X0 = 1u << 10, INPUT_X1 = 1u << 11,
    INPUT_X2 = 1u << 12, INPUT_X3 = 1u << 13, OUTPUT_POLARITY = 1u << 14,
    OUTPUT_Y0 = 1u << 15, OUTPUT_Y1 = 1u << 16, CUSTOM_OUTPUT = 1u << 17,
    SEGMENT_SPEED = 1u << 18, SEGMENT_ACCELERATION = 1u << 19,
    SEGMENT_DECELERATION = 1u << 20, SEGMENT_START_SPEED = 1u << 21,
    SEGMENT_PULSE_TARGET = 1u << 22
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
    READBACK_MISMATCH, NOT_COMPLETE, WIRING_REQUIRED, IO_EVIDENCE_REQUIRED,
    IO_EFFECTS_REQUIRED, OUTPUT_FUNCTION_UNRESOLVED, CUSTOM_DEPENDENCY,
    SEGMENT_INDEX, SEGMENT_SIGN_UNRESOLVED, TRIGGER_POLICY_REQUIRED
};
/** Select fields explicitly; values in unselected fields are ignored. Pair
 * candidates are present for explicit unavailable reporting, never split writes.
 * interruption selects the external PV trigger level/edge, not serial bit 3. */
struct DriverRequest {
    DriverGroup group = DriverGroup::DRIVE;
    uint32_t fields = 0;
    uint32_t configurationGeneration = 0;
    DefaultDirection direction = DefaultDirection::NORMAL;
    uint16_t subdivision = 400;
    WordOrder wordOrder = WordOrder::HIGH_WORD_FIRST;
    SoftLimitEnable softLimitEnable = SoftLimitEnable::LIMITS_OFF;
    OverLimitStop overLimitStop = OverLimitStop::FREE_PARKING;
    PvTriggerMode interruption = PvTriggerMode::LEVEL;
    PositionMode positionMode = PositionMode::RELATIVE;
    int64_t positiveLimit = 0, negativeLimit = 0;
    InputFunction inputFunctions[4] = {};
    OutputFunction outputFunctions[2] = {};
    uint16_t inputPolarity = 0, outputPolarity = 0, customOutput = 0;
    uint8_t segmentIndex = 1; ///< Stored record number, 1..16.
    int32_t segmentSpeed = 0, segmentStartSpeed = 0;
    uint16_t segmentAcceleration = 0, segmentDeceleration = 0; ///< Native time words, not physical acceleration.
    int64_t segmentPulseTarget = 0; ///< Explicit unsupported pair-write candidate.
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
 * external position mode, input polarity, four input functions, output polarity,
 * two output functions and custom-output mask. DRIVE and IO populate separate
 * groups. knownFields means resolved legal codes, never active settings.
 * Limits assemble unsigned bits only; signed meaning/native scale are unresolved. */
struct DriverObservation {
    DriverGroup group = DriverGroup::DRIVE;
    ReadTarget target;
    uint32_t operationId = 0, configurationGeneration = 0;
    uint16_t raw[DRIVER_FIELD_COUNT] = {};
    uint8_t segmentIndex = 1; ///< Segment groups: raw[0..2] speed/ramp/ramp; PT raw[3..4] pulse words.
    uint32_t knownFields = 0;
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
    InputWiring inputWiring[4] = {}, outputWiring[2] = {};
    bool ioLevelsQualified = false;
    ReadTarget ioTarget;
    uint32_t ioConfigurationGeneration = 0;
    uint16_t actualInputs = 0, actualOutputs = 0;
    uint64_t ioEarliestUs = 0, ioLatestUs = 0;
    /** Independent application qualification of the exact candidate and its
     * external effects. An asserted input may be reinterpreted by reassignment.
     * UNCONNECTED is distinct from a disabled function; none proves no output
     * electrical level and does not establish enable/release precedence. */
    uint32_t ioEffectsQualifiedFields = 0;
    DriverRequest qualifiedIo;
    /** IO verification policy for known UNCONNECTED affected terminals, or
     * segment configuration with independently qualified trigger inhibition.
     * A checked, on-time unconfirmed write echo may lead to a separate confirmed
     * readback. It never becomes an acknowledgement or proves activation. */
    bool allowEchoReadback = false;
    /** Independent qualification that external execution cannot occur during
     * this exact segment candidate. No input is silently disabled. */
    bool externalTriggerInhibitedQualified = false;
    ReadTarget triggerTarget;
    uint32_t triggerConfigurationGeneration = 0;
    uint64_t triggerEarliestUs = 0, triggerLatestUs = 0;
    DriverRequest qualifiedSegment;
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
 * are at most nine writes and nine reads (seven for DRIVE). Accepted writes retain effects and
 * uncertainty even when cancellation, expiry or a later read fails. Never retry
 * or rollback automatically. Historical previous/evidence keep original binding. */
struct DriverContext {
    DriverGroup group = DriverGroup::DRIVE;
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
    uint32_t effects = 0; ///< Possibly changed field mask; never cleared on failure.
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
                         uint32_t configurationGeneration, uint64_t nowUs, uint64_t deadlineUs,
                         DriverGroup group = DriverGroup::DRIVE, uint8_t segmentIndex = 1) noexcept;
Status prepareDriverSettings(DriverContext&, const ReadTarget&, uint32_t operationId,
                             const DriverRequest&, const DriverPrerequisites&,
                             uint64_t nowUs, uint64_t deadlineUs) noexcept;
/** Repeated calls yield the same token; admission/axis reservation are owned by
 * the application. Absolute deadline and readiness bounds are never renewed. */
Status nextDriver(const DriverContext&, uint64_t nowUs, PreparedDriver&) noexcept;
/** Invalid correlations/envelopes leave state unchanged. Qualified on-time final
 * completion can succeed when delivered later. A nonfinal late delivery cannot
 * start another transaction. Reads and update readbacks require responseConfirmed;
 * only explicit unconnected-IO or inhibited-segment readback policy can continue an unconfirmed
 * source write echo. CANCEL is local and never restores settings. */
Status advanceDriver(DriverContext&, const ActionEvent&, uint64_t nowUs) noexcept;
/** Only a complete READ publishes, leaving output unchanged on all failures. */
Status getDriver(const DriverContext&, DriverObservation&) noexcept;

/** Logical raw/progress indices 0..6 are DRIVE, 7..15 are IO; pairs have no
 * logical writable index. Segment groups use local indices 0..2 (shared start
 * speed only 0). An out-of-range index returns the empty field. */
DriverField driverFieldAt(uint8_t index, DriverGroup group = DriverGroup::DRIVE) noexcept;
/** These setters select the same checked update engine; UNDEFINED explicitly
 * assigns function zero. Bounds/choices fail without changing the request. */
Status prepareInputFunction(DriverRequest&, uint8_t terminal, InputFunction) noexcept;
Status prepareOutputFunction(DriverRequest&, uint8_t terminal, OutputFunction) noexcept;

/** Stored settings only: no active/persistence/electrical-level assertion.
 * Complete read provenance and unknown/reserved raw values are retained. */
struct IoObservation {
    ReadTarget target;
    uint32_t operationId = 0, configurationGeneration = 0;
    uint16_t inputPolarity = 0, outputPolarity = 0, customOutput = 0;
    InputFunction inputFunctions[4] = {};
    OutputFunction outputFunctions[2] = {};
    uint16_t rawInputFunctions[4] = {}, rawOutputFunctions[2] = {};
    uint8_t knownInputFunctions = 0, knownOutputFunctions = 0;
    uint16_t unknownInputPolarityBits = 0, unknownOutputPolarityBits = 0,
             unknownCustomOutputBits = 0;
    DriverEvidence provenance[3];
};
Status getIo(const DriverContext&, IoObservation&) noexcept;

}} // namespace MotorControlRS::ESS_RS
