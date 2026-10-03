// SPDX-License-Identifier: MIT
// Wire events progress independently of capture sampling and runner servicing.
// Exercises the actual adapter's polling/timer limits against simulated wire events.
#include "../examples/common/Esp32S3Uart.h"
#include <MotorControlRS/profiles/ess_rs/Codec.h>
#include "fakes/esp32_uart/Hardware.h"
#include <cassert>
#include <vector>

namespace Rtu = MotorControlRSExample::Rtu;
namespace Ess = MotorControlRS::ESS_RS;
using MotorControlRSExample::Esp32S3Uart;
namespace {
constexpr Esp32S3Uart::Pins PINS = {47, 48, 21, true};
const std::vector<uint8_t> REPLY = {1, 3, 2, 0, 0x3C, 0xB8, 0x55};

Rtu::Timing timing() {
    Rtu::Timing value;
    assert(Rtu::setRtuTiming(115200, 10, value));
    value.setupUs = 20;
    value.busTimeoutUs = 10000;
    value.txTimeoutUs = 25000;
    value.captureTimeoutUs = 5000;
    return value;
}
struct Fixture {
    Esp32S3Uart uart;
    uint8_t tx[32] = {}, rx[64] = {}, bytes[8] = {};
    Rtu::Request request;
    Rtu::Runner runner;

