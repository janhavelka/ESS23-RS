/** @file ActionOperation.h
 * @brief Explicit action policies and caller-supplied transaction evidence.
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "MotorControlRS/ReadOperation.h"

namespace MotorControlRS {

enum class ActionKind : uint8_t { ENABLE, RELEASE, CLEAR_ALARM, STOP, CLEAR_POSITION };
enum class StopBehavior : uint8_t { UNSPECIFIED, CONFIGURED_DECELERATION, DIRECT };
/** UNSPECIFIED explicitly requests no device-queue guarantee. It does not mean
 * preserve or discard. Host continuation cancellation is application-owned. */
enum class DeviceQueue : uint8_t { UNSPECIFIED, PRESERVE, DISCARD };
struct StopPolicy {
    StopBehavior behavior = StopBehavior::UNSPECIFIED;
    DeviceQueue deviceQueue = DeviceQueue::UNSPECIFIED;
    bool customDeceleration = false; ///< Unsupported ESS policy; never silently ignored.
};
struct ActionRequest {
    ActionKind kind = ActionKind::ENABLE;
    StopPolicy stop;
    int64_t devicePosition = 0; ///< ESS permits an explicit zero clear only, never an arbitrary counter setter.
    bool positionClearQualified = false; ///< Caller verified clear semantics, target and stopped-state prerequisites.
};
/** Finite observation scheduling, supplied by the application. The interval is
 * a polling policy, not a vendor-guaranteed action or stop latency. ESS actions
 * and finite positioning permit read-only observations after a checked FC06
 * frame even when its source remains unconfirmed; execution stays UNKNOWN.
 * Read observations and FC10 staging still require confirmed response evidence.
 * No policy infers acknowledgement from identical bytes or replays a write. */
struct ActionOptions {
    uint32_t pollIntervalUs = 10000;
    uint8_t maxPolls = 20;

};
/** Response storage is borrowed only for advanceAction. responseConfirmed must
 * mean the application excluded local echo/foreign/late-response ambiguity;
 * identical FC06 bytes alone cannot establish that fact. txComplete separately
 * reports physical request completion, not merely acceptance by a UART. */
struct ActionEvent {
    ReadEvent transport;
    bool responseConfirmed = false;
    bool txComplete = false;
};
enum class ActionState : uint8_t { EMPTY, ACTIVE, SUCCEEDED, FAILED };
enum class ActionExecution : uint8_t { NOT_TRANSMITTED, ACKNOWLEDGED, REJECTED, UNKNOWN };
/** OBSERVED is a checked drive status report, not an independent physical
 * measurement of winding current, shaft standstill or stopping latency. */
enum class ActionCompletion : uint8_t { NOT_OBSERVED, OBSERVED };
enum class ActionOutcome : uint8_t {
    NONE, OBSERVED, REPLY_ERROR, TRANSPORT_ERROR, CANCELLED, DEADLINE,
    TIMING_UNQUALIFIED, UNCONFIRMED_RESPONSE, OBSERVATION_LIMIT,
    ACKNOWLEDGED ///< Command-only sequence acknowledged; physical completion remains NOT_OBSERVED.
};
enum class ActionError : int32_t {
    NONE, INVALID_TARGET, INVALID_OPERATION, INVALID_DEADLINE, INVALID_POLICY,
    UNSUPPORTED_POLICY, INVALID_OPTIONS, INVALID_STATE, WRONG_CORRELATION,
    INVALID_EVENT, CLOCK_ERROR, DEADLINE_EXPIRED, TRANSPORT_FAILURE, CANCELLED,
    TIMING_UNQUALIFIED, UNCONFIRMED_RESPONSE, OBSERVATION_LIMIT
};

} // namespace MotorControlRS
