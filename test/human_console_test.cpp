// SPDX-License-Identifier: MIT
#include "../examples/probe_cli/ProbeConsole.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdio>
#include <string>
#include <vector>

namespace Probe = MotorControlRSExample::Probe;
namespace Rtu = MotorControlRSExample::Rtu;
namespace Core = MotorControlRS;
namespace Ess = MotorControlRS::ESS_RS;

namespace {
struct Admission {
    uint32_t command = 0, operation = 0;
    uint8_t address = 0;
    Core::ActionRequest action;
};
struct Fake {
    Probe::Snapshot snapshot;
    Probe::DebugSnapshot debug;
    Probe::ResultView retained;
    uint32_t nextOperation = 100;
    unsigned snapshots = 0, actions = 0, lookups = 0, releases = 0;
    bool blocked = false;
    std::vector<Admission> admitted;
    std::vector<std::string> lines;

    static bool emit(void* context, const char* text, std::size_t length) {
        auto& self = *static_cast<Fake*>(context);
        if (self.blocked) return false;
        assert(length && length < Probe::OUTPUT_CAPACITY);
        self.lines.emplace_back(text, length);
        return true;
    }
    static void readSnapshot(void* context, Probe::Snapshot& out) {
        auto& self = *static_cast<Fake*>(context);
        ++self.snapshots;
        out = self.snapshot;
    }
    static Probe::Action probe(void* context, uint32_t command, uint8_t address, uint32_t& operation) {
        auto& self = *static_cast<Fake*>(context);
        Admission admitted;
        admitted.command = command;
        admitted.operation = operation = self.nextOperation++;
        admitted.address = address;
        self.admitted.push_back(admitted);
        return Probe::Action::OK;
    }
    static Probe::Action action(void* context, uint32_t command, uint8_t address,
                               const Core::ActionRequest& requested, uint32_t& operation) {
        auto& self = *static_cast<Fake*>(context);
        ++self.actions;
        const auto result = probe(context, command, address, operation);
        self.admitted.back().action = requested;
        return result;
    }
    static bool result(void* context, uint32_t operation, Probe::ResultView& out) {
        auto& self = *static_cast<Fake*>(context);
        ++self.lookups;
        if (operation && operation != self.retained.operationId) return false;
        out = self.retained;
        return true;
    }
    static Probe::Action release(void* context, uint32_t) {
        ++static_cast<Fake*>(context)->releases;
        return Probe::Action::OK;
    }
    static Probe::Action debugMode(void* context, const Probe::DebugMode* requested,
                                  Probe::DebugSnapshot& out) {
        auto& self = *static_cast<Fake*>(context);
        if (requested) self.debug.mode = *requested;
        self.debug.capacity = 16;
        out = self.debug;
        return Probe::Action::OK;
    }
    Probe::Host host() {
        Probe::Host out;
        out.context = this;
        out.emitLine = emit;
        out.snapshot = readSnapshot;
        out.startProbe = probe;
        out.startAction = action;
        out.result = result;
        out.release = release;
        out.debug = debugMode;
        return out;
    }
};

void send(Probe::Console& console, const std::string& command) {
    for (char c : command) console.feed(c);
}
bool json(const std::string& line) {
    return !line.empty() && line.front() == '{' && line.back() == '}';
}
std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}
void contains(const std::string& text, const char* wanted) {
    if (lower(text).find(lower(wanted)) == std::string::npos)
        std::fprintf(stderr, "Missing '%s' in:\n%s\n", wanted, text.c_str());
    assert(lower(text).find(lower(wanted)) != std::string::npos);
}
Probe::ProbeResult probeResult() {
    static const uint8_t tx[] = {1,3,0,0,0,1,0x84,0x0A};
    static const uint8_t rx[] = {1,3,2,0,0x3C,0xB8,0x55};
    Probe::ProbeResult result;
    result.transport.reason = Rtu::Reason::FRAME;
    result.transport.txComplete = true;
    result.transport.txAccepted = sizeof(tx);
    result.transport.rxLength = sizeof(rx);
    result.codecChecked = true;
    result.rawModel = 60;
    result.tx = tx; result.txLength = sizeof(tx);
    result.rx = rx; result.rxLength = sizeof(rx);
    result.outcome = Rtu::Outcome::SUCCESS;
    return result;
}
Ess::ActionContext actionResult(const Admission& admitted) {
    Ess::ActionContext context;
    context.operationId = admitted.operation;
    context.target.id = context.target.address = admitted.address;
    context.target.generation = 1;
    context.request = admitted.action;
    context.state = Core::ActionState::SUCCEEDED;
    context.outcome = Core::ActionOutcome::OBSERVED;
    context.execution = Core::ActionExecution::ACKNOWLEDGED;
    context.completion = Core::ActionCompletion::OBSERVED;
    return context;
}

