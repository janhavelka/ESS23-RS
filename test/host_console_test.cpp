// SPDX-License-Identifier: MIT
#include "../examples/probe_cli/ProbeConsole.h"
#include <cassert>
#include <cstring>
#include <string>
#include <vector>

namespace Example = MotorControlRSExample;
namespace Probe = Example::Probe;
namespace Rtu = Example::Rtu;

namespace {
struct Fake {
    Probe::HostSnapshot serial;
    Probe::ResultView retained;
    unsigned changes = 0, probes = 0;
    bool setupFailure = false, restoreFailure = false, busy = false, recoveryRequired = false;
    std::vector<std::string> lines;

    Fake() {
        const uint32_t rates[] = {9600, 19200, 38400, 115200};
        for (unsigned i = 0; i < 4; ++i) { serial.supportedBauds[i] = rates[i]; serial.supportedFormats[i] = true; }
        Example::hostTiming(serial.active, serial.timing);
        serial.actualBaud = serial.active.baud;
    }
    static bool emit(void* context, const char* bytes, std::size_t length) {
        assert(length < Probe::OUTPUT_CAPACITY && bytes[0] == '{' && bytes[length - 1] == '}');
        static_cast<Fake*>(context)->lines.emplace_back(bytes, length); return true;
    }
    static void snapshot(void* context, Probe::Snapshot& out) {
        const auto& self = *static_cast<Fake*>(context);
        out.serial = self.serial; out.address = 1;
        out.baud = self.serial.activeKnown ? self.serial.active.baud : 0;
    }
    static Probe::Action hostSerial(void* context, const Probe::HostRequest* request, Probe::HostSnapshot& out) {
        auto& self = *static_cast<Fake*>(context);
        if (!request) { out = self.serial; return Probe::Action::OK; }
        if (self.busy) return Probe::Action::BUSY;
        if (self.recoveryRequired && !self.serial.blocked) return Probe::Action::RECOVERY_REQUIRED;
        const auto tuple = request->restore ? self.serial.original : request->tuple;
        if (self.serial.activeKnown && !self.serial.blocked && Example::sameTuple(tuple, self.serial.active)) {
            out = self.serial; return Probe::Action::OK;
        }
        ++self.changes; ++self.serial.generation; self.serial.requested = tuple;
        const bool failed = request->restore ? self.restoreFailure : self.setupFailure;
        self.serial.blocked = failed; self.serial.activeKnown = !failed;
        self.serial.actualBaud = failed ? 0 : tuple.baud;
        self.serial.failure = !failed ? Probe::HostFailure::NONE : request->restore ? Probe::HostFailure::RESTORE : Probe::HostFailure::ADAPTER;
        if (!failed) { self.serial.active = tuple; assert(Example::hostTiming(tuple, self.serial.timing)); }
        out = self.serial; return failed ? Probe::Action::FAILED : Probe::Action::OK;
    }
    static Probe::Action probe(void* context, uint32_t commandId, uint8_t address, uint32_t& operationId) {
        auto& self = *static_cast<Fake*>(context); ++self.probes;
        operationId = commandId + 100;
        self.retained.commandId = commandId; self.retained.operationId = operationId;
        self.retained.address = address; self.retained.pending = true;
        self.retained.serialTuple = self.serial.active; self.retained.serialGeneration = self.serial.generation;
        return Probe::Action::OK;
    }
    static bool result(void* context, uint32_t id, Probe::ResultView& out) {
        const auto& self = *static_cast<Fake*>(context);
        if (id != self.retained.operationId) return false;
        out = self.retained; return true;
    }
    static Probe::Action recover(void*, uint32_t, uint32_t&) { return Probe::Action::UNAVAILABLE; }
    static void reset(void*) {}
    Probe::Host hooks() {
        Probe::Host host; host.context = this; host.emitLine = emit; host.snapshot = snapshot;
        host.hostSerial = hostSerial; host.startProbe = probe; host.result = result;
        host.recover = recover; host.resetStats = reset;
        return host;
    }
    const std::string& last() const { assert(!lines.empty()); return lines.back(); }
};

void send(Probe::Console& console, const std::string& line) {
    for (const auto c : line) console.feed(c);
    console.feed('\n');
}
bool has(const Fake& fake, const char* fragment) { return fake.last().find(fragment) != std::string::npos; }
}

