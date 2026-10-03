/** @file Reads.h
 * @brief Non-changing ESS identity and configuration operations. No I/O.
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "MotorControlRS/ReadOperation.h"
#include "MotorControlRS/Units.h"
#include "MotorControlRS/profiles/ess_rs/Codec.h"

namespace MotorControlRS { namespace ESS_RS {

constexpr std::size_t READ_MAX_STEPS = 5;
constexpr std::size_t READ_MAX_REPLY_BYTES = 37;
constexpr std::size_t READ_INPUT_COUNT = 4;
enum class ReadKind : uint8_t { IDENTITY, CONFIG };

/** Copied wire provenance for each consumed step, including a failing reply.
 * Failed and partial operations retain this evidence without publishing a new
 * decoded observation. deliveredUs is application servicing, not freshness. */
struct ReadStepObservation {
    uint16_t first = 0, count = 0;
    ReadEventKind event = ReadEventKind::FRAME;
    uint8_t raw[READ_MAX_REPLY_BYTES] = {};
    std::size_t length = 0, receivedLength = 0; ///< Copied prefix and full supplied size.
    bool qualified = false;
    uint64_t earliestUs = 0, latestUs = 0, deliveredUs = 0;
    int32_t transportDetail = 0; ///< Original application transport reason, if supplied.
    std::size_t txAccepted = 0;
    bool executionUnknown = false;
    Status status;
    FrameError frameError = FrameError::NONE;
};

/** Fixed caller-owned storage. Treat fields as read-only between API calls.
 * Preparation snapshots target, active transport and declared wiring. One fixed
 * absolute deadline covers every step; no retries or renewed budget exist. */
struct ReadContext {
    ReadTarget target;
    uint32_t operationId = 0;
    ReadKind kind = ReadKind::IDENTITY;
    ReadState state = ReadState::EMPTY;
    ReadOutcome outcome = ReadOutcome::NONE;
    uint64_t startedUs = 0, deadlineUs = 0, servicedUs = 0;
    uint8_t step = 0, completedSteps = 0;
    Status status;
    ActiveSerialTuple activeSerial;
    InputWiring wiring[READ_INPUT_COUNT] = {};
    ReadStepObservation observations[READ_MAX_STEPS];
};

/** Repeated nextRead yields the same step; application admission prevents
 * duplicate transmission. Bytes are an FC03 request, never a motor action. */
struct PreparedRead {
    ReadTarget target;
    uint32_t operationId = 0;
    uint8_t step = 0;
    uint16_t first = 0, count = 0;
    uint8_t bytes[READ_REQUEST_LEN] = {};
    std::size_t length = 0;
    uint64_t deadlineUs = 0;
};

enum class ReadResolution : uint8_t {
    RESOLVED, UNKNOWN_CODE, MODEL_MAPPING_UNRESOLVED,
    VERSION_MAPPING_UNRESOLVED, DIP_MAPPING_UNRESOLVED,
    SCALE_UNRESOLVED, ZERO_ENCODER_SCALE
};

/** The raw codes are evidence, not a guessed model or firmware interpretation. */
struct IdentityObservation {
    ReadTarget target;
    uint32_t operationId = 0;
    uint16_t rawModel = 0, rawVersion = 0, rawActiveNode = 0, rawDip = 0;
    bool activeNodeKnown = false;
    uint8_t activeNode = 0;
    ReadResolution modelResolution = ReadResolution::MODEL_MAPPING_UNRESOLVED;
    ReadResolution versionResolution = ReadResolution::VERSION_MAPPING_UNRESOLVED;
    ReadResolution dipResolution = ReadResolution::DIP_MAPPING_UNRESOLVED;
    RegisterIssue dipIssues = RegisterIssue::NONE;
    ActiveSerialTuple activeSerial;
    ReadStepObservation provenance;
};

/** Stored/readback register codes, separate from observed activeSerial. */
struct RawConfig {
    uint16_t direction = 0, subdivision = 0;
    uint16_t customNode = 0, baud = 0, format = 0;
    uint16_t overLimitStop = 0, softLimitEnable = 0, wordOrder = 0;
    uint16_t inputPolarity = 0, inputFunctions[READ_INPUT_COUNT] = {};
    uint16_t algorithm = 0, encoderResolution = 0;
};