void helpAndSyntax() {
    Fake fake;
    Probe::Console console(fake.host());
    send(console, "help\n");
    assert(!json(fake.lines.back()));
    contains(fake.lines.back(), "probe");
    contains(fake.lines.back(), "debug");
    contains(fake.lines.back(), "help");
    send(console, "help\n");
    assert(!json(fake.lines.back()));
    contains(fake.lines.back(), "probe");
    send(console, "help probe\n");
    assert(!json(fake.lines.back()));
    contains(fake.lines.back(), "probe [address]");
    send(console, "probe nonsense\n");
    assert(!json(fake.lines.back()));
    contains(fake.lines.back(), "probe");
    contains(fake.lines.back(), "usage");
    send(console, "stop\n");
    assert(!json(fake.lines.back()));
    contains(fake.lines.back(), "stop");
    contains(fake.lines.back(), "normal");
    contains(fake.lines.back(), "fast");
    assert(fake.admitted.empty() && fake.actions == 0 && fake.releases == 0);

    send(console, "@42 help probe\n");
    assert(json(fake.lines.back()));
    contains(fake.lines.back(), "\"id\":42");
    contains(fake.lines.back(), "\"syntax\":\"probe [address]\"");
    send(console, "@43 probe nonsense\n");
    assert(json(fake.lines.back()));
    contains(fake.lines.back(), "\"ok\":false");
    send(console, "@44 " + std::string(Probe::LINE_CAPACITY + 1, 'x') + "\n");
    assert(json(fake.lines.back()));
    send(console, "status\n");
    assert(!json(fake.lines.back()));

    Fake machine;
    Probe::Console machineConsole(machine.host(), Probe::Format::JSON);
    send(machineConsole, "help\nstatus\n");
    assert(machine.lines.size() == 2 && json(machine.lines[0]) && json(machine.lines[1]));
    assert(machineConsole.welcome());
    assert(!json(machine.lines.back()));
    contains(machine.lines.back(), "MotorControl-RS");
    contains(machine.lines.back(), "help");
    send(machineConsole, "status\n");
    assert(json(machine.lines.back())); // Greeting does not change configured format.
}

