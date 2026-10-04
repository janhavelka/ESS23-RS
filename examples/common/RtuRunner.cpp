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

bool validEnd(const TxObservation& tx, uint64_t nowUs, uint64_t queuedUs,
              bool accepted) noexcept {
    return tx.uncertaintyUs <= tx.endedUs && tx.endedUs <= nowUs &&
           (!accepted || tx.endedUs - tx.uncertaintyUs >= queuedUs);
}

Reason checkTx(const TxObservation& tx, uint64_t nowUs, uint64_t queuedUs,
               uint64_t assertedUs, bool accepted, uint32_t holdUs) noexcept {
    if (!validEnd(tx, nowUs, queuedUs, accepted)) return Reason::CLOCK_ERROR;
    if (!tx.released) return Reason::NONE;
    if (tx.releaseUncertaintyUs > tx.releasedUs || tx.releasedUs > nowUs ||
        tx.releasedUs - tx.releaseUncertaintyUs < assertedUs) return Reason::CLOCK_ERROR;
    const uint64_t end = accepted ? tx.endedUs : queuedUs;
    const uint32_t width = accepted ? tx.uncertaintyUs : 0;
    if (!elapsed(tx.releasedUs - tx.releaseUncertaintyUs, end, holdUs)) {
        return elapsed(tx.releasedUs, end - width, holdUs) ?
            Reason::TIMING_UNCERTAIN : Reason::DIRECTION_ERROR;
    }
    return Reason::NONE;
}

