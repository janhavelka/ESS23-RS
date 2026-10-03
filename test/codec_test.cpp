// SPDX-License-Identifier: MIT
#include "RS485Motion/profiles/ess_rs/Codec.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

using RS485Motion::Err;
using RS485Motion::Status;
using namespace RS485Motion::ESS_RS;

namespace {

// Literal frames from the function manual, physical pages 7-8. The FC10
// paragraph has the correct FD 12 CRC; the diagram's B9 56 is a source error.
const uint8_t READ_REQUEST[] = {1, 3, 0, 0x23, 0, 1, 0x75, 0xC0};
const uint8_t READ_RESPONSE[] = {1, 3, 2, 0, 0x3C, 0xB8, 0x55};
const uint8_t POSITION_RESPONSE[] = {1, 3, 6, 0, 0, 0x13, 0x88, 0, 0, 0xA5, 0xDB};
const uint8_t SINGLE_WRITE[] = {1, 6, 0, 0x23, 0, 0x3C, 0x78, 0x11};
const uint8_t MULTIPLE_REQUEST[] = {1, 0x10, 0, 0x24, 0, 2, 4, 0, 0, 0x13, 0x88, 0xFD, 0x12};
const uint8_t MULTIPLE_RESPONSE[] = {1, 0x10, 0, 0x24, 0, 2, 1, 0xC3};
const uint8_t PROBE_REQUEST[] = {1, 3, 0, 0, 0, 1, 0x84, 0x0A};

// Independent fixture CRC: forward polynomial 0x8005 with reflected bytes.
// Production uses the reflected polynomial 0xA001. Literal vectors above and
// the published CRC-16/MODBUS check value also verify this fixture helper.
uint16_t reflect(uint16_t value, unsigned bits) {
    uint16_t result = 0;
    for (unsigned bit = 0; bit < bits; ++bit) {
        result = static_cast<uint16_t>((result << 1) | (value & 1));
        value >>= 1;
    }
    return result;
}

uint16_t fixtureCrc(const uint8_t* bytes, std::size_t length) {
    uint16_t crc = 0xFFFF;
    for (std::size_t i = 0; i < length; ++i) {
        crc ^= static_cast<uint16_t>(reflect(bytes[i], 8) << 8);
        for (unsigned bit = 0; bit < 8; ++bit) {
            const bool high = (crc & 0x8000) != 0;
            crc = static_cast<uint16_t>(crc << 1);
            if (high) crc ^= 0x8005;
        }
    }
    return reflect(crc, 16);
}

void seal(uint8_t* frame, std::size_t length) {
    assert(length >= 2);
    const uint16_t crc = fixtureCrc(frame, length - 2);
    frame[length - 2] = static_cast<uint8_t>(crc);
    frame[length - 1] = static_cast<uint8_t>(crc >> 8);
}

template<typename T, std::size_t N>
void filled(const T (&values)[N], T expected) {
    for (std::size_t i = 0; i < N; ++i) assert(values[i] == expected);
}

void testCrcAndSizes() {
    const uint8_t check[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    assert(fixtureCrc(check, sizeof(check)) == 0x4B37);
    assert(calcCrc16(check, sizeof(check)) == 0x4B37);
    assert(calcCrc16(READ_REQUEST, 6) == 0xC075);
    assert(calcCrc16(READ_RESPONSE, 5) == 0x55B8);
    assert(calcCrc16(SINGLE_WRITE, 6) == 0x1178);
    assert(calcCrc16(MULTIPLE_REQUEST, 11) == 0x12FD);
    assert(calcCrc16(MULTIPLE_RESPONSE, 6) == 0xC301);
    assert(calcCrc16(MULTIPLE_REQUEST, sizeof(MULTIPLE_REQUEST)) == 0);
    assert(calcCrc16(nullptr, 0) == 0xFFFF);
    assert(calcCrc16(nullptr, 1) == 0);
    uint8_t bytes[256];
    for (std::size_t i = 0; i < sizeof(bytes); ++i) bytes[i] = static_cast<uint8_t>(i);
    assert(calcCrc16(bytes, sizeof(bytes)) == fixtureCrc(bytes, sizeof(bytes)));
    // Impossible lengths must return before touching bytes beyond this fixture.
    assert(calcCrc16(check, 257) == 0);
    assert(calcCrc16(check, std::numeric_limits<std::size_t>::max()) == 0);

    assert(expectedReadRegistersLen(0) == 0);
    assert(expectedReadRegistersLen(1) == 7);
    assert(expectedReadRegistersLen(16) == 37);
    assert(expectedReadRegistersLen(17) == 0);
    assert(expectedReadRegistersLen(0xFFFF) == 0);
    assert(expectedWriteSingleRegisterLen() == 8);
    assert(expectedWriteMultipleRegistersLen(0) == 0);
    assert(expectedWriteMultipleRegistersLen(1) == 0);
    assert(expectedWriteMultipleRegistersLen(2) == 13);
    assert(expectedWriteMultipleRegistersLen(3) == 0);
    assert(expectedWriteMultipleRegistersLen(0xFFFF) == 0);
}

void testRequestPolicy() {
    for (unsigned address = 0; address <= 255; ++address) {
        const bool valid = address >= 1 && address <= 247;
        assert(isValidAddress(static_cast<uint8_t>(address)) == valid);
        assert(validateReadRegistersRequest(static_cast<uint8_t>(address), 0, 1).isOk() == valid);
    }
    assert(isReadRangeValid(0, 4));
    assert(isReadRangeValid(0x000A, 1)); // Raw halves can be inspected independently.
    assert(isReadRangeValid(0x000B, 1));
    assert(isReadRangeValid(0x0100, 16));
    assert(!isReadRangeValid(0x0100, 17));
    assert(!isReadRangeValid(0x0100, 0));
    assert(!isReadRangeValid(0x0003, 2)); // Undocumented gap.
    assert(!isReadRangeValid(0x0027, 1)); // Write-only motion command.
    assert(!isReadRangeValid(0x003B, 1)); // Source does not establish access.
    assert(!isReadRangeValid(0x0065, 1)); // Reserved segment word.
    assert(!isReadRangeValid(0xFFFF, 2));
    assert(!isReadRangeValid(0, 0xFFFF));

    assert(validateWriteSingleRegisterRequest(247, 0x0023, 0xFFFF));
    assert(validateWriteSingleRegisterRequest(1, 0x0027, 0x0100));
    assert(validateWriteSingleRegisterRequest(0, 0x0023, 1).code == Err::INVALID_CONFIG);
    const uint16_t denied[] = {0, 4, 0x000A, 0x0024, 0x0025, 0x003B, 0x0065, 0xFFFF};
    for (std::size_t i = 0; i < sizeof(denied) / sizeof(denied[0]); ++i) {
        assert(validateWriteSingleRegisterRequest(1, denied[i], 0).code == Err::UNSUPPORTED);
    }
    const uint16_t words[] = {0, 5000};
    assert(validateWriteMultipleRegistersRequest(1, 0x0024, words, 2));
    assert(validateWriteMultipleRegistersRequest(0, 0x0024, words, 2).code == Err::INVALID_CONFIG);
    assert(validateWriteMultipleRegistersRequest(1, 0x0024, nullptr, 2).code == Err::INVALID_CONFIG);
    assert(validateWriteMultipleRegistersRequest(1, 0x0023, words, 2).code == Err::UNSUPPORTED);
    assert(validateWriteMultipleRegistersRequest(1, 0x0025, words, 2).code == Err::UNSUPPORTED);
    const uint16_t deniedCounts[] = {0, 1, 3, 16, 123, 0xFFFF};
    for (std::size_t i = 0; i < sizeof(deniedCounts) / sizeof(deniedCounts[0]); ++i) {
        assert(validateWriteMultipleRegistersRequest(1, 0x0024, words, deniedCounts[i]).code == Err::UNSUPPORTED);
    }
}

void testBuilders() {
    uint8_t output[40];
    std::memset(output, 0xA5, sizeof(output));
    assert(buildReadRegisters(1, 0x0023, 1, output, 8) == sizeof(READ_REQUEST));
    assert(std::memcmp(output, READ_REQUEST, sizeof(READ_REQUEST)) == 0);
    assert(output[8] == 0xA5);
    assert(buildWriteSingleRegister(1, 0x0023, 60, output, 8) == sizeof(SINGLE_WRITE));
    assert(std::memcmp(output, SINGLE_WRITE, sizeof(SINGLE_WRITE)) == 0);
    const uint16_t words[] = {0, 5000};
    assert(buildWriteMultipleRegisters(1, 0x0024, words, 2, output, 13) == sizeof(MULTIPLE_REQUEST));
    assert(std::memcmp(output, MULTIPLE_REQUEST, sizeof(MULTIPLE_REQUEST)) == 0);
    assert(output[13] == 0xA5);
    assert(buildProbe(1, output, 8) == sizeof(PROBE_REQUEST));
    assert(std::memcmp(output, PROBE_REQUEST, sizeof(PROBE_REQUEST)) == 0);

    std::memset(output, 0xA5, sizeof(output));
    for (std::size_t capacity = 0; capacity < 8; ++capacity) {
        assert(buildReadRegisters(1, 0x0023, 1, output, capacity) == 0);
        assert(buildWriteSingleRegister(1, 0x0023, 60, output, capacity) == 0);
        assert(buildProbe(1, output, capacity) == 0);
        filled(output, uint8_t(0xA5));
    }
    for (std::size_t capacity = 0; capacity < 13; ++capacity) {
        assert(buildWriteMultipleRegisters(1, 0x0024, words, 2, output, capacity) == 0);
        filled(output, uint8_t(0xA5));
    }
    assert(buildReadRegisters(1, 0x0023, 1, nullptr, 8) == 0);
    assert(buildWriteSingleRegister(1, 0x0023, 60, nullptr, 8) == 0);
    assert(buildWriteMultipleRegisters(1, 0x0024, words, 2, nullptr, 13) == 0);
    assert(buildProbe(1, nullptr, 8) == 0);
    assert(buildReadRegisters(0, 0x0023, 1, output, sizeof(output)) == 0);
    assert(buildReadRegisters(1, 0x0027, 1, output, sizeof(output)) == 0);
    assert(buildReadRegisters(1, 0x0100, 17, output, sizeof(output)) == 0);
    assert(buildWriteSingleRegister(1, 0x0024, 0, output, sizeof(output)) == 0);
    assert(buildWriteSingleRegister(248, 0x0023, 0, output, sizeof(output)) == 0);
    assert(buildWriteMultipleRegisters(1, 0x0024, nullptr, 2, output, sizeof(output)) == 0);
    assert(buildWriteMultipleRegisters(1, 0x0023, words, 2, output, sizeof(output)) == 0);
    assert(buildWriteMultipleRegisters(1, 0x0024, words, 0xFFFF, output, sizeof(output)) == 0);
    assert(buildProbe(0, output, sizeof(output)) == 0);
    filled(output, uint8_t(0xA5));

    uint16_t shared[10];
    for (std::size_t i = 0; i < 10; ++i) shared[i] = 0xA55A;
    uint8_t* bytes = reinterpret_cast<uint8_t*>(shared);
    assert(buildWriteMultipleRegisters(1, 0x0024, shared, 2, bytes, sizeof(shared)) == 0);
    assert(buildWriteMultipleRegisters(1, 0x0024, shared + 3, 2, bytes, sizeof(shared)) == 0);
    assert(buildWriteMultipleRegisters(1, 0x0024, shared, 2, bytes + 1, sizeof(shared) - 1) == 0);
    filled(shared, uint16_t(0xA55A));
}

void readFailure(const uint8_t* frame, std::size_t length, uint8_t address,
                 uint16_t expectedCount, FrameError reason) {
    uint16_t words[17];
    for (std::size_t i = 0; i < 17; ++i) words[i] = 0xA55A;
    std::size_t count = 99;
    FrameError error = FrameError::NONE;
    const Status status = parseRegisters(frame, length, address, expectedCount,
                                         words, 17, count, &error);
    assert(!status && error == reason && count == 0);
    filled(words, uint16_t(0xA55A));
    FrameError validationError = FrameError::NONE;
    const Status validation = validateReadResponseExpected(frame, length, address,
                                                           expectedCount, &validationError);
    assert(validation.code == status.code && validationError == error);
}

void testReadResponses() {
    uint16_t value = 0xA55A;
    FrameError error = FrameError::CRC;
    assert(validateReadResponseExpected(READ_RESPONSE, sizeof(READ_RESPONSE), 1, 1, &error));
    assert(error == FrameError::NONE);
    assert(parseRegister(READ_RESPONSE, sizeof(READ_RESPONSE), 1, value, &error));
    assert(value == 60 && error == FrameError::NONE);
    uint16_t words[17] = {};
    std::size_t count = 99;
    assert(parseRegisters(POSITION_RESPONSE, sizeof(POSITION_RESPONSE), 1, 3,
                          words, 17, count, &error));
    assert(count == 3 && words[0] == 0 && words[1] == 5000 && words[2] == 0);
    assert(words[3] == 0 && error == FrameError::NONE);

    uint8_t maximum[37] = {247, 3, 32};
    for (unsigned i = 0; i < 16; ++i) {
        maximum[3 + i * 2] = static_cast<uint8_t>(i);
        maximum[4 + i * 2] = static_cast<uint8_t>(255 - i);
    }
    seal(maximum, sizeof(maximum));
    assert(parseRegisters(maximum, sizeof(maximum), 247, 16, words, 16, count));
    assert(count == 16);
    for (unsigned i = 0; i < 16; ++i) assert(words[i] == (i * 256 + 255 - i));

    for (std::size_t length = 0; length < sizeof(READ_RESPONSE); ++length) {
        readFailure(READ_RESPONSE, length, 1, 1, FrameError::LENGTH);
    }
    uint8_t longer[8] = {1, 3, 2, 0, 60, 0, 0, 0};
    seal(longer, sizeof(longer));
    readFailure(longer, sizeof(longer), 1, 1, FrameError::LENGTH);
    readFailure(READ_RESPONSE, std::numeric_limits<std::size_t>::max(), 1, 1, FrameError::LENGTH);
    readFailure(nullptr, 7, 1, 1, FrameError::ARGUMENT);
    readFailure(READ_RESPONSE, 7, 0, 1, FrameError::ARGUMENT);
    readFailure(READ_RESPONSE, 7, 248, 1, FrameError::ARGUMENT);
    readFailure(READ_RESPONSE, 7, 1, 0, FrameError::ARGUMENT);
    readFailure(READ_RESPONSE, 7, 1, 17, FrameError::ARGUMENT);
    // A large destination is not permission to accept a different reply count.
    readFailure(POSITION_RESPONSE, sizeof(POSITION_RESPONSE), 1, 1, FrameError::LENGTH);

    uint8_t frame[sizeof(READ_RESPONSE)];
    std::memcpy(frame, READ_RESPONSE, sizeof(frame));
    frame[0] = 2;
    seal(frame, sizeof(frame));
    readFailure(frame, sizeof(frame), 1, 1, FrameError::ADDRESS);
    frame[0] = 1;
    frame[1] = 4;
    seal(frame, sizeof(frame));
    readFailure(frame, sizeof(frame), 1, 1, FrameError::FUNCTION);
    frame[1] = 3;
    const uint8_t wrongCounts[] = {0, 1, 3, 4, 32, 255};
    for (std::size_t i = 0; i < sizeof(wrongCounts); ++i) {
        frame[2] = wrongCounts[i];
        seal(frame, sizeof(frame));
        readFailure(frame, sizeof(frame), 1, 1, FrameError::BYTE_COUNT);
    }
    frame[2] = 2;
    seal(frame, sizeof(frame));
    frame[6] ^= 1;
    readFailure(frame, sizeof(frame), 1, 1, FrameError::CRC);
    assert(validateReadResponseExpected(frame, sizeof(frame), 1, 1).code == Err::CRC_ERROR);
    value = 0xA55A;
    assert(!parseRegister(frame, sizeof(frame), 1, value));
    assert(value == 0xA55A);

    for (std::size_t i = 0; i < 17; ++i) words[i] = 0xA55A;
    count = 99;
    assert(!parseRegisters(POSITION_RESPONSE, sizeof(POSITION_RESPONSE), 1, 3, words, 2, count, &error));
    assert(count == 0 && error == FrameError::CAPACITY);
    filled(words, uint16_t(0xA55A));
    assert(!parseRegisters(READ_RESPONSE, sizeof(READ_RESPONSE), 1, 1, nullptr, 1, count, &error));
    assert(count == 0 && error == FrameError::ARGUMENT);

    uint16_t shared[8] = {};
    uint8_t* bytes = reinterpret_cast<uint8_t*>(shared);
    std::memcpy(bytes, READ_RESPONSE, sizeof(READ_RESPONSE));
    uint16_t saved[8];
    std::memcpy(saved, shared, sizeof(shared));
    assert(!parseRegisters(bytes, sizeof(READ_RESPONSE), 1, 1, shared, 8, count, &error));
    assert(count == 0 && error == FrameError::OVERLAP);
    assert(std::memcmp(shared, saved, sizeof(shared)) == 0);
    assert(!parseRegister(bytes, sizeof(READ_RESPONSE), 1, shared[2], &error));
    assert(error == FrameError::OVERLAP);
    assert(std::memcmp(shared, saved, sizeof(shared)) == 0);
}

void testExceptionsAndPrecedence() {
    // These fixed unknown-code frames were computed independently of the codec.
    const uint8_t read[] = {1, 0x83, 0xE7, 1, 0x7A};
    const uint8_t single[] = {1, 0x86, 0xE7, 2, 0x2A};
    const uint8_t multiple[] = {1, 0x90, 0xE7, 0x0C, 0x4A};
    const Status results[] = {
        validateReadResponseExpected(read, sizeof(read), 1, 1),
        parseWriteSingleRegister(single, sizeof(single), 1, 0x0023, 60),
        parseWriteMultipleRegisters(multiple, sizeof(multiple), 1, 0x0024, 2)
    };
    for (std::size_t i = 0; i < 3; ++i) {
        assert(results[i].code == Err::EXCEPTION && results[i].detail == 0xE7);
    }
    for (unsigned function = 0; function < 2; ++function) {
        uint8_t reply[6] = {1, function == 0 ? uint8_t(0x86) : uint8_t(0x90), 0xE7, 0, 0, 0};
        seal(reply, 5);
        FrameError diagnostic = FrameError::NONE;
        for (std::size_t length = 0; length < 5; ++length) {
            const Status result = function == 0 ?
                parseWriteSingleRegister(reply, length, 1, 0x0023, 60, &diagnostic) :
                parseWriteMultipleRegisters(reply, length, 1, 0x0024, 2, &diagnostic);
            assert(!result && diagnostic == FrameError::LENGTH);
        }
        reply[4] ^= 1;
        const Status badCrc = function == 0 ?
            parseWriteSingleRegister(reply, 5, 1, 0x0023, 60, &diagnostic) :
            parseWriteMultipleRegisters(reply, 5, 1, 0x0024, 2, &diagnostic);
        assert(badCrc.code == Err::CRC_ERROR && diagnostic == FrameError::CRC);
        seal(reply, 6);
        const Status tooLong = function == 0 ?
            parseWriteSingleRegister(reply, 6, 1, 0x0023, 60, &diagnostic) :
            parseWriteMultipleRegisters(reply, 6, 1, 0x0024, 2, &diagnostic);
        assert(!tooLong && diagnostic == FrameError::LENGTH);
        reply[1] = 0x83;
        seal(reply, 5);
        const Status wrongFunction = function == 0 ?
            parseWriteSingleRegister(reply, 5, 1, 0x0023, 60, &diagnostic) :
            parseWriteMultipleRegisters(reply, 5, 1, 0x0024, 2, &diagnostic);
        assert(!wrongFunction && diagnostic == FrameError::FUNCTION);
    }
    readFailure(read, sizeof(read), 1, 1, FrameError::EXCEPTION);
    readFailure(single, sizeof(single), 1, 1, FrameError::FUNCTION);
    for (std::size_t length = 0; length < sizeof(read); ++length) {
        readFailure(read, length, 1, 1, FrameError::LENGTH);
    }
    uint8_t longer[] = {1, 0x83, 0xE7, 0, 0, 0};
    seal(longer, sizeof(longer));
    readFailure(longer, sizeof(longer), 1, 1, FrameError::LENGTH);
    uint8_t frame[] = {1, 0x83, 0xE7, 1, 0x7B};
    readFailure(frame, sizeof(frame), 1, 1, FrameError::CRC);
    frame[0] = 2; // Address wins over bad CRC.
    readFailure(frame, sizeof(frame), 1, 1, FrameError::ADDRESS);
    frame[0] = 1;
    frame[1] = 0x86; // Function wins over bad CRC.
    readFailure(frame, sizeof(frame), 1, 1, FrameError::FUNCTION);
    readFailure(nullptr, std::numeric_limits<std::size_t>::max(), 0, 0, FrameError::ARGUMENT);
    readFailure(nullptr, std::numeric_limits<std::size_t>::max(), 1, 1, FrameError::ARGUMENT);

    // Preserve every raw exception byte, including zero and future values.
    for (unsigned code = 0; code <= 255; ++code) {
        frame[1] = 0x83;
        frame[2] = static_cast<uint8_t>(code);
        seal(frame, sizeof(frame));
        const Status result = validateReadResponseExpected(frame, sizeof(frame), 1, 1);
        assert(result.code == Err::EXCEPTION && result.detail == static_cast<int32_t>(code));
    }
    uint16_t words[1] = {0xA55A};
    std::size_t count = 99;
    FrameError error;
    assert(parseRegisters(read, sizeof(read), 1, 1, words, 0, count, &error).code == Err::EXCEPTION);
    assert(error == FrameError::EXCEPTION && count == 0 && words[0] == 0xA55A);
    uint16_t model = 0xA55A;
    assert(parseProbe(read, sizeof(read), 1, model).code == Err::EXCEPTION && model == 0xA55A);
}

void testWriteResponses() {
    FrameError error = FrameError::CRC;
    assert(parseWriteSingleRegister(SINGLE_WRITE, sizeof(SINGLE_WRITE), 1, 0x0023, 60, &error));
    assert(error == FrameError::NONE);
    assert(parseWriteMultipleRegisters(MULTIPLE_RESPONSE, sizeof(MULTIPLE_RESPONSE), 1, 0x0024, 2, &error));
    assert(error == FrameError::NONE);
    for (std::size_t length = 0; length < 8; ++length) {
        assert(!parseWriteSingleRegister(SINGLE_WRITE, length, 1, 0x0023, 60, &error));
        assert(error == FrameError::LENGTH);
        assert(!parseWriteMultipleRegisters(MULTIPLE_RESPONSE, length, 1, 0x0024, 2, &error));
        assert(error == FrameError::LENGTH);
    }
    uint8_t longer[9] = {};
    std::memcpy(longer, SINGLE_WRITE, sizeof(SINGLE_WRITE));
    seal(longer, sizeof(longer));
    assert(!parseWriteSingleRegister(longer, sizeof(longer), 1, 0x0023, 60, &error));
    assert(error == FrameError::LENGTH);
    std::memcpy(longer, MULTIPLE_RESPONSE, sizeof(MULTIPLE_RESPONSE));
    seal(longer, sizeof(longer));
    assert(!parseWriteMultipleRegisters(longer, sizeof(longer), 1, 0x0024, 2, &error));
    assert(error == FrameError::LENGTH);
    assert(!parseWriteSingleRegister(SINGLE_WRITE, std::numeric_limits<std::size_t>::max(), 1, 0x0023, 60, &error));
    assert(error == FrameError::LENGTH);
    assert(!parseWriteMultipleRegisters(MULTIPLE_RESPONSE, std::numeric_limits<std::size_t>::max(), 1, 0x0024, 2, &error));
    assert(error == FrameError::LENGTH);
    assert(!parseWriteSingleRegister(nullptr, 8, 1, 0x0023, 60, &error));
    assert(error == FrameError::ARGUMENT);
    assert(!parseWriteMultipleRegisters(nullptr, 8, 1, 0x0024, 2, &error));
    assert(error == FrameError::ARGUMENT);

    for (unsigned changed = 0; changed < 8; ++changed) {
        uint8_t single[8];
        uint8_t multiple[8];
        std::memcpy(single, SINGLE_WRITE, sizeof(single));
        std::memcpy(multiple, MULTIPLE_RESPONSE, sizeof(multiple));
        single[changed] ^= 1;
        multiple[changed] ^= 1;
        if (changed < 6) {
            seal(single, sizeof(single));
            seal(multiple, sizeof(multiple));
        }
        const FrameError expected = changed == 0 ? FrameError::ADDRESS :
            changed == 1 ? FrameError::FUNCTION : changed < 6 ? FrameError::ECHO : FrameError::CRC;
        assert(!parseWriteSingleRegister(single, sizeof(single), 1, 0x0023, 60, &error));
        assert(error == expected);
        assert(!parseWriteMultipleRegisters(multiple, sizeof(multiple), 1, 0x0024, 2, &error));
        assert(error == expected);
    }
    assert(!parseWriteSingleRegister(SINGLE_WRITE, 8, 0, 0x0023, 60, &error));
    assert(error == FrameError::ARGUMENT);
    assert(!parseWriteSingleRegister(SINGLE_WRITE, 8, 1, 0x0024, 60, &error));
    assert(error == FrameError::ARGUMENT);
    assert(!parseWriteMultipleRegisters(MULTIPLE_RESPONSE, 8, 1, 0x0023, 2, &error));
    assert(error == FrameError::ARGUMENT);
    assert(!parseWriteMultipleRegisters(MULTIPLE_RESPONSE, 8, 1, 0x0024, 1, &error));
    assert(error == FrameError::ARGUMENT);
    // A valid FC10 request must not be mistaken for its shorter acknowledgement.
    assert(!parseWriteMultipleRegisters(MULTIPLE_REQUEST, sizeof(MULTIPLE_REQUEST), 1, 0x0024, 2, &error));
    assert(error == FrameError::LENGTH);
}

void testProbe() {
    uint8_t frame[] = {1, 3, 2, 0xBE, 0xEF, 0, 0};
    seal(frame, sizeof(frame));
    uint16_t model = 0;
    FrameError error = FrameError::ECHO;
    assert(parseProbe(frame, sizeof(frame), 1, model, &error));
    assert(model == 0xBEEF && error == FrameError::NONE); // Unknown model is retained.
    frame[6] ^= 1;
    assert(!parseProbe(frame, sizeof(frame), 1, model, &error));
    assert(model == 0xBEEF && error == FrameError::CRC);
}

void testWordConversion() {
    const WordOrder orders[] = {WordOrder::HIGH_WORD_FIRST, WordOrder::LOW_WORD_FIRST};
    struct Vector { uint32_t bits; int32_t signedValue; uint16_t high; uint16_t low; };
    const Vector vectors[] = {
        {0, 0, 0, 0}, {1, 1, 0, 1}, {0x12345678u, 305419896, 0x1234, 0x5678},
        {0x7FFFFFFFu, INT32_MAX, 0x7FFF, 0xFFFF},
        {0x80000000u, INT32_MIN, 0x8000, 0}, {0xFFFFFFFFu, -1, 0xFFFF, 0xFFFF}
    };
    for (std::size_t order = 0; order < 2; ++order) {
        for (std::size_t i = 0; i < sizeof(vectors) / sizeof(vectors[0]); ++i) {
            const Vector& v = vectors[i];
            const uint16_t expected[] = {order == 0 ? v.high : v.low, order == 0 ? v.low : v.high};
            uint16_t encoded[3] = {0xA55A, 0xA55A, 0xA55A};
            assert(encodeUint32(v.bits, orders[order], encoded, 3));
            assert(encoded[0] == expected[0] && encoded[1] == expected[1] && encoded[2] == 0xA55A);
            assert(encodeInt32(v.signedValue, orders[order], encoded, 2));
            assert(encoded[0] == expected[0] && encoded[1] == expected[1] && encoded[2] == 0xA55A);
            uint32_t raw = 123;
            int32_t signedValue = 123;
            // Decoder uses literal expected words, not output from its encoder.
            assert(decodeUint32(expected, 2, orders[order], raw) && raw == v.bits);
            assert(decodeInt32(expected, 2, orders[order], signedValue) && signedValue == v.signedValue);
        }
    }
    const WordOrder invalid = static_cast<WordOrder>(2);
    uint16_t words[3] = {0xA55A, 0xA55A, 0xA55A};
    uint32_t raw = 123;
    int32_t signedValue = 123;
    assert(!encodeUint32(1, invalid, words, 3));
    assert(!encodeInt32(-1, invalid, words, 3));
    assert(!decodeUint32(words, 3, invalid, raw));
    assert(!decodeInt32(words, 3, invalid, signedValue));
    for (std::size_t count = 0; count < 2; ++count) {
        assert(!encodeUint32(1, orders[0], words, count));
        assert(!encodeInt32(-1, orders[0], words, count));
        assert(!decodeUint32(words, count, orders[0], raw));
        assert(!decodeInt32(words, count, orders[0], signedValue));
    }
    assert(!encodeUint32(1, orders[0], nullptr, 2));
    assert(!encodeInt32(-1, orders[0], nullptr, 2));
    assert(!decodeUint32(nullptr, 2, orders[0], raw));
    assert(!decodeInt32(nullptr, 2, orders[0], signedValue));
    filled(words, uint16_t(0xA55A));
    assert(raw == 123 && signedValue == 123);
    // Aligned scalar storage; overlap must be rejected before any word read.
    assert(!decodeUint32(reinterpret_cast<const uint16_t*>(&raw), 2, orders[0], raw));
    assert(!decodeInt32(reinterpret_cast<const uint16_t*>(&signedValue), 2, orders[0], signedValue));
    assert(raw == 123 && signedValue == 123);
}

} // namespace

int main() {
    testCrcAndSizes();
    testRequestPolicy();
    testBuilders();
    testReadResponses();
    testExceptionsAndPrecedence();
    testWriteResponses();
    testProbe();
    testWordConversion();
    return 0;
}