void cachedStatusAndAsyncFormats() {
    Fake fake;
    fake.snapshot.ready = true;
    fake.snapshot.busy = false;
    fake.snapshot.phase = Rtu::Phase::IDLE;
    fake.snapshot.address = 7;
    Probe::Console console(fake.host());
    send(console, "status\n");
    assert(!json(fake.lines.back()) && fake.snapshots == 1 && fake.admitted.empty());
    contains(fake.lines.back(), "status");
    contains(fake.lines.back(), "ready");

    send(console, "probe 1\n");
    const Admission human = fake.admitted.back();
    assert(!json(fake.lines.back()));
    send(console, "@90 probe 1\n");
    const Admission machine = fake.admitted.back();
    assert(json(fake.lines.back()));
    // An unrelated command must not change an admitted operation's format.
    send(console, "@91 status\n");
    const auto completed = probeResult();
    fake.retained.commandId = human.command;
    fake.retained.operationId = human.operation;
    fake.retained.address = human.address;
    fake.retained.probe = completed;
    send(console, "@93 result " + std::to_string(human.operation) + "\n");
    assert(json(fake.lines.back())); // Inspection neither consumes nor restyles its later terminal.
    assert(console.reportProbe(human.command, human.address, human.operation, completed));
    assert(!json(fake.lines.back()));
    contains(fake.lines.back(), "probe");
    contains(fake.lines.back(), std::to_string(human.operation).c_str());
    send(console, "status\n");
    assert(console.reportProbe(machine.command, machine.address, machine.operation, completed));
    assert(json(fake.lines.back()));
    contains(fake.lines.back(), "\"id\":90");
    assert(!console.reportProbe(machine.command, machine.address, machine.operation, completed));

    fake.retained.commandId = human.command;
    fake.retained.operationId = human.operation;
    fake.retained.address = human.address;
    fake.retained.probe = completed;
    send(console, "@92 result " + std::to_string(human.operation) + "\n");
    assert(json(fake.lines.back()));
    contains(fake.lines.back(), "\"command\":\"result\"");
    send(console, "result " + std::to_string(human.operation) + "\n");
    assert(!json(fake.lines.back()));
    contains(fake.lines.back(), std::to_string(human.operation).c_str());
    assert(fake.lookups == 3 && fake.releases == 0);
}
void unifiedHelpHasOneEntryPerCommand() {
    Fake fake; auto host = fake.host();
    host.simpleMotion = [](void*, uint32_t, const Probe::SimpleMotionCommand*, Probe::SimpleMotionView&) {
        return Probe::Action::OK;
    };
    host.startDriver = [](void*, uint32_t, uint8_t, Ess::DriverKind, const Ess::DriverRequest&, uint32_t&) {
        return Probe::Action::OK;
    };
    host.motionProfile = [](void*, Probe::MotionProfileCommand, Probe::MotionProfileView&) {
        return Probe::Action::OK;
    };
    Probe::Console console(host);
    send(console, "help\n");
    const auto menu=fake.lines.back();
    for(const char* name : {"settings", "moveby", "moveto", "driver", "motion-profile", "debug", "stop"}) {
        const std::string row=std::string("\n  ")+name+" ";
        const auto at=menu.find(row);
        assert(at!=std::string::npos && menu.find(row,at+1)==std::string::npos);
    }
    for(const char* alias : {"?", "ver", "ping", "reset"})
        assert(menu.find(std::string("\n  ")+alias+" ")==std::string::npos);
    contains(menu,"ESS emergency stop without the ramp");
    contains(menu,"help COMMAND");
    contains(menu,"no separate advanced list");
    for (const char* removed : {"?", "ver", "ping", "reset", "health check", "help advanced", "profile ess_rs enable", "stop direct"}) {
        send(console, std::string(removed) + "\n");
        contains(fake.lines.back(), "ERROR");
        assert(fake.admitted.empty() && fake.actions == 0);
    }
    send(console, "help stop\n");
    contains(fake.lines.back(),"stop normal|fast");
    contains(fake.lines.back(),"reserved priority");
    contains(fake.lines.back(),"not a hardwired emergency-stop circuit");
    send(console, "help ping\n");
    contains(fake.lines.back(),"unknown");
    assert(fake.admitted.empty() && fake.actions == 0);
    send(console, "@90 help\n");
    assert(json(fake.lines.back()));
    for(const char* alias : {"?", "ver", "ping", "reset"})
        assert(fake.lines.back().find(std::string("\"")+alias+"\"")==std::string::npos);

}

