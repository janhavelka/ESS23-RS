// SPDX-License-Identifier: MIT
#include "../examples/probe_cli/ProbeConsole.h"
#include <MotorControlRS/profiles/ess_rs/Tuning.h>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
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
    unsigned actions = 0;
    unsigned debugCalls = 0;
    Probe::DebugSnapshot debugState;
    unsigned moves = 0;
    unsigned velocities = 0;
    MotorControlRS::VelocityRequest velocityRequest;
    MotorControlRS::MoveRequest moveRequest;
    MotorControlRS::ActionRequest actionRequest;
    Probe::Action actionResult = Probe::Action::OK;
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
    MotorControlRS::AxisReference axisReference;
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
    static Probe::Action debug(void* context, const Probe::DebugMode* mode, Probe::DebugSnapshot& out) {
        Fake& self=*static_cast<Fake*>(context); ++self.debugCalls;
        if (mode) self.debugState.mode=*mode;
        self.debugState.capacity=16; out=self.debugState; return Probe::Action::OK;
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
    static Probe::Action startAction(void* context, uint32_t commandId, uint8_t address,
                                    const MotorControlRS::ActionRequest& request, uint32_t& operation) {
        Fake& self = *static_cast<Fake*>(context); ++self.actions;
        self.actionRequest = request; self.id = commandId; self.address = address;
        if (self.actionResult == Probe::Action::OK) operation = self.nextOperation++;
        return self.actionResult;
    }
    static Probe::Action startMove(void* context, uint32_t commandId, uint8_t address,
                                  const MotorControlRS::MoveRequest& request, uint32_t& operation) {
        Fake& self = *static_cast<Fake*>(context); ++self.moves;
        self.moveRequest = request; self.id = commandId; self.address = address;
        if (self.actionResult == Probe::Action::OK) operation = self.nextOperation++;
        return self.actionResult;
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
    static Probe::Action startVelocity(void* context, uint32_t commandId, uint8_t address,
                                      const MotorControlRS::VelocityRequest& request, uint32_t& operation) {
        Fake& self = *static_cast<Fake*>(context); ++self.velocities;
        self.velocityRequest = request; self.id = commandId; self.address = address;
        if (self.actionResult == Probe::Action::OK) operation = self.nextOperation++;
        return self.actionResult;
    }
    unsigned drivers = 0;
    Ess::DriverKind driverKind = Ess::DriverKind::READ;
    Ess::DriverRequest driverRequest;
    static Probe::Action startDriver(void* context, uint32_t commandId, uint8_t address,
                                    Ess::DriverKind kind, const Ess::DriverRequest& request, uint32_t& operation) {
        Fake& self = *static_cast<Fake*>(context); ++self.drivers;
        self.driverKind = kind; self.driverRequest = request; self.id = commandId; self.address = address;
        if (self.actionResult == Probe::Action::OK) operation = self.nextOperation++;
        return self.actionResult;
    }
    unsigned homes = 0;
    Ess::HomeRequest homeRequest;
    static Probe::Action startHome(void* context, uint32_t commandId, uint8_t address,
                                  const Ess::HomeRequest& request, uint32_t& operation) {
        Fake& self = *static_cast<Fake*>(context); ++self.homes;
        self.homeRequest = request; self.id = commandId; self.address = address;
        if (self.actionResult == Probe::Action::OK) operation = self.nextOperation++;
        return self.actionResult;
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
        return MotorControlRS::preparePosition(position, self.axisConfig, self.axisReference.nativeKnown ? &self.axisReference : nullptr, out.prepared);
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

void debugFixtures() {
    Fake fake; auto host=fake.host(); Probe::Console console(host);
    MotorControlRS::TrafficRecord tx; tx.kind=MotorControlRS::TrafficKind::TX;
    tx.sequence=1; tx.transaction=1; tx.atUs=100; tx.complete=true;
    tx.length=static_cast<uint16_t>(Ess::buildReadRegisters(1,0,1,tx.bytes,sizeof(tx.bytes)));
    assert(console.reportTraffic(tx,Probe::DebugMode::DECODED));
    auto rx=tx; rx.kind=MotorControlRS::TrafficKind::RX; rx.sequence=2; rx.atUs=rx.endUs=200; rx.startUs=150;
    rx.bytes[0]=1; rx.bytes[1]=3; rx.bytes[2]=2; rx.bytes[3]=0x4E; rx.bytes[4]=0xEA; rx.length=7;
    const auto crc=Ess::calcCrc16(rx.bytes,5); rx.bytes[5]=crc; rx.bytes[6]=crc>>8;
    assert(console.reportTraffic(rx,Probe::DebugMode::DECODED,&tx));
    rx.sequence=3; rx.bytes[6]^=1;
    assert(console.reportTraffic(rx,Probe::DebugMode::DECODED,&tx));
    rx.sequence=4; rx.complete=false; rx.length=3;
    assert(console.reportTraffic(rx,Probe::DebugMode::RAW));
    MotorControlRS::TrafficRecord end; end.kind=MotorControlRS::TrafficKind::END;
    end.sequence=5; end.transaction=1; end.atUs=300; end.code=1;
    assert(console.reportTraffic(end,Probe::DebugMode::DECODED));
    for (const auto& line:fake.lines) std::puts(line.c_str());
}

void testDebugTranslationAndOutputIsolation() {
    Fake fake; auto host=fake.host(); host.debug=Fake::debug; Probe::Console console(host);
    fake.data.phase=Rtu::Phase::RECEIVE; fake.data.busy=true; fake.data.pending=2; fake.data.retained=3;
    fake.data.timerCapture=true; fake.data.captureFaults=4; fake.data.maxPollGapUs=17;
    fake.data.memoryValid=true; fake.data.stackFreeBytes=2345; fake.data.internalFree=12345; fake.data.psramFree=54321;
    fake.debugState.missed=4; fake.debugState.skipped=7;
    send(console,"@1 debug decoded\n"); fake.contains("\"mode\":\"decoded\"");
    fake.contains("\"missed\":4,\"skipped\":7");
    fake.contains("\"owner\":{\"phase\":\"RECEIVE\",\"busy\":true,\"recovery_required\":false,\"pending\":2,\"retained\":3}");
    fake.contains("\"capture\":{\"mode\":\"timer\",\"faults\":4,\"max_poll_gap_us\":17}");
    fake.contains("\"memory\":{\"valid\":true,\"stack_free_bytes\":2345,\"internal_free\":12345,\"psram_free\":54321}");
    assert(fake.snapshots==1);
    assert(fake.debugCalls==1 && fake.debugState.mode==Probe::DebugMode::DECODED);
    MotorControlRS::TrafficRecord tx; tx.kind=MotorControlRS::TrafficKind::TX;
    tx.sequence=1; tx.transaction=1; tx.atUs=100; tx.complete=true;
    tx.length=static_cast<uint16_t>(Ess::buildReadRegisters(1,0,1,tx.bytes,sizeof(tx.bytes)));
    assert(console.reportTraffic(tx,Probe::DebugMode::DECODED));
    fake.contains("\"function_name\":\"read_registers\""); fake.contains("\"register_names\":[\"DRIVER_MODEL\"]");
    auto rx=tx; rx.kind=MotorControlRS::TrafficKind::RX; rx.sequence=2; rx.atUs=rx.endUs=200; rx.startUs=150;
    rx.bytes[0]=1; rx.bytes[1]=3; rx.bytes[2]=2; rx.bytes[3]=0x4E; rx.bytes[4]=0xEA; rx.length=7;
    const auto crc=Ess::calcCrc16(rx.bytes,5); rx.bytes[5]=crc; rx.bytes[6]=crc>>8;
    assert(console.reportTraffic(rx,Probe::DebugMode::DECODED,&tx)); fake.contains("\"words\":[20202]");
    rx.complete=false; rx.length=3;
    assert(console.reportTraffic(rx,Probe::DebugMode::DECODED,&tx)); fake.contains("\"decoded\":null");
    assert(console.reportTraffic(rx,Probe::DebugMode::RAW,&tx)); fake.contains("\"raw_hex\":\"010302\"");
    fake.contains("\"decode_status\":null");
    fake.blocked=true;
    assert(!console.reportTraffic(tx,Probe::DebugMode::RAW)); assert(!console.outputPending());
    send(console,"@2 status\n"); assert(console.outputPending());
    assert(!console.reportTraffic(tx,Probe::DebugMode::DECODED));
    send(console,"@3 debug off\n"); assert(fake.debugState.mode==Probe::DebugMode::OFF && console.outputPending());
    fake.blocked=false; assert(console.serviceOutput()); fake.contains("\"id\":2"); fake.contains("\"command\":\"status\"");
    assert(!console.reportTraffic(tx,Probe::DebugMode::OFF));
    send(console,"@4 debug invalid\n"); fake.contains("invalid_arguments"); assert(fake.debugCalls==2);
    send(console,"help debug\n"); fake.contains("debug [off|raw|decoded]"); fake.contains("\"bus_traffic\":false");
    send(console,"sniff raw\n"); fake.contains("unknown_command");
    Fake absent; Probe::Console other(absent.host()); send(other,"help\n");
    assert(absent.lines.back().find("\"debug\"")==std::string::npos);
    send(other,"help debug\n"); absent.contains("unavailable");
    fake.untouched();
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
    for (unsigned i = 1; i < Probe::OUTSTANDING_CAPACITY; ++i)
        send(full, "@" + std::to_string(i) + " probe\n");
    assert(bounded.probes == Probe::OUTSTANDING_CAPACITY - 1);
    send(full, "@100 probe\n@101 recover\n");
    bounded.contains("\"result\":\"busy\"");
    assert(bounded.probes == Probe::OUTSTANDING_CAPACITY - 1 && bounded.recoveries == 0);
    send(full, "@102 health\n"); bounded.contains("\"command\":\"health\"");
    assert(full.reportProbe(1, 1, 100, result));
    send(full, "@103 probe\n"); assert(bounded.probes == Probe::OUTSTANDING_CAPACITY);
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
    assert(fake.typedReads == 3 && fake.snapshots == snapshots + 2); // Cached action qualification only.
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
        block.invalidatedUs = UINT64_MAX - 1200;
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

void testActionRoutesAndStopPressure() {
    namespace Core = MotorControlRS;
    const char* commands[] = {"enable 2", "profile ess_rs enable 2", "motor-release 2", "profile ess_rs release 2",
        "alarm-clear 2", "profile ess_rs clear-alarm 2", "stop normal 2", "profile ess_rs normal-stop 2",
        "stop direct 2", "profile ess_rs emergency-stop 2"};
    for (unsigned i = 0; i < 10; ++i) {
        Fake fake; auto host = fake.host(false, true); host.startAction = Fake::startAction;
        Probe::Console console(host); send(console, std::string("@42 ") + commands[i] + "\n");
        assert(fake.actions == 1 && fake.address == 2 && fake.id == 42);
        assert(fake.actionRequest.kind == (i < 2 ? Core::ActionKind::ENABLE : i < 4 ? Core::ActionKind::RELEASE : i < 6 ? Core::ActionKind::CLEAR_ALARM : Core::ActionKind::STOP));
        if (i >= 6) assert(fake.actionRequest.stop.behavior == (i < 8 ? Core::StopBehavior::CONFIGURED_DECELERATION : Core::StopBehavior::DIRECT));
        fake.contains("\"result\":\"accepted\"");
        Ess::ActionContext context; context.operationId = 100; context.request = fake.actionRequest;
        context.target.id = 2; context.target.address = 2; context.target.generation = 1;
        context.state = Core::ActionState::FAILED; context.outcome = Core::ActionOutcome::TRANSPORT_ERROR;
        context.execution = Core::ActionExecution::UNKNOWN; context.writeEvidence.txAccepted = 3;
        assert(!console.reportAction(43, 100, context));
        assert(console.reportAction(42, 100, context)); fake.contains("\"type\":\"action\""); fake.contains("\"execution\":\"unknown\"");
        assert(!console.reportAction(42, 100, context));
        fake.view.actionContext = &context; fake.view.commandId = 42; fake.view.operationId = 100;
        send(console, "result 100\n"); fake.contains("\"command\":\"result\""); fake.contains("\"tx_accepted\":3");
    }
    Fake fake; auto host = fake.host(); host.startAction = Fake::startAction; Probe::Console console(host);
    for (const char* text : {"stop", "stop zero", "stop normal 0", "stop normal 2 3", "enable -1", "alarm-clear 1junk", "profile ess_rs release 248"}) {
        send(console, std::string(text) + "\n"); fake.contains("\"ok\":false"); assert(fake.actions == 0);
    }
    for (unsigned i = 1; i < Probe::OUTSTANDING_CAPACITY; ++i) send(console, "@" + std::to_string(100 + i) + " probe\n");
    send(console, "enable\n"); assert(fake.actions == 0); fake.contains("busy");
    fake.blocked = true; send(console, "@500 status\n"); assert(console.outputPending());
    send(console, "@501 stop direct\n"); assert(fake.actions == 1);
    send(console, "@502 stop normal\n"); assert(fake.actions == 1); // Reserved admission reply cannot be overwritten.
    Ess::ActionContext stopped; stopped.operationId = fake.nextOperation - 1; stopped.request = fake.actionRequest;
    stopped.state = Core::ActionState::SUCCEEDED; stopped.completion = Core::ActionCompletion::OBSERVED;
    assert(!console.reportAction(501, stopped.operationId, stopped));
    const auto before = fake.lines.size(); fake.blocked = false; assert(console.serviceOutput());
    assert(fake.lines.size() == before + 2); fake.contains("\"id\":501"); fake.contains("accepted");
    assert(console.reportAction(501, stopped.operationId, stopped)); fake.contains("\"completion\":\"observed\"");
    // Failed stop admission is also retained under pressure and has no terminal.
    fake.actionResult = Probe::Action::TIMING_UNQUALIFIED; fake.blocked = true;
    send(console, "@600 status\n@601 stop normal\n"); assert(fake.actions == 2);
    fake.blocked = false; assert(console.serviceOutput()); fake.contains("timing_unqualified");
}

void testMaximumActionAndInvalidatedCacheFormatting() {
    namespace Core = MotorControlRS;
    Fake fake; auto host = fake.host(false, true); host.startAction = Fake::startAction;
    Probe::Console console(host); fake.nextOperation = UINT32_MAX;
    send(console, "@4294967295 motor-release 247\n");
    Ess::ActionContext context; context.operationId = UINT32_MAX;
    context.target.id = context.target.generation = UINT32_MAX; context.target.address = 247;
    context.request.kind = Core::ActionKind::RELEASE; context.state = Core::ActionState::FAILED;
    context.execution = Core::ActionExecution::ACKNOWLEDGED; context.outcome = Core::ActionOutcome::OBSERVATION_LIMIT;
    context.startedUs = context.deadlineUs = context.servicedUs = UINT64_MAX;
    context.observationKnown = true; context.rawAlarm = context.rawMotion = UINT16_MAX; context.polls = 64;
    for (auto* e : {&context.writeEvidence, &context.lastObservation, &context.failureEvidence}) {
        e->step = 64; e->event = Core::ReadEventKind::TRANSPORT_FAILURE; e->length = sizeof(e->raw); e->receivedLength = UINT32_MAX;
        for (auto& byte : e->raw) byte = 255;
        e->txAccepted = 8; e->earliestUs = e->latestUs = e->deliveredUs = UINT64_MAX;
        e->transportDetail = INT32_MIN; e->status = Core::Status(Core::Err::INVALID_CONFIG, INT32_MIN, "not serialized");
    }
    assert(console.reportAction(UINT32_MAX, UINT32_MAX, context, true));
    fake.contains("\"interrupted_by_stop\":true"); fake.contains("FFFFFFFFFFFFFFFFFF");
    std::printf("Maximum-width action line: %zu/%zu bytes\n", fake.lines.back().size(), Probe::OUTPUT_CAPACITY);
    Probe::StateCache cache; auto& block = cache.blocks[0]; block.valid = true;
    block.value.target.id = block.value.target.address = 1; block.value.target.generation = 1;
    block.observedEarliestUs = 10; block.invalidatedUs = 20;
    fake.data.bindingGeneration = 1; fake.data.nowUs = 30; fake.data.stateCache = &cache;
    send(console, "status\n"); fake.contains("\"invalidated_us\":20"); fake.contains("\"current\":false");
    assert(!Probe::current(block, block.value.target));
    block.observedEarliestUs = 21; assert(Probe::current(block, block.value.target));
}

void testMoveRoutesExactParsingAndRetainedReports() {
    namespace Core = MotorControlRS;
    for (const char* route : {"move relative", "profile ess_rs move-relative"}) {
        Fake fake; auto host = fake.host(false, true); host.startMove = Fake::startMove; host.axis = Fake::axis;
        fake.axisConfig.generation = 17;
        Probe::Console console(host);
        send(console, std::string("@42 ") + route + " -12.50 deg load 60 configured 2\n");
        assert(fake.moves == 1 && fake.address == 2 && fake.id == 42);
        assert(fake.moveRequest.position.value.numerator == -25 && fake.moveRequest.position.value.denominator == 2);
        assert(fake.moveRequest.position.unit == Core::PositionUnit::DEGREES && fake.moveRequest.position.frame == Core::CoordinateFrame::LOAD);
        assert(fake.moveRequest.position.relative && fake.moveRequest.position.rounding == Core::Rounding::EXACT);
        assert(fake.moveRequest.position.basis == Core::RelativeBasis::ACTUAL && fake.moveRequest.position.configurationGeneration == 17);
        assert(fake.moveRequest.speedRpm == 60 && fake.moveRequest.ramp == Core::MoveRamp::VERIFIED_CONFIGURED);
        fake.contains("\"command\":\"move-relative\""); fake.contains("\"result\":\"accepted\"");
        Ess::MoveContext context; context.operationId = 100; context.request = fake.moveRequest;
        context.state = Core::ActionState::FAILED; context.outcome = Core::ActionOutcome::CANCELLED;
        context.prepared.effectiveNative = context.prepared.displacementNative = -25;
        context.prepared.configurationGeneration = 17; context.execution = Core::ActionExecution::UNKNOWN;
        context.stagingApplied = context.uncertain = true;
        context.stagingEvidence.txAccepted = 19; context.triggerEvidence.txAccepted = 3;
        assert(!console.reportMove(43, 100, context));
        fake.blocked = true; assert(console.reportMove(42, 100, context, true));
        assert(console.outputPending() && !console.reportMove(42, 100, context));
        send(console, "@43 move relative 1 steps native 60 configured\n"); assert(fake.moves == 1);
        fake.blocked = false; assert(console.serviceOutput()); fake.contains("\"type\":\"move\"");
        fake.contains("\"uncertain\":true"); fake.contains("\"interrupted_by_stop\":true");
        fake.contains("\"effective_native\":-25"); fake.contains("\"tx_accepted\":19");
        assert(!console.reportMove(42, 100, context));
        fake.view.moveContext = &context; fake.view.commandId = 42; fake.view.operationId = 100;
        send(console, "@44 result 100\n"); fake.contains("\"command\":\"result\""); fake.contains("\"move_kind\":\"relative\"");
        context.request.position.rounding = Core::Rounding::NEAREST;
        context.request.position.maximumQuantizationError = 0.5;
        context.prepared.roundingError = -0.5;
        send(console, "@45 result 100\n"); fake.contains("\"rounding\":1");
        fake.contains("\"maximum_quantization_error\":0.5"); fake.contains("\"rounding_error\":-0.5");
        context.request.position.unit = Core::PositionUnit::RADIANS;
        context.request.position.approximate = true; context.request.position.rationalRadians = false;
        context.request.position.radians = -1; context.request.position.maximumApproximationError = 0.01;
        context.prepared.exactArithmetic = false; context.prepared.approximateRequestedNative = -25.125;
        context.prepared.roundingError = 0.125; context.prepared.approximationErrorBound = 0.001;
        send(console, "@46 result 100\n"); fake.contains("\"radians\":-1");
        fake.contains("\"rational_radians\":false"); fake.contains("\"requested_native_approximate\":-25.125");
        fake.view.pending = true; send(console, "@45 result 100\n"); fake.contains("\"result\":\"pending\"");
        fake.contains("\"move_kind\":\"relative\"");
    }
    Fake fake; auto host = fake.host(); host.startMove = Fake::startMove; host.axis = Fake::axis;
    Probe::Console console(host);
    for (const char* input : {"move", "move absolute 1 steps native 60", "move relative 1 steps native 0 configured",
        "move relative 1 steps native -1 configured", "move relative 1 steps native 60.5 configured", "move relative 1 steps native 65536 configured",
        "move relative 1/2.0 steps native 60 configured", "move relative NaN steps native 60 configured", "move relative 1 deg other 60 configured",
        "move relative 1 steps native 60 default", "move relative 1 steps native 60 configured 0", "move relative 1 steps native 60 configured 248",
        "move relative 1 steps native 60 configured 1 2", "profile ess_rs move-relative 1 steps native 60"}) {
        send(console, std::string(input) + "\n"); fake.contains("\"ok\":false"); assert(fake.moves == 0);
    }
    fake.actionResult = Probe::Action::TIMING_UNQUALIFIED;
    send(console, "move relative 1 steps native 60 configured\n"); fake.contains("timing_unqualified");
    fake.actionResult = Probe::Action::AXIS_CONFLICT;
    send(console, "move relative 1 steps native 60 configured\n"); fake.contains("axis_conflict");
    assert(fake.moves == 2);
    Fake absent; Probe::Console unavailable(absent.host()); send(unavailable, "help\n");
    assert(absent.lines.back().find("\"move\"") == std::string::npos);
    for (bool missingSnapshot : {false, true}) {
        Fake partial; auto partialHost = partial.host(); partialHost.startMove = Fake::startMove;
        partialHost.axis = Fake::axis;
        if (missingSnapshot) partialHost.snapshot = nullptr;
        else partialHost.axis = nullptr;
        Probe::Console incomplete(partialHost); send(incomplete, "help\n");
        assert(partial.lines.back().find("\"move\"") == std::string::npos);
        send(incomplete, "help move\n"); partial.contains("\"result\":\"unavailable\"");
        send(incomplete, "move relative 1 steps native 60 configured\n");
        partial.contains("\"result\":\"unavailable\""); assert(partial.moves == 0);
        send(incomplete, "caps\n");
        if (missingSnapshot) partial.contains("\"result\":\"unavailable\"");
        else partial.contains("\"motion\":false");
    }
}

void testMaximumMoveReportFitsFixedOutput() {
    namespace Core = MotorControlRS;
    Fake fake; auto host = fake.host(false, true); host.startMove = Fake::startMove; host.axis = Fake::axis;
    Probe::Console console(host); fake.nextOperation = UINT32_MAX;
    send(console, "@4294967295 move relative 1 steps native 3000 configured 247\n");
    Ess::MoveContext c; c.operationId = UINT32_MAX; c.request = fake.moveRequest;
    c.target.id = c.target.generation = c.prepared.configurationGeneration = UINT32_MAX; c.target.address = 247;
    c.state = Core::ActionState::FAILED; c.outcome = Core::ActionOutcome::UNCONFIRMED_RESPONSE;
    c.setupExecution = c.execution = Core::ActionExecution::ACKNOWLEDGED;
    c.stagingApplied = c.uncertain = c.runningObserved = c.observationKnown = true;
    c.rawAlarm = c.rawMotion = UINT16_MAX; c.polls = 64;
    c.startedUs = c.deadlineUs = c.servicedUs = UINT64_MAX;
    c.request.position.value = Core::Rational(INT64_MIN, UINT64_MAX);
    c.request.position.unit = Core::PositionUnit::RADIANS;
    c.request.position.frame = Core::CoordinateFrame::MOTOR;
    c.request.position.rounding = Core::Rounding::FLOOR;
    c.request.position.approximate = true; c.request.position.rationalRadians = false;
    c.request.position.radians = -1.2345678901234567e+18;
    c.request.position.maximumQuantizationError = c.request.position.maximumApproximationError = std::numeric_limits<double>::max();
    c.prepared.exactArithmetic = false;
    c.prepared.approximateRequestedNative = -2147483647.875;
    c.prepared.roundingError = -0.125; c.prepared.approximationErrorBound = 3.0517578124998224e-5;
    c.prerequisites.target.id = c.prerequisites.target.generation = c.prerequisites.configurationGeneration = UINT32_MAX;
    c.prerequisites.observedUs = c.prerequisites.maximumAgeUs = UINT64_MAX;
    c.prerequisites.rawAlarm = c.prerequisites.rawMotion = c.prerequisites.startSpeed = UINT16_MAX;
    c.prepared.effectiveNative = c.prepared.displacementNative = INT32_MIN;
    c.prepared.endpointKnown = true; c.prepared.endpointNative = INT64_MIN;
    c.request.position.relative = false; c.request.position.wrapped = true;
    c.reference.target.id = c.reference.target.generation = c.reference.configurationGeneration = UINT32_MAX;
    c.reference.nativeKnown = true; c.reference.nativePosition = INT64_MIN;
    c.reference.observedUs = c.reference.maximumAgeUs = UINT64_MAX;
    for (auto& word : c.words) word = UINT16_MAX;
    for (auto* e : {&c.stagingEvidence, &c.triggerEvidence, &c.activityEvidence, &c.lastObservation, &c.failureEvidence}) {
        e->step = 65; e->event = Core::ReadEventKind::TRANSPORT_FAILURE;
        e->length = sizeof(e->raw); e->receivedLength = UINT32_MAX; e->txAccepted = 19;
        for (auto& byte : e->raw) byte = 255;
        e->earliestUs = e->latestUs = e->deliveredUs = UINT64_MAX;
        e->transportDetail = INT32_MIN; e->status = Core::Status(Core::Err::INVALID_CONFIG, INT32_MIN, "not serialized");
    }
    assert(console.reportMove(UINT32_MAX, UINT32_MAX, c, true));
    fake.contains("\"interrupted_by_stop\":true"); fake.contains("FFFFFFFFFFFFFFFFFF");
    fake.contains("\"requested_native_approximate\":-2147483647.875");
    std::printf("Maximum-width move line: %zu/%zu bytes\n", fake.lines.back().size(), Probe::OUTPUT_CAPACITY);
}

void testAbsoluteAngleAndClearRoutesUsePublicRequests() {
    namespace Core = MotorControlRS;
    Fake fake; auto host = fake.host(false, true);
    host.axis = Fake::axis; host.startMove = Fake::startMove; host.startAction = Fake::startAction;
    fake.axisConfig.target.id = fake.axisConfig.target.address = 1;
    fake.axisConfig.target.generation = 9; fake.axisConfig.generation = 17;
    fake.axisConfig.units.commandStepsPerMotorTurn = Core::UnitScale(1000, 1, Core::ScaleSource::ASSUMED);
    fake.axisConfig.originKnown = true; fake.axisConfig.originNative = 0; fake.axisConfig.originSource = Core::ScaleSource::ASSUMED;
    fake.axisReference.target = fake.axisConfig.target; fake.axisReference.configurationGeneration = 17;
    fake.axisReference.nativeKnown = true; fake.axisReference.nativePosition = 2000;
    fake.axisReference.source = Core::ScaleSource::ASSUMED;
    fake.axisReference.observedUs = fake.axisReference.nowUs = 100; fake.axisReference.maximumAgeUs = 1000;
    Probe::Console console(host);
    for (const char* route : {"move absolute", "profile ess_rs move-absolute"}) {
        send(console, std::string(route) + " 720 deg motor 60 configured\n");
        assert(!fake.moveRequest.position.relative && !fake.moveRequest.position.wrapped);
        assert(fake.moveRequest.position.value.numerator == 720 && fake.moveRequest.position.unit == Core::PositionUnit::DEGREES);
        fake.contains("\"command\":\"move-absolute\"");
    }
    for (const char* route : {"move angle", "profile ess_rs move-angle"}) {
        send(console, std::string(route) + " 1/2 turn motor shortest negative 60 configured\n");
        assert(!fake.moveRequest.position.relative && fake.moveRequest.position.wrapped);
        assert(fake.moveRequest.position.path == Core::AnglePath::SHORTEST && fake.moveRequest.position.tie == Core::HalfTurnTie::NEGATIVE);
        fake.contains("\"command\":\"move-angle\"");
    }
    send(console, "move relative 1 rad motor 60 configured round nearest 1 approx 0.001\n");
    assert(fake.moveRequest.position.approximate && fake.moveRequest.position.rationalRadians);
    assert(fake.moveRequest.position.maximumApproximationError > 0);
    send(console, "move relative 1 steps native 60 configured basis commanded\n");
    assert(fake.moveRequest.position.basis == Core::RelativeBasis::COMMANDED);
    const unsigned admitted = fake.moves;
    for (const char* input : {"move angle 0 deg motor shortest 60 configured", "move angle 0 deg motor wrong reject 60 configured",
        "move absolute 1 steps native 60 configured basis actual", "move relative 1 deg motor 60 configured round nearest 1 approx 0.01",
        "move relative 1 rad motor 60 configured round nearest 1 approx 0", "move absolute 1 steps native 60 configured round nearest"}) {
        send(console, std::string(input) + "\n"); fake.contains("\"ok\":false"); assert(fake.moves == admitted);
    }
    send(console, "prepare angle 90 deg motor positive reject\n");
    fake.contains("\"ok\":true"); fake.contains("\"wrapped\":true"); fake.contains("\"endpoint_native\":2250");
    auto direct = fake.axisRequest.position; direct.configurationGeneration = 17;
    Core::PreparedTarget prepared; assert(Core::preparePosition(direct, fake.axisConfig, &fake.axisReference, prepared));
    assert(prepared.endpointNative == 2250);
    Fake units; auto unitHost = units.host(); unitHost.axis = Fake::axis; unitHost.startMove = Fake::startMove;
    Probe::Console unitConsole(unitHost);
    for (const char* unit : {"steps", "fullsteps", "turn", "deg", "rad", "mm"}) {
        send(unitConsole, std::string("move absolute 1 ") + unit + " motor 60 configured\n");
        assert(!units.moveRequest.position.relative);
    }
    assert(units.moves == 6);
    Fake clear; auto clearHost = clear.host(); clearHost.startAction = Fake::startAction;
    Probe::Console clearConsole(clearHost);
    for (const char* route : {"position-clear", "profile ess_rs clear-position"}) {
        send(clearConsole, std::string(route) + " 2\n");
        assert(clear.actionRequest.kind == Core::ActionKind::CLEAR_POSITION && clear.actionRequest.devicePosition == 0);
        assert(!clear.actionRequest.positionClearQualified && clear.address == 2);
        clear.contains("\"command\":\"position-clear\"");
    }
    const unsigned actions = clear.actions;
    send(clearConsole, "position-clear 1 12\n"); clear.contains("\"ok\":false"); assert(clear.actions == actions);
    send(clearConsole, "help position-clear\n"); clear.contains("explicit_device_position_zero_only");
    Ess::ActionContext cleared; cleared.operationId = 100; cleared.request.kind = Core::ActionKind::CLEAR_POSITION;
    cleared.request.positionClearQualified = true; cleared.state = Core::ActionState::SUCCEEDED;
    cleared.completion = Core::ActionCompletion::OBSERVED; cleared.outcome = Core::ActionOutcome::OBSERVED;
    cleared.observationKnown = true; cleared.rawPosition = 0;
    assert(clearConsole.reportAction(1, 100, cleared));
    clear.contains("\"raw_position\":0"); clear.contains("\"raw_alarm\":null,\"raw_motion\":null");
    clear.contains("\"device_position\":0,\"position_clear_qualified\":true");
    auto partialHost = clear.host(); partialHost.startAction = Fake::startAction; partialHost.snapshot = nullptr;
    Probe::Console partial(partialHost); send(partial, "help position-clear\n"); clear.contains("\"result\":\"unavailable\"");
    send(console, "help move\n"); fake.contains("move angle");
}

void testVelocityExactRoutesRetentionAndBound() {
    namespace Core = MotorControlRS;
    Fake fake; auto host = fake.host(false, true); host.startVelocity = Fake::startVelocity; host.axis = Fake::axis;
    fake.axisConfig.target.id = 1; fake.axisConfig.target.generation = 1; fake.axisConfig.target.address = 1;
    Probe::Console console(host);
    send(console, "help velocity\n"); fake.contains("counts/s");
    send(console, "@41 velocity -3/2 rpm native 500 configured normal round nearest 1 2\n");
    assert(fake.velocities == 1 && fake.address == 2 && fake.id == 41);
    assert(fake.velocityRequest.value.numerator == -3 && fake.velocityRequest.value.denominator == 2);
    assert(fake.velocityRequest.durationUs == 500000 && fake.velocityRequest.rounding == Core::Rounding::NEAREST);
    assert(fake.velocityRequest.stop.behavior == Core::StopBehavior::CONFIGURED_DECELERATION);
    Core::PreparedVelocityTarget target;
    assert(Core::prepareVelocityTarget(fake.velocityRequest, fake.axisConfig, target)); assert(target.nativeRpm == -2);
    send(console, "profile ess_rs velocity 180 deg/s motor 100 configured direct\n");
    assert(fake.velocities == 2 && fake.velocityRequest.unit.position == Core::PositionUnit::DEGREES);
    assert(Core::prepareVelocityTarget(fake.velocityRequest, fake.axisConfig, target)); assert(target.nativeRpm == 30);
    const auto count = fake.velocities;
    for (const auto* invalid : {"velocity nan rpm native 100 configured normal", "velocity 1/0 rpm native 100 configured normal",
            "velocity 1 rpm native 0 configured normal", "velocity 1 rpm native 10001 configured normal", "velocity 1 rpm native 10 acceleration normal",
            "velocity 1 rpm native 10 configured release", "velocity 1 rpm native 10 configured normal round nearest -1",
            "velocity 1 rpm native 10 configured normal round nearest 1 approx 1", "velocity 1 rpm native 10 configured normal junk",
            "jog", "torque 1", "current 1", "velocity-update 10", "profile ess_rs jog"}) { send(console, (std::string(invalid) + "\n").c_str()); fake.contains("\"ok\":false"); }
    assert(fake.velocities == count);
    Ess::VelocityContext c; c.operationId = 100; c.request.configurationGeneration = 1;
    c.state = Core::ActionState::FAILED; c.outcome = Core::ActionOutcome::CANCELLED;
    assert(!console.reportVelocity(42, 100, c));
    fake.blocked = true; assert(console.reportVelocity(41, 100, c, true)); assert(!console.reportVelocity(41, 100, c));
    fake.blocked = false; assert(console.serviceOutput()); fake.contains("\"velocity\":true"); fake.contains("\"interrupted_by_stop\":true");
    fake.view.velocityContext = &c; fake.view.commandId = 41; fake.view.operationId = 100;
    send(console, "result 100\n"); fake.contains("\"command\":\"result\"");
    fake.nextOperation = UINT32_MAX;
    send(console, "@4294967295 velocity -3000 rpm native 100 configured direct 247\n");
    c.operationId = UINT32_MAX; c.request = fake.velocityRequest;
    c.target.id = c.target.generation = c.request.configurationGeneration = UINT32_MAX; c.target.address = 247;
    c.startedUs = c.deadlineUs = c.stopDueUs = c.servicedUs = UINT64_MAX; c.polls = 64;
    c.request.value = Core::Rational(INT64_MIN, UINT64_MAX); c.request.unit.position = Core::PositionUnit::RADIANS;
    c.request.maximumQuantizationErrorRpm = c.request.maximumApproximationErrorRpm = std::numeric_limits<double>::max();
    c.prepared.nativeRpm = -3000; c.prepared.approximateRequestedRpm = -1.2345678901234567e+18;
    c.prepared.roundingError = -0.12345678901234567; c.prepared.approximationErrorBound = 1.2345678901234567e-18;
    c.setupExecution = c.execution = c.stop.execution = Core::ActionExecution::NOT_TRANSMITTED;
    c.observationKnown = true; c.rawAlarm = c.rawMotion = UINT16_MAX;
    for (auto& word : c.words) word = UINT16_MAX;
    for (auto* e : {&c.stagingEvidence, &c.triggerEvidence, &c.activityEvidence, &c.lastObservation, &c.failureEvidence,
                   &c.stop.writeEvidence, &c.stop.lastObservation, &c.stop.failureEvidence}) {
        e->step = 255; e->event = Core::ReadEventKind::TRANSPORT_FAILURE; e->length = sizeof(e->raw);
        e->receivedLength = UINT32_MAX; e->txAccepted = 15; for (auto& byte : e->raw) byte = 255;
        e->earliestUs = e->latestUs = e->deliveredUs = UINT64_MAX;
        e->transportDetail = INT32_MIN; e->status = Core::Status(Core::Err::INVALID_CONFIG, INT32_MIN, "not serialized");
    }
    assert(console.reportVelocity(UINT32_MAX, UINT32_MAX, c, true));
    std::printf("Maximum-width velocity line: %zu/%zu bytes\n", fake.lines.back().size(), Probe::OUTPUT_CAPACITY);
}

// Produce records from actual public sequencing and console serialization. The
// Python parity test consumes this mode; no terminal fields are handwritten.
void velocityFixtures() {
    using namespace MotorControlRS;
    AxisConfig axis; axis.target.id = 1; axis.target.address = 1;
    axis.target.generation = 9; axis.generation = 3;
    VelocityRequest request; request.value = Rational(60);
    request.configurationGeneration = axis.generation;
    request.ramp = VelocityRamp::VERIFIED_CONFIGURED; request.durationUs = 1000;
    request.stop.behavior = StopBehavior::CONFIGURED_DECELERATION;
    Ess::VelocityPrerequisites prerequisites;
    prerequisites.target = axis.target; prerequisites.configurationGeneration = axis.generation;
    prerequisites.nativeRpmVerified = prerequisites.configuredRampVerified = true;
    prerequisites.serialInputsPermit = prerequisites.readinessQualified = true;
    prerequisites.accelerationTime = prerequisites.decelerationTime = 100;
    prerequisites.observedUs = 80; prerequisites.maximumAgeUs = 2000;
    prerequisites.rawMotion = 1;
    ActionOptions options; options.pollIntervalUs = 100; options.maxPolls = 8;
    const auto prepare = [&](uint64_t readinessAge, bool radians) {
        auto p = prerequisites; p.maximumAgeUs = readinessAge;
        auto r = request;
        if (radians) {
            r.value = Rational(6283185, 1000000); r.frame = CoordinateFrame::MOTOR;
            r.unit = VelocityUnit(PositionUnit::RADIANS, TimeUnit::SECOND);
            r.approximate = true; r.rounding = Rounding::NEAREST;
            r.maximumApproximationErrorRpm = 0.000001; r.maximumQuantizationErrorRpm = 0.001;
        }
        Ess::VelocityContext c;
        assert(Ess::prepareVelocity(c, axis, 102, r, p, 100, 10000, options));
        return c;
    };
    const auto consume = [](Ess::VelocityContext& c, uint16_t motion, uint64_t delivered) {
        Ess::PreparedVelocity work;
        assert(Ess::nextVelocity(c, c.eligibleUs, work));
        assert(work.kind == Ess::ActionWork::TRANSACTION);
        uint8_t reply[9] = {}; std::size_t length = 0;
        if (work.function == 6) { std::memcpy(reply, work.bytes, 8); length = 8; }
        else if (work.function == 16) { std::memcpy(reply, work.bytes, 6); length = 8; sealTypedReply(reply, length); }
        else {
            reply[0] = c.target.address; reply[1] = 3; reply[2] = 4;
            reply[5] = static_cast<uint8_t>(motion >> 8); reply[6] = static_cast<uint8_t>(motion);
            length = 9; sealTypedReply(reply, length);
        }
        ActionEvent event; event.transport.target = c.target;
        event.transport.operationId = c.operationId; event.transport.step = c.step;
        event.transport.frame = reply; event.transport.length = length;
        event.transport.txAccepted = work.length;
        event.transport.qualified = event.txComplete = event.responseConfirmed = true;
        event.transport.earliestUs = c.eligibleUs + 20;
        event.transport.latestUs = c.eligibleUs + 30;
        assert(Ess::advanceVelocity(c, event, delivered ? delivered : c.eligibleUs + 40));
    };
    const auto emit = [](const char* name, const Ess::VelocityContext& c) {
        assert(c.state != ActionState::ACTIVE);
        Fake fake; fake.view.velocityContext = &c; fake.view.commandId = 2;
        fake.view.operationId = c.operationId; fake.view.address = c.target.address;
        Probe::Console console(fake.host(false, true)); send(console, "@1 result 102\n");
        assert(fake.lines.size() == 1 && fake.lines[0].find("\"velocity\":true") != std::string::npos);
        std::printf("{\"case\":\"%s\",\"record\":%s}\n", name, fake.lines[0].c_str());
    };
    auto c = prepare(100, false); assert(Ess::serviceVelocity(c, 180)); emit("readiness_before_tx", c);
    c = prepare(100, false); consume(c, 4, 0); assert(Ess::serviceVelocity(c, 180)); emit("readiness_after_staging", c);
    c = prepare(2000, false); consume(c, 4, 0); assert(Ess::serviceVelocity(c, c.stopDueUs)); emit("duration_after_staging", c);
    for (const char* name : {"absent_activity", "service_missed", "success", "radians"}) {
        c = prepare(2000, std::strcmp(name, "radians") == 0); consume(c, 4, 0); consume(c, 4, 0);
        consume(c, std::strcmp(name, "absent_activity") == 0 ? 1 : 4, 0);
        assert(Ess::serviceVelocity(c, c.stopDueUs + (std::strcmp(name, "service_missed") == 0 ? 101 : 0)));
        consume(c, 4, 0); consume(c, 1, 0); emit(name, c);
    }
    c = prepare(2000, false); consume(c, 4, 0); consume(c, 4, 0); consume(c, 4, 0);
    assert(Ess::serviceVelocity(c, c.stopDueUs));
    ActionEvent failure; failure.transport.target = c.target;
    failure.transport.operationId = c.operationId; failure.transport.step = c.step;
    failure.transport.kind = ReadEventKind::TRANSPORT_FAILURE;
    failure.transport.txAccepted = 8; failure.transport.executionUnknown = true;
    assert(Ess::advanceVelocity(c, failure, c.servicedUs + 1)); emit("stop_failure", c);
    c = prepare(2000, false); consume(c, 4, 0); consume(c, 4, c.stopDueUs + 101);
    consume(c, 4, 0); consume(c, 1, 0); emit("late_trigger", c);
    c = prepare(2000, false); consume(c, 4, 0); consume(c, 4, 0); consume(c, 4, 0);
    assert(Ess::serviceVelocity(c, c.stopDueUs)); consume(c, 4, 0);
    assert(Ess::serviceVelocity(c, c.deadlineUs)); emit("stop_deadline_after_ack", c);
}

} // namespace

void testDriverProfileGrammarAndRetainedReports() {
    Fake fake; auto host = fake.host(false, true); host.startDriver = Fake::startDriver;
    Probe::Console console(host);
    send(console, "@600 profile ess_rs driver read 2\n");
    assert(fake.drivers == 1 && fake.driverKind == Ess::DriverKind::READ && fake.driverRequest.fields == 0 && fake.address == 2);
    fake.contains("\"result\":\"accepted\"");
    Ess::DriverContext result; result.operationId = fake.nextOperation - 1; result.target.id = 1;
    result.target.address = 2; result.target.generation = 3; result.state = MotorControlRS::ReadState::FAILED;
    result.outcome = Ess::DriverOutcome::CANCELLED;
    result.status = {MotorControlRS::Err::ILLEGAL_VALUE, static_cast<int32_t>(Ess::DriverError::CANCELLED), "cancelled"};
    result.startedUs = 1; result.deadlineUs = 100; result.servicedUs = 2;
    assert(console.reportDriver(600, result.operationId, result));
    fake.contains("\"driver_kind\":\"read\""); fake.contains("\"outcome\":\"cancelled\"");
    assert(!console.reportDriver(600, result.operationId, result));
    fake.view.commandId = 600; fake.view.operationId = result.operationId; fake.view.driverContext = &result;
    send(console, "@601 result " + std::to_string(result.operationId) + "\n");
    fake.contains("\"command\":\"result\""); fake.contains("\"driver\":true");
    send(console, "@602 profile ess_rs driver set direction 1 subdivision 1600 word-order 1 2\n");
    assert(fake.drivers == 2 && fake.driverKind == Ess::DriverKind::UPDATE && fake.driverRequest.fields == 7);
    assert(fake.driverRequest.direction == Ess::DefaultDirection::REVERSED && fake.driverRequest.subdivision == 1600 &&
           fake.driverRequest.wordOrder == Ess::WordOrder::LOW_WORD_FIRST);
    send(console, "@603 profile ess_rs driver set positive-limit 9223372036854775807 negative-limit -9223372036854775808\n");
    assert(fake.drivers == 3 && fake.driverRequest.fields == 384 && fake.driverRequest.positiveLimit == INT64_MAX && fake.driverRequest.negativeLimit == INT64_MIN);
    // Rejected lexical candidates never reach the public preparation callback.
    for (const char* line : {"profile ess_rs driver set\n", "profile ess_rs driver set direction 1 direction 0\n",
        "profile ess_rs driver set unknown 1\n", "profile ess_rs driver set subdivision -1\n",
        "profile ess_rs driver set subdivision 65536\n", "profile ess_rs driver set direction 1.0\n",
        "profile ess_rs driver set direction 1/1\n", "profile ess_rs driver read extra\n",
        "profile ess_rs driver set positive-limit 9223372036854775808\n", "driver read\n"}) {
        send(console, line); fake.contains("\"ok\":false"); assert(fake.drivers == 3);
    }
    fake.blocked = true; send(console, "version\n");
    send(console, "profile ess_rs driver set direction 0\n"); assert(fake.drivers == 3 && console.inputDropped() == 1);
    Fake noHook; Probe::Console unavailable(noHook.host()); send(unavailable, "profile ess_rs driver read\n"); noHook.contains("unavailable");
}

void testMaximumDriverReportFitsFixedOutput() {
    Fake fake; auto host = fake.host(false, true); host.startDriver = Fake::startDriver;
    Probe::Console console(host); send(console, "@900 profile ess_rs driver set direction 1\n");
    Ess::DriverContext c; c.kind = Ess::DriverKind::UPDATE; c.state = MotorControlRS::ReadState::FAILED;
    c.outcome = Ess::DriverOutcome::TRANSPORT_ERROR; c.status = {MotorControlRS::Err::ILLEGAL_VALUE, INT32_MIN, "transport"};
    c.operationId = fake.nextOperation - 1; c.target.id = c.target.generation = UINT32_MAX; c.target.address = 247;
    c.startedUs = c.deadlineUs = c.servicedUs = UINT64_MAX; c.request.fields = c.effects = 127; c.uncertain = true;
    for (unsigned i = 0; i < Ess::DRIVER_FIELD_COUNT; ++i) {
        auto& p = c.progress[i]; p.selected = true; p.field = static_cast<Ess::DriverField>(1U << i);
        p.reg = p.previous = p.requested = p.readback = p.active = UINT16_MAX;
        p.execution = MotorControlRS::ActionExecution::NOT_TRANSMITTED;
    }
    for (unsigned i = 0; i < Ess::DRIVER_MAX_STEPS; ++i) {
        auto& e = c.observations[i]; e.step = static_cast<uint8_t>(i); e.reg = 0x51; e.count = 1;
        std::memset(e.raw, 0xFF, sizeof(e.raw)); e.length = sizeof(e.raw); e.receivedLength = i == Ess::DRIVER_MAX_STEPS - 1 ? UINT32_MAX : sizeof(e.raw); e.txAccepted = 8;
        e.attemptedUs = e.earliestUs = e.latestUs = e.deliveredUs = UINT64_MAX; e.transportDetail = INT32_MIN;
        e.status = {MotorControlRS::Err::INVALID_CONFIG, INT32_MIN, "error"}; e.frameError = static_cast<Ess::FrameError>(255);
    }
    assert(console.reportDriver(900, c.operationId, c));
    fake.contains("\"driver_kind\":\"update\""); assert(fake.lines.back().size() < Probe::OUTPUT_CAPACITY);
    std::printf("Maximum-width driver line: %zu/%zu bytes\n", fake.lines.back().size(), Probe::OUTPUT_CAPACITY);
}

void driverFixtures(bool io = false) {
    using namespace MotorControlRS;
    ReadTarget target; target.id = target.address = 1; target.generation = 9;
    auto consume = [](Ess::DriverContext& c, int failure, bool unknown) {
        Ess::PreparedDriver p; assert(Ess::nextDriver(c, c.servicedUs, p));
        uint8_t raw[Ess::DRIVER_MAX_REPLY_BYTES] = {}; std::size_t length = 0;
        if (p.write) { length = 8; std::memcpy(raw, p.bytes, length); }
        else {
            uint16_t words[5] = {};
            if (c.kind == Ess::DriverKind::UPDATE) words[0] = static_cast<uint16_t>(p.value + (failure == 3 ? 1 : 0));
            else if (p.reg == 0x40) { words[0] = unknown ? 0x80 : 0; words[1] = unknown ? 99 : 1; words[2] = 2; words[3] = 3; }
            else if (p.reg == 0x4B) { words[1] = unknown ? 11 : 9; words[2] = 10; }
            else if (p.reg == 0x4F) { words[0] = unknown ? 8 : 0; }
            else if (p.reg == 0x10) { words[0] = unknown ? 77 : 0; words[1] = 1600; }
            else if (p.reg == 0x17) { words[0] = 0; words[1] = 0; words[2] = unknown ? 99 : 0; }
            else if (p.reg == 0x37) { words[0] = 1; words[1] = 2; words[2] = 3; words[3] = 4; }
            else { words[0] = 0; words[1] = 1; }
            length = 5 + 2 * p.count; raw[0] = c.target.address; raw[1] = 3; raw[2] = static_cast<uint8_t>(2 * p.count);
            for (unsigned i = 0; i < p.count; ++i) { raw[3 + 2*i] = static_cast<uint8_t>(words[i] >> 8); raw[4 + 2*i] = static_cast<uint8_t>(words[i]); }
            sealTypedReply(raw, length);
        }
        ActionEvent event; event.transport.target = c.target; event.transport.operationId = c.operationId;
        event.transport.step = p.step; event.transport.kind = ReadEventKind::FRAME; event.transport.frame = raw;
        event.transport.length = length; event.transport.txAccepted = 8; event.transport.qualified = true;
        event.transport.earliestUs = c.servicedUs + 1; event.transport.latestUs = c.servicedUs + 2;
        event.txComplete = event.responseConfirmed = true;
        uint64_t now = c.servicedUs + 3;
        if (failure == 1 || failure == 2 || failure == 4) {
            event.transport.kind = failure == 1 ? ReadEventKind::TRANSPORT_FAILURE : failure == 2 ? ReadEventKind::CANCEL : ReadEventKind::DEADLINE;
            event.transport.frame = nullptr; event.transport.length = 0; event.transport.qualified = false;
            event.transport.earliestUs = event.transport.latestUs = 0; event.responseConfirmed = false;
            event.transport.executionUnknown = failure == 1;
            if (failure == 4) now = c.deadlineUs;
        } else if (failure == 5) { raw[4] ^= 1; sealTypedReply(raw, length); }
        else if (failure == 6) now = c.deadlineUs + 1;
        else if (failure == 7) { event.transport.qualified = false; event.transport.earliestUs = event.transport.latestUs = 0; }
        else if (failure == 8) event.responseConfirmed = false;
        else if (failure == 9) { now = c.deadlineUs + 3; event.transport.earliestUs = c.deadlineUs + 1; event.transport.latestUs = c.deadlineUs + 2; }
        assert(Ess::advanceDriver(c, event, now));
    };
    auto read = [&](bool unknown) {
        Ess::DriverContext c; assert(Ess::prepareDriverRead(c, target, 101, 3, 100, 10000, io ? Ess::DriverGroup::IO : Ess::DriverGroup::DRIVE));
        while (c.state == ReadState::ACTIVE) consume(c, 0, unknown);
        return c;
    };
    Ess::DriverObservation previous; auto baseline = read(false); assert(Ess::getDriver(baseline, previous));
    auto prepare = [&]() {
        Ess::DriverRequest request; request.configurationGeneration = 3; request.fields = 127;
        request.direction = Ess::DefaultDirection::REVERSED; request.subdivision = 800; request.wordOrder = Ess::WordOrder::LOW_WORD_FIRST;
        request.overLimitStop = Ess::OverLimitStop::EMERGENCY_STOP; request.interruption = Ess::PvTriggerMode::RISING_EDGE;
        request.positionMode = Ess::PositionMode::ABSOLUTE;
        Ess::DriverPrerequisites evidence; evidence.previous = previous; evidence.configurationGeneration = 3;
        evidence.stationaryQualified = evidence.inputsPermit = true; evidence.stationaryTarget = target;
        evidence.stationaryEarliestUs = evidence.stationaryLatestUs = 100; evidence.maxAgeUs = 20000;
        if (io) {
            request.group = Ess::DriverGroup::IO; request.fields = 0x3FE00;
            request.outputFunctions[0] = Ess::OutputFunction::CUSTOM_0; request.outputFunctions[1] = Ess::OutputFunction::CUSTOM_1;
            evidence.ioLevelsQualified = true; evidence.ioTarget = target; evidence.ioConfigurationGeneration = 3;
            evidence.ioEarliestUs = evidence.ioLatestUs = 100;
            for (auto& w : evidence.inputWiring) w = InputWiring::UNCONNECTED;
            for (auto& w : evidence.outputWiring) w = InputWiring::UNCONNECTED;
            evidence.ioEffectsQualifiedFields = request.fields; evidence.qualifiedIo = request;
        }
        Ess::DriverContext c; assert(Ess::prepareDriverSettings(c, target, 101, request, evidence, 200, 10000)); return c;
    };
    auto emit = [io](const char* name, const Ess::DriverContext& c) {
        Fake fake; fake.nextOperation = c.operationId; auto host = fake.host(false, true); host.startDriver = Fake::startDriver;
        Probe::Console console(host); send(console, io ? "@77 profile ess_rs io read 1\n" : "@77 profile ess_rs driver read 1\n"); fake.lines.clear();
        assert(console.reportDriver(77, c.operationId, c)); assert(fake.lines.size() == 1);
        std::printf("{\"case\":\"%s\",\"record\":%s}\n", name, fake.lines[0].c_str());
    };
    emit("read", baseline); emit("unknown_read", read(true));
    auto c = prepare(); while (c.state == ReadState::ACTIVE) consume(c, 0, false); emit("full_update", c);
    for (int failure = 1; failure <= 9; ++failure) {
        c = prepare(); consume(c, 0, false); consume(c, 0, false);
        if (failure == 3) consume(c, 0, false);
        consume(c, failure, false);
        if (c.state == ReadState::ACTIVE) consume(c, 4, false);
        emit(failure == 1 ? "partial_lost_ack" : failure == 2 ? "cancelled" : failure == 3 ? "readback_mismatch" :
             failure == 4 ? "deadline" : failure == 5 ? "wrong_echo" : failure == 6 ? "late_delivery" :
             failure == 7 ? "unqualified" : failure == 8 ? "unconfirmed_echo" : "late_closure", c);
    }
    if (io) {
        c = prepare(); c.prerequisites.allowEchoReadback = true;
        while (c.state == ReadState::ACTIVE) { Ess::PreparedDriver w; assert(Ess::nextDriver(c, c.servicedUs, w)); consume(c, w.write ? 8 : 0, false); }
        emit("unconfirmed_settled", c);
        c = prepare(); auto candidate = c.request; auto prerequisites = c.prerequisites;
        prerequisites.maxAgeUs = 200; prerequisites.stationaryEarliestUs = prerequisites.stationaryLatestUs = 110;
        prerequisites.ioEarliestUs = prerequisites.ioLatestUs = 95;
        assert(Ess::prepareDriverSettings(c, target, 101, candidate, prerequisites, 200, 10000));
        Ess::PreparedDriver work; assert(Ess::nextDriver(c, 200, work) && work.deadlineUs == 295);
        ActionEvent expiry; expiry.transport.target = target; expiry.transport.operationId = 101;
        expiry.transport.step = 0; expiry.transport.kind = ReadEventKind::DEADLINE;
        assert(Ess::advanceDriver(c, expiry, 295)); emit("io_evidence_deadline", c);
    }
    // Actual core/formatter outcomes for an ambiguous settings response and
    // an ambiguous readback after a confirmed acknowledgement.
    assert(Ess::prepareDriverRead(c, target, 101, 3, 100, 10000, io ? Ess::DriverGroup::IO : Ess::DriverGroup::DRIVE));
    consume(c, 8, false); emit("unconfirmed_read", c);
    c = prepare(); consume(c, 0, false); consume(c, 8, false);
    emit("unconfirmed_readback", c);
}


void testIoRoutes() {
    Fake f; f.data.address = 1;
    auto h = f.host(false, true); h.startDriver = Fake::startDriver;
    Probe::Console c(h);
    send(c, "@1 profile ess_rs io read 2\n");
    assert(f.drivers == 1 && f.driverRequest.group == Ess::DriverGroup::IO && f.address == 2);
    f.contains("\"result\":\"accepted\"");
    send(c, "@2 profile ess_rs io set x0 none y1 none\n");
    assert(f.drivers == 2 && f.driverRequest.fields == ((1UL << 10) | (1UL << 16)));
    assert(f.driverRequest.inputFunctions[0] == Ess::InputFunction::UNDEFINED && f.driverRequest.outputFunctions[1] == Ess::OutputFunction::UNDEFINED);
    for (const char* line : {"profile ess_rs io set x4 none", "profile ess_rs io set y2 none", "profile ess_rs io set x0 18", "profile ess_rs io set y0 11", "profile ess_rs io set y0 6", "profile ess_rs io set x0 1.0", "profile ess_rs io set x0 1/1", "profile ess_rs io set x0 none x0 1", "profile ess_rs io set custom none", "io read"}) {
        send(c, std::string(line) + "\n"); f.contains("\"ok\":false"); assert(f.drivers == 2);
    }
    for (unsigned v = 0; v <= 17; ++v) {
        // Use fresh correlation storage for each admission.
        Fake isolated; isolated.data.address = 1; auto hook = isolated.host(false, true); hook.startDriver = Fake::startDriver;
        Probe::Console route(hook); send(route, "profile ess_rs io set x3 " + std::to_string(v) + "\n");
        assert(isolated.drivers == 1 && static_cast<unsigned>(isolated.driverRequest.inputFunctions[3]) == v);
    }
    send(c, "@3 help io\n"); f.contains("none assigns function 0");
}

void testHomeRoutesAndDescriptors() {
    Fake f; f.data.address = 1; f.axisConfig.generation = 3;
    auto h = f.host(false, true); h.startHome = Fake::startHome; h.axis = Fake::axis;
    Probe::Console c(h);
    send(c, "@1 home methods\n");
    f.contains("\"method\":35,\"support\":\"implemented\"");
    f.contains("\"method\":-1,\"support\":\"unresolved\"");
    assert(!f.homes && !f.probes && f.lines.back().size() < Probe::OUTPUT_CAPACITY);
    send(c, "@2 home 35 60 30 100 zero\n");
    assert(f.homes == 1 && f.homeRequest.method == Ess::HomingMethod::METHOD_35 && f.homeRequest.configurationGeneration == 3);
    send(c, "@3 profile ess_rs home 33 5 300 2000 zero 2\n");
    assert(f.homes == 2 && f.address == 2 && f.homeRequest.searchSpeed == 5 && f.homeRequest.returnSpeed == 300 && f.homeRequest.rampTime == 2000);
    for (const char* line : {"home 35 4 30 100 zero", "home 35 60 301 100 zero", "home 35 60 30 29 zero", "home 35 60 30 100 0", "home 35 60 30 100 zero 0", "home 3.5 60 30 100 zero", "home 35 60 30 100 zero extra extra"}) {
        send(c, std::string(line) + "\n"); f.contains("\"ok\":false"); assert(f.homes == 2);
    }
    f.blocked = true; send(c, "@100 status\n"); send(c, "@101 home 35 60 30 100 zero\n");
    assert(f.homes == 2 && c.inputDropped());
}

void homeFixtures() {
    using namespace MotorControlRS;
    AxisConfig axis; axis.target.id = axis.target.address = 1; axis.target.generation = 9; axis.generation = 3;
    auto prepare = [&](int method, bool old) {
        Ess::HomeRequest r; r.method = static_cast<Ess::HomingMethod>(method); r.configurationGeneration = 3;
        Ess::HomePrerequisites p; p.target = axis.target; p.configurationGeneration = 3;
        p.qualifiedMethod = static_cast<Ess::HomingMethod>(method);
        p.qualifiedSearchSpeed = 60; p.qualifiedReturnSpeed = 30; p.qualifiedRampTime = 100;
        p.methodQualified = p.nativeRatesQualified = p.nativeRampQualified = p.zeroOffsetQualified = p.auxiliaryQualified = true;
        p.inputsQualified = p.indexQualified = p.readinessQualified = p.referenceSemanticsQualified = true;
        p.rawMotion = old ? 3 : 1; p.observedUs = 100; p.maximumAgeUs = 20000;
        Ess::HomeContext c; ActionOptions options; options.pollIntervalUs = 10; options.maxPolls = 3;
        assert(Ess::prepareHome(c, axis, 101, r, p, 100, 10000, options)); return c;
    };
    auto consume = [](Ess::HomeContext& c, uint16_t motion, int failure) {
        Ess::PreparedHome w; assert(Ess::nextHome(c, c.eligibleUs, w));
        uint8_t raw[9] = {}; std::size_t length = 0;
        if (w.function == 16) { std::memcpy(raw, w.bytes, 6); length = 6; }
        else if (w.function == 6) { std::memcpy(raw, w.bytes, 6); length = 6; }
        else { raw[0] = 1; raw[1] = 3; raw[2] = 4; raw[5] = static_cast<uint8_t>(motion >> 8); raw[6] = static_cast<uint8_t>(motion); length = 7; }
        if (failure == 5 || failure == 9) { raw[1] = failure == 5 ? static_cast<uint8_t>(w.function | 0x80) : 0x90; raw[2] = 3; length = 3; }
        const uint16_t crc = Ess::calcCrc16(raw, length); raw[length++] = static_cast<uint8_t>(crc); raw[length++] = static_cast<uint8_t>(crc >> 8);
        ActionEvent e; e.transport.target = c.target; e.transport.operationId = c.operationId; e.transport.step = c.step;
        uint64_t now = c.eligibleUs + 20;
        if (failure == 1 || failure == 2 || failure == 3) {
            e.transport.kind = failure == 1 ? ReadEventKind::TRANSPORT_FAILURE : failure == 2 ? ReadEventKind::CANCEL : ReadEventKind::DEADLINE;
            e.transport.txAccepted = failure == 2 ? 0 : w.length; e.txComplete = failure != 2;
            if (failure == 3) now = w.deadlineUs;
        } else {
            e.transport.frame = raw; e.transport.length = length; e.transport.txAccepted = w.length;
            e.txComplete = e.responseConfirmed = e.transport.qualified = true;
            e.transport.earliestUs = c.eligibleUs + 1; e.transport.latestUs = c.eligibleUs + 10;
            if (failure == 4) e.responseConfirmed = false;
            if (failure == 6) { now = w.deadlineUs + 20; e.transport.earliestUs = w.deadlineUs + 1; e.transport.latestUs = w.deadlineUs + 10; }
            if (failure == 7) { e.transport.qualified = false; e.transport.earliestUs = e.transport.latestUs = 0; }
            if (failure == 8) now = w.deadlineUs + 20; // On-time intermediate evidence, no next-step budget.
        }
        assert(Ess::advanceHome(c, e, now));
    };
    auto emit = [](const char* name, const Ess::HomeContext& context) {
        Fake f; f.nextOperation = context.operationId; f.data.address = 1; f.axisConfig.generation = 3;
        auto h = f.host(false, true); h.axis = Fake::axis; h.startHome = Fake::startHome;
        Probe::Console console(h); send(console, "@77 home 35 60 30 100 zero\n"); f.lines.clear();
        assert(console.reportHome(77, context.operationId, context)); assert(f.lines.size() == 1);
        std::printf("{\"case\":\"%s\",\"record\":%s}\n", name, f.lines[0].c_str());
    };
    auto c = prepare(35, false); consume(c, 0, 0); consume(c, 0, 0); consume(c, 3, 0); consume(c, 0, 0); emit("current_origin", c);
    c = prepare(33, true); consume(c, 0, 0); consume(c, 0, 0); consume(c, 4, 0); consume(c, 3, 0); consume(c, 0, 0); emit("index_origin", c);
    c = prepare(35, true); consume(c, 0, 0); consume(c, 0, 0); consume(c, 3, 0); consume(c, 3, 0); consume(c, 3, 0); emit("stale_homed", c);
    for (int failure = 1; failure <= 6; ++failure) {
        c = prepare(35, false); consume(c, 0, 0); consume(c, 0, failure);
        emit(failure == 1 ? "lost_trigger_ack" : failure == 2 ? "cancelled" : failure == 3 ? "deadline" : failure == 4 ? "unconfirmed_trigger" : failure == 5 ? "exception" : "late_trigger_echo", c);
    }
    c = prepare(35, false); consume(c, 0, 7); emit("timing_unqualified", c);
    c = prepare(35, false); consume(c, 0, 0); consume(c, 0, 0); consume(c, 8, 0); emit("drive_fault", c);
    c = prepare(35, false); consume(c, 0, 0); consume(c, 0, 0); consume(c, 4, 0); emit("unexpected_activity", c);
    c = prepare(35, false); consume(c, 0, 0); consume(c, 0, 0); consume(c, 3, 0); consume(c, 1, 0); emit("nonzero_position", c);
    c = prepare(35, false); consume(c, 0, 0); consume(c, 0, 0); consume(c, 3, 8); emit("delayed_completion_deadline", c);
    c = prepare(35, false); consume(c, 0, 0); consume(c, 0, 9); emit("wrong_function_exception", c);
}

void testMaximumHomeOutputAndRetention() {
    Fake f; f.data.address = 1; f.nextOperation = UINT32_MAX;
    auto h = f.host(false, true); h.startHome = Fake::startHome; h.axis = Fake::axis;
    Probe::Console console(h); send(console, "@77 home 35 60 30 100 zero\n");
    Ess::HomeContext c; c.operationId = UINT32_MAX;
    c.state = MotorControlRS::ActionState::FAILED;
    c.outcome = MotorControlRS::ActionOutcome::TRANSPORT_ERROR;
    c.target.id = c.target.generation = c.request.configurationGeneration = UINT32_MAX; c.target.address = 247;
    c.startedUs = c.deadlineUs = c.servicedUs = c.completionObservedUs = UINT64_MAX;
    c.prerequisites.observedUs = c.prerequisites.maximumAgeUs = UINT64_MAX;
    c.prerequisites.qualifiedMethod = Ess::HomingMethod::METHOD_35;
    c.prerequisites.qualifiedSearchSpeed = 60; c.prerequisites.qualifiedReturnSpeed = 30; c.prerequisites.qualifiedRampTime = 100;
    c.words[0] = 35; c.words[1] = 60; c.words[2] = 30; c.words[3] = 100;
    c.rawAlarm = c.rawMotion = c.rawPositionWords[0] = c.rawPositionWords[1] = UINT16_MAX;
    Ess::ActionEvidence* evidence[] = {&c.stagingEvidence,&c.triggerEvidence,&c.activityEvidence,&c.lowEvidence,&c.lastObservation,&c.completionEvidence,&c.zeroEvidence,&c.failureEvidence};
    for (auto* e : evidence) {
        e->step = UINT8_MAX; e->length = 9; e->receivedLength = e->txAccepted = UINT32_MAX;
        std::memset(e->raw, 255, 9); e->earliestUs = e->latestUs = e->deliveredUs = UINT64_MAX;
        e->transportDetail = INT32_MIN; e->status = {MotorControlRS::Err::INVALID_CONFIG,INT32_MIN,"invalid"};
    }
    f.blocked = true; assert(console.reportHome(77, c.operationId, c));
    assert(console.outputPending() && !console.reportHome(77,c.operationId,c));
    f.blocked = false; assert(console.serviceOutput());
    f.contains("\"low_evidence\":["); assert(f.lines.back().size() < Probe::OUTPUT_CAPACITY);
    f.view.commandId = 77; f.view.operationId = c.operationId; f.view.homeContext = &c;
    send(console, "@78 result 4294967295\n"); f.contains("\"home\":true");
    f.view.pending = true; send(console, "@79 result 4294967295\n");
    f.contains("\"result\":\"pending\""); f.contains("\"home\":true");
}

// Complete valid candidates keep their original snapshot. Only its oldest
// observation bounds these terminal cases, before identity/stopped evidence.
void expirePreviousDriverSettings(Ess::DriverContext& c, bool lateEcho) {
    using namespace MotorControlRS;
    Ess::PreparedDriver work;
    assert(Ess::nextDriver(c, c.servicedUs, work));
    assert(work.write && work.deadlineUs == 250 && c.deadlineUs == 10000);
    ActionEvent event;
    event.transport.target = c.target; event.transport.operationId = c.operationId;
    event.transport.step = c.step; event.transport.kind = ReadEventKind::DEADLINE;
    if (lateEcho) {
        event.transport.kind = ReadEventKind::FRAME;
        event.transport.frame = work.bytes; event.transport.length = work.length;
        event.transport.txAccepted = work.length;
        event.transport.qualified = event.txComplete = event.responseConfirmed = true;
        event.transport.earliestUs = 250; event.transport.latestUs = 251;
    }
    assert(Ess::advanceDriver(c, event, lateEcho ? 252 : 250));
    assert(c.state == ReadState::FAILED && c.outcome == Ess::DriverOutcome::DEADLINE && !c.completedSteps);
    assert(c.uncertain == lateEcho);
    uint32_t expectedEffects = 0;
    for (const auto& progress : c.progress) {
        if (!progress.selected) continue;
        assert(!progress.acknowledged && !progress.readbackKnown);
        if (progress.reg == work.reg && lateEcho) {
            assert(progress.execution == ActionExecution::UNKNOWN);
            expectedEffects = static_cast<uint32_t>(progress.field);
        } else assert(progress.execution == ActionExecution::NOT_TRANSMITTED);
    }
    assert(c.effects == expectedEffects);
    assert(c.prerequisites.previous.provenance[0].attemptedUs == 50);
}

void tuningFixtures() {
    using namespace MotorControlRS;
    ReadTarget target;target.id=target.address=1;target.generation=9;
    const Ess::DriverGroup groups[]={Ess::DriverGroup::FILTERS,Ess::DriverGroup::CURRENT_LOOP,Ess::DriverGroup::LA,Ess::DriverGroup::COLLISION};
    const char* names[]={"filters","current-loop","la","collision"};
    const uint16_t defaults[4][8]={{2,5,4000,5,10,512},{4096,1024,28,1228},{10,32,320,15,33,320,20,35},{200,50}};
    const uint16_t updates[4][8]={{3,6,4001,6,11,511},{4097,1025,29,1229},{11,33,321,16,34,321,21,36},{201,51}};
    auto consume = [&](Ess::DriverContext& c,unsigned g,int fault) {
        Ess::PreparedDriver w;assert(Ess::nextDriver(c,c.servicedUs,w));
        uint8_t raw[15]={};const size_t length=w.write?8:5+2*w.count;
        if(w.write)std::memcpy(raw,w.bytes,8);
        else {
            raw[0]=1;raw[1]=3;raw[2]=2*w.count;
            Ess::TuningParameterInfo info;assert(Ess::tuningParameterInfo(Ess::tuningParameter(c.group,0),info));
            for(unsigned i=0;i<w.count;++i) {
                const unsigned slot=w.reg-info.reg+i;
                uint16_t value=c.kind==Ess::DriverKind::UPDATE?w.value:defaults[g][slot];
                if(fault==2)++value;
                if(fault==3) {if(g==0&&slot==1)value=65535;if(g==3&&slot==0)value=0;}
                raw[3+2*i]=value>>8;raw[4+2*i]=value;
            }
            sealTypedReply(raw,length);
        }
        ActionEvent e;e.transport.target=c.target;e.transport.operationId=c.operationId;e.transport.step=c.step;
        e.transport.kind=ReadEventKind::FRAME;e.transport.frame=raw;e.transport.length=length;e.transport.txAccepted=8;
        e.transport.qualified=true;e.transport.earliestUs=c.servicedUs+1;e.transport.latestUs=c.servicedUs+2;
        e.txComplete=true;e.responseConfirmed=!w.write||!c.prerequisites.allowEchoReadback;
        if(fault==1) {e.transport.kind=ReadEventKind::CANCEL;e.transport.frame=nullptr;e.transport.length=0;
            e.transport.qualified=false;e.transport.earliestUs=e.transport.latestUs=0;e.responseConfirmed=false;}
        assert(Ess::advanceDriver(c,e,c.servicedUs+3));
    };
    auto emit = [&](const char* name,unsigned g,const Ess::DriverContext& c) {
        Fake f;f.nextOperation=c.operationId;auto h=f.host(false,true);h.startDriver=Fake::startDriver;Probe::Console console(h);
        send(console,std::string("@77 profile ess_rs tuning ")+names[g]+" read\n");f.lines.clear();
        assert(console.reportDriver(77,c.operationId,c));assert(f.lines.size()==1);
        std::printf("{\"case\":\"%s\",\"record\":%s}\n",name,f.lines[0].c_str());
    };
    for(unsigned g=0;g<4;++g) {
        Ess::DriverContext c;assert(Ess::prepareTuningRead(c,target,101,3,100,10000,groups[g]));
        while(c.state==ReadState::ACTIVE) consume(c,g,0);
        emit("read",g,c);
        Ess::DriverPrerequisites p;assert(Ess::getDriver(c,p.previous));
        if(g==0||g==3) {
            assert(Ess::prepareTuningRead(c,target,101,3,100,10000,groups[g]));
            while(c.state==ReadState::ACTIVE) consume(c,g,3);
            emit("unknown_raw",g,c);
        }
        Ess::ReadContext identity;assert(Ess::prepareIdentity(identity,target,9,80,10000));
        uint8_t raw[13]={1,3,8,0x4e,0xea,0,0x29,0,1,0,0};sealTypedReply(raw,sizeof(raw));
        ReadEvent event;event.target=target;event.operationId=9;event.txAccepted=8;event.frame=raw;event.length=sizeof(raw);
        event.qualified=true;event.earliestUs=90;event.latestUs=91;
        assert(Ess::advanceRead(identity,event,92));assert(Ess::getIdentity(identity,p.controlIdentity));
        p.configurationGeneration=3;p.stationaryQualified=true;p.stationaryTarget=target;p.rawMotion=1;
        p.stationaryEarliestUs=110;p.stationaryLatestUs=120;p.maxAgeUs=20000;
        p.exactModelQualified=true;p.modelSourceId=2;p.qualifiedModelCode=0x4eea;p.qualifiedFirmwareCode=0x29;
        p.tuningEarliestUs=110;p.tuningLatestUs=120;
        Ess::DriverRequest request;request.configurationGeneration=3;
        const uint8_t count=Ess::tuningFieldCount(groups[g]);
        for(uint8_t i=0;i<count;++i)assert(Ess::prepareTuningValue(request,Ess::tuningParameter(groups[g],i),updates[g][i]));
        p.qualifiedTuning=request;p.tuningEffectsQualifiedFields=request.fields;
        assert(Ess::prepareTuningSettings(c,target,101,request,p,200,10000));
        while(c.state==ReadState::ACTIVE) consume(c,g,0);
        emit("full_checked_update",g,c);
        p.allowEchoReadback=true;
        assert(Ess::prepareTuningSettings(c,target,101,request,p,200,10000));
        while(c.state==ReadState::ACTIVE) consume(c,g,0);
        emit("full_echo_readback_update",g,c);
        for(unsigned step=0;step<2u*count;++step) {
            assert(Ess::prepareTuningSettings(c,target,101,request,p,200,10000));for(unsigned i=0;i<step;++i)consume(c,g,0);
            consume(c,g,1);emit("cancelled_boundary",g,c);
        }
        assert(Ess::prepareTuningSettings(c,target,101,request,p,200,10000));consume(c,g,0);consume(c,g,2);emit("readback_disagreement",g,c);
        auto oldest = p;
        oldest.previous.provenance[0].attemptedUs = 50; oldest.maxAgeUs = 200;
        for (bool lateEcho : {false, true}) {
            assert(Ess::prepareTuningSettings(c,target,101,request,oldest,200,10000));
            expirePreviousDriverSettings(c, lateEcho);
            emit(lateEcho ? "previous_settings_late_echo" : "previous_settings_deadline",g,c);
        }
    }
}
void testTuningRoutes() {
    Fake absent;auto ah=absent.host(false,true);ah.startDriver=nullptr;Probe::Console unavailable(ah);
    send(unavailable,"@80 help\n");assert(absent.lines.back().find("\"tuning\"")==std::string::npos);
    send(unavailable,"@81 help tuning\n");absent.contains("\"result\":\"unavailable\"");
    send(unavailable,"@82 caps\n");absent.contains("\"tuning\":false");
    send(unavailable,"@83 profile ess_rs tuning filters read\n");absent.contains("\"result\":\"unavailable\"");assert(!absent.drivers);
    Fake f;auto h=f.host(false,true);h.startDriver=Fake::startDriver;Probe::Console c(h);
    send(c,"@84 profile ess_rs tuning filters read 2\n");assert(f.drivers==1&&f.address==2&&f.driverRequest.group==Ess::DriverGroup::FILTERS);f.contains("\"result\":\"accepted\"");
    send(c,"@85 profile ess_rs tuning filters set arrival-time 0 pulse-mean 512\n");
    assert(f.drivers==2&&f.driverRequest.tuningValues[4]==0&&f.driverRequest.tuningValues[5]==512&&f.driverRequest.fields==48);
    send(c,"@86 profile ess_rs tuning current-loop set multiplier 65535 kp 65535 ki 65535 kc 0\n");
    assert(f.drivers==3&&f.driverRequest.group==Ess::DriverGroup::CURRENT_LOOP&&f.driverRequest.fields==15&&f.driverRequest.tuningValues[3]==0);
    send(c,"@87 profile ess_rs tuning la set node1 0 node2 65535 kvf 65535 position-ki 65535\n");
    assert(f.drivers==4&&f.driverRequest.group==Ess::DriverGroup::LA&&f.driverRequest.fields==228&&f.driverRequest.tuningValues[7]==65535);
    send(c,"@88 profile ess_rs tuning collision set threshold 200 current 20\n");
    assert(f.drivers==5&&f.driverRequest.group==Ess::DriverGroup::COLLISION&&f.driverRequest.fields==3&&f.driverRequest.tuningValues[1]==20);
    for(const char* bad:{"tuning filters read","profile ess_rs tuning bogus read","profile ess_rs tuning filters set kp 1","profile ess_rs tuning filters set arrival-time 1.0","profile ess_rs tuning filters set arrival-time 1/1","profile ess_rs tuning filters set arrival-time -1","profile ess_rs tuning filters set arrival-time 201","profile ess_rs tuning filters set arrival-window 0","profile ess_rs tuning filters set pulse-low-pass 1025","profile ess_rs tuning la set node1 65536","profile ess_rs tuning collision set threshold 50","profile ess_rs tuning collision set current 19","profile ess_rs tuning collision set 0x003B 200","profile ess_rs tuning filters set input-filter 1 input-filter 2"})send(c,std::string(bad)+"\n");
    assert(f.drivers==5);
    send(c,"@89 help tuning\n");f.contains("qualified_stopped_native_tuning_and_checked_readback");
    send(c,"@90 caps\n");f.contains("\"tuning\":true");f.contains("\"tuning_physical_scaling_known\":false");
    const Ess::DriverGroup groups[]={Ess::DriverGroup::FILTERS,Ess::DriverGroup::CURRENT_LOOP,Ess::DriverGroup::LA,Ess::DriverGroup::COLLISION};
    const char* names[]={"filters","current-loop","la","collision"};
    const char* fields[4][8]={{"input-filter","pulse-low-pass","deviation-threshold","arrival-window","arrival-time","pulse-mean"},{"multiplier","kp","ki","kc"},{"kp1","kv1","node1","kp2","kv2","node2","kvf","position-ki"},{"threshold","current"}};
    for(unsigned g=0;g<4;++g)for(uint8_t slot=0;slot<Ess::tuningFieldCount(groups[g]);++slot) {
        Ess::TuningParameterInfo info;assert(Ess::tuningParameterInfo(Ess::tuningParameter(groups[g],slot),info));
        Fake one;auto hook=one.host(false,true);hook.startDriver=Fake::startDriver;Probe::Console console(hook);
        send(console,std::string("@1 profile ess_rs tuning ")+names[g]+" set "+fields[g][slot]+" "+std::to_string(info.maximum)+"\n");
        assert(one.drivers==1&&one.driverRequest.group==groups[g]&&one.driverRequest.fields==(1u<<slot)&&one.driverRequest.tuningValues[slot]==info.maximum);
    }
}
void controlFixtures() {
    using namespace MotorControlRS;
    ReadTarget target; target.id=target.address=1; target.generation=9;
    auto consume = [](Ess::DriverContext& c, int fault) {
        Ess::PreparedDriver w; assert(Ess::nextDriver(c,c.servicedUs,w));
        uint8_t raw[15]={}; const size_t length=w.write?8:5+2*w.count;
        if(w.write) std::memcpy(raw,w.bytes,8);
        else {
            raw[0]=1;raw[1]=3;raw[2]=2*w.count;
            const uint16_t original[8]={2,4000,1000,100,40,100,50,4000};
            for(unsigned i=0;i<w.count;++i) {
                uint16_t value=c.kind==Ess::DriverKind::UPDATE?w.value:original[w.reg-0x100+i];
                if(fault==2) ++value;
                if(fault==3&&w.reg==0x100) { if(i==0)value=3;if(i==1)value=0; }
                raw[3+2*i]=value>>8;raw[4+2*i]=value;
            }
            sealTypedReply(raw,length);
        }
        ActionEvent e;e.transport.target=c.target;e.transport.operationId=c.operationId;e.transport.step=c.step;
        e.transport.kind=ReadEventKind::FRAME;e.transport.frame=raw;e.transport.length=length;e.transport.txAccepted=8;
        e.transport.qualified=true;e.transport.earliestUs=c.servicedUs+1;e.transport.latestUs=c.servicedUs+2;
        e.txComplete=true;e.responseConfirmed=!w.write||!c.prerequisites.allowEchoReadback;
        if(fault==1) { e.transport.kind=ReadEventKind::CANCEL;e.transport.frame=nullptr;e.transport.length=0;
            e.transport.qualified=false;e.transport.earliestUs=e.transport.latestUs=0;e.responseConfirmed=false; }
        assert(Ess::advanceDriver(c,e,c.servicedUs+3));
    };
    auto emit = [](const char* name,const Ess::DriverContext& c) {
        Fake f;f.nextOperation=c.operationId;auto h=f.host(false,true);h.startDriver=Fake::startDriver;Probe::Console console(h);
        send(console,"@77 profile ess_rs control read\n");f.lines.clear();
        assert(console.reportDriver(77,c.operationId,c));assert(f.lines.size()==1);
        std::printf("{\"case\":\"%s\",\"record\":%s}\n",name,f.lines[0].c_str());
    };
    Ess::DriverContext c;assert(Ess::prepareDriverRead(c,target,101,3,100,10000,Ess::DriverGroup::CONTROL_SETTINGS));
    consume(c,0);consume(c,0);emit("read",c);
    Ess::DriverPrerequisites p;assert(Ess::getDriver(c,p.previous));
    assert(Ess::prepareDriverRead(c,target,101,3,100,10000,Ess::DriverGroup::CONTROL_SETTINGS));consume(c,3);consume(c,0);emit("unknown_algorithm_zero_encoder",c);
    Ess::ReadContext identity;assert(Ess::prepareIdentity(identity,target,9,80,10000));
    uint8_t raw[13]={1,3,8,0x4e,0xea,0,0x29,0,1,0,0};sealTypedReply(raw,sizeof(raw));
    ReadEvent event;event.target=target;event.operationId=9;event.txAccepted=8;event.frame=raw;event.length=sizeof(raw);
    event.qualified=true;event.earliestUs=90;event.latestUs=91;
    assert(Ess::advanceRead(identity,event,92));assert(Ess::getIdentity(identity,p.controlIdentity));
    p.configurationGeneration=3;p.stationaryQualified=true;p.stationaryTarget=target;p.rawMotion=1;
    p.stationaryEarliestUs=110;p.stationaryLatestUs=120;p.maxAgeUs=20000;
    p.exactModelQualified=true;p.modelSourceId=2;p.qualifiedModelCode=0x4eea;p.qualifiedFirmwareCode=0x29;
    p.nativeCurrentLimitQualified=true;p.maximumEffectiveLimitMa=2000;p.currentPercentBaseQualified=true;
    p.controlEarliestUs=110;p.controlLatestUs=120;
    Ess::DriverRequest r;r.group=Ess::DriverGroup::CONTROL_SETTINGS;r.configurationGeneration=3;r.fields=0x7f800000;
    r.controlAlgorithm=Ess::ControlAlgorithm::ALGORITHM_1;r.encoderResolution=8000;r.maximumEffectiveCurrentMa=1000;
    r.closedMaximumPercent=100;r.closedBasePercent=40;r.openMaximumPercent=100;r.lockPercent=50;r.lockDelayMs=3999;
    p.qualifiedControl=r;p.controlEffectsQualifiedFields=r.fields;
    assert(Ess::prepareDriverSettings(c,target,101,r,p,200,10000));while(c.state==ReadState::ACTIVE)consume(c,0);emit("full_checked_update",c);
    p.allowEchoReadback=true;
    assert(Ess::prepareDriverSettings(c,target,101,r,p,200,10000));while(c.state==ReadState::ACTIVE)consume(c,0);emit("full_echo_readback_update",c);
    for(unsigned step=0;step<16;++step) {
        assert(Ess::prepareDriverSettings(c,target,101,r,p,200,10000));for(unsigned i=0;i<step;++i)consume(c,0);
        consume(c,1);emit("cancelled_boundary",c);
    }
    assert(Ess::prepareDriverSettings(c,target,101,r,p,200,10000));consume(c,0);consume(c,2);emit("readback_disagreement",c);
    auto oldest = p;
    oldest.previous.provenance[0].attemptedUs = 50; oldest.maxAgeUs = 200;
    for (bool lateEcho : {false, true}) {
        assert(Ess::prepareDriverSettings(c,target,101,r,oldest,200,10000));
        expirePreviousDriverSettings(c, lateEcho);
        emit(lateEcho ? "previous_settings_late_echo" : "previous_settings_deadline",c);
    }
}
void testControlRoutes() {
    {
        Fake absent; auto absentHost=absent.host(false,true); absentHost.startDriver=nullptr;
        Probe::Console unavailable(absentHost);
        send(unavailable,"@80 help\n");
        assert(absent.lines.size()==1 && absent.lines.back().find("\"control\"")==std::string::npos);
        send(unavailable,"@81 help control\n"); absent.contains("\"result\":\"unavailable\"");
        send(unavailable,"@82 caps\n"); absent.contains("\"control_settings\":false");
        send(unavailable,"@83 profile ess_rs control read\n"); absent.contains("\"result\":\"unavailable\"");
        assert(!absent.drivers);
    }
    Fake f;auto h=f.host(false,true);h.startDriver=Fake::startDriver;Probe::Console c(h);
    send(c,"@84 profile ess_rs control read 2\n");assert(f.drivers==1&&f.address==2&&f.driverRequest.group==Ess::DriverGroup::CONTROL_SETTINGS);
    f.contains("\"result\":\"accepted\"");
    send(c,"@85 profile ess_rs control set algorithm open-loop lock-delay 20000\n");
    assert(f.drivers==2&&f.driverRequest.controlAlgorithm==Ess::ControlAlgorithm::OPEN_LOOP&&f.driverRequest.lockDelayMs==20000&&f.driverRequest.fields==((1u<<23)|(1u<<30)));
    send(c,"@86 profile ess_rs control set algorithm algorithm-1 encoder-resolution 65535\n");
    assert(f.drivers==3&&f.driverRequest.controlAlgorithm==Ess::ControlAlgorithm::ALGORITHM_1&&f.driverRequest.encoderResolution==65535);
    send(c,"@87 profile ess_rs control set maximum-effective-current 5600 closed-base-current 75\n");
    assert(f.drivers==4&&f.driverRequest.maximumEffectiveCurrentMa==5600&&f.driverRequest.closedBasePercent==75);
    for(const char* bad:{"control read","profile ess_rs control read set","profile ess_rs control set x0 none","profile ess_rs control set lock-delay 1/1","profile ess_rs control set lock-delay 1.0","profile ess_rs control set lock-delay -1","profile ess_rs control set lock-delay 65536","profile ess_rs control set lock-delay 1 lock-delay 2","profile ess_rs control set algorithm closed-loop"}) send(c,std::string(bad)+"\n");
    assert(f.drivers==4);
    send(c,"@88 help control\n");f.contains("stopped_native_control_settings_and_checked_readback");
    send(c,"@89 caps\n");f.contains("\"control_settings\":true");f.contains("\"effective_current_limit_from_peak\":false");
}
void segmentFixtures() {
    using namespace MotorControlRS;
    ReadTarget target; target.id = target.address = 1; target.generation = 9;
    auto consume = [](Ess::DriverContext& c, int failure) {
        Ess::PreparedDriver w; assert(Ess::nextDriver(c, c.servicedUs, w));
        uint8_t raw[15] = {}; size_t length = w.write ? 8 : 5 + 2 * w.count;
        if (w.write) std::memcpy(raw, w.bytes, 8);
        else {
            raw[0]=1; raw[1]=3; raw[2]=2*w.count;
            uint16_t words[5] = {120,100,100,0,0};
            if (c.group == Ess::DriverGroup::POSITION_SEGMENT && c.kind == Ess::DriverKind::READ) { words[0]=0x1234; words[1]=0xABCD; words[2]=120; words[3]=100; words[4]=100; }
            if (c.group == Ess::DriverGroup::SEGMENT_START_SPEED) words[0]=50;
            if (c.kind == Ess::DriverKind::UPDATE) words[0]=w.value;
            for (unsigned i=0;i<w.count;++i) { raw[3+2*i]=words[i]>>8; raw[4+2*i]=words[i]; }
            sealTypedReply(raw,length);
        }
        ActionEvent e; e.transport.target=c.target; e.transport.operationId=c.operationId; e.transport.step=c.step;
        e.transport.kind=ReadEventKind::FRAME; e.transport.frame=raw; e.transport.length=length;
        e.transport.txAccepted=8; e.transport.qualified=true; e.transport.earliestUs=c.servicedUs+1; e.transport.latestUs=c.servicedUs+2;
        e.txComplete=true; e.responseConfirmed=!w.write;
        if (failure) { e.transport.kind=ReadEventKind::CANCEL; e.transport.frame=nullptr; e.transport.length=0; e.transport.qualified=false; e.transport.earliestUs=e.transport.latestUs=0; e.responseConfirmed=false; }
        assert(Ess::advanceDriver(c,e,c.servicedUs+3));
    };
    auto emit = [](const char* name, const Ess::DriverContext& c) {
        Fake f; f.nextOperation=c.operationId; auto h=f.host(false,true); h.startDriver=Fake::startDriver; Probe::Console console(h);
        send(console,"@77 profile ess_rs segment position 1 read\n"); f.lines.clear();
        assert(console.reportDriver(77,c.operationId,c));
        assert(f.lines.size()==1); std::printf("{\"case\":\"%s\",\"record\":%s}\n", name, f.lines[0].c_str());
    };
    for (auto group : {Ess::DriverGroup::POSITION_SEGMENT,Ess::DriverGroup::SPEED_SEGMENT,Ess::DriverGroup::SEGMENT_START_SPEED}) {
        for (uint8_t index : {uint8_t(1),uint8_t(16)}) {
            Ess::DriverContext c; assert(Ess::prepareDriverRead(c,target,101,3,100,10000,group,index)); consume(c,0); emit("read",c);
            Ess::DriverPrerequisites p; assert(Ess::getDriver(c,p.previous));
            Ess::DriverRequest r; r.group=group; r.segmentIndex=index; r.configurationGeneration=3;
            r.fields=group==Ess::DriverGroup::SEGMENT_START_SPEED ? 1<<21 : (1<<18)|(1<<19)|(1<<20);
            r.segmentSpeed=60; r.segmentAcceleration=90; r.segmentDeceleration=80; r.segmentStartSpeed=60;
            p.configurationGeneration=3; p.stationaryQualified=true; p.stationaryTarget=target; p.maxAgeUs=20000;
            p.stationaryEarliestUs=p.stationaryLatestUs=100; p.externalTriggerInhibitedQualified=true; p.triggerTarget=target;
            p.triggerConfigurationGeneration=3; p.triggerEarliestUs=p.triggerLatestUs=100; p.qualifiedSegment=r; p.allowEchoReadback=true;
            assert(Ess::prepareDriverSettings(c,target,101,r,p,200,10000));
            while(c.state==ReadState::ACTIVE) consume(c,0);
            emit("stored_update",c);
            assert(Ess::prepareDriverSettings(c,target,101,r,p,200,10000)); consume(c,0); consume(c,1); emit("cancelled_readback",c);
        }
    }
}
void testSegmentGrammarAndCorrelation() {
    Fake f; auto hook = f.host(false,true); hook.startDriver = Fake::startDriver;
    Probe::Console c(hook);
    send(c, "@80 profile ess_rs segment position 16 read 2\n");
    assert(f.drivers == 1 && f.address == 2 && f.driverRequest.segmentIndex == 16 && f.driverRequest.group == Ess::DriverGroup::POSITION_SEGMENT);
    f.contains("\"result\":\"accepted\"");
    send(c, "@81 profile ess_rs segment speed 1 set speed -1 acceleration 2000\n");
    assert(f.drivers == 2 && f.driverRequest.segmentSpeed == -1 && f.driverRequest.segmentAcceleration == 2000);
    send(c, "@82 profile ess_rs segment start 16 set value 180\n");
    assert(f.drivers == 3 && f.driverRequest.segmentStartSpeed == 180);
    for (const char* bad : {"segment position 1 read", "profile ess_rs segment position 0 read", "profile ess_rs segment position 17 read", "profile ess_rs segment speed 1 set target 0", "profile ess_rs segment start 1 set value 1/1", "profile ess_rs segment position 1 set speed 1 speed 2", "profile ess_rs segment position 1 set acceleration -1", "profile ess_rs segment start 1 set value 2147483648"}) send(c, std::string(bad)+"\n");
    assert(f.drivers == 3);
}
int main(int argc, char** argv) {
    if (argc==2 && !std::strcmp(argv[1],"--debug-fixtures")) { debugFixtures(); return 0; }
    if (argc == 2 && !std::strcmp(argv[1], "--tuning-fixtures")) { tuningFixtures(); return 0; }
    if (argc == 2 && !std::strcmp(argv[1], "--control-fixtures")) { controlFixtures(); return 0; }
    if (argc == 2 && !std::strcmp(argv[1], "--segment-fixtures")) { segmentFixtures(); return 0; }
    if (argc == 2 && std::strcmp(argv[1], "--velocity-fixtures") == 0) {
        velocityFixtures(); return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--driver-fixtures") == 0) {
        driverFixtures(); return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--home-fixtures") == 0) { homeFixtures(); return 0; }
    if (argc == 2 && std::strcmp(argv[1], "--io-fixtures") == 0) { driverFixtures(true); return 0; }
    testDebugTranslationAndOutputIsolation();
    testSegmentGrammarAndCorrelation();
    testControlRoutes();
    testTuningRoutes();
    testIoRoutes();
    testHomeRoutesAndDescriptors();
    testMaximumHomeOutputAndRetention();
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
    testActionRoutesAndStopPressure();
    testMaximumActionAndInvalidatedCacheFormatting();
    testMoveRoutesExactParsingAndRetainedReports();
    testMaximumMoveReportFitsFixedOutput();
    testAbsoluteAngleAndClearRoutesUsePublicRequests();
    testVelocityExactRoutesRetentionAndBound();
    testDriverProfileGrammarAndRetainedReports();
    testMaximumDriverReportFitsFixedOutput();
}
