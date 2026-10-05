// SPDX-License-Identifier: MIT
#include "ProbeConsole.h"
#include "ConsoleText.h"
#include "MotorControlRS/Version.h"
#include "MotorControlRS/profiles/ess_rs/Traffic.h"
#include "MotorControlRS/profiles/ess_rs/Registers.h"
#include "MotorControlRS/profiles/ess_rs/Segments.h"
#include "MotorControlRS/profiles/ess_rs/ControlSettings.h"
#include "MotorControlRS/profiles/ess_rs/Tuning.h"

#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <limits>

namespace MotorControlRSExample { namespace Probe {
Action driverAdmissionStatus(const MotorControlRS::Status& status) noexcept {
    using MotorControlRS::Err;
    using MotorControlRS::ESS_RS::DriverError;
    if (status) return Action::OK;
    if (status.code != Err::UNSUPPORTED) return Action::INVALID;
    switch (static_cast<DriverError>(status.detail)) {
    case DriverError::OUTPUT_FUNCTION_UNRESOLVED:
    case DriverError::SEGMENT_SIGN_UNRESOLVED:
    case DriverError::CURRENT_LIMIT_UNRESOLVED:
    case DriverError::CURRENT_BASE_UNRESOLVED:
    case DriverError::TUNING_ACCESS_UNRESOLVED: return Action::UNRESOLVED;
    case DriverError::IO_EFFECTS_REQUIRED:
    case DriverError::CONTROL_EFFECTS_REQUIRED:
    case DriverError::TUNING_EFFECTS_REQUIRED: return Action::INVALID;
    default: return Action::UNSUPPORTED;
    }
}
namespace {

enum class Command : uint8_t { HELP, VERSION, CONFIG, STATUS, HEALTH, STATS, PROBE, CAPTURE_READ, RECOVER, RESET, MEMORY, LOAD, DRV, RESULT, CANCEL, RELEASE, READ, PROFILE, CAPS, READ_IDENTITY, READ_CONFIG, READ_STATE, HEALTH_CHECK, MONITOR, AXIS, PREPARE, ENABLE, MOTOR_RELEASE, ALARM_CLEAR, STOP, MOVE, POSITION_CLEAR, VELOCITY, DRIVER, IO, HOME, SEGMENT, CONTROL, TUNING, HOST, COMMUNICATION, PERSISTENCE, MOTION_PROFILE, DEBUG, DISCOVER, USEADDR, WIRING };
struct Entry { const char* name; Command command; const char* syntax; const char* effect; bool bus; const char* description; };
const Entry COMMANDS[] = {
    {"discover", Command::DISCOVER, "discover [profile ess_rs|manufacturer stepperonline] [addresses FIRST LAST] [tuple BAUD FORMAT] [query-ms 1..5000] [overall-ms 1..60000] [requests 1..256] [results 1..8] [identity] | discover inspect|cancel|restore|finish; max4 distinct tuples,128bytes,20tokens; defaults selected endpoint/current tuple,query500ms,overall5000ms,requests16,results8,no identity,no retries", "bounded_nonchanging_queries_retained_findings_host_restoration", true, "Find responding drives within explicit address, serial and time limits."},
    {"debug", Command::DEBUG, "debug [off|raw|decoded]", "observe_regular_operations_and_cached_diagnostics", false, "Show diagnostics; select raw or decoded traffic while ordinary commands run."},
    {"motion-profile", Command::MOTION_PROFILE, "motion-profile read|inspect|restore|forget | profile ess_rs motion-profile ...", "snapshot_position_parameters_restore_or_explicitly_release_snapshot", true, "Read, inspect or restore saved position parameters; forget releases the snapshot."},
    {"help", Command::HELP, "help [command]", "show_callable_commands", false, "List available commands or show usage for one command."},
    {"?", Command::HELP, "? [command]", "show_callable_commands", false, "Alias for help."},
    {"version", Command::VERSION, "version", "show_build", false, "Show the firmware version and console protocol."},
    {"ver", Command::VERSION, "ver", "show_build", false, "Alias for version."},
    {"config", Command::CONFIG, "config", "show_host_settings", false, "Show the selected address and host communication settings."},
    {"settings", Command::CONFIG, "settings", "show_host_settings", false, "Alias for config."},
    {"useaddr", Command::USEADDR, "useaddr 1..247", "idle_host_selection_invalidates_dependent_confidence_no_motor_io", false, "Select a host target while idle; this does not change the drive address."},
    {"wiring", Command::WIRING, "wiring [x0|x1|x2|x3|y0|y1 unknown|unconnected|connected]", "declare_external_wiring_only_no_device_assignment_or_io", false, "Declare connected or unconnected terminals without changing their functions."},
    {"host", Command::HOST, "host [baud RATE | fmt 8N1|8N2|8E1|8O1 | set RATE FORMAT | restore | caps]", "settled_host_serial_only_no_motor_settings", false, "Show or change host UART settings; drive settings stay separate."},
    {"communication", Command::COMMUNICATION, "communication [inspect | plan|begin address|baud|format VALUE [address] | host before|requested | confirm before|requested | finish]", "explicit_communication_session_no_save_restart_or_replay", true, "Plan and apply a drive communication change, then check its responding endpoint."},
    {"persistence", Command::PERSISTENCE, "persistence [inspect | snapshot | plan|begin save|factory-restore | verify | host before | finish]", "one_explicit_save_or_factory_restore_no_retry_or_inferred_durability", true, "Inspect, plan or attempt an explicit save or factory restoration."},
    {"status", Command::STATUS, "status", "show_cached_observations", false, "Show cached drive and transport observations, including their age."},
    {"health", Command::HEALTH, "health [check [address]]", "show_cached_health_or_explicitly_read_state", true, "Show cached health; health check explicitly refreshes drive state."},
    {"stats", Command::STATS, "stats [reset]", "show_or_clear_host_counters", false, "Show diagnostic counters; stats reset clears host counters only."},
    {"probe", Command::PROBE, "probe [address]", "read_model_word_only", true, "Read one model word to check communication."},
    {"ping", Command::PROBE, "ping [address]", "read_model_word_only", true, "Alias for probe."},
    {"capture-read", Command::CAPTURE_READ, "capture-read [address]", "read_0x0130_16_words_for_capture_qualification", true, "Read a fixed 16-word window for transport timing diagnostics."},
    {"read", Command::READ, "read identity|config|state [address]", "checked_nonchanging_read", true, "Refresh drive identity, configuration or state through checked reads."},
    {"profile", Command::PROFILE, "profile list | profile ess_rs identity|config|state|enable|release|clear-alarm|clear-position|normal-stop|emergency-stop [address] | profile ess_rs move-relative|move-absolute|move-angle ... | profile ess_rs velocity ... | profile ess_rs driver read|set ... | profile ess_rs io read|set ... | profile ess_rs segment ... | profile ess_rs control read|set ... | profile ess_rs tuning GROUP read|set ... | profile ess_rs communication ... | profile ess_rs persistence ... | profile ess_rs home ... | profile ess_rs motion-profile ... | profile ess_rs caps", "public_profile_operations", true, "List profiles or call a named ESS operation."},
    {"driver", Command::DRIVER, "driver read [address] | driver set field integer [field integer ...] [address] | profile ess_rs driver ...", "typed_drive_settings_with_checked_readback", true, "Read or update supported drive settings and check their readback."},
    {"io", Command::IO, "io read [address] | io set input-polarity|x0|x1|x2|x3|output-polarity|y0|y1|custom value [field value ...] [address]; none assigns function 0 | profile ess_rs io ...", "explicit_typed_terminal_settings_and_readback", true, "Read or update terminal functions and polarity; none selects function zero."},
    {"segment", Command::SEGMENT, "segment position|speed|start INDEX read [address] | segment position|speed|start INDEX set FIELD INTEGER [FIELD INTEGER ...] [address] | profile ess_rs segment ...", "indexed_stored_records_only_external_execution", true, "Read or update stored segment settings; this does not execute a segment."},
    {"control", Command::CONTROL, "control read [address] | control set algorithm|encoder-resolution|maximum-effective-current|closed-maximum-current|closed-base-current|open-maximum-current|lock-current|lock-delay INTEGER [field integer ...] [address]; algorithm open-loop|algorithm-1 | profile ess_rs control ...", "stopped_native_control_settings_and_checked_readback", true, "Read or update supported control settings while the drive is stopped."},
    {"tuning", Command::TUNING, "tuning filters|current-loop|la|collision read [address] | tuning GROUP set FIELD INTEGER [FIELD INTEGER ...] [address]; filters: input-filter pulse-low-pass deviation-threshold arrival-window arrival-time pulse-mean; current-loop: multiplier kp ki kc; la: kp1 kv1 node1 kp2 kv2 node2 kvf position-ki; collision: threshold current | profile ess_rs tuning ...", "qualified_stopped_native_tuning_and_checked_readback", true, "Read or update named filter, current-loop, LA or collision parameters."},
    {"home", Command::HOME, "home methods | home method search_native return_native ramp_native zero [address] | profile ess_rs home ...", "qualified_homing_with_fresh_completion_and_zero_evidence", true, "List homing methods or request a supported method with explicit parameters."},
    {"enable", Command::ENABLE, "enable [address]", "request_enable_then_observe_flags", true, "Request motor enable and observe the resulting flags."},
    {"motor-release", Command::MOTOR_RELEASE, "motor-release [address]", "request_release_then_observe_flags", true, "Request motor release and observe the resulting flags."},
    {"alarm-clear", Command::ALARM_CLEAR, "alarm-clear [address]", "request_clear_resettable_alarm_then_observe_flags", true, "Request alarm clearing and check the drive flags."},
    {"stop", Command::STOP, "stop normal|direct [address]", "priority_stop_with_explicit_policy_then_observe_flags", true, "Request a priority normal or direct motor stop."},
    {"move", Command::MOVE, "move relative|absolute value unit frame native_rpm configured [basis actual|commanded|queued] [round mode error [approx error]] [address] | move angle value unit frame positive|negative|shortest reject|positive|negative native_rpm configured [round mode error [approx error]] [address]", "finite_move_through_public_coordinate_preparation", true, "Request a finite relative, absolute or wrapped-angle move with explicit units."},
    {"position-clear", Command::POSITION_CLEAR, "position-clear [address]", "explicit_device_position_zero_only", true, "Explicitly set the drive position counter to zero."},
    {"velocity", Command::VELOCITY, "velocity value rpm|steps/s|fullsteps/s|counts/s|turns/s|deg/s|rad/s|mm/s native|motor|load duration_ms configured normal|direct [round mode error [approx error]] [address]", "finite_serial_velocity_with_explicit_stop", true, "Request a bounded velocity operation with an explicit stop policy."},
    {"monitor", Command::MONITOR, "monitor [off | interval_ms count]", "finite_nonconsuming_state_polling", true, "Read drive state a finite number of times; off ends local polling."},
    {"caps", Command::CAPS, "caps", "show_implemented_routes_and_explicit_capability_gaps", false, "Show implemented operations and unsupported or unresolved capabilities."},
    {"axis", Command::AXIS, "axis config [set field value [maximum]] | axis origin exact_native", "configure_host_coordinates_only", false, "Configure host coordinates, units and origin without motor traffic."},
    {"prepare", Command::PREPARE, "prepare absolute value unit frame [round [max_error [radian_error]]] | prepare relative value unit frame basis [round [max_error [radian_error]]] | prepare angle value unit frame path tie [round [max_error [radian_error]]]", "preview_public_target_arithmetic_without_motion", false, "Preview coordinate conversion and rounding without moving the motor."},
    {"recover", Command::RECOVER, "recover", "recover_host_transport_only", false, "Recover the host transport; this does not stop or reset the motor."},
    {"reset", Command::RESET, "reset", "clear_host_counters_only", false, "Clear host diagnostic counters only."},
    {"memory", Command::MEMORY, "memory", "show_cached_memory", false, "Show cached memory and stack measurements."},
    {"load", Command::LOAD, "load [work_us owner_delay_us console_bytes]", "configure_or_report_host_load", false, "Show or configure the optional host workload used for timing checks."},
    {"drv", Command::DRV, "drv", "show_owner_queue_and_output", false, "Show the bus owner, queues, retained results and console pressure."},
    {"result", Command::RESULT, "result [operation_id]", "inspect_retained_result_without_consuming", false, "Inspect an operation without consuming its retained result."},
    {"cancel", Command::CANCEL, "cancel [operation_id]", "cancel_local_work_not_motor_stop", false, "Cancel local work; use stop to request a motor stop."},
    {"release", Command::RELEASE, "release operation_id", "release_retained_terminal_result", false, "Release an inspected terminal result to free its storage."}
};
const Entry IDENTITY_ENTRY = {"read-identity", Command::READ_IDENTITY, "read identity [address]", "checked_identity_read", true, "Read drive identity."};
const Entry CONFIG_ENTRY = {"read-config", Command::READ_CONFIG, "read config [address]", "checked_configuration_read", true, "Read drive configuration."};
const Entry STATE_ENTRY = {"read-state", Command::READ_STATE, "read state [address]", "checked_nonconsuming_state_read", true, "Read drive state."};
const Entry HEALTH_ENTRY = {"read-state", Command::HEALTH_CHECK, "health check [address]", "explicit_nonconsuming_state_refresh", true, "Refresh drive state and health."};
namespace Ess = MotorControlRS::ESS_RS;

const char* tuningName(Ess::DriverGroup group) {
    switch (group) {
    case Ess::DriverGroup::FILTERS: return "filters";
    case Ess::DriverGroup::CURRENT_LOOP: return "current_loop";
    case Ess::DriverGroup::LA: return "la";
    case Ess::DriverGroup::COLLISION: return "collision";
    default: return "unknown";
    }
}
bool tuningGroup(const char* token, Ess::DriverGroup& group) {
    if (!std::strcmp(token,"filters")) group=Ess::DriverGroup::FILTERS;
    else if (!std::strcmp(token,"current-loop")) group=Ess::DriverGroup::CURRENT_LOOP;
    else if (!std::strcmp(token,"la")) group=Ess::DriverGroup::LA;
    else if (!std::strcmp(token,"collision")) group=Ess::DriverGroup::COLLISION;
    else return false;
    return true;
}
const char* tuningField(Ess::DriverGroup group, std::size_t slot) {
    const char* filters[]={"input-filter","pulse-low-pass","deviation-threshold","arrival-window","arrival-time","pulse-mean"};
    const char* current[]={"multiplier","kp","ki","kc"};
    const char* la[]={"kp1","kv1","node1","kp2","kv2","node2","kvf","position-ki"};
    const char* collision[]={"threshold","current"};
    if (slot>=Ess::tuningFieldCount(group)) return "";
    return group==Ess::DriverGroup::FILTERS?filters[slot]:group==Ess::DriverGroup::CURRENT_LOOP?current[slot]:group==Ess::DriverGroup::LA?la[slot]:collision[slot];
}

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
const char* driverOutcome(Ess::DriverOutcome outcome) {
    switch (outcome) {
    case Ess::DriverOutcome::NONE: return "none";
    case Ess::DriverOutcome::SUCCESS: return "success";
    case Ess::DriverOutcome::REPLY_ERROR: return "reply_error";
    case Ess::DriverOutcome::TRANSPORT_ERROR: return "transport_error";
    case Ess::DriverOutcome::CANCELLED: return "cancelled";
    case Ess::DriverOutcome::DEADLINE: return "deadline";
    case Ess::DriverOutcome::TIMING_UNQUALIFIED: return "timing_unqualified";
    case Ess::DriverOutcome::UNCONFIRMED_RESPONSE: return "unconfirmed_response";
    case Ess::DriverOutcome::READBACK_MISMATCH: return "readback_mismatch";
    } return "unknown";
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

bool parseFormat(const char* token, HostFormat& value) {
    for (uint8_t i = 0; i < 4; ++i) {
        const auto format = static_cast<HostFormat>(i);
        if (!std::strcmp(token, formatName(format))) { value = format; return true; }
    }
    return false;
}
bool supports(const HostSnapshot& state, const HostTuple& tuple) {
    if (!reviewedHostTuple(tuple)) return false;
    const auto format = static_cast<uint8_t>(tuple.format);
    if (format >= 4 || !state.supportedFormats[format]) return false;
    for (uint32_t baud : state.supportedBauds) if (baud && baud == tuple.baud) return true;
    return false;
}
const char* hostFailure(HostFailure failure) {
    switch (failure) { case HostFailure::NONE: return "none"; case HostFailure::ADAPTER: return "adapter";
    case HostFailure::RUNNER: return "runner"; case HostFailure::RESTORE: return "restore"; }
    return "unknown";
}
bool hostTuple(char* output, std::size_t capacity, std::size_t& used, const HostTuple& tuple) {
    return append(output, capacity, used, "{\"baud\":%lu,\"format\":\"%s\"}",
                  static_cast<unsigned long>(tuple.baud), formatName(tuple.format));
}
bool hostState(char* output, std::size_t capacity, std::size_t& used, const HostSnapshot& state) {
    if (!append(output, capacity, used, ",\"original\":") || !hostTuple(output, capacity, used, state.original) ||
        !append(output, capacity, used, ",\"requested\":") || !hostTuple(output, capacity, used, state.requested) ||
        !append(output, capacity, used, ",\"active\":") || !hostTuple(output, capacity, used, state.active) ||
        !append(output, capacity, used, ",\"active_known\":%s,\"blocked\":%s,\"configuring\":%s,\"serial_generation\":%lu,\"actual_baud\":%lu,\"failure\":\"%s\",\"device_settings_changed\":false,\"supported_bauds\":[",
            state.activeKnown ? "true" : "false", state.blocked ? "true" : "false", state.configuring ? "true" : "false",
            static_cast<unsigned long>(state.generation), static_cast<unsigned long>(state.actualBaud), hostFailure(state.failure))) return false;
    bool comma = false;
    for (uint32_t baud : state.supportedBauds) if (baud) {
        if (!append(output, capacity, used, "%s%lu", comma ? "," : "", static_cast<unsigned long>(baud))) return false;
        comma = true;
    }
    if (!append(output, capacity, used, "],\"supported_formats\":[")) return false;
    comma = false;
    for (uint8_t i = 0; i < 4; ++i) if (state.supportedFormats[i]) {
        if (!append(output, capacity, used, "%s\"%s\"", comma ? "," : "", formatName(static_cast<HostFormat>(i)))) return false;
        comma = true;
    }
    const auto& t = state.timing;
    return append(output, capacity, used, "],\"timing\":{\"character_min_us\":%lu,\"character_max_us\":%lu,\"stop_guard_us\":%lu,\"capture_period_us\":%lu,\"gap15_us\":%lu,\"gap35_us\":%lu,\"reply_gap_us\":%lu,\"response_timeout_us\":%lu,\"request_timeout_us\":%lu,\"recovery_guard_us\":%lu,\"tx_timeout_us\":%lu}}",
        static_cast<unsigned long>(t.characterMinUs), static_cast<unsigned long>(t.characterMaxUs),
        static_cast<unsigned long>(t.stopGuardUs), static_cast<unsigned long>(t.capturePeriodUs),
        static_cast<unsigned long>(t.runner.gap15Us), static_cast<unsigned long>(t.runner.gap35Us),
        static_cast<unsigned long>(t.replyGapUs), static_cast<unsigned long>(t.responseTimeoutUs),
        static_cast<unsigned long>(t.requestTimeoutUs), static_cast<unsigned long>(t.recoveryGuardUs),
        static_cast<unsigned long>(t.runner.txTimeoutUs));
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
    if (!append(output, capacity, used, ",\"bus_traffic\":false,\"motion_command\":false,\"wire_motion\":\"not_requested\",\"polarity_known\":%s,\"configuration_generation\":%lu,\"target\":%lu,\"address\":%u,\"binding_generation\":%lu",
        view.commandPolarityKnown ? "true" : "false",
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

const char* helpGroup(Command command) {
    switch(command) {
    case Command::HELP: case Command::VERSION: case Command::CAPS: case Command::PROFILE:
        return "Getting started";
    case Command::READ: case Command::READ_IDENTITY: case Command::READ_CONFIG: case Command::READ_STATE:
    case Command::PROBE: case Command::STATUS: case Command::HEALTH: case Command::MONITOR:
        return "Read and observe";
    case Command::MOVE: case Command::VELOCITY: case Command::HOME: case Command::ENABLE:
    case Command::MOTOR_RELEASE: case Command::ALARM_CLEAR: case Command::POSITION_CLEAR: case Command::STOP:
        return "Motor operations";
    case Command::DRIVER: case Command::IO: case Command::SEGMENT: case Command::CONTROL: case Command::TUNING:
    case Command::MOTION_PROFILE: case Command::PERSISTENCE:
        return "Drive settings";
    case Command::CONFIG: case Command::HOST: case Command::USEADDR: case Command::WIRING:
    case Command::COMMUNICATION: case Command::DISCOVER: case Command::AXIS: case Command::PREPARE:
        return "Host and setup";
    case Command::RESULT: case Command::CANCEL: case Command::RELEASE:
        return "Operation results";
    default: return "Diagnostics";
    }
}
const char* helpExample(Command command) {
    switch(command) {
    case Command::HELP: return "help move\n  @1 status  (machine-readable JSON)";
    case Command::PROBE: return "probe 1";
    case Command::READ: return "read config 1\n  read state 1";
    case Command::DEBUG: return "debug decoded\n  debug off";
    case Command::MOVE: return "move relative 100 steps native 60 configured 1";
    case Command::STOP: return "stop normal 1\n  stop direct 1";
    case Command::RESULT: return "result 12";
    case Command::RELEASE: return "release 12  (after inspecting the terminal result)";
    case Command::CANCEL: return "cancel 12  (local cancellation; use stop for the motor)";
    case Command::HOST: return "host\n  host caps";
    case Command::DISCOVER: return "discover addresses 1 1\n  discover inspect";
    case Command::MOTION_PROFILE: return "motion-profile read\n  motion-profile inspect";
    case Command::MONITOR: return "monitor 500 10\n  monitor off";
    case Command::PROFILE: return "profile list\n  profile ess_rs caps";
    case Command::AXIS: return "axis config";
    case Command::PREPARE: return "prepare relative 100 steps native actual";
    case Command::HOME: return "home methods";
    default: return nullptr;
    }
}
bool helpEffect(char* output,std::size_t capacity,std::size_t& used,const char* effect) {
    for (const char* p=effect;*p;++p) {
        if (used+1>=capacity) return false;
        output[used++]=*p=='_'?' ':*p;
    }
    output[used]='\0';
    return true;
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
    case Core::ActionOutcome::ACKNOWLEDGED: return "acknowledged";
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
// Homing retains eight observations. One fixed column schema keeps every
// provenance field within the existing line bound, including 64-bit extremes.
bool homeEvidence(char* output, std::size_t capacity, std::size_t& used, const Ess::ActionEvidence& e) {
    char raw[Ess::ACTION_MAX_REPLY_BYTES * 2 + 1]; hex(e.raw, e.length, raw, sizeof(raw));
    return append(output, capacity, used,
        "[%u,%u,\"%s\",%u,%u,%s,%s,%s,%s,%llu,%llu,%llu,%ld,\"%s\",%ld,%u]",
        e.step, static_cast<unsigned>(e.event), raw, static_cast<unsigned>(e.receivedLength), static_cast<unsigned>(e.txAccepted),
        boolean(e.txComplete), boolean(e.responseConfirmed), boolean(e.qualified), boolean(e.executionUnknown),
        static_cast<unsigned long long>(e.earliestUs), static_cast<unsigned long long>(e.latestUs), static_cast<unsigned long long>(e.deliveredUs),
        static_cast<long>(e.transportDetail), Core::errToString(e.status.code), static_cast<long>(e.status.detail), static_cast<unsigned>(e.frameError));
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
    case Action::UNRESOLVED: return "unresolved";
    case Action::UNIMPLEMENTED: return "unimplemented";
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

bool Console::welcome() noexcept {
    if (outputPending()) return false;
    const Format saved=outputFormat_;
    outputFormat_=Format::HUMAN;
    std::snprintf(output_,sizeof(output_),
        "MotorControl-RS %s\nType help or ? for commands; help COMMAND shows usage.\nPlain commands use human output. Prefix @ID for JSON, for example @1 status.",Core::VERSION);
    emit(0,true);
    outputFormat_=saved;
    return true;
}

bool Console::reportTraffic(const Core::TrafficRecord& record, DebugMode mode,
                          const Core::TrafficRecord* request) noexcept {
    if (mode==DebugMode::OFF || mode>DebugMode::DECODED || outputPending() || !host_.emitLine ||
        record.length>Core::TRAFFIC_MAX_BYTES) return false;
    char raw[Core::TRAFFIC_MAX_BYTES*2+1], expected[Core::TRAFFIC_MAX_BYTES*2+1];
    hex(record.bytes,record.length,raw,sizeof(raw));
    const bool matching=request && request->transaction==record.transaction && request->length<=Core::TRAFFIC_MAX_BYTES;
    hex(matching?request->bytes:nullptr,matching?request->length:0,expected,sizeof(expected));
    std::size_t used=0;
    if (!append(output_,sizeof(output_),used,
        "{\"type\":\"traffic\",\"profile\":\"ess_rs\",\"mode\":\"%s\",\"sequence\":%llu,\"transaction\":%llu,\"kind\":\"%s\",\"at_us\":%llu,\"start_us\":%llu,\"end_us\":%llu,\"uncertainty_us\":%lu,\"length\":%u,\"code\":%u,\"complete\":%s,\"raw_hex\":\"%s\",\"expected_request_hex\":\"%s\",\"expected_request_sequence\":%llu,\"decode_status\":",
        mode==DebugMode::RAW?"raw":"decoded",static_cast<unsigned long long>(record.sequence),
        static_cast<unsigned long long>(record.transaction),Core::trafficKindName(record.kind),
        static_cast<unsigned long long>(record.atUs),static_cast<unsigned long long>(record.startUs),
        static_cast<unsigned long long>(record.endUs),static_cast<unsigned long>(record.uncertaintyUs),
        record.length,record.code,boolean(record.complete),raw,mode==DebugMode::DECODED && record.kind==Core::TrafficKind::RX?expected:"",static_cast<unsigned long long>(mode==DebugMode::DECODED && record.kind==Core::TrafficKind::RX && matching?request->sequence:0))) return false;
    if (mode==DebugMode::DECODED && (record.kind==Core::TrafficKind::TX || record.kind==Core::TrafficKind::RX)) {
        Ess::TrafficDecoded decoded; Ess::FrameError frameError=Ess::FrameError::NONE;
        const auto status=Ess::decodeTraffic(record,matching?request:nullptr,decoded,&frameError);
        if (!append(output_,sizeof(output_),used,"\"%s\",\"decode_detail\":%ld,\"frame_error\":%u,\"decoded\":",Core::errToString(status.code),static_cast<long>(status.detail),static_cast<unsigned>(frameError))) return false;
        if (status || (status.code==Core::Err::EXCEPTION && decoded.isException)) {
            if (!append(output_,sizeof(output_),used,"{\"address\":%u,\"function\":%u,\"register_start\":%u,\"register_count\":%u,\"exception\":%s,\"exception_code\":%u,\"words\":[",decoded.address,decoded.function,decoded.start,decoded.count,boolean(decoded.isException),decoded.exceptionCode)) return false;
            for (std::size_t i=0;i<decoded.wordCount;++i)
                if (!append(output_,sizeof(output_),used,"%s%u",i?",":"",decoded.words[i])) return false;
            if (!append(output_,sizeof(output_),used,"],\"function_name\":\"%s\",\"register_names\":[",decoded.function==3?"read_registers":decoded.function==6?"write_register":"write_registers")) return false;
            for (std::size_t i=0;i<decoded.count;++i) {
                const auto* descriptor=Ess::findRegister(static_cast<uint16_t>(decoded.start+i));
                if (descriptor) {
                    if (!append(output_,sizeof(output_),used,"%s\"%s\"",i?",":"",descriptor->name)) return false;
                } else if (!append(output_,sizeof(output_),used,"%snull",i?",":"")) return false;
            }
            if (!append(output_,sizeof(output_),used,"]}")) return false;
        } else if (!append(output_,sizeof(output_),used,"null")) return false;
    } else if (!append(output_,sizeof(output_),used,"null,\"decode_detail\":0,\"frame_error\":0,\"decoded\":null")) return false;
    if (!append(output_,sizeof(output_),used,"}")) return false;
    // Direct, nonblocking, best-effort delivery. No pending-output reservation.
    if (trafficFormat_ == Format::HUMAN) {
        if (!renderHuman(output_,pendingOutput_,sizeof(pendingOutput_))) return false;
        return host_.emitLine(host_.context,pendingOutput_,std::strlen(pendingOutput_));
    }
    return host_.emitLine(host_.context,output_,used);
}

bool Console::outstanding(uint32_t id) const noexcept {
    if (stopReply_.pending && stopReply_.id == id) return true;
    for (const auto& item : outstanding_) if (item.commandId == id) return true;
    return false;
}
bool Console::track(uint32_t id, uint32_t operationId, bool stop) noexcept {
    const std::size_t begin = stop ? OUTSTANDING_CAPACITY - 1 : 0;
    const std::size_t end = stop ? OUTSTANDING_CAPACITY : OUTSTANDING_CAPACITY - 1;
    for (std::size_t i = begin; i < end; ++i) { auto& item = outstanding_[i]; if (!item.commandId) {
        item.commandId = id; item.operationId = operationId; item.format = outputFormat_; return true;
    } }
    return false;
}
void Console::untrack(uint32_t operationId) noexcept {
    for (auto& item : outstanding_) if (item.operationId == operationId) item = Outstanding();
}
void Console::emit(uint32_t terminalOperation, bool humanText) noexcept {
    if (outputPending_) { ++inputDropped_; return; }
    if (outputFormat_ == Format::HUMAN && !humanText) {
        if (!renderHuman(output_, pendingOutput_, sizeof(pendingOutput_))) {
            if (terminalOperation)
                std::snprintf(pendingOutput_, sizeof(pendingOutput_),
                    "ERROR: response formatting failed. Inspect @ID result %lu for JSON evidence.",
                    static_cast<unsigned long>(terminalOperation));
            else
                std::snprintf(pendingOutput_, sizeof(pendingOutput_),
                    "ERROR: response formatting failed. Inspect retained results with @ID result N; do not repeat writes.");
        }
    } else std::memcpy(pendingOutput_, output_, std::strlen(output_) + 1);
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
        outputFormat_ = reply.format;
        // action emits at most this one additional line. The cleared pending
        // field bounds serviceOutput's nested call to one level.
        action(reply.id, "stop", reply.result, reply.address, reply.operationId);
        return !outputPending_;
    }
    return true;
}

void Console::error(uint32_t id, const char* command, const char* reason) noexcept {
    if (outputFormat_ == Format::HUMAN) {
        std::size_t used=0;
        const bool fits=append(output_,sizeof(output_),used,"ERROR: %s: ",command) &&
            helpEffect(output_,sizeof(output_),used,reason) &&
            append(output_,sizeof(output_),used,".%s%s%s%s",syntax_?"\nUsage: ":"",syntax_?syntax_:"",
                helpCommand_?"\nHelp: help ":"\nType help for available commands.",helpCommand_?helpCommand_:"");
        if (!fits) std::snprintf(output_,sizeof(output_),"ERROR: command failed. Type help for usage.");
        emit(0,true); return;
    }
    std::snprintf(output_, sizeof(output_),
        "{\"type\":\"reply\",\"profile\":\"ess_rs\",\"id\":%lu,\"command\":\"%s\",\"ok\":false,\"result\":\"%s\"}",
        static_cast<unsigned long>(id), command, reason);
    emit();
}

void Console::action(uint32_t id, const char* command, Action result, uint8_t address, uint32_t operationId) noexcept {
    const bool typed = std::strcmp(command, "read-identity") == 0 || std::strcmp(command, "read-config") == 0 || std::strcmp(command, "read-state") == 0;
    const bool motor = std::strcmp(command, "enable") == 0 || std::strcmp(command, "motor-release") == 0 || std::strcmp(command, "alarm-clear") == 0 || std::strcmp(command, "position-clear") == 0 || std::strcmp(command, "stop") == 0 || std::strncmp(command, "move-", 5) == 0 || std::strcmp(command, "velocity") == 0 || std::strcmp(command, "driver") == 0 || (std::strcmp(command, "io") == 0 || std::strcmp(command, "segment") == 0 || std::strcmp(command, "control") == 0 || std::strcmp(command, "tuning") == 0);
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
        // Select framing before errors/tokenization: even a malformed or
        // overlong correlated command must receive a machine-readable error.
        std::size_t first=0;
        while(first<length_ && (line_[first]==' ' || line_[first]=='\t')) ++first;
        outputFormat_=first<length_ && line_[first]=='@'?Format::JSON:defaultFormat_;
        syntax_=helpCommand_=nullptr;
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

void Console::setReportSerial(const HostTuple* tuple, uint32_t generation) noexcept {
    reportingGeneration_ = tuple && reviewedHostTuple(*tuple) ? generation : 0;
    reportingTuple_ = tuple ? *tuple : HostTuple();
}
const char* communicationField(Ess::CommunicationField field) {
    return field == Ess::CommunicationField::ADDRESS ? "address" : field == Ess::CommunicationField::BAUD ? "baud" : "format";
}
bool communicationEndpoint(char* output, std::size_t capacity, std::size_t& used,
                           const Core::ReadTarget& target, const Core::ActiveSerialTuple& serial) {
    return append(output, capacity, used, "{\"target\":%lu,\"address\":%u,\"generation\":%lu,\"baud\":%lu,\"data_bits\":%u,\"parity\":%u,\"stop_bits\":%u,\"serial_known\":%s}",
        static_cast<unsigned long>(target.id), target.address, static_cast<unsigned long>(target.generation),
        static_cast<unsigned long>(serial.baud), serial.dataBits, static_cast<unsigned>(serial.parity), serial.stopBits, boolean(serial.known));
}
bool communicationEvidence(char* output, std::size_t capacity, std::size_t& used, const Ess::CommunicationEvidence& e) {
    return append(output, capacity, used, "{\"endpoint\":") && communicationEndpoint(output, capacity, used, e.target, e.serial) &&
        append(output, capacity, used, ",\"eligible_us\":%llu,\"deadline_us\":%llu,\"readback_known\":%s,\"readback\":%u,\"wire\":",
            static_cast<unsigned long long>(e.eligibleUs), static_cast<unsigned long long>(e.deadlineUs), boolean(e.readbackKnown), e.readback) &&
        actionEvidence(output, capacity, used, e.wire) && append(output, capacity, used, "}");
}
bool communicationContext(char* output, std::size_t capacity, std::size_t& used, const Ess::CommunicationContext* c) {
    if (!c) return append(output, capacity, used, "null");
    if (!append(output, capacity, used,
        "{\"operation_id\":%lu,\"field\":\"%s\",\"register\":%u,\"previous\":%u,\"requested\":%u,\"readback\":%u,\"readback_known\":%s,\"observed_active_known\":%s,\"activation_unknown\":%s,\"effects\":%s,\"uncertain\":%s,\"execution\":%u,\"state\":%u,\"outcome\":%u,\"write_outcome\":%u,\"step\":%u,\"confirmations\":%u,\"save\":\"%s\",\"restart\":\"%s\",\"status\":\"%s\",\"detail\":%ld,\"deadline_us\":%llu,\"before\":",
        static_cast<unsigned long>(c->operationId), communicationField(c->request.field), c->reg,c->previous,c->requested,c->readback,
        boolean(c->readbackKnown),boolean(c->observedActiveKnown),boolean(c->activationUnknown),boolean(c->effects),boolean(c->uncertain),
        static_cast<unsigned>(c->execution),static_cast<unsigned>(c->state),static_cast<unsigned>(c->outcome),static_cast<unsigned>(c->writeOutcome),c->step,c->confirmations,
        c->save==Ess::CommunicationRequirement::REQUIRED?"required":"unresolved", c->restart==Ess::CommunicationRequirement::REQUIRED?"required":"unresolved",
        Core::errToString(c->status.code),static_cast<long>(c->status.detail),static_cast<unsigned long long>(c->deadlineUs)) ||
        !communicationEndpoint(output,capacity,used,c->beforeTarget,c->beforeSerial) || !append(output,capacity,used,",\"requested_endpoint\":") ||
        !communicationEndpoint(output,capacity,used,c->requestedTarget,c->requestedSerial) || !append(output,capacity,used,",\"observed_active\":") ||
        !communicationEndpoint(output,capacity,used,c->observedActiveTarget,c->observedActiveSerial) || !append(output,capacity,used,",\"write_evidence\":") ||
        !communicationEvidence(output,capacity,used,c->writeEvidence) || !append(output,capacity,used,",\"confirmation_evidence\":[")) return false;
    for (uint8_t i=0;i<c->confirmations && i<2;++i)
        if ((i && !append(output,capacity,used,",")) || !communicationEvidence(output,capacity,used,c->confirmationEvidence[i])) return false;
    return append(output,capacity,used,"]}");
}
const char* persistenceKind(Ess::PersistenceKind kind) {
    return kind == Ess::PersistenceKind::SAVE ? "save" : "factory-restore";
}
// These snapshots are explicitly partial. Compact arrays retain the raw values
// and observation budgets without duplicating the descriptive register ledger.
bool persistenceSnapshot(char* output, std::size_t capacity, std::size_t& used,
                         const Ess::ConfigObservation& c, const Ess::IdentityObservation& i,
                         const Ess::StateObservation& s, const Core::ActiveSerialTuple& serial,
                         uint32_t generation) {
    const auto& r=c.raw;
    if (!append(output,capacity,used,"{\"endpoint\":") || !communicationEndpoint(output,capacity,used,c.target,serial) ||
        !append(output,capacity,used," ,\"configuration_generation\":%lu,\"operation_ids\":[%lu,%lu,%lu],\"config_words\":[%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u],\"identity_words\":[%u,%u,%u,%u],\"motion_words\":[%u,%u],\"provenance\":[",
            static_cast<unsigned long>(generation),static_cast<unsigned long>(c.operationId),static_cast<unsigned long>(i.operationId),static_cast<unsigned long>(s.operationId),
            r.direction,r.subdivision,r.customNode,r.baud,r.format,r.overLimitStop,r.softLimitEnable,r.wordOrder,r.inputPolarity,
            r.inputFunctions[0],r.inputFunctions[1],r.inputFunctions[2],r.inputFunctions[3],r.algorithm,r.encoderResolution,
            i.rawModel,i.rawVersion,i.rawActiveNode,i.rawDip,s.rawAlarm,s.rawMotion)) return false;
    for (uint8_t index=0; index<7; ++index) {
        const auto& p=index<5?c.provenance[index]:index==5?i.provenance:s.provenance;
        char raw[Ess::READ_MAX_REPLY_BYTES*2+1];hex(p.raw,p.length,raw,sizeof(raw));
        if ((index && !append(output,capacity,used,",")) ||
            !append(output,capacity,used,"[%u,%u,\"%s\",%llu,%llu,%llu,%llu]",p.first,p.count,raw,
                static_cast<unsigned long long>(p.attemptedUs),static_cast<unsigned long long>(p.earliestUs),
                static_cast<unsigned long long>(p.latestUs),static_cast<unsigned long long>(p.deliveredUs))) return false;
    }
    return append(output,capacity,used,"]}");
}
bool persistenceContext(char* output, std::size_t capacity, std::size_t& used, const Ess::PersistenceContext* c) {
    if (!c) return append(output,capacity,used,"null");
    if (!append(output,capacity,used,
        "{\"operation_id\":%lu,\"kind\":\"%s\",\"state\":%u,\"outcome\":%u,\"execution\":%u,\"persistence\":%u,\"status\":\"%s\",\"detail\":%ld,\"register\":%u,\"value\":%u,\"effects\":%s,\"uncertain\":%s,\"snapshot_complete\":%s,\"verification_known\":%s,\"live_readback_known\":%s,\"restart_required_for_proof\":%s,\"manual_intervention_required\":%s,\"communication_changed\":%s,\"configuration_invalidated\":%s,\"matching_fields\":%u,\"verified_fields\":%u,\"verification_count\":%u,\"started_us\":%llu,\"deadline_us\":%llu,\"backup_source\":%lu,\"complete_backup\":%s,\"before\":",
        static_cast<unsigned long>(c->operationId),persistenceKind(c->kind),static_cast<unsigned>(c->state),static_cast<unsigned>(c->outcome),
        static_cast<unsigned>(c->execution),static_cast<unsigned>(c->persistence),Core::errToString(c->status.code),static_cast<long>(c->status.detail),c->reg,c->value,
        boolean(c->effects),boolean(c->uncertain),boolean(c->configurationSnapshotComplete),boolean(c->verificationKnown),boolean(c->liveReadbackKnown),
        boolean(c->restartRequiredForProof),boolean(c->manualInterventionRequired),boolean(c->communicationChanged),boolean(c->configurationInvalidated),
        c->matchingFields,c->verifiedFields,c->verificationCount,static_cast<unsigned long long>(c->startedUs),static_cast<unsigned long long>(c->deadlineUs),
        static_cast<unsigned long>(c->before.backupSourceId),boolean(c->before.completeBackupQualified)) ||
        !persistenceSnapshot(output,capacity,used,c->before.beforeConfig,c->before.beforeIdentity,c->before.stationary,c->before.beforeSerial,c->before.configurationGeneration) ||
        !append(output,capacity,used,",\"write_evidence\":") || !actionEvidence(output,capacity,used,c->writeEvidence) ||
        !append(output,capacity,used,",\"verification\":")) return false;
    if (c->verificationKnown) {
        if (!persistenceSnapshot(output,capacity,used,c->verification.config,c->verification.identity,c->verification.stationary,c->verification.serial,c->verification.configurationGeneration)) return false;
    } else if (!append(output,capacity,used,"null")) return false;
    if (!append(output,capacity,used,",\"restart_observed\":%s,\"restart_us\":%llu,\"restart_source\":%lu,\"fields\":[",
        boolean(c->verification.restartObserved),static_cast<unsigned long long>(c->verification.restartUs),static_cast<unsigned long>(c->verification.restartSourceId))) return false;
    for(uint8_t index=0;index<Ess::PERSISTENCE_FIELD_COUNT;++index) {
        const auto& f=c->fields[index];
        if ((index && !append(output,capacity,used,",")) || !append(output,capacity,used,"[%u,%u,%u,\"%s\",%s,%s]",
            f.reg,f.before,f.readback,f.sourceAccess?f.sourceAccess:"unresolved",boolean(f.readbackKnown),boolean(f.survivedRestart))) return false;
    }
    return append(output,capacity,used,"]}");
}
bool Console::appendReportSerial(std::size_t& used) noexcept {
    if (!used || output_[used - 1] != '}') return false;
    --used;
    return append(output_, sizeof(output_), used,
        ",\"host_serial\":{\"known\":%s,\"baud\":%lu,\"format\":\"%s\",\"generation\":%lu}}",
        boolean(reportingGeneration_ != 0), static_cast<unsigned long>(reportingGeneration_ ? reportingTuple_.baud : 0),
        reportingGeneration_ ? formatName(reportingTuple_.format) : "unknown", static_cast<unsigned long>(reportingGeneration_));
}

bool discoveryEvidence(char* out, std::size_t capacity, std::size_t& used, const Ess::ReadStepObservation& e) {
    if (e.length > sizeof(e.raw)) return false;
    char raw[sizeof(e.raw)*2+1]; hex(e.raw,e.length,raw,sizeof(raw));
    return append(out,capacity,used,"[%u,%u,%u,\"%s\",%u,%s,%llu,%llu,%llu,%llu,%ld,%u,%s,\"%s\",%ld,%u]",
        e.first,e.count,static_cast<unsigned>(e.event),raw,static_cast<unsigned>(e.receivedLength),boolean(e.qualified),
        static_cast<unsigned long long>(e.attemptedUs),static_cast<unsigned long long>(e.earliestUs),
        static_cast<unsigned long long>(e.latestUs),static_cast<unsigned long long>(e.deliveredUs),
        static_cast<long>(e.transportDetail),static_cast<unsigned>(e.txAccepted),boolean(e.executionUnknown),
        MotorControlRS::errToString(e.status.code),static_cast<long>(e.status.detail),static_cast<unsigned>(e.frameError));
}
bool discoveryScan(char* out, std::size_t capacity, std::size_t& used, const DiscoveryScan* scan) {
    if (!scan) return append(out,capacity,used,"null");
    const auto& s=*scan;
    if (s.count>DISCOVERY_MAX_RESULTS || s.settings.tupleCount>DISCOVERY_MAX_TUPLES) return false;
    if (!append(out,capacity,used,"{\"operation_id\":%lu,\"phase\":%u,\"outcome\":%u,\"owned\":%s,\"restored\":%s,\"released\":%s,\"cancel_requested\":%s,\"requests\":%u,\"count\":%u,\"tuple_index\":%u,\"address\":%u,\"started_us\":%llu,\"deadline_us\":%llu,\"finished_us\":%llu,\"original_target\":[%lu,%u,%lu],\"original_tuple\":",
        static_cast<unsigned long>(s.operationId),static_cast<unsigned>(s.phase),static_cast<unsigned>(s.outcome),boolean(s.owned),boolean(s.restored),boolean(s.released),boolean(s.cancelRequested),
        s.requests,s.count,s.tupleIndex,s.address,static_cast<unsigned long long>(s.startedUs),static_cast<unsigned long long>(s.deadlineUs),static_cast<unsigned long long>(s.finishedUs),
        static_cast<unsigned long>(s.originalTarget.id),s.originalTarget.address,static_cast<unsigned long>(s.originalTarget.generation)) ||
        !hostTuple(out,capacity,used,s.originalTuple) || !append(out,capacity,used,",\"original_serial_generation\":%lu,\"settings\":{\"first\":%u,\"last\":%u,\"query_ms\":%lu,\"overall_ms\":%lu,\"request_limit\":%u,\"result_limit\":%u,\"identity\":%s,\"tuples\":[",
        static_cast<unsigned long>(s.originalSerialGeneration),s.settings.first,s.settings.last,static_cast<unsigned long>(s.settings.queryMs),static_cast<unsigned long>(s.settings.overallMs),s.settings.requestLimit,s.settings.resultLimit,boolean(s.settings.identity))) return false;
    for(uint8_t i=0;i<s.settings.tupleCount;++i) if ((i && !append(out,capacity,used,",")) || !hostTuple(out,capacity,used,s.settings.tuples[i])) return false;
    if (!append(out,capacity,used,"]},\"evidence_columns\":[\"first\",\"count\",\"event\",\"raw_hex\",\"received_length\",\"qualified\",\"attempted_us\",\"earliest_us\",\"latest_us\",\"delivered_us\",\"transport_detail\",\"tx_accepted\",\"execution_unknown\",\"status\",\"detail\",\"frame_error\"],\"findings\":[")) return false;
    for(uint8_t i=0;i<s.count;++i) {
        const auto& f=s.findings[i];const auto& p=f.probe;const auto& r=f.request;
        if (r.length>sizeof(r.bytes)) return false;
        char tx[sizeof(r.bytes)*2+1];hex(r.bytes,r.length,tx,sizeof(tx));
        if ((i && !append(out,capacity,used,",")) || !append(out,capacity,used,
            "{\"profile\":\"ess_rs\",\"target\":[%lu,%u,%lu],\"operation_id\":%lu,\"serial\":[%s,%lu,%u,%u,%u],\"tx_hex\":\"%s\",\"started_us\":%llu,\"deadline_us\":%llu,\"outcome\":%u,\"confidence\":%u,\"raw_model_known\":%s,\"raw_model\":%u,\"probe\":",
            static_cast<unsigned long>(r.target.id),r.target.address,static_cast<unsigned long>(r.target.generation),static_cast<unsigned long>(r.operationId),
            boolean(r.activeSerial.known),static_cast<unsigned long>(r.activeSerial.baud),r.activeSerial.dataBits,static_cast<unsigned>(r.activeSerial.parity),r.activeSerial.stopBits,tx,
            static_cast<unsigned long long>(r.startedUs),static_cast<unsigned long long>(r.deadlineUs),static_cast<unsigned>(p.outcome),static_cast<unsigned>(p.confidence),boolean(p.rawModelKnown),p.rawModel) ||
            !discoveryEvidence(out,capacity,used,p.provenance) || !append(out,capacity,used,",\"identity_attempted\":%s,\"identity_admission\":[%lu,%llu],\"identity_known\":%s,\"identity_ambiguous\":%s,\"collision_excluded\":%s,\"identity\":[%u,%u,%u,%u],\"identity_evidence\":",
            boolean(f.identityAttempted),static_cast<unsigned long>(f.identityOperationId),static_cast<unsigned long long>(f.identityDeadlineUs),boolean(f.identityKnown),boolean(f.identityAmbiguous),boolean(f.collisionExcluded),f.identity.rawModel,f.identity.rawVersion,f.identity.rawActiveNode,f.identity.rawDip) ||
            !discoveryEvidence(out,capacity,used,f.identityEvidence) || !append(out,capacity,used,"}")) return false;
    }
    return append(out,capacity,used,"]}");
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
    if (entry) { syntax_=entry->syntax; helpCommand_=entry->name; }
    const char* capability = count > first + 2 && std::strcmp(tokens[first], "profile") == 0 &&
        std::strcmp(tokens[first + 1], "ess_rs") == 0 ? tokens[first + 2] : tokens[first];
    if (std::strcmp(capability, "jog") == 0 || std::strcmp(capability, "torque") == 0 ||
        std::strcmp(capability, "current") == 0 || std::strcmp(capability, "velocity-update") == 0) {
        error(id, capability, "unsupported"); return;
    }
    if (!entry) { error(id, "unknown", "unknown_command"); return; }
    if (entry->command == Command::DISCOVER) {
        if (!host_.discovery) { error(id,"discover","unavailable"); return; }
        DiscoveryCommand request; bool inspect=false;
        auto& settings=request.settings;
        std::size_t at=first+1; uint16_t seen=0;
        if (count==at+1 && !std::strcmp(tokens[at],"inspect")) inspect=true;
        else if (count==at+1 && !std::strcmp(tokens[at],"cancel")) request.kind=DiscoveryCommandKind::CANCEL;
        else if (count==at+1 && !std::strcmp(tokens[at],"restore")) request.kind=DiscoveryCommandKind::RESTORE;
        else if (count==at+1 && !std::strcmp(tokens[at],"finish")) request.kind=DiscoveryCommandKind::FINISH;
        else while(at<count) {
            const char* key=tokens[at++];uint16_t flag=0;uint32_t value=0;
            if (!std::strcmp(key,"profile") || !std::strcmp(key,"manufacturer")) {
                flag=1;
                if(at>=count || std::strcmp(tokens[at++],!std::strcmp(key,"profile")?"ess_rs":"stepperonline")) {error(id,"discover","unsupported_profile");return;}
            } else if (!std::strcmp(key,"addresses")) {
                flag=2;uint32_t last=0;
                if(at+2>count || !number(tokens[at++],value) || !number(tokens[at++],last) || value<1 || last<value || last>247) {error(id,"discover","invalid_addresses");return;}
                settings.first=static_cast<uint8_t>(value);settings.last=static_cast<uint8_t>(last);
            } else if (!std::strcmp(key,"tuple")) {
                HostTuple tuple;
                if(at+2>count || settings.tupleCount==DISCOVERY_MAX_TUPLES || !number(tokens[at++],tuple.baud) || !parseFormat(tokens[at++],tuple.format) || !reviewedHostTuple(tuple)) {error(id,"discover","invalid_tuple");return;}
                for(uint8_t i=0;i<settings.tupleCount;++i) if(sameTuple(tuple,settings.tuples[i])) {error(id,"discover","duplicate_tuple");return;}
                settings.tuples[settings.tupleCount++]=tuple;
            } else if (!std::strcmp(key,"identity")) {flag=4;settings.identity=true;}
            else {
                if (!std::strcmp(key,"query-ms")) flag=8;
                else if (!std::strcmp(key,"overall-ms")) flag=16;
                else if (!std::strcmp(key,"requests")) flag=32;
                else if (!std::strcmp(key,"results")) flag=64;
                else {error(id,"discover","invalid_arguments");return;}
                const uint32_t maximum=flag==8?5000:flag==16?60000:flag==32?256:8;
                if(at>=count || !number(tokens[at++],value) || !value || value>maximum) {error(id,"discover","invalid_budget");return;}
                if(flag==8) settings.queryMs=value; else if(flag==16)settings.overallMs=value;
                else if(flag==32)settings.requestLimit=static_cast<uint16_t>(value);else settings.resultLimit=static_cast<uint8_t>(value);
            }
            if(flag && (seen&flag)) {error(id,"discover","duplicate_option");return;}
            seen|=flag;
        }
        if(outputPending() && request.kind!=DiscoveryCommandKind::CANCEL) {++inputDropped_;return;}
        DiscoveryView view;const auto result=host_.discovery(host_.context,inspect?nullptr:&request,view);
        if(outputPending()) {++inputDropped_;return;}
        std::size_t used=0;
        if(append(output_,sizeof(output_),used,"{\"type\":\"reply\",\"profile\":\"ess_rs\",\"id\":%lu,\"command\":\"discover\",\"ok\":%s,\"result\":\"%s\",\"action\":%d,\"scan\":",static_cast<unsigned long>(id),boolean(result==Action::OK),actionName(result),inspect?-1:static_cast<int>(request.kind)) &&
            discoveryScan(output_,sizeof(output_),used,view.scan) && append(output_,sizeof(output_),used,"}")) emit();
        else error(id,"discover","output_capacity");
        return;
    }
    if (entry->command == Command::PROFILE && count >= first + 2 && !std::strcmp(tokens[first+1], "list")) {
        if (count != first + 2) { error(id,"profile-list","invalid_arguments"); return; }
        if (outputPending()) { ++inputDropped_; return; }
        std::size_t used = 0;
        bool fits = append(output_,sizeof(output_),used,
            "{\"type\":\"reply\",\"profile\":\"ess_rs\",\"id\":%lu,\"command\":\"profile-list\",\"ok\":true,\"bus_traffic\":false,\"profiles\":[",static_cast<unsigned long>(id));
        for (std::size_t i = 0; fits && i < MotorControlRS::discoveryProfileCount(); ++i) {
            MotorControlRS::DiscoveryProfile profile;
            MotorControlRS::DiscoveryCapabilities caps;
            if (!MotorControlRS::getDiscoveryProfile(i,profile).isOk() || !MotorControlRS::getDiscoveryCapabilities(profile.profile,caps).isOk()) {
                error(id,"profile-list","inventory_error"); return;
            }
            fits = (!i || append(output_,sizeof(output_),used,",")) && append(output_,sizeof(output_),used,
                "{\"manufacturer\":\"%s\",\"manufacturer_name\":\"%s\",\"profile\":\"%s\",\"name\":\"%s\",\"probe\":%s,\"identity\":%s,\"nonchanging\":%s,\"exact_model\":%s,\"firmware\":%s,\"minimum_address\":%u,\"maximum_address\":%u,\"probe_first\":%u,\"probe_count\":%u,\"identity_first\":%u,\"identity_count\":%u}",
                profile.manufacturerId,profile.manufacturerName,profile.profileId,profile.profileName,
                boolean(caps.probe),boolean(caps.identity),boolean(caps.nonChanging),boolean(caps.exactModel),boolean(caps.firmware),
                caps.minimumAddress,caps.maximumAddress,caps.probeFirst,caps.probeCount,caps.identityFirst,caps.identityCount);
        }
        if (fits && append(output_,sizeof(output_),used,"]}")) emit();
        else error(id,"profile-list","output_capacity");
        return;
    }
    if (entry->command == Command::DEBUG) {
        if (!host_.debug || !host_.snapshot) { error(id,"debug","unavailable"); return; }
        DebugMode mode=DebugMode::OFF; bool change=count==first+2;
        if (count<first+1 || count>first+2) { error(id,"debug","invalid_arguments"); return; }
        if (change) {
            if (!std::strcmp(tokens[first+1],"raw")) mode=DebugMode::RAW;
            else if (!std::strcmp(tokens[first+1],"decoded")) mode=DebugMode::DECODED;
            else if (std::strcmp(tokens[first+1],"off")) { error(id,"debug","invalid_arguments"); return; }
        }
        if (outputPending() && (!change || mode!=DebugMode::OFF)) { ++inputDropped_; return; }
        DebugSnapshot view;
        const auto result=host_.debug(host_.context,change?&mode:nullptr,view);
        if (change && result==Action::OK) trafficFormat_=outputFormat_;
        // Disabling remains possible under pressure; an existing ordinary reply
        // keeps its ownership and the diagnostic reply may be dropped.
        if (outputPending()) { ++inputDropped_; return; }
        Snapshot data;
        if (host_.snapshot) host_.snapshot(host_.context,data);
        std::size_t used=0;
        if (append(output_,sizeof(output_),used,
            "{\"type\":\"reply\",\"profile\":\"ess_rs\",\"id\":%lu,\"command\":\"debug\",\"ok\":%s,\"result\":\"%s\",\"mode\":\"%s\",\"observed\":%llu,\"emitted\":%llu,\"dropped\":%llu,\"missed\":%llu,\"skipped\":%llu,\"cursor\":%llu,\"overwritten\":%lu,\"capture_dropped\":%lu,\"retained\":%u,\"capacity\":%u,\"owner\":{\"phase\":\"%s\",\"busy\":%s,\"recovery_required\":%s,\"pending\":%u,\"retained\":%u},\"capture\":{\"mode\":\"%s\",\"faults\":%lu,\"max_poll_gap_us\":%llu},\"memory\":{\"valid\":%s,\"stack_free_bytes\":%lu,\"internal_free\":%lu,\"psram_free\":%lu}}",
            static_cast<unsigned long>(id),boolean(result==Action::OK),actionName(result),
            view.mode==DebugMode::RAW?"raw":view.mode==DebugMode::DECODED?"decoded":"off",
            static_cast<unsigned long long>(view.observed),static_cast<unsigned long long>(view.emitted),
            static_cast<unsigned long long>(view.dropped),static_cast<unsigned long long>(view.missed),static_cast<unsigned long long>(view.skipped),static_cast<unsigned long long>(view.cursor),
            static_cast<unsigned long>(view.overwritten),static_cast<unsigned long>(view.captureDropped),static_cast<unsigned>(view.retained),static_cast<unsigned>(view.capacity),
            Rtu::phaseName(data.phase),boolean(data.busy),boolean(data.recoveryRequired),static_cast<unsigned>(data.pending),static_cast<unsigned>(data.retained),
            data.timerCapture?"timer":"poll",static_cast<unsigned long>(data.captureFaults),static_cast<unsigned long long>(data.maxPollGapUs),
            boolean(data.memoryValid),static_cast<unsigned long>(data.stackFreeBytes),static_cast<unsigned long>(data.internalFree),static_cast<unsigned long>(data.psramFree))) emit();
        else error(id,"debug","output_capacity");
        return;
    }
    if (entry->command == Command::PROFILE && count > first + 2 &&
        !std::strcmp(tokens[first + 1], "ess_rs") && !std::strcmp(tokens[first + 2], "motion-profile")) {
        entry = find("motion-profile"); first += 2;
    }
    if (entry->command == Command::MOTION_PROFILE) {
        if (outputPending()) { ++inputDropped_; return; }
        if (!host_.motionProfile) { error(id, "motion-profile", "unavailable"); return; }
        if (count != first + 2) { error(id, "motion-profile", "invalid_arguments"); return; }
        MotionProfileCommand command;
        if (!std::strcmp(tokens[first + 1], "inspect")) command = MotionProfileCommand::INSPECT;
        else if (!std::strcmp(tokens[first + 1], "read")) command = MotionProfileCommand::SNAPSHOT;
        else if (!std::strcmp(tokens[first + 1], "restore")) command = MotionProfileCommand::RESTORE;
        else if (!std::strcmp(tokens[first + 1], "forget")) command = MotionProfileCommand::FORGET;
        else { error(id, "motion-profile", "invalid_arguments"); return; }
        MotionProfileView v;
        const Action result = host_.motionProfile(host_.context, command, v);
        char tx[39], rx[129], write[17], writeTx[39];
        hex(v.tx,v.txLength,tx,sizeof(tx)); hex(v.rx,v.rxLength,rx,sizeof(rx));
        hex(v.writeReply,v.writeReplyLength,write,sizeof(write)); hex(v.writeTx,v.writeTxLength,writeTx,sizeof(writeTx));
        std::size_t used=0;
        if (append(output_,sizeof(output_),used,
            "{\"type\":\"reply\",\"profile\":\"ess_rs\",\"id\":%lu,\"command\":\"motion-profile\",\"ok\":%s,\"result\":\"%s\",\"pending\":%s,\"saved\":%s,\"restored\":%s,\"session_ok\":%s,\"phase\":%u,\"address\":%u,\"configuration_generation\":%lu,\"serial_generation\":%lu,\"original\":[%u,%u,%u,%u,%u,%u],\"current\":[%u,%u,%u,%u,%u,%u],\"tx_hex\":\"%s\",\"rx_hex\":\"%s\",\"write_reply_hex\":\"%s\",\"tx_accepted\":%llu,\"closure_qualified\":%s,\"closure_earliest_us\":%llu,\"closure_latest_us\":%llu,\"execution_unknown\":%s,\"error\":\"%s\"}",
            static_cast<unsigned long>(id),boolean(result==Action::OK),actionName(result),boolean(v.pending),boolean(v.saved),boolean(v.restored),boolean(v.ok),static_cast<unsigned>(v.phase),v.address,
            static_cast<unsigned long>(v.generation),static_cast<unsigned long>(v.serialGeneration),
            v.original[0],v.original[1],v.original[2],v.original[3],v.original[4],v.original[5],
            v.current[0],v.current[1],v.current[2],v.current[3],v.current[4],v.current[5],tx,rx,write,
            static_cast<unsigned long long>(v.txAccepted),boolean(v.closureQualified),static_cast<unsigned long long>(v.closureEarliestUs),static_cast<unsigned long long>(v.closureLatestUs),boolean(v.executionUnknown),v.error)) {
            --used;
            if (append(output_,sizeof(output_),used,",\"request\":\"%s\",\"tx_complete\":%s,\"write_tx_complete\":%s,\"deadline_us\":%llu,\"delivered_us\":%llu,\"write_tx_hex\":\"%s\",\"write_tx_accepted\":%llu,\"write_closure_qualified\":%s,\"write_closure_earliest_us\":%llu,\"write_closure_latest_us\":%llu,\"write_delivered_us\":%llu,\"write_execution_unknown\":%s}",
                tokens[first+1],boolean(v.txComplete),boolean(v.writeTxComplete),static_cast<unsigned long long>(v.deadlineUs),static_cast<unsigned long long>(v.deliveredUs),writeTx,static_cast<unsigned long long>(v.writeTxAccepted),boolean(v.writeQualified),static_cast<unsigned long long>(v.writeEarliestUs),static_cast<unsigned long long>(v.writeLatestUs),static_cast<unsigned long long>(v.writeDeliveredUs),boolean(v.writeExecutionUnknown))) {
                --used;
                if (append(output_,sizeof(output_),used,",\"restore_unsettled\":%s,\"write_deadline_us\":%llu,\"write_configuration_generation\":%lu,\"write_serial_generation\":%lu,\"write_binding_generation\":%lu}",
                    boolean(v.restoreUnsettled),static_cast<unsigned long long>(v.writeDeadlineUs),static_cast<unsigned long>(v.writeConfigurationGeneration),
                    static_cast<unsigned long>(v.writeSerialGeneration),static_cast<unsigned long>(v.writeBindingGeneration))) emit();
                else error(id,"motion-profile","output_capacity");
            }
            else error(id,"motion-profile","output_capacity");
        }
        else error(id,"motion-profile","output_capacity");
        return;
    }
    if (entry->command == Command::HOST) {
        if (outputPending()) { ++inputDropped_; return; }
        if (!host_.hostSerial) { error(id, "host", "unavailable"); return; }
        const std::size_t args = first + 1;
        HostSnapshot state;
        const Action queried = host_.hostSerial(host_.context, nullptr, state);
        if (queried != Action::OK) { error(id, "host", actionName(queried)); return; }
        HostRequest request; request.tuple = state.active;
        bool changing = false;
        if (count == args) {}
        else if (count == args + 1 && !std::strcmp(tokens[args], "caps")) {}
        else if (count == args + 1 && !std::strcmp(tokens[args], "restore")) { changing = request.restore = true; }
        else if (count == args + 2 && !std::strcmp(tokens[args], "baud")) {
            if (!state.activeKnown) { error(id, "host", "active_tuple_unknown"); return; }
            if (!number(tokens[args + 1], request.tuple.baud)) { error(id, "host", "invalid_tuple"); return; }
            changing = true;
        } else if (count == args + 2 && !std::strcmp(tokens[args], "fmt")) {
            if (!state.activeKnown) { error(id, "host", "active_tuple_unknown"); return; }
            if (!parseFormat(tokens[args + 1], request.tuple.format)) { error(id, "host", "invalid_tuple"); return; }
            changing = true;
        } else if (count == args + 3 && !std::strcmp(tokens[args], "set")) {
            if (!number(tokens[args + 1], request.tuple.baud) || !parseFormat(tokens[args + 2], request.tuple.format)) {
                error(id, "host", "invalid_tuple"); return;
            }
            changing = true;
        } else { error(id, "host", "invalid_arguments"); return; }
        if (changing && !supports(state, request.restore ? state.original : request.tuple)) { error(id, "host", "unsupported"); return; }
        const Action result = changing ? host_.hostSerial(host_.context, &request, state) : Action::OK;
        std::size_t used = 0;
        const bool fits = append(output_, sizeof(output_), used,
            "{\"type\":\"reply\",\"profile\":\"ess_rs\",\"id\":%lu,\"command\":\"host\",\"ok\":%s,\"result\":\"%s\"",
            static_cast<unsigned long>(id), boolean(result == Action::OK), result == Action::OK ? "done" : actionName(result)) &&
            hostState(output_, sizeof(output_), used, state);
        if (!fits) { error(id, "host", "output_full"); return; }
        emit(); return;
    }
    const bool nativePersistence = entry->command == Command::PROFILE && count > first + 2 &&
        !std::strcmp(tokens[first+1],"ess_rs") && !std::strcmp(tokens[first+2],"persistence");
    if (entry->command == Command::PERSISTENCE || nativePersistence) {
        if (outputPending()) { ++inputDropped_; return; }
        if (!host_.persistence) { error(id,"persistence","unavailable"); return; }
        const std::size_t args=first+(nativePersistence?3:1);
        PersistenceCommand request; bool inspect=false,plan=false;
        if(count==args || (count==args+1 && !std::strcmp(tokens[args],"inspect"))) inspect=true;
        else if(count==args+1 && !std::strcmp(tokens[args],"snapshot")) request.kind=PersistenceCommandKind::SNAPSHOT;
        else if(count==args+1 && !std::strcmp(tokens[args],"verify")) request.kind=PersistenceCommandKind::VERIFY;
        else if(count==args+1 && !std::strcmp(tokens[args],"finish")) request.kind=PersistenceCommandKind::FINISH;
        else if(count==args+2 && !std::strcmp(tokens[args],"host") && !std::strcmp(tokens[args+1],"before")) request.kind=PersistenceCommandKind::SELECT_BEFORE;
        else if(count==args+2 && (!std::strcmp(tokens[args],"plan") || !std::strcmp(tokens[args],"begin"))) {
            plan=!std::strcmp(tokens[args],"plan");request.kind=plan?PersistenceCommandKind::PREVIEW:PersistenceCommandKind::BEGIN;
            if(!std::strcmp(tokens[args+1],"save")) request.request=Ess::PersistenceKind::SAVE;
            else if(!std::strcmp(tokens[args+1],"factory-restore")) request.request=Ess::PersistenceKind::FACTORY_RESTORE;
            else { error(id,"persistence","invalid_kind");return; }
        } else { error(id,"persistence","invalid_arguments");return; }
        PersistenceView view;const Action result=host_.persistence(host_.context,inspect?nullptr:&request,view);
        std::size_t used=0;
        bool fits=append(output_,sizeof(output_),used,
            "{\"type\":\"reply\",\"profile\":\"ess_rs\",\"id\":%lu,\"command\":\"persistence\",\"ok\":%s,\"result\":\"%s\",\"action\":%d,\"pending\":%s,\"owned\":%s,\"route_ready\":%s,\"snapshot_known\":%s,\"snapshot_failed\":%s,\"verification_attempts\":%u,\"parent_session\":%s,\"finished\":%s,\"invocations\":%u,\"invocation_limit\":2,\"restart_performed\":false,\"write_replayed\":false,\"plan\":",
            static_cast<unsigned long>(id),boolean(result==Action::OK),result==Action::OK?"done":actionName(result),inspect?-1:static_cast<int>(request.kind),
            boolean(view.pending),boolean(view.owned),boolean(view.routeReady),boolean(view.snapshotKnown),boolean(view.snapshotFailed),view.verificationAttempts,boolean(view.parentSession),boolean(view.finished),view.invocations);
        if (!inspect && (plan || request.kind==PersistenceCommandKind::BEGIN)) {
            fits=fits && append(output_,sizeof(output_),used,
                "{\"kind\":\"%s\",\"register\":45,\"value\":%u,\"writes\":1,\"scope\":\"all_parameters\",\"stopped_required\":true,\"completion\":\"unresolved\",\"durability\":\"unverified\",\"restart\":\"required_for_proof\",\"backup\":\"partial_standard_config\"}",
                persistenceKind(request.request),request.request==Ess::PersistenceKind::SAVE?66:65);
        } else fits=fits && append(output_,sizeof(output_),used,"null");
        fits=fits && append(output_,sizeof(output_),used,",\"baseline\":");
        if(view.baseline) fits=fits && persistenceSnapshot(output_,sizeof(output_),used,view.baseline->beforeConfig,view.baseline->beforeIdentity,view.baseline->stationary,view.baseline->beforeSerial,view.baseline->configurationGeneration);
        else fits=fits && append(output_,sizeof(output_),used,"null");
        fits=fits && append(output_,sizeof(output_),used,",\"context\":") && persistenceContext(output_,sizeof(output_),used,view.context) &&
            append(output_,sizeof(output_),used,",\"host\":{\"active\":") && hostTuple(output_,sizeof(output_),used,view.host.active) &&
            append(output_,sizeof(output_),used,",\"active_known\":%s,\"blocked\":%s,\"serial_generation\":%lu,\"actual_baud\":%lu,\"failure\":\"%s\"}}",
                boolean(view.host.activeKnown),boolean(view.host.blocked),static_cast<unsigned long>(view.host.generation),static_cast<unsigned long>(view.host.actualBaud),hostFailure(view.host.failure));
        if(!fits) {error(id,"persistence","output_full");return;}
        emit();return;
    }
    const bool nativeCommunication = entry->command == Command::PROFILE && count > first + 2 &&
        !std::strcmp(tokens[first+1],"ess_rs") && !std::strcmp(tokens[first+2],"communication");
    if (entry->command == Command::COMMUNICATION || nativeCommunication) {
        if (outputPending()) { ++inputDropped_; return; }
        if (!host_.communication) { error(id,"communication","unavailable"); return; }
        const std::size_t args=first+(nativeCommunication?3:1);
        CommunicationCommand request; bool inspect=false, plan=false;
        uint16_t reg=0,value=0;
        if (count==args || (count==args+1 && !std::strcmp(tokens[args],"inspect"))) inspect=true;
        else if (count==args+1 && !std::strcmp(tokens[args],"finish")) request.kind=CommunicationCommandKind::FINISH;
        else if (count==args+2 && (!std::strcmp(tokens[args],"host") || !std::strcmp(tokens[args],"confirm"))) {
            const bool before=!std::strcmp(tokens[args+1],"before");
            if (!before && std::strcmp(tokens[args+1],"requested")) { error(id,"communication","invalid_candidate"); return; }
            request.kind=!std::strcmp(tokens[args],"host") ? (before?CommunicationCommandKind::SELECT_BEFORE:CommunicationCommandKind::SELECT_REQUESTED) :
                (before?CommunicationCommandKind::CONFIRM_BEFORE:CommunicationCommandKind::CONFIRM_REQUESTED);
        } else if ((count==args+3 || count==args+4) && (!std::strcmp(tokens[args],"plan") || !std::strcmp(tokens[args],"begin"))) {
            plan=!std::strcmp(tokens[args],"plan"); request.kind=plan?CommunicationCommandKind::PREVIEW:CommunicationCommandKind::BEGIN;
            Snapshot snapshot; if (host_.snapshot) host_.snapshot(host_.context,snapshot); request.address=snapshot.address;
            uint32_t numberValue=0;
            if (!std::strcmp(tokens[args+1],"address")) {
                if (!number(tokens[args+2],numberValue) || numberValue<1 || numberValue>247) { error(id,"communication","invalid_address"); return; }
                request.request.field=Ess::CommunicationField::ADDRESS; request.request.address=static_cast<uint16_t>(numberValue); reg=0x0013; value=request.request.address;
            } else if (!std::strcmp(tokens[args+1],"baud")) {
                if (!number(tokens[args+2],numberValue) || (numberValue!=115200 && numberValue!=38400 && numberValue!=19200 && numberValue!=9600)) { error(id,"communication","invalid_baud"); return; }
                value=numberValue==115200?0:numberValue==38400?1:numberValue==19200?2:3;
                request.request.field=Ess::CommunicationField::BAUD; request.request.baud=static_cast<Ess::BaudRateCode>(value); reg=0x0014;
            } else if (!std::strcmp(tokens[args+1],"format")) {
                HostFormat format;
                if (!parseFormat(tokens[args+2],format)) { error(id,"communication","invalid_format"); return; }
                value=static_cast<uint16_t>(format); request.request.field=Ess::CommunicationField::FORMAT;
                request.request.format=static_cast<Ess::SerialFormatCode>(value); reg=0x0015;
            } else { error(id,"communication","invalid_field"); return; }
            if (count==args+4) {
                if (!number(tokens[args+3],numberValue) || numberValue<1 || numberValue>247) { error(id,"communication","invalid_address"); return; }
                request.address=static_cast<uint8_t>(numberValue);
            }
        } else { error(id,"communication","invalid_arguments"); return; }
        CommunicationView view;
        const Action result=host_.communication(host_.context,inspect?nullptr:&request,view);
        std::size_t used=0;
        bool fits=append(output_,sizeof(output_),used,
            "{\"type\":\"reply\",\"profile\":\"ess_rs\",\"id\":%lu,\"command\":\"communication\",\"ok\":%s,\"result\":\"%s\",\"action\":%d,\"pending\":%s,\"owned\":%s,\"route_ready\":%s,\"save_sent\":false,\"restart_performed\":false,\"write_replayed\":false,\"plan\":",
            static_cast<unsigned long>(id),boolean(result==Action::OK),result==Action::OK?"done":actionName(result),inspect?-1:static_cast<int>(request.kind),
            boolean(view.pending),boolean(view.owned),boolean(view.routeReady));
        if (plan || request.kind==CommunicationCommandKind::BEGIN) {
            fits=fits && append(output_,sizeof(output_),used,
                "{\"field\":\"%s\",\"address\":%u,\"register\":%u,\"value\":%u,\"writes\":1,\"save\":\"%s\",\"restart\":\"%s\",\"activation\":\"unresolved\",\"prerequisites\":\"fresh_checked_config_exact_candidate_stationary_effects_route_back%s\"}",
                communicationField(request.request.field),request.address,reg,value,reg==0x0013?"required":"unresolved",reg==0x0013?"unresolved":"required",reg==0x0013?"_address_dip_off":"");
        } else fits=fits && append(output_,sizeof(output_),used,"null");
        fits=fits && append(output_,sizeof(output_),used,",\"context\":") && communicationContext(output_,sizeof(output_),used,view.context) &&
            append(output_,sizeof(output_),used,",\"host\":{\"active\":") && hostTuple(output_,sizeof(output_),used,view.host.active) &&
            append(output_,sizeof(output_),used,",\"active_known\":%s,\"blocked\":%s,\"serial_generation\":%lu,\"actual_baud\":%lu,\"failure\":\"%s\"}}",
                boolean(view.host.activeKnown),boolean(view.host.blocked),static_cast<unsigned long>(view.host.generation),static_cast<unsigned long>(view.host.actualBaud),hostFailure(view.host.failure));
        if (!fits) { error(id,"communication","output_full"); return; }
        emit(); return;
    }
    const bool nativeHome = entry->command == Command::PROFILE && count > first + 2 &&
        std::strcmp(tokens[first + 1], "ess_rs") == 0 && std::strcmp(tokens[first + 2], "home") == 0;
    if (entry->command == Command::HOME || nativeHome) {
        if (outputPending()) { ++inputDropped_; return; }
        const std::size_t args = first + (nativeHome ? 3 : 1);
        if (count == args + 1 && std::strcmp(tokens[args], "methods") == 0) {
            std::size_t used = 0;
            bool fits = append(output_, sizeof(output_), used,
                "{\"type\":\"reply\",\"profile\":\"ess_rs\",\"id\":%lu,\"command\":\"home\",\"ok\":true,\"methods\":[", static_cast<unsigned long>(id));
            for (uint8_t i = 0; fits && i < Ess::HOME_METHOD_COUNT; ++i) {
                const auto* method = Ess::homeMethodAt(i);
                fits = method && append(output_, sizeof(output_), used,
                    "%s{\"method\":%d,\"support\":\"%s\",\"inputs\":%u,\"index\":%s,\"moves\":%s,\"reason\":%u}",
                    i ? "," : "", static_cast<int>(method->method),
                    method->support == Ess::HomeSupport::IMPLEMENTED ? "implemented" : method->support == Ess::HomeSupport::UNIMPLEMENTED ? "unimplemented" : "unresolved",
                    method->requiredInputs, boolean(method->requiresIndex), boolean(method->moves),
                    method->support == Ess::HomeSupport::UNIMPLEMENTED ? 0U : static_cast<int>(method->method) < 0 ? 1U :
                    static_cast<int>(method->method) == 18 ? 2U : static_cast<unsigned>(method->method) - 30U);
            }
            fits = fits && append(output_, sizeof(output_), used, "],\"reasons\":{\"0\":\"switch_edges_and_return_pending\",\"1\":\"collision_encoding_and_parameter_conflicts\",\"2\":\"limit_prose_diagram_conflict\",\"3\":\"negative_internal_index_search\",\"4\":\"positive_internal_index_search\",\"5\":\"current_position_origin\"},\"physical_qualified\":false,\"offset\":\"zero_only\",\"auxiliary\":7}");
            if (!fits) { error(id, "home", "output_full"); return; }
            emit(); return;
        }
        if (!host_.startHome || !host_.snapshot || !host_.axis) { error(id, "home", "unavailable"); return; }
        if ((count != args + 5 && count != args + 6) || std::strcmp(tokens[args + 4], "zero")) {
            error(id, "home", "invalid_arguments"); return;
        }
        Core::Rational method;
        const char* digit = tokens[args]; if (*digit == '-') ++digit;
        bool integer = *digit != '\0';
        for (const char* p = digit; *p; ++p) integer = integer && *p >= '0' && *p <= '9';
        uint32_t search = 0, returning = 0, ramp = 0, address = 0;
        if (!integer || !Core::parseExactNumber(tokens[args], method) || method.denominator != 1 ||
            method.numerator < -32768 || method.numerator > 32767 ||
            !number(tokens[args + 1], search) || search < 5 || search > 3000 ||
            !number(tokens[args + 2], returning) || returning < 5 || returning > 300 ||
            !number(tokens[args + 3], ramp) || ramp < 30 || ramp > 2000) {
            error(id, "home", "invalid_arguments"); return;
        }
        if (count == args + 6) {
            if (!number(tokens[args + 5], address) || address < 1 || address > 247) { error(id, "home", "invalid_address"); return; }
        } else { Snapshot snapshot; host_.snapshot(host_.context, snapshot); address = snapshot.address; }
        if (address < 1 || address > 247) { error(id, "home", "invalid_address"); return; }
        AxisCommand query; AxisView axis;
        query.kind = AxisCommandKind::QUERY;
        if (!host_.axis(host_.context, query, axis)) { error(id, "home", "unavailable"); return; }
        Ess::HomeRequest request;
        request.method = static_cast<Ess::HomingMethod>(method.numerator);
        request.searchSpeed = static_cast<uint16_t>(search); request.returnSpeed = static_cast<uint16_t>(returning);
        request.rampTime = static_cast<uint16_t>(ramp); request.configurationGeneration = axis.configuration.generation;
        bool available = false;
        for (std::size_t i = 0; i < OUTSTANDING_CAPACITY - 1; ++i) available = available || !outstanding_[i].commandId;
        uint32_t operationId = 0;
        const Action result = available ? host_.startHome(host_.context, id, static_cast<uint8_t>(address), request, operationId) : Action::BUSY;
        if (result == Action::OK) track(id, operationId);
        action(id, "home", result, static_cast<uint8_t>(address), result == Action::OK ? operationId : 0);
        return;
    }
    const bool nativeDriver = entry->command == Command::DRIVER || (entry->command == Command::PROFILE && count > first + 2 &&
        std::strcmp(tokens[first + 1], "ess_rs") == 0 && std::strcmp(tokens[first + 2], "driver") == 0);
    const bool nativeIo = entry->command == Command::IO || (entry->command == Command::PROFILE && count > first + 2 &&
        std::strcmp(tokens[first + 1], "ess_rs") == 0 && std::strcmp(tokens[first + 2], "io") == 0);
    const bool nativeSegment = entry->command == Command::SEGMENT || (entry->command == Command::PROFILE && count > first + 2 &&
        std::strcmp(tokens[first + 1], "ess_rs") == 0 && std::strcmp(tokens[first + 2], "segment") == 0);
    const bool nativeControl = entry->command == Command::CONTROL || (entry->command == Command::PROFILE && count > first + 2 &&
        std::strcmp(tokens[first + 1], "ess_rs") == 0 && std::strcmp(tokens[first + 2], "control") == 0);
    const bool nativeTuning = entry->command == Command::TUNING || (entry->command == Command::PROFILE && count > first + 2 &&
        std::strcmp(tokens[first + 1], "ess_rs") == 0 && std::strcmp(tokens[first + 2], "tuning") == 0);
    if (nativeDriver || nativeIo || nativeSegment || nativeControl || nativeTuning) {
        const char* command = nativeTuning ? "tuning" : nativeControl ? "control" :
            nativeSegment ? "segment" : nativeIo ? "io" : "driver";
        if (outputPending()) { ++inputDropped_; return; }
        if (!host_.startDriver || !host_.snapshot) { error(id, command, "unavailable"); return; }
        std::size_t next = first + (entry->command == Command::PROFILE ? 3 : 1);
        if (next >= count) { error(id, command, "invalid_arguments"); return; }
        Ess::DriverRequest request;
        request.group = nativeControl ? Ess::DriverGroup::CONTROL_SETTINGS : nativeIo ? Ess::DriverGroup::IO : Ess::DriverGroup::DRIVE;
        if (nativeTuning) {
            if (count < next+2 || !tuningGroup(tokens[next],request.group)) { error(id,command,"invalid_tuning_group");return; }
            ++next;
        }
        if (nativeSegment) {
            if (count < next + 3) { error(id, command, "invalid_arguments"); return; }
            if (!std::strcmp(tokens[next], "position")) request.group = Ess::DriverGroup::POSITION_SEGMENT;
            else if (!std::strcmp(tokens[next], "speed")) request.group = Ess::DriverGroup::SPEED_SEGMENT;
            else if (!std::strcmp(tokens[next], "start")) request.group = Ess::DriverGroup::SEGMENT_START_SPEED;
            else { error(id, command, "invalid_record_kind"); return; }
            uint32_t index = 0;
            if (!number(tokens[next + 1], index) || index < 1 || index > 16) { error(id, command, "invalid_index"); return; }
            request.segmentIndex = static_cast<uint8_t>(index); next += 2;
        }
        const bool reading = std::strcmp(tokens[next], "read") == 0;
        if (!reading && std::strcmp(tokens[next], "set") != 0) { error(id, command, "invalid_arguments"); return; }
        ++next;
        // An odd trailing token is the optional target; every remaining token
        // must belong to a complete field/value pair before host admission.
        const bool hasAddress = reading ? count == next + 1 : (count - next) % 2 != 0;
        const std::size_t end = count - (hasAddress ? 1 : 0);
        if ((reading && end != next) || (!reading && end == next)) { error(id, command, "invalid_arguments"); return; }
        uint32_t address = 0;
        if (hasAddress && (!number(tokens[end], address) || address < 1 || address > 247)) { error(id, command, "invalid_address"); return; }
        const char* driveFields[] = {"direction", "subdivision", "word-order", "soft-limit", "over-limit", "interruption", "position-mode", "positive-limit", "negative-limit"};
        const char* ioFields[] = {"input-polarity", "x0", "x1", "x2", "x3", "output-polarity", "y0", "y1", "custom"};
        const char* segmentFields[] = {"speed", "acceleration", "deceleration", "target"};
        const char* startFields[] = {"value"};
        const char* controlFields[] = {"algorithm", "encoder-resolution", "maximum-effective-current", "closed-maximum-current", "closed-base-current", "open-maximum-current", "lock-current", "lock-delay"};
        const bool startRecord = request.group == Ess::DriverGroup::SEGMENT_START_SPEED;
        const char* const* fields = nativeControl ? controlFields : nativeSegment ? (startRecord ? startFields : segmentFields) : nativeIo ? ioFields : driveFields;
        const std::size_t fieldCount = nativeTuning ? Ess::tuningFieldCount(request.group) : nativeControl ? 8 : nativeSegment ? (startRecord ? 1 : request.group == Ess::DriverGroup::POSITION_SEGMENT ? 4 : 3) : 9;
        for (; next < end; next += 2) {
            std::size_t field = 0;
            while (field < fieldCount && std::strcmp(tokens[next],nativeTuning?tuningField(request.group,field):fields[field]) != 0) ++field;
            if (field == fieldCount) { error(id, command, "unknown_field"); return; }
            const uint32_t mask = nativeTuning ? 1UL << field : nativeControl ? 1UL << (23 + field) : nativeSegment ? 1UL << (startRecord ? 21 : field == 3 ? 22 : 18 + field) : 1UL << (field + (nativeIo ? 9 : 0));
            if (request.fields & mask) { error(id, command, "duplicate_field"); return; }
            const bool noFunction = nativeIo && std::strcmp(tokens[next + 1], "none") == 0 &&
                ((field >= 1 && field <= 4) || field == 6 || field == 7);
            const char* numberText = noFunction ? "0" : tokens[next + 1];
            if (nativeControl && field == 0) {
                if (!std::strcmp(numberText, "open-loop")) numberText = "1";
                else if (!std::strcmp(numberText, "algorithm-1")) numberText = "2";
            }
            Core::Rational value;
            const char* digit = numberText; if (*digit == '-') ++digit;
            if (!*digit) { error(id, command, "invalid_integer"); return; }
            for (const char* p = digit; *p; ++p) if (*p < '0' || *p > '9') { error(id, command, "invalid_integer"); return; }
            if (!Core::parseExactNumber(numberText, value) || value.denominator != 1 ||
                (!nativeSegment && (nativeTuning || nativeControl || nativeIo || field < 7) && (value.numerator < 0 || value.numerator > 65535))) { error(id, command, "invalid_integer"); return; }
            if (nativeSegment) {
                if (field != 3 && (value.numerator < -2147483648LL || value.numerator > 2147483647LL)) { error(id, command, "invalid_integer"); return; }
                if (!startRecord && (field == 1 || field == 2) && (value.numerator < 0 || value.numerator > 65535)) { error(id, command, "invalid_integer"); return; }
                request.fields |= mask;
                if (startRecord) request.segmentStartSpeed = static_cast<int32_t>(value.numerator);
                else if (field == 0) request.segmentSpeed = static_cast<int32_t>(value.numerator);
                else if (field == 1) request.segmentAcceleration = static_cast<uint16_t>(value.numerator);
                else if (field == 2) request.segmentDeceleration = static_cast<uint16_t>(value.numerator);
                else request.segmentPulseTarget = value.numerator;
                continue;
            }
            const uint16_t word = static_cast<uint16_t>(value.numerator);
            if (nativeTuning) {
                if (!Ess::prepareTuningValue(request,Ess::tuningParameter(request.group,static_cast<uint8_t>(field)),word)) { error(id,command,"invalid_value");return; }
                continue;
            }
            request.fields |= mask;
            if (nativeControl) {
                switch (field) {
                case 0: request.controlAlgorithm = static_cast<Ess::ControlAlgorithm>(word); break;
                case 1: request.encoderResolution = word; break;
                case 2: request.maximumEffectiveCurrentMa = word; break;
                case 3: request.closedMaximumPercent = word; break;
                case 4: request.closedBasePercent = word; break;
                case 5: request.openMaximumPercent = word; break;
                case 6: request.lockPercent = word; break;
                case 7: request.lockDelayMs = word; break;
                }
                continue;
            }
            if (nativeIo) {
                Core::Status checked;
                if (field >= 1 && field <= 4) checked = Ess::prepareInputFunction(request, static_cast<uint8_t>(field - 1), static_cast<Ess::InputFunction>(word));
                else if (field == 6 || field == 7) checked = Ess::prepareOutputFunction(request, static_cast<uint8_t>(field - 6), static_cast<Ess::OutputFunction>(word));
                else if (field == 0) request.inputPolarity = word;
                else if (field == 5) request.outputPolarity = word;
                else request.customOutput = word;
                if (!checked) { error(id, command, driverAdmissionStatus(checked) == Action::UNRESOLVED ? "unresolved" : "invalid_value"); return; }
                continue;
            }
            switch (field) {
            case 0: request.direction = static_cast<Ess::DefaultDirection>(word); break;
            case 1: request.subdivision = word; break;
            case 2: request.wordOrder = static_cast<Ess::WordOrder>(word); break;
            case 3: request.softLimitEnable = static_cast<Ess::SoftLimitEnable>(word); break;
            case 4: request.overLimitStop = static_cast<Ess::OverLimitStop>(word); break;
            case 5: request.interruption = static_cast<Ess::PvTriggerMode>(word); break;
            case 6: request.positionMode = static_cast<Ess::PositionMode>(word); break;
            case 7: request.positiveLimit = value.numerator; break;
            case 8: request.negativeLimit = value.numerator; break;
            }
        }
        if (!hasAddress) { Snapshot snapshot; host_.snapshot(host_.context, snapshot); address = snapshot.address; }
        if (address < 1 || address > 247) { error(id, command, "invalid_address"); return; }
        bool available = false;
        for (std::size_t i = 0; i < OUTSTANDING_CAPACITY - 1; ++i) available = available || !outstanding_[i].commandId;
        uint32_t operationId = 0;
        const Action result = available ? host_.startDriver(host_.context, id, static_cast<uint8_t>(address),
            reading ? Ess::DriverKind::READ : Ess::DriverKind::UPDATE, request, operationId) : Action::BUSY;
        if (result == Action::OK) track(id, operationId);
        action(id, command, result, static_cast<uint8_t>(address), result == Action::OK ? operationId : 0);
        return;
    }
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
            stopReply_.format = outputFormat_;
        } else action(id, actionCommand, result, static_cast<uint8_t>(address), result == Action::OK ? operationId : 0);
        return;
    }
    if (entry->command == Command::USEADDR) {
        if (outputPending()) { ++inputDropped_; return; }
        uint32_t address = 0;
        if (count != first + 2 || !number(tokens[first + 1], address) || address < 1 || address > 247) {
            error(id, "useaddr", "invalid_address"); return;
        }
        if (!host_.selectTarget) { error(id, "useaddr", "unavailable"); return; }
        action(id, "useaddr", host_.selectTarget(host_.context, static_cast<uint8_t>(address)), static_cast<uint8_t>(address));
        return;
    }
    if (entry->command == Command::WIRING) {
        if (outputPending()) { ++inputDropped_; return; }
        if (!host_.wiring) { error(id, "wiring", "unavailable"); return; }
        const bool change = count == first + 3;
        if (count != first + 1 && !change) { error(id, "wiring", "invalid_arguments"); return; }
        WiringRequest request;
        if (change) {
            const char* terminal = tokens[first + 1];
            if (std::strlen(terminal) != 2 ||
                !((terminal[0] == 'x' && terminal[1] >= '0' && terminal[1] <= '3') ||
                  (terminal[0] == 'y' && terminal[1] >= '0' && terminal[1] <= '1'))) {
                error(id, "wiring", "invalid_arguments"); return;
            }
            request.terminal = static_cast<uint8_t>(terminal[1] - '0' + (terminal[0] == 'y' ? 4 : 0));
            const char* state = tokens[first + 2];
            if (!std::strcmp(state, "connected")) request.state = Core::InputWiring::CONNECTED;
            else if (!std::strcmp(state, "unconnected")) request.state = Core::InputWiring::UNCONNECTED;
            else if (std::strcmp(state, "unknown")) { error(id, "wiring", "invalid_arguments"); return; }
        }
        WiringSnapshot view;
        const Action result = host_.wiring(host_.context, change ? &request : nullptr, view);
        std::snprintf(output_, sizeof(output_),
            "{\"type\":\"reply\",\"profile\":\"ess_rs\",\"id\":%lu,\"command\":\"wiring\",\"ok\":%s,\"result\":\"%s\",\"target\":%lu,\"address\":%u,\"generation\":%lu,\"configuration_generation\":%lu,\"inputs\":[%u,%u,%u,%u],\"outputs\":[%u,%u],\"bus_traffic\":false}",
            static_cast<unsigned long>(id), boolean(result == Action::OK), result == Action::OK ? "done" : actionName(result),
            static_cast<unsigned long>(view.target.id), view.target.address, static_cast<unsigned long>(view.target.generation),
            static_cast<unsigned long>(view.configurationGeneration), static_cast<unsigned>(view.inputs[0]), static_cast<unsigned>(view.inputs[1]),
            static_cast<unsigned>(view.inputs[2]), static_cast<unsigned>(view.inputs[3]), static_cast<unsigned>(view.outputs[0]), static_cast<unsigned>(view.outputs[1]));
        emit(); return;
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
        case Command::HOST: return host_.hostSerial != nullptr;
        case Command::USEADDR: return host_.selectTarget != nullptr;
        case Command::WIRING: return host_.wiring != nullptr;
        case Command::DEBUG: return host_.debug && host_.snapshot;
        case Command::MOTION_PROFILE: return host_.motionProfile != nullptr;
        case Command::COMMUNICATION: return host_.communication != nullptr;
        case Command::PERSISTENCE: return host_.persistence != nullptr;
        case Command::DISCOVER: return host_.discovery != nullptr;
        case Command::CAPTURE_READ: return host_.startCaptureRead != nullptr;
        case Command::ENABLE: case Command::MOTOR_RELEASE: case Command::ALARM_CLEAR: case Command::STOP: case Command::POSITION_CLEAR: return host_.startAction && host_.snapshot;
        case Command::MOVE: return host_.startMove && host_.snapshot && host_.axis;
        case Command::VELOCITY: return host_.startVelocity && host_.snapshot && host_.axis;
        case Command::TUNING: case Command::CONTROL: case Command::SEGMENT: case Command::IO: case Command::DRIVER: return host_.startDriver && host_.snapshot;
        case Command::HOME: return true; // Method inventory is local; execution checks its own hooks.
        case Command::PROFILE: return true; // Local inventory is always callable; operation routes check their hooks.
        case Command::READ: case Command::READ_IDENTITY: case Command::READ_CONFIG: case Command::READ_STATE: case Command::HEALTH_CHECK: return host_.startTypedRead != nullptr;
        case Command::RESULT: return host_.result != nullptr;
        case Command::CANCEL: return host_.cancel != nullptr;
        case Command::RELEASE: return host_.release != nullptr;
        case Command::PROBE: return host_.startProbe != nullptr;
        case Command::RECOVER: return host_.recover != nullptr;
        case Command::RESET: return host_.resetStats != nullptr;
        case Command::STATS: return host_.snapshot || host_.resetStats;
        case Command::HEALTH: return host_.snapshot || host_.startTypedRead;
        case Command::CONFIG: case Command::STATUS:
        case Command::MEMORY: case Command::DRV: return host_.snapshot != nullptr;
        default: return true;
        }
    };
    if (!callable(entry->command) || (entry->command == Command::HEALTH && !host_.snapshot) ||
        (entry->command == Command::STATS && (arg ? !host_.resetStats : !host_.snapshot)) ||
        (described && !callable(described->command))) {
        error(id, entry->name, "unavailable"); return;
    }
    if (entry->command == Command::HELP) {
        const char* helpSyntax=described?(described->command==Command::HEALTH && !host_.startTypedRead?"health":
            described->command==Command::HEALTH && !host_.snapshot?"health check [address]":
            described->command==Command::HOME && !(host_.startHome && host_.snapshot && host_.axis)?"home methods":
            described->command==Command::STATS && !host_.snapshot?"stats reset":
            described->command==Command::STATS && !host_.resetStats?"stats":described->syntax):nullptr;
        if (outputFormat_ == Format::HUMAN) {
            std::size_t used=0; bool fits=true;
            if (described) {
                fits=append(output_,sizeof(output_),used,"%s\nUsage: %s\n",described->name,helpSyntax) &&
                    append(output_,sizeof(output_),used,"%s",described->description);
                if (const char* example=helpExample(described->command))
                    fits=fits && append(output_,sizeof(output_),used,"\nExamples:\n  %s",example);
                fits=fits && append(output_,sizeof(output_),used,"\nArguments in [brackets] are optional. Use @ID before the command for full JSON evidence.");
            } else {
                fits=append(output_,sizeof(output_),used,"MotorControl-RS commands\nUse help COMMAND for syntax. Prefix @ID for JSON.\n");
                const char* groups[]={"Getting started","Read and observe","Motor operations","Drive settings","Host and setup","Operation results","Diagnostics"};
                for (const char* group:groups) {
                    bool heading=false;
                    for (const Entry& item:COMMANDS) {
                        if (!callable(item.command) || std::strcmp(helpGroup(item.command),group)) continue;
                        if (!heading) { fits=fits && append(output_,sizeof(output_),used,"\n[%s]\n",group); heading=true; }
                        fits=fits && append(output_,sizeof(output_),used,"  %-16s ",item.name) &&
                            append(output_,sizeof(output_),used,"%s",item.description) && append(output_,sizeof(output_),used,"\n");
                    }
                }
                fits=fits && append(output_,sizeof(output_),used,"\nRead cached state with status; refresh it with read state.\nAccepted operations finish asynchronously. Inspect with result ID, then release ID.\nCancel only cancels local work; stop normal|direct commands the motor.");
            }
            if (!fits) { error(id,"help","output_full"); return; }
            emit(0,true); return;
        }
        if (described) {
            std::snprintf(output_, sizeof(output_),
                "{\"type\":\"reply\",\"profile\":\"ess_rs\",\"id\":%lu,\"command\":\"help\",\"ok\":true,\"syntax\":\"%s\",\"effect\":\"%s\",\"bus_traffic\":%s}",
                static_cast<unsigned long>(id), helpSyntax,
                described->effect, boolean(described->bus &&
                    (described->command != Command::HEALTH || host_.startTypedRead) &&
                    (described->command != Command::HOME || (host_.startHome && host_.snapshot && host_.axis))));
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
        Snapshot snapshot; if (host_.snapshot) host_.snapshot(host_.context, snapshot);
        std::snprintf(output_, sizeof(output_),
            "{\"type\":\"reply\",\"profile\":\"ess_rs\",\"id\":%lu,\"command\":\"caps\",\"ok\":true,\"probe\":%s,\"identity\":%s,\"config\":%s,\"state\":%s,\"max_steps\":%u,\"max_reply_bytes\":%u,\"writes\":%s,\"motion\":%s,\"write_response_confirmed\":%s,\"axis_reserved\":%s,\"actions\":%s,\"device_queue_guarantee\":false,\"velocity\":%s,\"configured_ramp_policy\":true,\"acceleration_mapping\":false,\"velocity_unsupported\":[\"jerk\",\"blending\",\"live_updates\",\"torque\",\"current\",\"external_jog\"],\"driver_settings\":%s,\"limit_pair_writes\":false,\"home\":%s,\"home_methods\":[33,34,35],\"home_physical_qualified\":false,\"io_settings\":%s,\"input_terminals\":4,\"output_terminals\":2,\"no_function\":0,\"input_function_codes\":[0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17],\"output_function_codes\":[0,1,2,3,4,5,9,10],\"output_function_11\":\"unresolved\",\"input_mask\":15,\"output_mask\":3,\"external_enable_precedence\":\"unknown\",\"electrical_output_state_known\":false,\"segment_storage\":16,\"maximum_input_selections\":8,\"segment_execution\":\"external_input\",\"segment_pulse_writes\":false,\"negative_segment_speed_encoding\":\"unresolved\",\"control_settings\":%s,\"control_algorithm_codes\":[1,2],\"configured_encoder_is_identification\":false,\"current_percent_base\":\"unresolved\",\"effective_current_limit_from_peak\":false,\"tuning\":%s,\"tuning_groups\":[\"filters\",\"current-loop\",\"la\",\"collision\"],\"tuning_physical_scaling_known\":false,\"collision_003b_003c_access\":\"unresolved\",\"persistence\":%s,\"persistence_operations\":[\"save\",\"factory_restore\"],\"persistence_verified_all\":false,\"persistence_invocation_limit\":2}",
            static_cast<unsigned long>(id), boolean(caps.probe && callable(Command::PROBE)), boolean(caps.identity && callable(Command::READ_IDENTITY)), boolean(caps.config && callable(Command::READ_CONFIG)), boolean(caps.state && callable(Command::READ_STATE)), caps.maxSteps, caps.maxReplyBytes,
            boolean(callable(Command::ENABLE) || callable(Command::MOVE) || callable(Command::VELOCITY) || callable(Command::DRIVER) || (host_.startHome && host_.snapshot && host_.axis) || callable(Command::COMMUNICATION) || callable(Command::PERSISTENCE) || callable(Command::MOTION_PROFILE)),
            boolean(host_.startMove && host_.snapshot && host_.axis), boolean(snapshot.writeResponseConfirmed), boolean(snapshot.axisReserved), callable(Command::ENABLE) ? "[\"enable\",\"release\",\"clear_alarm\",\"clear_position\",\"stop_normal\",\"stop_direct\"]" : "[]", boolean(host_.startVelocity && host_.snapshot && host_.axis), boolean(host_.startDriver && host_.snapshot), boolean(host_.startHome && host_.snapshot && host_.axis), boolean(host_.startDriver && host_.snapshot), boolean(host_.startDriver && host_.snapshot), boolean(host_.startDriver && host_.snapshot), boolean(host_.persistence != nullptr));
        std::size_t used=std::strlen(output_);if(used) --used;
        if(!append(output_,sizeof(output_),used,",\"discovery\":%s,\"discovery_profiles\":%u,\"discovery_nonchanging\":true,\"discovery_max_tuples\":%u,\"discovery_max_results\":%u,\"discovery_model_mapping\":\"unresolved\",\"discovery_manufacturer_confirmation\":false,\"capability_scope\":\"implemented_application_routes\",\"unsupported\":[\"torque_motion\",\"current_motion\",\"serial_segment_start\"],\"unresolved\":[\"nonzero_home_offset\",\"paired_native_settings\",\"physical_acceleration_mapping\"],\"unimplemented\":[\"position_start_speed_write\",\"switch_homing\",\"jog_parameter_access\",\"homing_parameter_access\"],\"coverage_inventory\":\"docs/reference/ess_rs_operations.json\"}",
            boolean(host_.discovery!=nullptr),static_cast<unsigned>(MotorControlRS::discoveryProfileCount()),DISCOVERY_MAX_TUPLES,DISCOVERY_MAX_RESULTS)) {
            error(id,"caps","output_capacity");return;
        }
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
        setReportSerial(&view.serialTuple, view.serialGeneration);
        if (view.pending) {
            std::snprintf(output_, sizeof(output_),
                "{\"type\":\"reply\",\"profile\":\"ess_rs\",\"id\":%lu,\"command\":\"result\",\"command_id\":%lu,\"operation_id\":%lu,\"ok\":true,\"result\":\"pending\",\"recovery\":%s,\"capture_read\":%s,\"read_kind\":%s,\"action_kind\":%s,\"stop_policy\":%s,\"move_kind\":%s,\"velocity\":%s,\"driver\":%s,\"home\":%s}",
                static_cast<unsigned long>(id), static_cast<unsigned long>(view.commandId),
                static_cast<unsigned long>(view.operationId), boolean(view.recovery), boolean(view.captureRead),
                !view.typedRead ? "null" : view.typedRead->kind == Ess::ReadKind::IDENTITY ? "\"identity\"" : view.typedRead->kind == Ess::ReadKind::CONFIG ? "\"config\"" : "\"state\"",
                !view.actionContext ? "null" : view.actionContext->request.kind == Core::ActionKind::ENABLE ? "\"enable\"" : view.actionContext->request.kind == Core::ActionKind::RELEASE ? "\"release\"" : view.actionContext->request.kind == Core::ActionKind::CLEAR_ALARM ? "\"clear_alarm\"" : view.actionContext->request.kind == Core::ActionKind::CLEAR_POSITION ? "\"clear_position\"" : "\"stop\"",
                view.actionContext ? stopPolicy(view.actionContext->request) : "null", view.moveContext ? (view.moveContext->request.position.wrapped ? "\"angle\"" : view.moveContext->request.position.relative ? "\"relative\"" : "\"absolute\"") : "null", boolean(view.velocityContext != nullptr), boolean(view.driverContext != nullptr), boolean(view.homeContext != nullptr));
            std::size_t used = std::strlen(output_);
            if (!appendReportSerial(used)) { error(id, "result", "output_full"); return; }
            emit();
        } else if (view.homeContext) formatHome(id, view.commandId, view.operationId, *view.homeContext, true, view.interruptedByStop);
        else if (view.driverContext) formatDriver(id, view.commandId, view.operationId, *view.driverContext, true);
        else if (view.velocityContext) formatVelocity(id, view.commandId, view.operationId, *view.velocityContext, true, view.interruptedByStop);
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
    if (host_.snapshot) host_.snapshot(host_.context, data);
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
            "{\"type\":\"reply\",\"profile\":\"ess_rs\",\"id\":%lu,\"command\":\"%s\",\"ok\":true,\"address\":%u,\"baud\":%lu,\"format\":\"%s\",\"response_timeout_us\":%lu,\"reply_gap_us\":%lu,\"gap15_us\":%lu,\"gap35_us\":%lu,\"stale_after_ms\":%lu,\"ready\":%s,\"timing_qualified\":%s,\"device_settings\":\"%s\",\"cache_off_supported\":%s,\"sample_gap_limit_us\":%lu,\"cached_identity_id\":%lu,\"cached_identity_address\":%u,\"cached_identity_generation\":%lu,\"cached_config_id\":%lu,\"cached_config_address\":%u,\"cached_config_generation\":%lu,\"binding_generation\":%lu}",
            static_cast<unsigned long>(id), entry->name, data.address, static_cast<unsigned long>(data.baud),
            host_.hostSerial ? (data.serial.activeKnown ? formatName(data.serial.active.format) : "unknown") : "8N1",
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
            data.staleAfterMs && data.ageMs > data.staleAfterMs ? "stale" : "current";
        MotorControlRS::ReadTarget target; target.id = data.address; target.address = data.address; target.generation = data.bindingGeneration;
        if (data.communicationKnown && data.ready && !data.recoveryRequired) {
            communication = !sameTarget(data.communicationTarget, target) || data.nowUs < data.communicationEarliestUs ? "unknown" :
                data.staleAfterMs && data.nowUs - data.communicationEarliestUs > static_cast<uint64_t>(data.staleAfterMs) * 1000 ? "stale" : "current";
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
    if (host_.hostSerial && (entry->command == Command::STATUS || entry->command == Command::CONFIG)) {
        std::size_t used = std::strlen(output_);
        setReportSerial(data.serial.activeKnown ? &data.serial.active : nullptr, data.serial.activeKnown ? data.serial.generation : 0);
        if (!appendReportSerial(used)) { error(id, entry->name, "output_full"); return; }
    }
    emit();
}

bool Console::reportProbe(uint32_t id, uint8_t address, uint32_t operationId, const ProbeResult& result,
                          const HostTuple* tuple, uint32_t serialGeneration) noexcept {
    setReportSerial(tuple, serialGeneration);
    if (outputPending()) return false;
    for (auto& item : outstanding_) if (item.commandId == id && item.operationId == operationId && !item.transferred) {
        outputFormat_ = item.format;
        syntax_=helpCommand_=nullptr;
        if (!formatProbe(id, id, address, operationId, result, false)) return false;
        if (item.operationId == operationId) item.transferred = true;
        return true;
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
    std::size_t used = static_cast<std::size_t>(written);
    --used;
    const bool exception = result.transport.reason==Rtu::Reason::FRAME && result.codecChecked && result.codec.code==MotorControlRS::Err::EXCEPTION;
    if(!append(output_,sizeof(output_),used,",\"confidence\":\"%s\",\"manufacturer_confirmed\":false,\"exact_model_confirmed\":false,\"collision_excluded\":false}",
        result.captureRead?"not_requested":ok?"responder_model_unresolved":exception?"responder_only":"none")) return false;
    if (!appendReportSerial(used)) return false;
    emit(inspection ? 0 : operationId);
    return true;
}

bool Console::reportRecovery(uint32_t id, uint32_t operationId, const Rtu::RecoveryResult& result,
                             const HostTuple* tuple, uint32_t serialGeneration) noexcept {
    setReportSerial(tuple, serialGeneration);
    if (outputPending()) return false;
    for (auto& item : outstanding_) if (item.commandId == id && item.operationId == operationId && !item.transferred) {
        outputFormat_ = item.format;
        syntax_=helpCommand_=nullptr;
        if (!formatRecovery(id, id, operationId, result, false)) return false;
        if (item.operationId == operationId) item.transferred = true;
        return true;
    }
    return false;
}

bool Console::reportAction(uint32_t id, uint32_t operationId, const Ess::ActionContext& context, bool interruptedByStop,
                           const HostTuple* tuple, uint32_t serialGeneration) noexcept {
    setReportSerial(tuple, serialGeneration);
    if (outputPending() || context.operationId != operationId ||
        (context.state != Core::ActionState::SUCCEEDED && context.state != Core::ActionState::FAILED)) return false;
    for (auto& item : outstanding_) if (item.commandId == id && item.operationId == operationId && !item.transferred) {
        outputFormat_ = item.format;
        syntax_=helpCommand_=nullptr;
        if (!formatAction(id, id, operationId, context, false, interruptedByStop)) return false;
        // A successful immediate emit may already have cleared this slot.
        if (item.operationId == operationId) item.transferred = true;
        return true;
    }
    return false;
}

bool Console::reportDriver(uint32_t id, uint32_t operationId, const Ess::DriverContext& context,
                           const HostTuple* tuple, uint32_t serialGeneration) noexcept {
    setReportSerial(tuple, serialGeneration);
    if (outputPending() || context.operationId != operationId ||
        (context.state != Core::ReadState::SUCCEEDED && context.state != Core::ReadState::FAILED)) return false;
    for (auto& item : outstanding_) if (item.commandId == id && item.operationId == operationId && !item.transferred) {
        outputFormat_ = item.format;
        syntax_=helpCommand_=nullptr;
        if (!formatDriver(id, id, operationId, context, false)) return false;
        if (item.operationId == operationId) item.transferred = true;
        return true;
    }
    return false;
}

bool Console::formatDriver(uint32_t id, uint32_t commandId, uint32_t operationId,
                           const Ess::DriverContext& c, bool inspection) noexcept {
    std::size_t used = 0;
    const bool io = c.group == Ess::DriverGroup::IO;
    const bool segment = c.group >= Ess::DriverGroup::POSITION_SEGMENT && c.group <= Ess::DriverGroup::SEGMENT_START_SPEED;
    const bool control = c.group == Ess::DriverGroup::CONTROL_SETTINGS;
    const bool tuning = Ess::isTuningGroup(c.group);
    const char* command = tuning ? "tuning" : control ? "control" : segment ? "segment" : io ? "io" : "driver";
    const uint64_t stationaryUntil = Ess::driverWriteDeadline(c);
    if (!append(output_, sizeof(output_), used,
        "{\"type\":\"%s\",\"profile\":\"ess_rs\",\"id\":%lu,\"command\":\"%s\",\"command_id\":%lu,\"operation_id\":%lu,\"driver\":true,\"driver_kind\":\"%s\",\"ok\":%s,\"state\":\"%s\",\"outcome\":\"%s\",\"status\":\"%s\",\"detail\":%ld,\"target\":%lu,\"address\":%u,\"generation\":%lu,\"configuration_generation\":%lu,\"started_us\":%llu,\"deadline_us\":%llu,\"stationary_valid_until_us\":%llu,\"serviced_us\":%llu,\"completed_steps\":%u,\"fields\":%u,\"effects\":%u,\"uncertain\":%s,\"atomic\":false,\"active_settings_known\":false,\"progress_columns\":[\"field\",\"register\",\"previous\",\"requested\",\"acknowledged\",\"readback_known\",\"readback\",\"active_known\",\"active\",\"execution\"],\"progress\":[",
        inspection ? "reply" : command, static_cast<unsigned long>(id), inspection ? "result" : command,
        static_cast<unsigned long>(commandId), static_cast<unsigned long>(operationId), c.kind == Ess::DriverKind::READ ? "read" : "update",
        boolean(c.state == Core::ReadState::SUCCEEDED), readState(c.state), driverOutcome(c.outcome), Core::errToString(c.status.code), static_cast<long>(c.status.detail),
        static_cast<unsigned long>(c.target.id), c.target.address, static_cast<unsigned long>(c.target.generation), static_cast<unsigned long>(c.configurationGeneration),
        static_cast<unsigned long long>(c.startedUs), static_cast<unsigned long long>(c.deadlineUs), static_cast<unsigned long long>(stationaryUntil), static_cast<unsigned long long>(c.servicedUs),
        c.completedSteps, c.request.fields, c.effects, boolean(c.uncertain))) return false;
    // Additional policy fields follow progress/evidence; bounded rows stay shared.
    bool comma = false;
    for (const auto& p : c.progress) if (p.selected) {
        if (!append(output_, sizeof(output_), used, "%s[%u,%u,%u,%u,%s,%s,%u,%s,%u,\"%s\"]", comma ? "," : "",
            static_cast<unsigned>(p.field), p.reg, p.previous, p.requested, boolean(p.acknowledged), boolean(p.readbackKnown), p.readback,
            boolean(p.activeKnown), p.active, executionName(p.execution))) return false;
        comma = true;
    }
    if (!append(output_, sizeof(output_), used,
        "],\"evidence_columns\":[\"step\",\"register\",\"count\",\"write\",\"event\",\"raw_hex\",\"received_length\",\"tx_accepted\",\"tx_complete\",\"response_confirmed\",\"qualified\",\"execution_unknown\",\"earliest_us\",\"latest_us\",\"delivered_us\",\"transport_detail\",\"status\",\"detail\",\"frame_error\",\"attempted_us\"],\"evidence\":[")) return false;
    comma = false;
    for (std::size_t i = 0; i < Ess::DRIVER_MAX_STEPS; ++i) {
        const auto& e = c.observations[i];
        if (!e.count) continue;
        char raw[Ess::DRIVER_MAX_REPLY_BYTES * 2 + 1]; hex(e.raw, e.length, raw, sizeof(raw));
        if (!append(output_, sizeof(output_), used,
            "%s[%u,%u,%u,%s,%u,\"%s\",%u,%u,%s,%s,%s,%s,%llu,%llu,%llu,%ld,\"%s\",%ld,%u,%llu]",
            comma ? "," : "", e.step, e.reg, e.count, boolean(e.write), static_cast<unsigned>(e.event), raw,
            static_cast<unsigned>(e.receivedLength), static_cast<unsigned>(e.txAccepted), boolean(e.txComplete), boolean(e.responseConfirmed),
            boolean(e.qualified), boolean(e.executionUnknown), static_cast<unsigned long long>(e.earliestUs),
            static_cast<unsigned long long>(e.latestUs), static_cast<unsigned long long>(e.deliveredUs), static_cast<long>(e.transportDetail),
            Core::errToString(e.status.code), static_cast<long>(e.status.detail), static_cast<unsigned>(e.frameError), static_cast<unsigned long long>(e.attemptedUs))) return false;
        comma = true;
    }
    if (!append(output_, sizeof(output_), used, "],\"observation\":")) return false;
    if (c.kind == Ess::DriverKind::READ && Ess::getDriver(c, driverView_)) {
        if (io) {
            const auto& v = driverView_;
            if (!append(output_, sizeof(output_), used, "{\"raw\":[%u,%u,%u,%u,%u,%u,%u,%u,%u],\"known_fields\":%u,\"unknown_input_polarity_bits\":%u,\"unknown_output_polarity_bits\":%u,\"unknown_custom_output_bits\":%u}",
                v.raw[7], v.raw[8], v.raw[9], v.raw[10], v.raw[11], v.raw[12], v.raw[13], v.raw[14], v.raw[15], v.knownFields,
                v.raw[7] & ~15U, v.raw[12] & ~3U, v.raw[15] & ~3U)) return false;
        } else if (segment) {
            Ess::SegmentObservation typed;
            if (!Ess::getSegment(c, typed)) return false;
            const auto& v = driverView_;
            if (!append(output_, sizeof(output_), used, "{\"raw\":[%u,%u,%u,%u,%u],\"known_fields\":%u,\"paired_write_supported\":false,\"signed_encoding\":\"unresolved\",\"active_settings_known\":false}",
                v.raw[0], v.raw[1], v.raw[2], v.raw[3], v.raw[4], v.knownFields)) return false;
        } else if (tuning) {
            Ess::TuningObservation typed;
            if (!Ess::getTuning(c,typed)) return false;
            if (!append(output_,sizeof(output_),used,"{\"raw\":[")) return false;
            for (uint8_t i=0;i<typed.count;++i)
                if (!append(output_,sizeof(output_),used,"%s%u",i?",":"",typed.raw[i])) return false;
            if (!append(output_,sizeof(output_),used,"],\"known_fields\":%u,\"native_units_only\":true,\"physical_scaling_known\":false,\"active_settings_known\":false}",typed.knownFields)) return false;
        } else if (control) {
            Ess::ControlObservation typed;
            if (!Ess::getControl(c, typed)) return false;
            const auto& v = typed;
            if (!append(output_, sizeof(output_), used,
                "{\"raw\":[%u,%u,%u,%u,%u,%u,%u,%u],\"known_fields\":%u,\"algorithm_known\":%s,\"encoder_scale_usable\":%s,\"encoder_resolution\":%u,\"maximum_effective_current_ma\":%u,\"closed_maximum_percent\":%u,\"closed_base_percent\":%u,\"open_maximum_percent\":%u,\"lock_percent\":%u,\"lock_delay_ms\":%u,\"current_percent_base\":\"unresolved\",\"active_settings_known\":false}",
                v.raw[0], v.raw[1], v.raw[2], v.raw[3], v.raw[4], v.raw[5], v.raw[6], v.raw[7], v.knownFields,
                boolean(v.algorithmKnown), boolean(v.encoderScaleUsable), v.encoderResolution, v.maximumEffectiveCurrentMa,
                v.closedMaximumPercent, v.closedBasePercent, v.openMaximumPercent, v.lockPercent, v.lockDelayMs)) return false;
        } else {
        const auto& v = driverView_;
        if (!append(output_, sizeof(output_), used,
            "{\"raw\":[%u,%u,%u,%u,%u,%u,%u],\"known_fields\":%u,\"positive_words\":[%u,%u],\"negative_words\":[%u,%u],\"pair_known\":%s,\"positive_bits\":%lu,\"negative_bits\":%lu,\"signed_limits\":\"unresolved\",\"native_scale\":\"unresolved\"}",
            v.raw[0], v.raw[1], v.raw[2], v.raw[3], v.raw[4], v.raw[5], v.raw[6], v.knownFields,
            v.positiveWords[0], v.positiveWords[1], v.negativeWords[0], v.negativeWords[1], boolean(v.pairKnown),
            static_cast<unsigned long>(v.positiveBits), static_cast<unsigned long>(v.negativeBits))) return false;
        }
    } else if (!append(output_, sizeof(output_), used, "null")) return false;
    if (!append(output_, sizeof(output_), used, ",\"driver_group\":\"%s\",\"echo_readback_policy\":%s,\"settlement\":\"%s\"}",
        tuning ? tuningName(c.group) : control ? "control_settings" : segment ? (c.group == Ess::DriverGroup::POSITION_SEGMENT ? "position_segment" : c.group == Ess::DriverGroup::SPEED_SEGMENT ? "speed_segment" : "segment_start_speed") : io ? "io" : "drive", boolean(c.prerequisites.allowEchoReadback), io || segment || control || tuning ? "stored_readback" : "checked_ack_readback")) return false;
    // Index belongs to the retained request context, separately from host correlation.
    if (segment) {
        used -= 1;
        if (!append(output_, sizeof(output_), used, ",\"segment_index\":%u,\"execution_path\":\"external_input\",\"storage_capacity\":16,\"maximum_input_selections\":8}", c.request.segmentIndex)) return false;
    }
    if (!appendReportSerial(used)) return false;
    emit(inspection ? 0 : operationId); return true;
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
    if (!appendReportSerial(used)) return false;
    emit(inspection ? 0 : operationId);
    return true;
}

bool Console::reportRead(uint32_t id, uint32_t operationId, const Ess::ReadContext& context,
                         const HostTuple* tuple, uint32_t serialGeneration) noexcept {
    setReportSerial(tuple, serialGeneration);
    if (outputPending() || context.operationId != operationId ||
        (context.state != MotorControlRS::ReadState::SUCCEEDED && context.state != MotorControlRS::ReadState::FAILED)) return false;
    for (auto& item : outstanding_) if (item.commandId == id && item.operationId == operationId && !item.transferred) {
        outputFormat_ = item.format;
        syntax_=helpCommand_=nullptr;
        if (!formatRead(id, id, operationId, context, false)) return false;
        if (item.operationId == operationId) item.transferred = true;
        return true;
    }
    return false;
}

bool Console::reportHome(uint32_t id, uint32_t operationId, const Ess::HomeContext& context,
                         bool interruptedByStop, const HostTuple* tuple, uint32_t serialGeneration) noexcept {
    setReportSerial(tuple, serialGeneration);
    if (outputPending() || context.operationId != operationId ||
        (context.state != Core::ActionState::SUCCEEDED && context.state != Core::ActionState::FAILED)) return false;
    for (auto& item : outstanding_) if (item.commandId == id && item.operationId == operationId && !item.transferred) {
        outputFormat_ = item.format;
        syntax_=helpCommand_=nullptr;
        if (!formatHome(id, id, operationId, context, false, interruptedByStop)) return false;
        if (item.operationId == operationId) item.transferred = true;
        return true;
    }
    return false;
}

bool Console::formatHome(uint32_t id, uint32_t commandId, uint32_t operationId,
                         const Ess::HomeContext& c, bool inspection, bool interruptedByStop) noexcept {
    std::size_t used = 0;
    const bool ok = c.state == Core::ActionState::SUCCEEDED && c.completion == Core::ActionCompletion::OBSERVED;
    bool fits = append(output_, sizeof(output_), used,
        "{\"type\":\"%s\",\"profile\":\"ess_rs\",\"id\":%lu,\"command\":\"%s\",\"command_id\":%lu,\"operation_id\":%lu,\"home\":true,\"read_kind\":null,\"action_kind\":null,\"move_kind\":null,\"capture_read\":false,\"recovery\":false,\"ok\":%s,\"state\":\"%s\",\"outcome\":\"%s\",\"status\":\"%s\",\"detail\":%ld,\"setup_execution\":\"%s\",\"execution\":\"%s\",\"completion\":\"%s\",\"target\":%lu,\"address\":%u,\"generation\":%lu,\"configuration_generation\":%lu,\"started_us\":%llu,\"deadline_us\":%llu,\"serviced_us\":%llu,\"polls\":%u,\"phase\":%u,\"method\":%d,\"staging_words\":[%u,%u,%u,%u,%u,%u],\"staging_applied\":%s,\"uncertain\":%s,\"running_observed\":%s,\"homed_low_observed\":%s,\"observation_known\":%s,\"raw_alarm\":%u,\"raw_motion\":%u,\"raw_position_words\":[%u,%u],\"interrupted_by_stop\":%s,\"prerequisites\":{\"observed_us\":%llu,\"maximum_age_us\":%llu,\"raw_motion\":%u,\"auxiliary\":%u,\"reference_semantics_qualified\":%s,\"qualified_parameters\":[%d,%u,%u,%u]},\"completion_observed_us\":%llu,\"evidence_columns\":[\"step\",\"event\",\"raw_hex\",\"received_length\",\"tx_accepted\",\"tx_complete\",\"response_confirmed\",\"qualified\",\"execution_unknown\",\"earliest_us\",\"latest_us\",\"delivered_us\",\"transport_detail\",\"status\",\"detail\",\"frame_error\"],\"staging_evidence\":",
        inspection ? "reply" : "home", static_cast<unsigned long>(id), inspection ? "result" : "home",
        static_cast<unsigned long>(commandId), static_cast<unsigned long>(operationId), boolean(ok),
        c.state == Core::ActionState::SUCCEEDED ? "succeeded" : "failed", motorOutcome(c.outcome), Core::errToString(c.status.code), static_cast<long>(c.status.detail),
        executionName(c.setupExecution), executionName(c.execution), c.completion == Core::ActionCompletion::OBSERVED ? "observed" : "not_observed",
        static_cast<unsigned long>(c.target.id), c.target.address, static_cast<unsigned long>(c.target.generation), static_cast<unsigned long>(c.request.configurationGeneration),
        static_cast<unsigned long long>(c.startedUs), static_cast<unsigned long long>(c.deadlineUs), static_cast<unsigned long long>(c.servicedUs), c.polls, static_cast<unsigned>(c.phase), static_cast<int>(c.request.method),
        c.words[0], c.words[1], c.words[2], c.words[3], c.words[4], c.words[5], boolean(c.stagingApplied), boolean(c.uncertain), boolean(c.runningObserved), boolean(c.homedLowObserved), boolean(c.observationKnown),
        c.rawAlarm, c.rawMotion, c.rawPositionWords[0], c.rawPositionWords[1], boolean(interruptedByStop),
        static_cast<unsigned long long>(c.prerequisites.observedUs), static_cast<unsigned long long>(c.prerequisites.maximumAgeUs), c.prerequisites.rawMotion, static_cast<unsigned>(c.prerequisites.auxiliary), boolean(c.prerequisites.referenceSemanticsQualified), static_cast<int>(c.prerequisites.qualifiedMethod),
        c.prerequisites.qualifiedSearchSpeed, c.prerequisites.qualifiedReturnSpeed, c.prerequisites.qualifiedRampTime, static_cast<unsigned long long>(c.completionObservedUs)) &&
        homeEvidence(output_, sizeof(output_), used, c.stagingEvidence) && append(output_, sizeof(output_), used, ",\"trigger_evidence\":") &&
        homeEvidence(output_, sizeof(output_), used, c.triggerEvidence) && append(output_, sizeof(output_), used, ",\"activity_evidence\":") &&
        homeEvidence(output_, sizeof(output_), used, c.activityEvidence) && append(output_, sizeof(output_), used, ",\"low_evidence\":") &&
        homeEvidence(output_, sizeof(output_), used, c.lowEvidence) && append(output_, sizeof(output_), used, ",\"last_observation\":") &&
        homeEvidence(output_, sizeof(output_), used, c.lastObservation) && append(output_, sizeof(output_), used, ",\"completion_evidence\":") &&
        homeEvidence(output_, sizeof(output_), used, c.completionEvidence) && append(output_, sizeof(output_), used, ",\"zero_evidence\":") &&
        homeEvidence(output_, sizeof(output_), used, c.zeroEvidence) && append(output_, sizeof(output_), used, ",\"failure_evidence\":") &&
        homeEvidence(output_, sizeof(output_), used, c.failureEvidence) && append(output_, sizeof(output_), used, "}");
    if (!fits || !appendReportSerial(used)) return false;
    emit(inspection ? 0 : operationId); return true;
}

bool Console::reportMove(uint32_t id, uint32_t operationId, const Ess::MoveContext& context,
                         bool interruptedByStop, const HostTuple* tuple, uint32_t serialGeneration) noexcept {
    setReportSerial(tuple, serialGeneration);
    if (outputPending() || context.operationId != operationId ||
        (context.state != Core::ActionState::SUCCEEDED && context.state != Core::ActionState::FAILED)) return false;
    for (auto& item : outstanding_) if (item.commandId == id && item.operationId == operationId && !item.transferred) {
        outputFormat_ = item.format;
        syntax_=helpCommand_=nullptr;
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
        "{\"type\":\"%s\",\"profile\":\"ess_rs\",\"id\":%lu,\"command\":\"%s\",\"command_id\":%lu,\"operation_id\":%lu,\"move_kind\":\"%s\",\"read_kind\":null,\"action_kind\":null,\"capture_read\":false,\"recovery\":false,\"ok\":%s,\"state\":\"%s\",\"outcome\":\"%s\",\"status\":\"%s\",\"detail\":%ld,\"setup_execution\":\"%s\",\"execution\":\"%s\",\"completion\":\"%s\",\"target\":%lu,\"address\":%u,\"generation\":%lu,\"configuration_generation\":%lu,\"started_us\":%llu,\"deadline_us\":%llu,\"serviced_us\":%llu,\"polls\":%u,\"staging_applied\":%s,\"uncertain\":%s,\"running_observed\":%s,\"observation_known\":%s,\"raw_alarm\":%s,\"raw_motion\":%s,\"native_rpm\":%u,\"ramp\":\"configured\",\"staging_words\":[%u,%u,%u,%u,%u],\"requested\":{\"numerator\":%lld,\"denominator\":%llu,\"unit\":\"%s\",\"frame\":%u,\"relative\":%s,\"wrapped\":%s,\"angle_path\":%u,\"half_turn_tie\":%u,\"basis\":%u,\"rounding\":%u,\"approximate\":%s,\"rational_radians\":%s,\"radians\":%s,\"maximum_quantization_error\":%.17g,\"maximum_approximation_error\":%s},\"effective_native\":%lld,\"endpoint_known\":%s,\"endpoint_native\":%lld,\"displacement_known\":%s,\"displacement_native\":%lld,\"zero_displacement\":%s,\"rounding_error\":%.17g,\"approximation_error_bound\":%.17g,\"exact_arithmetic\":%s,\"requested_native_approximate\":%s,\"staging_evidence\":",
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
        boolean(p.displacementKnown), static_cast<long long>(p.displacementNative), boolean(p.zeroDisplacement), p.roundingError, p.approximationErrorBound, boolean(p.exactArithmetic), approximateNative) &&
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
    if (!fits || !appendReportSerial(used)) return false;
    emit(inspection ? 0 : operationId);
    return true;
}

bool Console::reportVelocity(uint32_t id, uint32_t operationId, const Ess::VelocityContext& context,
                             bool interruptedByStop, const HostTuple* tuple, uint32_t serialGeneration) noexcept {
    setReportSerial(tuple, serialGeneration);
    if (outputPending() || context.operationId != operationId ||
        (context.state != Core::ActionState::SUCCEEDED && context.state != Core::ActionState::FAILED)) return false;
    for (auto& item : outstanding_) if (item.commandId == id && item.operationId == operationId && !item.transferred) {
        outputFormat_ = item.format;
        syntax_=helpCommand_=nullptr;
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
    if (!fits || !appendReportSerial(used)) return false;
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
                "%s{\"function\":%u,\"known\":%s,\"inverted\":%s,\"wiring\":%u,\"level_known\":%s,\"level\":null,\"assignment\":\"%s\",\"declared_wiring\":\"%s\"}",
                i ? "," : "", raw.inputFunctions[i], boolean(v.inputFunctionKnown[i]), boolean(v.inputInverted[i]),
                static_cast<unsigned>(v.wiring[i]), boolean(v.inputLevelKnown[i]),
                !v.inputFunctionKnown[i] ? "unresolved" : raw.inputFunctions[i] == 0 ? "disabled" : "assigned",
                v.wiring[i] == Core::InputWiring::CONNECTED ? "connected" : v.wiring[i] == Core::InputWiring::UNCONNECTED ? "unconnected" : "unknown");
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
    if (!appendReportSerial(used)) return false;
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
    std::size_t used = static_cast<std::size_t>(written);
    if (!appendReportSerial(used)) return false;
    emit(inspection ? 0 : operationId);
    return true;
}

}} // namespace MotorControlRSExample::Probe
