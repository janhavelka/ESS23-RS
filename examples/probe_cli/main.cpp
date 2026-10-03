// SPDX-License-Identifier: MIT
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <new>
#include "ProbeConsole.h"
#include "../common/E2Uart.h"
#include "../common/BuildConfig.h"
#if MOTORCONTROLRS_LOAD_FIXTURE
#include "E2Load.h"
#endif

using namespace MotorControlRSExample;
namespace {
constexpr uint32_t BAUD = 115200;
constexpr uint32_t REPLY_GAP_US = 304;
constexpr uint32_t RESPONSE_US = 200000; // Bench policy, not a vendor maximum.
constexpr uint32_t RECOVER_US = 500000;  // Explicit read-only host recovery guard.
E2Uart uart; // ISR capture state stays internal; application histories use PSRAM.
#if MOTORCONTROLRS_LOAD_FIXTURE
E2Load loadFixture;
bool fixtureReady = false;
uint64_t nextServiceUs = 0;
#endif
bool platformReady = false;
uint64_t nowUs() { return static_cast<uint64_t>(esp_timer_get_time()); }

Rtu::Storage storage(uint8_t* tx, uint8_t* rx, Rtu::Trace* trace) {
    Rtu::Storage s; s.tx = tx; s.txCapacity = 32; s.rx = rx; s.rxCapacity = 64;
    s.trace = trace; s.traceCapacity = 128; return s;
}
Rtu::Timing timing() {
    Rtu::Timing t;
    Rtu::setRtuTiming(BAUD, 10, t);
    t.setupUs = 20; t.holdUs = 20; // Conservative bench choices; electrical qualification pending.
    t.busTimeoutUs = 100000; t.txTimeoutUs = 20000; t.captureTimeoutUs = 10000;
    return t;
}
struct App;
Probe::Host host(App* app);
struct App {
    uint8_t tx[32] = {}, rx[64] = {};
    Rtu::Trace trace[128];
    Rtu::Runner runner;
    Probe::Console console;
    uint32_t activeId = 0;
    uint8_t address = 1;
    uint16_t model = 0;
    uint64_t finishedUs = 0;
    bool active = false, known = false, ok = false, parserFault = false;
    bool codecChecked = false;
    MotorControlRS::Status codec;
    MotorControlRS::ESS_RS::FrameError frameError = MotorControlRS::ESS_RS::FrameError::NONE;
    App() : runner(uart.port(), storage(tx, rx, trace), timing()), console(host(this)) {}
};
App* app = nullptr;

void emit(void*, const char* text, std::size_t size) {
#if MOTORCONTROLRS_LOAD_FIXTURE
    loadFixture.writeLine(text, size);
#else
    Serial.write(reinterpret_cast<const uint8_t*>(text), size);
    Serial.write('\n');
#endif
}
void snapshot(void* context, Probe::Snapshot& s) {
    App& a = *static_cast<App*>(context);
    s.address = 1; s.probeAddress = a.address; s.baud = BAUD; s.responseTimeoutUs = RESPONSE_US;
    s.replyGapUs = REPLY_GAP_US; s.gap15Us = timing().gap15Us; s.gap35Us = timing().gap35Us;
    s.uptimeMs = nowUs() / 1000; s.ready = platformReady;
    s.timingQualified = false; // No external TX/RX/DE trace qualifies the sampling guard yet.
    s.busy = a.active; s.recoveryRequired = a.runner.needsRecovery() || uart.needsRecovery() || a.parserFault;
    s.phase = a.runner.phase(); s.transport = a.runner.result().reason;
    s.transmitEnabled = a.runner.transmitEnabled();
    s.codecChecked = a.codecChecked; s.codec = a.codec; s.frameError = a.frameError;
    s.probeKnown = a.known; s.probeOk = a.ok; s.rawModel = a.model;
    s.ageMs = a.known ? (nowUs() - a.finishedUs) / 1000 : 0;
    s.stats = a.runner.stats();
    const auto capture = uart.stats();
    s.maxPollGapUs = capture.maxGapUs; s.captureFaults = capture.faults; s.rxErrors = capture.rxErrors;
    s.memoryValid = true;
    s.internalFree = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    s.internalMin = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    s.internalLargest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    s.psramFree = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s.psramMin = heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s.psramLargest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s.stackFreeBytes = uxTaskGetStackHighWaterMark(nullptr); // ESP-IDF reports bytes.
}
Probe::Action probe(void* context, uint32_t id, uint8_t address) {
    App& a = *static_cast<App*>(context);
    if (!platformReady) return Probe::Action::UNAVAILABLE;
    if (a.active) return Probe::Action::BUSY;
    const uint64_t sampled = uart.sample(); // Include faults first observed at admission.
    if (a.parserFault || a.runner.needsRecovery() || uart.needsRecovery()) return Probe::Action::RECOVERY_REQUIRED;
    uint8_t bytes[8];
    Rtu::Request request;
    request.bytes = bytes;
    request.length = MotorControlRS::ESS_RS::buildProbe(address, bytes, sizeof(bytes));
    request.replyLength = 7; request.responseTimeoutUs = RESPONSE_US;
    // Observed E2 responder starts around 0.5 ms after TX. Explicit bench
    // deviation from recommended high-baud t3.5=1750 us: allow 3.5 8N1 chars.
    // Host admission/final framing still use 1750 us. Not a family-wide guarantee.
    request.replyGapUs = REPLY_GAP_US;
    const Rtu::Admission admitted = a.runner.start(request, sampled);
    if (admitted != Rtu::Admission::STARTED) return Probe::Action::FAILED;
    if (a.address != address) a.known = a.ok = false;
    a.address = address; a.activeId = id; a.active = true;
    return Probe::Action::OK;
}
Probe::Action recover(void* context) {
    App& a = *static_cast<App*>(context);
    if (a.active || a.runner.transmitEnabled()) return Probe::Action::BUSY;
    // A user-requested host reset policy for this one non-changing read, not
    // proof that an arbitrarily delayed response cannot still arrive.
    if (a.finishedUs && nowUs() - a.finishedUs < RECOVER_US) return Probe::Action::BUSY;
    if (!uart.clear() || !a.runner.recover(uart.sample())) return Probe::Action::FAILED;
    a.known = a.ok = a.parserFault = false;
    return Probe::Action::OK;
}
void reset(void* context) {
    static_cast<App*>(context)->runner.clearStats();
#if MOTORCONTROLRS_LOAD_FIXTURE
    loadFixture.resetStats(uart);
#else
    uart.resetStats();
#endif
}
#if MOTORCONTROLRS_LOAD_FIXTURE
Probe::Action load(void* context, const Probe::LoadSettings* requested, Probe::LoadSnapshot& out) {
    App& a = *static_cast<App*>(context);
    if (!fixtureReady) return Probe::Action::UNAVAILABLE;
    if (requested && (a.active || a.runner.transmitEnabled())) return Probe::Action::BUSY;
    const auto result = loadFixture.configure(requested, out, uart);
    out.ready = platformReady;
    if (requested) nextServiceUs = 0;
    return result;
}
#endif
Probe::Host host(App* a) {
    Probe::Host h; h.context = a; h.emitLine = emit; h.snapshot = snapshot;
    h.startProbe = probe; h.recover = recover; h.resetStats = reset;
#if MOTORCONTROLRS_LOAD_FIXTURE
    h.load = load;
#endif
    return h;
}

void complete(App& a) {
    Probe::ProbeResult result;
    result.transport = a.runner.result();
    result.tx = a.tx; result.txLength = 8;
    result.rx = a.rx; result.rxLength = result.transport.rxLength;
    if (result.transport.txAccepted) {
        const auto capture = uart.stats();
        result.txEndUs = capture.txEndUs; result.txUncertaintyUs = capture.txWidthUs;
    }
    for (std::size_t i = 0; i < a.runner.traceSize(); ++i) {
        const Rtu::Trace& e = *a.runner.traceAt(i);
        if (e.event == Rtu::Event::RX && e.atUs >= result.transport.startedUs &&
            (e.phase == Rtu::Phase::DRAIN || e.phase == Rtu::Phase::HOLD || e.phase == Rtu::Phase::RECEIVE)) {
            if (!result.firstRxStartUs) result.firstRxStartUs = e.wireStartUs;
            if (e.uncertaintyUs > result.maxRxUncertaintyUs) result.maxRxUncertaintyUs = e.uncertaintyUs;
        }
    }
    result.timingValid = result.transport.reason == Rtu::Reason::FRAME;
    if (result.timingValid) {
        result.codecChecked = true;
        result.codec = MotorControlRS::ESS_RS::parseProbe(a.rx, result.rxLength, a.address,
                                                        result.rawModel, &result.frameError);
        // A checked Modbus exception is a complete rejection, not corrupt framing.
        a.parserFault = !result.codec && result.codec.code != MotorControlRS::Err::EXCEPTION;
    }
    a.codecChecked = result.codecChecked; a.codec = result.codec; a.frameError = result.frameError;
    a.ok = result.timingValid && result.codecChecked && result.codec.isOk();
    a.known = true; a.finishedUs = nowUs(); a.model = a.ok ? result.rawModel : 0;
    a.active = false;
    a.console.reportProbe(a.activeId, a.address, result);
}
}

