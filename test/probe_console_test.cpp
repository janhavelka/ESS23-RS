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
    Probe::LoadSnapshot loadData;
    Probe::Action probeAction = Probe::Action::OK, recoverAction = Probe::Action::OK;
    Probe::Action loadAction = Probe::Action::OK;
    unsigned snapshots = 0, probes = 0, recoveries = 0, resets = 0;
    unsigned loads = 0, loadChanges = 0;
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
    static Probe::Action load(void* context, const Probe::LoadSettings* requested,
                              Probe::LoadSnapshot& output) {
        Fake& self = *static_cast<Fake*>(context);
        ++self.loads;
        if (requested && self.loadAction == Probe::Action::OK) {
            ++self.loadChanges;
            self.loadData.settings = *requested;
        }
        output = self.loadData;
        return self.loadAction;
    }
    Probe::Host host(bool withLoad = false) {
        Probe::Host result;
        result.context = this; result.emitLine = emit; result.snapshot = snapshot;
        result.startProbe = probe; result.recover = recover; result.resetStats = reset;
        if (withLoad) result.load = load;
        return result;
    }
    void contains(const char* fragment) const {
        assert(!lines.empty() && lines.back().find(fragment) != std::string::npos);
    }
    void untouched() const { assert(probes == 0 && recoveries == 0 && resets == 0 && loadChanges == 0); }
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
        "read 0", "write 1", "move 1", "probe;reset", "PROBE", "settings junk", "memory junk",
        "@4 probe 1 2 3", "@4 reset 0 0 0", "@4 help load extra", "@4 stats reset extra"
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

void testLoadQueryAndSettings() {
    Fake fake; fake.loadData.ready = true; fake.loadData.timer = true;
    fake.loadData.elapsedUs = 123456; fake.loadData.workUs = 20000;
    fake.loadData.workIterations = 10; fake.loadData.consoleLines = 8; fake.loadData.consoleDropped = 2;
    fake.loadData.captureUs = 5000; fake.loadData.captureSamples = 6000;
    fake.loadData.ownerGapMaxUs = 5002; fake.loadData.captureGapMaxUs = 23;
    fake.loadData.workStackFreeBytes = 2400;
    Probe::Console console(fake.host(true));
    send(console, "@4294967295 load 5000 20000 256\n");
    assert(fake.loads == 1 && fake.loadChanges == 1);
    assert(fake.loadData.settings.workUs == 5000 && fake.loadData.settings.ownerDelayUs == 20000);
    assert(fake.loadData.settings.consoleBytes == 256);
    fake.contains("\"id\":4294967295"); fake.contains("\"result\":\"done\"");
    fake.contains("\"ready\":true"); fake.contains("\"capture_mode\":\"timer\"");
    fake.contains("\"workload_us\":5000"); fake.contains("\"owner_delay_us\":20000");
    fake.contains("\"console_bytes\":256"); fake.contains("\"elapsed_us\":123456");
    fake.contains("\"work_us\":20000"); fake.contains("\"work_iterations\":10");
    fake.contains("\"console_lines\":8"); fake.contains("\"console_dropped\":2");
    fake.contains("\"capture_us\":5000"); fake.contains("\"capture_samples\":6000");
    fake.contains("\"owner_gap_max_us\":5002"); fake.contains("\"capture_gap_max_us\":23");
    fake.contains("\"work_stack_free_bytes\":2400"); fake.contains("\"cpu_valid\":false");
    fake.contains("\"cpu0_busy_pct\":null"); fake.contains("\"cpu1_busy_pct\":null");
    fake.loadData.timer = false; fake.loadData.cpuValid = true;
    fake.loadData.cpu0BusyPct = 23; fake.loadData.cpu1BusyPct = 100;
    send(console, "@9 load\n");
    assert(fake.loads == 2 && fake.loadChanges == 1);
    fake.contains("\"id\":9"); fake.contains("\"capture_mode\":\"poll\"");
    fake.contains("\"workload_us\":5000"); fake.contains("\"cpu_valid\":true");
    fake.contains("\"cpu0_busy_pct\":23"); fake.contains("\"cpu1_busy_pct\":100");
    send(console, "load 0 0 0\n");
    assert(fake.loads == 3 && fake.loadChanges == 2);
    fake.contains("\"workload_us\":0"); fake.contains("\"owner_delay_us\":0");
    fake.contains("\"console_bytes\":0");
    assert(fake.probes == 0 && fake.recoveries == 0 && fake.resets == 0 && fake.snapshots == 0);
}

