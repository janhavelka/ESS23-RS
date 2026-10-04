// SPDX-License-Identifier: MIT
// Included inside the standalone application's private namespace.
bool functionalActionAllowed(const MotorControlRS::ActionRequest& request) {
    using MotorControlRS::ActionKind;
    return MOTORCONTROLRS_FUNCTIONAL_BENCH && (request.kind == ActionKind::STOP ||
        request.kind == ActionKind::ENABLE || request.kind == ActionKind::RELEASE);
}
MotorControlRS::ActionOptions functionalActionOptions(const MotorControlRS::ActionRequest& request) {
    MotorControlRS::ActionOptions options;
    options.allowUnconfirmedWriteObservation = !actionTimingQualified && functionalActionAllowed(request);
    return options;
}
bool functionalEndpoint(const App& a) {
    const auto& f = a.functional;
    return f.saved && f.address == a.axis.target.address &&
        f.serialGeneration == a.serial.generation && a.functionalBinding == a.bindingGeneration &&
        a.configuration.operationId && Probe::sameTarget(a.configuration.target,a.axis.target) &&
        std::memcmp(&a.functionalConfig,&a.configuration.raw,sizeof(a.functionalConfig)) == 0;
}
bool functionalBound(const App& a) { return functionalEndpoint(a) && a.functional.generation == a.axis.generation; }
bool functionalStationary(const App& a, uint64_t now) {
    const auto& motion = a.stateCache.blocks[static_cast<uint8_t>(ESS::StateBlock::MOTION)];
    return Probe::fresh(motion,a.axis.target,now,5000000) && !motion.value.rawAlarm &&
        !motion.value.alarmFlag && !motion.value.running && !motion.value.positiveSoftLimit && !motion.value.negativeSoftLimit;
}
// A literal device-native zero command is a bounded functional experiment.
// The raw position bounds admission only; it is not an exact command-coordinate
// witness, a physical displacement, a calibration or a host origin.
bool functionalZeroEnvelope(const App& a, uint64_t now) {
    const auto& feedback=a.stateCache.blocks[static_cast<uint8_t>(ESS::StateBlock::FEEDBACK)];
    return MOTORCONTROLRS_FUNCTIONAL_BENCH && functionalBound(a) && functionalStationary(a,now) &&
        Probe::fresh(feedback,a.axis.target,now,5000000) && feedback.value.pairKnown &&
        !feedback.value.rawSpeed && feedback.value.rawPosition>0 && feedback.value.rawPosition<=250;
}
bool functionalMovePrerequisites(const App& a, const MotorControlRS::MoveRequest& request,
                                uint64_t now, ESS::MovePrerequisites& out) {
    using namespace MotorControlRS;
    if (!MOTORCONTROLRS_FUNCTIONAL_BENCH || !functionalBound(a) || !a.functional.ok || a.functional.pending ||
        !a.functional.closureQualified || now < a.functional.closureEarliestUs || now-a.functional.closureEarliestUs>30000000 ||
        request.position.wrapped || request.position.frame != CoordinateFrame::NATIVE ||
        request.position.unit != PositionUnit::STEPS || (request.position.relative ? request.position.value.numerator <= 0 : request.position.value.numerator != 0) ||
        request.position.basis != RelativeBasis::ACTUAL || !functionalStationary(a,now)) return false;
    const auto& raw = a.configuration.raw;
    const auto& io = a.stateCache.blocks[static_cast<uint8_t>(ESS::StateBlock::IO)];
    const auto& motion = a.stateCache.blocks[static_cast<uint8_t>(ESS::StateBlock::MOTION)];
    if (!Probe::fresh(io,a.axis.target,now,5000000) || io.value.unknownInputBits ||
        raw.softLimitEnable != 0 || raw.subdivision != 1000 || !a.configuration.wordOrderKnown ||
        motion.value.released || a.functional.current[0] > request.speedRpm ||
        a.functional.current[1] > 2000 || a.functional.current[2] > 2000) return false;
    // Existing inactive origin/limit assignments remain intact. Unconnected does
    // not establish inactivity; the checked logical input report does.
    for (uint8_t i=0;i<4;++i)
        if (raw.inputFunctions[i] > 3 || (raw.inputFunctions[i] && io.value.inputs[i])) return false;
    out = ESS::MovePrerequisites(); out.target = a.axis.target; out.configurationGeneration = a.axis.generation;
    // Finite positive native words use the documented serial position command.
    // This scoped experiment makes no physical scale, negative encoding or
    // undocumented algorithm claim. All physical-unit routes remain closed.
    out.commandUnitsVerified = out.relativeBasisVerified = true;
    out.configuredRampVerified = out.serialInputsPermit = out.readinessQualified = true;
    out.accelerationTime = a.functional.current[1]; out.decelerationTime = a.functional.current[2];
    out.startSpeedKnown = true; out.startSpeed = a.functional.current[0];
    if (!request.position.relative) {
        if (!functionalZeroEnvelope(a,now)) return false;
        out.nativeZeroEnvelopeVerified = true;
    }
    out.maximumAgeUs = 3000000;
    return true;
}
bool submitFunctional(App& a, bool write, uint64_t now) {
    auto& f=a.functional; Rtu::BusRequest request;
    request.wire.length = write ? ESS::buildWriteMultipleRegisters(f.address,0x0021,f.original+1,5,f.tx,sizeof(f.tx)) :
        ESS::buildReadRegisters(f.address,0x0020,6,f.tx,sizeof(f.tx));
    f.txLength=request.wire.length; request.wire.bytes=f.tx;
    request.wire.replyLength=write?ESS::WRITE_RESPONSE_LEN:ESS::expectedReadRegistersLen(6);
    request.wire.responseTimeoutUs=a.serial.timing.responseTimeoutUs; request.wire.replyGapUs=a.serial.timing.replyGapUs;
    request.wire.deadlineUs=a.functionalDeadline;
    request.expected.address=f.address; request.expected.target=f.address;
    request.expected.targetGeneration=a.bindingGeneration; request.expected.function=write?16:3;
    request.expected.first=write?0x0021:0x0020; request.expected.count=write?5:6;
    request.validator=Rtu::essValidator();
    return request.wire.length && a.owner.admit(request,now,a.functionalRequest)==Rtu::BusAdmission::ACCEPTED;
}
Probe::Action functionalCommand(void* context, Probe::FunctionalCommand command, Probe::FunctionalView& out) {
    App& a=*static_cast<App*>(context); auto& f=a.functional; f.enabled=MOTORCONTROLRS_FUNCTIONAL_BENCH;
    out=f;
    if (command==Probe::FunctionalCommand::INSPECT) return Probe::Action::OK;
    if (!MOTORCONTROLRS_FUNCTIONAL_BENCH) return Probe::Action::UNAVAILABLE;
    if (!platformReady || !a.serial.activeKnown || a.serial.blocked || a.owner.needsRecovery() || uart.needsRecovery())
        return Probe::Action::RECOVERY_REQUIRED;
    if (f.pending || a.owner.active() || a.owner.pending() || a.owner.configurationOwned() || a.owner.commissioningOwned() ||
        reading(a) || acting(a) || a.monitorState.settings.enabled) return Probe::Action::BUSY;
    if (!a.configuration.operationId || !Probe::sameTarget(a.configuration.target,a.axis.target) ||
        !(a.knownTargets[a.axis.target.address/8] & (1U<<(a.axis.target.address%8)))) return Probe::Action::UNAVAILABLE;
    const bool restore=command==Probe::FunctionalCommand::RESTORE;
    if (f.saved && !functionalEndpoint(a)) return Probe::Action::UNAVAILABLE; // Preserve the original, never silently replace it.
    if (restore && (!functionalEndpoint(a) || !functionalStationary(a,nowUs()) || axisReserved(a,a.axis.target.address)))
        return Probe::Action::UNAVAILABLE;
    if (!f.saved) a.functionalConfig=a.configuration.raw;
    f.address=a.axis.target.address; f.generation=a.axis.generation; f.serialGeneration=a.serial.generation;
    a.functionalBinding=a.bindingGeneration; a.functionalDeadline=nowUs()+REQUEST_US; f.deadlineUs=a.functionalDeadline;
    f.phase=restore?2:1; f.pending=true; f.ok=false; f.restored=false; f.error="none";
    f.rxLength=0; f.writeReplyLength=0; f.writeTxLength=0; f.writeTxAccepted=0;
    f.txComplete=f.writeTxComplete=false; f.writeQualified=false; f.writeExecutionUnknown=false; f.writeEarliestUs=f.writeLatestUs=f.writeDeliveredUs=0; f.deliveredUs=0; f.executionUnknown=false; f.txAccepted=0;
    f.closureQualified=false; f.closureEarliestUs=f.closureLatestUs=0;
    if (!submitFunctional(a,restore,nowUs())) { f.pending=false; f.error="admission"; out=f; return Probe::Action::FAILED; }
    out=f; return Probe::Action::OK;
}
void serviceFunctional(App& a, uint64_t now) {
    auto& f=a.functional; if (!f.pending) return;
    const auto* result=a.owner.result(a.functionalRequest); if (!result) return;
    f.txComplete=result->transport.txComplete; f.deliveredUs=now; f.txAccepted=result->transport.txAccepted; f.executionUnknown=f.executionUnknown || result->executionUnknown;
    f.rxLength=std::min<std::size_t>(result->transport.rxLength,sizeof(f.rx)); std::memcpy(f.rx,result->raw,f.rxLength);
    f.closureQualified=result->transport.closureQualified;
    f.closureEarliestUs=result->transport.closureEarliestUs; f.closureLatestUs=result->transport.closureLatestUs;
    const bool valid=result->outcome==Rtu::Outcome::SUCCESS && f.closureQualified &&
        f.closureLatestUs<=a.functionalDeadline && result->transport.txComplete;
    a.owner.release(a.functionalRequest); a.functionalRequest=Rtu::RequestId();
    if (f.phase==2) {
        f.writeTxLength=f.txLength; std::memcpy(f.writeTx,f.tx,f.txLength);
        f.writeTxComplete=f.txComplete; f.writeTxAccepted=f.txAccepted; f.writeQualified=f.closureQualified; f.writeExecutionUnknown=f.executionUnknown;
        f.writeEarliestUs=f.closureEarliestUs; f.writeLatestUs=f.closureLatestUs; f.writeDeliveredUs=now;
        f.writeReplyLength=std::min(f.rxLength,sizeof(f.writeReply)); std::memcpy(f.writeReply,f.rx,f.writeReplyLength);
    }
    if (!valid) { f.pending=false; f.error="transaction"; return; }
    if (f.phase==2) {
        f.phase=3; f.rxLength=0; f.txAccepted=0; f.txComplete=false; f.deliveredUs=0;
        f.closureQualified=false; f.closureEarliestUs=f.closureLatestUs=0;
        if (!submitFunctional(a,false,now)) { f.pending=false; f.error="readback_admission"; }
        return;
    }
    std::size_t count=0; uint16_t words[6]={};
    if (!ESS::parseRegisters(f.rx,f.rxLength,f.address,6,words,6,count)) {
        f.pending=false; f.error="decode"; return;
    }
    std::memcpy(f.current,words,sizeof(words));
    if (!f.saved) { std::memcpy(f.original,words,sizeof(words)); f.saved=true; }
    f.pending=false;
    if (f.phase==3 && std::memcmp(f.original,f.current,sizeof(f.original))) { f.error="readback_mismatch"; return; }
    f.restored=f.phase==3; f.ok=true;
}
