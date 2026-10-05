// SPDX-License-Identifier: MIT
#pragma once
#include "../common/Esp32S3Uart.h"
#include <MotorControlRS/profiles/ess_rs/Position.h>

namespace MotorControlRSExample {
/** Age-only expiry is opt-in. Zero keeps checked observations until invalidated
 * by an event/configuration change; it does not prove the drive stayed powered.
 * Transaction deadlines, missing evidence and generation checks always apply. */
struct ApplicationOptions {
    uint32_t observationMaxAgeMs = 0;
    uint32_t moveTimeoutMs = 30000; ///< 1..30000 ms, finite movement observation deadline.
    ApplicationOptions() {
        // Usable nominal convention, explicitly assumed rather than calibrated.
        positionUnits.commandStepsPerMotorTurn = MotorControlRS::UnitScale(1000, 1, MotorControlRS::ScaleSource::ASSUMED);
    }
    MotorControlRS::UnitConfig positionUnits; ///< Host scales; default 1000 command steps/turn is ASSUMED. No device settings write.
};
/** Construct the shared standalone application in required PSRAM and initialize
 * its one UART/capture owner. Failure is explicit; there is no internal-RAM
 * fallback. Pins/topology are supplied by the selected example configuration.
 * Call once from the same task that subsequently calls serviceApplication().
 */
bool beginApplication(const Esp32S3Uart::Pins&, bool receiverDisabledDuringTransmit,
                      const ApplicationOptions& = ApplicationOptions());
/// One bounded cooperative console/owner service turn, including an idle yield.
void serviceApplication();

/** Submit through the same owner/admission/polling path as console moves.
 * Existing configuration, state and motion-profile reads establish readiness.
 * Call from the serviceApplication task. Output ID is unchanged on rejection.
 * moveBy requests relative motion: steps are native command
 * increments; other units use the motor frame, except millimetres (load frame).
 * Scales come from ApplicationOptions::positionUnits or host axis configuration.
 * Optional setup policy defaults to full write. USE_STORED requires a matching
 * remembered profile/binding in this example; it does not detect an unseen reset.
 */
MotorControlRS::Status moveBy(MotorControlRS::Rational value, MotorControlRS::PositionUnit unit,
                            uint32_t& operationId, uint16_t speedRpm = 60,
                            MotorControlRS::MoveSetup = MotorControlRS::MoveSetup::WRITE_ALL);
/// Absolute target. Engineering coordinates require their configured origin/scales.
MotorControlRS::Status moveTo(MotorControlRS::Rational value, MotorControlRS::PositionUnit unit,
                            uint32_t& operationId, uint16_t speedRpm = 60,
                            MotorControlRS::MoveSetup = MotorControlRS::MoveSetup::WRITE_ALL);
/// Advanced units/frame/rounding/absolute request; configuration generation is checked, not rebound.
MotorControlRS::Status submitMove(const MotorControlRS::PositionRequest&, uint16_t speedRpm,
                            uint32_t& operationId,
                            MotorControlRS::MoveSetup = MotorControlRS::MoveSetup::WRITE_ALL);
/// Copy current host coordinates/scales/generation for an advanced request; no bus traffic.
bool positionConfiguration(MotorControlRS::AxisConfig&);
/** Small copied progress/result. Observation flags describe drive reports;
 * success requires the existing new RUNNING then ARRIVED sequence. */
struct MoveProgress {
    bool pending = false, runningObserved = false, interruptedByStop = false, uncertain = false;
    bool observationKnown = false;
    uint16_t rawAlarm = 0, rawMotion = 0;
    MotorControlRS::ActionOutcome outcome = MotorControlRS::ActionOutcome::NONE;
    MotorControlRS::ActionExecution execution = MotorControlRS::ActionExecution::NOT_TRANSMITTED;
    MotorControlRS::ActionCompletion completion = MotorControlRS::ActionCompletion::NOT_OBSERVED;
    MotorControlRS::Status status;
    int64_t effectiveNative = 0;
};
/// Cached inspection only; serviceApplication performs actual bus polling.
bool moveProgress(uint32_t operationId, MoveProgress&);
/// Explicitly free a completed programmatic move result; does not stop the motor.
bool releaseMove(uint32_t operationId);
}
