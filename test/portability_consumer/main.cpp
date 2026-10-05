// SPDX-License-Identifier: MIT
// Compile/link fixture only: no UART, GPIO, clock, USB, PSRAM or motor backend.
#include <MotorControlRS/MotorControlRS.h>
#include <MotorControlRS/profiles/ess_rs/Codec.h>
#include "../../examples/common/RtuBusOwner.h"
#include "../../examples/common/EssRtuValidator.h"
#include "../../examples/common/HostSerial.h"
#include "../../examples/probe_cli/ProbeConsole.h"
#include <cstdlib>
#include <cstring>

namespace {
namespace Rtu = MotorControlRSExample::Rtu;
namespace Probe = MotorControlRSExample::Probe;
namespace Ess = MotorControlRS::ESS_RS;

uint8_t tx[8], rx[64];
Rtu::Trace trace[16];
Rtu::PendingSlot pending[1];
Rtu::ResultSlot results[1];
Rtu::ProducerSlot producers[1];
char output[Probe::OUTPUT_CAPACITY];
std::size_t emitted = 0;

Rtu::Storage runnerStorage() {
    Rtu::Storage value;
    value.tx = tx; value.txCapacity = sizeof(tx);
    value.rx = rx; value.rxCapacity = sizeof(rx);
    value.trace = trace; value.traceCapacity = 16;
    return value;
}
Rtu::Timing runnerTiming() {
    MotorControlRSExample::HostTiming value;
    if (!MotorControlRSExample::hostTiming(MotorControlRSExample::HostTuple(), value)) std::abort();
    return value.runner;
}
Rtu::BusStorage ownerStorage() {
    Rtu::BusStorage value;
    value.pending = pending; value.pendingCapacity = 1;
    value.results = results; value.resultCapacity = 1;
    value.producers = producers; value.producerCapacity = 1;
    return value;
}
// A missing port is deliberately unavailable, rather than fabricated evidence
// of an S2 transceiver or physical timing. Construction itself performs no I/O.
Rtu::Runner runner{Rtu::Port(), runnerStorage(), runnerTiming()};
Rtu::BusOwner owner(runner, ownerStorage());

bool emit(void*, const char* text, std::size_t length) {
    if (!text || length >= sizeof(output)) return false;
    std::memcpy(output, text, length); output[length] = '\0';
    ++emitted;
    return true;
}
Probe::Host consoleHost() {
    Probe::Host value; value.emitLine = emit;
    // Motor callbacks are absent: the real console reports unavailable routes.
    return value;
}
Probe::Console console(consoleHost());

void checkPortableConsumption() {
    const std::size_t length = Ess::buildProbe(1, tx, sizeof(tx));
    if (length != sizeof(tx) || Ess::calcCrc16(tx, length)) std::abort();
    const uint8_t reply[] = {1, 3, 2, 3, 5, 0x78, 0xB7}; // Synthetic codec fixture.
    uint16_t model = 0;
    if (!Ess::parseProbe(reply, sizeof(reply), 1, model) || model != 0x0305) std::abort();
    if (Ess::parseProbe(reply, sizeof(reply), 2, model) || model != 0x0305) std::abort();

    Rtu::Request request; request.bytes = tx; request.length = length;
    request.replyLength = sizeof(reply); request.responseTimeoutUs = 200000;
    if (runner.start(request, 0) != Rtu::Admission::INVALID || !owner.valid()) std::abort();
    owner.service(0); // Empty owner with no physical backend; no transport call.

    Rtu::Expectation expected; expected.address = 1; expected.function = 3;
    expected.first = 0; expected.count = 1;
    const auto validator = Rtu::essValidator();
    if (!validator.checkRequest(expected, request) ||
        !validator.checkReply(expected, reply, sizeof(reply))) std::abort();

    const char commands[] = "@1 version\n@2 caps\n@3 probe\n";
    for (char c : commands) if (c) console.feed(c);
    console.serviceOutput();
    if (emitted != 3 || !std::strstr(output, "\"ok\":false")) std::abort();
}
}

// The fixture contains no framework calls. Building the IDF entry point is
// compile-only evidence and does not execute these checks.
extern "C" void app_main() { checkPortableConsumption(); }