/** Consult each known flag before using its typed enum. Unknown codes remain a
 * successful read. Input levels are explicitly unavailable in this operation.
 * units contains only configured encoder READBACK metadata when nonzero; command
 * subdivision remains unresolved and is never automatically promoted to a scale.
 * Encoder metadata proves no physical resolution, IC, interface or readiness. */
struct ConfigObservation {
    ReadTarget target;
    uint32_t operationId = 0;
    RawConfig raw;
    bool directionKnown = false, baudKnown = false, formatKnown = false;
    bool overLimitStopKnown = false, softLimitEnableKnown = false, wordOrderKnown = false;
    bool algorithmKnown = false, inputFunctionKnown[READ_INPUT_COUNT] = {};
    DefaultDirection direction = DefaultDirection::NORMAL;
    BaudRateCode baud = BaudRateCode::BAUD_115200;
    SerialFormatCode format = SerialFormatCode::FORMAT_8N1;
    OverLimitStop overLimitStop = OverLimitStop::FREE_PARKING;
    SoftLimitEnable softLimitEnable = SoftLimitEnable::LIMITS_OFF;
    WordOrder wordOrder = WordOrder::HIGH_WORD_FIRST;
    ControlAlgorithm algorithm = ControlAlgorithm::OPEN_LOOP;
    InputFunction inputFunctions[READ_INPUT_COUNT] = {};
    bool inputInverted[READ_INPUT_COUNT] = {};
    uint16_t unknownPolarityBits = 0;
    InputWiring wiring[READ_INPUT_COUNT] = {};
    bool inputLevelKnown[READ_INPUT_COUNT] = {}, inputLevel[READ_INPUT_COUNT] = {};
    RegisterIssue subdivisionIssues = RegisterIssue::SCALE_UNRESOLVED;
    RegisterIssue customNodeIssues = RegisterIssue::MODEL_APPLICABILITY;
    RegisterIssue overLimitStopIssues = RegisterIssue::SEMANTICS_UNRESOLVED;
    RegisterIssue softLimitIssues = RegisterIssue::SOURCE_CONFLICT;
    ReadResolution subdivisionResolution = ReadResolution::SCALE_UNRESOLVED;
    ReadResolution encoderResolution = ReadResolution::ZERO_ENCODER_SCALE;
    UnitConfig units;
    ActiveSerialTuple activeSerial;
    ReadStepObservation provenance[READ_MAX_STEPS];
};

ReadCapabilities readCapabilities() noexcept;
/** Output context stays unchanged if arguments are rejected. IDs/generation
 * must be nonzero and address unicast; deadline must be strictly after nowUs. */
Status prepareIdentity(ReadContext& output, const ReadTarget& target, uint32_t operationId,
                       uint64_t nowUs, uint64_t deadlineUs,
                       const ActiveSerialTuple& activeSerial = ActiveSerialTuple()) noexcept;
Status prepareConfig(ReadContext& output, const ReadTarget& target, uint32_t operationId,
                     uint64_t nowUs, uint64_t deadlineUs,
                     const ActiveSerialTuple& activeSerial = ActiveSerialTuple(),
                     const InputWiring* wiring = nullptr) noexcept;
/** Output unchanged on failure. Expiry refuses more traffic; supply a DEADLINE
 * event to terminalize an operation with no retained on-time completion. */
Status nextRead(const ReadContext&, uint64_t nowUs, PreparedRead& output) noexcept;
/** Rejected malformed envelopes/wrong tokens leave context unchanged. OK means
 * the correlated event was consumed: inspect state/outcome/status for success or
 * terminal failure. Qualified final closure <= deadline may be delivered later;
 * a partial completion serviced at/after deadline terminates without more work.
 * CANCEL is local cancellation, never a motor stop. No borrowed pointer remains. */
Status advanceRead(ReadContext&, const ReadEvent&, uint64_t nowUs) noexcept;
/** Atomic publication only after the complete successful operation. The output
 * is unchanged on any error, partial result or wrong operation kind. */
Status getIdentity(const ReadContext&, IdentityObservation& output) noexcept;
Status getConfig(const ReadContext&, ConfigObservation& output) noexcept;

}} // namespace MotorControlRS::ESS_RS
