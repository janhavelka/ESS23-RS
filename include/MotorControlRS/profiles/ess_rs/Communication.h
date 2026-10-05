/** @file Communication.h
 * @brief Explicit ESS communication commissioning; no transport, retries or save.
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "MotorControlRS/profiles/ess_rs/Actions.h"
#include "MotorControlRS/profiles/ess_rs/Reads.h"

namespace MotorControlRS { namespace ESS_RS {

enum class CommunicationField : uint8_t { ADDRESS, BAUD, FORMAT };
enum class CommunicationRequirement : uint8_t { REQUIRED, UNRESOLVED };
enum class CommunicationOutcome : uint8_t {
    NONE, ACKNOWLEDGED, ADDRESS_ACK_UNRESOLVED, CONFIRMED, READBACK_MISMATCH,
    REPLY_ERROR, TRANSPORT_ERROR, CANCELLED, DEADLINE, TIMING_UNQUALIFIED,
    UNCONFIRMED_RESPONSE
};
enum class CommunicationError : int32_t {
    INVALID_TARGET, INVALID_OPERATION, INVALID_DEADLINE, INVALID_CANDIDATE,
    INVALID_TUPLE, STALE_SETTINGS, PENDING_BASELINE, PREREQUISITES_REQUIRED, DIP_REQUIRED,
    INVALID_STATE, WRONG_CORRELATION, INVALID_EVENT, CLOCK_ERROR,
    DEADLINE_EXPIRED, TRANSPORT_FAILURE, CANCELLED, TIMING_UNQUALIFIED,
    UNCONFIRMED_RESPONSE, READBACK_MISMATCH, ADDRESS_ACK_UNRESOLVED
};
/** Exactly one field; unselected members are ignored. Address writes accept
 * only usable unicast endpoints 1..247, not the entire source register 0..255. */
struct CommunicationRequest {
    CommunicationField field = CommunicationField::ADDRESS;
    uint16_t address = 0;
    BaudRateCode baud = BaudRateCode::BAUD_115200;
    SerialFormatCode format = SerialFormatCode::FORMAT_8N1;
};
/** Application qualifications refer to this exact candidate and previous target.
 * An available route back includes actual restart/fixture control where needed;
 * a software host restore alone does not establish it. Stationary qualification
 * imposes no unwired external-I/O requirement. DIP qualification for ADDRESS
 * independently establishes the address switches OFF; raw DIP is not decoded.
 * Configuration block 0x0013/3 is re-parsed and age checked before any work.
 * Stored baud/format must match beforeSerial; pending older changes require
 * reconciliation before staging another change with only two candidates. */
struct CommunicationPrerequisites {
    ConfigObservation previous;
    ActiveSerialTuple beforeSerial;
    uint64_t maxAgeUs = 0; ///< Zero disables age expiry; checked binding/provenance and operation deadlines still apply.
    bool stationaryQualified = false, effectsQualified = false, routeBackQualified = false;
    bool addressDipOffQualified = false;
    CommunicationRequest qualifiedRequest;
};
struct CommunicationEvidence {
    ReadTarget target;
    ActiveSerialTuple serial;
    uint64_t eligibleUs = 0, deadlineUs = 0; ///< Immutable transaction budget, retained across confirmations.
    ActionEvidence wire;
    uint16_t readback = 0;
    bool readbackKnown = false;
};
/** Fixed caller-owned state. One write and at most two independently selected
 * non-changing confirmations. Terminal write outcome/evidence never disappear
 * when a confirmation is armed. Callers treat fields as read-only between calls.
 * Requirements are source requirements, never claims that save/restart happened.
 * Observed responding interface is separate from register readback and requested
 * activation. No confirmation proves persistence or identity by itself. */
struct CommunicationContext {
    ReadTarget beforeTarget, requestedTarget, confirmationTarget, observedActiveTarget;
    ActiveSerialTuple beforeSerial, requestedSerial, confirmationSerial, observedActiveSerial;
    uint32_t operationId = 0;
    CommunicationRequest request;
    ReadState state = ReadState::EMPTY;
    CommunicationOutcome outcome = CommunicationOutcome::NONE, writeOutcome = CommunicationOutcome::NONE;
    ActionExecution execution = ActionExecution::NOT_TRANSMITTED;
    uint16_t reg = 0, previous = 0, requested = 0, readback = 0;
    bool readbackKnown = false, observedActiveKnown = false;
    bool effects = false; ///< Any accepted/possibly sent write; retained even on a checked rejection.
    bool uncertain = false, activationUnknown = true;
    CommunicationRequirement restart = CommunicationRequirement::UNRESOLVED;
    CommunicationRequirement save = CommunicationRequirement::UNRESOLVED;
    uint8_t step = 0, confirmations = 0;
    uint64_t startedUs = 0, deadlineUs = 0, servicedUs = 0, eligibleUs = 0;
    CommunicationEvidence writeEvidence, confirmationEvidence[2];
    Status status;
};
struct PreparedCommunication {
    ActionWork kind = ActionWork::DONE;
    ReadTarget target;
    ActiveSerialTuple serial;
    uint32_t operationId = 0;
    uint8_t step = 0;
    bool write = false;
    uint16_t reg = 0, value = 0, count = 0;
    uint8_t bytes[READ_REQUEST_LEN] = {};
    std::size_t length = 0;
    uint64_t deadlineUs = 0;
};
/** Rejected calls preserve output and yield no traffic. */
Status prepareCommunication(CommunicationContext&, const ReadTarget&, uint32_t operationId,
                            const CommunicationRequest&, const CommunicationPrerequisites&,
                            uint64_t nowUs, uint64_t deadlineUs) noexcept;
/** Explicit read-only recovery/confirmation of either the exact before candidate
 * or requested candidate. The application must first settle/switch the host and
 * exclude competing producers. No automatic switch, scan, write replay, save or
 * restart. Each call consumes one of two confirmation slots, with its own finite
 * deadline. Both candidates retain the original operation's logical generation.
 * May follow lost ACK or failed confirmation, never an active transaction. */
Status prepareCommunicationConfirmation(CommunicationContext&, const ReadTarget& candidate,
                            const ActiveSerialTuple&, uint64_t nowUs, uint64_t deadlineUs) noexcept;
Status nextCommunication(const CommunicationContext&, uint64_t nowUs, PreparedCommunication&) noexcept;
/** Exact token/target and valid event envelope required; rejection is immutable.
 * ADDRESS never infers acknowledgement/activation from an old-address echo.
 * New-address write replies fail the original request expectation. Only explicit
 * confirmed FC03 establishes the selected responding context. A qualified final
 * closure may be delivered late; no automatic follow-up is yielded. */
Status advanceCommunication(CommunicationContext&, const ActionEvent&, uint64_t nowUs) noexcept;

}} // namespace MotorControlRS::ESS_RS