    Rtu::Storage storage() {
        Rtu::Storage value;
        value.tx = tx; value.txCapacity = sizeof(tx);
        value.rx = rx; value.rxCapacity = sizeof(rx);
        return value;
    }
    explicit Fixture(uint16_t words = 1) : runner(uart.port(), storage(), timing()) {
        resetHardware();
        hardware.txCharacterUs = 87;
        assert(uart.begin(PINS));
        request.bytes = bytes;
        request.length = words == 1 ? Ess::buildProbe(1, bytes, sizeof(bytes)) :
            Ess::buildReadRegisters(1, 0x0130, words, bytes, sizeof(bytes));
        request.replyLength = Ess::expectedReadRegistersLen(words);
        request.responseTimeoutUs = 10000;
        assert(runner.start(request, hardware.time) == Rtu::Admission::STARTED);
    }
    void service(bool pollRunner = true) {
        advanceHardware(hardware.time + 10);
        const uint64_t at = uart.sample();
        if (pollRunner) runner.poll(at);
    }
    void receive() {
        for (unsigned i = 0; i < 1000 && runner.phase() != Rtu::Phase::RECEIVE; ++i) service();
        assert(runner.phase() == Rtu::Phase::RECEIVE);
        assert(hardware.writes == 1 && hardware.de == 0);
    }
    void finish() {
        for (unsigned i = 0; i < 3000 && runner.busy(); ++i) service();
        assert(!runner.busy());
    }
    void retainedFailure(Rtu::Reason reason) {
        assert(runner.result().reason == reason && runner.needsRecovery());
        for (unsigned i = 0; i < 2000; ++i) service();
        assert(runner.result().reason == reason && hardware.writes == 1);
        assert(runner.start(request, hardware.time) == Rtu::Admission::RECOVERY_REQUIRED);
    }
};

void testScheduledSuccess() {
    Fixture test;
    test.receive();
    scheduleReply(hardware.time + 2000, REPLY);
    test.finish();
    assert(test.runner.result().reason == Rtu::Reason::FRAME);
    assert(test.runner.result().txComplete && test.uart.captureFaults() == 0);
    uint16_t model = 0;
    assert(Ess::parseProbe(test.rx, test.runner.result().rxLength, 1, model) && model == 60);
    assert(hardware.writes == 1 && hardware.events.empty());
}
void testOwnerDelayAfterCapture() {
    Fixture test;
    test.receive();
    scheduleReply(hardware.time + 2000, REPLY);
    // Sampling continues every ~13 us, but the owner drains nothing for 5 ms.
    const uint64_t until = hardware.time + 5000;
    while (hardware.time < until) test.service(false);
    assert(hardware.rx.empty() && hardware.droppedBytes == 0);
    assert(test.uart.captureFaults() == 0);
    test.finish();
    assert(test.runner.result().reason == Rtu::Reason::FRAME && hardware.writes == 1);
}
void testCaptureQueueOverflow() {
    Fixture test;
    test.receive();
    const std::vector<uint8_t> noise(Esp32S3Uart::CAPTURE_CAPACITY + 1, 0x55);
    scheduleReply(hardware.time + 2000, noise);
    const uint64_t until = hardware.time + 2000 + noise.size() * 87 + 100;
    while (hardware.time < until) test.service(false);
    assert(hardware.rx.empty() && hardware.droppedBytes == 0);
    assert(test.uart.captureFaults() == 1 && test.uart.rxErrors() == 0);
    test.service();
    test.retainedFailure(Rtu::Reason::RX_ERROR);
}
void testDelayedCaptureSeesBatch() {
    Fixture test;
    test.receive();
    scheduleReply(hardware.time + 2000, REPLY);
    advanceHardware(hardware.time + 5000); // Wire continues while all servicing sleeps.
    assert(hardware.rx.size() == REPLY.size() && hardware.droppedBytes == 0);
    test.service();
    assert(test.uart.captureFaults() == 1 && test.uart.rxErrors() == 0);
    test.retainedFailure(Rtu::Reason::RX_ERROR);
}
void testFifoOverflow() {
    Fixture test;
    test.receive();
    const std::vector<uint8_t> noise(140, 0x55);
    scheduleReply(hardware.time + 2000, noise);
    advanceHardware(hardware.time + 15000);
    assert(hardware.rx.size() == 128 && hardware.droppedBytes == 12);
    test.service();
    assert(test.uart.captureFaults() == 1 && test.uart.rxErrors() == 1);
    test.retainedFailure(Rtu::Reason::RX_ERROR);
}
void testMissingCaptureEvidence() {
    Fixture test;
    test.receive();
    // No UART sample: an old EMPTY watermark cannot become a new silence proof.
    advanceHardware(hardware.time + 6000);
    test.runner.poll(hardware.time);
    assert(test.uart.captureFaults() == 0);
    test.retainedFailure(Rtu::Reason::CAPTURE_TIMEOUT);
}
void testInterruptedReplyGap() {
    Fixture test;
    test.receive();
    // Every byte is captured, but a measured 900 us stop-to-start gap is neither
    // legal within a frame (750 us) nor a frame boundary (1750 us).
    scheduleReply(hardware.time + 2000, REPLY, 87, 900);
    test.finish();
    assert(test.uart.captureFaults() == 0);
    test.retainedFailure(Rtu::Reason::GAP);
}
void testAmbiguousSingleByte() {
    Fixture test;
    test.receive();
    test.service(); // Establish one receive-mode idle sample after DE release.
    scheduleReply(hardware.time + 2000, std::vector<uint8_t>(1, 1));
    advanceHardware(hardware.time + 3000);
    test.service();
    // Only one byte survives, but its arrival interval crosses the required
    // turnaround gap. A plausible byte value does not restore its lost timing.
    assert(test.uart.captureFaults() == 0 && test.uart.maxRxWidthUs() > 2000);
    test.finish();
    test.retainedFailure(Rtu::Reason::TIMING_UNCERTAIN);
}
void testLateReplyCannotClearTimeout() {
    Fixture test;
    test.receive();
    scheduleReply(hardware.time + 12000, REPLY);
    test.finish();
    assert(test.runner.result().reason == Rtu::Reason::NO_RESPONSE);
    assert(!hardware.events.empty()); // Reply has not arrived at the terminal result.
    test.retainedFailure(Rtu::Reason::NO_RESPONSE);
    assert(hardware.events.empty() && test.runner.result().rxLength == 0);
}
void testForeignFrameNeedsCheckedParser() {
    Fixture test;
    test.receive();
    const std::vector<uint8_t> foreign = {2, 3, 2, 0, 0x3C, 0xFC, 0x55};
    scheduleReply(hardware.time + 2000, foreign);
    test.finish();
    assert(test.runner.result().reason == Rtu::Reason::FRAME);
    uint16_t model = 0xAA55;
    Ess::FrameError error = Ess::FrameError::NONE;
    assert(Ess::parseProbe(test.rx, test.runner.result().rxLength, 2, model));
    model = 0xAA55;
    assert(!Ess::parseProbe(test.rx, test.runner.result().rxLength, 1, model, &error));
    assert(model == 0xAA55 && error == Ess::FrameError::ADDRESS);
    // The runner collects framing only. The application retains this rejection
    // and settles possible later traffic; the runner never invents a retry.
    for (unsigned i = 0; i < 2000; ++i) test.service();
    assert(hardware.writes == 1 && test.runner.result().reason == Rtu::Reason::FRAME);
}
void testBackgroundCaptureWhileOwnerSleeps() {
    Fixture test;
    assert(test.uart.startCapture());
    bool scheduled = false;
    for (unsigned i = 0; i < 20 && test.runner.busy(); ++i) {
        // Only the hardware timer samples during this interval. Owner service
        // at 5 ms must not hold DE over the independent drive response.
        advanceHardware(hardware.time + 5000);
        test.runner.poll(test.uart.sample());
        if (hardware.writes && !scheduled) {
            scheduleReply(hardware.writeStarted + 8 * 87 + 2000, REPLY);
            scheduled = true;
        }
    }
    assert(scheduled && hardware.timerCallbacks > 500);
    assert(test.runner.result().reason == Rtu::Reason::FRAME);
    assert(hardware.de == 0 && hardware.writes == 1 && test.uart.captureFaults() == 0);
    uint16_t model = 0;
    assert(Ess::parseProbe(test.rx, test.runner.result().rxLength, 1, model) && model == 60);
}
void testMaskedCaptureCannotReconstructBatch() {
    Fixture test;
    assert(test.uart.startCapture());
    test.receive();
    scheduleReply(hardware.time + 2000, REPLY);
    fakeEnterCritical();
    advanceHardware(hardware.time + 5000);
    assert(hardware.rx.size() == REPLY.size());
    fakeExitCritical(); // One overdue callback sees a batch, not fabricated history.
    test.service();
    assert(test.uart.captureFaults() == 1 && test.uart.rxErrors() == 0);
    test.retainedFailure(Rtu::Reason::RX_ERROR);
}

std::vector<uint8_t> longReply() {
    std::vector<uint8_t> bytes = {1, 3, 32};
    for (uint16_t word = 0; word < 16; ++word) {
        bytes.push_back(static_cast<uint8_t>(word));
        bytes.push_back(static_cast<uint8_t>(255 - word));
    }
    const uint16_t crc = Ess::calcCrc16(bytes.data(), bytes.size());
    bytes.push_back(static_cast<uint8_t>(crc));
    bytes.push_back(static_cast<uint8_t>(crc >> 8));
    assert(bytes.size() == 37);
    return bytes;
}

void testLongestReplyWhileOwnerSleeps() {
    Fixture test(16);
    assert(test.uart.startCapture());
    test.receive();
    scheduleReply(hardware.time + 2000, longReply());
    // A complete 37-byte reply and its final silence arrive with no task sample
    // or runner service. Only the independent timer owns capture in this gap.
    const unsigned callbacks = hardware.timerCallbacks;
    advanceHardware(hardware.time + 8000);
    assert(hardware.timerCallbacks > callbacks + 300);
    assert(hardware.events.empty() && hardware.rx.empty() && hardware.droppedBytes == 0);
    assert(test.uart.stats().highWater == 37 && test.uart.captureFaults() == 0);
    test.finish();
    assert(test.runner.result().reason == Rtu::Reason::FRAME);
    assert(test.runner.result().rxLength == 37 && hardware.writes == 1 && hardware.de == 0);
    uint16_t words[16] = {};
    std::size_t count = 0;
    assert(Ess::parseRegisters(test.rx, 37, 1, 16, words, 16, count));
    assert(count == 16);
    for (uint16_t index = 0; index < 16; ++index)
        assert(words[index] == static_cast<uint16_t>((index << 8) | (255 - index)));
}

void testLongestReplyDuringMaskedCapture() {
    Fixture test(16);
    assert(test.uart.startCapture());
    test.receive();
    scheduleReply(hardware.time + 2000, longReply());
    fakeEnterCritical();
    advanceHardware(hardware.time + 8000);
    assert(hardware.rx.size() == 37 && hardware.droppedBytes == 0);
    fakeExitCritical();
    test.service();
    // FIFO capacity was sufficient, but arrival evidence was lost. A valid CRC
    // cannot turn a batch accumulated during starvation into a timed frame.
    assert(test.uart.captureFaults() == 1 && test.uart.rxErrors() == 0);
    test.retainedFailure(Rtu::Reason::RX_ERROR);
}

void testShortExceptionToLongestRead() {
    Fixture test(16);
    assert(test.uart.startCapture());
    test.receive();
    std::vector<uint8_t> exception = {1, 0x83, 2};
    const uint16_t crc = Ess::calcCrc16(exception.data(), exception.size());
    exception.push_back(static_cast<uint8_t>(crc));
    exception.push_back(static_cast<uint8_t>(crc >> 8));
    scheduleReply(hardware.time + 2000, exception);
    advanceHardware(hardware.time + 5000);
    test.finish();
    assert(test.runner.result().reason == Rtu::Reason::FRAME);
    assert(test.runner.result().rxLength == 5 && hardware.writes == 1);
    uint16_t words[16] = {};
    words[0] = 0xA55A;
    std::size_t count = 16;
    const auto status = Ess::parseRegisters(test.rx, 5, 1, 16, words, 16, count);
    assert(status.code == MotorControlRS::Err::EXCEPTION && status.detail == 2);
    assert(count == 0 && words[0] == 0xA55A && test.uart.captureFaults() == 0);
}
}
int main() {
    testScheduledSuccess(); testOwnerDelayAfterCapture(); testDelayedCaptureSeesBatch();
    testCaptureQueueOverflow(); testFifoOverflow(); testMissingCaptureEvidence(); testInterruptedReplyGap();
    testAmbiguousSingleByte();
    testLateReplyCannotClearTimeout(); testForeignFrameNeedsCheckedParser();
    testBackgroundCaptureWhileOwnerSleeps(); testMaskedCaptureCannotReconstructBatch();
    testLongestReplyWhileOwnerSleeps(); testLongestReplyDuringMaskedCapture();
    testShortExceptionToLongestRead();
}
