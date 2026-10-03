/**
 * @file Codec.h
 * @brief Bounded ESS-RS Modbus RTU codecs. No transport, timing or retained state.
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <cstddef>
#include <stdint.h>
#include "RS485Motion/Status.h"
#include "RS485Motion/profiles/ess_rs/Types.h"

namespace RS485Motion { namespace ESS_RS {

constexpr uint16_t MAX_READ_REGISTERS = 16; ///< Vendor function manual p7.
constexpr std::size_t READ_REQUEST_LEN = 8;
constexpr std::size_t WRITE_RESPONSE_LEN = 8;
constexpr std::size_t EXCEPTION_RESPONSE_LEN = 5;

/** @brief Optional machine-readable parser diagnostic, reset to NONE on entry. */
enum class FrameError : uint8_t {
    NONE, ARGUMENT, LENGTH, ADDRESS, FUNCTION, BYTE_COUNT, CRC, ECHO,
    CAPACITY, OVERLAP, EXCEPTION
};

/** @brief Supported unicast range 1..247; broadcast/vendor extensions deferred. */
bool isValidAddress(uint8_t address) noexcept;
/** @brief 1..16 documented readable words, with no gaps/reserved/WO/unknown access.
 * A partial pair may be read as raw words. This does not classify read effects.
 */
bool isReadRangeValid(uint16_t start, uint16_t count) noexcept;

/** @brief Validate address and raw FC03 window; no buffer access or I/O. */
Status validateReadRegistersRequest(uint8_t address, uint16_t start, uint16_t count) noexcept;
/** @brief Validate a documented writable single word for FC06.
 * Rejects both halves of paired fields. Raw value meaning, ranges, operational
 * prerequisites and side effects are the caller's responsibility in this codec.
 */
Status validateWriteSingleRegisterRequest(uint8_t address, uint16_t reg, uint16_t value) noexcept;
/** @brief Validate the initially reviewed FC10 window: 0x0024, exactly two words.
 * words must point to count readable uint16_t values. Other windows/counts are
 * UNSUPPORTED, not an assertion that the drive rejects them. No device-wide
 * FC10 maximum or atomic-write guarantee has been established. Values are raw.
 */
Status validateWriteMultipleRegistersRequest(uint8_t address, uint16_t start,
                                             const uint16_t* words, uint16_t count) noexcept;

/** @brief Normal FC03 reply bytes (5 + 2*count), or zero for count outside 1..16. */
std::size_t expectedReadRegistersLen(uint16_t count) noexcept;
/** @brief FC06 request AND normal reply bytes: 8. */
std::size_t expectedWriteSingleRegisterLen() noexcept;
/** @brief FC10 REQUEST bytes: 13 for the supported count of two, otherwise zero.
 * This checks count only; the request validator also checks the reviewed window.
 * A normal FC10 reply is always WRITE_RESPONSE_LEN; an exception is five bytes.
 */
std::size_t expectedWriteMultipleRegistersLen(uint16_t count) noexcept;
/** @brief Modbus CRC16 (seed FFFF, polynomial A001); low CRC byte goes first.
 * Reads at most 256 bytes. Empty input returns FFFF (nullptr is allowed then).
 * Null nonempty input or length >256 returns zero without reading. Zero is also
 * a possible valid CRC; use request/response validators for diagnostics.
 */
uint16_t calcCrc16(const uint8_t* data, std::size_t length) noexcept;

/** @name Raw request builders
 * Return bytes written, or zero with the output unchanged on any failure.
 * capacity is in BYTES. Buffers must be valid for the accessed length and may
 * not overlap any input word buffer. Validation completes before the first write.
 * No pointer is retained. These calls only build bytes; no motion/state change
 * occurs until the application explicitly transmits them. FC06/FC10 may encode
 * actions or persistent settings; raw access is not a validated motion workflow.
 * @{ */
std::size_t buildReadRegisters(uint8_t address, uint16_t start, uint16_t count,
                               uint8_t* output, std::size_t capacity) noexcept;
std::size_t buildWriteSingleRegister(uint8_t address, uint16_t reg, uint16_t value,
                                    uint8_t* output, std::size_t capacity) noexcept;
