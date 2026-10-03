// SPDX-License-Identifier: MIT
#include "EssRtuValidator.h"
#include <MotorControlRS/profiles/ess_rs/Codec.h>
#include <cstring>

namespace MotorControlRSExample { namespace Rtu {
namespace {
namespace Ess = MotorControlRS::ESS_RS;
uint16_t readWord(const uint8_t* bytes) noexcept {
    return static_cast<uint16_t>((static_cast<uint16_t>(bytes[0]) << 8) | bytes[1]);
}
bool checkRequest(const Expectation& expected, const Request& request) noexcept {
    if (!request.bytes || request.length > MAX_FRAME) return false;
    uint8_t built[21]; // Largest reviewed ESS request, not a device-wide FC10 limit.
    std::size_t length = 0, replyLength = 0;
    switch (expected.function) {
    case 3:
        length = Ess::buildReadRegisters(expected.address, expected.first, expected.count, built, sizeof(built));
        replyLength = Ess::expectedReadRegistersLen(expected.count);
        break;
    case 6:
        if (expected.count != 1) return false;
        length = Ess::buildWriteSingleRegister(expected.address, expected.first, expected.value, built, sizeof(built));
        replyLength = Ess::WRITE_RESPONSE_LEN;
        break;
    case 0x10: {
        const std::size_t expectedLength = Ess::expectedWriteMultipleRegistersLen(expected.count);
        if (!expectedLength || expected.count > 6 || request.length != expectedLength) return false;
        uint16_t words[6];
        for (std::size_t i = 0; i < expected.count; ++i) words[i] = readWord(request.bytes + 7 + 2 * i);
        length = Ess::buildWriteMultipleRegisters(expected.address, expected.first, words, expected.count, built, sizeof(built));
        replyLength = Ess::WRITE_RESPONSE_LEN;
        break;
    }
    default: return false;
    }
    return length && length == request.length && replyLength == request.replyLength &&
           std::memcmp(built, request.bytes, length) == 0;
}
MotorControlRS::Status checkReply(const Expectation& expected, const uint8_t* bytes, std::size_t length) noexcept {
    switch (expected.function) {
    case 3: {
        uint16_t words[Ess::MAX_READ_REGISTERS];
        std::size_t count = 0;
        return Ess::parseRegisters(bytes, length, expected.address, expected.count,
                                   words, Ess::MAX_READ_REGISTERS, count);
    }
    case 6:
        return Ess::parseWriteSingleRegister(bytes, length, expected.address, expected.first, expected.value);
    case 0x10:
        return Ess::parseWriteMultipleRegisters(bytes, length, expected.address, expected.first, expected.count);
    default:
        return MotorControlRS::Status(MotorControlRS::Err::UNSUPPORTED, 0, "unsupported ESS transaction");
    }
}
}
Validator essValidator() noexcept {
    Validator validator;
    validator.checkRequest = checkRequest;
    validator.checkReply = checkReply;
    return validator;
}
}}
