// SPDX-License-Identifier: MIT
#include "RS485Motion/profiles/ess_rs/Codec.h"
#include "RS485Motion/profiles/ess_rs/Registers.h"
#include "../../rtu/Frame.h"

#include <limits>

namespace RS485Motion { namespace ESS_RS {
namespace {

constexpr uint8_t READ = 0x03;
constexpr uint8_t WRITE_SINGLE = 0x06;
constexpr uint8_t WRITE_MULTIPLE = 0x10;

Status invalid(int32_t detail, const char* message) noexcept {
    return Status(Err::INVALID_CONFIG, detail, message);
}

Status unsupported(int32_t detail, const char* message) noexcept {
    return Status(Err::UNSUPPORTED, detail, message);
}

void clearError(FrameError* error) noexcept {
    if (error) *error = FrameError::NONE;
}

Status frameError(FrameError reason, FrameError* error) noexcept {
    if (error) *error = reason;
    const Err code = reason == FrameError::CRC ? Err::CRC_ERROR : Err::FRAME_ERROR;
    return Status(code, static_cast<int32_t>(reason), "reply does not match request");
}

Status argumentError(Status status, FrameError* error) noexcept {
    if (error) *error = FrameError::ARGUMENT;
    return status;
}

bool readable(RegisterAccess access) noexcept {
    return access == RegisterAccess::READ_ONLY || access == RegisterAccess::READ_WRITE;
}

bool writable(RegisterAccess access) noexcept {
    return access == RegisterAccess::WRITE_ONLY || access == RegisterAccess::READ_WRITE;
}

Status writeWindow(uint8_t address, uint16_t start, uint16_t count) noexcept {
    if (!isValidAddress(address)) return invalid(address, "expected unicast address 1..247");
    // This is a reviewed window, not a guessed device-wide FC10 maximum.
    if (start != Registers::POSITION_PULSES || count != 2) {
        return unsupported(start, "FC10 currently supports only 0x0024 with two words");
    }
    return Ok();
}

Status checkReply(const uint8_t* frame, std::size_t length, uint8_t address,
                  uint8_t function, std::size_t normalLength, FrameError* error) noexcept {
    if (!frame) return argumentError(invalid(0, "null reply"), error);
    // Reject arbitrary lengths before touching bytes or scanning a CRC.
    if (length < EXCEPTION_RESPONSE_LEN || length > normalLength) {
        return frameError(FrameError::LENGTH, error);
    }
    if (frame[0] != address) return frameError(FrameError::ADDRESS, error);
    const bool exception = frame[1] == static_cast<uint8_t>(function | 0x80);
    if (frame[1] != function && !exception) return frameError(FrameError::FUNCTION, error);
    const std::size_t required = exception ? EXCEPTION_RESPONSE_LEN : normalLength;
    if (length != required) return frameError(FrameError::LENGTH, error);
    if (!exception && function == READ && frame[2] != normalLength - 5) {
        return frameError(FrameError::BYTE_COUNT, error);
    }
    if (!Rtu::validCrc(frame, length)) return frameError(FrameError::CRC, error);
    if (exception) {
        if (error) *error = FrameError::EXCEPTION;
        return Status(Err::EXCEPTION, frame[2], "device exception (raw code)");
    }
    return Ok();
}

Status checkEcho(const uint8_t* frame, uint16_t reg, uint16_t value,
                 FrameError* error) noexcept {
    if (Rtu::readWord(frame + 2) != reg || Rtu::readWord(frame + 4) != value) {
        return frameError(FrameError::ECHO, error);
    }
    return Ok();
}

bool validOrder(WordOrder order) noexcept {
    return order == WordOrder::HIGH_WORD_FIRST || order == WordOrder::LOW_WORD_FIRST;
}

Status checkWords(const uint16_t* words, std::size_t count, WordOrder order) noexcept {
    if (!validOrder(order)) return invalid(static_cast<int32_t>(order), "invalid word order");
    if (!words || count < 2) return invalid(0, "two words required");
    return Ok();
}

uint32_t joinWords(const uint16_t* words, WordOrder order) noexcept {
    const unsigned high = order == WordOrder::HIGH_WORD_FIRST ? 0 : 1;
    return (static_cast<uint32_t>(words[high]) << 16) | words[1 - high];
}

} // namespace

bool isValidAddress(uint8_t address) noexcept {
    return address >= 1 && address <= 247;
}

bool isReadRangeValid(uint16_t start, uint16_t count) noexcept {
    if (count == 0 || count > MAX_READ_REGISTERS ||
        static_cast<uint32_t>(start) + count > 0x10000u) return false;
    for (uint16_t offset = 0; offset < count; ++offset) {
        const RegisterDescriptor* entry = findRegister(static_cast<uint16_t>(start + offset));
        if (!entry || !readable(entry->access)) return false;
    }
    return true;
}

Status validateReadRegistersRequest(uint8_t address, uint16_t start, uint16_t count) noexcept {
    if (!isValidAddress(address)) return invalid(address, "expected unicast address 1..247");
    if (count == 0 || count > MAX_READ_REGISTERS) return invalid(count, "read count must be 1..16");
    if (!isReadRangeValid(start, count)) return unsupported(start, "unreviewed or unreadable register window");
    return Ok();
}

Status validateWriteSingleRegisterRequest(uint8_t address, uint16_t reg, uint16_t value) noexcept {
    (void)value; // Raw bits; typed register-value and workflow checks are a separate layer.
    if (!isValidAddress(address)) return invalid(address, "expected unicast address 1..247");
    const RegisterDescriptor* entry = findRegister(reg);
    if (!entry || !writable(entry->access)) return unsupported(reg, "unreviewed or unwritable register");
    if (entry->wordCount != 1) return unsupported(reg, "paired values require a reviewed multi-word write");
    return Ok();
}

Status validateWriteMultipleRegistersRequest(uint8_t address, uint16_t start,
                                             const uint16_t* words, uint16_t count) noexcept {
    const Status status = writeWindow(address, start, count);
    if (!status) return status;
    if (!words) return invalid(0, "null write words");
    return Ok();
}

std::size_t expectedReadRegistersLen(uint16_t count) noexcept {
    return count >= 1 && count <= MAX_READ_REGISTERS ? 5u + 2u * count : 0;
}

std::size_t expectedWriteSingleRegisterLen() noexcept { return 8; }

std::size_t expectedWriteMultipleRegistersLen(uint16_t count) noexcept {
    return count == 2 ? 13 : 0;
}

uint16_t calcCrc16(const uint8_t* data, std::size_t length) noexcept {
    return Rtu::crc16(data, length);
}

std::size_t buildReadRegisters(uint8_t address, uint16_t start, uint16_t count,
                               uint8_t* output, std::size_t capacity) noexcept {
    if (!validateReadRegistersRequest(address, start, count) || !output || capacity < READ_REQUEST_LEN) return 0;
    Rtu::writeHeader(output, address, READ, start, count);
    return Rtu::finishFrame(output, 6);
}

std::size_t buildWriteSingleRegister(uint8_t address, uint16_t reg, uint16_t value,
                                    uint8_t* output, std::size_t capacity) noexcept {
    if (!validateWriteSingleRegisterRequest(address, reg, value) || !output || capacity < 8) return 0;
    Rtu::writeHeader(output, address, WRITE_SINGLE, reg, value);
    return Rtu::finishFrame(output, 6);
}

std::size_t buildWriteMultipleRegisters(uint8_t address, uint16_t start,
                                       const uint16_t* words, uint16_t count,
                                       uint8_t* output, std::size_t capacity) noexcept {
    if (!validateWriteMultipleRegistersRequest(address, start, words, count)) return 0;
    const std::size_t length = expectedWriteMultipleRegistersLen(count);
    if (!output || capacity < length || Rtu::overlaps(words, count * sizeof(*words), output, length)) return 0;
    Rtu::writeHeader(output, address, WRITE_MULTIPLE, start, count);
    output[6] = static_cast<uint8_t>(count * 2);
    for (uint16_t i = 0; i < count; ++i) Rtu::writeWord(output + 7 + 2 * i, words[i]);
    return Rtu::finishFrame(output, length - 2);
}

Status validateReadResponseExpected(const uint8_t* frame, std::size_t length,
                                    uint8_t address, uint16_t expectedCount,
                                    FrameError* error) noexcept {
    clearError(error);
    if (!isValidAddress(address)) return argumentError(invalid(address, "expected unicast address 1..247"), error);
    const std::size_t normalLength = expectedReadRegistersLen(expectedCount);
    if (normalLength == 0) return argumentError(invalid(expectedCount, "read count must be 1..16"), error);
    return checkReply(frame, length, address, READ, normalLength, error);
}

Status parseRegisters(const uint8_t* frame, std::size_t length, uint8_t address,
                      uint16_t expectedCount, uint16_t* output, std::size_t capacity,
                      std::size_t& outCount, FrameError* error) noexcept {
    outCount = 0;
    const Status status = validateReadResponseExpected(frame, length, address, expectedCount, error);
    if (!status) return status;
    if (!output) return argumentError(invalid(0, "null output words"), error);
    if (capacity < expectedCount) {
        if (error) *error = FrameError::CAPACITY;
        return invalid(expectedCount, "output word capacity too small");
    }
    if (Rtu::overlaps(frame, length, output, expectedCount * sizeof(*output))) {
        if (error) *error = FrameError::OVERLAP;
        return invalid(0, "reply and output overlap");
    }
    // All checks have completed. Decode before publication; no caller data is
    // touched by a failure and the temporary has a fixed small stack footprint.
    uint16_t decoded[MAX_READ_REGISTERS];
    for (uint16_t i = 0; i < expectedCount; ++i) decoded[i] = Rtu::readWord(frame + 3 + 2 * i);
    for (uint16_t i = 0; i < expectedCount; ++i) output[i] = decoded[i];
    outCount = expectedCount;
    return Ok();
}

Status parseRegister(const uint8_t* frame, std::size_t length, uint8_t address,
                     uint16_t& output, FrameError* error) noexcept {
    std::size_t count = 0;
    return parseRegisters(frame, length, address, 1, &output, 1, count, error);
}

Status parseWriteSingleRegister(const uint8_t* frame, std::size_t length,
                               uint8_t address, uint16_t reg, uint16_t value,
                               FrameError* error) noexcept {
    clearError(error);
    Status status = validateWriteSingleRegisterRequest(address, reg, value);
    if (!status) return argumentError(status, error);
    status = checkReply(frame, length, address, WRITE_SINGLE, WRITE_RESPONSE_LEN, error);
    return status ? checkEcho(frame, reg, value, error) : status;
}

Status parseWriteMultipleRegisters(const uint8_t* frame, std::size_t length,
                                  uint8_t address, uint16_t start, uint16_t count,
                                  FrameError* error) noexcept {
    clearError(error);
    Status status = writeWindow(address, start, count);
    if (!status) return argumentError(status, error);
    status = checkReply(frame, length, address, WRITE_MULTIPLE, WRITE_RESPONSE_LEN, error);
    return status ? checkEcho(frame, start, count, error) : status;
}

std::size_t buildProbe(uint8_t address, uint8_t* output, std::size_t capacity) noexcept {
    return buildReadRegisters(address, Registers::DRIVER_MODEL, 1, output, capacity);
}

Status parseProbe(const uint8_t* frame, std::size_t length, uint8_t address,
                  uint16_t& model, FrameError* error) noexcept {
    return parseRegister(frame, length, address, model, error);
}

Status encodeUint32(uint32_t value, WordOrder order, uint16_t* output, std::size_t capacity) noexcept {
    const Status status = checkWords(output, capacity, order);
    if (!status) return status;
    const unsigned high = order == WordOrder::HIGH_WORD_FIRST ? 0 : 1;
    output[high] = static_cast<uint16_t>(value >> 16);
    output[1 - high] = static_cast<uint16_t>(value);
    return Ok();
}

Status decodeUint32(const uint16_t* words, std::size_t count, WordOrder order, uint32_t& output) noexcept {
    const Status status = checkWords(words, count, order);
    if (!status) return status;
    if (Rtu::overlaps(words, 2 * sizeof(*words), &output, sizeof(output))) return invalid(0, "words and output overlap");
    output = joinWords(words, order);
    return Ok();
}

Status encodeInt32(int32_t value, WordOrder order, uint16_t* output, std::size_t capacity) noexcept {
    return encodeUint32(static_cast<uint32_t>(value), order, output, capacity);
}

Status decodeInt32(const uint16_t* words, std::size_t count, WordOrder order, int32_t& output) noexcept {
    const Status status = checkWords(words, count, order);
    if (!status) return status;
    if (Rtu::overlaps(words, 2 * sizeof(*words), &output, sizeof(output))) return invalid(0, "words and output overlap");
    const uint32_t bits = joinWords(words, order);
    // Avoid an implementation-defined unsigned-to-signed conversion above INT32_MAX.
    output = bits <= static_cast<uint32_t>(std::numeric_limits<int32_t>::max())
        ? static_cast<int32_t>(bits)
        : -1 - static_cast<int32_t>(std::numeric_limits<uint32_t>::max() - bits);
    return Ok();
}

}} // namespace RS485Motion::ESS_RS
