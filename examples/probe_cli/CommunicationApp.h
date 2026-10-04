// SPDX-License-Identifier: MIT
// Included in the standalone application's private namespace after hostSerialImpl.
// One retained commissioning session; all wire work uses the existing bus owner.
MotorControlRS::ActiveSerialTuple communicationTuple(const HostTuple& host) {
    MotorControlRS::ActiveSerialTuple value;
    value.known = true; value.baud = host.baud; value.dataBits = 8;
    value.stopBits = host.format == HostFormat::N8_2 ? 2 : 1;
    value.parity = host.format == HostFormat::E8_1 ? MotorControlRS::SerialParity::EVEN :
        host.format == HostFormat::O8_1 ? MotorControlRS::SerialParity::ODD : MotorControlRS::SerialParity::NONE;
    return value;
}
HostTuple communicationHost(const MotorControlRS::ActiveSerialTuple& serial) {
    HostTuple value; value.baud = serial.baud;
    value.format = serial.parity == MotorControlRS::SerialParity::EVEN ? HostFormat::E8_1 :
        serial.parity == MotorControlRS::SerialParity::ODD ? HostFormat::O8_1 :
        serial.stopBits == 2 ? HostFormat::N8_2 : HostFormat::N8_1;
    return value;
}
void communicationView(const App& a, Probe::CommunicationView& out) {
    out.context = a.commissioning.operationId ? &a.commissioning : nullptr;
    out.pending = a.commissioningRequest.owner != nullptr;
    out.owned = a.owner.commissioningOwned(); out.host = a.serial;
    out.routeReady = a.commissioningPrerequisites.routeBackQualified;
}
Probe::Action submitCommunication(App& a, uint64_t now) {
    ESS::PreparedCommunication work;
    if (!ESS::nextCommunication(a.commissioning, now, work) || work.kind != ESS::ActionWork::TRANSACTION)
        return Probe::Action::INVALID;
    Rtu::BusRequest request;
    request.wire.bytes = work.bytes; request.wire.length = work.length;
    request.wire.replyLength = work.write ? ESS::WRITE_RESPONSE_LEN : ESS::expectedReadRegistersLen(work.count);
    request.wire.deadlineUs = work.deadlineUs;
    request.wire.responseTimeoutUs = a.serial.timing.responseTimeoutUs;
    request.wire.replyGapUs = a.serial.timing.replyGapUs;
    request.expected.address = work.target.address; request.expected.target = work.target.id;
    request.expected.targetGeneration = work.target.generation;
    request.expected.function = work.write ? 6 : 3;
    request.expected.first = work.reg; request.expected.count = work.write ? 1 : work.count; request.expected.value = work.value;
    request.validator = Rtu::essValidator();
    const auto admitted = a.owner.admitCommissioning(request, a.commissioningLease, now, a.commissioningRequest);
    if (admitted == Rtu::BusAdmission::ACCEPTED) {
        a.commissioningRequestBinding = a.bindingGeneration;
        return Probe::Action::OK;
    }
    // Rejected admission still closes this explicit attempt, without sending or retrying it.
    MotorControlRS::ActionEvent failed;
    failed.transport.target = work.target; failed.transport.operationId = work.operationId;
    failed.transport.step = work.step; failed.transport.kind = ReadEventKind::TRANSPORT_FAILURE;
    failed.transport.transportDetail = static_cast<int32_t>(admitted);
    ESS::advanceCommunication(a.commissioning, failed, now);
    return admissionResult(admitted);
}
void serviceCommissioning(App& a, uint64_t now) {
    if (!a.commissioningRequest.owner) return;
    if (!a.commissioningInvalidated && a.commissioning.step == 0 && a.owner.txAccepted(a.commissioningRequest)) {
        a.commissioningInvalidated = true;
        ++a.serial.generation; // A device-interface write also invalidates old transport-context observations.
        invalidateSerialConfidence(a, now);
    }
    const auto* result = a.owner.result(a.commissioningRequest);
    if (!result) return;
    MotorControlRS::ActionEvent event;
    event.transport.target.id = result->expected.target;
    event.transport.target.address = result->expected.address;
    event.transport.target.generation = result->expected.targetGeneration;
    event.transport.operationId = a.commissioning.operationId; event.transport.step = a.commissioning.step;
    event.transport.kind = result->transport.reason == Rtu::Reason::FRAME ? ReadEventKind::FRAME :
        result->outcome == Rtu::Outcome::CANCELLED ? ReadEventKind::CANCEL : ReadEventKind::TRANSPORT_FAILURE;
    event.transport.txAccepted = result->transport.txAccepted;
    event.transport.executionUnknown = result->executionUnknown;
    event.transport.transportDetail = static_cast<int32_t>(result->transport.reason);
    event.transport.frame = result->transport.rxLength ? result->raw : nullptr;
    event.transport.length = result->transport.rxLength;
    event.txComplete = result->transport.txComplete;
    event.responseConfirmed = event.transport.kind == ReadEventKind::FRAME &&
        (a.commissioning.step != 0 || a.commissioningResponseQualified);
    if (event.transport.kind == ReadEventKind::FRAME && result->transport.closureQualified) {
        event.transport.qualified = true;
        event.transport.earliestUs = result->transport.closureEarliestUs;
        event.transport.latestUs = result->transport.closureLatestUs;
    }
    if (!ESS::advanceCommunication(a.commissioning, event, now)) return;
    if (a.commissioning.step && a.commissioning.observedActiveKnown &&
        a.commissioningRequestBinding == a.bindingGeneration)
        a.commissioningConfirmedGeneration = a.serial.generation;
    a.owner.release(a.commissioningRequest); a.commissioningRequest = Rtu::RequestId();
}
Probe::Action communication(void* context, const Probe::CommunicationCommand* command, Probe::CommunicationView& out) {
    App& a = *static_cast<App*>(context); communicationView(a, out);
    if (!command) return Probe::Action::OK;
    using Kind = Probe::CommunicationCommandKind;
    const uint64_t now = uart.sample();
    if (!platformReady) return Probe::Action::UNAVAILABLE;
    if (command->kind == Kind::PREVIEW) return Probe::Action::OK; // Console shows source effects and current fixture prerequisites.
    if (command->kind == Kind::BEGIN) {
        if (a.owner.commissioningOwned() || a.serial.blocked || !a.serial.activeKnown ||
            a.owner.active() || a.owner.pending() || reading(a) || acting(a) ||
            a.monitorState.settings.enabled || a.recovery.operationId || axisReserved(a)) return Probe::Action::BUSY;
        if (!a.nextOperationId || a.serial.generation == UINT32_MAX || a.bindingGeneration == UINT32_MAX)
            return Probe::Action::IDS_EXHAUSTED;
        // Qualifications are application-owned evidence. No CLI flag manufactures a restart route.
        auto& p = a.commissioningPrerequisites;
        if (!p.routeBackQualified) return Probe::Action::UNAVAILABLE;
        p.previous = a.configuration; p.beforeSerial = communicationTuple(a.serial.active);
        MotorControlRS::ReadTarget target = a.axis.target; target.address = command->address;
        if (!Probe::sameTarget(target, a.configuration.target)) return Probe::Action::INVALID;
        const auto prepared = ESS::prepareCommunication(a.commissioningPrepared, target,
            a.nextOperationId, command->request, p, now, now + REQUEST_US);
        if (!prepared) return Probe::Action::INVALID;
        if (!a.owner.beginCommissioning(now, a.commissioningLease)) return Probe::Action::BUSY;
        a.commissioning = a.commissioningPrepared; ++a.nextOperationId;
        a.commissioningInvalidated = false; a.commissioningConfirmedGeneration = 0;
        a.commissioningResponseQualified = actionTimingQualified;
        const auto admitted = submitCommunication(a, now); communicationView(a, out); return admitted;
    }
    if (!a.owner.commissioningOwned()) return Probe::Action::INVALID;
    if (a.commissioningRequest.owner) return Probe::Action::BUSY;
    if (command->kind == Kind::SELECT_BEFORE || command->kind == Kind::SELECT_REQUESTED) {
        Probe::HostRequest request;
        request.tuple = communicationHost(command->kind == Kind::SELECT_BEFORE ?
            a.commissioning.beforeSerial : a.commissioning.requestedSerial);
        const auto selected = hostSerialImpl(&a, &request, out.host, true);
        a.commissioningConfirmedGeneration = 0;
        communicationView(a, out); return selected;
    }
    if (command->kind == Kind::CONFIRM_BEFORE || command->kind == Kind::CONFIRM_REQUESTED) {
        const bool before = command->kind == Kind::CONFIRM_BEFORE;
        const auto& target = before ? a.commissioning.beforeTarget : a.commissioning.requestedTarget;
        const auto& serial = before ? a.commissioning.beforeSerial : a.commissioning.requestedSerial;
        if (!a.serial.activeKnown || a.serial.blocked || a.owner.needsRecovery() || uart.needsRecovery())
            return Probe::Action::RECOVERY_REQUIRED;
        if (!sameTuple(a.serial.active, communicationHost(serial))) return Probe::Action::INVALID;
        if (!ESS::prepareCommunicationConfirmation(a.commissioning, target, serial, now, now + REQUEST_US))
            return Probe::Action::INVALID;
        a.commissioningConfirmedGeneration = 0;
        const auto admitted = submitCommunication(a, now); communicationView(a, out); return admitted;
    }
    if (command->kind != Kind::FINISH) return Probe::Action::INVALID;
    const auto& session = a.commissioning;
    const bool noWrite = session.execution == MotorControlRS::ActionExecution::NOT_TRANSMITTED && !session.effects;
    if (!a.serial.activeKnown || a.serial.blocked || a.owner.needsRecovery() || uart.needsRecovery() ||
        (noWrite && !sameTuple(a.serial.active, communicationHost(session.beforeSerial))) ||
        (!noWrite && (!session.observedActiveKnown || a.commissioningConfirmedGeneration != a.serial.generation ||
            !sameTuple(a.serial.active, communicationHost(session.observedActiveSerial))))) return Probe::Action::RECOVERY_REQUIRED;
    if (a.bindingGeneration == UINT32_MAX) return Probe::Action::IDS_EXHAUSTED;
    if (!a.owner.endCommissioning(a.commissioningLease, now)) return Probe::Action::BUSY;
    a.commissioningLease = 0;
    if (!noWrite) {
        const uint8_t selected = session.observedActiveTarget.address;
        ++a.bindingGeneration;
        invalidateAxis(a);
        a.axis.target.address = a.axis.target.id = selected; a.axis.target.generation = a.bindingGeneration;
        a.address = selected;
        invalidateSerialConfidence(a, now);
    }
    communicationView(a, out); return Probe::Action::OK;
}
