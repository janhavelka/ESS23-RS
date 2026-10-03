// SPDX-License-Identifier: MIT
#include "ProbeConsole.h"
#include "MotorControlRS/Version.h"

#include <cstdio>
#include <cstring>
#include <limits>

namespace MotorControlRSExample { namespace Probe {
namespace {

enum class Command : uint8_t { HELP, VERSION, CONFIG, STATUS, HEALTH, STATS, PROBE, RECOVER, RESET, MEMORY };
struct Entry { const char* name; Command command; const char* syntax; const char* effect; bool bus; };
const Entry COMMANDS[] = {
    {"help", Command::HELP, "help [command]", "show_callable_commands", false},
    {"version", Command::VERSION, "version", "show_build", false},
    {"ver", Command::VERSION, "ver", "show_build", false},
    {"config", Command::CONFIG, "config", "show_host_settings", false},
    {"settings", Command::CONFIG, "settings", "show_host_settings", false},
    {"status", Command::STATUS, "status", "show_cached_observations", false},
    {"health", Command::HEALTH, "health", "show_cached_presence", false},
    {"stats", Command::STATS, "stats [reset]", "show_or_clear_host_counters", false},
    {"probe", Command::PROBE, "probe [address]", "read_model_word_only", true},
    {"ping", Command::PROBE, "ping [address]", "read_model_word_only", true},
    {"recover", Command::RECOVER, "recover", "recover_host_transport_only", false},
    {"reset", Command::RESET, "reset", "clear_host_counters_only", false},
    {"memory", Command::MEMORY, "memory", "show_cached_memory", false}
};

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
void hex(const uint8_t* bytes, std::size_t length, char* output, std::size_t capacity) {
    static const char DIGITS[] = "0123456789ABCDEF";
    const std::size_t count = bytes ? (length < (capacity - 1) / 2 ? length : (capacity - 1) / 2) : 0;
    for (std::size_t i = 0; i < count; ++i) {
        output[2 * i] = DIGITS[bytes[i] >> 4];
        output[2 * i + 1] = DIGITS[bytes[i] & 15];
    }
    output[2 * count] = '\0';
}
const char* actionName(Action value) {
    switch (value) {
    case Action::OK: return "accepted";
    case Action::BUSY: return "busy";
    case Action::RECOVERY_REQUIRED: return "recovery_required";
    case Action::UNAVAILABLE: return "unavailable";
    case Action::FAILED: return "failed";
    }
    return "failed";
}

} // namespace

void Console::emit() noexcept {
    if (host_.emitLine) host_.emitLine(host_.context, output_, std::strlen(output_));
}

void Console::error(uint32_t id, const char* command, const char* reason) noexcept {
    std::snprintf(output_, sizeof(output_),
        "{\"type\":\"reply\",\"profile\":\"ess_rs\",\"id\":%lu,\"command\":\"%s\",\"ok\":false,\"result\":\"%s\"}",
        static_cast<unsigned long>(id), command, reason);
    emit();
}

void Console::action(uint32_t id, const char* command, Action result, uint8_t address) noexcept {
    std::snprintf(output_, sizeof(output_),
        "{\"type\":\"reply\",\"profile\":\"ess_rs\",\"id\":%lu,\"command\":\"%s\",\"ok\":%s,\"result\":\"%s\",\"address\":%u}",
        static_cast<unsigned long>(id), command, boolean(result == Action::OK),
        result == Action::OK && !address ? "done" : actionName(result), address);
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
    char* tokens[4] = {};
    std::size_t count = 0;
    char* next = line_;
    while (*next) {
        while (*next == ' ' || *next == '\t') ++next;
        if (!*next) break;
        if (count == 4) { error(0, "input", "too_many_arguments"); return; }
        tokens[count++] = next;
        while (*next && *next != ' ' && *next != '\t') ++next;
        if (*next) *next++ = '\0';
    }
    if (!count) return;
    uint32_t id = nextId_;
    if (++nextId_ == 0) nextId_ = 1;
    std::size_t first = 0;
    if (tokens[0][0] == '@') {
        if (!number(tokens[0] + 1, id) || id == 0) { error(0, "input", "invalid_id"); return; }
        first = 1;
        if (count == first) { error(id, "input", "missing_command"); return; }
    }
    const Entry* entry = find(tokens[first]);
    if (!entry) { error(id, "unknown", "unknown_command"); return; }
    // The asynchronous terminal event uses the canonical probe name too.
    if (entry->command == Command::PROBE) entry = find("probe");
    const std::size_t args = count - first - 1;
    const char* arg = args ? tokens[first + 1] : nullptr;
    if (args > 1 || (args && entry->command != Command::HELP &&
        entry->command != Command::STATS && entry->command != Command::PROBE)) {
        error(id, entry->name, "invalid_arguments"); return;
    }
    if (!host_.snapshot || !host_.startProbe || !host_.recover || !host_.resetStats) {
        error(id, entry->name, "unavailable"); return;
    }

    // Validate every argument before querying application state or admitting work.
    uint32_t address = 0;
    if (entry->command == Command::PROBE && arg &&
        (!number(arg, address) || address < 1 || address > 247)) {
        error(id, entry->name, "invalid_address"); return;
    }
    if (entry->command == Command::STATS && arg && std::strcmp(arg, "reset") != 0) {
        error(id, entry->name, "invalid_arguments"); return;
    }
    const Entry* described = entry->command == Command::HELP && arg ? find(arg) : nullptr;
    if (entry->command == Command::HELP && arg && !described) {
        error(id, entry->name, "unknown_command"); return;
    }
    if (entry->command == Command::HELP) {
        if (described) {
            std::snprintf(output_, sizeof(output_),
                "{\"type\":\"reply\",\"profile\":\"ess_rs\",\"id\":%lu,\"command\":\"help\",\"ok\":true,\"syntax\":\"%s\",\"effect\":\"%s\",\"bus_traffic\":%s}",
                static_cast<unsigned long>(id), described->syntax, described->effect, boolean(described->bus));
        } else {
            const int prefix = std::snprintf(output_, sizeof(output_),
                "{\"type\":\"reply\",\"profile\":\"ess_rs\",\"id\":%lu,\"command\":\"help\",\"ok\":true,\"commands\":[", static_cast<unsigned long>(id));
            if (prefix < 0 || static_cast<std::size_t>(prefix) >= sizeof(output_)) { error(id, "help", "output_full"); return; }
            std::size_t used = static_cast<std::size_t>(prefix);
            for (const Entry& item : COMMANDS) {
                const int written = std::snprintf(output_ + used, sizeof(output_) - used,
                    "%s\"%s\"", &item == COMMANDS ? "" : ",", item.name);
                if (written < 0 || static_cast<std::size_t>(written) >= sizeof(output_) - used) { error(id, "help", "output_full"); return; }
                used += static_cast<std::size_t>(written);
            }
            if (sizeof(output_) - used < 3) { error(id, "help", "output_full"); return; }
            std::snprintf(output_ + used, sizeof(output_) - used, "]}");
        }
        emit(); return;
    }
    if (entry->command == Command::VERSION) {
        std::snprintf(output_, sizeof(output_),
            "{\"type\":\"reply\",\"profile\":\"ess_rs\",\"id\":%lu,\"command\":\"%s\",\"ok\":true,\"product\":\"MotorControl-RS\",\"protocol\":1,\"version\":\"%s\"}",
            static_cast<unsigned long>(id), entry->name, MotorControlRS::VERSION);
        emit(); return;
    }
    if (entry->command == Command::RESET || (entry->command == Command::STATS && arg)) {
        host_.resetStats(host_.context); action(id, entry->name, Action::OK); return;
    }
    if (entry->command == Command::RECOVER) {
        action(id, entry->name, host_.recover(host_.context)); return;
    }

    Snapshot data;
    host_.snapshot(host_.context, data);
    if (entry->command == Command::PROBE) {
        if (!arg) address = data.address;
        if (address < 1 || address > 247) { error(id, entry->name, "invalid_address"); return; }
        action(id, entry->name, host_.startProbe(host_.context, id, static_cast<uint8_t>(address)), static_cast<uint8_t>(address));
        return;
    }
    char rawModel[8] = "null", age[24] = "null", probeAddress[5] = "null";
    if (data.probeKnown) std::snprintf(age, sizeof(age), "%llu", static_cast<unsigned long long>(data.ageMs));
    if (data.probeKnown) std::snprintf(probeAddress, sizeof(probeAddress), "%u", data.probeAddress);
    if (data.probeKnown && data.probeOk) std::snprintf(rawModel, sizeof(rawModel), "%u", data.rawModel);
    switch (entry->command) {
    case Command::CONFIG:
        std::snprintf(output_, sizeof(output_),
            "{\"type\":\"reply\",\"profile\":\"ess_rs\",\"id\":%lu,\"command\":\"%s\",\"ok\":true,\"address\":%u,\"baud\":%lu,\"format\":\"8N1\",\"response_timeout_us\":%lu,\"reply_gap_us\":%lu,\"gap15_us\":%lu,\"gap35_us\":%lu,\"stale_after_ms\":%lu,\"ready\":%s,\"timing_qualified\":%s,\"device_settings\":\"unknown\"}",
            static_cast<unsigned long>(id), entry->name, data.address, static_cast<unsigned long>(data.baud),
            static_cast<unsigned long>(data.responseTimeoutUs), static_cast<unsigned long>(data.replyGapUs),
            static_cast<unsigned long>(data.gap15Us), static_cast<unsigned long>(data.gap35Us),
            static_cast<unsigned long>(data.staleAfterMs), boolean(data.ready), boolean(data.timingQualified));
        break;
    case Command::STATUS:
        std::snprintf(output_, sizeof(output_),
            "{\"type\":\"reply\",\"profile\":\"ess_rs\",\"id\":%lu,\"command\":\"status\",\"ok\":true,\"uptime_ms\":%llu,\"ready\":%s,\"timing_qualified\":%s,\"busy\":%s,\"transmit_enabled\":%s,\"recovery_required\":%s,\"phase\":\"%s\",\"transport\":\"%s\",\"codec\":\"%s\",\"detail\":%ld,\"frame_error\":%u,\"last_probe_known\":%s,\"last_probe_ok\":%s,\"probe_address\":%s,\"raw_model\":%s,\"age_ms\":%s,\"stale_after_ms\":%lu}",
            static_cast<unsigned long>(id), static_cast<unsigned long long>(data.uptimeMs), boolean(data.ready), boolean(data.timingQualified),
            boolean(data.busy), boolean(data.transmitEnabled), boolean(data.recoveryRequired), Rtu::phaseName(data.phase), Rtu::reasonName(data.transport),
            data.codecChecked ? MotorControlRS::errToString(data.codec.code) : "NOT_CHECKED",
            data.codecChecked ? static_cast<long>(data.codec.detail) : 0L,
            data.codecChecked ? static_cast<unsigned>(data.frameError) : 0U,
            boolean(data.probeKnown), boolean(data.probeKnown && data.probeOk), probeAddress, rawModel, age, static_cast<unsigned long>(data.staleAfterMs));
        break;
    case Command::HEALTH: {
        const char* communication = !data.ready ? "unavailable" : data.recoveryRequired ? "failed" :
            !data.probeKnown ? "unknown" : !data.probeOk ? "failed" :
            data.ageMs > data.staleAfterMs ? "stale" : "current";
        std::snprintf(output_, sizeof(output_),
            "{\"type\":\"reply\",\"profile\":\"ess_rs\",\"id\":%lu,\"command\":\"health\",\"ok\":true,\"communication\":\"%s\",\"probe_address\":%s,\"age_ms\":%s,\"stale_after_ms\":%lu,\"readiness\":\"unknown\",\"alarms\":\"unknown\",\"state\":\"unknown\",\"identity\":\"%s\"}",
            static_cast<unsigned long>(id), communication, probeAddress, age, static_cast<unsigned long>(data.staleAfterMs), data.probeKnown && data.probeOk ? "responder_only" : "unknown");
        break;
    }
    case Command::STATS:
        std::snprintf(output_, sizeof(output_),
            "{\"type\":\"reply\",\"profile\":\"ess_rs\",\"id\":%lu,\"command\":\"stats\",\"ok\":true,\"started\":%lu,\"frames\":%lu,\"failed\":%lu,\"timeouts\":%lu,\"cancelled\":%lu,\"rx_bytes\":%lu,\"discarded\":%lu,\"echo_bytes\":%lu,\"trace_overwritten\":%lu,\"max_poll_gap_us\":%llu,\"capture_faults\":%lu,\"rx_errors\":%lu}",
            static_cast<unsigned long>(id), static_cast<unsigned long>(data.stats.started), static_cast<unsigned long>(data.stats.frames),
            static_cast<unsigned long>(data.stats.failed), static_cast<unsigned long>(data.stats.timeouts), static_cast<unsigned long>(data.stats.cancelled),
            static_cast<unsigned long>(data.stats.rxBytes), static_cast<unsigned long>(data.stats.discarded), static_cast<unsigned long>(data.stats.echoBytes), static_cast<unsigned long>(data.stats.traceOverwritten),
            static_cast<unsigned long long>(data.maxPollGapUs), static_cast<unsigned long>(data.captureFaults), static_cast<unsigned long>(data.rxErrors));
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
    emit();
}

void Console::reportProbe(uint32_t id, uint8_t address, const ProbeResult& result) noexcept {
    const bool ok = result.transport.reason == Rtu::Reason::FRAME && result.codecChecked && result.codec.isOk();
    const uint64_t duration = result.transport.endedUs >= result.transport.startedUs ? result.transport.endedUs - result.transport.startedUs : 0;
    char model[8] = "null";
    char txHex[PROBE_TX_CAPACITY * 2 + 1], rxHex[PROBE_RX_CAPACITY * 2 + 1];
    hex(result.tx, result.txLength, txHex, sizeof(txHex));
    hex(result.rx, result.rxLength, rxHex, sizeof(rxHex));
    const bool truncated = result.transport.rxTruncated || result.txLength > PROBE_TX_CAPACITY || result.rxLength > PROBE_RX_CAPACITY ||
        (!result.tx && result.txLength) || (!result.rx && result.rxLength);
    if (ok) std::snprintf(model, sizeof(model), "%u", result.rawModel);
    std::snprintf(output_, sizeof(output_),
        "{\"type\":\"probe\",\"profile\":\"ess_rs\",\"id\":%lu,\"command\":\"probe\",\"ok\":%s,\"address\":%u,\"transport\":\"%s\",\"codec\":\"%s\",\"detail\":%ld,\"frame_error\":%u,\"raw_model\":%s,\"duration_us\":%llu,\"tx_bytes\":%u,\"rx_bytes\":%u,\"identity\":\"%s\",\"tx_hex\":\"%s\",\"rx_hex\":\"%s\",\"raw_truncated\":%s,\"timing_valid\":%s,\"tx_uncertainty_us\":%lu,\"max_rx_uncertainty_us\":%lu,\"tx_end_us\":%llu,\"first_rx_start_us\":%llu}",
        static_cast<unsigned long>(id), boolean(ok), address, Rtu::reasonName(result.transport.reason), result.codecChecked ? MotorControlRS::errToString(result.codec.code) : "NOT_CHECKED",
        result.codecChecked ? static_cast<long>(result.codec.detail) : 0L,
        result.codecChecked ? static_cast<unsigned>(result.frameError) : 0U, model, static_cast<unsigned long long>(duration),
        result.transport.txAccepted, result.transport.rxLength, ok ? "responder_only" : "unknown", txHex, rxHex, boolean(truncated), boolean(result.timingValid),
        static_cast<unsigned long>(result.txUncertaintyUs), static_cast<unsigned long>(result.maxRxUncertaintyUs),
        static_cast<unsigned long long>(result.txEndUs), static_cast<unsigned long long>(result.firstRxStartUs));
    emit();
}

}} // namespace MotorControlRSExample::Probe
