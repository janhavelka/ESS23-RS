// SPDX-License-Identifier: MIT
#pragma once
#include "RtuRunner.h"

namespace MotorControlRSExample {

/** Exclusive UART2 adapter with application-supplied pins. Use exactly one
 * adapter instance and owner for UART2. No HardwareSerial/IDF UART driver may
 * own it concurrently. begin() detects SDK drivers, not another adapter.
 * ESP32-S3/IDF-5.5 specific.
 * Capture brackets are observations, not exact wire timestamps. The RX
 * character/stop-sampling guard remains an explicit qualification assumption.
 * Optional GPTimer capture continues RX and DE release while the owner sleeps.
 * Keep this object in internal RAM. Service sample() before runner.poll().
 * Lost timing fails closed. Cache-off/flash writes and sleep are unsupported
 * while capture runs. Quiesce the owner and stop capture before those operations.
 * Timer sampling gaps of one minimum character or longer latch a failure even
 * on an idle bus; this detects starvation after it happens, not while masked.
 */
class Esp32S3Uart {
public:
    Esp32S3Uart() = default;
    ~Esp32S3Uart(); ///< Owner must be quiescent. A timer that cannot be stopped is fatal.
    Esp32S3Uart(const Esp32S3Uart&) = delete;
    Esp32S3Uart& operator=(const Esp32S3Uart&) = delete;
    /** Pin selection for an external RS485 transceiver with combined DE/RE.
     * Pins must be distinct, valid ESP32-S3 GPIOs. The application must also
     * exclude pins used by its flash, PSRAM, USB console or other peripherals.
     * Transceiver polarity is explicit; disconnected DE is not supported.
     */
    struct Pins { int tx, rx, de; bool activeHigh; };
    /** Configure UART2 at the currently supported 115200 8N1.
     * Invalid pins/baud fail before GPIO changes. This adapter is example
     * support, not part of the framework-independent motor library.
     */
    bool begin(const Pins& pins, uint32_t baud = 115200) noexcept;
    static constexpr unsigned CAPTURE_CAPACITY = 64;
    static constexpr bool CACHE_OFF_SUPPORTED = false;
    bool startCapture(uint32_t periodUs = 20, uint32_t holdUs = 20) noexcept;
    bool stopCapture() noexcept; ///< Idle-only, retryable cleanup; no motor configuration change.
    Rtu::Port port() noexcept;
    uint64_t sample() noexcept;
    bool clear() noexcept; ///< Explicit idle-only host RX/error reset; never transmits.
    struct CaptureStats {
        uint64_t samples = 0, timerCallbacks = 0, busyUs = 0, txEndUs = 0;
        uint32_t maxGapUs = 0, faults = 0, rxErrors = 0;
        uint32_t txWidthUs = 0, maxRxWidthUs = 0, highWater = 0;
        uint32_t sampleGapLimitUs = 0; ///< Timer gaps must be strictly below this limit.
        bool ready = false, failed = false, timer = false;
        bool sampleGapExceeded = false; ///< Sticky cause; statistics reset preserves it.
    };
    CaptureStats stats() const noexcept; ///< Atomic task-context snapshot.
    bool ready() const noexcept { return stats().ready; }
    bool needsRecovery() const noexcept { return stats().failed; }
    uint32_t maxPollGapUs() const noexcept { return stats().maxGapUs; }
    uint32_t captureFaults() const noexcept { return stats().faults; }
    uint32_t rxErrors() const noexcept { return stats().rxErrors; }
    uint32_t txWidthUs() const noexcept { return stats().txWidthUs; }
    uint32_t maxRxWidthUs() const noexcept { return stats().maxRxWidthUs; }
    uint64_t txEndUs() const noexcept { return stats().txEndUs; }
    void resetStats() noexcept;

private:
    static bool direction(void*, bool);
    static Rtu::WriteResult write(void*, const uint8_t*, std::size_t);
    static Rtu::TxState txState(void*, uint64_t, Rtu::TxObservation&);
    static Rtu::ReadState read(void*, uint64_t, Rtu::RxByte&, uint64_t&);
    void fault(bool uartError) noexcept;
    uint64_t captureSample(bool timerCallback) noexcept;
    bool stopTimer() noexcept; // Retains failed cleanup stages for an explicit retry.

    Pins pins_{-1, -1, -1, true};
    Rtu::RxByte pending_[CAPTURE_CAPACITY]; // ISR working set; internal RAM only.
    void* timer_ = nullptr;
    unsigned head_ = 0, count_ = 0;
    uint64_t sampled_ = 0, emptySince_ = 0, idleThrough_ = 0;
    uint64_t txBusyAt_ = 0, txEnd_ = 0;
    uint64_t directionAt_ = 0;
    uint64_t releasedAt_ = 0, samples_ = 0, timerCallbacks_ = 0, busyUs_ = 0;
    uint32_t releaseWidth_ = 0, holdUs_ = 0, highWater_ = 0;
    uint32_t charMin_ = 0, charMax_ = 0, stopGuard_ = 0;
    uint32_t txWidth_ = 0, maxRxWidth_ = 0, maxPollGap_ = 0;
    uint32_t captureFaults_ = 0, rxErrors_ = 0;
    bool ready_ = false, failed_ = false, transmitting_ = false;
    bool txPending_ = false, txIdle_ = true, rxIdle_ = false;
    bool timerEnabled_ = false, timerRunning_ = false;
    bool sampleGapExceeded_ = false;
};
} // namespace MotorControlRSExample
