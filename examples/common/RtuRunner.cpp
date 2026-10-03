// SPDX-License-Identifier: MIT
#include "RtuRunner.h"

#include <cstring>
#include <limits>

namespace MotorControlRSExample { namespace Rtu {
namespace {
void increment(uint32_t& value) noexcept {
    if (value != std::numeric_limits<uint32_t>::max()) ++value;
}

bool elapsed(uint64_t now, uint64_t since, uint32_t duration) noexcept {
    return now >= since && now - since >= duration;
}

bool overlaps(const void* left, std::size_t leftSize,
              const void* right, std::size_t rightSize) noexcept {
    if (!leftSize || !rightSize) return false;
    const uintptr_t a = reinterpret_cast<uintptr_t>(left);
    const uintptr_t b = reinterpret_cast<uintptr_t>(right);
    return a <= b ? b - a < leftSize : a - b < rightSize;
}
} // namespace

bool setRtuTiming(uint32_t baud, uint8_t bits, Timing& output) noexcept {
    if (!baud || (bits != 10 && bits != 11)) return false;
    const uint64_t divisor = static_cast<uint64_t>(baud) * 2;
    const uint32_t gap15 = baud > 19200 ? 750 :
        static_cast<uint32_t>((static_cast<uint64_t>(bits) * 3000000 + divisor - 1) / divisor);
    const uint32_t gap35 = baud > 19200 ? 1750 :
        static_cast<uint32_t>((static_cast<uint64_t>(bits) * 7000000 + divisor - 1) / divisor);
    output.gap15Us = gap15;
    output.gap35Us = gap35;
    return true;
}

Runner::Runner(const Port& port, const Storage& storage, const Timing& timing) noexcept
    : port_(port), storage_(storage), timing_(timing) {}

bool Runner::valid() const noexcept {
    if (!port_.setTransmit || !port_.write || !port_.txState || !port_.read ||
        !storage_.tx || !storage_.rx || !storage_.txCapacity || !storage_.rxCapacity ||
        storage_.txCapacity > MAX_FRAME || storage_.rxCapacity > MAX_FRAME ||
        storage_.traceCapacity > 65535 || (!storage_.trace && storage_.traceCapacity) ||
        !timing_.gap15Us || timing_.gap35Us <= timing_.gap15Us || !timing_.captureTimeoutUs ||
        timing_.busTimeoutUs < timing_.gap35Us ||
        timing_.txTimeoutUs <= static_cast<uint64_t>(timing_.setupUs) + timing_.holdUs) return false;
    const std::size_t traceBytes = storage_.traceCapacity * sizeof(Trace);
    return !overlaps(storage_.tx, storage_.txCapacity, storage_.rx, storage_.rxCapacity) &&
           !overlaps(storage_.tx, storage_.txCapacity, storage_.trace, traceBytes) &&
           !overlaps(storage_.rx, storage_.rxCapacity, storage_.trace, traceBytes);
}

bool Runner::busy() const noexcept {
    return phase_ != Phase::IDLE && phase_ != Phase::DONE && phase_ != Phase::FAULT;
}

void Runner::record(Event event, uint64_t atUs, uint8_t byte, uint16_t count,
                    uint64_t wireStartUs) noexcept {
    if (!storage_.traceCapacity) return;
    Trace& trace = storage_.trace[traceHead_];
    trace.atUs = atUs;
    trace.wireStartUs = wireStartUs;
    trace.event = event;
    trace.phase = phase_;
    trace.reason = event == Event::CLOCK_ERROR ? Reason::CLOCK_ERROR : result_.reason;
    trace.byte = byte;
    trace.count = count;
    traceHead_ = (traceHead_ + 1) % storage_.traceCapacity;
    if (traceSize_ < storage_.traceCapacity) ++traceSize_;
    else increment(stats_.traceOverwritten);
}

const Trace* Runner::traceAt(std::size_t index) const noexcept {
    if (index >= traceSize_) return nullptr;
    const std::size_t first = (traceHead_ + storage_.traceCapacity - traceSize_) % storage_.traceCapacity;
    return &storage_.trace[(first + index) % storage_.traceCapacity];
}

void Runner::phase(Phase next, uint64_t nowUs) noexcept {
    phase_ = next;
    record(Event::PHASE, nowUs);
}

void Runner::finish(Reason reason, uint64_t nowUs, bool fault) noexcept {
    result_.reason = reason;
    result_.endedUs = nowUs;
    if (reason == Reason::FRAME) increment(stats_.frames);
    else {
        increment(stats_.failed);
        if (reason == Reason::CANCELLED) increment(stats_.cancelled);
        if (reason == Reason::NO_RESPONSE || reason == Reason::PARTIAL_RESPONSE ||
            reason == Reason::TX_TIMEOUT || reason == Reason::BUS_TIMEOUT ||
            reason == Reason::CAPTURE_TIMEOUT) increment(stats_.timeouts);
    }
    phase_ = fault ? Phase::FAULT : Phase::DONE;
    record(Event::END, nowUs, 0, result_.rxLength);
}

bool Runner::clock(uint64_t nowUs) noexcept {
    if (clockSet_ && nowUs < now_) {
        if (busy()) finish(Reason::CLOCK_ERROR, now_, true);
        else {
            // An invalid later call must not erase a retained transaction outcome.
            phase_ = Phase::FAULT;
            record(Event::CLOCK_ERROR, now_);
        }
        return false;
    }
    clockSet_ = true;
    now_ = nowUs;
    return true;
}

bool Runner::direction(bool enabled, uint64_t nowUs) noexcept {
    if (!port_.setTransmit(port_.context, enabled)) {
        de_ = true; // Direction is uncertain; never claim receive mode after failure.
        finish(Reason::DIRECTION_ERROR, nowUs, true);
        return false;
    }
    de_ = enabled;
    record(Event::DIRECTION, nowUs, enabled ? 1 : 0);
    return true;
}

Admission Runner::start(const Request& request, uint64_t nowUs) noexcept {
    if (needsRecovery()) return Admission::RECOVERY_REQUIRED;
    if (busy()) return Admission::BUSY;
    if (!valid() || !request.bytes || request.length < 4 ||
        request.length > storage_.txCapacity || request.replyLength < 5 ||
        request.replyLength > storage_.rxCapacity || !request.responseTimeoutUs ||
        request.bytes[0] < 1 || request.bytes[0] > 247 ||
        request.bytes[1] == 0 || request.bytes[1] >= 0x80 ||
        (request.echo != Echo::NONE && request.echo != Echo::REQUIRED)) return Admission::INVALID;
    if (!clock(nowUs)) return Admission::RECOVERY_REQUIRED;
    // Input may be this same TX buffer or a prior RX payload; no pointer retained.
    std::memmove(storage_.tx, request.bytes, request.length);
    result_ = Result();
    result_.startedUs = nowUs;
    txLength_ = request.length;
    replyLength_ = request.replyLength;
    responseTimeoutUs_ = request.responseTimeoutUs;
    echo_ = request.echo;
    pending_ = Reason::NONE;
    quietSince_ = nowUs;
    observed_ = haveRx_ = false;
    observedUs_ = lastRxEndUs_ = txEndUs_ = releasedUs_ = queuedUs_ = 0;
    increment(stats_.started);
    phase_ = Phase::WAIT_BUS;
    record(Event::START, nowUs, 0, static_cast<uint16_t>(txLength_));
    return Admission::STARTED;
}

void Runner::frame(uint64_t nowUs) noexcept {
    if (echo_ == Echo::REQUIRED && result_.echoBytes != txLength_) {
        finish(Reason::ECHO_ERROR, nowUs, true);
        return;
    }
    const std::size_t expected = result_.rxLength >= 2 &&
        storage_.rx[1] == static_cast<uint8_t>(storage_.tx[1] | 0x80) ? 5 : replyLength_;
    finish(result_.rxLength == expected ? Reason::FRAME : Reason::LENGTH,
           nowUs, result_.rxLength != expected);
}

bool Runner::onByte(const RxByte& byte, uint64_t nowUs) noexcept {
    // Retain every observed byte in the optional trace, including rejected bytes.
    record(Event::RX, byte.endUs, byte.value, result_.rxLength, byte.startUs);
    if (byte.startUs >= byte.endUs || byte.endUs > nowUs ||
        (observed_ && byte.startUs < observedUs_) ||
        (haveRx_ && byte.startUs < lastRxEndUs_)) {
        finish(Reason::CLOCK_ERROR, nowUs, true);
        return false;
    }
    const uint64_t previousEnd = lastRxEndUs_;
    lastRxEndUs_ = byte.endUs;
    haveRx_ = true;
    increment(stats_.rxBytes);
    if (phase_ == Phase::WAIT_BUS) {
        if (byte.endUs > quietSince_) quietSince_ = byte.endUs;
        increment(stats_.discarded);
        record(Event::DISCARD, byte.endUs, byte.value);
        return true;
    }

    // Local echo is admitted only in the known transmit interval. Timestamped
    // echo buffered until after DE release still qualifies; a real FC06 reply does not.
    if (echo_ == Echo::REQUIRED && result_.echoBytes < txLength_) {
        if (!result_.txAccepted || byte.startUs < queuedUs_ ||
            (result_.txComplete && byte.endUs > txEndUs_) ||
            byte.value != storage_.tx[result_.echoBytes]) {
            finish(Reason::ECHO_ERROR, nowUs, true);
            return false;
        }
        ++result_.echoBytes;
        increment(stats_.echoBytes);
        record(Event::ECHO, byte.endUs, byte.value, result_.echoBytes);
        return true;
    }
    if (phase_ != Phase::RECEIVE || byte.startUs < releasedUs_ ||
        !elapsed(byte.startUs, txEndUs_, timing_.gap35Us)) {
        finish(Reason::EARLY_REPLY, nowUs, true);
        return false;
    }

    // A new frame after t3.5 proves the preceding frame ended. Preserve that
    // first frame and record the consumed next-frame byte as discarded evidence.
    if (result_.rxLength && elapsed(byte.startUs, previousEnd, timing_.gap35Us)) {
        increment(stats_.discarded);
        record(Event::DISCARD, byte.endUs, byte.value);
        const uint64_t completed = previousEnd + timing_.gap35Us;
        if (completed - txEndUs_ <= responseTimeoutUs_) frame(completed);
        else finish(Reason::PARTIAL_RESPONSE, nowUs, true);
        return false;
    }
    if (byte.endUs - txEndUs_ > responseTimeoutUs_) {
        finish(result_.rxLength ? Reason::PARTIAL_RESPONSE : Reason::NO_RESPONSE, nowUs, true);
        return false;
    }
    if (result_.rxLength && byte.startUs - previousEnd > timing_.gap15Us) {
        finish(Reason::GAP, nowUs, true);
        return false;
    }
    if (result_.rxLength == storage_.rxCapacity) {
        result_.rxTruncated = true;
        finish(Reason::RX_OVERFLOW, nowUs, true);
        return false;
    }
    storage_.rx[result_.rxLength++] = byte.value;
    return true;
}

bool Runner::receive(uint64_t nowUs) noexcept {
    for (unsigned i = 0; i < READ_BUDGET; ++i) {
        RxByte byte;
        uint64_t through = 0;
        const ReadState state = port_.read(port_.context, nowUs, byte, through);
        if (state == ReadState::ERROR) {
            finish(Reason::RX_ERROR, nowUs, true);
            return false;
        }
        if (state == ReadState::EMPTY) {
            if (through > nowUs || (observed_ && through < observedUs_) ||
                (haveRx_ && through < lastRxEndUs_)) {
                finish(Reason::CLOCK_ERROR, nowUs, true);
                return false;
            }
            observed_ = true;
            observedUs_ = through;
            if (nowUs - through > timing_.captureTimeoutUs) {
                finish(Reason::CAPTURE_TIMEOUT, nowUs, true);
                return false;
            }
            return true;
        }
        if (state != ReadState::BYTE || !onByte(byte, nowUs)) {
            if (busy()) finish(Reason::RX_ERROR, nowUs, true);
            return false;
        }
    }
    return false; // More queued history may exist; never infer silence from a budget limit.
}

void Runner::releaseFault(uint64_t nowUs) noexcept {
    if (!de_) return;
    uint64_t ended = 0;
    if (port_.txState(port_.context, nowUs, ended) != TxState::IDLE) return;
    if (result_.txAccepted && (ended < queuedUs_ || ended > nowUs ||
        !elapsed(nowUs, ended, timing_.holdUs))) return;
    // Preserve the original terminal reason even if cleanup itself fails.
    if (port_.setTransmit(port_.context, false)) {
        de_ = false;
        record(Event::DIRECTION, nowUs, 0);
    }
}

void Runner::poll(uint64_t nowUs) noexcept {
    if (!valid() || !clock(nowUs)) return;
    if (phase_ == Phase::FAULT) { releaseFault(nowUs); return; }
    if (!busy()) return;

    if (phase_ == Phase::WAIT_BUS) {
        const bool empty = receive(nowUs);
        if (!busy()) return;
        if (elapsed(nowUs, result_.startedUs, timing_.busTimeoutUs)) {
            finish(Reason::BUS_TIMEOUT, nowUs, true);
            return;
        }
        if (!empty || observedUs_ != nowUs ||
            !elapsed(observedUs_, quietSince_, timing_.gap35Us)) return;
        uint64_t ended = 0;
        const TxState state = port_.txState(port_.context, nowUs, ended);
        if (state == TxState::BUSY) return;
        if (state != TxState::IDLE) { finish(Reason::TX_ERROR, nowUs, true); return; }
        if (!direction(true, nowUs)) return;
        assertedUs_ = nowUs;
        phase(Phase::SETUP, nowUs);
        return;
    }

    if (phase_ == Phase::SETUP) {
        const bool empty = receive(nowUs);
        if (!busy()) { releaseFault(nowUs); return; }
        if (elapsed(nowUs, assertedUs_, timing_.txTimeoutUs)) {
            finish(Reason::TX_TIMEOUT, nowUs, true);
            releaseFault(nowUs);
            return;
        }
        if (!empty || observedUs_ != nowUs ||
            !elapsed(nowUs, assertedUs_, timing_.setupUs)) return;
        queuedUs_ = nowUs;
        const WriteResult sent = port_.write(port_.context, storage_.tx, txLength_);
        result_.txAccepted = static_cast<uint16_t>(sent.accepted <= txLength_ ? sent.accepted : txLength_);
        record(Event::TX, nowUs, 0, result_.txAccepted);
        if (sent.error || sent.accepted != txLength_) pending_ = Reason::TX_ERROR;
        phase(Phase::DRAIN, nowUs);
        return;
    }

    if (phase_ == Phase::DRAIN) {
        uint64_t ended = 0;
        const TxState state = port_.txState(port_.context, nowUs, ended);
        if (state == TxState::ERROR || (state != TxState::IDLE && state != TxState::BUSY)) {
            finish(Reason::TX_ERROR, nowUs, true);
            return;
        }
        if (state == TxState::IDLE) {
            if ((result_.txAccepted && ended < queuedUs_) || ended > nowUs ||
                (echo_ == Echo::REQUIRED && result_.echoBytes && lastRxEndUs_ > ended)) {
                finish(Reason::CLOCK_ERROR, nowUs, true);
                return;
            }
            txEndUs_ = result_.txAccepted ? ended : queuedUs_;
            result_.txComplete = result_.txAccepted == txLength_;
            record(Event::TX_DONE, txEndUs_, 0, result_.txAccepted);
            phase(Phase::HOLD, nowUs);
        }
        if (elapsed(nowUs, assertedUs_, timing_.txTimeoutUs)) {
            finish(Reason::TX_TIMEOUT, nowUs, true);
            releaseFault(nowUs);
            return;
        }
        if (phase_ == Phase::DRAIN) {
            receive(nowUs);
            if (!busy()) releaseFault(nowUs);
            return;
        }
    }

    if (phase_ == Phase::HOLD) {
        if (elapsed(nowUs, assertedUs_, timing_.txTimeoutUs)) {
            finish(Reason::TX_TIMEOUT, nowUs, true);
            releaseFault(nowUs);
            return;
        }
        if (!elapsed(nowUs, txEndUs_, timing_.holdUs)) {
            receive(nowUs);
            return;
        }
        if (!direction(false, nowUs)) return;
        releasedUs_ = nowUs;
        if (pending_ != Reason::NONE) { finish(pending_, nowUs, true); return; }
        phase(Phase::RECEIVE, nowUs);
    }

    if (phase_ == Phase::RECEIVE) {
        const bool empty = receive(nowUs);
        if (!busy() || !empty) return;
        if (result_.rxLength && elapsed(observedUs_, lastRxEndUs_, timing_.gap35Us) &&
            lastRxEndUs_ - txEndUs_ <= responseTimeoutUs_ &&
            timing_.gap35Us <= responseTimeoutUs_ - (lastRxEndUs_ - txEndUs_)) {
            frame(lastRxEndUs_ + timing_.gap35Us);
        } else if (elapsed(observedUs_, txEndUs_, responseTimeoutUs_)) {
            const bool missingEcho = echo_ == Echo::REQUIRED && result_.echoBytes != txLength_;
            if (missingEcho) increment(stats_.timeouts);
            finish(missingEcho ? Reason::ECHO_ERROR :
                   result_.rxLength ? Reason::PARTIAL_RESPONSE : Reason::NO_RESPONSE, nowUs, true);
        }
    }
}

void Runner::cancel(uint64_t nowUs) noexcept {
    if (!valid() || !clock(nowUs) || !busy()) return;
    if (phase_ == Phase::WAIT_BUS || phase_ == Phase::SETUP) {
        if (de_ && !direction(false, nowUs)) return;
        finish(Reason::CANCELLED, nowUs, false);
    } else if (phase_ == Phase::DRAIN || phase_ == Phase::HOLD) {
        if (pending_ == Reason::NONE) pending_ = Reason::CANCELLED;
        // Drain safely; never truncate an active UART frame or erase an earlier error.
    } else finish(Reason::CANCELLED, nowUs, true);
}

bool Runner::recover(uint64_t nowUs) noexcept {
    if (!valid() || !clock(nowUs) || busy()) return false;
    phase_ = Phase::FAULT; // Failed recovery must never reopen admission.
    uint64_t ended = 0;
    if (port_.txState(port_.context, nowUs, ended) != TxState::IDLE) return false;
    if (result_.txAccepted && (ended < queuedUs_ || ended > nowUs ||
        !elapsed(nowUs, ended, timing_.holdUs))) return false;
    if (!port_.setTransmit(port_.context, false)) { de_ = true; return false; }
    de_ = false;
    record(Event::DIRECTION, nowUs, 0);
    phase_ = Phase::IDLE;
    record(Event::RECOVER, nowUs);
    return true; // start() still establishes a fresh, fully observed bus-idle interval.
}

const char* phaseName(Phase value) noexcept {
    switch (value) {
#define PHASE_NAME(name) case Phase::name: return #name
        PHASE_NAME(IDLE); PHASE_NAME(WAIT_BUS); PHASE_NAME(SETUP); PHASE_NAME(DRAIN);
        PHASE_NAME(HOLD); PHASE_NAME(RECEIVE); PHASE_NAME(DONE); PHASE_NAME(FAULT);
#undef PHASE_NAME
    }
    return "UNKNOWN";
}

const char* reasonName(Reason value) noexcept {
    switch (value) {
#define REASON_NAME(name) case Reason::name: return #name
        REASON_NAME(NONE); REASON_NAME(FRAME); REASON_NAME(NO_RESPONSE); REASON_NAME(PARTIAL_RESPONSE);
        REASON_NAME(LENGTH); REASON_NAME(GAP); REASON_NAME(EARLY_REPLY); REASON_NAME(RX_OVERFLOW);
        REASON_NAME(RX_ERROR); REASON_NAME(TX_ERROR); REASON_NAME(TX_TIMEOUT); REASON_NAME(BUS_TIMEOUT);
        REASON_NAME(DIRECTION_ERROR); REASON_NAME(ECHO_ERROR); REASON_NAME(CANCELLED); REASON_NAME(CLOCK_ERROR);
        REASON_NAME(CAPTURE_TIMEOUT);
#undef REASON_NAME
    }
    return "UNKNOWN";
}

}} // namespace MotorControlRSExample::Rtu
