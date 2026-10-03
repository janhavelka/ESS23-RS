// SPDX-License-Identifier: MIT
#pragma once
#include "RtuRunner.h"

namespace MotorControlRSExample {

/** Exclusive, task-polled UART2 adapter for E2. No HardwareSerial/IDF UART
 * driver may own this peripheral concurrently. ESP32-S3/IDF-5.5 specific.
 * Capture brackets are observations, not exact wire timestamps. The RX
 * character/stop-sampling guard remains an explicit qualification assumption.
 * Service sample() immediately before runner.poll(returned time), without
 * logging/blocking during a transaction. Lost timing fails closed.
 */
class E2Uart {
public:
    E2Uart() = default;
    E2Uart(const E2Uart&) = delete;
    E2Uart& operator=(const E2Uart&) = delete;
    bool begin(uint32_t baud = 115200) noexcept;
    Rtu::Port port() noexcept;
    uint64_t sample() noexcept;
    bool clear() noexcept; ///< Explicit idle-only host RX/error reset; never transmits.
    bool ready() const noexcept { return ready_; }
    bool needsRecovery() const noexcept { return failed_; } ///< Includes faults observed while idle.
    uint32_t maxPollGapUs() const noexcept { return maxPollGap_; }
    uint32_t captureFaults() const noexcept { return captureFaults_; }
    uint32_t rxErrors() const noexcept { return rxErrors_; }
    uint32_t txWidthUs() const noexcept { return txWidth_; }
    uint32_t maxRxWidthUs() const noexcept { return maxRxWidth_; }
    uint64_t txEndUs() const noexcept { return txEnd_; }
    void resetStats() noexcept;

private:
    static bool direction(void*, bool);
    static Rtu::WriteResult write(void*, const uint8_t*, std::size_t);
    static Rtu::TxState txState(void*, uint64_t, uint64_t&);
    static uint32_t txWidth(void*);
    static Rtu::ReadState read(void*, uint64_t, Rtu::RxByte&, uint64_t&);
    void fault(bool uartError) noexcept;

    Rtu::RxByte pending_[4]; // Small, internal capture working set; task-owned.
    unsigned head_ = 0, count_ = 0;
    uint64_t sampled_ = 0, emptySince_ = 0, idleThrough_ = 0;
    uint64_t txBusyAt_ = 0, txEnd_ = 0;
    uint64_t directionAt_ = 0;
    uint32_t charMin_ = 0, charMax_ = 0, stopGuard_ = 0;
    uint32_t txWidth_ = 0, maxRxWidth_ = 0, maxPollGap_ = 0;
    uint32_t captureFaults_ = 0, rxErrors_ = 0;
    bool ready_ = false, failed_ = false, transmitting_ = false;
    bool txPending_ = false, txIdle_ = true, rxIdle_ = false;
};
} // namespace MotorControlRSExample
