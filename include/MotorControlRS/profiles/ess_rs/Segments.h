/** @file Segments.h
 * @brief ESS indexed stored records. Configuration never starts execution.
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "MotorControlRS/profiles/ess_rs/DriverSettings.h"

namespace MotorControlRS { namespace ESS_RS {
constexpr uint8_t SEGMENT_RECORD_COUNT = 16;
enum class SegmentKind : uint8_t { POSITION, SPEED, START_SPEED };
/** Raw native words retain unresolved scale, signed encoding and activation.
 * PT records contain a reserved sixth word which is never read or written.
 * Starting speed is shared by PT/PV, separately from active position speed. */
struct SegmentObservation {
    SegmentKind kind = SegmentKind::POSITION;
    uint8_t index = 1;
    ReadTarget target;
    uint32_t operationId = 0, configurationGeneration = 0, knownFields = 0;
    uint16_t speed = 0, acceleration = 0, deceleration = 0, startSpeed = 0;
    uint16_t pulseWords[2] = {};
    bool pairOrderKnown = false;
    uint32_t pulseBits = 0; ///< Ordered unsigned bits, never a guessed signed pulse count.
    const char* unresolved = "native scale and signed encoding not qualified";
    DriverEvidence provenance;
};
/** These wrappers use DriverContext/nextDriver/advanceDriver, not another
 * sequence. Segment index is 1..16 and all rejected outputs stay unchanged. */
Status prepareSegmentRead(DriverContext&, const ReadTarget&, uint32_t operationId,
    uint32_t configurationGeneration, uint64_t nowUs, uint64_t deadlineUs,
    SegmentKind, uint8_t index) noexcept;
/** Candidate selects SEGMENT_* fields in a matching segment DriverGroup.
 * Pulse writes always report unsupported before any other candidate field.
 * Native nonnegative speed/time words are supported; negative PV/start speed
 * encoding remains unresolved. External-trigger inhibition and exact stored
 * baseline must be independently qualified for a stopped-state update. */
Status prepareSegmentSettings(DriverContext&, const ReadTarget&, uint32_t operationId,
    const DriverRequest&, const DriverPrerequisites&, uint64_t nowUs, uint64_t deadlineUs) noexcept;
/** No physical units or signed meaning are inferred. The caller may supply
 * independently qualified word order for this exact target/generation. */
Status getSegment(const DriverContext&, SegmentObservation&,
    bool qualifiedWordOrder = false, WordOrder = WordOrder::HIGH_WORD_FIRST) noexcept;
}} // namespace MotorControlRS::ESS_RS
