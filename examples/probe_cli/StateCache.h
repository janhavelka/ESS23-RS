// SPDX-License-Identifier: MIT
#pragma once
#include <MotorControlRS/profiles/ess_rs/Reads.h>

namespace MotorControlRSExample { namespace Probe {

/** Application-owned observations, separate from the outcome of an operation.
 * A failed attempt never replaces a checked value. All calls share the bus
 * owner's cooperative context. This cache performs no I/O or clock reads. */
struct StateCache {
    struct Block {
        MotorControlRS::ESS_RS::StateObservation value;
        MotorControlRS::ReadTarget lastAttemptTarget;
        uint32_t lastAttemptOperationId = 0;
        bool valid = false, attemptKnown = false, lastAttemptOk = false;
        uint64_t lastAttemptUs = 0, lastSuccessUs = 0;
        uint64_t observedEarliestUs = 0, observedLatestUs = 0, deliveredUs = 0;
        MotorControlRS::Status lastAttemptStatus;
    } blocks[MotorControlRS::ESS_RS::STATE_BLOCK_COUNT];
};

inline bool sameTarget(const MotorControlRS::ReadTarget& a, const MotorControlRS::ReadTarget& b) {
    return a.id == b.id && a.address == b.address && a.generation == b.generation;
}
inline bool current(const StateCache::Block& block, const MotorControlRS::ReadTarget& target) {
    return block.valid && sameTarget(block.value.target, target);
}
/** Age bounds the transaction observation conservatively from request dispatch,
 * including bus wait. The drive's internal sample age remains undocumented.
 * Closure time alone is too late to serve as this lower bound. */
inline uint64_t ageUs(const StateCache::Block& block, uint64_t nowUs) {
    return block.valid && nowUs >= block.observedEarliestUs ? nowUs - block.observedEarliestUs : 0;
}
inline bool fresh(const StateCache::Block& block, const MotorControlRS::ReadTarget& target,
                  uint64_t nowUs, uint64_t maxAgeUs) {
    return current(block, target) && nowUs >= block.observedEarliestUs && ageUs(block, nowUs) <= maxAgeUs;
}
/** Admission is the attempt timestamp, not a claim that bytes reached the bus. */
inline void stateAttempt(StateCache& cache, const MotorControlRS::ReadTarget& target,
                         uint32_t operationId, uint8_t block, uint64_t nowUs) {
    if (block >= MotorControlRS::ESS_RS::STATE_BLOCK_COUNT) return;
    auto& entry = cache.blocks[block];
    entry.attemptKnown = true; entry.lastAttemptTarget = target;
    entry.lastAttemptOperationId = operationId; entry.lastAttemptUs = nowUs;
    entry.lastAttemptOk = false; entry.lastAttemptStatus = MotorControlRS::Ok();
}
/** Harvest each completed block before releasing its retained owner result.
 * Calling again for the same event is harmless. Older delivered observations
 * cannot replace newer wire evidence. Failed blocks keep previous values. */
inline void stateResult(StateCache& cache, const MotorControlRS::ESS_RS::ReadContext& context,
                        uint8_t block, uint64_t transactionStartedUs) {
    namespace ESS = MotorControlRS::ESS_RS;
    if (context.kind != ESS::ReadKind::STATE || block >= ESS::STATE_BLOCK_COUNT) return;
    auto& entry = cache.blocks[block];
    const bool latestAttempt = entry.lastAttemptOperationId == context.operationId &&
        sameTarget(entry.lastAttemptTarget, context.target);
    ESS::StateObservation checked;
    const auto decoded = ESS::getStateBlock(context, block, checked);
    if (latestAttempt) {
        entry.lastAttemptOk = decoded.isOk();
        entry.lastAttemptStatus = decoded ? MotorControlRS::Ok() : context.observations[block].status;
        if (entry.lastAttemptStatus && !decoded) entry.lastAttemptStatus = context.status;
    }
    if (!decoded) return;
    if (transactionStartedUs > checked.provenance.earliestUs) return;
    if (entry.valid && (checked.provenance.latestUs < entry.observedLatestUs ||
        (entry.value.target.id == checked.target.id && entry.value.target.address == checked.target.address &&
         checked.target.generation < entry.value.target.generation))) return;
    entry.value = checked; entry.valid = true;
    entry.lastSuccessUs = entry.observedLatestUs = checked.provenance.latestUs;
    entry.observedEarliestUs = transactionStartedUs;
    entry.deliveredUs = checked.provenance.deliveredUs;
}

}} // namespace MotorControlRSExample::Probe
