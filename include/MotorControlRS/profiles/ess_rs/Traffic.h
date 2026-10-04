/** @file Traffic.h
 * @brief Read-only ESS traffic translation through the checked wire codecs.
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "MotorControlRS/Traffic.h"
#include "MotorControlRS/profiles/ess_rs/Codec.h"

namespace MotorControlRS { namespace ESS_RS {
struct TrafficDecoded {
    uint8_t address = 0, function = 0, exceptionCode = 0;
    uint16_t start = 0, count = 0;
    uint16_t words[MAX_READ_REGISTERS] = {};
    std::size_t wordCount = 0;
    bool isException = false;
};
/** Decode a copied full TX request or RX envelope. RX requires its exact copied
 * TX request and matching transaction ID. The start address of FC03 is from that
 * expectation, never guessed from reply bytes. Returns EXCEPTION and publishes
 * a valid exception; every other failure leaves output unchanged. Metadata records
 * are UNSUPPORTED. No input pointer is retained. Output/error must not overlap
 * either input record. This validates protocol shape, not echo origin, execution,
 * device state, completion, or physical timing. Raw records remain available when
 * translation fails, including partial TX and rejected/discarded RX.
 */
Status decodeTraffic(const TrafficRecord& record, const TrafficRecord* request,
                     TrafficDecoded& output, FrameError* error = nullptr) noexcept;
}} // namespace MotorControlRS::ESS_RS
