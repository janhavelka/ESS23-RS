/** @file ReadOperation.h
 * @brief Caller-supplied correlation and evidence for bounded read operations.
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <cstddef>
#include <stdint.h>

namespace MotorControlRS {

/** Application identity and binding generation; all three fields are immutable
 * during one operation. Address is the selected unicast wire endpoint. */
struct ReadTarget {
    uint32_t id = 0;
    uint8_t address = 0;
    uint32_t generation = 0;
};

enum class SerialParity : uint8_t { UNKNOWN, NONE, EVEN, ODD };
/** Independently observed active host tuple, not decoded stored drive settings.
 * known=false retains explicitly unknown evidence; preparing a read changes no
 * host or drive settings. */
struct ActiveSerialTuple {
    bool known = false;
    uint32_t baud = 0;
    uint8_t dataBits = 0;
    SerialParity parity = SerialParity::UNKNOWN;
    uint8_t stopBits = 0;
};

/** Caller declaration only. UNCONNECTED does not disable a drive assignment. */
enum class InputWiring : uint8_t { UNKNOWN, UNCONNECTED, CONNECTED };
/** Only capabilities consumed by the currently implemented non-changing reads. */
struct ReadCapabilities {
    bool probe = false;
    bool identity = false;
    bool config = false;
    bool state = false;
    uint8_t maxSteps = 0;
    uint8_t maxReplyBytes = 0;
};

enum class ReadEventKind : uint8_t { FRAME, TRANSPORT_FAILURE, CANCEL, DEADLINE };
/** FRAME memory is borrowed only for advanceRead. The application must correlate
 * its retained transaction before supplying this event. Qualified bounds enclose
 * frame closure, in the caller's monotonic microsecond time domain. Transport
 * failures, cancellation and deadlines may carry a captured prefix without
 * qualified closure; it is retained evidence, never decoded as a successful read.
 * All frame pointers designate valid length-byte storage. */
struct ReadEvent {
    ReadTarget target;
    uint32_t operationId = 0;
    uint8_t step = 0;
    ReadEventKind kind = ReadEventKind::FRAME;
    const uint8_t* frame = nullptr;
    std::size_t length = 0;
    bool qualified = false;
    uint64_t earliestUs = 0;
    uint64_t latestUs = 0;
    int32_t transportDetail = 0; ///< Application-defined transport evidence/reason.
    std::size_t txAccepted = 0; ///< Accepted request prefix, 0..8; not drive acknowledgement.
    bool executionUnknown = false; ///< Supplied application uncertainty, retained verbatim.
};

enum class ReadState : uint8_t { EMPTY, ACTIVE, SUCCEEDED, FAILED };
enum class ReadOutcome : uint8_t {
    NONE, SUCCESS, REPLY_ERROR, TRANSPORT_ERROR, CANCELLED, DEADLINE,
    TIMING_UNQUALIFIED
};
/** Numeric Status::detail reasons for sequencing/envelope errors. Codec failures
 * retain the codec's own Status and exception detail instead. */
enum class ReadError : int32_t {
    NONE, INVALID_TARGET, INVALID_OPERATION, INVALID_DEADLINE, INVALID_TUPLE,
    INVALID_WIRING, INVALID_STATE, WRONG_CORRELATION, INVALID_EVENT,
    CLOCK_ERROR, DEADLINE_EXPIRED, TRANSPORT_FAILURE, CANCELLED,
    TIMING_UNQUALIFIED, NOT_COMPLETE, WRONG_KIND
};

} // namespace MotorControlRS