void reservedStopAndBlockedOutput() {
    Fake fake;
    Probe::Console console(fake.host());
    // Occupy all ordinary correlations, retaining the separate urgent stop slot.
    for (std::size_t i = 0; i < Probe::OUTSTANDING_CAPACITY - 1; ++i)
        send(console, "probe\n");
    assert(fake.admitted.size() == Probe::OUTSTANDING_CAPACITY - 1);
    const Admission interrupted = fake.admitted.front();
    send(console, "enable\n");
    assert(fake.actions == 0);
    fake.blocked = true;
    auto completed = probeResult();
    assert(console.reportProbe(interrupted.command, interrupted.address, interrupted.operation, completed));
    assert(console.outputPending());
    send(console, "@501 stop fast\n");
    assert(fake.actions == 1);
    const Admission stop = fake.admitted.back();
    send(console, "stop normal\n@502 status\n");
    assert(fake.actions == 1 && console.inputDropped() >= 2);
    auto stopped = actionResult(stop);
    assert(!console.reportAction(stop.command, stop.operation, stopped));
    const auto before = fake.lines.size();
    fake.blocked = false;
    assert(console.serviceOutput());
    assert(fake.lines.size() == before + 2);
    assert(!json(fake.lines[before])); // Retained human terminal is immutable.
    assert(json(fake.lines[before + 1])); // Deferred stop admission keeps @501.
    contains(fake.lines[before + 1], "\"id\":501");
    send(console, "status\n");
    assert(console.reportAction(stop.command, stop.operation, stopped));
    assert(json(fake.lines.back()));
    contains(fake.lines.back(), "\"completion\":\"observed\"");

    // Conversely, a human stop remains human behind a blocked JSON reply.
    Fake other;
    Probe::Console second(other.host());
    other.blocked = true;
    send(second, "@600 status\nstop normal\n");
    assert(other.actions == 1);
    const Admission humanStop = other.admitted.back();
    other.blocked = false;
    assert(second.serviceOutput());
    assert(other.lines.size() == 2 && json(other.lines[0]) && !json(other.lines[1]));
    send(second, "@601 status\n");
    stopped = actionResult(humanStop);
    assert(second.reportAction(humanStop.command, humanStop.operation, stopped));
    assert(!json(other.lines.back()));
    contains(other.lines.back(), "Drive reported stopped");
}

void debugStreamPresentation() {
    Fake fake;
    Probe::Console console(fake.host());
    Core::TrafficRecord tx;
    tx.kind = Core::TrafficKind::TX;
    tx.sequence = tx.transaction = 1;
    tx.complete = true;
    tx.length = static_cast<uint16_t>(Ess::buildReadRegisters(1, 0, 1, tx.bytes, sizeof(tx.bytes)));
    send(console, "debug decoded\n");
    assert(!json(fake.lines.back()));
    assert(console.reportTraffic(tx, Probe::DebugMode::DECODED));
    assert(!json(fake.lines.back()));
    contains(fake.lines.back(), "TX");
    contains(fake.lines.back(), "DRIVER_MODEL");
    send(console, "@700 status\n@701 debug\n");
    assert(console.reportTraffic(tx, Probe::DebugMode::DECODED));
    assert(!json(fake.lines.back())); // Query does not change the stream style.
    send(console, "@702 debug raw\n");
    assert(console.reportTraffic(tx, Probe::DebugMode::RAW));
    assert(json(fake.lines.back()));
    contains(fake.lines.back(), "\"raw_hex\":\"010300000001840A\"");
    send(console, "status\ndebug\n");
    assert(console.reportTraffic(tx, Probe::DebugMode::RAW) && json(fake.lines.back()));
    fake.blocked = true;
    assert(!console.reportTraffic(tx, Probe::DebugMode::RAW) && !console.outputPending());
    send(console, "status\n");
    assert(console.outputPending());
    assert(!console.reportTraffic(tx, Probe::DebugMode::RAW));
    fake.blocked = false;
    assert(console.serviceOutput() && !json(fake.lines.back()));
}
} // namespace

int main() {
    helpAndSyntax();
    cachedStatusAndAsyncFormats();
    unifiedHelpHasOneEntryPerCommand();
    reservedStopAndBlockedOutput();
    debugStreamPresentation();
}
