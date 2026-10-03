// SPDX-License-Identifier: MIT
#include "../examples/probe_cli/ProbeConsole.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
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
    uint32_t id = 0, nextOperation = 100;
    bool blocked = false;
    unsigned resultQueries = 0, cancellations = 0, releases = 0;
    Probe::ResultView view;
    bool resultAvailable = true;
    Probe::Action cancelAction = Probe::Action::OK, releaseAction = Probe::Action::OK;
    uint8_t address = 0;
    std::vector<std::string> lines;

    static bool emit(void* context, const char* line, std::size_t length) {
        Fake& self = *static_cast<Fake*>(context);
        if (self.blocked) return false;
        assert(length < Probe::OUTPUT_CAPACITY);
        assert(length > 1 && line[0] == '{' && line[length - 1] == '}');
        self.lines.emplace_back(line, length);
        assert(self.lines.back().find('\n') == std::string::npos);
        return true;
    }
    static void snapshot(void* context, Probe::Snapshot& output) {
        Fake& self = *static_cast<Fake*>(context);
        ++self.snapshots;
        output = self.data;
    }
    static Probe::Action probe(void* context, uint32_t id, uint8_t address, uint32_t& operation) {
        Fake& self = *static_cast<Fake*>(context);
        ++self.probes; self.id = id; self.address = address;
        if (self.probeAction == Probe::Action::OK) operation = self.nextOperation++;
        return self.probeAction;
    }
    static Probe::Action recover(void* context, uint32_t, uint32_t& operation) {
        Fake& self = *static_cast<Fake*>(context);
        ++self.recoveries;
        if (self.recoverAction == Probe::Action::OK) operation = self.nextOperation++;
        return self.recoverAction;
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
    static bool result(void* context, uint32_t operation, Probe::ResultView& out) {
        Fake& self = *static_cast<Fake*>(context); ++self.resultQueries;
        self.id = operation; out = self.view; return self.resultAvailable;
    }
    static Probe::Action cancel(void* context, uint32_t operation) {
        Fake& self = *static_cast<Fake*>(context); ++self.cancellations; self.id = operation;
        return self.cancelAction;
    }
    static Probe::Action release(void* context, uint32_t operation) {
        Fake& self = *static_cast<Fake*>(context); ++self.releases; self.id = operation;
        return self.releaseAction;
    }
    Probe::Host host(bool withLoad = false, bool withOwner = false) {
        Probe::Host result;
        result.context = this; result.emitLine = emit; result.snapshot = snapshot;
        result.startProbe = probe; result.recover = recover; result.resetStats = reset;
        if (withLoad) result.load = load;
        if (withOwner) { result.result = Fake::result; result.cancel = cancel; result.release = release; }
        return result;
    }
    void contains(const char* fragment) const {
        if (lines.empty() || lines.back().find(fragment) == std::string::npos)
            std::fprintf(stderr, "Missing %s in %s\n", fragment, lines.empty() ? "<empty>" : lines.back().c_str());
        assert(!lines.empty() && lines.back().find(fragment) != std::string::npos);
    }
    void untouched() const { assert(probes == 0 && recoveries == 0 && resets == 0 && loadChanges == 0); }
};

void send(Probe::Console& console, const std::string& input) {
    for (char c : input) console.feed(c);
}

void report(Probe::Console& console, Fake& fake, uint32_t id, uint8_t address,
            uint32_t operation, const Probe::ProbeResult& result) {
    fake.nextOperation = operation;
    send(console, "@" + std::to_string(id) + " probe " + std::to_string(address) + "\n");
    assert(console.reportProbe(id, address, operation, result));
}

