/** @file Defaults.h
 * @brief Source-labelled ESS bench unit assumptions; no device configuration.
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "MotorControlRS/Units.h"

namespace MotorControlRS {
namespace ESS_RS {

/** Documented ESS values, not readback of the connected motor. See reference/06. */
constexpr uint32_t kDocumentedSubdivisionDefault = 1000;
constexpr uint32_t kDocumentedEncoderLines = 1000;
constexpr uint32_t kDocumentedDecodedEncoderCounts = 4000;
constexpr uint32_t kDocumentedFullStepsPerMotorTurn = 200; // RS10/RS20: 360 / 1.8 degrees.
constexpr uint32_t kBenchMotorEncoderSource = 1; // Local identifier, not a wire value.

/**
 * Create explicitly assumed, usable free-shaft bench conversion settings.
 * Interpretation: subdivision default = command steps/motor turn, encoder
 * default = decoded counts/motor turn, direct 1:1 coupling and positive signs.
 * Full-step geometry comes from both model datasheets. Linear lead stays absent.
 * Performs no device configuration, readback, movement or reference establishment.
 * A caller must reconcile these assumptions with actual configuration later.
 */
inline UnitConfig makeBenchUnitConfig() {
    UnitConfig config;
    config.commandStepsPerMotorTurn = UnitScale(kDocumentedSubdivisionDefault, 1, ScaleSource::ASSUMED);
    config.motorTurnsPerLoadTurn = UnitScale(1, 1, ScaleSource::ASSUMED);
    config.fullStepsPerMotorTurn = UnitScale(kDocumentedFullStepsPerMotorTurn, 1, ScaleSource::DOCUMENTED);
    config.encoder.countsPerUnit = UnitScale(kDocumentedDecodedEncoderCounts, 1, ScaleSource::ASSUMED);
    config.encoder.basis = EncoderBasis::MOTOR_TURN;
    config.encoder.sourceId = kBenchMotorEncoderSource;
    return config;
}

} // namespace ESS_RS
} // namespace MotorControlRS
