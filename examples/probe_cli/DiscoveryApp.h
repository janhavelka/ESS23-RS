// SPDX-License-Identifier: MIT
// Application-owned finite scan. Included after the existing host/tuple adapters.
// One query at a time uses the same cooperative bus owner and checked validator.
MotorControlRS::ReadEvent discoveryEvent(const Rtu::Completion& result, uint32_t operation) {
    MotorControlRS::ReadEvent event;
    event.target.id = result.expected.target; event.target.address = result.expected.address;
    event.target.generation = result.expected.targetGeneration; event.operationId = operation;
    event.kind = result.transport.reason == Rtu::Reason::FRAME ? ReadEventKind::FRAME :
        result.outcome == Rtu::Outcome::CANCELLED ? ReadEventKind::CANCEL : ReadEventKind::TRANSPORT_FAILURE;
    event.frame = result.transport.rxLength ? result.raw : nullptr; event.length = result.transport.rxLength;
    event.txAccepted = result.transport.txAccepted; event.executionUnknown = result.executionUnknown;
    event.transportDetail = static_cast<int32_t>(result.transport.reason);
    if (event.kind == ReadEventKind::FRAME && result.transport.closureQualified) {
        event.qualified = true; event.earliestUs = result.transport.closureEarliestUs;
        event.latestUs = result.transport.closureLatestUs;
    }
    return event;
}
void discoveryEnd(App& a, Probe::DiscoveryOutcome outcome) {
    if (a.discovery.outcome == Probe::DiscoveryOutcome::NONE) a.discovery.outcome = outcome;
    a.discovery.phase = Probe::DiscoveryPhase::RESTORING;
}
void discoveryRestore(App& a) {
    auto& scan = a.discovery;
    if (a.discoveryRequest.owner || a.owner.active() || a.owner.pending() || a.owner.recovering()) return;
    if (a.owner.needsRecovery() || (!a.serial.blocked && uart.needsRecovery())) {
        scan.phase = Probe::DiscoveryPhase::INTERLOCK; return;
    }
    Probe::HostRequest request; request.tuple = scan.originalTuple;
    const auto result = hostSerialImpl(&a, &request, a.serial, true);
    if (result == Probe::Action::BUSY) return;
    if (result != Probe::Action::OK || !a.serial.activeKnown || !sameTuple(a.serial.active, scan.originalTuple)) {
        scan.outcome = Probe::DiscoveryOutcome::RESTORE_FAILED;
        scan.phase = Probe::DiscoveryPhase::INTERLOCK; return;
    }
    scan.restored = true; scan.owned = false; scan.phase = Probe::DiscoveryPhase::TERMINAL;
    scan.finishedUs = nowUs();
}
void serviceDiscovery(App& a, uint64_t now) {
    using namespace MotorControlRS;
    using Probe::DiscoveryPhase; using Probe::DiscoveryOutcome;
    auto& scan = a.discovery;
    if (!scan.owned || scan.phase == DiscoveryPhase::INTERLOCK) return;
    if (a.discoveryRequest.owner) {
        const auto* result = a.owner.result(a.discoveryRequest);
        if (!result) return; // Physical TX/capture must settle, even after cancellation.
        bool fault = a.owner.needsRecovery() || uart.needsRecovery();
        if (a.discoveryIdentityPending) {
            auto& finding = scan.findings[scan.count - 1];
            const auto event = discoveryEvent(*result, a.discoveryIdentity.operationId);
            if (!ESS::advanceRead(a.discoveryIdentity, event, now)) fault = true;
            finding.identityEvidence = a.discoveryIdentity.observations[0];
            finding.identityKnown = bool(ESS::getIdentity(a.discoveryIdentity, finding.identity));
            finding.identityAmbiguous = finding.identityKnown &&
                (finding.identity.rawModel != finding.probe.rawModel || finding.identity.rawActiveNode != finding.request.target.address);
            a.discoveryIdentityPending = false;
            scan.phase = DiscoveryPhase::PROBE;
        } else {
            auto& finding = scan.findings[scan.count++];
            finding.request = a.discoveryPrepared;
            const auto event = discoveryEvent(*result, finding.request.operationId);
            const auto checked = MotorControlRS::checkProbe(finding.request, event, now, finding.probe);
            if (!checked) {
                // Correlation failure is never a responder or a reason to continue.
                finding.probe.status = checked; finding.probe.outcome = ProbeOutcome::TRANSPORT_ERROR;
                fault = true;
            } else if (event.kind == ReadEventKind::TRANSPORT_FAILURE) {
                if (result->transport.reason == Rtu::Reason::NO_RESPONSE) finding.probe.outcome = ProbeOutcome::NO_RESPONSE;
                else if (result->transport.reason == Rtu::Reason::REQUEST_DEADLINE ||
                    result->outcome == Rtu::Outcome::QUEUE_EXPIRED || result->outcome == Rtu::Outcome::DISPATCH_EXPIRED)
                    finding.probe.outcome = ProbeOutcome::DEADLINE;
                else if (result->transport.reason == Rtu::Reason::LENGTH) finding.probe.outcome = ProbeOutcome::MALFORMED;
                else if (result->transport.reason == Rtu::Reason::TIMING_UNCERTAIN || result->transport.reason == Rtu::Reason::GAP)
                    finding.probe.outcome = ProbeOutcome::TIMING_UNQUALIFIED;
            }
            if (scan.settings.identity && finding.probe.outcome == ProbeOutcome::RESPONDER && !fault && !scan.cancelRequested)
                scan.phase = DiscoveryPhase::IDENTITY;
        }
        a.owner.release(a.discoveryRequest); a.discoveryRequest = Rtu::RequestId();
        if (fault) discoveryEnd(a, DiscoveryOutcome::TRANSPORT_FAULT);
        else if (scan.cancelRequested) discoveryEnd(a, DiscoveryOutcome::CANCELLED);
        else if (now >= scan.deadlineUs) discoveryEnd(a, DiscoveryOutcome::DEADLINE);
        else if (scan.phase != DiscoveryPhase::IDENTITY) {
            if (scan.address < scan.settings.last) ++scan.address;
            else if (scan.tupleIndex + 1 < scan.settings.tupleCount) { ++scan.tupleIndex; scan.address = scan.settings.first; }
            else discoveryEnd(a, DiscoveryOutcome::COMPLETE);
        }
    }
    if (scan.phase == DiscoveryPhase::RESTORING) { discoveryRestore(a); return; }
    if (scan.cancelRequested) discoveryEnd(a, DiscoveryOutcome::CANCELLED);
    else if (now >= scan.deadlineUs) discoveryEnd(a, DiscoveryOutcome::DEADLINE);
    else if (scan.requests >= scan.settings.requestLimit) discoveryEnd(a, DiscoveryOutcome::REQUEST_LIMIT);
    else if (scan.phase == DiscoveryPhase::PROBE && scan.count >= scan.settings.resultLimit) discoveryEnd(a, DiscoveryOutcome::RESULT_LIMIT);
    if (scan.phase == DiscoveryPhase::RESTORING) { discoveryRestore(a); return; }
    if (a.owner.needsRecovery() || uart.needsRecovery()) { discoveryEnd(a, DiscoveryOutcome::TRANSPORT_FAULT); discoveryRestore(a); return; }
    // Urgent and required work already admitted by a cooperative producer gets its opportunity.
    if (a.owner.active() || a.owner.pending() || a.owner.recovering()) return;
    Probe::HostRequest tuple; tuple.tuple = scan.settings.tuples[scan.tupleIndex];
    const auto configured = hostSerialImpl(&a, &tuple, a.serial, true);
    if (configured == Probe::Action::BUSY) return;
    if (configured != Probe::Action::OK) { discoveryEnd(a, DiscoveryOutcome::SETUP_FAILED); discoveryRestore(a); return; }
    now = uart.sample(); // UART setup consumes real time; do not renew the scan deadline.
    if (now >= scan.deadlineUs) { discoveryEnd(a, DiscoveryOutcome::DEADLINE); discoveryRestore(a); return; }
    if (!a.nextOperationId) { discoveryEnd(a, DiscoveryOutcome::REQUEST_LIMIT); discoveryRestore(a); return; }
    const uint64_t deadline = std::min(scan.deadlineUs, now + uint64_t(scan.settings.queryMs) * 1000);
    ReadTarget target; target.id = target.address = scan.address; target.generation = scan.originalTarget.generation;
    Rtu::BusRequest request;
    ESS::PreparedRead identity;
    if (scan.phase == DiscoveryPhase::IDENTITY) {
        if (!ESS::prepareIdentity(a.discoveryIdentity, target, a.nextOperationId, now, deadline, communicationTuple(a.serial.active)) ||
            !ESS::nextRead(a.discoveryIdentity, now, identity)) { discoveryEnd(a, DiscoveryOutcome::SETUP_FAILED); return; }
        request.wire.bytes = identity.bytes; request.wire.length = identity.length;
        request.expected.count = 4;
    } else {
        if (!MotorControlRS::prepareProbe(a.discoveryPrepared, scan.settings.profile, target, a.nextOperationId,
                now, deadline, communicationTuple(a.serial.active))) { discoveryEnd(a, DiscoveryOutcome::SETUP_FAILED); return; }
        request.wire.bytes = a.discoveryPrepared.bytes; request.wire.length = a.discoveryPrepared.length;
        request.expected.count = 1;
    }
    request.wire.replyLength = ESS::expectedReadRegistersLen(request.expected.count);
    request.wire.responseTimeoutUs = a.serial.timing.responseTimeoutUs; request.wire.replyGapUs = a.serial.timing.replyGapUs;
    request.wire.deadlineUs = deadline;
    request.expected.address = target.address; request.expected.target = target.id;
    request.expected.targetGeneration = target.generation; request.expected.function = 3; request.expected.first = 0;
    request.validator = Rtu::essValidator();
    const auto admitted = a.owner.admit(request, now, a.discoveryRequest);
    if (admitted == Rtu::BusAdmission::QUEUE_FULL || admitted == Rtu::BusAdmission::RESULTS_FULL) return;
    if (admitted != Rtu::BusAdmission::ACCEPTED) { discoveryEnd(a, DiscoveryOutcome::SETUP_FAILED); return; }
    ++a.nextOperationId; ++scan.requests;
    a.discoveryIdentityPending = scan.phase == DiscoveryPhase::IDENTITY;
    if (a.discoveryIdentityPending) {
        auto& finding = scan.findings[scan.count - 1];
        finding.identityAttempted = true;
        finding.identityOperationId = a.discoveryIdentity.operationId;
        finding.identityDeadlineUs = a.discoveryIdentity.deadlineUs;
    }
}
Probe::Action discoveryCommand(void* context, const Probe::DiscoveryCommand* command, Probe::DiscoveryView& out) {
    using Probe::DiscoveryPhase; using Probe::DiscoveryOutcome; using Probe::DiscoveryCommandKind;
    App& a = *static_cast<App*>(context); out.scan = &a.discovery;
    if (!command) return Probe::Action::OK;
    auto& scan = a.discovery;
    if (command->kind == DiscoveryCommandKind::CANCEL) {
        if (!scan.owned || scan.phase == DiscoveryPhase::INTERLOCK) return Probe::Action::ALREADY_TERMINAL;
        scan.cancelRequested = true;
        if (a.discoveryRequest.owner) a.owner.cancelUnsent(a.discoveryRequest, nowUs());
        return Probe::Action::OK;
    }
    if (command->kind == DiscoveryCommandKind::RESTORE) {
        if (scan.phase != DiscoveryPhase::INTERLOCK) return Probe::Action::INVALID;
        scan.phase = DiscoveryPhase::RESTORING; discoveryRestore(a);
        return scan.restored ? Probe::Action::OK : Probe::Action::RECOVERY_REQUIRED;
    }
    if (command->kind == DiscoveryCommandKind::FINISH) {
        if (scan.phase != DiscoveryPhase::TERMINAL || scan.owned) return Probe::Action::BUSY;
        scan.released = true; return Probe::Action::OK;
    }
    if (command->kind != DiscoveryCommandKind::BEGIN) return Probe::Action::INVALID;
    if (scan.owned || (scan.operationId && !scan.released)) return Probe::Action::BUSY;
    if (!platformReady) return Probe::Action::UNAVAILABLE;
    if (a.serial.blocked || !a.serial.activeKnown || a.owner.needsRecovery() || uart.needsRecovery()) return Probe::Action::RECOVERY_REQUIRED;
    if (a.owner.configurationOwned() || a.owner.commissioningOwned() || a.owner.active() || a.owner.pending() ||
        a.owner.recovering() || reading(a) || acting(a) || axisReserved(a) || a.monitorState.settings.enabled ||
        a.persistenceCapture || a.persistenceRequest.owner || a.commissioningRequest.owner || a.motionProfile.view.pending)
        return Probe::Action::BUSY;
    if (!a.nextOperationId) return Probe::Action::IDS_EXHAUSTED;
    auto settings = command->settings;
    MotorControlRS::DiscoveryCapabilities caps;
    if (!MotorControlRS::getDiscoveryCapabilities(settings.profile, caps)) return Probe::Action::UNSUPPORTED;
    if (!settings.first && !settings.last) settings.first = settings.last = a.axis.target.address;
    if (settings.first < caps.minimumAddress || settings.last > caps.maximumAddress || settings.first > settings.last ||
        !settings.queryMs || settings.queryMs > 5000 || !settings.overallMs || settings.overallMs > 60000 ||
        !settings.requestLimit || settings.requestLimit > 256 || !settings.resultLimit || settings.resultLimit > Probe::DISCOVERY_MAX_RESULTS ||
        settings.tupleCount > Probe::DISCOVERY_MAX_TUPLES) return Probe::Action::INVALID;
    if (!settings.tupleCount) { settings.tupleCount = 1; settings.tuples[0] = a.serial.active; }
    for (uint8_t i = 0; i < settings.tupleCount; ++i) {
        if (!Esp32S3Uart::supports(settings.tuples[i])) return Probe::Action::UNSUPPORTED;
        for (uint8_t j = 0; j < i; ++j) if (sameTuple(settings.tuples[i], settings.tuples[j])) return Probe::Action::INVALID;
    }
    scan.~DiscoveryScan(); new (&scan) Probe::DiscoveryScan(); // Large retained findings stay in PSRAM.
    scan.settings = settings; scan.originalTarget = a.axis.target; scan.originalTuple = a.serial.active;
    scan.originalSerialGeneration = a.serial.generation;
    scan.operationId = a.nextOperationId++; scan.startedUs = nowUs(); scan.deadlineUs = scan.startedUs + uint64_t(settings.overallMs) * 1000;
    scan.address = settings.first; scan.owned = true; scan.phase = DiscoveryPhase::PROBE;
    a.discoveryOriginalKnown = (a.knownTargets[scan.originalTarget.address / 8] & (1U << (scan.originalTarget.address % 8))) != 0;
    return Probe::Action::OK;
}