void testFramingAndIds() {
    Fake fake;
    Probe::Console console(fake.host());
    send(console, "\r\n \t\n"); assert(fake.lines.empty());
    send(console, "@42 ver"); assert(fake.lines.empty());
    send(console, "sion\r\n"); assert(fake.lines.size() == 1);
    fake.contains("\"id\":42"); fake.contains("\"product\":\"MotorControl-RS\"");
    fake.contains("\"protocol\":2"); fake.contains("\"profile\":\"ess_rs\"");
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
    fake.data.modelKnown = true; fake.data.modelAddress = 17;
    fake.data.observedEarliestUs = fake.data.observedLatestUs = 1000;
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
    fake.data.probeAddress = 18;
    send(console, "status\n"); fake.contains("\"raw_model\":60"); fake.contains("\"last_probe_ok\":false");
    fake.contains("\"age_ms\":5001"); fake.contains("\"probe_address\":18"); fake.contains("\"model_address\":17");
    send(console, "health\n"); fake.contains("\"communication\":\"failed\""); fake.contains("\"model_address\":17");
    fake.data.modelKnown = false;
    send(console, "status\n"); fake.contains("\"raw_model\":null"); fake.contains("\"model_address\":null"); fake.contains("\"age_ms\":null");
    fake.data.modelKnown = true; fake.data.probeAddress = 17;
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
    send(console, "recover\n"); fake.contains("\"result\":\"accepted\"");
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
    report(console, fake, 99, 1, 199, result);
    fake.contains("\"type\":\"probe\""); fake.contains("\"id\":99"); fake.contains("\"ok\":true");
    fake.contains("\"raw_model\":60"); fake.contains("\"duration_us\":4000");
    fake.contains("\"tx_hex\":\"010300000001840A\""); fake.contains("\"rx_hex\":\"010302003CB855\"");
    fake.contains("\"max_rx_uncertainty_us\":8"); fake.contains("\"timing_valid\":true");
    result.codec = MotorControlRS::Status(MotorControlRS::Err::EXCEPTION, 231, "untrusted text\"\n");
    result.frameError = Ess::FrameError::EXCEPTION;
    report(console, fake, 100, 1, 200, result);
    fake.contains("\"ok\":false"); fake.contains("\"codec\":\"EXCEPTION\"");
    fake.contains("\"detail\":231"); fake.contains("\"raw_model\":null");
    assert(fake.lines.back().find("untrusted") == std::string::npos);
    result.codecChecked = false; result.transport.reason = Rtu::Reason::NO_RESPONSE;
    report(console, fake, 101, 1, 201, result);
    fake.contains("\"codec\":\"NOT_CHECKED\""); fake.contains("\"ok\":false");
    fake.contains("\"detail\":0"); fake.contains("\"frame_error\":0");
    assert(fake.snapshots == 3 && fake.probes == 3 && fake.recoveries == 0);
}

