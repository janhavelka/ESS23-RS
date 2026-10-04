// SPDX-License-Identifier: MIT
#include "ProbeConsole.h"
#include "MotorControlRS/Version.h"

#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <limits>

namespace MotorControlRSExample { namespace Probe {
namespace {

enum class Command : uint8_t { HELP, VERSION, CONFIG, STATUS, HEALTH, STATS, PROBE, CAPTURE_READ, RECOVER, RESET, MEMORY, LOAD, DRV, RESULT, CANCEL, RELEASE, READ, PROFILE, CAPS, READ_IDENTITY, READ_CONFIG, READ_STATE, HEALTH_CHECK, MONITOR, AXIS, PREPARE, ENABLE, MOTOR_RELEASE, ALARM_CLEAR, STOP, MOVE, POSITION_CLEAR, VELOCITY };
struct Entry { const char* name; Command command; const char* syntax; const char* effect; bool bus; };
const Entry COMMANDS[] = {
    {"help", Command::HELP, "help [command]", "show_callable_commands", false},
    {"version", Command::VERSION, "version", "show_build", false},
    {"ver", Command::VERSION, "ver", "show_build", false},
    {"config", Command::CONFIG, "config", "show_host_settings", false},
    {"settings", Command::CONFIG, "settings", "show_host_settings", false},
    {"status", Command::STATUS, "status", "show_cached_observations", false},
    {"health", Command::HEALTH, "health [check [address]]", "show_cached_health_or_explicitly_read_state", false},
    {"stats", Command::STATS, "stats [reset]", "show_or_clear_host_counters", false},
    {"probe", Command::PROBE, "probe [address]", "read_model_word_only", true},
    {"ping", Command::PROBE, "ping [address]", "read_model_word_only", true},
    {"capture-read", Command::CAPTURE_READ, "capture-read [address]", "read_0x0130_16_words_for_capture_qualification", true},
    {"read", Command::READ, "read identity|config|state [address]", "checked_nonchanging_read", true},
    {"profile", Command::PROFILE, "profile ess_rs identity|config|state|enable|release|clear-alarm|clear-position|normal-stop|emergency-stop [address] | profile ess_rs move-relative|move-absolute|move-angle ... | profile ess_rs velocity ... | profile ess_rs caps", "public_profile_operations", true},
    {"enable", Command::ENABLE, "enable [address]", "request_enable_then_observe_flags", true},
    {"motor-release", Command::MOTOR_RELEASE, "motor-release [address]", "request_release_then_observe_flags", true},
    {"alarm-clear", Command::ALARM_CLEAR, "alarm-clear [address]", "request_clear_resettable_alarm_then_observe_flags", true},
    {"stop", Command::STOP, "stop normal|direct [address]", "priority_stop_with_explicit_policy_then_observe_flags", true},
    {"move", Command::MOVE, "move relative|absolute value unit frame native_rpm configured [basis actual|commanded|queued] [round mode error [approx error]] [address] | move angle value unit frame positive|negative|shortest reject|positive|negative native_rpm configured [round mode error [approx error]] [address]", "finite_move_through_public_coordinate_preparation", true},
    {"position-clear", Command::POSITION_CLEAR, "position-clear [address]", "explicit_device_position_zero_only", true},
    {"velocity", Command::VELOCITY, "velocity value rpm|steps/s|fullsteps/s|turns/s|deg/s|rad/s|mm/s native|motor|load duration_ms configured normal|direct [round mode error [approx error]] [address]", "finite_serial_velocity_with_explicit_stop", true},
    {"monitor", Command::MONITOR, "monitor [off | interval_ms count]", "finite_nonconsuming_state_polling", false},
    {"caps", Command::CAPS, "caps", "show_public_read_capabilities", false},
    {"axis", Command::AXIS, "axis config [set field value [maximum]] | axis origin exact_native", "configure_host_coordinates_only", false},
    {"prepare", Command::PREPARE, "prepare absolute|relative value unit frame [basis] [round [max_error [radian_error]]] | prepare angle value unit frame path tie [round [max_error [radian_error]]]", "preview_public_target_arithmetic_without_motion", false},
    {"recover", Command::RECOVER, "recover", "recover_host_transport_only", false},
    {"reset", Command::RESET, "reset", "clear_host_counters_only", false},
    {"memory", Command::MEMORY, "memory", "show_cached_memory", false},
    {"load", Command::LOAD, "load [work_us owner_delay_us console_bytes]", "configure_or_report_host_load", false},
    {"drv", Command::DRV, "drv", "show_owner_queue_and_output", false},
    {"result", Command::RESULT, "result [operation_id]", "inspect_retained_result_without_consuming", false},
    {"cancel", Command::CANCEL, "cancel [operation_id]", "cancel_local_work_not_motor_stop", false},
    {"release", Command::RELEASE, "release operation_id", "release_retained_terminal_result", false}
};
const Entry IDENTITY_ENTRY = {"read-identity", Command::READ_IDENTITY, "read identity [address]", "checked_identity_read", true};
const Entry CONFIG_ENTRY = {"read-config", Command::READ_CONFIG, "read config [address]", "checked_configuration_read", true};
const Entry STATE_ENTRY = {"read-state", Command::READ_STATE, "read state [address]", "checked_nonconsuming_state_read", true};
const Entry HEALTH_ENTRY = {"read-state", Command::HEALTH_CHECK, "health check [address]", "explicit_nonconsuming_state_refresh", true};
namespace Ess = MotorControlRS::ESS_RS;

const char* readName(Ess::ReadKind kind) { return kind == Ess::ReadKind::IDENTITY ? "identity" : kind == Ess::ReadKind::CONFIG ? "config" : "state"; }
const char* readCommandName(Ess::ReadKind kind) { return kind == Ess::ReadKind::IDENTITY ? "read-identity" : kind == Ess::ReadKind::CONFIG ? "read-config" : "read-state"; }
const char* readState(MotorControlRS::ReadState state) {
    switch (state) { case MotorControlRS::ReadState::EMPTY: return "empty";
    case MotorControlRS::ReadState::ACTIVE: return "active";
    case MotorControlRS::ReadState::SUCCEEDED: return "succeeded";
    case MotorControlRS::ReadState::FAILED: return "failed"; } return "unknown";
}
const char* readOutcome(MotorControlRS::ReadOutcome outcome) {
    switch (outcome) { case MotorControlRS::ReadOutcome::NONE: return "none";
    case MotorControlRS::ReadOutcome::SUCCESS: return "success";
    case MotorControlRS::ReadOutcome::REPLY_ERROR: return "reply_error";
    case MotorControlRS::ReadOutcome::TRANSPORT_ERROR: return "transport_error";
    case MotorControlRS::ReadOutcome::CANCELLED: return "cancelled";
    case MotorControlRS::ReadOutcome::DEADLINE: return "deadline";
    case MotorControlRS::ReadOutcome::TIMING_UNQUALIFIED: return "timing_unqualified"; } return "unknown";
}

// Append complete fragments or fail; never publish truncated JSON.
bool append(char* output, std::size_t capacity, std::size_t& used, const char* format, ...) {
    if (used >= capacity) return false;
    va_list arguments; va_start(arguments, format);
    const int written = std::vsnprintf(output + used, capacity - used, format, arguments);
    va_end(arguments);
    if (written < 0 || static_cast<std::size_t>(written) >= capacity - used) return false;
    used += static_cast<std::size_t>(written); return true;
}

bool stateValue(char* output, std::size_t capacity, std::size_t& used, const Ess::StateObservation& v) {
    if (!append(output, capacity, used, "{\"block\":%u,\"config_operation_id\":%lu,", static_cast<unsigned>(v.block), static_cast<unsigned long>(v.configOperationId))) return false;
    switch (v.block) {
    case Ess::StateBlock::MOTION:
        return append(output, capacity, used,
            "\"raw_alarm\":%u,\"alarm_known\":%s,\"raw_motion\":%u,\"unknown_motion_bits\":%u,\"in_position\":%s,\"homing_complete\":%s,\"running\":%s,\"alarm_flag\":%s,\"released\":%s,\"enabled\":%s,\"positive_soft_limit\":%s,\"negative_soft_limit\":%s}",
            v.rawAlarm, v.alarmKnown ? "true" : "false", v.rawMotion, v.unknownMotionBits,
            v.inPosition ? "true" : "false", v.homingComplete ? "true" : "false", v.running ? "true" : "false", v.alarmFlag ? "true" : "false",
            v.released ? "true" : "false", v.enabled ? "true" : "false", v.positiveSoftLimit ? "true" : "false", v.negativeSoftLimit ? "true" : "false");
    case Ess::StateBlock::IO:
        return append(output, capacity, used,
            "\"raw_inputs\":%u,\"raw_outputs\":%u,\"unknown_input_bits\":%u,\"unknown_output_bits\":%u,\"inputs\":[%s,%s,%s,%s],\"outputs\":[%s,%s],\"levels\":\"logical_valid_not_voltage\"}",
            v.rawInputs, v.rawOutputs, v.unknownInputBits, v.unknownOutputBits,
            v.inputs[0] ? "true" : "false", v.inputs[1] ? "true" : "false", v.inputs[2] ? "true" : "false", v.inputs[3] ? "true" : "false",
            v.outputs[0] ? "true" : "false", v.outputs[1] ? "true" : "false");
    case Ess::StateBlock::FEEDBACK:
        return append(output, capacity, used,
            "\"position_words\":[%u,%u],\"raw_speed\":%u,\"pair_known\":%s,\"raw_position\":%lu,\"position_source\":%u,\"word_order_resolution\":%u,\"position_source_resolution\":%u,\"position_signed_resolution\":%u,\"position_scale_resolution\":%u,\"speed_signed_resolution\":%u,\"speed_unit_resolution\":%u,\"physical_units\":\"unresolved\",\"raw_encoder_counts\":null}",
            v.rawPositionWords[0], v.rawPositionWords[1], v.rawSpeed, v.pairKnown ? "true" : "false", static_cast<unsigned long>(v.rawPosition),
            static_cast<unsigned>(v.positionSource), static_cast<unsigned>(v.wordOrderResolution), static_cast<unsigned>(v.positionSourceResolution),
            static_cast<unsigned>(v.positionSignedResolution), static_cast<unsigned>(v.positionScaleResolution), static_cast<unsigned>(v.speedSignedResolution), static_cast<unsigned>(v.speedUnitResolution));
    }
    return false;
}

namespace Core = MotorControlRS;
Core::Status axisArgument() {
    return Core::Status(Core::Err::INVALID_CONFIG, static_cast<int32_t>(Core::AxisError::INVALID_ARGUMENT), "invalid axis arguments");
}
const char* unitName(Core::PositionUnit unit) {
    const char* names[] = {"steps", "fullsteps", "counts", "turn", "deg", "rad", "mm"};
    const auto index = static_cast<unsigned>(unit);
    return index < 7 ? names[index] : "unknown";
}
bool positionUnit(const char* text, Core::PositionUnit& output) {
    for (unsigned i = 0; i < 7; ++i) {
        const auto value = static_cast<Core::PositionUnit>(i);
        if (std::strcmp(text, unitName(value)) == 0) { output = value; return true; }
    }
    return false;
}
bool rateUnit(const char* text, Core::VelocityUnit& output) {
    if (std::strcmp(text, "rpm") == 0) { output = Core::VelocityUnit(Core::PositionUnit::TURNS, Core::TimeUnit::MINUTE); return true; }
    if (std::strcmp(text, "turns/s") == 0) { output = Core::VelocityUnit(Core::PositionUnit::TURNS); return true; }
    for (unsigned i = 0; i < 7; ++i) {
        char name[20]; std::snprintf(name, sizeof(name), "%s/s", unitName(static_cast<Core::PositionUnit>(i)));
        if (std::strcmp(text, name) == 0) { output = Core::VelocityUnit(static_cast<Core::PositionUnit>(i)); return true; }
    }
    return false;
}
bool accelerationUnit(const char* text, Core::AccelerationUnit& output) {
    if (std::strcmp(text, "rpm/s") == 0) { output = Core::AccelerationUnit(Core::PositionUnit::TURNS, Core::TimeUnit::MINUTE, Core::TimeUnit::SECOND); return true; }
    for (unsigned i = 0; i < 7; ++i) {
        char name[20]; std::snprintf(name, sizeof(name), "%s/s2", unitName(static_cast<Core::PositionUnit>(i)));
        if (std::strcmp(text, name) == 0) { output = Core::AccelerationUnit(static_cast<Core::PositionUnit>(i)); return true; }
    }
    return false;
}
bool coordinateFrame(const char* text, Core::CoordinateFrame& output) {
    if (std::strcmp(text, "native") == 0) output = Core::CoordinateFrame::NATIVE;
    else if (std::strcmp(text, "motor") == 0) output = Core::CoordinateFrame::MOTOR;
    else if (std::strcmp(text, "load") == 0) output = Core::CoordinateFrame::LOAD;
    else return false;
    return true;
}
bool relativeBasis(const char* text, Core::RelativeBasis& output) {
    if (std::strcmp(text, "actual") == 0) output = Core::RelativeBasis::ACTUAL;
    else if (std::strcmp(text, "commanded") == 0) output = Core::RelativeBasis::COMMANDED;
    else if (std::strcmp(text, "queued") == 0) output = Core::RelativeBasis::QUEUED;
    else return false;
    return true;
}
bool rounding(const char* text, Core::Rounding& output) {
    if (std::strcmp(text, "exact") == 0) output = Core::Rounding::EXACT;
    else if (std::strcmp(text, "nearest") == 0) output = Core::Rounding::NEAREST;
    else if (std::strcmp(text, "zero") == 0) output = Core::Rounding::TOWARD_ZERO;
    else if (std::strcmp(text, "floor") == 0) output = Core::Rounding::FLOOR;
    else if (std::strcmp(text, "ceil") == 0) output = Core::Rounding::CEIL;
    else return false;
    return true;
}
bool anglePath(const char* text, Core::AnglePath& output) {
    if (std::strcmp(text, "positive") == 0) output = Core::AnglePath::POSITIVE;
    else if (std::strcmp(text, "negative") == 0) output = Core::AnglePath::NEGATIVE;
    else if (std::strcmp(text, "shortest") == 0) output = Core::AnglePath::SHORTEST;
    else return false;
    return true;
}
bool halfTurnTie(const char* text, Core::HalfTurnTie& output) {
    if (std::strcmp(text, "reject") == 0) output = Core::HalfTurnTie::REJECT;
    else if (std::strcmp(text, "positive") == 0) output = Core::HalfTurnTie::POSITIVE;
    else if (std::strcmp(text, "negative") == 0) output = Core::HalfTurnTie::NEGATIVE;
    else return false;
    return true;
}
const char* moveKind(const Core::PositionRequest& request) {
    return request.wrapped ? "angle" : request.relative ? "relative" : "absolute";
}
const char* moveCommand(const Core::PositionRequest& request) {
    return request.wrapped ? "move-angle" : request.relative ? "move-relative" : "move-absolute";
}
Core::Status allowance(const char* text, double& output) {
    Core::Rational value;
    auto status = Core::parseExactNumber(text, value);
    if (!status) return status;
    if (value.numerator < 0) return axisArgument();
    return Core::rationalToDouble(value, output);
}
Core::Status axisRequest(char* const* tokens, std::size_t count, bool prepare, AxisCommand& output) {
    if (prepare) {
        if (count < 4 || count > 9) return axisArgument();
        output.kind = AxisCommandKind::PREPARE;
        auto& p = output.position;
        if (std::strcmp(tokens[0], "relative") == 0) p.relative = true;
        else if (std::strcmp(tokens[0], "absolute") == 0) p.relative = false;
        else if (std::strcmp(tokens[0], "angle") == 0) { p.relative = false; p.wrapped = true; }
        else return axisArgument();
        auto status = Core::parseExactNumber(tokens[1], p.value);
        if (!status) return status;
        if (!positionUnit(tokens[2], p.unit) || !coordinateFrame(tokens[3], p.frame)) return axisArgument();
        std::size_t next = 4;
        if (p.wrapped) {
            if (next + 2 > count || !anglePath(tokens[next], p.path) || !halfTurnTie(tokens[next + 1], p.tie)) return axisArgument();
            next += 2;
        }
        if (p.relative) {
            if (next >= count || !relativeBasis(tokens[next++], p.basis)) return axisArgument();
        }
        if (next < count && !rounding(tokens[next++], p.rounding)) return axisArgument();
        if (next < count) { status = allowance(tokens[next++], p.maximumQuantizationError); if (!status) return status; }
        if (next < count) {
            status = allowance(tokens[next++], p.maximumApproximationError); if (!status) return status;
            if (p.unit != Core::PositionUnit::RADIANS || p.maximumApproximationError <= 0) return axisArgument();
            p.approximate = true;
        }
        return next == count ? Core::Ok() : axisArgument();
    }
    if (count == 1 && std::strcmp(tokens[0], "config") == 0) return Core::Ok();
    if (count == 2 && std::strcmp(tokens[0], "origin") == 0) {
        output.kind = AxisCommandKind::ORIGIN;
        const auto status = Core::parseExactNumber(tokens[1], output.value);
        return status && output.value.denominator != 1 ? axisArgument() : status;
    }
    if (count < 4 || count > 5 || std::strcmp(tokens[0], "config") != 0 || std::strcmp(tokens[1], "set") != 0) return axisArgument();
    const char* names[] = {"command", "gear", "fullsteps", "lead", "encoder-scale", "encoder-id", "encoder-basis", "encoder-polarity", "polarity", "position-unit", "velocity-unit", "acceleration-unit", "native-limits", "soft-limits", "relative-bases"};
    unsigned field = 0;
    for (; field < 15 && std::strcmp(tokens[2], names[field]) != 0; ++field) {}
    if (field == 15) return axisArgument();
    output.kind = AxisCommandKind::CONFIGURE;
    output.field = static_cast<AxisField>(field);
    const bool limits = output.field == AxisField::NATIVE_LIMITS || output.field == AxisField::SOFT_LIMITS;
    const bool scale = field <= static_cast<unsigned>(AxisField::ENCODER_SCALE);
    if (std::strcmp(tokens[3], "none") == 0 && (scale || output.field == AxisField::SOFT_LIMITS) && count == 4) {
        output.clear = true; return Core::Ok();
    }
    if (count != (limits ? 5U : 4U)) return axisArgument();
    switch (output.field) {
    case AxisField::POSITION_UNIT: return positionUnit(tokens[3], output.positionUnit) ? Core::Ok() : axisArgument();
    case AxisField::VELOCITY_UNIT: return rateUnit(tokens[3], output.velocityUnit) ? Core::Ok() : axisArgument();
    case AxisField::ACCELERATION_UNIT: return accelerationUnit(tokens[3], output.accelerationUnit) ? Core::Ok() : axisArgument();
    case AxisField::ENCODER_BASIS:
        if (std::strcmp(tokens[3], "motor") == 0) output.encoderBasis = Core::EncoderBasis::MOTOR_TURN;
        else if (std::strcmp(tokens[3], "load") == 0) output.encoderBasis = Core::EncoderBasis::LOAD_TURN;
        else if (std::strcmp(tokens[3], "linear") == 0) output.encoderBasis = Core::EncoderBasis::MILLIMETRE;
        else return axisArgument();
        return Core::Ok();
    default: break;
    }
    auto status = Core::parseExactNumber(tokens[3], output.value);
    if (!status) return status;
    if (scale) {
        if (output.value.numerator <= 0 || static_cast<uint64_t>(output.value.numerator) > UINT32_MAX || output.value.denominator > UINT32_MAX) return axisArgument();
    } else if (output.value.denominator != 1) return axisArgument();
    if (limits) {
        status = Core::parseExactNumber(tokens[4], output.secondValue);
        if (!status) return status;
        if (output.secondValue.denominator != 1 || output.value.numerator > output.secondValue.numerator) return axisArgument();
    }
    if ((output.field == AxisField::POLARITY || output.field == AxisField::ENCODER_POLARITY) && output.value.numerator != -1 && output.value.numerator != 1) return axisArgument();
    if (output.field == AxisField::ENCODER_ID && (output.value.numerator < 0 || static_cast<uint64_t>(output.value.numerator) > UINT32_MAX)) return axisArgument();
    if (output.field == AxisField::RELATIVE_BASES && (output.value.numerator < 0 || output.value.numerator > 7)) return axisArgument();
    return Core::Ok();
}

bool axisView(char* output, std::size_t capacity, std::size_t& used, const AxisView& view, bool prepare) {
    const auto& c = view.configuration;
    if (!append(output, capacity, used, ",\"bus_traffic\":false,\"motion_command\":false,\"wire_motion\":\"not_requested\",\"configuration_generation\":%lu,\"target\":%lu,\"address\":%u,\"binding_generation\":%lu",
        static_cast<unsigned long>(c.generation), static_cast<unsigned long>(c.target.id), c.target.address, static_cast<unsigned long>(c.target.generation))) return false;
    if (prepare) {
        const auto& p = view.prepared;
        if (!append(output, capacity, used, ",\"requested\":{\"numerator\":%lld,\"denominator\":%llu,\"unit\":\"%s\",\"frame\":%u,\"relative\":%s,\"wrapped\":%s,\"angle_path\":%u,\"half_turn_tie\":%u,\"basis\":%u,\"rounding\":%u},\"requested_native\":",
            static_cast<long long>(p.requested.value.numerator), static_cast<unsigned long long>(p.requested.value.denominator), unitName(p.requested.unit), static_cast<unsigned>(p.requested.frame), p.requested.relative ? "true" : "false", p.requested.wrapped ? "true" : "false", static_cast<unsigned>(p.requested.path), static_cast<unsigned>(p.requested.tie), static_cast<unsigned>(p.requested.basis), static_cast<unsigned>(p.requested.rounding))) return false;
        if (p.exactArithmetic) {
            if (!append(output, capacity, used, "{\"integral\":%lld,\"numerator\":%llu,\"denominator\":%llu,\"negative\":%s},\"requested_native_approximate\":null",
                static_cast<long long>(p.requestedNative.integral), static_cast<unsigned long long>(p.requestedNative.numerator), static_cast<unsigned long long>(p.requestedNative.denominator), p.requestedNative.negative ? "true" : "false")) return false;
        } else if (!append(output, capacity, used, "null,\"requested_native_approximate\":%.17g", p.approximateRequestedNative)) return false;
        return append(output, capacity, used, ",\"effective_native\":%lld,\"endpoint_known\":%s,\"endpoint_native\":%lld,\"displacement_known\":%s,\"displacement_native\":%lld,\"zero_displacement\":%s,\"rounding_error\":%.17g,\"approximation_error_bound\":%.17g,\"exact_arithmetic\":%s}",
            static_cast<long long>(p.effectiveNative), p.endpointKnown ? "true" : "false", static_cast<long long>(p.endpointNative), p.displacementKnown ? "true" : "false", static_cast<long long>(p.displacementNative), p.zeroDisplacement ? "true" : "false", p.roundingError, p.approximationErrorBound, p.exactArithmetic ? "true" : "false");
    }
    if (!append(output, capacity, used, ",\"operator_scales\":[")) return false;
    const Core::UnitScale scales[] = {c.units.commandStepsPerMotorTurn, c.units.motorTurnsPerLoadTurn, c.units.fullStepsPerMotorTurn, c.units.millimetresPerLoadTurn, c.units.encoder.countsPerUnit};
    for (unsigned i = 0; i < 5; ++i) if (!append(output, capacity, used, "%s{\"numerator\":%lu,\"denominator\":%lu,\"source\":%u}", i ? "," : "", static_cast<unsigned long>(scales[i].numerator), static_cast<unsigned long>(scales[i].denominator), static_cast<unsigned>(scales[i].source))) return false;
    return append(output, capacity, used, "],\"encoder_id\":%lu,\"encoder_basis\":%u,\"encoder_polarity\":%d,\"polarity\":%d,\"position_unit\":\"%s\",\"velocity_unit\":{\"position\":\"%s\",\"time\":%u},\"acceleration_unit\":{\"position\":\"%s\",\"velocity_time\":%u,\"acceleration_time\":%u},\"relative_bases\":%u,\"native_limits\":[%lld,%lld],\"soft_limits_known\":%s,\"soft_limits\":[%lld,%lld],\"origin_known\":%s,\"origin_native\":%lld,\"origin_source\":%u,\"encoder_origin_known\":%s,\"encoder_origin_native\":%lld,\"encoder_origin_source\":%u}",
        static_cast<unsigned long>(c.units.encoder.sourceId), static_cast<unsigned>(c.units.encoder.basis), c.units.encoder.polarity, c.units.commandPolarity, unitName(c.units.settings.position), unitName(c.units.settings.velocity.position), static_cast<unsigned>(c.units.settings.velocity.time), unitName(c.units.settings.acceleration.position), static_cast<unsigned>(c.units.settings.acceleration.velocityTime), static_cast<unsigned>(c.units.settings.acceleration.accelerationTime), c.supportedRelativeBases, static_cast<long long>(c.nativeMinimum), static_cast<long long>(c.nativeMaximum), c.softLimitsKnown ? "true" : "false", static_cast<long long>(c.softMinimum), static_cast<long long>(c.softMaximum), c.originKnown ? "true" : "false", static_cast<long long>(c.originNative), static_cast<unsigned>(c.originSource), c.encoderOriginKnown ? "true" : "false", static_cast<long long>(c.encoderOriginNative), static_cast<unsigned>(c.encoderOriginSource));
}

bool stateCache(char* output, std::size_t capacity, std::size_t& used, const Snapshot& snapshot) {
    MotorControlRS::ReadTarget target; target.id = snapshot.address; target.address = snapshot.address; target.generation = snapshot.bindingGeneration;
    if (!append(output, capacity, used, ",\"now_us\":%llu,\"monitoring\":\"%s\",\"atomic_snapshot\":false,\"sample_time\":\"drive_internal_age_undocumented\",\"state_blocks\":[",
        static_cast<unsigned long long>(snapshot.nowUs), snapshot.monitorState.settings.enabled ? "enabled" : "disabled")) return false;
    for (uint8_t i = 0; i < Ess::STATE_BLOCK_COUNT; ++i) {
        const StateCache::Block* b = snapshot.stateCache ? &snapshot.stateCache->blocks[i] : nullptr;
        const bool valid = b && b->valid;
        const bool same = b && current(*b, target);
        const bool recent = b && fresh(*b, target, snapshot.nowUs, static_cast<uint64_t>(snapshot.staleAfterMs) * 1000);
        const uint32_t configuration = valid && b->value.target.id == snapshot.cachedConfigAddress &&
            b->value.target.address == snapshot.cachedConfigAddress &&
            b->value.target.generation == snapshot.cachedConfigGeneration ? snapshot.cachedConfigId : 0;
        if (!append(output, capacity, used,
            "%s{\"block\":%u,\"valid\":%s,\"current\":%s,\"fresh\":%s,\"source\":\"%s\",\"target\":%lu,\"address\":%u,\"generation\":%lu,\"operation_id\":%lu,\"attempt_known\":%s,\"last_attempt_ok\":%s,\"last_attempt_us\":%llu,\"last_attempt_target\":%lu,\"last_attempt_address\":%u,\"last_attempt_generation\":%lu,\"last_attempt_operation_id\":%lu,\"last_attempt_status\":\"%s\",\"last_attempt_detail\":%ld,\"last_success_us\":%llu,\"observed_earliest_us\":%llu,\"observed_latest_us\":%llu,\"delivered_us\":%llu,\"age_us\":",
            i ? "," : "", i, valid ? "true" : "false", same ? "true" : "false", recent ? "true" : "false", valid ? "checked_rtu_register" : "absent",
            static_cast<unsigned long>(valid ? b->value.target.id : 0), valid ? b->value.target.address : 0, static_cast<unsigned long>(valid ? b->value.target.generation : 0),
            static_cast<unsigned long>(valid ? b->value.operationId : 0), b && b->attemptKnown ? "true" : "false", b && b->lastAttemptOk ? "true" : "false",
            static_cast<unsigned long long>(b ? b->lastAttemptUs : 0), static_cast<unsigned long>(b ? b->lastAttemptTarget.id : 0), b ? b->lastAttemptTarget.address : 0,
            static_cast<unsigned long>(b ? b->lastAttemptTarget.generation : 0), static_cast<unsigned long>(b ? b->lastAttemptOperationId : 0),
            b ? MotorControlRS::errToString(b->lastAttemptStatus.code) : "OK", static_cast<long>(b ? b->lastAttemptStatus.detail : 0),
            static_cast<unsigned long long>(valid ? b->lastSuccessUs : 0), static_cast<unsigned long long>(valid ? b->observedEarliestUs : 0),
            static_cast<unsigned long long>(valid ? b->observedLatestUs : 0), static_cast<unsigned long long>(valid ? b->deliveredUs : 0))) return false;
        if (valid && snapshot.nowUs >= b->observedEarliestUs) {
            if (!append(output, capacity, used, "%llu", static_cast<unsigned long long>(ageUs(*b, snapshot.nowUs)))) return false;
        } else if (!append(output, capacity, used, "null")) return false;
        if (!append(output, capacity, used, ",\"invalidated_us\":%llu", static_cast<unsigned long long>(b ? b->invalidatedUs : 0))) return false;
        if (i == static_cast<uint8_t>(Ess::StateBlock::FEEDBACK) &&
            !append(output, capacity, used, ",\"current_config_operation_id\":%lu,\"interpretation_current\":%s",
                static_cast<unsigned long>(configuration),
                valid && b->value.configOperationId && b->value.configOperationId == configuration ? "true" : "false")) return false;
        if (!append(output, capacity, used, ",\"value\":")) return false;
        if (valid) { if (!stateValue(output, capacity, used, b->value)) return false; }
        else if (!append(output, capacity, used, "null")) return false;
        if (!append(output, capacity, used, "}")) return false;
    }
    if (!append(output, capacity, used, "],\"selected_target\":%lu,\"selected_address\":%u,\"selected_generation\":%lu,\"communication_known\":%s,\"communication_target\":%lu,\"communication_address\":%u,\"communication_generation\":%lu,\"communication_earliest_us\":%llu,\"communication_latest_us\":%llu", static_cast<unsigned long>(target.id), target.address, static_cast<unsigned long>(target.generation), snapshot.communicationKnown ? "true" : "false", static_cast<unsigned long>(snapshot.communicationTarget.id), snapshot.communicationTarget.address, static_cast<unsigned long>(snapshot.communicationTarget.generation), static_cast<unsigned long long>(snapshot.communicationEarliestUs), static_cast<unsigned long long>(snapshot.communicationLatestUs))) return false;
    if (!append(output, capacity, used, ",\"age_source\":\"model_probe\",\"communication_age_us\":")) return false;
    if (snapshot.communicationKnown && snapshot.nowUs >= snapshot.communicationEarliestUs)
        return append(output, capacity, used, "%llu", static_cast<unsigned long long>(snapshot.nowUs - snapshot.communicationEarliestUs));
    return append(output, capacity, used, "null");
}

const Entry* find(const char* name) {
    for (const Entry& entry : COMMANDS) if (std::strcmp(name, entry.name) == 0) return &entry;
    return nullptr;
}

bool number(const char* text, uint32_t& value) {
    if (!*text) return false;
    uint32_t result = 0;
    for (; *text; ++text) {
        if (*text < '0' || *text > '9') return false;
        const uint32_t digit = static_cast<uint32_t>(*text - '0');
        if (result > (std::numeric_limits<uint32_t>::max() - digit) / 10) return false;
        result = result * 10 + digit;
    }
    value = result;
    return true;
}

const char* boolean(bool value) { return value ? "true" : "false"; }
const char* motorKind(Core::ActionKind kind) {
    switch (kind) { case Core::ActionKind::ENABLE: return "enable";
    case Core::ActionKind::RELEASE: return "release"; case Core::ActionKind::CLEAR_ALARM: return "clear_alarm";
    case Core::ActionKind::STOP: return "stop"; case Core::ActionKind::CLEAR_POSITION: return "clear_position"; } return "unknown";
}
const char* motorCommand(Core::ActionKind kind) {
    return kind == Core::ActionKind::CLEAR_POSITION ? "position-clear" : kind == Core::ActionKind::RELEASE ? "motor-release" : kind == Core::ActionKind::CLEAR_ALARM ? "alarm-clear" : motorKind(kind);
}
const char* stopPolicy(const Core::ActionRequest& request) {
    return request.kind != Core::ActionKind::STOP ? "null" : request.stop.behavior == Core::StopBehavior::CONFIGURED_DECELERATION ? "\"normal\"" : "\"direct\"";
}
const char* executionName(Core::ActionExecution execution) {
    switch (execution) { case Core::ActionExecution::NOT_TRANSMITTED: return "not_transmitted";
    case Core::ActionExecution::ACKNOWLEDGED: return "acknowledged";
    case Core::ActionExecution::REJECTED: return "rejected"; case Core::ActionExecution::UNKNOWN: return "unknown"; } return "unknown";
}
const char* motorOutcome(Core::ActionOutcome outcome) {
    switch (outcome) { case Core::ActionOutcome::NONE: return "none"; case Core::ActionOutcome::OBSERVED: return "observed";
    case Core::ActionOutcome::REPLY_ERROR: return "reply_error"; case Core::ActionOutcome::TRANSPORT_ERROR: return "transport_error";
    case Core::ActionOutcome::CANCELLED: return "cancelled"; case Core::ActionOutcome::DEADLINE: return "deadline";
    case Core::ActionOutcome::TIMING_UNQUALIFIED: return "timing_unqualified";
    case Core::ActionOutcome::UNCONFIRMED_RESPONSE: return "unconfirmed_response";
    case Core::ActionOutcome::OBSERVATION_LIMIT: return "observation_limit"; } return "unknown";
}
void hex(const uint8_t* bytes, std::size_t length, char* output, std::size_t capacity) {
    static const char DIGITS[] = "0123456789ABCDEF";
    const std::size_t count = bytes ? (length < (capacity - 1) / 2 ? length : (capacity - 1) / 2) : 0;
    for (std::size_t i = 0; i < count; ++i) {
        output[2 * i] = DIGITS[bytes[i] >> 4];
        output[2 * i + 1] = DIGITS[bytes[i] & 15];
    }
    output[2 * count] = '\0';
}
bool actionEvidence(char* output, std::size_t capacity, std::size_t& used, const Ess::ActionEvidence& evidence) {
    char raw[Ess::ACTION_MAX_REPLY_BYTES * 2 + 1]; hex(evidence.raw, evidence.length, raw, sizeof(raw));
    return append(output, capacity, used,
        "{\"step\":%u,\"event\":%u,\"raw_hex\":\"%s\",\"received_length\":%u,\"tx_accepted\":%u,\"tx_complete\":%s,\"response_confirmed\":%s,\"qualified\":%s,\"execution_unknown\":%s,\"earliest_us\":%llu,\"latest_us\":%llu,\"delivered_us\":%llu,\"transport_detail\":%ld,\"status\":\"%s\",\"detail\":%ld,\"frame_error\":%u}",
        evidence.step, static_cast<unsigned>(evidence.event), raw, static_cast<unsigned>(evidence.receivedLength), static_cast<unsigned>(evidence.txAccepted),
        boolean(evidence.txComplete), boolean(evidence.responseConfirmed), boolean(evidence.qualified), boolean(evidence.executionUnknown),
        static_cast<unsigned long long>(evidence.earliestUs), static_cast<unsigned long long>(evidence.latestUs), static_cast<unsigned long long>(evidence.deliveredUs),
        static_cast<long>(evidence.transportDetail), Core::errToString(evidence.status.code), static_cast<long>(evidence.status.detail), static_cast<unsigned>(evidence.frameError));
}
const char* actionName(Action value) {
    switch (value) {
    case Action::OK: return "accepted";
    case Action::BUSY: return "busy";
    case Action::RECOVERY_REQUIRED: return "recovery_required";
    case Action::UNAVAILABLE: return "unavailable";
    case Action::FAILED: return "failed";
    case Action::QUEUE_FULL: return "queue_full";
    case Action::RESULTS_FULL: return "results_full";
    case Action::IDS_EXHAUSTED: return "ids_exhausted";
    case Action::INVALID: return "invalid";
    case Action::ALREADY_TERMINAL: return "already_terminal";
    case Action::TIMING_UNQUALIFIED: return "timing_unqualified";
    case Action::UNSUPPORTED: return "unsupported";
    case Action::AXIS_CONFLICT: return "axis_conflict";
    }
    return "failed";
}
const char* outcomeName(Rtu::Outcome value) {
    switch (value) {
    case Rtu::Outcome::QUEUE_EXPIRED: return "queue_expired";
    case Rtu::Outcome::TRANSPORT: return "transport";
    case Rtu::Outcome::SUCCESS: return "success";
    case Rtu::Outcome::DEVICE_REJECTED: return "device_rejected";
    case Rtu::Outcome::INVALID_REPLY: return "invalid_reply";
    case Rtu::Outcome::CANCELLED: return "cancelled";
    case Rtu::Outcome::DISPATCH_EXPIRED: return "dispatch_expired";
    }
    return "unknown";
}
const char* cancellationName(Rtu::Cancellation value) {
    switch (value) {
    case Rtu::Cancellation::NONE: return "none";
    case Rtu::Cancellation::REQUEST: return "request";
    case Rtu::Cancellation::GENERATION: return "generation";
    case Rtu::Cancellation::RECOVERY: return "recovery";
    }
    return "unknown";
}

} // namespace

bool Console::outstanding(uint32_t id) const noexcept {
    if (stopReply_.pending && stopReply_.id == id) return true;
    for (const auto& item : outstanding_) if (item.commandId == id) return true;
    return false;
}
bool Console::track(uint32_t id, uint32_t operationId, bool stop) noexcept {
    const std::size_t begin = stop ? OUTSTANDING_CAPACITY - 1 : 0;
    const std::size_t end = stop ? OUTSTANDING_CAPACITY : OUTSTANDING_CAPACITY - 1;
    for (std::size_t i = begin; i < end; ++i) { auto& item = outstanding_[i]; if (!item.commandId) {
        item.commandId = id; item.operationId = operationId; return true;
    } }
    return false;
}
void Console::untrack(uint32_t operationId) noexcept {
    for (auto& item : outstanding_) if (item.operationId == operationId) item = Outstanding();
}
void Console::emit(uint32_t terminalOperation) noexcept {
    if (outputPending_) { ++inputDropped_; return; }
    std::memcpy(pendingOutput_, output_, std::strlen(output_) + 1);
    outputPending_ = true;
    pendingTerminalOperation_ = terminalOperation;
    serviceOutput();
}
bool Console::serviceOutput() noexcept {
    if (outputPending_) {
        if (!host_.emitLine || !host_.emitLine(host_.context, pendingOutput_, std::strlen(pendingOutput_))) return false;
        outputPending_ = false;
        if (pendingTerminalOperation_) untrack(pendingTerminalOperation_);
        pendingTerminalOperation_ = 0;
    }
    if (stopReply_.pending) {
        const StopReply reply = stopReply_;
        stopReply_ = StopReply();
        // action emits at most this one additional line. The cleared pending
        // field bounds serviceOutput's nested call to one level.
        action(reply.id, "stop", reply.result, reply.address, reply.operationId);
        return !outputPending_;
    }
    return true;
}

void Console::error(uint32_t id, const char* command, const char* reason) noexcept {
    std::snprintf(output_, sizeof(output_),
        "{\"type\":\"reply\",\"profile\":\"ess_rs\",\"id\":%lu,\"command\":\"%s\",\"ok\":false,\"result\":\"%s\"}",
        static_cast<unsigned long>(id), command, reason);
    emit();
}

void Console::action(uint32_t id, const char* command, Action result, uint8_t address, uint32_t operationId) noexcept {
    const bool typed = std::strcmp(command, "read-identity") == 0 || std::strcmp(command, "read-config") == 0 || std::strcmp(command, "read-state") == 0;
    const bool motor = std::strcmp(command, "enable") == 0 || std::strcmp(command, "motor-release") == 0 || std::strcmp(command, "alarm-clear") == 0 || std::strcmp(command, "position-clear") == 0 || std::strcmp(command, "stop") == 0 || std::strncmp(command, "move-", 5) == 0 || std::strcmp(command, "velocity") == 0;
    std::snprintf(output_, sizeof(output_),
        "{\"type\":\"reply\",\"profile\":\"ess_rs\",\"id\":%lu,\"command\":\"%s\",\"ok\":%s,\"result\":\"%s\",\"address\":%u,\"operation_id\":%lu%s}",
        static_cast<unsigned long>(id), command, boolean(result == Action::OK),
        result == Action::OK && !typed && !motor && (std::strcmp(command, "probe") != 0 && std::strcmp(command, "capture-read") != 0 && std::strcmp(command, "recover") != 0) ? "done" : actionName(result), address,
        static_cast<unsigned long>(operationId), typed ? (std::strcmp(command, "read-identity") == 0 ? ",\"read_kind\":\"identity\"" : std::strcmp(command, "read-config") == 0 ? ",\"read_kind\":\"config\"" : ",\"read_kind\":\"state\"") : "");
    emit();
}

void Console::feed(char value) noexcept {
    if (value == '\n' && afterCr_) { afterCr_ = false; return; }
    afterCr_ = value == '\r';
    if (value == '\r' || value == '\n') {
        if (overflow_) error(0, "input", "line_too_long");
        else if (invalid_) error(0, "input", "invalid_input");
        else if (length_) { line_[length_] = '\0'; dispatch(); }
        length_ = 0;
        overflow_ = invalid_ = false;
        return;
    }
    const unsigned char byte = static_cast<unsigned char>(value);
    if ((byte < 32 && value != '\t') || byte > 126) { invalid_ = true; return; }
    if (length_ + 1 >= sizeof(line_)) { overflow_ = true; return; }
    if (!overflow_ && !invalid_) line_[length_++] = value;
}

void Console::dispatch() noexcept {
    char* tokens[20] = {}; // Full-width rationals, path policies and bounded preview options.
    std::size_t count = 0;
    char* next = line_;
    while (*next) {
        while (*next == ' ' || *next == '\t') ++next;
        if (!*next) break;
        if (count == sizeof(tokens) / sizeof(tokens[0])) { error(0, "input", "too_many_arguments"); return; }
        tokens[count++] = next;
        while (*next && *next != ' ' && *next != '\t') ++next;
        if (*next) *next++ = '\0';
    }
    if (!count) return;
    while (outstanding(nextId_)) if (++nextId_ == 0) nextId_ = 1;
    uint32_t id = nextId_;
    if (++nextId_ == 0) nextId_ = 1;
    std::size_t first = 0;
    if (tokens[0][0] == '@') {
        if (!number(tokens[0] + 1, id) || id == 0) { error(0, "input", "invalid_id"); return; }
        first = 1;
        if (count == first) { error(id, "input", "missing_command"); return; }
    }
    if (outstanding(id)) { error(id, "input", "duplicate_id"); return; }
    const Entry* entry = find(tokens[first]);
    const char* capability = count > first + 2 && std::strcmp(tokens[first], "profile") == 0 &&
        std::strcmp(tokens[first + 1], "ess_rs") == 0 ? tokens[first + 2] : tokens[first];
    if (std::strcmp(capability, "jog") == 0 || std::strcmp(capability, "torque") == 0 ||
        std::strcmp(capability, "current") == 0 || std::strcmp(capability, "velocity-update") == 0) {
        error(id, capability, "unsupported"); return;
    }
    if (!entry) { error(id, "unknown", "unknown_command"); return; }
    const bool nativeVelocity = entry->command == Command::PROFILE && count > first + 2 &&
        std::strcmp(tokens[first + 1], "ess_rs") == 0 && std::strcmp(tokens[first + 2], "velocity") == 0;
    if (entry->command == Command::VELOCITY || nativeVelocity) {
        if (outputPending()) { ++inputDropped_; return; }
        if (!host_.startVelocity || !host_.snapshot || !host_.axis) { error(id, "velocity", "unavailable"); return; }
        const std::size_t args = first + (nativeVelocity ? 3 : 1);
        Core::VelocityRequest request; uint32_t duration = 0;
        if (count < args + 6 || !Core::parseExactNumber(tokens[args], request.value) ||
            !rateUnit(tokens[args + 1], request.unit) || !coordinateFrame(tokens[args + 2], request.frame) ||
            !number(tokens[args + 3], duration) || duration < 1 || duration > 1000 ||
            std::strcmp(tokens[args + 4], "configured") != 0) { error(id, "velocity", "invalid_arguments"); return; }
        request.durationUs = duration * 1000; request.ramp = Core::VelocityRamp::VERIFIED_CONFIGURED;
        if (std::strcmp(tokens[args + 5], "normal") == 0) request.stop.behavior = Core::StopBehavior::CONFIGURED_DECELERATION;
        else if (std::strcmp(tokens[args + 5], "direct") == 0) request.stop.behavior = Core::StopBehavior::DIRECT;
        else { error(id, "velocity", "unsupported_policy"); return; }
        std::size_t next = args + 6;
        if (next < count && std::strcmp(tokens[next], "round") == 0) {
            ++next;
            if (next + 2 > count || !rounding(tokens[next], request.rounding) ||
                !allowance(tokens[next + 1], request.maximumQuantizationErrorRpm)) { error(id, "velocity", "invalid_arguments"); return; }
            next += 2;
            if (next < count && std::strcmp(tokens[next], "approx") == 0) {
                ++next;
                if (next >= count || request.unit.position != Core::PositionUnit::RADIANS ||
                    !allowance(tokens[next++], request.maximumApproximationErrorRpm) || request.maximumApproximationErrorRpm <= 0) {
                    error(id, "velocity", "invalid_arguments"); return;
                }
                request.approximate = true;
            }
        }
        Snapshot snapshot; host_.snapshot(host_.context, snapshot); uint32_t address = snapshot.address;
        if (count > next + 1 || (count == next + 1 && !number(tokens[next], address))) { error(id, "velocity", "invalid_arguments"); return; }
        if (address < 1 || address > 247) { error(id, "velocity", "invalid_address"); return; }
        AxisCommand query; AxisView axis;
        if (!host_.axis(host_.context, query, axis)) { error(id, "velocity", "invalid_axis_configuration"); return; }
        request.configurationGeneration = axis.configuration.generation;
        bool available = false;
        for (std::size_t i = 0; i < OUTSTANDING_CAPACITY - 1; ++i) available = available || !outstanding_[i].commandId;
        uint32_t operationId = 0;
        const Action result = available ? host_.startVelocity(host_.context, id, static_cast<uint8_t>(address), request, operationId) : Action::BUSY;
        if (result == Action::OK) track(id, operationId);
        action(id, "velocity", result, static_cast<uint8_t>(address), result == Action::OK ? operationId : 0);
        return;
    }
    const bool nativeMove = entry->command == Command::PROFILE && count > first + 2 &&
        std::strcmp(tokens[first + 1], "ess_rs") == 0 && (std::strcmp(tokens[first + 2], "move-relative") == 0 ||
        std::strcmp(tokens[first + 2], "move-absolute") == 0 || std::strcmp(tokens[first + 2], "move-angle") == 0);
    if (entry->command == Command::MOVE || nativeMove) {
        if (outputPending()) { ++inputDropped_; return; }
        const char* kind = nativeMove ? tokens[first + 2] + 5 : count > first + 1 ? tokens[first + 1] : "";
        Core::MoveRequest move;
        if (std::strcmp(kind, "relative") == 0) move.position.relative = true;
        else if (std::strcmp(kind, "absolute") == 0) move.position.relative = false;
        else if (std::strcmp(kind, "angle") == 0) { move.position.relative = false; move.position.wrapped = true; }
        else { error(id, "move", "invalid_arguments"); return; }
        const char* command = moveCommand(move.position);
        if (!host_.startMove || !host_.snapshot || !host_.axis) { error(id, command, "unavailable"); return; }
        const std::size_t args = first + (nativeMove ? 3 : 2);
        const std::size_t required = move.position.wrapped ? 7 : 5;
        if (count < args + required) { error(id, command, "invalid_arguments"); return; }
        Core::Rational speed;
        if (!Core::parseExactNumber(tokens[args], move.position.value) ||
            !positionUnit(tokens[args + 1], move.position.unit) || !coordinateFrame(tokens[args + 2], move.position.frame)) { error(id, command, "invalid_arguments"); return; }
        std::size_t next = args + 3;
        if (move.position.wrapped) {
            if (!anglePath(tokens[next], move.position.path) || !halfTurnTie(tokens[next + 1], move.position.tie)) { error(id, command, "invalid_arguments"); return; }
            next += 2;
        }
        if (!Core::parseExactNumber(tokens[next++], speed) || speed.denominator != 1 || speed.numerator <= 0 || speed.numerator > UINT16_MAX ||
            std::strcmp(tokens[next++], "configured") != 0) { error(id, command, "invalid_arguments"); return; }
        move.speedRpm = static_cast<uint16_t>(speed.numerator); move.ramp = Core::MoveRamp::VERIFIED_CONFIGURED;
        if (next < count && std::strcmp(tokens[next], "basis") == 0) {
            if (!move.position.relative || ++next >= count || !relativeBasis(tokens[next++], move.position.basis)) { error(id, command, "invalid_arguments"); return; }
        }
        if (next < count && std::strcmp(tokens[next], "round") == 0) {
            ++next;
            if (next + 2 > count || !rounding(tokens[next], move.position.rounding) ||
                !allowance(tokens[next + 1], move.position.maximumQuantizationError)) { error(id, command, "invalid_arguments"); return; }
            next += 2;
            if (next < count && std::strcmp(tokens[next], "approx") == 0) {
                ++next;
                if (next >= count || move.position.unit != Core::PositionUnit::RADIANS ||
                    !allowance(tokens[next++], move.position.maximumApproximationError) || move.position.maximumApproximationError <= 0) { error(id, command, "invalid_arguments"); return; }
                move.position.approximate = true;
            }
        }
        Snapshot snapshot; host_.snapshot(host_.context, snapshot);
        uint32_t address = snapshot.address;
        if (count > next + 1 || (count == next + 1 && !number(tokens[next], address))) { error(id, command, "invalid_arguments"); return; }
        if (address < 1 || address > 247) { error(id, command, "invalid_address"); return; }
        AxisCommand query; AxisView axis;
        if (!host_.axis(host_.context, query, axis)) { error(id, command, "invalid_axis_configuration"); return; }
        move.position.configurationGeneration = axis.configuration.generation;
        bool available = false;
        for (std::size_t i = 0; i < OUTSTANDING_CAPACITY - 1; ++i) available = available || !outstanding_[i].commandId;
        uint32_t operationId = 0;
        const Action result = available ? host_.startMove(host_.context, id, static_cast<uint8_t>(address), move, operationId) : Action::BUSY;
        if (result == Action::OK) track(id, operationId);
        action(id, command, result, static_cast<uint8_t>(address), result == Action::OK ? operationId : 0);
        return;
    }
    Core::ActionRequest request;
    const char* actionCommand = nullptr;
    std::size_t actionArgs = first + 1;
    const bool nativeAction = entry->command == Command::PROFILE && count > first + 2 &&
        std::strcmp(tokens[first + 1], "ess_rs") == 0;
    const char* actionToken = nativeAction ? tokens[first + 2] : tokens[first];
    if (entry->command == Command::ENABLE || (nativeAction && std::strcmp(actionToken, "enable") == 0)) {
        request.kind = Core::ActionKind::ENABLE; actionCommand = "enable";
    } else if (entry->command == Command::MOTOR_RELEASE || (nativeAction && std::strcmp(actionToken, "release") == 0)) {
        request.kind = Core::ActionKind::RELEASE; actionCommand = "motor-release";
    } else if (entry->command == Command::ALARM_CLEAR || (nativeAction && std::strcmp(actionToken, "clear-alarm") == 0)) {
        request.kind = Core::ActionKind::CLEAR_ALARM; actionCommand = "alarm-clear";
    } else if (entry->command == Command::POSITION_CLEAR || (nativeAction && std::strcmp(actionToken, "clear-position") == 0)) {
        request.kind = Core::ActionKind::CLEAR_POSITION; actionCommand = "position-clear";
    } else if (entry->command == Command::STOP || (nativeAction &&
        (std::strcmp(actionToken, "normal-stop") == 0 || std::strcmp(actionToken, "emergency-stop") == 0))) {
        request.kind = Core::ActionKind::STOP; actionCommand = "stop";
        if (nativeAction) request.stop.behavior = std::strcmp(actionToken, "normal-stop") == 0 ? Core::StopBehavior::CONFIGURED_DECELERATION : Core::StopBehavior::DIRECT;
        else {
            if (count <= actionArgs) { error(id, "stop", "invalid_arguments"); return; }
            if (std::strcmp(tokens[actionArgs], "normal") == 0) request.stop.behavior = Core::StopBehavior::CONFIGURED_DECELERATION;
            else if (std::strcmp(tokens[actionArgs], "direct") == 0) request.stop.behavior = Core::StopBehavior::DIRECT;
            else { error(id, "stop", "unsupported_policy"); return; }
            ++actionArgs;
        }
    }
    if (actionCommand) {
        const bool stop = request.kind == Core::ActionKind::STOP;
        if ((outputPending() && !stop) || stopReply_.pending) { ++inputDropped_; return; }
        if (nativeAction) actionArgs = first + 3;
        if (!host_.startAction || !host_.snapshot) { error(id, actionCommand, "unavailable"); return; }
        Snapshot snapshot; host_.snapshot(host_.context, snapshot);
        uint32_t address = snapshot.address;
        if (count > actionArgs + 1 || (count == actionArgs + 1 && !number(tokens[actionArgs], address))) {
            error(id, actionCommand, "invalid_arguments"); return;
        }
        if (address < 1 || address > 247) { error(id, actionCommand, "invalid_address"); return; }
        bool available = false;
        const std::size_t begin = stop ? OUTSTANDING_CAPACITY - 1 : 0;
        const std::size_t end = stop ? OUTSTANDING_CAPACITY : OUTSTANDING_CAPACITY - 1;
        for (std::size_t i = begin; i < end; ++i) available = available || !outstanding_[i].commandId;
        uint32_t operationId = 0;
        const Action result = available ? host_.startAction(host_.context, id, static_cast<uint8_t>(address), request, operationId) : Action::BUSY;
        if (result == Action::OK) track(id, operationId, stop);
        if (stop && outputPending_) {
            stopReply_.pending = true; stopReply_.id = id;
            stopReply_.address = static_cast<uint8_t>(address); stopReply_.result = result;
            stopReply_.operationId = result == Action::OK ? operationId : 0;
        } else action(id, actionCommand, result, static_cast<uint8_t>(address), result == Action::OK ? operationId : 0);
        return;
    }
    // Cancellation, monitor off and the validated stop path above remain responsive.
    const bool monitorOff = entry->command == Command::MONITOR && count == first + 2 && std::strcmp(tokens[first + 1], "off") == 0;
    if (outputPending() && entry->command != Command::CANCEL && !monitorOff) { ++inputDropped_; return; }
    if (entry->command == Command::AXIS || entry->command == Command::PREPARE) {
        if (!host_.axis) { error(id, entry->name, "unavailable"); return; }
        AxisCommand request;
        auto status = axisRequest(tokens + first + 1, count - first - 1, entry->command == Command::PREPARE, request);
        AxisView view;
        if (status) status = host_.axis(host_.context, request, view);
        const int prefix = std::snprintf(output_, sizeof(output_),
            "{\"type\":\"reply\",\"profile\":\"ess_rs\",\"id\":%lu,\"command\":\"%s\",\"ok\":%s,\"code\":\"%s\",\"detail\":%ld",
            static_cast<unsigned long>(id), entry->name, status ? "true" : "false", Core::errToString(status.code), static_cast<long>(status.detail));
        if (prefix < 0 || static_cast<std::size_t>(prefix) >= sizeof(output_)) { error(id, entry->name, "output_full"); return; }
        std::size_t used = static_cast<std::size_t>(prefix);
        if (status) {
            if (!axisView(output_, sizeof(output_), used, view, request.kind == AxisCommandKind::PREPARE)) { error(id, entry->name, "output_full"); return; }
        } else if (!append(output_, sizeof(output_), used, ",\"bus_traffic\":false,\"motion_command\":false}")) { error(id, entry->name, "output_full"); return; }
        emit(); return;
    }
    // The asynchronous terminal event uses the canonical probe name too.
    if (entry->command == Command::PROBE) entry = find("probe");
    if (entry->command == Command::HEALTH && count > first + 1) {
        if (std::strcmp(tokens[first + 1], "check") != 0) { error(id, "health", "invalid_arguments"); return; }
        entry = &HEALTH_ENTRY; ++first;
    }
    const bool profileRoute = entry->command == Command::PROFILE;
    if (entry->command == Command::READ || entry->command == Command::PROFILE) {
        std::size_t kind = first + 1;
        if (entry->command == Command::PROFILE) {
            if (kind >= count || std::strcmp(tokens[kind], "ess_rs") != 0) { error(id, "profile", "invalid_profile"); return; }
            ++kind;
        }
        if (kind >= count) { error(id, entry->name, "invalid_arguments"); return; }
        if (std::strcmp(tokens[kind], "identity") == 0) entry = &IDENTITY_ENTRY;
        else if (std::strcmp(tokens[kind], "config") == 0) entry = &CONFIG_ENTRY;
        else if (std::strcmp(tokens[kind], "state") == 0) entry = &STATE_ENTRY;
        else if (entry->command == Command::PROFILE && std::strcmp(tokens[kind], "caps") == 0) entry = find("caps");
        else { error(id, entry->name, "invalid_arguments"); return; }
        first = kind;
    }
    const std::size_t args = count - first - 1;
    const char* arg = args ? tokens[first + 1] : nullptr;
    const bool loadCommand = entry->command == Command::LOAD;
    const bool typedCommand = entry->command == Command::READ_IDENTITY || entry->command == Command::READ_CONFIG || entry->command == Command::READ_STATE || entry->command == Command::HEALTH_CHECK;
    const bool readCommand = entry->command == Command::PROBE || entry->command == Command::CAPTURE_READ || typedCommand;
    const bool optionalArg = entry->command == Command::HELP ||
        entry->command == Command::STATS || readCommand ||
        entry->command == Command::RESULT || entry->command == Command::CANCEL;
    const bool validArgs = entry->command == Command::MONITOR ? (args == 0 || args == 1 || args == 2) : loadCommand ? (args == 0 || args == 3) :
        entry->command == Command::RELEASE ? args == 1 : args <= (optionalArg ? 1U : 0U);
    if (!validArgs) {
        error(id, entry->name, "invalid_arguments"); return;
    }
    if (!host_.snapshot || !host_.startProbe || !host_.recover || !host_.resetStats) {
        error(id, entry->name, "unavailable"); return;
    }

    // Validate every argument before querying application state or admitting work.
    uint32_t address = 0;
    uint32_t operationId = 0;
    if ((entry->command == Command::RESULT || entry->command == Command::CANCEL || entry->command == Command::RELEASE) &&
        arg && (!number(arg, operationId) || !operationId)) {
        error(id, entry->name, "invalid_operation_id"); return;
    }
    if (readCommand && arg &&
        (!number(arg, address) || address < 1 || address > 247)) {
        error(id, entry->name, "invalid_address"); return;
    }
    if (entry->command == Command::STATS && arg && std::strcmp(arg, "reset") != 0) {
        error(id, entry->name, "invalid_arguments"); return;
    }
    LoadSettings loadSettings;
    if (loadCommand && args &&
        (!number(tokens[first + 1], loadSettings.workUs) || loadSettings.workUs > 5000 ||
         !number(tokens[first + 2], loadSettings.ownerDelayUs) || loadSettings.ownerDelayUs > 20000 ||
         !number(tokens[first + 3], loadSettings.consoleBytes) || loadSettings.consoleBytes > 256)) {
        error(id, entry->name, "invalid_arguments"); return;
    }
    MonitorSettings monitorSettings;
    if (entry->command == Command::MONITOR && args) {
        if (args == 1 && std::strcmp(arg, "off") == 0) monitorSettings.enabled = false;
        else if (args == 2 && number(arg, monitorSettings.intervalMs) &&
            monitorSettings.intervalMs >= 100 && monitorSettings.intervalMs <= 60000 &&
            number(tokens[first + 2], monitorSettings.count) && monitorSettings.count >= 1 && monitorSettings.count <= 1000)
            monitorSettings.enabled = true;
        else { error(id, "monitor", "invalid_arguments"); return; }
    }
    const Entry* described = entry->command == Command::HELP && arg ? find(arg) : nullptr;
    if (entry->command == Command::HELP && arg && !described) {
        error(id, entry->name, "unknown_command"); return;
    }
    if ((loadCommand || (described && described->command == Command::LOAD)) && !host_.load) {
        error(id, entry->name, "unavailable"); return;
    }
    const auto callable = [this](Command c) {
        switch (c) {
        case Command::AXIS: case Command::PREPARE: return host_.axis != nullptr;
        case Command::MONITOR: return host_.monitor != nullptr;
        case Command::LOAD: return host_.load != nullptr;
        case Command::CAPTURE_READ: return host_.startCaptureRead != nullptr;
        case Command::ENABLE: case Command::MOTOR_RELEASE: case Command::ALARM_CLEAR: case Command::STOP: case Command::POSITION_CLEAR: return host_.startAction && host_.snapshot;
        case Command::MOVE: return host_.startMove && host_.snapshot && host_.axis;
        case Command::VELOCITY: return host_.startVelocity && host_.snapshot && host_.axis;
        case Command::PROFILE: return ((host_.startTypedRead || host_.startAction) && host_.snapshot) ||
            ((host_.startMove || host_.startVelocity) && host_.snapshot && host_.axis);
        case Command::READ: case Command::READ_IDENTITY: case Command::READ_CONFIG: case Command::READ_STATE: case Command::HEALTH_CHECK: return host_.startTypedRead != nullptr;
        case Command::RESULT: return host_.result != nullptr;
        case Command::CANCEL: return host_.cancel != nullptr;
        case Command::RELEASE: return host_.release != nullptr;
        default: return true;
        }
    };
    if (!callable(entry->command) || (profileRoute && !host_.startTypedRead) || (described && !callable(described->command))) {
        error(id, entry->name, "unavailable"); return;
    }
    if (entry->command == Command::HELP) {
        if (described) {
            std::snprintf(output_, sizeof(output_),
                "{\"type\":\"reply\",\"profile\":\"ess_rs\",\"id\":%lu,\"command\":\"help\",\"ok\":true,\"syntax\":\"%s\",\"effect\":\"%s\",\"bus_traffic\":%s}",
                static_cast<unsigned long>(id), described->command == Command::HEALTH && !host_.startTypedRead ? "health" : described->syntax, described->effect, boolean(described->bus));
        } else {
            const int prefix = std::snprintf(output_, sizeof(output_),
                "{\"type\":\"reply\",\"profile\":\"ess_rs\",\"id\":%lu,\"command\":\"help\",\"ok\":true,\"commands\":[", static_cast<unsigned long>(id));
            if (prefix < 0 || static_cast<std::size_t>(prefix) >= sizeof(output_)) { error(id, "help", "output_full"); return; }
            std::size_t used = static_cast<std::size_t>(prefix);
            bool firstName = true;
            for (const Entry& item : COMMANDS) {
                if (!callable(item.command)) continue;
                const int written = std::snprintf(output_ + used, sizeof(output_) - used,
                    "%s\"%s\"", firstName ? "" : ",", item.name);
                if (written < 0 || static_cast<std::size_t>(written) >= sizeof(output_) - used) { error(id, "help", "output_full"); return; }
                used += static_cast<std::size_t>(written);
                firstName = false;
            }
            if (sizeof(output_) - used < 3) { error(id, "help", "output_full"); return; }
            std::snprintf(output_ + used, sizeof(output_) - used, "]}");
        }
        emit(); return;
    }
    if (entry->command == Command::VERSION) {
        std::snprintf(output_, sizeof(output_),
            "{\"type\":\"reply\",\"profile\":\"ess_rs\",\"id\":%lu,\"command\":\"%s\",\"ok\":true,\"product\":\"MotorControl-RS\",\"protocol\":2,\"version\":\"%s\",\"outstanding_capacity\":%u}",
            static_cast<unsigned long>(id), entry->name, MotorControlRS::VERSION, static_cast<unsigned>(OUTSTANDING_CAPACITY));
        emit(); return;
    }
    if (entry->command == Command::CAPS) {
        const auto caps = Ess::readCapabilities();
        Snapshot snapshot; host_.snapshot(host_.context, snapshot);
        std::snprintf(output_, sizeof(output_),
            "{\"type\":\"reply\",\"profile\":\"ess_rs\",\"id\":%lu,\"command\":\"caps\",\"ok\":true,\"probe\":%s,\"identity\":%s,\"config\":%s,\"state\":%s,\"max_steps\":%u,\"max_reply_bytes\":%u,\"writes\":true,\"motion\":%s,\"actions_qualified\":%s,\"axis_reserved\":%s,\"actions\":[\"enable\",\"release\",\"clear_alarm\",\"clear_position\",\"stop_normal\",\"stop_direct\"],\"device_queue_guarantee\":false,\"velocity\":%s,\"configured_ramp_policy\":true,\"acceleration_mapping\":false,\"velocity_unsupported\":[\"jerk\",\"blending\",\"live_updates\",\"torque\",\"current\",\"external_jog\"]}",
            static_cast<unsigned long>(id), boolean(caps.probe), boolean(caps.identity), boolean(caps.config), boolean(caps.state), caps.maxSteps, caps.maxReplyBytes,
            boolean(host_.startMove && host_.snapshot && host_.axis), boolean(snapshot.actionsQualified), boolean(snapshot.axisReserved), boolean(host_.startVelocity && host_.snapshot && host_.axis));
        emit(); return;
    }
    if (entry->command == Command::RESET || (entry->command == Command::STATS && arg)) {
        host_.resetStats(host_.context); action(id, entry->name, Action::OK); return;
    }
    if (entry->command == Command::RECOVER) {
        std::size_t occupied = 0; for (std::size_t i = 0; i < OUTSTANDING_CAPACITY - 1; ++i) occupied += outstanding_[i].commandId != 0;
        if (occupied == OUTSTANDING_CAPACITY - 1) { action(id, entry->name, Action::BUSY); return; }
        const Action result = host_.recover(host_.context, id, operationId);
        if (result == Action::OK) track(id, operationId);
        action(id, entry->name, result, 0, result == Action::OK ? operationId : 0); return;
    }
    if (entry->command == Command::CANCEL || entry->command == Command::RELEASE) {
        const Action result = entry->command == Command::CANCEL ?
            host_.cancel(host_.context, operationId) : host_.release(host_.context, operationId);
        action(id, entry->name, result, 0, operationId); return;
    }
    if (entry->command == Command::RESULT) {
        ResultView view;
        if (!host_.result(host_.context, operationId, view)) { error(id, entry->name, "unavailable"); return; }
        if (view.pending) {
            std::snprintf(output_, sizeof(output_),
                "{\"type\":\"reply\",\"profile\":\"ess_rs\",\"id\":%lu,\"command\":\"result\",\"command_id\":%lu,\"operation_id\":%lu,\"ok\":true,\"result\":\"pending\",\"recovery\":%s,\"capture_read\":%s,\"read_kind\":%s,\"action_kind\":%s,\"stop_policy\":%s,\"move_kind\":%s,\"velocity\":%s}",
                static_cast<unsigned long>(id), static_cast<unsigned long>(view.commandId),
                static_cast<unsigned long>(view.operationId), boolean(view.recovery), boolean(view.captureRead),
                !view.typedRead ? "null" : view.typedRead->kind == Ess::ReadKind::IDENTITY ? "\"identity\"" : view.typedRead->kind == Ess::ReadKind::CONFIG ? "\"config\"" : "\"state\"",
                !view.actionContext ? "null" : view.actionContext->request.kind == Core::ActionKind::ENABLE ? "\"enable\"" : view.actionContext->request.kind == Core::ActionKind::RELEASE ? "\"release\"" : view.actionContext->request.kind == Core::ActionKind::CLEAR_ALARM ? "\"clear_alarm\"" : view.actionContext->request.kind == Core::ActionKind::CLEAR_POSITION ? "\"clear_position\"" : "\"stop\"",
                view.actionContext ? stopPolicy(view.actionContext->request) : "null", view.moveContext ? (view.moveContext->request.position.wrapped ? "\"angle\"" : view.moveContext->request.position.relative ? "\"relative\"" : "\"absolute\"") : "null", boolean(view.velocityContext != nullptr));
            emit();
        } else if (view.velocityContext) formatVelocity(id, view.commandId, view.operationId, *view.velocityContext, true, view.interruptedByStop);
        else if (view.moveContext) formatMove(id, view.commandId, view.operationId, *view.moveContext, true, view.interruptedByStop);
        else if (view.actionContext) formatAction(id, view.commandId, view.operationId, *view.actionContext, true, view.interruptedByStop);
        else if (view.typedRead) formatRead(id, view.commandId, view.operationId, *view.typedRead, true);
        else if (view.recovery) formatRecovery(id, view.commandId, view.operationId, view.recoveryResult, true);
        else formatProbe(id, view.commandId, view.address, view.operationId, view.probe, true);
        return;
    }
    if (entry->command == Command::MONITOR) {
        MonitorSnapshot state;
        const Action result = host_.monitor(host_.context, args ? &monitorSettings : nullptr, state);
        if (result != Action::OK) { action(id, "monitor", result); return; }
        std::snprintf(output_, sizeof(output_),
            "{\"type\":\"reply\",\"profile\":\"ess_rs\",\"id\":%lu,\"command\":\"monitor\",\"ok\":true,\"enabled\":%s,\"interval_ms\":%lu,\"count\":%lu,\"remaining\":%lu,\"operation_id\":%lu,\"next_due_us\":%llu,\"admitted\":%llu,\"rejected\":%llu,\"cancelled\":%llu}",
            static_cast<unsigned long>(id), boolean(state.settings.enabled), static_cast<unsigned long>(state.settings.intervalMs),
            static_cast<unsigned long>(state.settings.count), static_cast<unsigned long>(state.remaining), static_cast<unsigned long>(state.operationId),
            static_cast<unsigned long long>(state.nextDueUs), static_cast<unsigned long long>(state.admitted),
            static_cast<unsigned long long>(state.rejected), static_cast<unsigned long long>(state.cancelled));
        emit(); return;
    }
    if (loadCommand) {
        LoadSnapshot data;
        const Action result = host_.load(host_.context, args ? &loadSettings : nullptr, data);
        if (result != Action::OK) { action(id, entry->name, result); return; }
        const bool cpuValid = data.cpuValid && data.cpu0BusyPct <= 100 && data.cpu1BusyPct <= 100;
        char cpu0[5] = "null", cpu1[5] = "null";
        if (cpuValid) {
            std::snprintf(cpu0, sizeof(cpu0), "%u", static_cast<unsigned>(data.cpu0BusyPct));
            std::snprintf(cpu1, sizeof(cpu1), "%u", static_cast<unsigned>(data.cpu1BusyPct));
        }
        const int written = std::snprintf(output_, sizeof(output_),
            "{\"type\":\"reply\",\"profile\":\"ess_rs\",\"id\":%lu,\"command\":\"load\",\"ok\":true,\"result\":\"done\",\"ready\":%s,\"capture_mode\":\"%s\",\"workload_us\":%lu,\"owner_delay_us\":%lu,\"console_bytes\":%lu,\"elapsed_us\":%llu,\"work_us\":%llu,\"work_iterations\":%llu,\"console_lines\":%llu,\"console_dropped\":%llu,\"capture_us\":%llu,\"capture_samples\":%llu,\"timer_callbacks\":%llu,\"sample_gap_limit_us\":%lu,\"sample_gap_exceeded\":%s,\"capture_high_water\":%lu,\"owner_gap_max_us\":%llu,\"capture_gap_max_us\":%llu,\"work_stack_free_bytes\":%lu,\"cpu_valid\":%s,\"cpu0_busy_pct\":%s,\"cpu1_busy_pct\":%s}",
            static_cast<unsigned long>(id), boolean(data.ready), data.timer ? "timer" : "poll",
            static_cast<unsigned long>(data.settings.workUs), static_cast<unsigned long>(data.settings.ownerDelayUs),
            static_cast<unsigned long>(data.settings.consoleBytes), static_cast<unsigned long long>(data.elapsedUs),
            static_cast<unsigned long long>(data.workUs), static_cast<unsigned long long>(data.workIterations),
            static_cast<unsigned long long>(data.consoleLines), static_cast<unsigned long long>(data.consoleDropped),
            static_cast<unsigned long long>(data.captureUs), static_cast<unsigned long long>(data.captureSamples),
            static_cast<unsigned long long>(data.timerCallbacks), static_cast<unsigned long>(data.sampleGapLimitUs),
            boolean(data.sampleGapExceeded), static_cast<unsigned long>(data.captureHighWater),
            static_cast<unsigned long long>(data.ownerGapMaxUs), static_cast<unsigned long long>(data.captureGapMaxUs),
            static_cast<unsigned long>(data.workStackFreeBytes), boolean(cpuValid), cpu0, cpu1);
        if (written < 0 || static_cast<std::size_t>(written) >= sizeof(output_)) {
            error(id, entry->name, "output_full"); return;
        }
        emit(); return;
    }

    Snapshot data;
    host_.snapshot(host_.context, data);
    if (readCommand) {
        if (!arg) address = data.address;
        if (address < 1 || address > 247) { error(id, entry->name, "invalid_address"); return; }
        std::size_t occupied = 0; for (std::size_t i = 0; i < OUTSTANDING_CAPACITY - 1; ++i) occupied += outstanding_[i].commandId != 0;
        if (occupied == OUTSTANDING_CAPACITY - 1) { action(id, entry->name, Action::BUSY); return; }
        const auto start = entry->command == Command::CAPTURE_READ ? host_.startCaptureRead : host_.startProbe;
        const Action result = typedCommand ? host_.startTypedRead(host_.context, id, static_cast<uint8_t>(address),
            entry->command == Command::READ_IDENTITY ? Ess::ReadKind::IDENTITY : entry->command == Command::READ_CONFIG ? Ess::ReadKind::CONFIG : Ess::ReadKind::STATE, operationId) :
            start(host_.context, id, static_cast<uint8_t>(address), operationId);
        if (result == Action::OK) track(id, operationId);
        action(id, entry->name, result, static_cast<uint8_t>(address), result == Action::OK ? operationId : 0);
        return;
    }
    char rawModel[8] = "null", age[24] = "null", probeAddress[5] = "null", modelAddress[5] = "null";
    if (data.modelKnown && data.observedEarliestUs)
        std::snprintf(age, sizeof(age), "%llu", static_cast<unsigned long long>(data.ageMs));
    if (data.probeKnown) std::snprintf(probeAddress, sizeof(probeAddress), "%u", data.probeAddress);
    if (data.modelKnown) {
        std::snprintf(rawModel, sizeof(rawModel), "%u", data.rawModel);
        std::snprintf(modelAddress, sizeof(modelAddress), "%u", data.modelAddress);
    }
    switch (entry->command) {
    case Command::DRV: {
        const int written = std::snprintf(output_, sizeof(output_),
            "{\"type\":\"reply\",\"profile\":\"ess_rs\",\"id\":%lu,\"command\":\"drv\",\"ok\":true,\"phase\":\"%s\",\"busy\":%s,\"transmit_enabled\":%s,\"recovery_required\":%s,\"pending\":%u,\"retained\":%u,\"reserved\":%u,\"pending_capacity\":%u,\"result_capacity\":%u,\"outstanding_capacity\":%u,\"operation_id\":%lu,\"output_queued\":%u,\"output_blocked\":%llu,\"output_short_writes\":%llu,\"input_bytes\":%llu,\"input_lines\":%llu,\"input_dropped\":%llu,\"recovery_guard_until_us\":%llu,\"request_deadline_us\":%llu,\"capture_mode\":\"%s\",\"read_budget\":%lu,\"model_address\":%s,\"model_operation_id\":%lu,\"observed_earliest_us\":%llu,\"observed_latest_us\":%llu,\"delivered_us\":%llu}",
            static_cast<unsigned long>(id), Rtu::phaseName(data.phase), boolean(data.busy),
            boolean(data.transmitEnabled), boolean(data.recoveryRequired),
            static_cast<unsigned>(data.pending), static_cast<unsigned>(data.retained), static_cast<unsigned>(data.reserved),
            static_cast<unsigned>(data.pendingCapacity), static_cast<unsigned>(data.resultCapacity),
            static_cast<unsigned>(data.outstandingCapacity), static_cast<unsigned long>(data.operationId),
            static_cast<unsigned>(data.outputQueued), static_cast<unsigned long long>(data.outputBlocked),
            static_cast<unsigned long long>(data.outputShortWrites), static_cast<unsigned long long>(data.inputBytes),
            static_cast<unsigned long long>(data.inputLines), static_cast<unsigned long long>(data.inputDropped),
            static_cast<unsigned long long>(data.recoveryGuardUntilUs),
            static_cast<unsigned long long>(data.deadlineUs), data.timerCapture ? "timer" : "poll",
            static_cast<unsigned long>(data.readBudget), modelAddress, static_cast<unsigned long>(data.modelOperationId),
            static_cast<unsigned long long>(data.observedEarliestUs), static_cast<unsigned long long>(data.observedLatestUs),
            static_cast<unsigned long long>(data.deliveredUs));
        if (written < 0 || static_cast<std::size_t>(written) >= sizeof(output_)) { error(id, entry->name, "output_full"); return; }
        break;
    }
    case Command::CONFIG:
        std::snprintf(output_, sizeof(output_),
            "{\"type\":\"reply\",\"profile\":\"ess_rs\",\"id\":%lu,\"command\":\"%s\",\"ok\":true,\"address\":%u,\"baud\":%lu,\"format\":\"8N1\",\"response_timeout_us\":%lu,\"reply_gap_us\":%lu,\"gap15_us\":%lu,\"gap35_us\":%lu,\"stale_after_ms\":%lu,\"ready\":%s,\"timing_qualified\":%s,\"device_settings\":\"%s\",\"cache_off_supported\":%s,\"sample_gap_limit_us\":%lu,\"cached_identity_id\":%lu,\"cached_identity_address\":%u,\"cached_identity_generation\":%lu,\"cached_config_id\":%lu,\"cached_config_address\":%u,\"cached_config_generation\":%lu,\"binding_generation\":%lu}",
            static_cast<unsigned long>(id), entry->name, data.address, static_cast<unsigned long>(data.baud),
            static_cast<unsigned long>(data.responseTimeoutUs), static_cast<unsigned long>(data.replyGapUs),
            static_cast<unsigned long>(data.gap15Us), static_cast<unsigned long>(data.gap35Us),
            static_cast<unsigned long>(data.staleAfterMs), boolean(data.ready), boolean(data.timingQualified),
            data.cachedConfigId ? "cached" : "unknown", boolean(data.cacheOffSupported), static_cast<unsigned long>(data.sampleGapLimitUs),
            static_cast<unsigned long>(data.cachedIdentityId), data.cachedIdentityAddress, static_cast<unsigned long>(data.cachedIdentityGeneration),
            static_cast<unsigned long>(data.cachedConfigId), data.cachedConfigAddress, static_cast<unsigned long>(data.cachedConfigGeneration),
            static_cast<unsigned long>(data.bindingGeneration));
        break;
    case Command::STATUS:
        std::snprintf(output_, sizeof(output_),
            "{\"type\":\"reply\",\"profile\":\"ess_rs\",\"id\":%lu,\"command\":\"status\",\"ok\":true,\"uptime_ms\":%llu,\"ready\":%s,\"timing_qualified\":%s,\"busy\":%s,\"transmit_enabled\":%s,\"recovery_required\":%s,\"phase\":\"%s\",\"transport\":\"%s\",\"codec\":\"%s\",\"detail\":%ld,\"frame_error\":%u,\"last_probe_known\":%s,\"last_probe_ok\":%s,\"probe_address\":%s,\"model_address\":%s,\"raw_model\":%s,\"age_ms\":%s,\"stale_after_ms\":%lu}",
            static_cast<unsigned long>(id), static_cast<unsigned long long>(data.uptimeMs), boolean(data.ready), boolean(data.timingQualified),
            boolean(data.busy), boolean(data.transmitEnabled), boolean(data.recoveryRequired), Rtu::phaseName(data.phase), Rtu::reasonName(data.transport),
            data.codecChecked ? MotorControlRS::errToString(data.codec.code) : "NOT_CHECKED",
            data.codecChecked ? static_cast<long>(data.codec.detail) : 0L,
            data.codecChecked ? static_cast<unsigned>(data.frameError) : 0U,
            boolean(data.probeKnown), boolean(data.probeKnown && data.probeOk), probeAddress, modelAddress, rawModel, age, static_cast<unsigned long>(data.staleAfterMs));
        break;
    case Command::HEALTH: {
        const char* communication = !data.ready ? "unavailable" : data.recoveryRequired ? "failed" :
            !data.probeKnown ? "unknown" : !data.probeOk ? "failed" : !data.observedEarliestUs ? "unknown" :
            data.ageMs > data.staleAfterMs ? "stale" : "current";
        MotorControlRS::ReadTarget target; target.id = data.address; target.address = data.address; target.generation = data.bindingGeneration;
        if (data.communicationKnown && data.ready && !data.recoveryRequired) {
            communication = !sameTarget(data.communicationTarget, target) || data.nowUs < data.communicationEarliestUs ? "unknown" :
                data.nowUs - data.communicationEarliestUs > static_cast<uint64_t>(data.staleAfterMs) * 1000 ? "stale" : "current";
        }
        const StateCache::Block* motion = data.stateCache ? &data.stateCache->blocks[0] : nullptr;
        const bool motionFresh = motion && fresh(*motion, target, data.nowUs, static_cast<uint64_t>(data.staleAfterMs) * 1000);
        const char* alarms = !motionFresh ? "unknown" : motion->value.alarmFlag ||
            (motion->value.rawAlarm >= 1 && motion->value.rawAlarm <= 5) ? "present" :
            motion->value.rawAlarm == 0 ? "clear" : "unknown";
        std::snprintf(output_, sizeof(output_),
            "{\"type\":\"reply\",\"profile\":\"ess_rs\",\"id\":%lu,\"command\":\"health\",\"ok\":true,\"communication\":\"%s\",\"probe_address\":%s,\"model_address\":%s,\"age_ms\":%s,\"stale_after_ms\":%lu,\"readiness\":\"unknown\",\"alarms\":\"%s\",\"state\":\"%s\",\"identity\":\"%s\"}",
            static_cast<unsigned long>(id), communication, probeAddress, modelAddress, age, static_cast<unsigned long>(data.staleAfterMs), alarms, motionFresh ? "observed" : "unknown", data.probeKnown && data.probeOk ? "responder_only" : "unknown");
        break;
    }
    case Command::STATS:
        std::snprintf(output_, sizeof(output_),
            "{\"type\":\"reply\",\"profile\":\"ess_rs\",\"id\":%lu,\"command\":\"stats\",\"ok\":true,\"started\":%lu,\"frames\":%lu,\"failed\":%lu,\"timeouts\":%lu,\"cancelled\":%lu,\"rx_bytes\":%lu,\"discarded\":%lu,\"echo_bytes\":%lu,\"trace_overwritten\":%lu,\"max_poll_gap_us\":%llu,\"capture_faults\":%lu,\"rx_errors\":%lu,\"sample_gap_exceeded\":%s}",
            static_cast<unsigned long>(id), static_cast<unsigned long>(data.stats.started), static_cast<unsigned long>(data.stats.frames),
            static_cast<unsigned long>(data.stats.failed), static_cast<unsigned long>(data.stats.timeouts), static_cast<unsigned long>(data.stats.cancelled),
            static_cast<unsigned long>(data.stats.rxBytes), static_cast<unsigned long>(data.stats.discarded), static_cast<unsigned long>(data.stats.echoBytes), static_cast<unsigned long>(data.stats.traceOverwritten),
            static_cast<unsigned long long>(data.maxPollGapUs), static_cast<unsigned long>(data.captureFaults), static_cast<unsigned long>(data.rxErrors), boolean(data.sampleGapExceeded));
        break;
    case Command::MEMORY:
        std::snprintf(output_, sizeof(output_),
            "{\"type\":\"reply\",\"profile\":\"ess_rs\",\"id\":%lu,\"command\":\"memory\",\"ok\":true,\"valid\":%s,\"internal_free\":%lu,\"internal_min\":%lu,\"internal_largest\":%lu,\"psram_free\":%lu,\"psram_min\":%lu,\"psram_largest\":%lu,\"stack_free_bytes\":%lu}",
            static_cast<unsigned long>(id), boolean(data.memoryValid), static_cast<unsigned long>(data.internalFree), static_cast<unsigned long>(data.internalMin),
            static_cast<unsigned long>(data.internalLargest), static_cast<unsigned long>(data.psramFree), static_cast<unsigned long>(data.psramMin),
            static_cast<unsigned long>(data.psramLargest), static_cast<unsigned long>(data.stackFreeBytes));
        break;
    default: error(id, entry->name, "unavailable"); return;
    }
    if (entry->command == Command::STATUS || entry->command == Command::HEALTH) {
        std::size_t used = std::strlen(output_);
        if (!used || output_[used - 1] != '}') { error(id, entry->name, "output_full"); return; }
        --used;
        if (!stateCache(output_, sizeof(output_), used, data) || !append(output_, sizeof(output_), used, "}")) {
            error(id, entry->name, "output_full"); return;
        }
    }
    emit();
}

bool Console::reportProbe(uint32_t id, uint8_t address, uint32_t operationId, const ProbeResult& result) noexcept {
    if (outputPending()) return false;
    for (auto& item : outstanding_) if (item.commandId == id && item.operationId == operationId && !item.transferred) {
        item.transferred = true;
        return formatProbe(id, id, address, operationId, result, false);
    }
    return false;
}

bool Console::formatProbe(uint32_t id, uint32_t commandId, uint8_t address, uint32_t operationId,
                          const ProbeResult& result, bool inspection) noexcept {
    const bool ok = result.transport.reason == Rtu::Reason::FRAME && result.codecChecked && result.codec.isOk();
    const uint64_t duration = result.transport.endedUs >= result.transport.startedUs ? result.transport.endedUs - result.transport.startedUs : 0;
    char model[8] = "null";
    char txHex[PROBE_TX_CAPACITY * 2 + 1], rxHex[PROBE_RX_CAPACITY * 2 + 1];
    hex(result.tx, result.txLength, txHex, sizeof(txHex));
    hex(result.rx, result.rxLength, rxHex, sizeof(rxHex));
    const bool truncated = result.transport.rxTruncated || result.txLength > PROBE_TX_CAPACITY || result.rxLength > PROBE_RX_CAPACITY ||
        (!result.tx && result.txLength) || (!result.rx && result.rxLength);
    if (ok && !result.captureRead) std::snprintf(model, sizeof(model), "%u", result.rawModel);
    const char* command = result.captureRead ? "capture-read" : "probe";
    const int written = std::snprintf(output_, sizeof(output_),
        "{\"type\":\"%s\",\"profile\":\"ess_rs\",\"id\":%lu,\"command\":\"%s\",\"command_id\":%lu,\"operation_id\":%lu,\"ok\":%s,\"address\":%u,\"outcome\":\"%s\",\"execution_unknown\":%s,\"cancellation\":\"%s\",\"transport\":\"%s\",\"codec\":\"%s\",\"detail\":%ld,\"frame_error\":%u,\"raw_model\":%s,\"capture_read\":%s,\"register_start\":%u,\"register_count\":%u,\"duration_us\":%llu,\"tx_bytes\":%u,\"rx_bytes\":%u,\"identity\":\"%s\",\"tx_hex\":\"%s\",\"rx_hex\":\"%s\",\"raw_truncated\":%s,\"timing_valid\":%s,\"tx_uncertainty_us\":%lu,\"max_rx_uncertainty_us\":%lu,\"tx_end_us\":%llu,\"first_rx_start_us\":%llu,\"observed_earliest_us\":%llu,\"observed_latest_us\":%llu,\"delivered_us\":%llu}",
        inspection ? "reply" : result.captureRead ? "capture_read" : "probe", static_cast<unsigned long>(id), inspection ? "result" : command,
        static_cast<unsigned long>(commandId), static_cast<unsigned long>(operationId), boolean(ok), address,
        outcomeName(result.outcome), boolean(result.executionUnknown), cancellationName(result.cancellation),
        Rtu::reasonName(result.transport.reason), result.codecChecked ? MotorControlRS::errToString(result.codec.code) : "NOT_CHECKED",
        result.codecChecked ? static_cast<long>(result.codec.detail) : 0L,
        result.codecChecked ? static_cast<unsigned>(result.frameError) : 0U, model, boolean(result.captureRead),
        result.captureRead ? CAPTURE_FIRST : 0U, result.captureRead ? CAPTURE_WORDS : 1U,
        static_cast<unsigned long long>(duration),
        result.transport.txAccepted, result.transport.rxLength, result.captureRead ? "not_requested" : ok ? "responder_only" : "unknown", txHex, rxHex, boolean(truncated), boolean(result.timingValid),
        static_cast<unsigned long>(result.txUncertaintyUs), static_cast<unsigned long>(result.maxRxUncertaintyUs),
        static_cast<unsigned long long>(result.txEndUs), static_cast<unsigned long long>(result.firstRxStartUs),
        static_cast<unsigned long long>(result.observedEarliestUs), static_cast<unsigned long long>(result.observedLatestUs),
        static_cast<unsigned long long>(result.deliveredUs));
    if (written < 0 || static_cast<std::size_t>(written) >= sizeof(output_)) {
        error(id, inspection ? "result" : command, "output_full");
        if (!inspection) { if (outputPending_) pendingTerminalOperation_ = operationId; else untrack(operationId); }
        return true;
    }
    emit(inspection ? 0 : operationId);
    return true;
}

bool Console::reportRecovery(uint32_t id, uint32_t operationId, const Rtu::RecoveryResult& result) noexcept {
    if (outputPending()) return false;
    for (auto& item : outstanding_) if (item.commandId == id && item.operationId == operationId && !item.transferred) {
        item.transferred = true;
        return formatRecovery(id, id, operationId, result, false);
    }
    return false;
}

bool Console::reportAction(uint32_t id, uint32_t operationId, const Ess::ActionContext& context, bool interruptedByStop) noexcept {
    if (outputPending() || context.operationId != operationId ||
        (context.state != Core::ActionState::SUCCEEDED && context.state != Core::ActionState::FAILED)) return false;
    for (auto& item : outstanding_) if (item.commandId == id && item.operationId == operationId && !item.transferred) {
        if (!formatAction(id, id, operationId, context, false, interruptedByStop)) return false;
        // A successful immediate emit may already have cleared this slot.
        if (item.operationId == operationId) item.transferred = true;
        return true;
    }
    return false;
}

bool Console::formatAction(uint32_t id, uint32_t commandId, uint32_t operationId,
                           const Ess::ActionContext& context, bool inspection, bool interruptedByStop) noexcept {
    std::size_t used = 0;
    const bool ok = context.state == Core::ActionState::SUCCEEDED && context.completion == Core::ActionCompletion::OBSERVED;
    char alarm[8] = "null", motion[8] = "null";
    if (context.observationKnown && context.request.kind != Core::ActionKind::CLEAR_POSITION) {
        std::snprintf(alarm, sizeof(alarm), "%u", context.rawAlarm);
        std::snprintf(motion, sizeof(motion), "%u", context.rawMotion);
    }
    bool fits = append(output_, sizeof(output_), used,
        "{\"type\":\"%s\",\"profile\":\"ess_rs\",\"id\":%lu,\"command\":\"%s\",\"command_id\":%lu,\"operation_id\":%lu,\"action_kind\":\"%s\",\"stop_policy\":%s,\"read_kind\":null,\"capture_read\":false,\"recovery\":false,\"ok\":%s,\"state\":\"%s\",\"outcome\":\"%s\",\"execution\":\"%s\",\"completion\":\"%s\",\"target\":%lu,\"address\":%u,\"generation\":%lu,\"started_us\":%llu,\"deadline_us\":%llu,\"serviced_us\":%llu,\"polls\":%u,\"observation_known\":%s,\"raw_alarm\":%s,\"raw_motion\":%s,\"write_evidence\":",
        inspection ? "reply" : "action", static_cast<unsigned long>(id), inspection ? "result" : motorCommand(context.request.kind),
        static_cast<unsigned long>(commandId), static_cast<unsigned long>(operationId), motorKind(context.request.kind), stopPolicy(context.request), boolean(ok),
        context.state == Core::ActionState::SUCCEEDED ? "succeeded" : "failed", motorOutcome(context.outcome), executionName(context.execution),
        context.completion == Core::ActionCompletion::OBSERVED ? "observed" : "not_observed",
        static_cast<unsigned long>(context.target.id), context.target.address, static_cast<unsigned long>(context.target.generation),
        static_cast<unsigned long long>(context.startedUs), static_cast<unsigned long long>(context.deadlineUs), static_cast<unsigned long long>(context.servicedUs),
        context.polls, boolean(context.observationKnown), alarm, motion) &&
        actionEvidence(output_, sizeof(output_), used, context.writeEvidence) && append(output_, sizeof(output_), used, ",\"last_observation\":") &&
        actionEvidence(output_, sizeof(output_), used, context.lastObservation) && append(output_, sizeof(output_), used, ",\"failure_evidence\":") &&
        actionEvidence(output_, sizeof(output_), used, context.failureEvidence) &&
        append(output_, sizeof(output_), used, ",\"interrupted_by_stop\":%s", boolean(interruptedByStop));
    if (context.request.kind == Core::ActionKind::CLEAR_POSITION) {
        char position[16] = "null";
        if (context.observationKnown) std::snprintf(position, sizeof(position), "%lu", static_cast<unsigned long>(context.rawPosition));
        fits = fits && append(output_, sizeof(output_), used, ",\"device_position\":%lld,\"position_clear_qualified\":%s,\"raw_position\":%s",
            static_cast<long long>(context.request.devicePosition), boolean(context.request.positionClearQualified), position);
    }
    fits = fits && append(output_, sizeof(output_), used, "}");
    if (!fits) return false; // Keep the complete host result available; never publish partial JSON.
    emit(inspection ? 0 : operationId);
    return true;
}

bool Console::reportRead(uint32_t id, uint32_t operationId, const Ess::ReadContext& context) noexcept {
    if (outputPending() || context.operationId != operationId ||
        (context.state != MotorControlRS::ReadState::SUCCEEDED && context.state != MotorControlRS::ReadState::FAILED)) return false;
    for (auto& item : outstanding_) if (item.commandId == id && item.operationId == operationId && !item.transferred) {
        item.transferred = true;
        return formatRead(id, id, operationId, context, false);
    }
    return false;
}

bool Console::reportMove(uint32_t id, uint32_t operationId, const Ess::MoveContext& context,
                         bool interruptedByStop) noexcept {
    if (outputPending() || context.operationId != operationId ||
        (context.state != Core::ActionState::SUCCEEDED && context.state != Core::ActionState::FAILED)) return false;
    for (auto& item : outstanding_) if (item.commandId == id && item.operationId == operationId && !item.transferred) {
        if (!formatMove(id, id, operationId, context, false, interruptedByStop)) return false;
        if (item.operationId == operationId) item.transferred = true;
        return true;
    }
    return false;
}

bool Console::formatMove(uint32_t id, uint32_t commandId, uint32_t operationId,
                         const Ess::MoveContext& c, bool inspection, bool interruptedByStop) noexcept {
    std::size_t used = 0;
    const bool ok = c.state == Core::ActionState::SUCCEEDED && c.completion == Core::ActionCompletion::OBSERVED;
    const auto& p = c.prepared;
    const auto& r = c.request.position;
    char alarm[8] = "null", motion[8] = "null";
    if (c.observationKnown) {
        std::snprintf(alarm, sizeof(alarm), "%u", c.rawAlarm);
        std::snprintf(motion, sizeof(motion), "%u", c.rawMotion);
    }
    char radians[32] = "null", approximateNative[32] = "null", approximationLimit[32] = "null";
    if (r.unit == Core::PositionUnit::RADIANS && !r.rationalRadians)
        std::snprintf(radians, sizeof(radians), "%.17g", r.radians);
    if (r.unit == Core::PositionUnit::RADIANS && r.approximate)
        std::snprintf(approximationLimit, sizeof(approximationLimit), "%.17g", r.maximumApproximationError);
    if (!p.exactArithmetic) {
        std::snprintf(approximateNative, sizeof(approximateNative), "%.17g", p.approximateRequestedNative);
    }
    bool fits = append(output_, sizeof(output_), used,
        "{\"type\":\"%s\",\"profile\":\"ess_rs\",\"id\":%lu,\"command\":\"%s\",\"command_id\":%lu,\"operation_id\":%lu,\"move_kind\":\"%s\",\"read_kind\":null,\"action_kind\":null,\"capture_read\":false,\"recovery\":false,\"ok\":%s,\"state\":\"%s\",\"outcome\":\"%s\",\"status\":\"%s\",\"detail\":%ld,\"setup_execution\":\"%s\",\"execution\":\"%s\",\"completion\":\"%s\",\"target\":%lu,\"address\":%u,\"generation\":%lu,\"configuration_generation\":%lu,\"started_us\":%llu,\"deadline_us\":%llu,\"serviced_us\":%llu,\"polls\":%u,\"staging_applied\":%s,\"uncertain\":%s,\"running_observed\":%s,\"observation_known\":%s,\"raw_alarm\":%s,\"raw_motion\":%s,\"native_rpm\":%u,\"ramp\":\"configured\",\"staging_words\":[%u,%u,%u,%u,%u],\"requested\":{\"numerator\":%lld,\"denominator\":%llu,\"unit\":\"%s\",\"frame\":%u,\"relative\":%s,\"wrapped\":%s,\"angle_path\":%u,\"half_turn_tie\":%u,\"basis\":%u,\"rounding\":%u,\"approximate\":%s,\"rational_radians\":%s,\"radians\":%s,\"maximum_quantization_error\":%.17g,\"maximum_approximation_error\":%s},\"effective_native\":%lld,\"endpoint_known\":%s,\"endpoint_native\":%lld,\"displacement_native\":%lld,\"zero_displacement\":%s,\"rounding_error\":%.17g,\"approximation_error_bound\":%.17g,\"exact_arithmetic\":%s,\"requested_native_approximate\":%s,\"staging_evidence\":",
        inspection ? "reply" : "move", static_cast<unsigned long>(id), inspection ? "result" : moveCommand(r),
        static_cast<unsigned long>(commandId), static_cast<unsigned long>(operationId), moveKind(r), boolean(ok),
        c.state == Core::ActionState::SUCCEEDED ? "succeeded" : "failed", motorOutcome(c.outcome), Core::errToString(c.status.code), static_cast<long>(c.status.detail),
        executionName(c.setupExecution), executionName(c.execution), c.completion == Core::ActionCompletion::OBSERVED ? "observed" : "not_observed",
        static_cast<unsigned long>(c.target.id), c.target.address, static_cast<unsigned long>(c.target.generation), static_cast<unsigned long>(p.configurationGeneration),
        static_cast<unsigned long long>(c.startedUs), static_cast<unsigned long long>(c.deadlineUs), static_cast<unsigned long long>(c.servicedUs), c.polls,
        boolean(c.stagingApplied), boolean(c.uncertain), boolean(c.runningObserved), boolean(c.observationKnown), alarm, motion,
        c.request.speedRpm, c.words[0], c.words[1], c.words[2], c.words[3], c.words[4],
        static_cast<long long>(r.value.numerator), static_cast<unsigned long long>(r.value.denominator), unitName(r.unit),
        static_cast<unsigned>(r.frame), boolean(r.relative), boolean(r.wrapped), static_cast<unsigned>(r.path), static_cast<unsigned>(r.tie), static_cast<unsigned>(r.basis), static_cast<unsigned>(r.rounding), boolean(r.approximate), boolean(r.rationalRadians), radians, r.maximumQuantizationError, approximationLimit,
        static_cast<long long>(p.effectiveNative), boolean(p.endpointKnown), static_cast<long long>(p.endpointNative),
        static_cast<long long>(p.displacementNative), boolean(p.zeroDisplacement), p.roundingError, p.approximationErrorBound, boolean(p.exactArithmetic), approximateNative) &&
        actionEvidence(output_, sizeof(output_), used, c.stagingEvidence) && append(output_, sizeof(output_), used, ",\"trigger_evidence\":") &&
        actionEvidence(output_, sizeof(output_), used, c.triggerEvidence) && append(output_, sizeof(output_), used, ",\"activity_evidence\":") &&
        actionEvidence(output_, sizeof(output_), used, c.activityEvidence) && append(output_, sizeof(output_), used, ",\"last_observation\":") &&
        actionEvidence(output_, sizeof(output_), used, c.lastObservation) && append(output_, sizeof(output_), used, ",\"failure_evidence\":") &&
        actionEvidence(output_, sizeof(output_), used, c.failureEvidence) && append(output_, sizeof(output_), used,
        ",\"prerequisites\":{\"target\":%lu,\"generation\":%lu,\"configuration_generation\":%lu,\"observed_us\":%llu,\"maximum_age_us\":%llu,\"raw_alarm\":%u,\"raw_motion\":%u,\"command_units_verified\":%s,\"relative_basis_verified\":%s,\"negative_encoding_verified\":%s,\"configured_ramp_verified\":%s,\"serial_inputs_permit\":%s,\"readiness_qualified\":%s,\"word_order_known\":%s,\"word_order\":%u,\"start_speed_known\":%s,\"start_speed\":%u},\"interrupted_by_stop\":%s",
        static_cast<unsigned long>(c.prerequisites.target.id), static_cast<unsigned long>(c.prerequisites.target.generation),
        static_cast<unsigned long>(c.prerequisites.configurationGeneration), static_cast<unsigned long long>(c.prerequisites.observedUs),
        static_cast<unsigned long long>(c.prerequisites.maximumAgeUs), c.prerequisites.rawAlarm, c.prerequisites.rawMotion,
        boolean(c.prerequisites.commandUnitsVerified), boolean(c.prerequisites.relativeBasisVerified), boolean(c.prerequisites.negativeTwosComplementVerified),
        boolean(c.prerequisites.configuredRampVerified), boolean(c.prerequisites.serialInputsPermit), boolean(c.prerequisites.readinessQualified),
        boolean(c.prerequisites.wordOrderKnown), static_cast<unsigned>(c.prerequisites.wordOrder), boolean(c.prerequisites.startSpeedKnown),
        c.prerequisites.startSpeed, boolean(interruptedByStop)) && append(output_, sizeof(output_), used,
        ",\"reference\":{\"target\":%lu,\"generation\":%lu,\"configuration_generation\":%lu,\"native_known\":%s,\"native_position\":%lld,\"basis\":%u,\"source\":%u,\"observed_us\":%llu,\"maximum_age_us\":%llu}",
        static_cast<unsigned long>(c.reference.target.id), static_cast<unsigned long>(c.reference.target.generation),
        static_cast<unsigned long>(c.reference.configurationGeneration), boolean(c.reference.nativeKnown), static_cast<long long>(c.reference.nativePosition),
        static_cast<unsigned>(c.reference.basis), static_cast<unsigned>(c.reference.source),
        static_cast<unsigned long long>(c.reference.observedUs), static_cast<unsigned long long>(c.reference.maximumAgeUs)) && append(output_, sizeof(output_), used, "}");
    if (!fits) return false;
    emit(inspection ? 0 : operationId);
    return true;
}

bool Console::reportVelocity(uint32_t id, uint32_t operationId, const Ess::VelocityContext& context,
                             bool interruptedByStop) noexcept {
    if (outputPending() || context.operationId != operationId ||
        (context.state != Core::ActionState::SUCCEEDED && context.state != Core::ActionState::FAILED)) return false;
    for (auto& item : outstanding_) if (item.commandId == id && item.operationId == operationId && !item.transferred) {
        if (!formatVelocity(id, id, operationId, context, false, interruptedByStop)) return false;
        if (item.operationId == operationId) item.transferred = true;
        return true;
    }
    return false;
}

bool Console::formatVelocity(uint32_t id, uint32_t commandId, uint32_t operationId,
                             const Ess::VelocityContext& c, bool inspection, bool interruptedByStop) noexcept {
    std::size_t used = 0;
    const auto& r = c.request; const auto& p = c.prepared;
    const bool ok = c.state == Core::ActionState::SUCCEEDED && c.completion == Core::ActionCompletion::OBSERVED;
    char alarm[8] = "null", motion[8] = "null";
    if (c.observationKnown) { std::snprintf(alarm, sizeof(alarm), "%u", c.rawAlarm); std::snprintf(motion, sizeof(motion), "%u", c.rawMotion); }
    char observedRaw[Ess::ACTION_MAX_REPLY_BYTES * 2 + 1];
    hex(c.lastObservation.raw, c.lastObservation.length, observedRaw, sizeof(observedRaw));
    const bool fits = append(output_, sizeof(output_), used,
        "{\"type\":\"%s\",\"profile\":\"ess_rs\",\"id\":%lu,\"command\":\"%s\",\"command_id\":%lu,\"operation_id\":%lu,\"velocity\":true,\"ok\":%s,\"state\":\"%s\",\"outcome\":\"%s\",\"status\":\"%s\",\"detail\":%ld,\"setup_execution\":\"%s\",\"execution\":\"%s\",\"completion\":\"%s\",\"target\":%lu,\"address\":%u,\"generation\":%lu,\"configuration_generation\":%lu,\"started_us\":%llu,\"deadline_us\":%llu,\"stop_due_us\":%llu,\"serviced_us\":%llu,\"polls\":%u,\"phase\":%u,\"staging_applied\":%s,\"uncertain\":%s,\"needs_stop\":%s,\"running_observed\":%s,\"service_missed\":%s,\"observation_known\":%s,\"raw_alarm\":%s,\"raw_motion\":%s,\"native_rpm\":%d,\"ramp\":\"configured\",\"staging_words\":[%u,%u,%u],\"stop_policy\":\"%s\",\"requested\":{\"numerator\":%lld,\"denominator\":%llu,\"position_unit\":%u,\"time_unit\":%u,\"frame\":%u,\"duration_us\":%lu,\"rounding\":%u,\"approximate\":%s,\"maximum_quantization_error_rpm\":%.17g,\"maximum_approximation_error_rpm\":%.17g},\"rounding_error\":%.17g,\"approximation_error_bound\":%.17g,\"exact_arithmetic\":%s,\"requested_rpm_approximate\":%.17g,\"stop_execution\":\"%s\",\"stop_completion\":\"%s\",\"stop_outcome\":\"%s\",\"interrupted_by_stop\":%s,\"staging_evidence\":",
        inspection ? "reply" : "velocity", static_cast<unsigned long>(id), inspection ? "result" : "velocity",
        static_cast<unsigned long>(commandId), static_cast<unsigned long>(operationId), boolean(ok), ok ? "succeeded" : "failed",
        motorOutcome(c.outcome), Core::errToString(c.status.code), static_cast<long>(c.status.detail), executionName(c.setupExecution), executionName(c.execution),
        c.completion == Core::ActionCompletion::OBSERVED ? "observed" : "not_observed", static_cast<unsigned long>(c.target.id), c.target.address,
        static_cast<unsigned long>(c.target.generation), static_cast<unsigned long>(r.configurationGeneration),
        static_cast<unsigned long long>(c.startedUs), static_cast<unsigned long long>(c.deadlineUs), static_cast<unsigned long long>(c.stopDueUs), static_cast<unsigned long long>(c.servicedUs),
        c.polls, static_cast<unsigned>(c.phase), boolean(c.stagingApplied), boolean(c.uncertain), boolean(c.needsStop), boolean(c.runningObserved), boolean(c.serviceMissed),
        boolean(c.observationKnown), alarm, motion, p.nativeRpm, c.words[0], c.words[1], c.words[2],
        r.stop.behavior == Core::StopBehavior::DIRECT ? "direct" : "normal", static_cast<long long>(r.value.numerator), static_cast<unsigned long long>(r.value.denominator),
        static_cast<unsigned>(r.unit.position), static_cast<unsigned>(r.unit.time), static_cast<unsigned>(r.frame), static_cast<unsigned long>(r.durationUs),
        static_cast<unsigned>(r.rounding), boolean(r.approximate), r.maximumQuantizationErrorRpm, r.maximumApproximationErrorRpm,
        p.roundingError, p.approximationErrorBound, boolean(p.exactArithmetic), p.approximateRequestedRpm, executionName(c.stop.execution),
        c.stop.completion == Core::ActionCompletion::OBSERVED ? "observed" : "not_observed", motorOutcome(c.stop.outcome), boolean(interruptedByStop)) &&
        actionEvidence(output_, sizeof(output_), used, c.stagingEvidence) && append(output_, sizeof(output_), used, ",\"trigger_evidence\":") &&
        actionEvidence(output_, sizeof(output_), used, c.triggerEvidence) && append(output_, sizeof(output_), used, ",\"activity_evidence\":") &&
        actionEvidence(output_, sizeof(output_), used, c.activityEvidence) && append(output_, sizeof(output_), used, ",\"failure_evidence\":") &&
        actionEvidence(output_, sizeof(output_), used, c.failureEvidence) && append(output_, sizeof(output_), used, ",\"stop_write_evidence\":") &&
        actionEvidence(output_, sizeof(output_), used, c.stop.writeEvidence) && append(output_, sizeof(output_), used, ",\"stop_observation\":") &&
        actionEvidence(output_, sizeof(output_), used, c.stop.lastObservation) && append(output_, sizeof(output_), used, ",\"stop_failure_evidence\":") &&
        actionEvidence(output_, sizeof(output_), used, c.stop.failureEvidence) && append(output_, sizeof(output_), used,
        ",\"last_observation\":{\"step\":%u,\"raw_hex\":\"%s\",\"earliest_us\":%llu,\"latest_us\":%llu,\"delivered_us\":%llu}}",
        c.lastObservation.step, observedRaw, static_cast<unsigned long long>(c.lastObservation.earliestUs),
        static_cast<unsigned long long>(c.lastObservation.latestUs), static_cast<unsigned long long>(c.lastObservation.deliveredUs));
    if (!fits) return false;
    emit(inspection ? 0 : operationId); return true;
}

bool Console::formatRead(uint32_t id, uint32_t commandId, uint32_t operationId,
                         const Ess::ReadContext& context, bool inspection) noexcept {
    std::size_t used = 0;
    const bool identity = context.kind == Ess::ReadKind::IDENTITY;
    const bool decoded = context.state == MotorControlRS::ReadState::SUCCEEDED &&
        (identity ? Ess::getIdentity(context, identityView_).isOk() : context.kind == Ess::ReadKind::CONFIG ? Ess::getConfig(context, configView_).isOk() : true);
    bool fits = append(output_, sizeof(output_), used,
        "{\"type\":\"%s\",\"profile\":\"ess_rs\",\"id\":%lu,\"command\":\"%s\",\"command_id\":%lu,\"operation_id\":%lu,\"read_kind\":\"%s\",\"ok\":%s,\"state\":\"%s\",\"outcome\":\"%s\",\"status\":\"%s\",\"detail\":%ld,\"target\":%lu,\"address\":%u,\"generation\":%lu,\"started_us\":%llu,\"deadline_us\":%llu,\"serviced_us\":%llu,\"completed_steps\":%u,\"active_serial\":{\"known\":%s,\"baud\":%lu,\"data_bits\":%u,\"parity\":%u,\"stop_bits\":%u},\"%s\":",
        inspection ? "reply" : "read", static_cast<unsigned long>(id), inspection ? "result" : readCommandName(context.kind),
        static_cast<unsigned long>(commandId), static_cast<unsigned long>(operationId), readName(context.kind), boolean(decoded),
        readState(context.state), readOutcome(context.outcome), MotorControlRS::errToString(context.status.code), static_cast<long>(context.status.detail),
        static_cast<unsigned long>(context.target.id), context.target.address, static_cast<unsigned long>(context.target.generation),
        static_cast<unsigned long long>(context.startedUs), static_cast<unsigned long long>(context.deadlineUs),
        static_cast<unsigned long long>(context.servicedUs), context.completedSteps, boolean(context.activeSerial.known),
        static_cast<unsigned long>(context.activeSerial.baud), context.activeSerial.dataBits, static_cast<unsigned>(context.activeSerial.parity),
        context.activeSerial.stopBits, context.kind == Ess::ReadKind::STATE ? "state_blocks" : readName(context.kind));
    if (context.kind == Ess::ReadKind::STATE) {
        fits = fits && append(output_, sizeof(output_), used, "[");
        bool first = true;
        for (uint8_t block = 0; block < Ess::STATE_BLOCK_COUNT && fits; ++block) {
            if (!Ess::getStateBlock(context, block, stateView_)) continue;
            fits = append(output_, sizeof(output_), used, "%s", first ? "" : ",") && stateValue(output_, sizeof(output_), used, stateView_);
            first = false;
        }
        fits = fits && append(output_, sizeof(output_), used, "]");
    } else if (!decoded) fits = fits && append(output_, sizeof(output_), used, "null");
    else if (identity) {
        const auto& v = identityView_;
        fits = fits && append(output_, sizeof(output_), used,
            "{\"raw_model\":%u,\"raw_version\":%u,\"raw_active_node\":%u,\"raw_dip\":%u,\"active_node_known\":%s,\"active_node\":%u,\"model_resolution\":%u,\"version_resolution\":%u,\"dip_resolution\":%u,\"dip_issues\":%lu,\"model\":\"mapping_unresolved\",\"firmware\":\"mapping_unresolved\",\"dip\":\"mapping_conflict_unresolved\"}",
            v.rawModel, v.rawVersion, v.rawActiveNode, v.rawDip, boolean(v.activeNodeKnown), v.activeNode,
            static_cast<unsigned>(v.modelResolution), static_cast<unsigned>(v.versionResolution), static_cast<unsigned>(v.dipResolution),
            static_cast<unsigned long>(v.dipIssues));
    } else {
        const auto& v = configView_; const auto& raw = v.raw;
        fits = fits && append(output_, sizeof(output_), used,
            "{\"raw\":{\"direction\":%u,\"subdivision\":%u,\"custom_node\":%u,\"baud\":%u,\"format\":%u,\"over_limit_stop\":%u,\"soft_limit_enable\":%u,\"word_order\":%u,\"input_polarity\":%u,\"algorithm\":%u,\"encoder_resolution\":%u},\"known\":{\"direction\":%s,\"baud\":%s,\"format\":%s,\"over_limit_stop\":%s,\"soft_limit_enable\":%s,\"word_order\":%s,\"algorithm\":%s},\"unknown_polarity_bits\":%u,\"subdivision_resolution\":%u,\"encoder_resolution\":%u,\"subdivision_issues\":%lu,\"custom_node_issues\":%lu,\"over_limit_stop_issues\":%lu,\"soft_limit_issues\":%lu,\"encoder_scale_source\":%u,\"inputs\":[",
            raw.direction, raw.subdivision, raw.customNode, raw.baud, raw.format, raw.overLimitStop, raw.softLimitEnable,
            raw.wordOrder, raw.inputPolarity, raw.algorithm, raw.encoderResolution, boolean(v.directionKnown), boolean(v.baudKnown),
            boolean(v.formatKnown), boolean(v.overLimitStopKnown), boolean(v.softLimitEnableKnown), boolean(v.wordOrderKnown),
            boolean(v.algorithmKnown), v.unknownPolarityBits, static_cast<unsigned>(v.subdivisionResolution), static_cast<unsigned>(v.encoderResolution),
            static_cast<unsigned long>(v.subdivisionIssues), static_cast<unsigned long>(v.customNodeIssues),
            static_cast<unsigned long>(v.overLimitStopIssues), static_cast<unsigned long>(v.softLimitIssues),
            static_cast<unsigned>(v.units.encoder.countsPerUnit.source));
        for (std::size_t i = 0; i < Ess::READ_INPUT_COUNT && fits; ++i)
            fits = append(output_, sizeof(output_), used,
                "%s{\"function\":%u,\"known\":%s,\"inverted\":%s,\"wiring\":%u,\"level_known\":%s,\"level\":null}",
                i ? "," : "", raw.inputFunctions[i], boolean(v.inputFunctionKnown[i]), boolean(v.inputInverted[i]),
                static_cast<unsigned>(v.wiring[i]), boolean(v.inputLevelKnown[i]));
        fits = fits && append(output_, sizeof(output_), used,
            "],\"stored_serial\":{\"baud_code\":%u,\"format_code\":%u,\"activation\":\"power_cycle_required\"},\"units\":\"command_scale_unresolved_encoder_readback_only\"}", raw.baud, raw.format);
    }
    if (context.kind == Ess::ReadKind::STATE)
        fits = fits && append(output_, sizeof(output_), used,
            ",\"decode_config\":{\"operation_id\":%lu,\"word_order_known\":%s,\"word_order\":%u,\"algorithm_known\":%s,\"algorithm\":%u}",
            static_cast<unsigned long>(context.configOperationId), boolean(context.stateWordOrderKnown), static_cast<unsigned>(context.stateWordOrder),
            boolean(context.stateAlgorithmKnown), static_cast<unsigned>(context.stateAlgorithm));
    fits = fits && append(output_, sizeof(output_), used, ",\"steps\":[");
    bool firstStep = true;
    for (std::size_t i = 0; i < Ess::READ_MAX_STEPS && fits; ++i) {
        const auto& v = context.observations[i];
        if (!v.count) continue; // Failed evidence does not increment completedSteps.
        uint8_t tx[Ess::READ_REQUEST_LEN]; char txHex[Ess::READ_REQUEST_LEN * 2 + 1], rxHex[Ess::READ_MAX_REPLY_BYTES * 2 + 1];
        const auto txLength = Ess::buildReadRegisters(context.target.address, v.first, v.count, tx, sizeof(tx));
        hex(tx, txLength, txHex, sizeof(txHex)); hex(v.raw, v.length, rxHex, sizeof(rxHex));
        fits = append(output_, sizeof(output_), used,
            "%s{\"step\":%u,\"first\":%u,\"count\":%u,\"event\":%u,\"status\":\"%s\",\"detail\":%ld,\"frame_error\":%u,\"qualified\":%s,\"attempted_us\":%llu,\"earliest_us\":%llu,\"latest_us\":%llu,\"delivered_us\":%llu,\"tx\":\"%s\",\"rx\":\"%s\",\"received_length\":%u,\"tx_accepted\":%u,\"execution_unknown\":%s,\"transport_detail\":%ld}",
            firstStep ? "" : ",", static_cast<unsigned>(i), v.first, v.count, static_cast<unsigned>(v.event),
            MotorControlRS::errToString(v.status.code), static_cast<long>(v.status.detail), static_cast<unsigned>(v.frameError), boolean(v.qualified),
            static_cast<unsigned long long>(v.attemptedUs), static_cast<unsigned long long>(v.earliestUs), static_cast<unsigned long long>(v.latestUs), static_cast<unsigned long long>(v.deliveredUs),
            txHex, rxHex, static_cast<unsigned>(v.receivedLength), static_cast<unsigned>(v.txAccepted), boolean(v.executionUnknown), static_cast<long>(v.transportDetail));
        firstStep = false;
    }
    fits = fits && append(output_, sizeof(output_), used, "]}");
    if (!fits) {
        error(id, inspection ? "result" : readCommandName(context.kind), "output_full");
        if (!inspection) { if (outputPending_) pendingTerminalOperation_ = operationId; else untrack(operationId); }
        return true;
    }
    emit(inspection ? 0 : operationId); return true;
}

bool Console::formatRecovery(uint32_t id, uint32_t commandId, uint32_t operationId,
                             const Rtu::RecoveryResult& result, bool inspection) noexcept {
    const char* outcome = "expired";
    switch (result.outcome) {
    case Rtu::RecoveryOutcome::RECOVERED: outcome = "recovered"; break;
    case Rtu::RecoveryOutcome::EXPIRED: break;
    case Rtu::RecoveryOutcome::READ_ERROR: outcome = "read_error"; break;
    case Rtu::RecoveryOutcome::TRANSPORT_ERROR: outcome = "transport_error"; break;
    }
    const int written = std::snprintf(output_, sizeof(output_),
        "{\"type\":\"%s\",\"profile\":\"ess_rs\",\"id\":%lu,\"command\":\"%s\",\"command_id\":%lu,\"operation_id\":%lu,\"ok\":%s,\"recovery\":true,\"outcome\":\"%s\",\"transport\":\"%s\",\"requested_us\":%llu,\"deadline_us\":%llu,\"finished_us\":%llu}",
        inspection ? "reply" : "recovery", static_cast<unsigned long>(id), inspection ? "result" : "recover",
        static_cast<unsigned long>(commandId), static_cast<unsigned long>(operationId),
        boolean(result.outcome == Rtu::RecoveryOutcome::RECOVERED), outcome, Rtu::reasonName(result.reason),
        static_cast<unsigned long long>(result.requestedUs), static_cast<unsigned long long>(result.deadlineUs),
        static_cast<unsigned long long>(result.finishedUs));
    if (written < 0 || static_cast<std::size_t>(written) >= sizeof(output_)) {
        error(id, inspection ? "result" : "recover", "output_full");
        if (!inspection) { if (outputPending_) pendingTerminalOperation_ = operationId; else untrack(operationId); }
        return true;
    }
    emit(inspection ? 0 : operationId);
    return true;
}

}} // namespace MotorControlRSExample::Probe
