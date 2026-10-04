// SPDX-License-Identifier: MIT
// Included inside the standalone application's private namespace.
// Explicit profile snapshot/restoration uses the normal owner and typed codecs.
bool submitMotionProfile(App& a, bool write, uint64_t now) {
    auto& session = a.motionProfile;
    auto& view = session.view;
    Rtu::BusRequest request;
    if (write) {
        ESS::PositionProfile original;
        original.startSpeed = view.original[0];
        original.accelerationTime = view.original[1];
        original.decelerationTime = view.original[2];
        original.speed = view.original[3];
        if (!ESS::decodeUint32(view.original + 4, 2, a.configuration.wordOrder, original.targetBits)) return false;
        request.wire.length = ESS::buildWritePositionProfile(view.address, original,
            a.configuration.wordOrder, view.tx, sizeof(view.tx));
    } else {
        request.wire.length = ESS::buildReadPositionProfile(view.address, view.tx, sizeof(view.tx));
    }
    view.txLength = request.wire.length;
    request.wire.bytes = view.tx;
    request.wire.replyLength = write ? ESS::WRITE_RESPONSE_LEN : ESS::expectedReadRegistersLen(6);
    request.wire.responseTimeoutUs = a.serial.timing.responseTimeoutUs;
    request.wire.replyGapUs = a.serial.timing.replyGapUs;
    request.wire.deadlineUs = view.deadlineUs;
    request.expected.address = request.expected.target = view.address;
    request.expected.targetGeneration = a.bindingGeneration;
    request.expected.function = write ? 16 : 3;
    request.expected.first = write ? 0x0021 : 0x0020;
    request.expected.count = write ? 5 : 6;
    request.validator = Rtu::essValidator();
    return request.wire.length && a.owner.admit(request, now, session.request) == Rtu::BusAdmission::ACCEPTED;
}
Probe::Action motionProfileCommand(void* context, Probe::MotionProfileCommand command, Probe::MotionProfileView& out) {
    if (command > Probe::MotionProfileCommand::RESTORE) return Probe::Action::INVALID;
    App& a = *static_cast<App*>(context);
    auto& session = a.motionProfile;
    auto& view = session.view;
    out = view;
    if (command == Probe::MotionProfileCommand::INSPECT) return Probe::Action::OK;
    if (!platformReady || !a.serial.activeKnown || a.serial.blocked || a.owner.needsRecovery() || uart.needsRecovery())
        return Probe::Action::RECOVERY_REQUIRED;
    if (view.pending || a.owner.active() || a.owner.pending() || a.owner.configurationOwned() ||
        a.owner.commissioningOwned() || reading(a) || acting(a) || a.monitorState.settings.enabled)
        return Probe::Action::BUSY;
    if (!a.configuration.operationId || !Probe::sameTarget(a.configuration.target, a.axis.target) ||
        !(a.knownTargets[a.axis.target.address / 8] & (1U << (a.axis.target.address % 8))))
        return Probe::Action::UNAVAILABLE;
    const bool restore = command == Probe::MotionProfileCommand::RESTORE;
    // Preserve the original snapshot; never silently replace it after rebinding.
    if (view.saved && !motionProfileEndpoint(a)) return Probe::Action::UNAVAILABLE;
    if (restore && (!motionProfileEndpoint(a) || !motionProfileStationary(a, nowUs()) ||
        axisReserved(a, a.axis.target.address))) return Probe::Action::UNAVAILABLE;
    if (!view.saved) session.configuration = a.configuration.raw;
    view.address = a.axis.target.address;
    view.generation = a.axis.generation;
    view.serialGeneration = a.serial.generation;
    session.bindingGeneration = a.bindingGeneration;
    view.deadlineUs = nowUs() + REQUEST_US;
    view.phase = restore ? Probe::MotionProfilePhase::RESTORE : Probe::MotionProfilePhase::READ;
    view.pending = true;
    view.ok = view.restored = false;
    view.error = "none";
    view.rxLength = view.writeReplyLength = view.writeTxLength = 0;
    view.txAccepted = view.writeTxAccepted = 0;
    view.txComplete = view.writeTxComplete = false;
    view.writeQualified = view.writeExecutionUnknown = false;
    view.writeEarliestUs = view.writeLatestUs = view.writeDeliveredUs = 0;
    view.deliveredUs = 0;
    view.executionUnknown = view.closureQualified = false;
    view.closureEarliestUs = view.closureLatestUs = 0;
    if (!submitMotionProfile(a, restore, nowUs())) {
        view.pending = false;
        view.error = "admission";
        out = view;
        return Probe::Action::FAILED;
    }
    out = view;
    return Probe::Action::OK;
}
void serviceMotionProfile(App& a, uint64_t now) {
    auto& session = a.motionProfile;
    auto& view = session.view;
    if (!view.pending) return;
    const auto* result = a.owner.result(session.request);
    if (!result) return;
    view.txComplete = result->transport.txComplete;
    view.deliveredUs = now;
    view.txAccepted = result->transport.txAccepted;
    view.executionUnknown = view.executionUnknown || result->executionUnknown;
    view.rxLength = std::min<std::size_t>(result->transport.rxLength, sizeof(view.rx));
    std::memcpy(view.rx, result->raw, view.rxLength);
    view.closureQualified = result->transport.closureQualified;
    view.closureEarliestUs = result->transport.closureEarliestUs;
    view.closureLatestUs = result->transport.closureLatestUs;
    const bool valid = result->outcome == Rtu::Outcome::SUCCESS && view.closureQualified &&
        view.closureLatestUs <= view.deadlineUs && result->transport.txComplete;
    a.owner.release(session.request);
    session.request = Rtu::RequestId();
    if (view.phase == Probe::MotionProfilePhase::RESTORE) {
        view.writeTxLength = view.txLength;
        std::memcpy(view.writeTx, view.tx, view.txLength);
        view.writeTxComplete = view.txComplete;
        view.writeTxAccepted = view.txAccepted;
        view.writeQualified = view.closureQualified;
        view.writeExecutionUnknown = view.executionUnknown;
        view.writeEarliestUs = view.closureEarliestUs;
        view.writeLatestUs = view.closureLatestUs;
        view.writeDeliveredUs = now;
        view.writeReplyLength = std::min(view.rxLength, sizeof(view.writeReply));
        std::memcpy(view.writeReply, view.rx, view.writeReplyLength);
    }
    if (!valid) {
        view.pending = false;
        view.error = "transaction";
        return;
    }
    if (view.phase == Probe::MotionProfilePhase::RESTORE) {
        view.phase = Probe::MotionProfilePhase::READBACK;
        view.rxLength = 0;
        view.txAccepted = 0;
        view.deliveredUs = 0;
        view.txComplete = view.closureQualified = false;
        view.closureEarliestUs = view.closureLatestUs = 0;
        if (!submitMotionProfile(a, false, now)) {
            view.pending = false;
            view.error = "readback_admission";
        }
        return;
    }
    ESS::PositionProfile parsed;
    if (!ESS::parsePositionProfile(view.rx, view.rxLength, view.address, a.configuration.wordOrder, parsed)) {
        view.pending = false;
        view.error = "decode";
        return;
    }
    uint16_t words[6] = {parsed.startSpeed, parsed.accelerationTime, parsed.decelerationTime, parsed.speed, 0, 0};
    if (!ESS::encodeUint32(parsed.targetBits, a.configuration.wordOrder, words + 4, 2)) {
        view.pending = false;
        view.error = "word_order";
        return;
    }
    std::memcpy(view.current, words, sizeof(words));
    if (!view.saved) {
        std::memcpy(view.original, words, sizeof(words));
        view.saved = true;
    }
    view.pending = false;
    if (view.phase == Probe::MotionProfilePhase::READBACK &&
        std::memcmp(view.original, view.current, sizeof(view.original))) {
        view.error = "readback_mismatch";
        return;
    }
    view.restored = view.phase == Probe::MotionProfilePhase::READBACK;
    view.ok = true;
}
