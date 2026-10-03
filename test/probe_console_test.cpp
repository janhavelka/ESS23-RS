// SPDX-License-Identifier: MIT
#include "../examples/probe_cli/ProbeConsole.h"

#include <cassert>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace Probe = MotorControlRSExample::Probe;
namespace Rtu = MotorControlRSExample::Rtu;
namespace Ess = MotorControlRS::ESS_RS;

namespace {

struct Fake {
    Probe::Snapshot data;
    Probe::Action probeAction = Probe::Action::OK, recoverAction = Probe::Action::OK;
    unsigned snapshots = 0, probes = 0, recoveries = 0, resets = 0;
    uint32_t id = 0;
    uint8_t address = 0;
    std::vector<std::string> lines;

    static void emit(void* context, const char* line, std::size_t length) {
        Fake& self = *static_cast<Fake*>(context);
        assert(length < Probe::OUTPUT_CAPACITY);
        assert(length > 1 && line[0] == '{' && line[length - 1] == '}');
        self.lines.emplace_back(line, length);
        assert(self.lines.back().find('\n') == std::string::npos);
    }
    static void snapshot(void* context, Probe::Snapshot& output) {
        Fake& self = *static_cast<Fake*>(context);
        ++self.snapshots;
        output = self.data;
    }
    static Probe::Action probe(void* context, uint32_t id, uint8_t address) {
        Fake& self = *static_cast<Fake*>(context);
        ++self.probes; self.id = id; self.address = address;
        return self.probeAction;
    }
    static Probe::Action recover(void* context) {
        Fake& self = *static_cast<Fake*>(context);
        ++self.recoveries; return self.recoverAction;
    }
    static void reset(void* context) { ++static_cast<Fake*>(context)->resets; }
    Probe::Host host() {
        Probe::Host result;
        result.context = this; result.emitLine = emit; result.snapshot = snapshot;
        result.startProbe = probe; result.recover = recover; result.resetStats = reset;
        return result;
    }
    void contains(const char* fragment) const {
        assert(!lines.empty() && lines.back().find(fragment) != std::string::npos);
    }
    void untouched() const { assert(probes == 0 && recoveries == 0 && resets == 0); }
};

void send(Probe::Console& console, const std::string& input) {
    for (char c : input) console.feed(c);
}

void testFramingAndIds() {
    Fake fake;
    Probe::Console console(fake.host());
    send(console, "\r\n \t\n"); assert(fake.lines.empty());
    send(console, "@42 ver"); assert(fake.lines.empty());
    send(console, "sion\r\n"); assert(fake.lines.size() == 1);
    fake.contains("\"id\":42"); fake.contains("\"product\":\"MotorControl-RS\"");
    fake.contains("\"protocol\":1"); fake.contains("\"profile\":\"ess_rs\"");
    send(console, "@4294967295\tstatus\n"); fake.contains("\"id\":4294967295");
    send(console, "health\n"); fake.contains("\"id\":3");
    assert(fake.snapshots == 2); fake.untouched();
}

void testInvalidInputHasNoEffects() {
    const char* invalid[] = {
        "probe 0", "probe 248", "probe -1", "probe +1", "probe 1.0", "probe 1junk",
        "probe 0x01", "probe 4294967296", "probe 999999999999999999999999999999999999",
        "probe 1 2", "probe 1 2 3 4", "@1 probe 1 2", "@0 probe", "@4294967296 probe",
        "@-1 probe", "@+1 probe", "@1.0 probe", "@ probe", "@1", "health check",
        "recover now", "reset motor", "stats clear", "help not_a_command", "version extra",
        "read 0", "write 1", "move 1", "probe;reset", "PROBE", "settings junk", "memory junk"
    };
    for (const char* input : invalid) {
        Fake fake; Probe::Console console(fake.host());
        send(console, std::string(input) + "\n");
        assert(fake.lines.size() == 1); fake.contains("\"ok\":false");
        assert(fake.snapshots == 0); fake.untouched();
    }
}

void testOverflowAndControlDiscardWholeLine() {
    Fake fake; Probe::Console console(fake.host());
    send(console, "probe" + std::string(200, ' ') + "1\n");
    fake.contains("line_too_long"); assert(fake.snapshots == 0); fake.untouched();
    send(console, std::string("probe\0 1\n", 9));
    fake.contains("invalid_input"); fake.untouched();
    send(console, "pro\bbe\n"); fake.contains("invalid_input"); fake.untouched();
    send(console, std::string("probe ") + static_cast<char>(0xFF) + "1\n");
    fake.contains("invalid_input"); fake.untouched();
    send(console, "@7 probe 247\n");
    assert(fake.probes == 1 && fake.address == 247 && fake.id == 7);
    fake.contains("\"result\":\"accepted\"");
}

void testProbeAdmissionAndAliases() {
    Fake fake; fake.data.address = 17;
    Probe::Console console(fake.host());
    send(console, "@8 ping\n"); assert(fake.probes == 1 && fake.address == 17 && fake.id == 8);
    fake.contains("\"command\":\"probe\"");
    fake.probeAction = Probe::Action::BUSY;
    send(console, "@9 probe 1\n"); fake.contains("\"result\":\"busy\""); fake.contains("\"ok\":false");
    fake.probeAction = Probe::Action::RECOVERY_REQUIRED;
    send(console, "probe\n"); fake.contains("recovery_required");
    fake.probeAction = Probe::Action::UNAVAILABLE;
    send(console, "probe\n"); fake.contains("unavailable");
    fake.probeAction = Probe::Action::FAILED;
    send(console, "probe\n"); fake.contains("failed");
    fake.data.address = 0;
    send(console, "probe\n"); fake.contains("invalid_address"); assert(fake.probes == 5);
    assert(fake.recoveries == 0 && fake.resets == 0);
}

void testCachedHealthAndStatus() {
    Fake fake; Probe::Console console(fake.host());
    send(console, "health\n"); fake.contains("\"communication\":\"unavailable\"");
    fake.data.ready = true;
    send(console, "health\n"); fake.contains("\"communication\":\"unknown\""); fake.contains("\"age_ms\":null");
    fake.contains("\"probe_address\":null");
    fake.data.recoveryRequired = true;
    send(console, "health\n"); fake.contains("\"communication\":\"failed\"");
    fake.data.recoveryRequired = false;
    fake.data.probeKnown = fake.data.probeOk = true;
    fake.data.probeAddress = 17;
    fake.data.rawModel = 60; fake.data.ageMs = 5000;
    send(console, "health\n"); fake.contains("\"communication\":\"current\"");
    fake.contains("\"probe_address\":17");
    fake.contains("\"readiness\":\"unknown\""); fake.contains("\"alarms\":\"unknown\"");
    fake.data.ageMs = 5001;
    send(console, "health\n"); fake.contains("\"communication\":\"stale\"");
    fake.data.recoveryRequired = true;
    send(console, "health\n"); fake.contains("\"communication\":\"failed\"");
    fake.data.probeOk = false;
    send(console, "status\n"); fake.contains("\"raw_model\":null"); fake.contains("\"last_probe_ok\":false");
    fake.data.probeOk = true; fake.data.busy = true; fake.data.timingQualified = false;
    fake.data.transmitEnabled = true;
    fake.data.codecChecked = true; fake.data.codec = MotorControlRS::Status(MotorControlRS::Err::EXCEPTION, 2, "");
    send(console, "status\n"); fake.contains("\"raw_model\":60"); fake.contains("\"busy\":true");
    fake.contains("\"probe_address\":17");
    fake.contains("\"transmit_enabled\":true"); fake.contains("\"codec\":\"EXCEPTION\""); fake.contains("\"detail\":2");
    fake.contains("\"timing_qualified\":false"); fake.untouched();
}

void testHelpConfigMemoryAndStats() {
    Fake fake; Probe::Console console(fake.host());
    send(console, "help\n"); fake.contains("\"commands\":["); fake.contains("\"probe\"");
    assert(fake.lines.back().find("\"move\"") == std::string::npos);
    send(console, "help probe\n"); fake.contains("\"bus_traffic\":true");
    send(console, "help recover\n"); fake.contains("recover_host_transport_only"); fake.contains("\"bus_traffic\":false");
    fake.data.baud = 38400; fake.data.responseTimeoutUs = 500000;
    fake.data.replyGapUs = 304; fake.data.gap15Us = 750; fake.data.gap35Us = 1750;
    send(console, "settings\n"); fake.contains("\"baud\":38400"); fake.contains("\"device_settings\":\"unknown\"");
    fake.contains("\"reply_gap_us\":304"); fake.contains("\"gap15_us\":750"); fake.contains("\"gap35_us\":1750");
    fake.data.memoryValid = true; fake.data.internalFree = 123456; fake.data.psramFree = 7000000;
    fake.data.internalLargest = 32000; fake.data.psramLargest = 6000000;
    send(console, "memory\n"); fake.contains("\"valid\":true"); fake.contains("\"psram_free\":7000000");
    fake.contains("\"internal_largest\":32000"); fake.contains("\"psram_largest\":6000000");
    fake.data.stats.started = 100; fake.data.maxPollGapUs = 999; fake.data.captureFaults = 2;
    send(console, "stats\n"); fake.contains("\"started\":100"); fake.contains("\"max_poll_gap_us\":999");
    fake.contains("\"capture_faults\":2"); fake.untouched();
    send(console, "stats reset\nreset\n"); assert(fake.resets == 2 && fake.recoveries == 0 && fake.probes == 0);
    fake.contains("\"result\":\"done\"");
    fake.recoverAction = Probe::Action::BUSY;
    send(console, "recover\n"); fake.contains("\"result\":\"busy\"");
    assert(fake.recoveries == 1 && fake.probes == 0);
    fake.recoverAction = Probe::Action::OK;
    send(console, "recover\n"); fake.contains("\"result\":\"done\"");
}

void testProbeResultEvidence() {
    Fake fake; Probe::Console console(fake.host());
    const uint8_t request[] = {1, 3, 0, 0, 0, 1, 0x84, 0x0A};
    const uint8_t response[] = {1, 3, 2, 0, 0x3C, 0xB8, 0x55};
    Probe::ProbeResult result;
    result.transport.reason = Rtu::Reason::FRAME;
    result.transport.startedUs = 1000; result.transport.endedUs = 5000;
    result.transport.txAccepted = 8; result.transport.rxLength = 7;
    result.tx = request; result.txLength = sizeof(request);
    result.rx = response; result.rxLength = sizeof(response);
    result.codecChecked = true;
    result.codec = Ess::parseProbe(response, sizeof(response), 1, result.rawModel, &result.frameError);
    result.timingValid = true; result.txUncertaintyUs = 4; result.maxRxUncertaintyUs = 8;
    result.txEndUs = 2500; result.firstRxStartUs = 4500;
    console.reportProbe(99, 1, result);
    fake.contains("\"type\":\"probe\""); fake.contains("\"id\":99"); fake.contains("\"ok\":true");
    fake.contains("\"raw_model\":60"); fake.contains("\"duration_us\":4000");
    fake.contains("\"tx_hex\":\"010300000001840A\""); fake.contains("\"rx_hex\":\"010302003CB855\"");
    fake.contains("\"max_rx_uncertainty_us\":8"); fake.contains("\"timing_valid\":true");
    result.codec = MotorControlRS::Status(MotorControlRS::Err::EXCEPTION, 231, "untrusted text\"\n");
    result.frameError = Ess::FrameError::EXCEPTION;
    console.reportProbe(100, 1, result);
    fake.contains("\"ok\":false"); fake.contains("\"codec\":\"EXCEPTION\"");
    fake.contains("\"detail\":231"); fake.contains("\"raw_model\":null");
    assert(fake.lines.back().find("untrusted") == std::string::npos);
    result.codecChecked = false; result.transport.reason = Rtu::Reason::NO_RESPONSE;
    console.reportProbe(101, 1, result);
    fake.contains("\"codec\":\"NOT_CHECKED\""); fake.contains("\"ok\":false");
    fake.contains("\"detail\":0"); fake.contains("\"frame_error\":0");
    assert(fake.snapshots == 0); fake.untouched();
}

void testMaximumOutputAndRawBounds() {
    Fake fake; Probe::Console console(fake.host());
    const uint32_t max = std::numeric_limits<uint32_t>::max();
    const uint64_t max64 = std::numeric_limits<uint64_t>::max();
    fake.data.uptimeMs = max64; fake.data.ageMs = max64; fake.data.staleAfterMs = max;
    fake.data.ready = fake.data.probeKnown = fake.data.probeOk = true;
    fake.data.rawModel = 65535; fake.data.maxPollGapUs = max64;
    fake.data.stats.started = fake.data.stats.frames = fake.data.stats.failed = max;
    fake.data.stats.timeouts = fake.data.stats.cancelled = fake.data.stats.rxBytes = max;
    fake.data.stats.discarded = fake.data.stats.echoBytes = fake.data.stats.traceOverwritten = max;
    fake.data.captureFaults = fake.data.rxErrors = max;
    send(console, "@4294967295 status\n@4294967295 stats\n");
    Probe::ProbeResult result;
    result.transport.reason = Rtu::Reason::FRAME; result.codecChecked = true;
    result.transport.endedUs = max64; result.transport.txAccepted = result.transport.rxLength = 65535;
    result.codec.detail = std::numeric_limits<int32_t>::min();
    result.rawModel = 65535;
    const uint8_t bytes[65] = {};
    result.tx = result.rx = bytes; result.txLength = 9; result.rxLength = 65;
    result.txUncertaintyUs = result.maxRxUncertaintyUs = max;
    result.txEndUs = result.firstRxStartUs = max64;
    console.reportProbe(max, 247, result); fake.contains("\"raw_truncated\":true");
    fake.contains("18446744073709551615");
    result.txLength = 8; result.rxLength = 64; result.transport.rxTruncated = true;
    console.reportProbe(max, 247, result); fake.contains("\"raw_truncated\":true");
    result.tx = result.rx = nullptr;
    console.reportProbe(max, 247, result); fake.contains("\"tx_hex\":\"\""); fake.contains("\"rx_hex\":\"\"");
    fake.contains("\"raw_truncated\":true"); fake.untouched();
}

} // namespace

int main() {
    testFramingAndIds();
    testInvalidInputHasNoEffects();
    testOverflowAndControlDiscardWholeLine();
    testProbeAdmissionAndAliases();
    testCachedHealthAndStatus();
    testHelpConfigMemoryAndStats();
    testProbeResultEvidence();
    testMaximumOutputAndRawBounds();
}
