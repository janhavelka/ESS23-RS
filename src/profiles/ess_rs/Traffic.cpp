// SPDX-License-Identifier: MIT
#include "MotorControlRS/profiles/ess_rs/Traffic.h"
#include <cstring>

namespace MotorControlRS { namespace ESS_RS {
namespace {
uint16_t word(const uint8_t* data) noexcept {
    return static_cast<uint16_t>((static_cast<uint16_t>(data[0]) << 8) | data[1]);
}
Status bad(FrameError value, FrameError* error, const char* message) noexcept {
    if (error) *error = value;
    return Status(value == FrameError::CRC ? Err::CRC_ERROR : Err::FRAME_ERROR, 0, message);
}
Status requestFields(const TrafficRecord& record, TrafficDecoded& decoded,
                     FrameError* error) noexcept {
    if (record.kind != TrafficKind::TX || !record.complete)
        return bad(FrameError::ARGUMENT, error, "complete TX request required");
    const std::size_t length = record.length;
    const uint8_t* frame = record.bytes;
    if (length < 8 || length > TRAFFIC_MAX_BYTES)
        return bad(FrameError::LENGTH, error, "request length");
    if (!isValidAddress(frame[0])) return bad(FrameError::ADDRESS, error, "request address");
    if (calcCrc16(frame, length - 2) != static_cast<uint16_t>(frame[length - 2] |
        (static_cast<uint16_t>(frame[length - 1]) << 8)))
        return bad(FrameError::CRC, error, "request CRC");
    decoded.address = frame[0]; decoded.function = frame[1]; decoded.start = word(frame + 2);
    Status status;
    if (frame[1] == 3) {
        if (length != READ_REQUEST_LEN) return bad(FrameError::LENGTH, error, "FC03 request length");
        decoded.count = word(frame + 4);
        status = validateReadRegistersRequest(decoded.address, decoded.start, decoded.count);
    } else if (frame[1] == 6) {
        if (length != WRITE_RESPONSE_LEN) return bad(FrameError::LENGTH, error, "FC06 request length");
        decoded.count = 1; decoded.wordCount = 1; decoded.words[0] = word(frame + 4);
        status = validateWriteSingleRegisterRequest(decoded.address, decoded.start, decoded.words[0]);
    } else if (frame[1] == 16) {
        decoded.count = word(frame + 4);
        if (!decoded.count || decoded.count > MAX_READ_REGISTERS ||
            length != 9U + 2U * decoded.count)
            return bad(FrameError::LENGTH, error, "FC10 request length");
        if (frame[6] != 2U * decoded.count) return bad(FrameError::BYTE_COUNT, error, "FC10 byte count");
        decoded.wordCount = decoded.count;
        for (std::size_t i = 0; i < decoded.wordCount; ++i) decoded.words[i] = word(frame + 7 + 2 * i);
        status = validateWriteMultipleRegistersRequest(decoded.address, decoded.start,
                                                      decoded.words, decoded.count);
    } else return bad(FrameError::FUNCTION, error, "unreviewed function");
    return status;
}
bool overlaps(const void* a, std::size_t as, const void* b, std::size_t bs) noexcept {
    if (!a || !b) return false;
    const uintptr_t x = reinterpret_cast<uintptr_t>(a), y = reinterpret_cast<uintptr_t>(b);
    return x <= y ? y - x < as : x - y < bs;
}
} // namespace
Status decodeTraffic(const TrafficRecord& record, const TrafficRecord* request,
                     TrafficDecoded& output, FrameError* error) noexcept {
    if (overlaps(&output, sizeof(output), &record, sizeof(record)) ||
        (request && overlaps(&output, sizeof(output), request, sizeof(*request))) ||
        (error && (overlaps(error, sizeof(*error), &record, sizeof(record)) ||
                   (request && overlaps(error, sizeof(*error), request, sizeof(*request))) ||
                   overlaps(error, sizeof(*error), &output, sizeof(output)))))
        return Status(Err::INVALID_CONFIG, 0, "traffic output overlap");
    if (error) *error = FrameError::NONE;
    TrafficDecoded decoded;
    if (record.kind == TrafficKind::TX) {
        const Status status = requestFields(record, decoded, error);
        if (status) output = decoded;
        return status;
    }
    if (record.kind != TrafficKind::RX) return Status(Err::UNSUPPORTED, 0, "traffic metadata");
    if (record.length > TRAFFIC_MAX_BYTES)
        return bad(FrameError::LENGTH, error, "RX length exceeds copied storage");
    if (!record.complete || !request || !record.transaction || record.transaction != request->transaction)
        return bad(FrameError::ARGUMENT, error, "complete correlated RX required");
    if (!request->sequence || record.sequence <= request->sequence)
        return bad(FrameError::ARGUMENT, error, "RX must follow its copied request");
    const Status expected = requestFields(*request, decoded, error);
    if (!expected) return expected;
    Status status;
    if (decoded.function == 3) {
        status = parseRegisters(record.bytes, record.length, decoded.address, decoded.count,
                                decoded.words, MAX_READ_REGISTERS, decoded.wordCount, error);
    } else if (decoded.function == 6) {
        status = parseWriteSingleRegister(record.bytes, record.length, decoded.address,
                                          decoded.start, decoded.words[0], error);
    } else {
        status = parseWriteMultipleRegisters(record.bytes, record.length, decoded.address,
                                             decoded.start, decoded.count, error);
        decoded.wordCount = 0; // FC10 acknowledgement carries no values.
        std::memset(decoded.words, 0, sizeof(decoded.words));
    }
    if (status.code == Err::EXCEPTION) {
        decoded.isException = true;
        decoded.exceptionCode = static_cast<uint8_t>(status.detail);
        decoded.wordCount = 0;
        std::memset(decoded.words, 0, sizeof(decoded.words));
    }
    if (status || status.code == Err::EXCEPTION) output = decoded;
    return status;
}
}} // namespace MotorControlRS::ESS_RS
