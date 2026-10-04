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
    unsigned typedReads = 0, monitors = 0, monitorChanges = 0;
    Probe::MonitorSnapshot monitorData;
    Ess::ReadKind typedKind = Ess::ReadKind::IDENTITY;
    uint32_t id = 0, nextOperation = 100;
    bool blocked = false;
    unsigned resultQueries = 0, cancellations = 0, releases = 0;
    Probe::ResultView view;
    bool resultAvailable = true;
    unsigned axisCalls = 0;
    Probe::AxisCommand axisRequest;
    MotorControlRS::AxisConfig axisConfig;
    MotorControlRS::Status axisStatus;
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
    static Probe::Action typedRead(void* context, uint32_t commandId, uint8_t address,
                                  Ess::ReadKind kind, uint32_t& operation) {
        Fake& self = *static_cast<Fake*>(context); ++self.typedReads;
        self.typedKind = kind; self.id = commandId; self.address = address;
        if (self.probeAction == Probe::Action::OK) operation = self.nextOperation++;
        return self.probeAction;
    }
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
    static Probe::Action monitor(void* context, const Probe::MonitorSettings* request, Probe::MonitorSnapshot& out) {
        Fake& self = *static_cast<Fake*>(context); ++self.monitors;
        if (request) { ++self.monitorChanges; self.monitorData.settings = *request; self.monitorData.remaining = request->count; }
        out = self.monitorData; return Probe::Action::OK;
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
    static MotorControlRS::Status axis(void* context, const Probe::AxisCommand& request, Probe::AxisView& out) {
        Fake& self = *static_cast<Fake*>(context);
        ++self.axisCalls; self.axisRequest = request;
        out.configuration = self.axisConfig;
        if (!self.axisStatus) return self.axisStatus;
        if (request.kind != Probe::AxisCommandKind::PREPARE) return MotorControlRS::Ok();
        auto position = request.position; position.configurationGeneration = self.axisConfig.generation;
        return MotorControlRS::preparePosition(position, self.axisConfig, nullptr, out.prepared);
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
void testCaptureReadOptionalHookAndDiagnostics() {
    Fake fake; Probe::Console unavailable(fake.host());
    send(unavailable, "capture-read\n"); fake.contains("unavailable");
    assert(fake.probes == 0);
    send(unavailable, "help\n"); assert(fake.lines.back().find("capture-read") == std::string::npos);
    auto host = fake.host(); host.startCaptureRead = Fake::probe;
    Probe::Console console(host);
    send(console, "@1 help capture-read\n"); fake.contains("read_0x0130_16_words");
    send(console, "@2 capture-read 247\n"); fake.contains("accepted"); assert(fake.address == 247);
    Probe::ProbeResult result; result.captureRead = true;
    result.transport.reason = Rtu::Reason::FRAME; result.codecChecked = true;
    result.rawModel = 123; result.transport.txAccepted = 8; result.transport.rxLength = 37;
    assert(console.reportProbe(2, 247, 100, result));
    fake.contains("\"type\":\"capture_read\""); fake.contains("\"command\":\"capture-read\"");
    fake.contains("\"raw_model\":null"); fake.contains("\"identity\":\"not_requested\"");
    fake.data.sampleGapLimitUs = 85; fake.data.sampleGapExceeded = true;
    send(console, "config\n"); fake.contains("\"cache_off_supported\":false"); fake.contains("\"sample_gap_limit_us\":85");
    send(console, "stats\n"); fake.contains("\"sample_gap_exceeded\":true");
}

void sealTypedReply(uint8_t* frame, std::size_t length) {
    uint16_t crc = 0xFFFF;
    for (std::size_t i = 0; i < length - 2; ++i) {
        crc ^= frame[i];
        for (unsigned b = 0; b < 8; ++b) crc = (crc >> 1) ^ ((crc & 1) ? 0xA001 : 0);
    }
    frame[length - 2] = static_cast<uint8_t>(crc); frame[length - 1] = static_cast<uint8_t>(crc >> 8);
}
Ess::ReadContext typedContext(bool identity, uint32_t operation) {
    Ess::ReadContext context;
    MotorControlRS::ReadTarget target; target.id = UINT32_MAX; target.address = 247; target.generation = UINT32_MAX;
    const uint64_t start = UINT64_MAX - 1000;
    MotorControlRS::ActiveSerialTuple active;
    active.known = true; active.baud = UINT32_MAX; active.dataBits = 8;
    active.parity = MotorControlRS::SerialParity::ODD; active.stopBits = 2;
    assert(identity ? Ess::prepareIdentity(context, target, operation, start, UINT64_MAX, active).isOk() :
        Ess::prepareConfig(context, target, operation, start, UINT64_MAX, active).isOk());
    return context;
}
void completeTypedStep(Ess::ReadContext& context, bool invalid = false) {
    Ess::PreparedRead read; assert(Ess::nextRead(context, context.servicedUs, read));
    uint8_t frame[Ess::READ_MAX_REPLY_BYTES] = {247, 3};
    frame[2] = static_cast<uint8_t>(read.count * 2);
    const std::size_t length = 5 + read.count * 2;
    for (std::size_t i = 3; i < length - 2; ++i) frame[i] = 0xFF;
    sealTypedReply(frame, length);
    if (invalid) frame[length - 1] ^= 1;
    MotorControlRS::ReadEvent event; event.target = context.target; event.operationId = context.operationId;
    event.step = context.step; event.frame = frame; event.length = length; event.qualified = true;
    event.earliestUs = context.servicedUs + 10; event.latestUs = context.servicedUs + 20; event.txAccepted = 8;
    event.transportDetail = INT32_MIN;
    assert(Ess::advanceRead(context, event, context.servicedUs + 30));
}
void testTypedRoutesAndValidation() {
    Fake fake; auto host = fake.host(); host.startTypedRead = Fake::typedRead;
    Probe::Console console(host);
    send(console, "@1 read identity 247\n"); fake.contains("\"command\":\"read-identity\""); fake.contains("\"result\":\"accepted\"");
    assert(fake.typedReads == 1 && fake.typedKind == Ess::ReadKind::IDENTITY && fake.address == 247);
    send(console, "@2 profile ess_rs config 1\n"); fake.contains("\"command\":\"read-config\"");
    assert(fake.typedReads == 2 && fake.typedKind == Ess::ReadKind::CONFIG && fake.address == 1);
    send(console, "@3 read config\n"); assert(fake.typedReads == 3 && fake.address == fake.data.address);
    const unsigned snapshots = fake.snapshots;
    for (const char* bad : {"read", "read unknown", "read identity 0", "read config 248", "read config -1", "read config 1 extra", "profile", "profile unknown identity", "profile ess_rs", "profile ess_rs caps 1", "read-identity 1"})
        send(console, std::string(bad) + "\n");
    assert(fake.typedReads == 3 && fake.snapshots == snapshots && fake.probes == 0);
    send(console, "caps\n"); fake.contains("\"identity\":true"); fake.contains("\"max_steps\":5");
    send(console, "profile ess_rs caps\n"); fake.contains("\"command\":\"caps\"");
    assert(fake.typedReads == 3 && fake.snapshots == snapshots);
    send(console, "help read\n"); fake.contains("read identity|config|state [address]");
    send(console, "help profile\n"); fake.contains("profile ess_rs caps");
    fake.data.cachedIdentityId = 91; fake.data.cachedIdentityAddress = 4; fake.data.cachedIdentityGeneration = 2;
    fake.data.cachedConfigId = 92; fake.data.cachedConfigAddress = 5; fake.data.cachedConfigGeneration = 3;
    fake.data.bindingGeneration = 4;
    send(console, "config\n"); fake.contains("\"device_settings\":\"cached\"");
    fake.contains("\"cached_identity_id\":91,\"cached_identity_address\":4,\"cached_identity_generation\":2");
    fake.contains("\"cached_config_id\":92,\"cached_config_address\":5,\"cached_config_generation\":3,\"binding_generation\":4");
    Fake absent; Probe::Console noHook(absent.host());
    send(noHook, "read identity\n"); absent.contains("unavailable"); absent.untouched();
    send(noHook, "profile ess_rs caps\n"); absent.contains("unavailable"); absent.untouched();
    send(noHook, "caps\n"); absent.contains("\"identity\":true"); absent.untouched();
    send(noHook, "help\n"); assert(absent.lines.back().find("\"read\"") == std::string::npos);
    assert(absent.lines.back().find(",\"profile\",") == std::string::npos);
}
void testTypedTerminalInspectionAndBound() {
    Fake fake; auto host = fake.host(false, true); host.startTypedRead = Fake::typedRead;
    Probe::Console console(host);
    fake.nextOperation = UINT32_MAX;
    send(console, "@4294967295 read config 247\n");
    Ess::ReadContext context = typedContext(false, UINT32_MAX);
    for (unsigned i = 0; i < 5; ++i) completeTypedStep(context);
    assert(!console.reportRead(UINT32_MAX, 101, context));
    assert(console.reportRead(UINT32_MAX, UINT32_MAX, context));
    fake.contains("\"type\":\"read\""); fake.contains("\"config\":{\"raw\""); fake.contains("\"function\":65535,\"known\":false");
    fake.contains("\"wiring\":0,\"level_known\":false,\"level\":null"); fake.contains("\"step\":4,\"first\":256,\"count\":2");
    assert(fake.lines.back().size() < Probe::OUTPUT_CAPACITY);
    std::printf("Maximum-width config line: %zu/%zu bytes\n", fake.lines.back().size(), Probe::OUTPUT_CAPACITY);
    const std::string terminal = fake.lines.back();
    assert(!console.reportRead(UINT32_MAX, UINT32_MAX, context));
    fake.view.commandId = UINT32_MAX; fake.view.operationId = UINT32_MAX; fake.view.typedRead = &context;
    send(console, "@8 result 4294967295\n");
    assert(fake.lines.back().substr(fake.lines.back().find("\"command_id\"")) == terminal.substr(terminal.find("\"command_id\"")));
    fake.view.pending = true; send(console, "@9 result 4294967295\n"); fake.contains("\"result\":\"pending\""); fake.contains("\"read_kind\":\"config\"");
    fake.nextOperation = 101;
    send(console, "@10 read identity 247\n");
    auto failed = typedContext(true, 101); completeTypedStep(failed, true);
    fake.blocked = true;
    assert(console.reportRead(10, 101, failed)); assert(console.outputPending());
    assert(!console.reportRead(10, 101, failed));
    fake.blocked = false; assert(console.serviceOutput());
    fake.contains("\"identity\":null"); fake.contains("\"completed_steps\":0"); fake.contains("\"step\":0,\"first\":0,\"count\":4");
    fake.contains("\"outcome\":\"reply_error\""); fake.contains("\"rx\":\"F70308FFFFFFFFFFFFFFFF");
    send(console, "@11 read identity 247\n");
    auto identity = typedContext(true, 102); completeTypedStep(identity);
    assert(console.reportRead(11, 102, identity)); fake.contains("\"raw_model\":65535"); fake.contains("\"active_node_known\":false");
    send(console, "@12 profile ess_rs config 247\n");
    auto partial = typedContext(false, 103);
    for (unsigned i = 0; i < 4; ++i) completeTypedStep(partial);
    uint8_t prefix[Ess::READ_MAX_REPLY_BYTES]; for (auto& byte : prefix) byte = 0xFF;
    MotorControlRS::ReadEvent event; event.target = partial.target; event.operationId = 103; event.step = 4;
    event.kind = MotorControlRS::ReadEventKind::TRANSPORT_FAILURE; event.frame = prefix; event.length = sizeof(prefix);
    event.txAccepted = 8; event.executionUnknown = true; event.transportDetail = INT32_MIN;
    assert(Ess::advanceRead(partial, event, partial.servicedUs + 30));
    assert(console.reportRead(12, 103, partial)); fake.contains("\"config\":null");
    fake.contains("\"received_length\":37,\"tx_accepted\":8,\"execution_unknown\":true");
    fake.contains("\"completed_steps\":4"); fake.contains("\"step\":4,\"first\":256");
    assert(fake.lines.back().size() < Probe::OUTPUT_CAPACITY);
}

void testStateRoutesCacheAndPolling() {
    Fake fake; auto host = fake.host(); host.startTypedRead = Fake::typedRead; host.monitor = Fake::monitor;
    Probe::Console console(host);
    send(console, "@1 read state 247\n@2 profile ess_rs state 247\n@3 health check 247\n");
    assert(fake.typedReads == 3 && fake.typedKind == Ess::ReadKind::STATE);
    fake.contains("\"command\":\"read-state\""); fake.contains("\"read_kind\":\"state\"");
    send(console, "health check 0\nhealth x\nmonitor 99 1\nmonitor 100 0\nmonitor on\nmonitor 100 1001\nmonitor off extra\n");
    assert(fake.typedReads == 3 && fake.monitors == 0);
    send(console, "monitor 100 3\n"); fake.contains("\"enabled\":true"); fake.contains("\"remaining\":3");
    send(console, "monitor\n"); assert(fake.monitors == 2 && fake.monitorChanges == 1);
    fake.blocked = true; send(console, "status\n"); assert(console.outputPending());
    send(console, "monitor off\n"); assert(fake.monitorChanges == 2 && !fake.monitorData.settings.enabled);
    fake.blocked = false; assert(console.serviceOutput());
    Ess::ReadContext context = typedContext(true, 100);
    assert(Ess::prepareState(context, context.target, 100, context.startedUs, context.deadlineUs, context.activeSerial));
    for (unsigned i = 0; i < 3; ++i) completeTypedStep(context);
    assert(console.reportRead(1, 100, context));
    fake.contains("\"state_blocks\":[{\"block\":0"); fake.contains("\"released\":true,\"enabled\":false");
    fake.contains("\"unknown_motion_bits\":65408"); fake.contains("\"raw_encoder_counts\":null");
    assert(fake.lines.back().size() < Probe::OUTPUT_CAPACITY);
    Probe::StateCache cache;
    for (uint8_t i = 0; i < 3; ++i) {
        Probe::stateAttempt(cache, context.target, 100, i, context.startedUs);
        Probe::stateResult(cache, context, i, context.observations[i].attemptedUs);
    }
    for (auto& block : cache.blocks) {
        block.lastAttemptTarget = context.target; block.lastAttemptOperationId = UINT32_MAX;
        block.lastAttemptUs = UINT64_MAX - 1000; block.lastSuccessUs = UINT64_MAX - 800;
        block.observedEarliestUs = UINT64_MAX - 1000; block.observedLatestUs = UINT64_MAX - 800;
        block.deliveredUs = UINT64_MAX - 600; block.lastAttemptStatus = MotorControlRS::Status(MotorControlRS::Err::FRAME_ERROR, INT32_MIN, "");
        block.value.operationId = block.value.configOperationId = UINT32_MAX;
    }
    cache.blocks[2].value.pairKnown = true; cache.blocks[2].value.rawPosition = UINT32_MAX;
    fake.data.uptimeMs = fake.data.ageMs = UINT64_MAX; fake.data.staleAfterMs = UINT32_MAX;
    fake.data.probeKnown = fake.data.probeOk = fake.data.modelKnown = true;
    fake.data.rawModel = UINT16_MAX; fake.data.probeAddress = fake.data.modelAddress = 247;
    fake.data.observedEarliestUs = UINT64_MAX - 1000;
    fake.data.communicationKnown = true; fake.data.communicationTarget = context.target;
    fake.data.communicationEarliestUs = UINT64_MAX - 1000; fake.data.communicationLatestUs = UINT64_MAX - 800;
    fake.data.stateCache = &cache; fake.data.nowUs = UINT64_MAX;
    fake.data.address = context.target.address; fake.data.bindingGeneration = context.target.generation;
    send(console, "status\n"); fake.contains("\"atomic_snapshot\":false"); fake.contains("\"source\":\"checked_rtu_register\"");
    fake.contains("\"current_config_operation_id\":0,\"interpretation_current\":false");
    fake.contains("\"current\":false"); // Cache target id differs from address: exact binding matters.
    std::printf("Maximum-width cached status line: %zu/%zu bytes\n", fake.lines.back().size(), Probe::OUTPUT_CAPACITY);
    const auto retained = cache.blocks[0].lastSuccessUs;
    send(console, "health\nhealth\n"); assert(cache.blocks[0].lastSuccessUs == retained);
    fake.contains("\"readiness\":\"unknown\"");
    std::printf("Maximum-width cached health line: %zu/%zu bytes\n", fake.lines.back().size(), Probe::OUTPUT_CAPACITY);
    assert(fake.lines.back().size() < Probe::OUTPUT_CAPACITY);
    for (auto& block : cache.blocks) block.value.target.id = fake.data.address;
    fake.data.cachedConfigId = UINT32_MAX; fake.data.cachedConfigAddress = fake.data.address;
    fake.data.cachedConfigGeneration = fake.data.bindingGeneration;
    send(console, "status\n"); fake.contains("\"current_config_operation_id\":4294967295,\"interpretation_current\":true");
    assert(fake.lines.back().size() < Probe::OUTPUT_CAPACITY);
    ++fake.data.cachedConfigGeneration;
    send(console, "health\n"); fake.contains("\"current_config_operation_id\":0,\"interpretation_current\":false");
    --fake.data.cachedConfigGeneration;
    cache.blocks[0].value.rawAlarm = 4; cache.blocks[0].value.alarmKnown = false; cache.blocks[0].value.alarmFlag = false;
    send(console, "health\n"); fake.contains("\"alarms\":\"present\"");
    cache.blocks[0].value.rawAlarm = 0xBEEF;
    send(console, "health\n"); fake.contains("\"alarms\":\"unknown\"");
    cache.blocks[0].value.alarmFlag = true;
    send(console, "health\n"); fake.contains("\"alarms\":\"present\"");
    ++fake.data.bindingGeneration; send(console, "status\n"); fake.contains("\"fresh\":false");
    Fake absent; Probe::Console unsupported(absent.host());
    send(unsupported, "help\n"); assert(absent.lines.back().find("\"monitor\"") == std::string::npos);
    send(unsupported, "monitor off\n"); absent.contains("unavailable");
}

void testExactHostPreparationAndParsing() {
    namespace Core = MotorControlRS;
    Fake fake;
    fake.axisConfig.target.id = fake.axisConfig.target.address = 1;
    fake.axisConfig.target.generation = 1;
    fake.axisConfig.supportedRelativeBases = 7;
    auto host = fake.host(); host.axis = Fake::axis;
    Probe::Console console(host);
    send(console, "axis config\n"); fake.contains("\"motion_command\":false"); fake.contains("\"origin_known\":false");
    send(console, "@4294967295 prepare relative -9223372036854775808 steps native actual\n");
    fake.contains("\"ok\":true"); fake.contains("\"effective_native\":-9223372036854775808");
    fake.contains("\"id\":4294967295"); fake.contains("\"bus_traffic\":false");
    Core::PositionRequest direct; direct.configurationGeneration = fake.axisConfig.generation;
    direct.value = Core::Rational(INT64_MIN);
    Core::PreparedTarget expected;
    assert(Core::preparePosition(direct, fake.axisConfig, nullptr, expected));
    assert(expected.effectiveNative == fake.axisRequest.position.value.numerator);
    send(console, "prepare absolute 9223372036854775807 steps native\n");
    fake.contains("\"effective_native\":9223372036854775807");
    send(console, "prepare absolute 9223372036854775807.000 steps native\n");
    fake.contains("\"effective_native\":9223372036854775807");
    send(console, "prepare relative -5/2 steps native commanded nearest 1\n");
    fake.contains("\"effective_native\":-2"); fake.contains("\"rounding_error\":0.5");
    direct.value = Core::Rational(-5, 2); direct.basis = Core::RelativeBasis::COMMANDED;
    direct.rounding = Core::Rounding::NEAREST; direct.maximumQuantizationError = 1;
    assert(Core::preparePosition(direct, fake.axisConfig, nullptr, expected));
    assert(expected.effectiveNative == -2 && expected.roundingError == 0.5);
    // The longest admitted full-width rational includes explicit correlation and options.
    send(console, "@4294967295 prepare relative -9223372036854775808/18446744073709551615 steps native queued nearest 1\n");
    fake.contains("\"ok\":true"); assert(fake.axisRequest.position.value.denominator == UINT64_MAX);
    send(console, "prepare relative 0.001 steps native actual zero 1\n");
    fake.contains("\"zero_displacement\":true");
    send(console, "prepare relative 1.5 steps native actual\n"); fake.contains("\"ok\":false");
    fake.axisConfig.units.commandStepsPerMotorTurn = Core::UnitScale(1000, 1, Core::ScaleSource::ASSUMED);
    send(console, "prepare relative 1 rad motor actual nearest 1 0.0001\n");
    fake.contains("\"ok\":true"); fake.contains("\"requested_native\":null");
    fake.contains("\"requested_native_approximate\":159."); fake.contains("\"exact_arithmetic\":false");
    send(console, "prepare relative -5/2 steps native actual nearest 9223372036854775807/18446744073709551615\n");
    fake.contains("\"ok\":false"); // An allowance just below 0.5 cannot round upward to admit the tie.
    send(console, "axis config set command 1000/2\n");
    assert(fake.axisRequest.field == Probe::AxisField::COMMAND_SCALE && fake.axisRequest.value.numerator == 500 && fake.axisRequest.value.denominator == 1);
    send(console, "axis config set lead none\n"); assert(fake.axisRequest.clear);
    send(console, "axis config set relative-bases 7\n"); assert(fake.axisRequest.field == Probe::AxisField::RELATIVE_BASES);
    send(console, "axis config set native-limits -9223372036854775808 9223372036854775807\n");
    assert(fake.axisRequest.value.numerator == INT64_MIN && fake.axisRequest.secondValue.numerator == INT64_MAX);
    send(console, "axis config set soft-limits none\n"); assert(fake.axisRequest.clear);
    send(console, "axis config set position-unit deg\n"); assert(fake.axisRequest.positionUnit == Core::PositionUnit::DEGREES);
    send(console, "axis config set velocity-unit rpm\n"); assert(fake.axisRequest.velocityUnit.time == Core::TimeUnit::MINUTE);
    for (const char* unit : {"steps/s2", "deg/s2", "rad/s2", "rpm/s"}) {
        send(console, std::string("axis config set acceleration-unit ") + unit + "\n");
        assert(fake.axisRequest.field == Probe::AxisField::ACCELERATION_UNIT);
    }
    assert(fake.axisRequest.accelerationUnit.velocityTime == Core::TimeUnit::MINUTE && fake.axisRequest.accelerationUnit.accelerationTime == Core::TimeUnit::SECOND);
    send(console, "axis origin -9223372036854775808\n");
    assert(fake.axisRequest.kind == Probe::AxisCommandKind::ORIGIN && fake.axisRequest.value.numerator == INT64_MIN);
    fake.axisStatus = Core::Status(Core::Err::UNSUPPORTED, 12345, "do not serialize untrusted text\"");
    send(console, "axis origin 0\n"); fake.contains("\"code\":\"UNSUPPORTED\""); fake.contains("\"detail\":12345");
    assert(fake.lines.back().find("untrusted") == std::string::npos);
    fake.untouched(); assert(fake.snapshots == 0);
}

void testHostArgumentsAndBackpressure() {
    Fake fake; auto host = fake.host(); host.axis = Fake::axis;
    Probe::Console console(host);
    const char* invalid[] = {
        "axis", "axis origin 1/2", "axis config set bad 1", "axis config set command 0",
        "axis config set gear 4294967296", "axis config set lead -1", "axis config set command 1/4294967296",
        "axis config set command 1/2.0", "axis config set gear 1/2.000",
        "axis config set polarity 0", "axis config set encoder-id -1", "axis config set relative-bases 8",
        "axis config set acceleration-unit rpm/s2", "axis config set native-limits 2 1",
        "axis config set soft-limits 1", "axis config set position-unit native", "axis config set encoder-basis none",
        "prepare relative 1 steps native", "prepare relative 1 steps native unknown", "prepare absolute 1 steps other",
        "prepare absolute nan steps native", "prepare absolute inf steps native", "prepare absolute 1e3 steps native",
        "prepare absolute 1/0 steps native", "prepare absolute 0x10 steps native", "prepare absolute 1junk steps native",
        "prepare relative 1/2.0 steps native actual nearest 1", "prepare absolute 1/2.000 steps native nearest 1",
        "prepare relative 1/2 steps native actual nearest 1/2.0",
        "prepare absolute 9223372036854775808 steps native", "prepare absolute 1 steps native nearest -1",
        "prepare absolute 1 steps native nearest 1 0.001", "prepare absolute 1 rad load nearest 1 0",
        "prepare absolute 1 rad load nearest 1 1e-3"
    };
    for (const char* text : invalid) {
        send(console, std::string(text) + "\n"); fake.contains("\"ok\":false");
        assert(fake.axisCalls == 0); fake.untouched();
    }
    send(console, "help axis\n"); fake.contains("configure_host_coordinates_only");
    send(console, "help prepare\n"); fake.contains("preview_public_target_arithmetic_without_motion");
    send(console, "prepare relative 1 rad motor actual nearest 1 0.0001\n");
    assert(fake.axisCalls == 1 && fake.axisRequest.position.approximate);
    assert(fake.axisRequest.position.maximumApproximationError <= 0.0001 &&
        fake.axisRequest.position.maximumApproximationError >= 0.0001 * (1 - 1e-12));
    fake.blocked = true; send(console, "axis config\n"); assert(console.outputPending());
    const auto calls = fake.axisCalls;
    send(console, "axis config set polarity -1\nprepare relative 1 steps native actual\n");
    assert(fake.axisCalls == calls && console.inputDropped() == 2);
    Fake absent; Probe::Console unavailable(absent.host());
    send(unavailable, "help\n"); assert(absent.lines.back().find("\"axis\"") == std::string::npos && absent.lines.back().find("\"prepare\"") == std::string::npos);
    send(unavailable, "axis config\n"); absent.contains("unavailable");
}

void testZeroRadiansAndApproximateCancellationMatchApi() {
    namespace Core = MotorControlRS;
    Fake fake;
    fake.axisConfig.target.id = fake.axisConfig.target.address = 1;
    fake.axisConfig.target.generation = 1;
    fake.axisConfig.supportedRelativeBases = 1;
    fake.axisConfig.units.commandStepsPerMotorTurn = Core::UnitScale(1000, 1, Core::ScaleSource::ASSUMED);
    auto host = fake.host(); host.axis = Fake::axis;
    Probe::Console console(host);
    send(console, "prepare relative 0 rad motor actual\n");
    fake.contains("\"ok\":true"); fake.contains("\"exact_arithmetic\":true");
    for (const char* policy : {"exact", "nearest", "zero", "floor", "ceil"}) {
        send(console, std::string("prepare relative 0 rad motor actual ") + policy + " 0\n");
        fake.contains("\"ok\":true"); fake.contains("\"effective_native\":0");
        fake.contains("\"zero_displacement\":true"); fake.contains("\"exact_arithmetic\":true");
        fake.contains("\"rounding_error\":0"); fake.contains("\"approximation_error_bound\":0");
        auto request = fake.axisRequest.position; request.configurationGeneration = fake.axisConfig.generation;
        Core::PreparedTarget expected;
        assert(Core::preparePosition(request, fake.axisConfig, nullptr, expected));
        assert(expected.effectiveNative == 0 && expected.zeroDisplacement && expected.exactArithmetic);
    }
    fake.axisConfig.originKnown = true; fake.axisConfig.originSource = Core::ScaleSource::ASSUMED;
    for (int64_t origin : {INT64_MIN, INT64_MAX}) {
        fake.axisConfig.originNative = origin;
        send(console, "prepare absolute 0 rad motor\n");
        const std::string effective = "\"effective_native\":" + std::to_string(origin);
        fake.contains("\"ok\":true"); fake.contains(effective.c_str());
        fake.contains("\"exact_arithmetic\":true"); fake.contains("\"displacement_known\":false");
        auto request = fake.axisRequest.position; request.configurationGeneration = fake.axisConfig.generation;
        Core::PreparedTarget expected;
        assert(Core::preparePosition(request, fake.axisConfig, nullptr, expected));
        assert(expected.effectiveNative == origin && expected.requestedNative.integral == origin);
    }
    fake.axisConfig.originNative = -159;
    send(console, "prepare absolute 1 rad motor nearest 1 0.0001\n");
    fake.contains("\"ok\":true"); fake.contains("\"effective_native\":0");
    fake.contains("\"exact_arithmetic\":false"); fake.contains("\"requested_native\":null");
    auto request = fake.axisRequest.position; request.configurationGeneration = fake.axisConfig.generation;
    Core::PreparedTarget expected;
    assert(Core::preparePosition(request, fake.axisConfig, nullptr, expected));
    assert(expected.effectiveNative == 0 && !expected.exactArithmetic && expected.approximationErrorBound > 0);
    fake.untouched(); assert(fake.snapshots == 0);
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
    testCaptureReadOptionalHookAndDiagnostics();
    testTypedRoutesAndValidation();
    testTypedTerminalInspectionAndBound();
    testStateRoutesCacheAndPolling();
    testExactHostPreparationAndParsing();
    testHostArgumentsAndBackpressure();
    testZeroRadiansAndApproximateCancellationMatchApi();
}
