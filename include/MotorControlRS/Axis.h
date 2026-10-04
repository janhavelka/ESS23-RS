/** @file Axis.h
 * @brief Exact, pure coordinate preparation and host-only configuration.
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <stdint.h>
#include <limits>
#include "MotorControlRS/Units.h"
#include "MotorControlRS/ReadOperation.h"

namespace MotorControlRS {
/** Exact signed input. The denominator must be positive; native integers never
 * pass through floating point. Decimal and fraction text use this same type. */
struct Rational {
    int64_t numerator = 0;
    uint64_t denominator = 1;
    Rational() = default;
    Rational(int64_t n, uint64_t d = 1) : numerator(n), denominator(d) {}
};
enum class CoordinateFrame : uint8_t { NATIVE, MOTOR, LOAD };
enum class RelativeBasis : uint8_t { ACTUAL, COMMANDED, QUEUED };
enum class AnglePath : uint8_t { POSITIVE, NEGATIVE, SHORTEST };
enum class HalfTurnTie : uint8_t { REJECT, POSITIVE, NEGATIVE };
enum class Rounding : uint8_t { EXACT, NEAREST, TOWARD_ZERO, FLOOR, CEIL };
enum class AxisError : int32_t {
    NONE, INVALID_ARGUMENT, INVALID_TARGET, STALE_GENERATION, STALE_REFERENCE,
    NOT_STATIONARY, MISSING_ORIGIN, MISSING_REFERENCE, UNSUPPORTED_BASIS,
    ARITHMETIC_OVERFLOW, FRACTIONAL_TARGET, QUANTIZATION_ERROR, LIMIT,
    APPROXIMATION_REQUIRED, AMBIGUOUS, GENERATION_EXHAUSTED,
    UNIT_ERROR_BASE = 100 ///< Unit errors use this base plus UnitError; Err/msg are preserved.
};
/** Host interpretation only. Native/soft bounds are inclusive command-register
 * coordinates. Optional scales/origins are required only when actually used. */
struct AxisConfig {
    ReadTarget target;
    uint32_t generation = 1;
    UnitConfig units;
    int64_t nativeMinimum = std::numeric_limits<int64_t>::min();
    int64_t nativeMaximum = std::numeric_limits<int64_t>::max();
    uint8_t supportedRelativeBases = 0; ///< bit 0 actual, 1 commanded, 2 queued; supplied reviewed policy.
    bool originKnown = false;
    int64_t originNative = 0;
    ScaleSource originSource = ScaleSource::UNKNOWN;
    bool encoderOriginKnown = false;
    int64_t encoderOriginNative = 0; ///< Command position corresponding to this encoder's zero.
    ScaleSource encoderOriginSource = ScaleSource::UNKNOWN;
    bool softLimitsKnown = false;
    int64_t softMinimum = 0;
    int64_t softMaximum = 0;
};
/** Supplied evidence; no clock is read. Stationary/idle are caller-qualified
 * facts, never inferred from zero displacement. nativeKnown additionally means
 * the exact command-coordinate relation is established, not an unsigned/raw
 * feedback guess. Freshness checks include target and coordinate generation. */
struct AxisReference {
    ReadTarget target;
    uint32_t configurationGeneration = 0;
    bool idle = false;
    bool stationary = false;
    bool nativeKnown = false;
    int64_t nativePosition = 0;
    RelativeBasis basis = RelativeBasis::ACTUAL;
    ScaleSource source = ScaleSource::UNKNOWN;
    uint64_t observedUs = 0;
    uint64_t nowUs = 0;
    uint64_t maximumAgeUs = 0;
};
struct PositionRequest {
    Rational value;
    PositionUnit unit = PositionUnit::STEPS;
    CoordinateFrame frame = CoordinateFrame::NATIVE;
    bool relative = true;
    bool wrapped = false; ///< Explicit orientation selection; ordinary absolute targets retain all turns.
    AnglePath path = AnglePath::SHORTEST;
    HalfTurnTie tie = HalfTurnTie::REJECT;
    RelativeBasis basis = RelativeBasis::ACTUAL;
    uint32_t configurationGeneration = 0;
    Rounding rounding = Rounding::EXACT;
    double maximumQuantizationError = 0; ///< Native increments, finite and nonnegative.
    bool approximate = false; ///< Only radians use this explicit approximation path.
    bool rationalRadians = true; ///< Use retained exact value; false selects supplied binary64 radians.
    double radians = 0;
    double maximumApproximationError = 0; ///< Native increments; strictly positive for radians.
};
/** Exact mixed native quantity: integral + (negative ? -1 : 1)*numerator/denominator.
 * The fraction is proper. This avoids overflowing origin*denominator. */
