// SPDX-License-Identifier: MIT
#include "ConsoleText.h"
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace MotorControlRSExample { namespace Probe {
namespace {
constexpr std::size_t MAX_INPUT = 16384;
constexpr unsigned MAX_DEPTH = 8;
const char TRUNCATED[] = "[Details truncated. Inspect retained results with @ID result N; do not repeat writes.]\n";

enum class Kind { STRING, NUMBER, BOOLEAN, NIL, OBJECT, ARRAY };
struct Value {
    const char* begin = nullptr;
    const char* end = nullptr;
    Kind kind = Kind::NIL;
};
bool digit(char c) { return c >= '0' && c <= '9'; }
bool hexDigit(char c) {
    return digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
bool equal(const Value& value, const char* text) {
    return value.begin && static_cast<std::size_t>(value.end - value.begin) == std::strlen(text) &&
        std::memcmp(value.begin, text, static_cast<std::size_t>(value.end - value.begin)) == 0;
}

// Only the console's JSON vocabulary is consumed. Depth and input length are
// bounded; tokens borrow input spans rather than allocating a document tree.
class Reader {
public:
    Reader(const char* begin, const char* end) : at(begin), end_(end) {}
    void space() {
        while (at < end_ && (*at == ' ' || *at == '\t' || *at == '\r' || *at == '\n')) ++at;
    }
    bool take(char c) {
        space();
        if (at == end_ || *at != c) return false;
        ++at;
        return true;
    }
    bool string(Value& output) {
        space();
        if (at == end_ || *at++ != '"') return false;
        output.begin = at;
        output.kind = Kind::STRING;
        while (at < end_) {
            const unsigned char c = static_cast<unsigned char>(*at++);
            if (c == '"') { output.end = at - 1; return true; }
            if (c < 32) return false;
            if (c != '\\') continue;
            if (at == end_) return false;
            const char escaped = *at++;
            if (escaped == 'u') {
                for (unsigned i = 0; i < 4; ++i)
                    if (at == end_ || !hexDigit(*at++)) return false;
            } else if (!std::strchr("\"\\/bfnrt", escaped)) return false;
        }
        return false;
    }
    bool value(Value& output, unsigned depth = 0) {
        space();
        if (at == end_ || depth > MAX_DEPTH) return false;
        output.begin = at;
        if (*at == '"') return string(output);
        if (*at == '{' || *at == '[') {
            const bool object = *at++ == '{';
            output.kind = object ? Kind::OBJECT : Kind::ARRAY;
            const char close = object ? '}' : ']';
            space();
            if (at < end_ && *at == close) { output.end = ++at; return true; }
            for (;;) {
                Value member;
                if (object && (!string(member) || !take(':'))) return false;
                if (!value(member, depth + 1)) return false;
                space();
                if (at == end_) return false;
                if (*at == close) { output.end = ++at; return true; }
                if (*at++ != ',') return false;
            }
        }
        if (*at == '-' || digit(*at)) {
            output.kind = Kind::NUMBER;
            if (*at == '-' && ++at == end_) return false;
            if (*at == '0') ++at;
            else {
                if (*at < '1' || *at > '9') return false;
                while (at < end_ && digit(*at)) ++at;
            }
            if (at < end_ && *at == '.') {
                if (++at == end_ || !digit(*at)) return false;
                while (at < end_ && digit(*at)) ++at;
            }
            if (at < end_ && (*at == 'e' || *at == 'E')) {
                ++at;
                if (at < end_ && (*at == '+' || *at == '-')) ++at;
                if (at == end_ || !digit(*at)) return false;
                while (at < end_ && digit(*at)) ++at;
            }
        } else {
            const char* literal = *at == 't' ? "true" : *at == 'f' ? "false" : "null";
            const std::size_t length = std::strlen(literal);
            if (static_cast<std::size_t>(end_ - at) < length || std::memcmp(at, literal, length)) return false;
            output.kind = *at == 'n' ? Kind::NIL : Kind::BOOLEAN;
            at += length;
        }
        output.end = at;
        return true;
    }
    const char* at;
private:
    const char* end_;
};

Value member(const Value& object, const char* name) {
    if (object.kind != Kind::OBJECT) return Value();
    Reader reader(object.begin + 1, object.end - 1);
    Value key, value;
    while (reader.string(key) && reader.take(':') && reader.value(value)) {
        if (equal(key, name)) return value;
        if (!reader.take(',')) break;
    }
    return Value();
}

class Writer {
public:
    Writer(char* output, std::size_t capacity) : output_(output), capacity_(capacity),
        limit_(capacity > sizeof(TRUNCATED) ? capacity - sizeof(TRUNCATED) : 0) {}
    bool text(const char* value) { return span(value, std::strlen(value)); }
    bool span(const char* value, std::size_t length) {
        if (truncated || length > limit_ - size) { truncated = true; return false; }
        std::memcpy(output_ + size, value, length);
        size += length;
        return true;
    }
    bool character(char c) { return span(&c, 1); }
    bool indent(unsigned level) {
        for (unsigned i = 0; i < level; ++i) if (!text("  ")) return false;
        return true;
    }
    bool string(const Value& value) {
        for (const char* at = value.begin; at < value.end; ++at) {
            if (*at != '\\') { if (!character(*at)) return false; continue; }
            const char c = *++at;
            if (c == '"' || c == '\\' || c == '/') {
                if (!character(c)) return false;
            } else {
                // Keep control/unicode escapes legible and on the same line.
                // They must never inject a terminal control byte or NUL.
                if (!character('\\') || !character(c)) return false;
                if (c == 'u') {
                    if (!span(at + 1, 4)) return false;
                    at += 4;
                }
            }
        }
        return true;
    }
    bool scalar(const Value& value) {
        if (value.kind == Kind::STRING) return value.begin == value.end ? text("(empty)") : string(value);
        if (value.kind == Kind::BOOLEAN) return text(equal(value, "true") ? "yes" : "no");
        if (value.kind == Kind::NIL) return text("unavailable");
        return span(value.begin, static_cast<std::size_t>(value.end - value.begin));
    }
    void finish() {
        if (truncated) {
            const std::size_t available = capacity_ - 1 - size;
            const std::size_t length = sizeof(TRUNCATED) - 1;
            if (length <= available) { std::memcpy(output_ + size, TRUNCATED, length); size += length; }
        }
        output_[size] = '\0';
    }
    std::size_t size = 0;
    bool truncated = false;
private:
    char* output_;
    std::size_t capacity_, limit_;
};

bool label(Writer& writer, const Value& key) {
    struct Named { const char* key; const char* label; };
    static const Named names[] = {
        {"tx_hex", "TX bytes"}, {"rx_hex", "RX bytes"}, {"raw_hex", "Bytes"},
        {"operation_id", "Operation"}, {"native_rpm", "Speed (rpm)"},
        {"raw_position", "Position (raw)"}, {"raw_speed", "Speed (raw)"},
        {"setup_execution", "Setup execution"}, {"interrupted_by_stop", "Interrupted by stop"}
    };
    for (const auto& entry : names) if (equal(key, entry.key)) return writer.text(entry.label);
    const std::size_t length = static_cast<std::size_t>(key.end - key.begin);
    const bool microseconds = length > 3 && !std::memcmp(key.end - 3, "_us", 3);
    const bool milliseconds = length > 3 && !std::memcmp(key.end - 3, "_ms", 3);
    const char* end = microseconds || milliseconds ? key.end - 3 : key.end;
    for (const char* at = key.begin; at < end; ++at) {
        char c = *at == '_' ? ' ' : *at;
        if (at == key.begin && c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
        if (!writer.character(c)) return false;
    }
    return microseconds ? writer.text(" (us)") : milliseconds ? writer.text(" (ms)") : true;
}
Value key(const char* text) {
    Value value;
    value.begin = text;
    value.end = text + std::strlen(text);
    value.kind = Kind::STRING;
    return value;
}
bool bytes(Writer& writer, const Value& value) {
    if (value.kind != Kind::STRING) return writer.scalar(value);
    if (value.begin == value.end) return writer.text("(none)");
    std::size_t offset = 0;
    for (const char* at = value.begin; at < value.end; ++at, ++offset) {
        if (offset && offset % 2 == 0 && !writer.character(' ')) return false;
        if (!writer.character(*at)) return false;
    }
    return true;
}
bool field(Writer&, const Value&, const Value&, unsigned);
bool contents(Writer& writer, const Value& value, unsigned level) {
    Reader reader(value.begin + 1, value.end - 1);
    if (value.kind == Kind::OBJECT) {
        Value name, child;
        while (reader.string(name) && reader.take(':') && reader.value(child)) {
            if (!field(writer, name, child, level)) return false;
            if (!reader.take(',')) break;
        }
        return true;
    }
    Value child;
    unsigned index = 0;
    while (reader.value(child)) {
        char name[24];
        std::snprintf(name, sizeof(name), "Item %u", ++index);
        if (!field(writer, key(name), child, level)) return false;
        if (!reader.take(',')) break;
    }
    return true;
}
bool flatArray(const Value& value) {
    Reader reader(value.begin + 1, value.end - 1);
    Value child;
    while (reader.value(child)) {
        if (child.kind == Kind::ARRAY || child.kind == Kind::OBJECT) return false;
        if (!reader.take(',')) break;
    }
    return true;
}
bool field(Writer& writer, const Value& name, const Value& value, unsigned level) {
    const std::size_t start = writer.size;
    bool ok = writer.indent(level) && label(writer, name) && writer.text(":");
    if (value.kind == Kind::OBJECT || (value.kind == Kind::ARRAY && !flatArray(value))) {
        ok = ok && writer.character('\n');
        if (!ok) { writer.size = start; return false; }
        return contents(writer, value, level + 1);
    }
    ok = ok && writer.character(' ');
    if (value.kind == Kind::ARRAY) {
        Reader reader(value.begin + 1, value.end - 1);
        Value child;
        bool first = true;
        while (ok && reader.value(child)) {
            ok = (first || writer.text(", ")) && writer.scalar(child);
            first = false;
            if (!reader.take(',')) break;
        }
        if (first) ok = ok && writer.text("(none)");
    } else if (equal(name, "raw_hex") || equal(name, "tx_hex") || equal(name, "rx_hex")) {
        ok = ok && bytes(writer, value);
    } else ok = ok && writer.scalar(value);
    ok = ok && writer.character('\n');
    if (!ok) writer.size = start;
    return ok;
}

const char* const SUMMARY[] = {"operation_id", "result", "state", "outcome", "error", "status", "detail",
    "execution", "setup_execution", "completion", "uncertain", "execution_unknown", "interrupted_by_stop",
    "activation_unknown", "manual_intervention_required", "restart_required_for_proof",
    "stop_execution", "stop_completion", "stop_outcome"};
bool summaryKey(const Value& name) {
    for (const auto text : SUMMARY) if (equal(name, text)) return true;
    return false;
}
bool metadata(const Value& name) {
    return equal(name, "type") || equal(name, "profile") || equal(name, "command") || equal(name, "ok");
}
bool hint(Writer& writer, const Value& report) {
    const Value execution = member(report, "execution");
    const Value error = member(report, "error");
    const Value result = member(report, "result");
    const Value operation = member(report, "operation_id");
    if (equal(execution, "unknown") || equal(member(report, "execution_unknown"), "true")) {
        if (!writer.text("Hint: inspect result") ||
            (operation.begin && (!writer.character(' ') || !writer.scalar(operation)))) return false;
        return writer.text("; execution is unknown. Do not repeat the write.\n");
    }
    if (equal(error, "recovery_required") || equal(result, "recovery_required"))
        return writer.text("Hint: recover restores host transport only; inspect status afterward.\n");
    if (equal(error, "results_full") || equal(result, "results_full"))
        return writer.text("Hint: inspect result, then release a reviewed terminal operation by its ID.\n");
    if (equal(error, "unknown_command")) return writer.text("Hint: help lists commands; help COMMAND shows syntax.\n");
    if (operation.begin && !equal(operation, "0")) {
        if (!writer.text("Details: result ") || !writer.scalar(operation) || !writer.text(".\n")) return false;
        if (equal(member(report, "ok"), "false") &&
            (!writer.text("After review: release ") || !writer.scalar(operation) || !writer.text(".\n"))) return false;
        if (equal(member(report, "outcome"), "cancelled"))
            return writer.text("Local cancellation does not stop the motor.\n");
        return true;
    }
    if (equal(member(report, "ok"), "false")) {
        Value command = member(report, "command");
        if (!command.begin) command = key("help");
        if (command.end - command.begin >= 5 && !std::memcmp(command.begin, "move-", 5)) command = key("move");
        if (command.end - command.begin >= 5 && !std::memcmp(command.begin, "read-", 5)) command = key("read");
        return writer.text("Hint: help ") && writer.string(command) && writer.text(" shows syntax and prerequisites.\n");
    }
    return true;
}
// Each captured event stays compact. The unchanged JSON form retains the full
// machine report; human traffic suppresses inapplicable decoder placeholders.
bool trafficLineField(Writer& writer, const Value& object, const char* name, bool& first) {
    const Value value = member(object, name);
    if (!value.begin || value.kind == Kind::NIL) return true;
    if (!first && !writer.text("; ")) return false;
    if (!field(writer, key(name), value, 0)) return false;
    --writer.size; // Join scalar/flat-array fields on this event's single line.
    first = false;
    return true;
}
bool trafficReport(Writer& writer, const Value& report) {
    if (!writer.character('[') || !writer.scalar(member(report, "kind")) ||
        !writer.text("] transaction ") || !writer.scalar(member(report, "transaction"))) return false;
    const Value at = member(report, "at_us"), sequence = member(report, "sequence");
    if (at.begin && (!writer.text(" at ") || !writer.scalar(at) || !writer.text(" us"))) return false;
    if (sequence.begin && (!writer.text(" (#") || !writer.scalar(sequence) || !writer.character(')'))) return false;
    if (!writer.character('\n')) return false;
    bool first = true;
    static const char* const envelope[] = {"start_us", "end_us", "uncertainty_us", "length", "complete", "code",
                                          "dropped", "capture_dropped", "overwritten"};
    for (const char* name : envelope) {
        if ((!std::strcmp(name, "start_us") || !std::strcmp(name, "end_us")) &&
            equal(member(report, "start_us"), "0") && equal(member(report, "end_us"), "0")) continue;
        if (!trafficLineField(writer, report, name, first)) return false;
    }
    if (!first && !writer.character('\n')) return false;
    const Value raw = member(report, "raw_hex");
    if (raw.kind == Kind::STRING && raw.begin != raw.end && !field(writer, key("raw_hex"), raw, 0)) return false;
    if (!equal(member(report, "mode"), "decoded")) return true;
    const Value status = member(report, "decode_status");
    if (status.kind == Kind::NIL) return true;
    first = true;
    static const char* const diagnosis[] = {"decode_status", "decode_detail", "frame_error", "expected_request_sequence"};
    for (const char* name : diagnosis) {
        if (std::strcmp(name, "decode_status") && equal(member(report, name), "0")) continue;
        if (!trafficLineField(writer, report, name, first)) return false;
    }
    if (!first && !writer.character('\n')) return false;
    const Value decoded = member(report, "decoded");
    if (decoded.kind != Kind::OBJECT) return true;
    Reader fields(decoded.begin + 1, decoded.end - 1);
    Value name, value;
    first = true;
    while (fields.string(name) && fields.take(':') && fields.value(value)) {
        const bool omitted = value.kind == Kind::NIL ||
            (value.kind == Kind::ARRAY && value.end - value.begin == 2) ||
            (equal(name, "exception") && equal(value, "false")) ||
            (equal(name, "exception_code") && equal(value, "0"));
        if (!omitted) {
            if ((!first && !writer.text("; ")) || !field(writer, name, value, 0)) return false;
            --writer.size;
            first = false;
        }
        if (!fields.take(',')) break;
    }
    return first || writer.character('\n');
}
bool heading(Writer& writer, const Value& report) {
    const Value ok = member(report, "ok");
    const Value context = member(report, "context");
    const bool failed = equal(ok, "false") || equal(member(report, "state"), "failed");
    const Value pendingField = member(report, "pending");
    const Value operation = member(report, "operation_id");
    const bool pending = equal(pendingField, "true") || equal(member(report, "state"), "active") ||
        equal(member(context, "uncertain"), "true") || equal(member(context, "activation_unknown"), "true") ||
        (!pendingField.begin && operation.begin && !equal(operation, "0") && equal(member(report, "result"), "accepted"));
    const Value command = member(report, "command");
    return writer.text(failed ? "[ERROR] " : pending ? "[WAIT] " : ok.begin ? "[OK] " : "[INFO] ") &&
        (command.begin ? writer.string(command) : writer.text("response")) && writer.character('\n');
}

// Ordinary motion output answers what happened. The original report remains
// available through @ID result N; none of the evidence or ownership is changed.
const char* motionSubject(const Value& report) {
    const Value command = member(report, "command"), kind = member(report, "action_kind");
    if (equal(member(report, "type"), "simple_move") || equal(command, "moveby") || equal(command, "moveto") ||
        equal(command, "move-relative") || equal(command, "move-absolute") ||
        equal(command, "move-angle") || member(report, "move_kind").kind == Kind::STRING) return "Move";
    if (equal(command, "stop") || equal(kind, "stop")) return "Stop";
    if (equal(command, "enable") || equal(kind, "enable")) return "Enable";
    if (equal(command, "motor-release") || equal(kind, "release")) return "Motor release";
    if (equal(command, "alarm-clear") || equal(kind, "clear_alarm")) return "Alarm clear";
    if (equal(command, "position-clear") || equal(kind, "clear_position")) return "Position clear";
    return nullptr;
}
bool operationNumber(Writer& writer, const Value& report) {
    const Value operation = member(report, "operation_id");
    return !operation.begin || equal(operation, "0") ||
        (writer.text(" (operation ") && writer.scalar(operation) && writer.character(')'));
}
bool motionDetails(Writer& writer, const Value& report) {
    Value operation = member(report, "move_operation_id");
    if (operation.kind != Kind::NUMBER || equal(operation, "0")) operation = member(report, "read_operation_id");
    if (operation.kind != Kind::NUMBER || equal(operation, "0")) operation = member(report, "operation_id");
    return !operation.begin || equal(operation, "0") ||
        (writer.text("Details: @1 result ") && writer.scalar(operation) && writer.text(".\n"));
}
bool reviewReleaseHint(Writer& writer, const Value& report) {
    Value operation = member(report, "move_operation_id");
    if (operation.kind != Kind::NUMBER || equal(operation, "0")) {
        operation = member(report, "operation_id");
        return operation.kind != Kind::NUMBER || equal(operation, "0") ||
            (writer.text("After reviewing: release ") && writer.scalar(operation) && writer.text(".\n"));
    }
    return operation.kind != Kind::NUMBER || equal(operation, "0") ||
        (writer.text("Retained details: release ") && writer.scalar(operation) && writer.text(" after review; this frees result storage only.\n"));
}
const char* motionReason(const Value& reason) {
    if (equal(reason, "busy")) return "the application is still finishing an operation or delivering its result";
    if (equal(reason, "axis_conflict")) return "the motor has active or unresolved work; a confirmed stop is required before a new move";
    if (equal(reason, "queue_full")) return "the request queue is full";
    if (equal(reason, "results_full")) return "retained results are full; inspect and release a terminal result";
    if (equal(reason, "recovery_required")) return "host transport needs explicit recovery";
    if (equal(reason, "invalid") || equal(reason, "invalid_arguments")) return "check the arguments, motor setup and readiness";
    if (equal(reason, "unsupported") || equal(reason, "unavailable") || equal(reason, "unresolved")) return "this request is not supported with the current setup";
    if (equal(reason, "transport_error")) return "communication failed";
    if (equal(reason, "reply_error")) return "the drive reply or state did not pass validation";
    if (equal(reason, "unconfirmed_response")) return "the response could be an echo; acknowledgement was not confirmed";
    if (equal(reason, "deadline")) return "the operation timed out";
    if (equal(reason, "observation_limit")) return "the required completion was not seen during polling";
    if (equal(reason, "timing_unqualified")) return "communication timing could not be validated";
    return nullptr;
}
bool motionReport(Writer& writer, const Value& report, const char* subject) {
    const Value state = member(report, "state"), result = member(report, "result");
    const Value outcome = member(report, "outcome"), execution = member(report, "execution");
    const Value completion = member(report, "completion");
    const bool uncertain = equal(member(report, "uncertain"), "true") || equal(execution, "unknown") ||
        equal(member(report, "execution_unknown"), "true");
    const bool accepted = equal(member(report, "ok"), "true") && equal(result, "accepted") && !state.begin;
    const bool pending = equal(result, "pending") || equal(state, "active");
    const bool cancelled = equal(outcome, "cancelled");
    const bool noMotionSent = equal(member(report, "no_motion_sent"), "true");
    const bool interrupted = equal(member(report, "interrupted_by_stop"), "true");
    const bool observed = equal(member(report, "ok"), "true") && equal(state, "succeeded") &&
        equal(completion, "observed");
    const bool terminal = state.begin || outcome.begin || execution.begin;
    const char* disposition = uncertain ? " uncertain" : interrupted ? " interrupted by stop" :
        cancelled ? " cancelled locally" : accepted ? " accepted" : pending ? " in progress" :
        observed ? " complete" : !terminal ? " rejected" : " not confirmed complete";
    if (!writer.text(subject) || !writer.text(disposition) || !operationNumber(writer, report) || !writer.text(".\n")) return false;
    if (accepted || pending) return true;
    const Value roundingError = member(report, "rounding_error");
    if (roundingError.begin && !equal(roundingError, "0")) {
        if (!writer.text("Rounded native target: ") || !writer.scalar(member(report, "effective_native")) ||
            !writer.text(" command steps; rounding error: ") || !writer.scalar(roundingError) ||
            !writer.text(" step(s).\n")) return false;
    }
    if (equal(member(report, "already_at_target"), "true"))
        return writer.text("Requested target already satisfied; drive reports standstill. No new motion command was sent.\n") && motionDetails(writer, report);
    if (observed && !uncertain && !interrupted && !cancelled) {
        const char* observation = !std::strcmp(subject, "Move") ?
            (equal(member(report, "running_observed"), "true") ? "Drive reported running, then target reached." :
             equal(member(report, "position_confirmed"), "true") ?
                "Final position verified twice; drive reports stopped. RUNNING was missed between reads." :
                "Drive reported target reached; running was not observed.") :
            !std::strcmp(subject, "Stop") ? "Drive reported stopped." :
            !std::strcmp(subject, "Enable") ? "Drive reported enabled." :
            !std::strcmp(subject, "Motor release") ? "Drive reported released." :
            !std::strcmp(subject, "Alarm clear") ? "Drive reported alarm cleared." : "Drive position reset was read back.";
        if (!writer.text(observation) || !writer.character('\n')) return false;
    } else {
        const Value message = member(report, "message");
        Value reason = member(report, "error");
        if (!reason.begin) reason = terminal ? outcome : result;
        if (message.kind == Kind::STRING && message.begin != message.end) {
            if (!writer.string(message) || !writer.character('\n')) return false;
        } else if (reason.begin && !cancelled) {
            const char* explanation = motionReason(reason);
            if (!writer.text("Reason: ") || !(explanation ? writer.text(explanation) : writer.scalar(reason)) || !writer.text(".\n")) return false;
        }
        if (uncertain && !writer.text(equal(execution, "acknowledged") ?
            "Command acknowledged; final completion is uncertain. Do not replay automatically.\n" :
            "Execution is unknown. Do not repeat the write.\n")) return false;
        if (noMotionSent && !writer.text("No motion command was sent.\n")) return false;
        if (interrupted) {
            if (!writer.text("Check the separate stop result; interruption does not prove the motor stopped.\n")) return false;
        } else if (cancelled && !writer.text("Local cancellation does not stop the motor.\n")) return false;
        if (!cancelled && !noMotionSent && terminal && !writer.text(equal(completion, "observed") ?
            "Completion was observed; the operation still has an error.\n" : "Motion/state completion was not observed.\n")) return false;
        if (!uncertain && equal(execution, "acknowledged") && !writer.text("Command acknowledged by the drive.\n")) return false;
        if (equal(member(report, "setup_execution"), "acknowledged") && !writer.text("Motion settings were acknowledged.\n")) return false;
    }
    const Value alarm = member(report, "raw_alarm");
    if (equal(member(report, "observation_known"), "true") && alarm.kind == Kind::NUMBER) {
        if (equal(alarm, "0")) { if (!writer.text("No drive alarm reported.\n")) return false; }
        else if (!writer.text("Drive alarm code: ") || !writer.scalar(alarm) || !writer.text(".\n")) return false;
    }
    const Value suppliedHint = member(report, "hint");
    if (suppliedHint.kind == Kind::STRING && suppliedHint.begin != suppliedHint.end &&
        (!writer.text("Next: ") || !writer.string(suppliedHint) || !writer.character('\n'))) return false;
    if (!motionDetails(writer, report)) return false;
    if (equal(member(report, "type"), "simple_move") && equal(member(report, "ok"), "false") &&
        !equal(member(report, "move_operation_id"), "0"))
        return reviewReleaseHint(writer, report);
    return true;
}

Value element(const Value& array, unsigned index) {
    if (array.kind != Kind::ARRAY) return Value();
    Reader reader(array.begin + 1, array.end - 1);
    Value value;
    for (unsigned i = 0; reader.value(value); ++i) {
        if (i == index) return value;
        if (!reader.take(',')) break;
    }
    return Value();
}
bool unsignedValue(const Value& value, uint32_t& output) {
    if (value.kind != Kind::NUMBER || value.begin == value.end) return false;
    uint32_t result = 0;
    for (const char* at = value.begin; at != value.end; ++at) {
        if (!digit(*at) || result > (UINT32_MAX - static_cast<unsigned>(*at - '0')) / 10) return false;
        result = result * 10 + static_cast<unsigned>(*at - '0');
    }
    output = result; return true;
}
bool setting(Writer& writer, const char* label, const Value& value, const char* suffix = "") {
    return writer.text(label) && writer.text(": ") && writer.scalar(value) && writer.text(suffix) && writer.character('\n');
}
bool namedSetting(Writer& writer, const char* label, const Value& raw, bool known,
                  const char* first, const char* second, unsigned firstCode = 0) {
    uint32_t code = 0;
    const char* name = known && unsignedValue(raw, code) ?
        code == firstCode ? first : code == firstCode + 1 ? second : nullptr : nullptr;
    return writer.text(label) && writer.text(": ") && writer.text(name ? name : "unknown") &&
        writer.text(" (readback ") && writer.scalar(raw) && writer.text(")\n");
}
bool configSettings(Writer& writer, const Value& raw, const Value& known, bool flat = false) {
    if (!setting(writer, "Microstep / subdivision", member(raw, "subdivision"), " (drive register value; steps/turn mapping unresolved)")) return false;
    return namedSetting(writer, "Direction", member(raw, "direction"), equal(member(known, flat ? "direction_known" : "direction"), "true"), "normal", "reversed") &&
        namedSetting(writer, "Word order", member(raw, "word_order"), equal(member(known, flat ? "word_order_known" : "word_order"), "true"), "high word first", "low word first") &&
        namedSetting(writer, "Control algorithm", member(raw, "algorithm"), equal(member(known, flat ? "algorithm_known" : "algorithm"), "true"), "open loop", "closed loop algorithm 1", 1) &&
        setting(writer, "Configured encoder resolution", member(raw, "encoder_resolution"), " (readback; not measured)") &&
        namedSetting(writer, "Software limit setting", member(raw, "soft_limit_enable"), equal(member(known, flat ? "soft_limit_known" : "soft_limit_enable"), "true"), "off", "after homing");
}
bool profileSettings(Writer& writer, const Value& words, const Value& order, bool orderKnown) {
    static const char* const labels[] = {"Starting speed", "Acceleration ramp time", "Deceleration ramp time", "Positioning speed"};
    for (unsigned i = 0; i < 4; ++i)
        if (!setting(writer, labels[i], element(words, i), i == 0 || i == 3 ? " rpm" : " ms")) return false;
    uint32_t first = 0, second = 0, wordOrder = 0;
    if (orderKnown && unsignedValue(order, wordOrder) && wordOrder <= 1 &&
        unsignedValue(element(words, 4), first) && first <= UINT16_MAX &&
        unsignedValue(element(words, 5), second) && second <= UINT16_MAX) {
        const uint32_t bits = wordOrder ? (second << 16) | first : (first << 16) | second;
        char value[16]; std::snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(bits));
        return writer.text("Stored target: ") && writer.text(value) && writer.text(" (unsigned native bits; signed/physical meaning unresolved)\n");
    }
    return setting(writer, "Target first register", element(words, 4)) &&
        setting(writer, "Target second register", element(words, 5)) &&
        writer.text("Target value: unavailable until word order is known.\n");
}
bool conciseFailure(Writer& writer, const Value& report) {
    Value reason = member(report, "message");
    if (reason.kind != Kind::STRING || reason.begin == reason.end) reason = member(report, "error");
    if (!reason.begin || equal(reason, "none")) reason = member(report, "status");
    if (!reason.begin) reason = member(report, "result");
    if (reason.begin && (!writer.text("Reason: ") || !writer.scalar(reason) || !writer.character('\n'))) return false;
    if ((equal(member(report, "uncertain"), "true") || equal(member(report, "execution_unknown"), "true") ||
         equal(member(report, "restore_unsettled"), "true")) &&
        !writer.text("Write outcome is uncertain. Do not repeat the write.\n")) return false;
    return motionDetails(writer, report);
}
bool readConfigReport(Writer& writer, const Value& report) {
    const Value config = member(report, "config");
    if (equal(member(report, "result"), "accepted"))
        return writer.text("Drive configuration read accepted") && operationNumber(writer, report) && writer.text(".\n");
    if (!equal(member(report, "ok"), "true") || config.kind != Kind::OBJECT)
        return writer.text("Drive configuration read failed; complete settings are unavailable.\n") && conciseFailure(writer, report);
    if (!writer.text("Drive configuration (readback)") || !operationNumber(writer, report) || !writer.text("\n") ||
        !configSettings(writer, member(config, "raw"), member(config, "known"))) return false;
    return writer.text("Motion speeds and ramps: settings\n") && motionDetails(writer, report);
}
bool profileReport(Writer& writer, const Value& report) {
    if (equal(member(report, "pending"), "true")) return writer.text("Position profile: operation pending. Inspect with motion-profile inspect.\n") &&
        (!equal(member(report, "execution_unknown"), "true") || writer.text("Previous write outcome is uncertain. Do not repeat the write.\n"));
    if (equal(member(report, "execution_unknown"), "true") || equal(member(report, "restore_unsettled"), "true") ||
        equal(member(report, "ok"), "false") || equal(member(report, "session_ok"), "false"))
        return writer.text("Position profile is not confirmed.\n") && conciseFailure(writer, report) && writer.text("Details: @1 motion-profile inspect\n");
    if (!equal(member(report, "saved"), "true")) return writer.text("No position profile snapshot. Use settings to read motor settings.\n");
    return writer.text(equal(member(report, "restored"), "true") ? "Position profile restored and read back.\n" : "Position profile (last readback)\n") &&
        profileSettings(writer, member(report, "current"), Value(), false) && writer.text("Details: @1 motion-profile inspect\n");
}
bool motorSettingsReport(Writer& writer, const Value& report) {
    if (equal(member(report, "pending"), "true")) return writer.text("Settings: reading motor...\n");
    if (!equal(member(report, "ok"), "true"))
        return writer.text("Settings read failed; a complete fresh snapshot is unavailable.\n") &&
            conciseFailure(writer, report) && reviewReleaseHint(writer, report);
    const Value actual = member(report, "actual");
    if (!writer.text("Motor settings (read from drive)\n")) return false;
    if (equal(member(actual, "config_known"), "true")) {
        if (!configSettings(writer, actual, actual, true)) return false;
    } else if (!writer.text("Drive configuration: unavailable.\n")) return false;
    if (equal(member(actual, "profile_known"), "true")) {
        if (!profileSettings(writer, member(actual, "profile"), member(actual, "word_order"),
            equal(member(actual, "config_known"), "true") && equal(member(actual, "word_order_known"), "true"))) return false;
    } else if (!writer.text("Position profile: unavailable.\n")) return false;
    if (!setting(writer, "Position speed limit at this subdivision", member(report, "maximum_position_rpm"), " rpm") ||
        !writer.text("Position policy: <=2000 rpm, <=200000 command increments/s, ramps 100..2000 ms.\n")) return false;
    const Value desired = member(report, "desired");
    if (desired.kind == Kind::OBJECT) {
        if (!writer.text("Next move (host preferences; applied when requested)\n") ||
            !setting(writer, "  Speed", member(desired, "speed_rpm"), " rpm") ||
            !setting(writer, "  Acceleration ramp time", member(desired, "acceleration"), " ms") ||
            !setting(writer, "  Deceleration ramp time", member(desired, "deceleration"), " ms")) return false;
        const Value scale = member(desired, "steps_per_turn");
        if (scale.kind == Kind::OBJECT && equal(member(scale, "numerator"), "0")) {
            if (!writer.text("  Host command steps/turn: unavailable\n")) return false;
        } else if (scale.kind == Kind::OBJECT) {
            if (!writer.text("  Host command steps/turn: ") || !writer.scalar(member(scale, "numerator")) ||
                !writer.character('/') || !writer.scalar(member(scale, "denominator")) ||
                !writer.text(" (host declaration; not inferred from subdivision)\n")) return false;
        } else if (!setting(writer, "  Host command steps/turn", equal(scale, "0") ? Value() : scale,
            scale.kind == Kind::NUMBER && !equal(scale, "0") ? " (host declaration; not inferred from subdivision)" : "")) return false;
    }
    return motionDetails(writer, report);
}
bool driveChanges(Writer& writer, const Value& report) {
    const Value rows = member(report, "progress");
    if (rows.kind != Kind::ARRAY) return true;
    Reader reader(rows.begin + 1, rows.end - 1); Value row;
    static const char* const labels[] = {"Direction", "Microstep / subdivision", "Word order", "Software limits",
        "Over-limit stop", "External trigger", "External position mode", "Positive limit", "Negative limit"};
    while (reader.value(row)) {
        uint32_t mask = 0; unsigned index = 0;
        if (unsignedValue(element(row, 0), mask))
            while (index < 9 && mask != (1u << index)) ++index;
        else index = 9;
        if (!writer.text(index < 9 ? labels[index] : "Setting") || !writer.text(": requested ") ||
            !writer.scalar(element(row, 3)) || !writer.text("; readback ") ||
            !(equal(element(row, 5), "true") ? writer.scalar(element(row, 6)) : writer.text("not confirmed")) ||
            !writer.character('\n')) return false;
        if (!reader.take(',')) break;
    }
    return true;
}
bool driveReport(Writer& writer, const Value& report) {
    const bool update = equal(member(report, "driver_kind"), "update");
    const bool confirmed = equal(member(report, "ok"), "true") && !equal(member(report, "uncertain"), "true");
    if (equal(member(report, "result"), "accepted")) return writer.text("Drive settings operation accepted") && operationNumber(writer, report) && writer.text(".\n");
    if (!writer.text(update ? "Drive settings update" : "Drive settings read") || !operationNumber(writer, report) ||
        !writer.text(confirmed ? " complete.\n" : " not confirmed.\n")) return false;
    if (!confirmed) return conciseFailure(writer, report) && (!update || driveChanges(writer, report));
    if (update) return driveChanges(writer, report) &&
        writer.text("Selected settings were checked by readback. Active behavior is not established by this result.\n") && motionDetails(writer, report);
    const Value observation = member(report, "observation"), raw = member(observation, "raw");
    uint32_t known = 0; unsignedValue(member(observation, "known_fields"), known);
    return setting(writer, "Microstep / subdivision", element(raw, 1), " (drive register value; steps/turn mapping unresolved)") &&
        namedSetting(writer, "Direction", element(raw, 0), known & 1, "normal", "reversed") &&
        namedSetting(writer, "Word order", element(raw, 2), known & 4, "high word first", "low word first") &&
        namedSetting(writer, "Software limit setting", element(raw, 3), known & 8, "off", "after homing") &&
        namedSetting(writer, "Over-limit stop", element(raw, 4), known & 16, "free parking (stop/torque meaning unresolved)", "emergency stop") &&
        namedSetting(writer, "External trigger", element(raw, 5), known & 32, "level", "rising edge") &&
        namedSetting(writer, "External position mode", element(raw, 6), known & 64, "relative", "absolute") &&
        writer.text("Active behavior and signed limit interpretation remain unconfirmed.\n") && motionDetails(writer, report);
}
} // namespace

bool renderHuman(const char* json, char* output, std::size_t capacity) noexcept {
    if (!output || !capacity) return false;
    output[0] = '\0';
    if (!json) return false;
    std::size_t length = 0;
    while (length < MAX_INPUT && json[length]) ++length;
    if (length == MAX_INPUT) return false;
    Reader reader(json, json + length);
    Value report;
    if (!reader.value(report) || report.kind != Kind::OBJECT) return false;
    reader.space();
    if (reader.at != json + length) return false;
    Writer writer(output, capacity);
    const Value command = member(report, "command");
    const bool settings = equal(member(report, "type"), "motor_settings");
    const bool configuration = equal(command, "read-config") || equal(member(report, "read_kind"), "config");
    const bool profile = equal(command, "motion-profile");
    const bool drive = equal(command, "driver") || equal(member(report, "driver_group"), "drive");
    if (settings || configuration || profile || drive) {
        const bool rendered = settings ? motorSettingsReport(writer, report) : configuration ? readConfigReport(writer, report) :
            profile ? profileReport(writer, report) : driveReport(writer, report);
        if (!rendered) { output[0] = '\0'; return false; }
        writer.finish(); return true;
    }
    const char* subject = motionSubject(report);
    if (subject) {
        if (!motionReport(writer, report, subject)) { output[0] = '\0'; return false; }
        writer.finish();
        return true;
    }
    if (equal(member(report, "type"), "traffic")) {
        // Dropping a best-effort diagnostic is preferable to showing an event
        // whose byte count, completion flag or error was silently cut off.
        if (!trafficReport(writer, report)) { output[0] = '\0'; return false; }
        writer.finish();
        return true;
    }
    if (!heading(writer, report)) { output[0] = '\0'; return false; }
    // These fields remain before potentially large evidence arrays. If the
    // outcome itself cannot fit, report formatting failure rather than hide it.
    for (const auto name : SUMMARY) {
        const Value value = member(report, name);
        if (value.begin && !field(writer, key(name), value, 0)) { output[0] = '\0'; return false; }
    }
    const Value context = member(report, "context");
    if (context.kind == Kind::OBJECT) {
        if (!writer.text("Operation context:\n")) { output[0] = '\0'; return false; }
        for (const auto name : SUMMARY) {
            const Value value = member(context, name);
            if (value.begin && !field(writer, key(name), value, 1)) { output[0] = '\0'; return false; }
        }
    }
    if (!hint(writer, report)) { output[0] = '\0'; return false; }
    Reader fields(report.begin + 1, report.end - 1);
    Value name, value;
    while (!writer.truncated && fields.string(name) && fields.take(':') && fields.value(value)) {
        if (!metadata(name) && !summaryKey(name))
            field(writer, name, value, 0);
        if (!fields.take(',')) break;
    }
    writer.finish();
    return true;
}
}} // namespace MotorControlRSExample::Probe