Reason txDeadline(const TxObservation& tx, uint64_t nowUs,
                  uint64_t assertedUs, uint32_t timeoutUs) noexcept {
    // Owner scheduling delay is distinct from a transmitter/DE deadline failure.
    const uint64_t latest = tx.released ? tx.releasedUs : nowUs;
    if (!elapsed(latest, assertedUs, timeoutUs)) return Reason::NONE;
    if (tx.released && !elapsed(tx.releasedUs - tx.releaseUncertaintyUs,
                               assertedUs, timeoutUs)) return Reason::TIMING_UNCERTAIN;
    return Reason::TX_TIMEOUT;
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

bool Runner::setTrafficCapture(MotorControlRS::TrafficCapture* capture) noexcept {
    if (busy()) return false;
    traffic_ = capture;
    return true;
}

void Runner::record(Event event, uint64_t atUs, uint8_t byte, uint16_t count,
                    uint64_t wireStartUs, uint32_t uncertaintyUs) noexcept {
    if (traffic_) {
        using MotorControlRS::TrafficKind;
        if (event == Event::START) traffic_->begin(atUs);
        else if (event == Event::TX_DONE) traffic_->event(TrafficKind::TX_END, atUs, count, uncertaintyUs);
        else if (event == Event::DIRECTION) traffic_->event(TrafficKind::DIRECTION, atUs, byte, uncertaintyUs);
        else if (event == Event::END) {
            traffic_->flushReceived(result_.reason == Reason::FRAME, atUs, result_.rxLength);
            traffic_->event(TrafficKind::END, atUs, static_cast<uint16_t>(result_.reason));
        }
    }
    if (!storage_.traceCapacity) return;
    Trace& trace = storage_.trace[traceHead_];
    trace.atUs = atUs;
    trace.wireStartUs = wireStartUs;
    trace.event = event;
    trace.phase = phase_;
    trace.reason = event == Event::CLOCK_ERROR ? Reason::CLOCK_ERROR : result_.reason;
    trace.byte = byte;
    trace.count = count;
    trace.uncertaintyUs = uncertaintyUs;
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
            reason == Reason::CAPTURE_TIMEOUT || reason == Reason::REQUEST_DEADLINE) increment(stats_.timeouts);
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

bool Runner::accepts(const Request& request) const noexcept {
    return valid() && request.bytes && request.length >= 4 &&
        request.length <= storage_.txCapacity && request.replyLength >= 5 &&
        request.replyLength <= storage_.rxCapacity && request.responseTimeoutUs &&
        request.bytes[0] >= 1 && request.bytes[0] <= 247 &&
        request.bytes[1] != 0 && request.bytes[1] < 0x80 &&
        (request.echo == Echo::NONE || request.echo == Echo::REQUIRED);
}

bool Runner::checkClock(uint64_t nowUs) noexcept {
    return valid() && clock(nowUs);
}

bool Runner::storageOverlaps(const void* data, std::size_t bytes) const noexcept {
    return overlaps(data, bytes, storage_.tx, storage_.txCapacity) ||
        overlaps(data, bytes, storage_.rx, storage_.rxCapacity) ||
        overlaps(data, bytes, storage_.trace, storage_.traceCapacity * sizeof(Trace));
}

bool Runner::rejectFrame() noexcept {
    if (phase_ != Phase::DONE || result_.reason != Reason::FRAME) return false;
    return requireRecovery();
}

bool Runner::requireRecovery() noexcept {
    if (busy()) return false;
    phase_ = Phase::FAULT;
    return true;
}

DrainResult Runner::discard(uint64_t nowUs) noexcept {
    DrainResult output;
    if (!valid() || !clock(nowUs)) {
        output.state = ReadState::ERROR; output.reason = Reason::CLOCK_ERROR; return output;
    }
    if (!needsRecovery() || de_) return output;
    for (unsigned i = 0; i < READ_BUDGET; ++i) {
        RxByte byte; uint64_t through = 0;
        const ReadState state = port_.read(port_.context, nowUs, byte, through);
        if (traffic_ && state == ReadState::BYTE)
            traffic_->received(byte.value, byte.startUs, byte.endUs, byte.uncertaintyUs, nowUs);
        if (state == ReadState::PENDING) return output;
        if (state == ReadState::EMPTY && through <= nowUs &&
            (!observed_ || through >= observedUs_) && (!haveRx_ || through >= lastRxEndUs_)) {
            observed_ = true;
            observedUs_ = through;
            output.state = state;
            output.throughUs = through;
            if (traffic_) traffic_->flushReceived(false, nowUs);
            return output;
        }
        if (state == ReadState::BYTE && byte.startUs < byte.endUs && byte.endUs <= nowUs &&
            byte.uncertaintyUs <= byte.endUs &&
            byte.startUs <= std::numeric_limits<uint64_t>::max() - byte.uncertaintyUs &&
            (!observed_ || byte.startUs + byte.uncertaintyUs >= observedUs_) &&
            (!haveRx_ || (byte.startUs + byte.uncertaintyUs >= lastRxEndUs_ - rxUncertaintyUs_ &&
                         byte.endUs > lastRxEndUs_ - rxUncertaintyUs_))) {
            haveRx_ = true;
            lastRxEndUs_ = byte.endUs;
            rxUncertaintyUs_ = byte.uncertaintyUs;
            output.lastByteUs = byte.endUs;
            increment(stats_.rxBytes); increment(stats_.discarded);
            record(Event::DISCARD, byte.endUs, byte.value);
            continue;
        }
        output.state = ReadState::ERROR;
        output.reason = state == ReadState::EMPTY || state == ReadState::BYTE ?
            Reason::CLOCK_ERROR : Reason::RX_ERROR;
        if (traffic_) traffic_->flushReceived(false, nowUs);
        return output;
    }
    return output; // Budget exhaustion is not EMPTY evidence.
}

bool Runner::expired(uint64_t nowUs) const noexcept {
    return deadlineUs_ && nowUs >= deadlineUs_;
}

Admission Runner::start(const Request& request, uint64_t nowUs) noexcept {
    if (needsRecovery()) return Admission::RECOVERY_REQUIRED;
    if (busy()) return Admission::BUSY;
    if (!accepts(request) || (request.deadlineUs && nowUs >= request.deadlineUs)) return Admission::INVALID;
    if (!clock(nowUs)) return Admission::RECOVERY_REQUIRED;
    // Input may be this same TX buffer or a prior RX payload; no pointer retained.
    std::memmove(storage_.tx, request.bytes, request.length);
    result_ = Result();
    result_.startedUs = nowUs;
    txLength_ = request.length;
    replyLength_ = request.replyLength;
    responseTimeoutUs_ = request.responseTimeoutUs;
    deadlineUs_ = request.deadlineUs;
    cancellationUs_ = 0;
    replyGapUs_ = request.replyGapUs ? request.replyGapUs : timing_.gap35Us;
    echo_ = request.echo;
    pending_ = Reason::NONE;
    quietSince_ = nowUs;
    observed_ = haveRx_ = false;
    observedUs_ = lastRxEndUs_ = txEndUs_ = releasedUs_ = queuedUs_ = 0;
    txUncertaintyUs_ = rxUncertaintyUs_ = releaseUncertaintyUs_ = 0;
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

void Runner::closure(uint64_t latestUs, uint32_t uncertaintyUs, bool qualified) noexcept {
    result_.closureLatestUs = latestUs;
    result_.closureEarliestUs = latestUs - uncertaintyUs;
    result_.closureQualified = qualified;
}

bool Runner::requestDeadlineFirst() const noexcept {
    // Compare without adding a relative timeout near the clock's upper bound.
    return deadlineUs_ && (deadlineUs_ <= txEndUs_ ||
                          deadlineUs_ - txEndUs_ <= responseTimeoutUs_);
}

bool Runner::cancellationFirst() const noexcept {
    // Compare latest cutoffs without overflowing the TX-relative deadline.
    // Equality belongs to expiry; an earlier accepted cancellation is immutable.
    return cancellationUs_ && (!deadlineUs_ || cancellationUs_ < deadlineUs_) &&
        (cancellationUs_ <= txEndUs_ || cancellationUs_ - txEndUs_ < responseTimeoutUs_);
}

Reason Runner::closureDeadline(uint64_t latestUs, uint32_t uncertaintyUs) const noexcept {
    const uint64_t earliestUs = latestUs - uncertaintyUs;
    if (cancellationFirst() && latestUs > cancellationUs_)
        return earliestUs > cancellationUs_ ? Reason::CANCELLED : Reason::TIMING_UNCERTAIN;
    const bool responseLate = latestUs - (txEndUs_ - txUncertaintyUs_) > responseTimeoutUs_;
    if (deadlineUs_ && latestUs > deadlineUs_ && (requestDeadlineFirst() || !responseLate))
        return earliestUs > deadlineUs_ ? Reason::REQUEST_DEADLINE : Reason::TIMING_UNCERTAIN;
    if (responseLate)
        return earliestUs > txEndUs_ && earliestUs - txEndUs_ > responseTimeoutUs_ ?
            Reason::PARTIAL_RESPONSE : Reason::TIMING_UNCERTAIN;
    if (cancellationUs_ && latestUs > cancellationUs_)
        return earliestUs > cancellationUs_ ? Reason::CANCELLED : Reason::TIMING_UNCERTAIN;
    return Reason::NONE;
}

void Runner::completedFrame(uint64_t latestUs, uint32_t uncertaintyUs) noexcept {
    closure(latestUs, uncertaintyUs, true);
    const Reason deadline = closureDeadline(latestUs, uncertaintyUs);
    if (deadline != Reason::NONE) finish(deadline, now_, true);
    else frame(latestUs);
}

bool Runner::onByte(const RxByte& byte, uint64_t nowUs) noexcept {
    if (traffic_ && haveRx_ && byte.startUs >= lastRxEndUs_ &&
        byte.startUs - lastRxEndUs_ >= timing_.gap35Us) {
        // The next-frame byte may qualify the preceding frame's closure. Let
        // the unchanged parser publish that result before copying this byte;
        // otherwise the valid preceding RX copy would be labelled incomplete.
        const bool accepted = processByte(byte, nowUs);
        traffic_->flushReceived(false, nowUs);
        traffic_->received(byte.value, byte.startUs, byte.endUs, byte.uncertaintyUs, nowUs);
        traffic_->flushReceived(false, nowUs); // Retain even a rejected next-frame byte.
        return accepted;
    }
    if (traffic_) traffic_->received(byte.value, byte.startUs, byte.endUs, byte.uncertaintyUs, nowUs);
    return processByte(byte, nowUs);
}

bool Runner::processByte(const RxByte& byte, uint64_t nowUs) noexcept {
    // Retain every observed byte in the optional trace, including rejected bytes.
    record(Event::RX, byte.endUs, byte.value, result_.rxLength, byte.startUs,
           byte.uncertaintyUs);
    if (byte.startUs >= byte.endUs || byte.endUs > nowUs ||
        byte.endUs > std::numeric_limits<uint64_t>::max() - timing_.gap35Us ||
        byte.uncertaintyUs > byte.endUs ||
        byte.startUs > std::numeric_limits<uint64_t>::max() - byte.uncertaintyUs) {
        finish(Reason::CLOCK_ERROR, nowUs, true);
        return false;
    }
    const uint64_t startLatest = byte.startUs + byte.uncertaintyUs;
    const uint64_t endEarliest = byte.endUs - byte.uncertaintyUs;
    const uint64_t previousEarliest = lastRxEndUs_ - rxUncertaintyUs_;
    if ((observed_ && startLatest < observedUs_) ||
        (haveRx_ && (startLatest < previousEarliest || byte.endUs <= previousEarliest))) {
        finish(Reason::CLOCK_ERROR, nowUs, true);
        return false;
    }
    const uint64_t previousEnd = lastRxEndUs_;
    const uint32_t previousUncertainty = rxUncertaintyUs_;
    lastRxEndUs_ = byte.endUs;
    rxUncertaintyUs_ = byte.uncertaintyUs;
    haveRx_ = true;
    increment(stats_.rxBytes);
    if (phase_ == Phase::WAIT_BUS) {
        if (byte.endUs > quietSince_) quietSince_ = byte.endUs;
        increment(stats_.discarded);
        record(Event::DISCARD, byte.endUs, byte.value);
        return true;
    }

    if (!result_.firstRxStartUs) result_.firstRxStartUs = byte.startUs;
    if (byte.uncertaintyUs > result_.maxRxUncertaintyUs) result_.maxRxUncertaintyUs = byte.uncertaintyUs;

    // Local echo is admitted only in the known transmit interval. Timestamped
    // echo buffered until after DE release still qualifies; a real FC06 reply does not.
    if (echo_ == Echo::REQUIRED && result_.echoBytes < txLength_) {
        if (!result_.txAccepted || startLatest < queuedUs_ ||
            (result_.txComplete && endEarliest > txEndUs_) ||
            byte.value != storage_.tx[result_.echoBytes]) {
            finish(Reason::ECHO_ERROR, nowUs, true);
            return false;
        }
        if (byte.startUs < queuedUs_ || (result_.txComplete &&
            byte.endUs > txEndUs_ - txUncertaintyUs_)) {
            finish(Reason::TIMING_UNCERTAIN, nowUs, true);
            return false;
        }
        ++result_.echoBytes;
        increment(stats_.echoBytes);
        record(Event::ECHO, byte.endUs, byte.value, result_.echoBytes);
        if (traffic_ && result_.echoBytes == txLength_) traffic_->flushReceived(false, nowUs);
        return true;
    }
    if (phase_ != Phase::RECEIVE || startLatest < releasedUs_ - releaseUncertaintyUs_ ||
        !elapsed(startLatest, txEndUs_ - txUncertaintyUs_, replyGapUs_)) {
        finish(Reason::EARLY_REPLY, nowUs, true);
        return false;
    }
    if (byte.startUs < releasedUs_ || !elapsed(byte.startUs, txEndUs_, replyGapUs_)) {
        finish(Reason::TIMING_UNCERTAIN, nowUs, true);
        return false;
    }

    // A new frame after t3.5 proves the preceding frame ended. Preserve that
    // first frame and record the consumed next-frame byte as discarded evidence.
    if (result_.rxLength && elapsed(byte.startUs, previousEnd, timing_.gap35Us)) {
        increment(stats_.discarded);
        record(Event::DISCARD, byte.endUs, byte.value);
        completedFrame(previousEnd + timing_.gap35Us, previousUncertainty);
        return false;
    }
    if ((cancellationUs_ && byte.endUs > cancellationUs_) ||
        (deadlineUs_ && byte.endUs > deadlineUs_) ||
        byte.endUs - (txEndUs_ - txUncertaintyUs_) > responseTimeoutUs_) {
        closure(byte.endUs + timing_.gap35Us, byte.uncertaintyUs, false);
        const Reason deadline = closureDeadline(result_.closureLatestUs, byte.uncertaintyUs);
        finish(deadline == Reason::PARTIAL_RESPONSE && !result_.rxLength ?
               Reason::NO_RESPONSE : deadline, nowUs, true);
        return false;
    }
    if (result_.rxLength && startLatest - previousEarliest > timing_.gap15Us) {
        const bool definitelyInvalid = byte.startUs > previousEnd &&
            byte.startUs - previousEnd > timing_.gap15Us &&
            startLatest - previousEarliest < timing_.gap35Us;
        finish(definitelyInvalid ? Reason::GAP : Reason::TIMING_UNCERTAIN, nowUs, true);
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
        if (state == ReadState::PENDING) {
            uint64_t progress = observed_ ? observedUs_ : result_.startedUs;
            if (haveRx_ && lastRxEndUs_ > progress) progress = lastRxEndUs_;
            if (nowUs - progress > timing_.captureTimeoutUs)
                finish(Reason::CAPTURE_TIMEOUT, nowUs, true);
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
            // Qualified historical closure is useful even when task service is late.
            if (nowUs - through > timing_.captureTimeoutUs &&
                !(phase_ == Phase::RECEIVE && result_.rxLength &&
                  elapsed(through, lastRxEndUs_, timing_.gap35Us))) {
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
    TxObservation tx;
    if (port_.txState(port_.context, nowUs, tx) != TxState::IDLE) return;
    if (!validEnd(tx, nowUs, queuedUs_, result_.txAccepted != 0)) return;
    if (result_.txAccepted && !elapsed(nowUs, tx.endedUs, timing_.holdUs)) return;
    const bool released = tx.released && checkTx(tx, nowUs, queuedUs_, assertedUs_,
        result_.txAccepted != 0, timing_.holdUs) == Reason::NONE;
    // Preserve the original terminal reason even if cleanup itself fails.
    // Bad release evidence does not prevent a fresh, explicit receive-mode action.
    if (released || port_.setTransmit(port_.context, false)) {
        de_ = false;
        record(Event::DIRECTION, released ? tx.releasedUs : nowUs, 0, 0, 0,
               released ? tx.releaseUncertaintyUs : 0);
    }
}

void Runner::poll(uint64_t nowUs) noexcept {
    if (!valid() || !clock(nowUs)) return;
    if (phase_ == Phase::FAULT) { releaseFault(nowUs); return; }
    if (!busy()) return;

    if (phase_ == Phase::WAIT_BUS) {
        if (expired(nowUs)) { finish(Reason::REQUEST_DEADLINE, nowUs, false); return; }
        const bool empty = receive(nowUs);
        if (!busy()) return;
        if (elapsed(nowUs, result_.startedUs, timing_.busTimeoutUs)) {
            finish(Reason::BUS_TIMEOUT, nowUs, true);
            return;
        }
        if (!empty || observedUs_ != nowUs ||
            !elapsed(observedUs_, quietSince_, timing_.gap35Us)) return;
        TxObservation tx;
        const TxState state = port_.txState(port_.context, nowUs, tx);
        if (state == TxState::BUSY) return;
        if (state != TxState::IDLE) { finish(Reason::TX_ERROR, nowUs, true); return; }
        if (!direction(true, nowUs)) return;
        assertedUs_ = nowUs;
        phase(Phase::SETUP, nowUs);
        return;
    }

    if (phase_ == Phase::SETUP) {
        if (expired(nowUs)) {
            if (!direction(false, nowUs)) return;
            finish(Reason::REQUEST_DEADLINE, nowUs, false);
            return;
        }
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
        if (traffic_) traffic_->transmitted(storage_.tx, result_.txAccepted,
            !sent.error && sent.accepted == txLength_, nowUs);
        record(Event::TX, nowUs, 0, result_.txAccepted);
        if (sent.error || sent.accepted != txLength_) pending_ = Reason::TX_ERROR;
        phase(Phase::DRAIN, nowUs);
        return;
    }

    if (phase_ == Phase::DRAIN || phase_ == Phase::HOLD) {
        TxObservation tx;
        const TxState state = port_.txState(port_.context, nowUs, tx);
        if (state == TxState::ERROR || (state != TxState::IDLE && state != TxState::BUSY)) {
            finish(Reason::TX_ERROR, nowUs, true);
            return;
        }
        if (state == TxState::IDLE) {
            const Reason invalid = checkTx(tx, nowUs, queuedUs_, assertedUs_,
                                           result_.txAccepted != 0, timing_.holdUs);
            if (invalid != Reason::NONE) {
                finish(invalid, nowUs, true);
                return;
            }
            const uint64_t end = result_.txAccepted ? tx.endedUs : queuedUs_;
            const uint32_t width = result_.txAccepted ? tx.uncertaintyUs : 0;
            if ((phase_ == Phase::HOLD && (txEndUs_ != end || txUncertaintyUs_ != width)) ||
                (echo_ == Echo::REQUIRED && result_.echoBytes &&
                 lastRxEndUs_ - rxUncertaintyUs_ > tx.endedUs)) {
                finish(Reason::CLOCK_ERROR, nowUs, true);
                return;
            }
            txEndUs_ = end;
            txUncertaintyUs_ = width;
            result_.txEndUs = end;
            result_.txUncertaintyUs = width;
            result_.txComplete = result_.txAccepted == txLength_;
            if (phase_ == Phase::DRAIN)
                record(Event::TX_DONE, txEndUs_, 0, result_.txAccepted, 0, txUncertaintyUs_);
            if (echo_ == Echo::REQUIRED && result_.echoBytes &&
                lastRxEndUs_ > txEndUs_ - txUncertaintyUs_) {
                finish(Reason::TIMING_UNCERTAIN, nowUs, true);
                releaseFault(nowUs);
                return;
            }
            if (phase_ == Phase::DRAIN) phase(Phase::HOLD, nowUs);
        } else {
            tx = TxObservation(); // BUSY supplies no completion/release evidence.
        }
        // Read retained autonomous completion/release before considering task time.
        // Safe DE release remains permitted after expiry; no new enqueue occurs.
        if (deadlineUs_ && ((state == TxState::BUSY && expired(nowUs)) ||
            (state == TxState::IDLE && (tx.released ? tx.releasedUs : nowUs) >= deadlineUs_))) {
            const uint64_t latest = tx.released ? tx.releasedUs : nowUs;
            const uint32_t width = tx.released ? tx.releaseUncertaintyUs : 0;
            finish(latest - width < deadlineUs_ ? Reason::TIMING_UNCERTAIN :
                   Reason::REQUEST_DEADLINE, nowUs, true);
            releaseFault(nowUs);
            return;
        }
        const Reason deadline = txDeadline(tx, nowUs, assertedUs_, timing_.txTimeoutUs);
        if (deadline != Reason::NONE) {
            finish(deadline, nowUs, true);
            releaseFault(nowUs);
            return;
        }
        if (state == TxState::BUSY && phase_ == Phase::HOLD) return;
        if (phase_ == Phase::DRAIN) {
            receive(nowUs);
            if (!busy()) releaseFault(nowUs);
            return;
        }
        if (!elapsed(nowUs, txEndUs_, timing_.holdUs)) {
            receive(nowUs);
            return;
        }
        if (tx.released) {
            de_ = false;
            releasedUs_ = tx.releasedUs;
            releaseUncertaintyUs_ = tx.releaseUncertaintyUs;
            record(Event::DIRECTION, releasedUs_, 0, 0, 0, releaseUncertaintyUs_);
        } else {
            if (!direction(false, nowUs)) return;
            releasedUs_ = nowUs;
            releaseUncertaintyUs_ = 0;
        }
        if (pending_ != Reason::NONE) { finish(pending_, nowUs, true); return; }
        phase(Phase::RECEIVE, nowUs);
    }

    if (phase_ == Phase::RECEIVE) {
        const bool empty = receive(nowUs);
        if (!busy() || !empty) return;
        const bool requestExpired = deadlineUs_ && observedUs_ >= deadlineUs_;
        const bool responseExpired = elapsed(observedUs_, txEndUs_, responseTimeoutUs_);
        if (result_.rxLength && elapsed(observedUs_, lastRxEndUs_, timing_.gap35Us)) {
            completedFrame(lastRxEndUs_ + timing_.gap35Us, rxUncertaintyUs_);
        } else if (cancellationFirst() && observedUs_ >= cancellationUs_) {
            if (result_.rxLength) closure(lastRxEndUs_ + timing_.gap35Us, rxUncertaintyUs_, false);
            finish(Reason::CANCELLED, nowUs, true);
        } else if (requestExpired || responseExpired) {
            if (result_.rxLength) {
                closure(lastRxEndUs_ + timing_.gap35Us, rxUncertaintyUs_, false);
                finish(closureDeadline(result_.closureLatestUs, rxUncertaintyUs_), nowUs, true);
                return;
            }
            if (requestExpired && (requestDeadlineFirst() || !responseExpired)) {
                finish(Reason::REQUEST_DEADLINE, nowUs, true);
                return;
            }
            const bool missingEcho = echo_ == Echo::REQUIRED && result_.echoBytes != txLength_;
            if (missingEcho) increment(stats_.timeouts);
            finish(missingEcho ? Reason::ECHO_ERROR : Reason::NO_RESPONSE, nowUs, true);
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

void Runner::cancelCaptured(uint64_t nowUs) noexcept {
    if (!valid() || !clock(nowUs) || !busy()) return;
    if (phase_ != Phase::RECEIVE) { cancel(nowUs); return; }
    if (!cancellationUs_) cancellationUs_ = nowUs;
    if (observed_ && observedUs_ >= cancellationUs_) {
        if (result_.rxLength) closure(lastRxEndUs_ + timing_.gap35Us, rxUncertaintyUs_, false);
        finish(Reason::CANCELLED, nowUs, true);
    }
}

bool Runner::configureTiming(const Timing& timing, uint64_t nowUs) noexcept {
    if (!valid() || busy() || needsRecovery() || de_ || !timing.gap15Us ||
        timing.gap35Us <= timing.gap15Us || !timing.busTimeoutUs ||
        timing.busTimeoutUs < timing.gap35Us ||
        timing.txTimeoutUs <= static_cast<uint64_t>(timing.setupUs) + timing.holdUs ||
        !timing.captureTimeoutUs || !clock(nowUs)) return false;
    timing_ = timing;
    return true;
}

bool Runner::recover(uint64_t nowUs) noexcept {
    if (!valid() || !clock(nowUs) || busy()) return false;
    phase_ = Phase::FAULT; // Failed recovery must never reopen admission.
    TxObservation tx;
    if (port_.txState(port_.context, nowUs, tx) != TxState::IDLE) return false;
    if (!validEnd(tx, nowUs, queuedUs_, result_.txAccepted != 0)) return false;
    if (result_.txAccepted && !elapsed(nowUs, tx.endedUs, timing_.holdUs)) return false;
    const bool released = tx.released && checkTx(tx, nowUs, queuedUs_, assertedUs_,
        result_.txAccepted != 0, timing_.holdUs) == Reason::NONE;
    if (!released && !port_.setTransmit(port_.context, false)) { de_ = true; return false; }
    de_ = false;
    record(Event::DIRECTION, released ? tx.releasedUs : nowUs, 0, 0, 0,
           released ? tx.releaseUncertaintyUs : 0);
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
        REASON_NAME(TIMING_UNCERTAIN);
        REASON_NAME(REQUEST_DEADLINE);
#undef REASON_NAME
    }
    return "UNKNOWN";
}

}} // namespace MotorControlRSExample::Rtu
