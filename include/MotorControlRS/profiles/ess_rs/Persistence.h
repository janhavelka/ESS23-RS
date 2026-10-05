/** @file Persistence.h
 * @brief Explicit ESS save/factory restore and bounded evidence retention. No I/O.
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "MotorControlRS/profiles/ess_rs/Actions.h"
#include "MotorControlRS/profiles/ess_rs/Reads.h"

namespace MotorControlRS { namespace ESS_RS {

constexpr uint8_t PERSISTENCE_FIELD_COUNT = 15;
enum class PersistenceKind : uint8_t { SAVE, FACTORY_RESTORE };
enum class PersistenceKnowledge : uint8_t { UNVERIFIED, VERIFIED_FIELDS };
enum class PersistenceOutcome : uint8_t {
    NONE, ACKNOWLEDGED, REPLY_ERROR, TRANSPORT_ERROR, CANCELLED, DEADLINE,
    TIMING_UNQUALIFIED, UNCONFIRMED_RESPONSE
};
enum class PersistenceError : int32_t {
    INVALID_TARGET, INVALID_OPERATION, INVALID_DEADLINE, INVALID_STATE,
    INVALID_POLICY, STALE_EVIDENCE, STATIONARY_REQUIRED, BACKUP_REQUIRED,
    ROUTE_BACK_REQUIRED, WRONG_CORRELATION, INVALID_EVENT, CLOCK_ERROR,
    DEADLINE_EXPIRED, TRANSPORT_FAILURE, CANCELLED, TIMING_UNQUALIFIED,
    UNCONFIRMED_RESPONSE, INVALID_RESTART, IDENTITY_MISMATCH
};
/** Retained partial backup: the standard configuration read covers fifteen
 * words, not every parameter affected by save/restore. Unknown raw codes are
 * allowed. Source/target/time provenance is checked before yielding any write.
 * A complete external backup and actual recommissioning route are additionally
 * required for factory restore; neither is established by these snapshots. */
struct PersistencePrerequisites {
    ConfigObservation beforeConfig;
    IdentityObservation beforeIdentity;
    StateObservation stationary;
    ActiveSerialTuple beforeSerial;
    uint32_t configurationGeneration = 0;
    uint64_t maxAgeUs = 0; ///< Zero disables age expiry, not checked provenance or operation deadlines.
    bool stationaryQualified = false, effectsQualified = false;
    PersistenceKind qualifiedKind = PersistenceKind::SAVE;
    bool completeBackupQualified = false, routeBackQualified = false;
    uint32_t backupSourceId = 0;
};
/** Existing typed reads supply these observations. A live readback never proves
 * persistence. restartObserved requires an independently established actual
 * motor power cycle, its completion time and nonzero retained evidence token;
 * restarting the MCU or restoring the host UART does not qualify. */
struct PersistenceVerification {
    ConfigObservation config;
    IdentityObservation identity;
    StateObservation stationary;
    ActiveSerialTuple serial;
    uint32_t configurationGeneration = 0;
    uint64_t maxAgeUs = 0; ///< Zero disables age expiry; post-operation/restart observations remain mandatory.
    bool restartObserved = false;
    uint64_t restartUs = 0;
    uint32_t restartSourceId = 0;
};
struct PersistenceField {
    uint16_t reg = 0, before = 0, readback = 0;
    const char* sourceAccess = nullptr; ///< Static ledger notation, including RW/S; no automatic durability inference.
    bool readbackKnown = false, survivedRestart = false;
};
/** One caller-owned invocation yields exactly one write token. No sleep,
 * completion flag, retries, save/restore replay or automatic verification.
 * Treat fields as read-only between calls. Verification never replaces write
 * outcome/uncertainty. All-parameter durability is deliberately unavailable. */
struct PersistenceContext {
    ReadTarget target;
    uint32_t operationId = 0;
    PersistenceKind kind = PersistenceKind::SAVE;
    ReadState state = ReadState::EMPTY;
    PersistenceOutcome outcome = PersistenceOutcome::NONE;
    ActionExecution execution = ActionExecution::NOT_TRANSMITTED;
    PersistenceKnowledge persistence = PersistenceKnowledge::UNVERIFIED;
    PersistencePrerequisites before;
    PersistenceVerification verification;
    bool verificationKnown = false, liveReadbackKnown = false;
    bool configurationSnapshotComplete = false; ///< Always false: only the fifteen standard configuration words are retained here.
    bool effects = false, uncertain = false;
    bool restartRequiredForProof = true, manualInterventionRequired = false;
    bool communicationChanged = false, configurationInvalidated = false;
    uint16_t matchingFields = 0, verifiedFields = 0;
    uint8_t verificationCount = 0; ///< At most two explicit submissions; no extra bus work is generated.
    uint16_t reg = 0, value = 0;
    uint64_t startedUs = 0, deadlineUs = 0, servicedUs = 0;
    ActionEvidence writeEvidence;
    PersistenceField fields[PERSISTENCE_FIELD_COUNT];
    Status status;
};
struct PreparedPersistence {
    ActionWork kind = ActionWork::DONE;
    ReadTarget target;
    ActiveSerialTuple serial;
    uint32_t operationId = 0;
    uint8_t step = 0;
    bool write = false;
    uint16_t reg = 0, value = 0;
    uint8_t bytes[READ_REQUEST_LEN] = {};
    std::size_t length = 0;
    uint64_t deadlineUs = 0;
};
/** Rejected preparations preserve output. Prerequisites must not alias the
 * output's retained before member. Applications own exclusive commissioning
 * admission and invocation budgets beyond this single explicit operation. */
Status prepareSave(PersistenceContext&, const ReadTarget&, uint32_t operationId,
                   const PersistencePrerequisites&, uint64_t nowUs, uint64_t deadlineUs) noexcept;
Status prepareFactoryRestore(PersistenceContext&, const ReadTarget&, uint32_t operationId,
                             const PersistencePrerequisites&, uint64_t nowUs, uint64_t deadlineUs) noexcept;
Status nextPersistence(const PersistenceContext&, uint64_t nowUs, PreparedPersistence&) noexcept;
/** Exact target/token, checked echo and qualified source are required for ACK.
 * A lost/unconfirmed reply preserves possible execution and never yields replay.
 * Qualified on-time closure can be delivered after the absolute deadline. */
Status advancePersistence(PersistenceContext&, const ActionEvent&, uint64_t nowUs) noexcept;
/** Explicit submission of fresh identity/configuration/stopped-state evidence.
 * Same logical target/generation and responding host tuple must be retained;
 * configuration generation may advance after application cache invalidation.
 * Changed stored communications are reported; no active route is inferred.
 * For SAVE only, matching individual fields observed after a qualified actual
 * restart can become VERIFIED_FIELDS. Unknown/changed/unread parameters remain
 * outside that claim. This proves field survival, not that SAVE caused it;
 * an unknown write execution remains unknown. FACTORY_RESTORE never guesses factory defaults.
 * Rejection leaves context unchanged. Input must not alias retained verification. */
Status verifyPersistence(PersistenceContext&, const PersistenceVerification&, uint64_t nowUs) noexcept;

}} // namespace MotorControlRS::ESS_RS