struct NativeQuantity {
    int64_t integral = 0;
    uint64_t numerator = 0;
    uint64_t denominator = 1;
    bool negative = false;
};
struct PreparedTarget {
    PositionRequest requested;
    NativeQuantity requestedNative; ///< Exact endpoint when known, otherwise exact displacement; approximate path uses estimate.
    double approximateRequestedNative = 0; ///< Only meaningful when exactArithmetic=false.
    int64_t effectiveNative = 0; ///< Relative displacement, or absolute target.
    bool endpointKnown = false;
    int64_t endpointNative = 0;
    bool displacementKnown = false;
    int64_t displacementNative = 0;
    bool zeroDisplacement = false;
    double roundingError = 0; ///< Signed effective minus requested, in native increments.
    double approximationErrorBound = 0;
    bool exactArithmetic = true;
    ReadTarget target;
    uint32_t configurationGeneration = 0;
};
/** Strict ASCII integer/decimal/fraction parser. No exponent, whitespace, NaN,
 * infinity or silent precision loss; output is unchanged on error. */
Status parseExactNumber(const char* text, Rational& output);
/** Convert a policy allowance toward zero without widening its exact value.
 * Quantity preparation owns all scales; output is unchanged on error. */
Status rationalToDouble(const Rational& value, double& output);
Status validateAxisConfig(const AxisConfig& config);
/** Validate a supplied stationary/idle witness and atomically advance generation.
 * Interpretation changes invalidate origins/soft limits. Preferences remain
 * independent. Optional retainedReference preserves the witness and its original
 * observation age under the new generation when interpretation is unchanged;
 * otherwise it receives an empty reference. It may alias evidence. Configuration
 * and retainedReference stay unchanged on error. No drive traffic occurs. */
Status configureAxis(AxisConfig& current, const AxisConfig& candidate,
                     const AxisReference& evidence, AxisReference* retainedReference = nullptr);
/** Set host zero only from established stationary native-reference evidence.
 * Advances generation and invalidates soft limits; changes no drive counter. */
Status setAxisOrigin(AxisConfig& config, int64_t originNative,
                     const AxisReference& evidence);
/** Explicitly forget position confidence after caller-established loss/release,
 * device clear or interpretation changes. No I/O. Clears both origins, derived
 * limits and supplied reference, preserving scales/preferences. Advances the
 * coordinate generation; at exhaustion confidence still clears and an error
 * forbids treating dependent prepared targets as reusable. */
Status invalidateAxisReference(AxisConfig& config, AxisReference& reference);
/** Pure target arithmetic only, not an implemented motion command. EXACT is the
 * default. Quantization applies to the final absolute native target, or to the
 * relative displacement. Relative native
 * requests without endpoint limits need no unrelated origin, gear or reference.
 * Wrapped orientation requires an absolute angular MOTOR/LOAD request, known
 * host origin and fresh multi-turn native reference. Equal orientation stays
 * still; shortest half-turn ties reject unless explicitly directed. Selected
 * paths fail at limits rather than selecting another turn.
 * Every output remains unchanged on error. */
Status preparePosition(const PositionRequest& request, const AxisConfig& config,
                       const AxisReference* reference, PreparedTarget& output);
}
