/** @file Tuning.h
 * @brief Typed ESS native tuning parameters and bounded settings sequences.
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "MotorControlRS/profiles/ess_rs/DriverSettings.h"
namespace MotorControlRS { namespace ESS_RS {
enum class TuningParameter : uint8_t {
    INPUT_FILTER, PULSE_LOW_PASS_FILTER, POSITION_ERROR_ALARM_THRESHOLD,
    POSITION_ARRIVAL_WINDOW, ARRIVAL_TIME, PULSE_MEAN_FILTER,
    CURRENT_LOOP_KP_MULTIPLIER, CURRENT_LOOP_KP, CURRENT_LOOP_KI, CURRENT_LOOP_KC,
    LA_SPEED_KP1, LA_SPEED_KV1, LA_SPEED_NODE1, LA_SPEED_KP2, LA_SPEED_KV2,
    LA_SPEED_NODE2, LA_SPEED_FEEDFORWARD_KVF, LA_POSITION_KI,
    COLLISION_THRESHOLD_0122, COLLISION_CURRENT_0123,
    COLLISION_THRESHOLD_003B, COLLISION_CURRENT_003C, NONE
};
enum class LaStageField : uint8_t { KP, KV, NODE };
/** Native range/access policy only. Generated register constants supply the
 * address; unspecified access at 003B/003C stays unavailable and unaliased. */
struct TuningParameterInfo {
    DriverGroup group = DriverGroup::FILTERS;
    uint8_t slot = 0;
    uint16_t reg = 0, minimum = 0, maximum = 0;
    bool accessReviewed = false;
};
bool isTuningGroup(DriverGroup) noexcept;
uint8_t tuningFieldCount(DriverGroup) noexcept;
TuningParameter tuningParameter(DriverGroup, uint8_t slot) noexcept;
TuningParameter laStageParameter(uint8_t stage, LaStageField) noexcept;
/** Invalid enum leaves output unchanged. Documented unavailable entries return
 * true with accessReviewed=false; they do not authorize a read or write. */
bool tuningParameterInfo(TuningParameter, TuningParameterInfo&) noexcept;
/** Select one reviewed native parameter. An empty request selects its group;
 * subsequent fields must belong to that same group. Rejections leave it intact. */
Status prepareTuningValue(DriverRequest&, TuningParameter, uint32_t nativeValue) noexcept;
struct TuningObservation {
    DriverGroup group = DriverGroup::FILTERS;
    ReadTarget target;
    uint32_t operationId = 0, configurationGeneration = 0, knownFields = 0;
    uint8_t count = 0;
    uint16_t raw[8] = {};
    DriverEvidence provenance[2];
};
/** Reads are non-changing and do not cross undocumented gaps. No physical
 * filter delay, gain scale, position-error unit or arrival-time unit is inferred.
 * A complete multi-read observation is not an atomic motor snapshot. */
Status prepareTuningRead(DriverContext&, const ReadTarget&, uint32_t operationId,
    uint32_t configurationGeneration, uint64_t nowUs, uint64_t deadlineUs, DriverGroup) noexcept;
/** Select fields with prepareTuningValue. Every selected native value and exact
 * model/effects qualification is checked before the first write. Existing
 * nextDriver/advanceDriver retain partial application; no retries or rollback. */
Status prepareTuningSettings(DriverContext&, const ReadTarget&, uint32_t operationId,
    const DriverRequest&, const DriverPrerequisites&, uint64_t nowUs, uint64_t deadlineUs) noexcept;
Status getTuning(const DriverContext&, TuningObservation&) noexcept;
}} // namespace MotorControlRS::ESS_RS
