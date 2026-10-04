// SPDX-License-Identifier: MIT
// Private cooperative application adapter. One existing commissioning lease,
// one retained persistence invocation; typed reads provide explicit verification.
constexpr uint8_t PERSISTENCE_MAX_INVOCATIONS = 2;
void persistenceView(const App& a, Probe::PersistenceView& out) {
    out.context = a.persistence.operationId ? &a.persistence : nullptr;
    out.baseline = a.persistenceBaselineKnown ? &a.persistenceBaseline : nullptr;
    out.snapshotKnown = a.persistenceBaselineKnown;
    out.pending = a.persistenceRequest.owner || a.persistenceCapture;
    out.owned = a.owner.commissioningOwned(); out.host = a.serial;
    out.routeReady = a.persistencePrerequisites.routeBackQualified;
    out.parentSession = a.persistenceParentSession; out.finished = a.persistenceFinished;
    out.invocations = a.persistenceInvocations;
    out.snapshotFailed = a.persistenceSnapshotFailed;
    out.verificationAttempts = a.persistenceVerificationAttempts;
}
MotorControlRS::ActionEvent persistenceEvent(const Rtu::Completion& result, uint32_t operation,
                                           uint8_t step, bool write) {
    MotorControlRS::ActionEvent event;
    event.transport.target.id = result.expected.target;
    event.transport.target.address = result.expected.address;
    event.transport.target.generation = result.expected.targetGeneration;
    event.transport.operationId = operation; event.transport.step = step;
    event.transport.kind = result.transport.reason == Rtu::Reason::FRAME ? ReadEventKind::FRAME :
        result.outcome == Rtu::Outcome::CANCELLED ? ReadEventKind::CANCEL : ReadEventKind::TRANSPORT_FAILURE;
    event.transport.txAccepted = result.transport.txAccepted;
    event.transport.executionUnknown = result.executionUnknown;
    event.transport.transportDetail = static_cast<int32_t>(result.transport.reason);
    event.transport.frame = result.transport.rxLength ? result.raw : nullptr;
    event.transport.length = result.transport.rxLength;
    event.txComplete = result.transport.txComplete;
    event.responseConfirmed = event.transport.kind == ReadEventKind::FRAME && (!write || writeResponseConfirmed);
    if (event.transport.kind == ReadEventKind::FRAME && result.transport.closureQualified) {
        event.transport.qualified = true;
        event.transport.earliestUs = result.transport.closureEarliestUs;
        event.transport.latestUs = result.transport.closureLatestUs;
    }
    return event;
}
Probe::Action persistenceReadStep(App& a, uint64_t now) {
    auto& read = a.persistenceReads[a.persistenceReadIndex];
    ESS::PreparedRead work;
    if (!ESS::nextRead(read, now, work)) return Probe::Action::FAILED;
    Rtu::BusRequest request;
    request.wire.bytes = work.bytes; request.wire.length = work.length;
    request.wire.replyLength = ESS::expectedReadRegistersLen(work.count);
    request.wire.deadlineUs = work.deadlineUs;
    request.wire.responseTimeoutUs = a.serial.timing.responseTimeoutUs;
    request.wire.replyGapUs = a.serial.timing.replyGapUs;
    request.expected.address = work.target.address; request.expected.target = work.target.id;
    request.expected.targetGeneration = work.target.generation; request.expected.function = 3;
    request.expected.first = work.first; request.expected.count = work.count;
    request.validator = Rtu::essValidator();
    return admissionResult(a.owner.admitCommissioning(request, a.commissioningLease, now, a.persistenceReadRequest));
}
bool persistencePrepareRead(App& a, uint64_t now) {
    auto& read = a.persistenceReads[a.persistenceReadIndex];
    const auto serial = communicationTuple(a.serial.active);
    if (a.persistenceReadIndex == 0)
        return ESS::prepareIdentity(read, a.persistenceReadTarget, a.persistenceReadOperation, now, a.persistenceReadDeadline, serial).isOk();
    if (a.persistenceReadIndex == 1)
        return ESS::prepareConfig(read, a.persistenceReadTarget, a.persistenceReadOperation, now, a.persistenceReadDeadline, serial, a.inputWiring).isOk();
    if (!ESS::getConfig(a.persistenceReads[1], a.persistenceVerification.config)) return false;
    return ESS::prepareState(read, a.persistenceReadTarget, a.persistenceReadOperation, now, a.persistenceReadDeadline,
                            serial, &a.persistenceVerification.config).isOk();
}
void persistenceCaptureFailed(App& a) {
    a.persistenceCapture = false; a.persistenceSnapshotFailed = true;
    a.persistenceConfirmedGeneration = 0;
    // Failed partial reads remain in their caller-owned contexts. No new attempt
    // or write is scheduled, and the commissioning lease remains inspectable.
}
Probe::Action persistenceCaptureStart(App& a, uint64_t now, bool after) {
    if (!a.nextOperationId) return Probe::Action::IDS_EXHAUSTED;
    a.persistenceAfter = after; a.persistenceReadIndex = 0;
    a.persistenceCapture = true; a.persistenceSnapshotFailed = false;
    a.persistenceReadDeadline = now + 3000000;
    a.persistenceReadOperation = a.nextOperationId++;
    a.persistenceSnapshotSerialGeneration = a.serial.generation;
    a.persistenceSnapshotBinding = a.bindingGeneration;
    a.persistenceConfirmedGeneration = 0;
    if (!after) a.persistenceBaselineKnown = false;
    if (!persistencePrepareRead(a, now)) { persistenceCaptureFailed(a); return Probe::Action::INVALID; }
    const auto admitted = persistenceReadStep(a, now);
    if (admitted != Probe::Action::OK) persistenceCaptureFailed(a);
    return admitted;
}
void servicePersistence(App& a, uint64_t now) {
    if (a.persistenceRequest.owner) {
        if (!a.persistenceInvalidated && a.owner.txAccepted(a.persistenceRequest)) {
            a.persistenceInvalidated = true;
            a.commissioningConfirmedGeneration = a.persistenceConfirmedGeneration = 0;
            // Save-all can commit pending assumptions; restore can replace all
            // settings. Invalidate interpretation without editing old results.
            // A confirmed new address is still the same physical axis. Its
            // host binding is deliberately not changed until commissioning ends.
            invalidateDriverAssumptions(a, a.persistenceParentSession ?
                a.axis.target.address : a.persistence.target.address, UINT32_MAX, now);
            invalidateSerialConfidence(a, now);
        }
        const auto* result = a.owner.result(a.persistenceRequest);
        if (result) {
            const auto event = persistenceEvent(*result, a.persistence.operationId, 0, true);
            if (ESS::advancePersistence(a.persistence, event, now)) {
                a.owner.release(a.persistenceRequest); a.persistenceRequest = Rtu::RequestId();
            }
        }
    }
    if (!a.persistenceCapture) return;
    auto& read = a.persistenceReads[a.persistenceReadIndex];
    if (a.persistenceReadRequest.owner) {
        const auto* result = a.owner.result(a.persistenceReadRequest);
        if (!result) return;
        const auto event = persistenceEvent(*result, read.operationId, read.step, false);
        if (!ESS::advanceRead(read, event.transport, now)) return;
        a.owner.release(a.persistenceReadRequest); a.persistenceReadRequest = Rtu::RequestId();
    }
    if (a.persistenceSnapshotSerialGeneration != a.serial.generation ||
        a.persistenceSnapshotBinding != a.bindingGeneration || read.state == ReadState::FAILED) {
        persistenceCaptureFailed(a); return;
    }
    if (read.state == ReadState::ACTIVE) {
        if (persistenceReadStep(a, now) != Probe::Action::OK) persistenceCaptureFailed(a);
        return;
    }
    if (++a.persistenceReadIndex < 3) {
        if (!persistencePrepareRead(a, now) || persistenceReadStep(a, now) != Probe::Action::OK)
            persistenceCaptureFailed(a);
        return;
    }
    auto& verification = a.persistenceVerification;
    if (!ESS::getIdentity(a.persistenceReads[0], verification.identity) ||
        !ESS::getConfig(a.persistenceReads[1], verification.config) ||
        !ESS::getStateBlock(a.persistenceReads[2], 0, verification.stationary)) {
        persistenceCaptureFailed(a); return;
    }
    verification.serial = communicationTuple(a.serial.active);
    verification.configurationGeneration = a.axis.generation;
    verification.maxAgeUs = a.persistencePrerequisites.maxAgeUs;
    // This application has no motor restart control/evidence. MCU restart and
    // host restoration cannot mark any field as proven durable.
    verification.restartObserved = false; verification.restartUs = 0; verification.restartSourceId = 0;
    a.persistenceCapture = false;
    if (a.persistenceAfter) {
        if (!ESS::verifyPersistence(a.persistence, verification, now)) { persistenceCaptureFailed(a); return; }
        a.persistenceConfirmedGeneration = a.serial.generation;
    } else {
        a.persistenceBaseline = a.persistencePrerequisites;
        auto& baseline = a.persistenceBaseline;
        baseline.beforeIdentity = verification.identity; baseline.beforeConfig = verification.config;
        baseline.stationary = verification.stationary; baseline.beforeSerial = verification.serial;
        baseline.configurationGeneration = verification.configurationGeneration;
        a.persistenceBaselineKnown = true;
    }
}
Probe::Action persistence(void* context, const Probe::PersistenceCommand* command, Probe::PersistenceView& out) {
    App& a = *static_cast<App*>(context); persistenceView(a, out);
    if (!command) return Probe::Action::OK;
    using Kind = Probe::PersistenceCommandKind;
    const uint64_t now = uart.sample();
    if (!platformReady) return Probe::Action::UNAVAILABLE;
    if (command->kind == Kind::PREVIEW) return Probe::Action::OK;
    if (a.persistenceRequest.owner || a.persistenceCapture || a.commissioningRequest.owner)
        return Probe::Action::BUSY;
    if (command->kind == Kind::SELECT_BEFORE) {
        if (!a.owner.commissioningOwned() || !a.commissioningLease) return Probe::Action::INVALID;
        Probe::HostRequest request;
        const auto& serial = a.persistence.operationId && !a.persistenceFinished ?
            a.persistence.before.beforeSerial : a.persistenceBaseline.beforeSerial;
        if (!serial.known) return Probe::Action::INVALID;
        request.tuple = communicationHost(serial);
        const auto selected = hostSerialImpl(&a, &request, out.host, true);
        persistenceView(a, out); return selected;
    }
    if (a.serial.blocked || !a.serial.activeKnown || a.owner.needsRecovery() || uart.needsRecovery())
        return Probe::Action::RECOVERY_REQUIRED;
    if (command->kind == Kind::SNAPSHOT) {
        if (a.persistence.operationId && !a.persistenceFinished) return Probe::Action::BUSY;
        if (a.owner.commissioningOwned()) {
            if (a.persistenceParentSession && a.commissioningLease &&
                a.commissioning.observedActiveKnown && a.commissioningConfirmedGeneration == a.serial.generation) {
                a.persistenceReadTarget = a.commissioning.observedActiveTarget;
                a.persistenceParentSession = true;
            } else if (!a.persistenceParentSession && a.commissioningLease) {
                a.persistenceReadTarget = a.axis.target;
            } else return Probe::Action::RECOVERY_REQUIRED;
        } else {
            if (reading(a) || acting(a) || axisReserved(a) || a.monitorState.settings.enabled || a.recovery.operationId)
                return Probe::Action::BUSY;
            if (!a.owner.beginCommissioning(now, a.commissioningLease)) return Probe::Action::BUSY;
            a.persistenceParentSession = false; a.persistenceReadTarget = a.axis.target;
            a.persistenceBaseline.beforeSerial = communicationTuple(a.serial.active);
        }
        const auto result = persistenceCaptureStart(a, now, false); persistenceView(a, out); return result;
    }
    if (command->kind == Kind::BEGIN) {
        if (command->request > ESS::PersistenceKind::FACTORY_RESTORE) return Probe::Action::INVALID;
        if (a.persistence.operationId && !a.persistenceFinished) return Probe::Action::BUSY;
        if (a.persistenceInvocations == PERSISTENCE_MAX_INVOCATIONS) return Probe::Action::RESULTS_FULL;
        if (!a.nextOperationId || a.bindingGeneration == UINT32_MAX) return Probe::Action::IDS_EXHAUSTED;
        // Physical invocation needs an actual backup/restart/recommission route.
        // The core remains portable; the bench never manufactures this evidence.
        auto& p = a.persistencePrerequisites;
        if (!p.routeBackQualified || !p.backupSourceId) return Probe::Action::UNAVAILABLE;
        if (a.persistenceBaselineKnown && a.owner.commissioningOwned()) {
            if (a.persistenceSnapshotBinding != a.bindingGeneration ||
                a.persistenceSnapshotSerialGeneration != a.serial.generation) return Probe::Action::INVALID;
            p.beforeConfig = a.persistenceBaseline.beforeConfig; p.beforeIdentity = a.persistenceBaseline.beforeIdentity;
            p.stationary = a.persistenceBaseline.stationary; p.beforeSerial = a.persistenceBaseline.beforeSerial;
            p.configurationGeneration = a.persistenceBaseline.configurationGeneration;
        } else {
            if (a.owner.commissioningOwned()) return Probe::Action::INVALID;
            p.beforeConfig = a.configuration; p.beforeIdentity = a.identity;
            const auto& motion = a.stateCache.blocks[0];
            if (!Probe::fresh(motion, a.axis.target, now, p.maxAgeUs)) return Probe::Action::INVALID;
            p.stationary = motion.value; p.beforeSerial = communicationTuple(a.serial.active);
            p.configurationGeneration = a.axis.generation;
        }
        const auto prepare = command->request == ESS::PersistenceKind::SAVE ? ESS::prepareSave : ESS::prepareFactoryRestore;
        if (!prepare(a.persistencePrepared, p.beforeConfig.target, a.nextOperationId, p, now, now + REQUEST_US))
            return Probe::Action::INVALID;
        if (!a.owner.commissioningOwned()) {
            if (reading(a) || acting(a) || axisReserved(a) || a.monitorState.settings.enabled || a.recovery.operationId)
                return Probe::Action::BUSY;
            if (!a.owner.beginCommissioning(now, a.commissioningLease)) return Probe::Action::BUSY;
            a.persistenceParentSession = false;
        }
        a.persistence = a.persistencePrepared; ++a.nextOperationId; ++a.persistenceInvocations;
        a.persistenceFinished = false; a.persistenceInvalidated = false; a.persistenceConfirmedGeneration = 0;
        a.persistenceVerificationAttempts = 0;
        ESS::PreparedPersistence work;
        if (!ESS::nextPersistence(a.persistence, now, work)) return Probe::Action::INVALID;
        Rtu::BusRequest request;
        request.wire.bytes = work.bytes; request.wire.length = work.length;
        request.wire.replyLength = ESS::WRITE_RESPONSE_LEN; request.wire.deadlineUs = work.deadlineUs;
        request.wire.responseTimeoutUs = a.serial.timing.responseTimeoutUs; request.wire.replyGapUs = a.serial.timing.replyGapUs;
        request.expected.address = work.target.address; request.expected.target = work.target.id;
        request.expected.targetGeneration = work.target.generation; request.expected.function = 6;
        request.expected.first = work.reg; request.expected.count = 1; request.expected.value = work.value;
        request.validator = Rtu::essValidator();
        const auto admitted = a.owner.admitCommissioning(request, a.commissioningLease, now, a.persistenceRequest);
        if (admitted != Rtu::BusAdmission::ACCEPTED) {
            MotorControlRS::ActionEvent failed;
            failed.transport.target = work.target; failed.transport.operationId = work.operationId;
            failed.transport.kind = ReadEventKind::TRANSPORT_FAILURE;
            failed.transport.transportDetail = static_cast<int32_t>(admitted);
            ESS::advancePersistence(a.persistence, failed, now);
        }
        persistenceView(a, out); return admissionResult(admitted);
    }
    if (!a.owner.commissioningOwned() || !a.commissioningLease) return Probe::Action::INVALID;
    if (command->kind == Kind::VERIFY) {
        if (!a.persistence.operationId || a.persistenceFinished || a.persistenceVerificationAttempts >= 2) return Probe::Action::INVALID;
        if (!sameTuple(a.serial.active, communicationHost(a.persistence.before.beforeSerial))) return Probe::Action::INVALID;
        a.persistenceReadTarget = a.persistence.target;
        ++a.persistenceVerificationAttempts;
        const auto result = persistenceCaptureStart(a, now, true); persistenceView(a, out); return result;
    }
    if (command->kind != Kind::FINISH) return Probe::Action::INVALID;
    const bool noWrite = !a.persistence.operationId || a.persistenceFinished || !a.persistence.effects;
    if (!noWrite && a.persistence.kind == ESS::PersistenceKind::FACTORY_RESTORE &&
        a.persistence.manualInterventionRequired) return Probe::Action::RECOVERY_REQUIRED;
    if (!noWrite && !a.persistenceParentSession && a.persistence.communicationChanged)
        return Probe::Action::RECOVERY_REQUIRED;
    if (!noWrite && (a.persistenceConfirmedGeneration != a.serial.generation ||
        a.persistenceSnapshotBinding != a.bindingGeneration || !a.persistence.verificationKnown))
        return Probe::Action::RECOVERY_REQUIRED;
    if (noWrite) {
        const auto& before = a.persistence.operationId && !a.persistenceFinished ?
            a.persistence.before.beforeSerial : a.persistenceBaseline.beforeSerial;
        if (!before.known || !sameTuple(a.serial.active, communicationHost(before))) return Probe::Action::RECOVERY_REQUIRED;
    }
    if (!a.persistenceParentSession) {
        if (!a.owner.endCommissioning(a.commissioningLease, now)) return Probe::Action::BUSY;
        a.commissioningLease = 0;
        a.persistenceBaselineKnown = false;
        if (!noWrite) { ++a.bindingGeneration; a.axis.target.generation = a.bindingGeneration; invalidateSerialConfidence(a, now); }
    }
    a.persistenceFinished = true;
    persistenceView(a, out); return Probe::Action::OK;
}