void testMaximumOutputAndRawBounds() {
    Fake fake; Probe::Console console(fake.host());
    const uint32_t max = std::numeric_limits<uint32_t>::max();
    const uint64_t max64 = std::numeric_limits<uint64_t>::max();
    fake.data.uptimeMs = max64; fake.data.ageMs = max64; fake.data.staleAfterMs = max;
    fake.data.observedEarliestUs = fake.data.observedLatestUs = max64;
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
    result.observedEarliestUs = result.observedLatestUs = result.deliveredUs = max64;
    result.outcome = Rtu::Outcome::DISPATCH_EXPIRED;
    result.cancellation = Rtu::Cancellation::GENERATION;
    result.executionUnknown = true;
    report(console, fake, max, 247, max, result); fake.contains("\"raw_truncated\":true");
    fake.contains("18446744073709551615");
    fake.contains("\"observed_latest_us\":18446744073709551615");
    fake.contains("\"delivered_us\":18446744073709551615");
    result.txLength = 8; result.rxLength = 64; result.transport.rxTruncated = true;
    report(console, fake, max, 247, max, result); fake.contains("\"raw_truncated\":true");
    result.tx = result.rx = nullptr;
    report(console, fake, max, 247, max, result); fake.contains("\"tx_hex\":\"\""); fake.contains("\"rx_hex\":\"\"");
    fake.contains("\"raw_truncated\":true"); assert(fake.probes == 3);
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

void testCorrelationAndOutstandingLimit() {
    Fake fake; Probe::Console console(fake.host());
    send(console, "@2 ping\n");
    fake.contains("\"operation_id\":100");
    const unsigned snapshots = fake.snapshots;
    send(console, "@2 status\n@2 reset\n@2 recover\n");
    fake.contains("duplicate_id");
    assert(fake.snapshots == snapshots && fake.resets == 0 && fake.recoveries == 0);
    send(console, "health\n"); fake.contains("\"id\":6");
    Probe::ProbeResult result;
    assert(!console.reportProbe(2, 1, 101, result));
    assert(!console.reportProbe(3, 1, 100, result));
    assert(console.reportProbe(2, 1, 100, result));
    assert(!console.reportProbe(2, 1, 100, result));
    send(console, "@2 reset\n"); assert(fake.resets == 1);

    Fake bounded; Probe::Console full(bounded.host());
    for (unsigned i = 1; i <= Probe::OUTSTANDING_CAPACITY; ++i)
        send(full, "@" + std::to_string(i) + " probe\n");
    assert(bounded.probes == Probe::OUTSTANDING_CAPACITY);
    send(full, "@100 probe\n@101 recover\n");
    bounded.contains("\"result\":\"busy\"");
    assert(bounded.probes == Probe::OUTSTANDING_CAPACITY && bounded.recoveries == 0);
    send(full, "@102 health\n"); bounded.contains("\"command\":\"health\"");
    assert(full.reportProbe(1, 1, 100, result));
    send(full, "@103 probe\n"); assert(bounded.probes == Probe::OUTSTANDING_CAPACITY + 1);
}

void testOutputBackpressureOwnership() {
    Fake fake; Probe::Console console(fake.host(false, true));
    fake.blocked = true;
    send(console, "@17 probe\n");
    assert(fake.lines.empty() && fake.probes == 1 && console.outputPending());
    send(console, "@18 probe\n@19 status\n@20 reset\n");
    assert(fake.probes == 1 && fake.snapshots == 1 && fake.resets == 0);
    assert(console.inputDropped() == 3 && !console.serviceOutput());
    send(console, "@21 cancel 100\n");
    assert(fake.cancellations == 1 && fake.id == 100 && console.inputDropped() == 4);
    Probe::ProbeResult result;
    assert(!console.reportProbe(17, 1, 100, result));
    fake.blocked = false; assert(console.serviceOutput());
    assert(fake.lines.size() == 1); fake.contains("\"result\":\"accepted\"");
    fake.blocked = true;
    assert(console.reportProbe(17, 1, 100, result)); // Ownership transfers even while sink is blocked.
    assert(console.outputPending() && !console.reportProbe(17, 1, 100, result));
    send(console, "@22 cancel 100\n@23 version\n");
    assert(fake.cancellations == 2 && fake.resets == 0 && console.inputDropped() == 6);
    fake.blocked = false; assert(console.serviceOutput());
    assert(fake.lines.size() == 2); fake.contains("\"type\":\"probe\"");
    assert(!console.reportProbe(17, 1, 100, result));
    send(console, "@17 reset\n"); assert(fake.resets == 1);
    assert(fake.lines.size() == 3);
}

void testOwnerControlsAndRetainedInspections() {
    Fake fake; Probe::Console console(fake.host(false, true));
    send(console, "help\n"); fake.contains("\"drv\""); fake.contains("\"result\"");
    fake.contains("\"cancel\""); fake.contains("\"release\"");
    send(console, "help cancel\n"); fake.contains("cancel_local_work_not_motor_stop");
    fake.data.pending = 3; fake.data.retained = 4; fake.data.reserved = 5;
    fake.data.pendingCapacity = 8; fake.data.resultCapacity = 8;
    fake.data.operationId = 120; fake.data.recoveryGuardUntilUs = 500001;
    fake.data.deadlineUs = 2000000; fake.data.timerCapture = true;
    fake.data.observedEarliestUs = 100; fake.data.observedLatestUs = 120; fake.data.deliveredUs = 200;
    send(console, "drv\n"); fake.contains("\"pending\":3"); fake.contains("\"retained\":4");
    fake.contains("\"reserved\":5"); fake.contains("\"recovery_guard_until_us\":500001");
    fake.contains("\"request_deadline_us\":2000000"); fake.contains("\"capture_mode\":\"timer\"");
    fake.contains("\"read_budget\":64");
    fake.contains("\"observed_latest_us\":120"); fake.contains("\"delivered_us\":200");
    fake.view.commandId = 77; fake.view.operationId = 120; fake.view.address = 1; fake.view.pending = true;
    send(console, "@4 result\n"); fake.contains("\"result\":\"pending\"");
    fake.contains("\"id\":4"); fake.contains("\"command_id\":77"); fake.contains("\"operation_id\":120");
    assert(fake.id == 0);
    fake.view.pending = false;
    fake.view.probe.outcome = Rtu::Outcome::CANCELLED;
    fake.view.probe.executionUnknown = true; fake.view.probe.cancellation = Rtu::Cancellation::REQUEST;
    send(console, "@5 result 120\n@6 result 120\n");
    fake.contains("\"type\":\"reply\""); fake.contains("\"command\":\"result\"");
    fake.contains("\"outcome\":\"cancelled\""); fake.contains("\"execution_unknown\":true");
    assert(fake.resultQueries == 3 && fake.releases == 0 && fake.cancellations == 0);
    fake.view.recovery = true; fake.view.recoveryResult.outcome = Rtu::RecoveryOutcome::RECOVERED;
    send(console, "result 120\n"); fake.contains("\"outcome\":\"recovered\"");
    send(console, "cancel 120\n"); fake.contains("\"result\":\"done\""); assert(fake.id == 120);
    send(console, "cancel\n"); assert(fake.id == 0 && fake.cancellations == 2);
    send(console, "release 120\n"); fake.contains("\"result\":\"done\""); assert(fake.releases == 1);
    fake.resultAvailable = false;
    send(console, "result 120\n"); fake.contains("\"result\":\"unavailable\"");
    const char* invalid[] = {"result 0", "result -1", "result 4294967296", "result 1 2",
        "cancel 0", "cancel +1", "cancel 1x", "cancel 1 2", "release", "release 0", "release 1 2"};
    const unsigned before = fake.resultQueries + fake.cancellations + fake.releases;
    for (const char* input : invalid) {
        send(console, std::string(input) + "\n"); fake.contains("\"ok\":false");
    }
    assert(before == fake.resultQueries + fake.cancellations + fake.releases);
    assert(fake.probes == 0 && fake.recoveries == 0 && fake.resets == 0);
}

void testRecoveryTerminalAndOptionalOwnerHooks() {
    Fake fake; Probe::Console console(fake.host());
    send(console, "@8 recover\n"); fake.contains("\"result\":\"accepted\"");
    fake.contains("\"operation_id\":100");
    Rtu::RecoveryResult result; result.outcome = Rtu::RecoveryOutcome::RECOVERED;
    result.requestedUs = 1000; result.deadlineUs = 500000; result.finishedUs = 2500;
    assert(console.reportRecovery(8, 100, result));
    fake.contains("\"type\":\"recovery\""); fake.contains("\"command\":\"recover\"");
    fake.contains("\"operation_id\":100"); fake.contains("\"outcome\":\"recovered\"");
    fake.contains("\"finished_us\":2500");
    assert(!console.reportRecovery(8, 100, result));
    send(console, "@8 recover\n"); assert(fake.recoveries == 2);
    result.outcome = Rtu::RecoveryOutcome::READ_ERROR; result.reason = Rtu::Reason::RX_ERROR;
    assert(console.reportRecovery(8, 101, result)); fake.contains("\"ok\":false");
    fake.contains("\"outcome\":\"read_error\""); fake.contains("\"transport\":\"RX_ERROR\"");
    for (const char* command : {"result", "cancel", "release 1", "help result", "help cancel", "help release"}) {
        send(console, std::string(command) + "\n"); fake.contains("\"result\":\"unavailable\"");
    }
    send(console, "help\n");
    assert(fake.lines.back().find("\"result\"") == std::string::npos);
    assert(fake.lines.back().find("\"cancel\"") == std::string::npos);
    assert(fake.lines.back().find("\"release\"") == std::string::npos);
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
    testCorrelationAndOutstandingLimit();
    testOutputBackpressureOwnership();
    testOwnerControlsAndRetainedInspections();
    testRecoveryTerminalAndOptionalOwnerHooks();
}
