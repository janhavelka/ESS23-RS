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
        if (!writer.text("Hint: result ") || !writer.scalar(operation) || !writer.text("; release ") ||
            !writer.scalar(operation) || !writer.text(" after reviewing the terminal result.\n")) return false;
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