std::size_t buildWriteMultipleRegisters(uint8_t address, uint16_t start,
                                       const uint16_t* words, uint16_t count,
                                       uint8_t* output, std::size_t capacity) noexcept;
/** @} */

/** @name Checked responses
 * Every parser checks the expected unicast slave/function and exact reply length,
 * byte count (FC03), CRC and echo (writes). Error precedence: invalid expectations,
 * null input, impossible length, address, function, function-specific length,
 * byte count, CRC, device exception, write echo, null payload output, output
 * capacity, buffer overlap. Parser capacity and outCount are in WORDS.
 * A valid exception returns EXCEPTION with its RAW code in Status::detail; unknown
 * codes remain intact. No standard exception label is asserted for ESS firmware.
 *
 * Payload outputs stay unchanged on failure. outCount resets to zero on entry
 * and is set only on success. error may be nullptr. outCount and error must be
 * distinct from each other, frame memory and payload output storage. All other
 * input/output overlaps in the accessed ranges are rejected before publication. Pointers must
 * designate valid storage for the stated frame/output lengths. No pointer is retained.
 *
 * FC03 does not echo the start register: expectedCount is from the request, NOT
 * output capacity. The bus owner must correlate replies and reject local echo /
 * late frames. In particular an FC06 local echo is indistinguishable from a reply
 * here. A validated write reply does not establish motion completion.
 * @{ */
Status validateReadResponseExpected(const uint8_t* frame, std::size_t length,
                                    uint8_t address, uint16_t expectedCount,
                                    FrameError* error = nullptr) noexcept;
Status parseRegisters(const uint8_t* frame, std::size_t length, uint8_t address,
                      uint16_t expectedCount, uint16_t* output, std::size_t capacity,
                      std::size_t& outCount, FrameError* error = nullptr) noexcept;
Status parseRegister(const uint8_t* frame, std::size_t length, uint8_t address,
                     uint16_t& output, FrameError* error = nullptr) noexcept;
Status parseWriteSingleRegister(const uint8_t* frame, std::size_t length,
                               uint8_t address, uint16_t reg, uint16_t value,
                               FrameError* error = nullptr) noexcept;
Status parseWriteMultipleRegisters(const uint8_t* frame, std::size_t length,
                                  uint8_t address, uint16_t start, uint16_t count,
                                  FrameError* error = nullptr) noexcept;
/** @} */

/** @brief Smallest documented non-changing ESS probe: FC03, 0x0000, one word.
 * Reads the read-only driver-model word (function manual p68), with no documented
 * read side effect. Request: 8 bytes; normal reply: 7 bytes. Same builder rules.
 * No hardware timing or side-effect qualification is claimed. No retry or I/O.
 */
std::size_t buildProbe(uint8_t address, uint8_t* output, std::size_t capacity) noexcept;
/** @brief Validate probe reply and preserve the raw model word, including unknown
 * values. Success proves a matching responder, not manufacturer/model identity,
 * readiness or fresh motion state. model stays unchanged on failure.
 */
Status parseProbe(const uint8_t* frame, std::size_t length, uint8_t address,
                  uint16_t& model, FrameError* error = nullptr) noexcept;

/** @name Pure 32-bit word conversion
 * Operates on host uint16_t words in increasing register-address order. Explicit
 * order is required; invalid order, null/short buffers and overlap return an error
 * without changing payload outputs. capacity/count are in WORDS, at least two.
 * Only the first two words are accessed. No pointers are retained and no I/O occurs.
 * Signed helpers explicitly use two's complement; their availability does NOT
 * establish a particular ESS register's signed encoding, scale or range. Those
 * field-specific uncertainties remain in the catalogue. Raw codecs do not call
 * signed conversion implicitly or infer the active word order.
 * @{ */
Status encodeUint32(uint32_t value, WordOrder order, uint16_t* output, std::size_t capacity) noexcept;
Status decodeUint32(const uint16_t* words, std::size_t count, WordOrder order, uint32_t& output) noexcept;
Status encodeInt32(int32_t value, WordOrder order, uint16_t* output, std::size_t capacity) noexcept;
Status decodeInt32(const uint16_t* words, std::size_t count, WordOrder order, int32_t& output) noexcept;
/** @} */

}} // namespace RS485Motion::ESS_RS