int main() {
    Fake fake; Probe::Console console(fake.hooks());
    send(console, "@1 help host"); assert(has(fake, "host [baud RATE"));
    send(console, "@2 host caps");
    assert(has(fake, "\"supported_bauds\":[9600,19200,38400,115200]"));
    assert(has(fake, "\"supported_formats\":[\"8N1\",\"8N2\",\"8E1\",\"8O1\"]"));
    assert(has(fake, "\"actual_baud\":115200"));
    assert(has(fake, "\"device_settings_changed\":false"));
    const char* invalid[] = {"host baud 57600", "host baud 0", "host baud 4294967296", "host fmt 7E1", "host set 9600 8E2", "host set 9600 8N1 extra", "host restore extra"};
    for (const auto* line : invalid) { send(console, line); assert(has(fake, "\"ok\":false")); }
    assert(fake.changes == 0 && fake.probes == 0);
    fake.serial.supportedFormats[2] = false;
    send(console, "host fmt 8E1"); assert(has(fake, "\"result\":\"unsupported\"")); assert(fake.changes == 0);
    fake.serial.supportedFormats[2] = true;
    const uint32_t rates[] = {9600, 19200, 38400, 115200};
    for (const auto baud : rates) for (unsigned format = 0; format < 4; ++format) {
        const auto fmt = static_cast<Example::HostFormat>(format);
        send(console, std::string("host set ") + std::to_string(baud) + " " + Example::formatName(fmt));
        assert(has(fake, "\"ok\":true"));
        assert(fake.serial.active.baud == baud && fake.serial.active.format == fmt);
        assert(fake.serial.timing.replyGapUs == (baud == 115200 && format == 0 ? 304 : fake.serial.timing.runner.gap35Us));
    }
    send(console, "host restore"); assert(Example::sameTuple(fake.serial.active, fake.serial.original));
    const auto generation = fake.serial.generation, changes = fake.changes;
    send(console, "host restore"); assert(fake.serial.generation == generation && fake.changes == changes);
    fake.busy = true; send(console, "host baud 9600"); assert(has(fake, "\"result\":\"busy\""));
    assert(fake.serial.generation == generation); fake.busy = false;
    fake.recoveryRequired = true; send(console, "host baud 9600"); assert(has(fake, "\"result\":\"recovery_required\""));
    assert(fake.serial.generation == generation); fake.recoveryRequired = false;

    fake.setupFailure = true; send(console, "host set 9600 8E1");
    assert(has(fake, "\"failure\":\"adapter\"")); assert(has(fake, "\"active_known\":false"));
    assert(has(fake, "\"actual_baud\":0"));
    send(console, "host"); assert(has(fake, "\"blocked\":true"));
    const auto failedChanges = fake.changes;
    send(console, "host fmt 8N2"); assert(has(fake, "\"result\":\"active_tuple_unknown\"")); assert(fake.changes == failedChanges);
    fake.setupFailure = false; send(console, "host set 9600 8E1"); assert(has(fake, "\"ok\":true"));
    send(console, "config"); assert(has(fake, "\"format\":\"8E1\"")); assert(has(fake, "\"host_serial\":{\"known\":true,\"baud\":9600"));
    fake.restoreFailure = true; send(console, "host restore"); assert(has(fake, "\"failure\":\"restore\""));
    send(console, "config"); assert(has(fake, "\"format\":\"unknown\"")); assert(has(fake, "\"host_serial\":{\"known\":false"));
    fake.restoreFailure = false; send(console, "host restore"); assert(has(fake, "\"ok\":true"));

    // The record owns its original tuple; current host settings cannot relabel it.
    send(console, "@100 probe 1"); assert(fake.retained.operationId == 200);
    send(console, "@101 result 200"); assert(has(fake, "\"result\":\"pending\"")); assert(has(fake, "\"baud\":115200"));
    auto oldTuple = fake.retained.serialTuple; const auto oldGeneration = fake.retained.serialGeneration;
    Probe::ProbeResult result; result.transport.reason = Rtu::Reason::FRAME;
    result.codecChecked = true; result.rawModel = 60; result.transport.txAccepted = 8; result.transport.rxLength = 7;
    result.outcome = Rtu::Outcome::SUCCESS;
    assert(console.reportProbe(100, 1, 200, result, &oldTuple, oldGeneration));
    fake.retained.probe = result; fake.retained.pending = false;
    const auto oldSuffix = fake.last().substr(fake.last().find("\"host_serial\""));
    send(console, "@102 host set 19200 8O1"); assert(has(fake, "\"ok\":true"));
    send(console, "@103 result 200");
    assert(fake.last().substr(fake.last().find("\"host_serial\"")) == oldSuffix);
    assert(has(fake, "\"baud\":115200,\"format\":\"8N1\""));
    assert(fake.serial.generation > oldGeneration);

    // BusOwner preserves accepted-TX uncertainty even for a read-only timeout.
    send(console, "@110 probe 1");
    Probe::ProbeResult timeout;
    timeout.transport.reason = Rtu::Reason::NO_RESPONSE; timeout.transport.txAccepted = 8;
    timeout.outcome = Rtu::Outcome::TRANSPORT; timeout.executionUnknown = true;
    assert(console.reportProbe(110, 1, 210, timeout, &fake.retained.serialTuple, fake.retained.serialGeneration));
    assert(has(fake, "\"transport\":\"NO_RESPONSE\"")); assert(has(fake, "\"codec\":\"NOT_CHECKED\""));
    assert(has(fake, "\"outcome\":\"transport\"")); assert(has(fake, "\"execution_unknown\":true"));
    assert(has(fake, "\"tx_bytes\":8,\"rx_bytes\":0"));
    assert(has(fake, "\"raw_model\":null")); assert(has(fake, "\"timing_valid\":false"));

    Fake legacy; auto absent = legacy.hooks(); absent.hostSerial = nullptr; Probe::Console oldConsole(absent);
    send(oldConsole, "help"); assert(!has(legacy, "\"host\""));
    send(oldConsole, "host"); assert(has(legacy, "\"result\":\"unavailable\""));
    send(oldConsole, "config"); assert(has(legacy, "\"format\":\"8N1\""));
    return 0;
}