void testLoadValidationAndOptionalCallback() {
    const char* invalid[] = {
        "load 0", "load 0 0", "load 0 0 0 0", "@1 load 0 0 0 0",
        "load -1 0 0", "load +1 0 0", "load 0.0 0 0", "load 0x1 0 0",
        "load 5001 0 0", "load 0 20001 0", "load 0 0 257", "load 4294967296 0 0",
        "load 0 junk 0", "load 0 0 1x", "@-1 load 0 0 0"
    };
    for (const char* input : invalid) {
        Fake fake; Probe::Console console(fake.host(true));
        send(console, std::string(input) + "\n");
        fake.contains("\"ok\":false");
        assert(fake.lines.size() == 1 && fake.loads == 0 && fake.snapshots == 0);
        fake.untouched();
    }
    Fake fake; Probe::Console unavailable(fake.host());
    send(unavailable, "load\n"); fake.contains("\"result\":\"unavailable\"");
    send(unavailable, "load 0 0 0\n"); fake.contains("\"result\":\"unavailable\"");
    send(unavailable, "help load\n"); fake.contains("\"result\":\"unavailable\"");
    send(unavailable, "help\n");
    assert(fake.lines.back().find("\"load\"") == std::string::npos);
    assert(fake.loads == 0 && fake.snapshots == 0); fake.untouched();
    Probe::Console available(fake.host(true));
    send(available, "help\n"); fake.contains("\"load\"");
    send(available, "help load\n"); fake.contains("load [work_us owner_delay_us console_bytes]");
    fake.contains("\"bus_traffic\":false"); assert(fake.loads == 0); fake.untouched();
    for (Probe::Action result : {Probe::Action::BUSY, Probe::Action::UNAVAILABLE,
                                Probe::Action::RECOVERY_REQUIRED, Probe::Action::FAILED}) {
        fake.loadAction = result;
        send(available, "@31 load 1 2 3\n"); fake.contains("\"ok\":false"); fake.contains("\"id\":31");
        assert(fake.loadChanges == 0 && fake.snapshots == 0); fake.untouched();
    }
}

void testLoadMaximumOutput() {
    Fake fake; Probe::Console console(fake.host(true));
    const uint64_t max64 = std::numeric_limits<uint64_t>::max();
    fake.loadData.elapsedUs = fake.loadData.workUs = fake.loadData.workIterations = max64;
    fake.loadData.consoleLines = fake.loadData.consoleDropped = max64;
    fake.loadData.captureUs = fake.loadData.captureSamples = max64;
    fake.loadData.ownerGapMaxUs = fake.loadData.captureGapMaxUs = max64;
    fake.loadData.workStackFreeBytes = std::numeric_limits<uint32_t>::max();
    fake.loadData.cpuValid = true; fake.loadData.cpu0BusyPct = fake.loadData.cpu1BusyPct = 100;
    send(console, "@4294967295 load 5000 20000 256\n");
    fake.contains("\"capture_gap_max_us\":18446744073709551615");
    fake.contains("\"work_stack_free_bytes\":4294967295");
    fake.contains("\"cpu1_busy_pct\":100");
    assert(fake.lines.back().size() < Probe::OUTPUT_CAPACITY);
    fake.loadData.cpu0BusyPct = 255;
    send(console, "load\n"); fake.contains("\"cpu_valid\":false");
    fake.contains("\"cpu0_busy_pct\":null"); fake.contains("\"cpu1_busy_pct\":null");
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
    testLoadQueryAndSettings();
    testLoadValidationAndOptionalCallback();
    testLoadMaximumOutput();
}