void setup() {
#if MOTORCONTROLRS_LOAD_FIXTURE
    // The stock 256-byte ring cannot admit the fixture's largest diagnostic
    // line. Allocate once before the USB ISR starts; this driver buffer must
    // remain internal. Histories and console formatting still live in PSRAM.
    const bool consoleReady = Serial.setTxBufferSize(1024) == 1024;
#endif
    Serial.begin(kConsoleBaud);
#if MOTORCONTROLRS_LOAD_FIXTURE
    Serial.setTxTimeoutMs(5); // Bound each SDK enqueue wait under backpressure.
#endif
    void* memory = heap_caps_malloc(sizeof(App), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!memory) {
        Serial.println("{\"type\":\"boot\",\"ok\":false,\"error\":\"psram_allocation\"}");
        return; // No silent large internal-RAM fallback.
    }
    platformReady = uart.begin(BAUD);
#if MOTORCONTROLRS_TIMER_CAPTURE
    platformReady = platformReady && uart.startCapture(20, timing().holdUs);
#endif
#if MOTORCONTROLRS_LOAD_FIXTURE
    fixtureReady = consoleReady && loadFixture.begin();
    platformReady = platformReady && fixtureReady;
#endif
    app = new (memory) App;
}

void loop() {
    if (!app) { delay(10); return; }
#if MOTORCONTROLRS_LOAD_FIXTURE
    if (app->active && nowUs() < nextServiceUs) { delay(1); return; }
#endif
    const uint64_t sampled = uart.sample();
#if MOTORCONTROLRS_LOAD_FIXTURE
    loadFixture.serviced(sampled, app->active);
#endif
    app->runner.poll(sampled);
    if (app->active && !app->runner.busy()) complete(*app);
#if MOTORCONTROLRS_LOAD_FIXTURE
    nextServiceUs = app->active ? sampled + loadFixture.ownerDelayUs() : 0;
#endif
    // Formatting/USB writes happen outside the wire timing path. Commands wait
    // at most the bounded probe duration. A terminal fault can still be draining
    // TX/DE; keep diagnostics available while the runner continues that cleanup.
    if (!app->active) {
        for (unsigned i = 0; i < 32 && Serial.available(); ++i) {
            app->console.feed(static_cast<char>(Serial.read()));
            if (app->active) break;
        }
        if (!app->active) delay(1);
    }
}
