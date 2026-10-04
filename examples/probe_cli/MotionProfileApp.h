// SPDX-License-Identifier: MIT
// Included inside the standalone application's private namespace.
bool motionProfileEndpoint(const App& a) {
    const auto& f = a.motionProfile;
    return f.saved && f.address == a.axis.target.address &&
        f.serialGeneration == a.serial.generation && a.motionProfileBinding == a.bindingGeneration &&
        a.configuration.operationId && Probe::sameTarget(a.configuration.target,a.axis.target) &&
        std::memcmp(&a.motionProfileConfig,&a.configuration.raw,sizeof(a.motionProfileConfig)) == 0;
}
bool motionProfileBound(const App& a) { return motionProfileEndpoint(a) && a.motionProfile.generation == a.axis.generation; }
bool motionProfileStationary(const App& a, uint64_t now) {
    const auto& motion = a.stateCache.blocks[static_cast<uint8_t>(ESS::StateBlock::MOTION)];
    return Probe::fresh(motion,a.axis.target,now,5000000) && !motion.value.rawAlarm &&
        !motion.value.alarmFlag && !motion.value.running && !motion.value.positiveSoftLimit && !motion.value.negativeSoftLimit;
}
// The example bounds an unreferenced absolute command using fresh raw feedback.
// This application envelope does not manufacture a command-coordinate reference
// or claim a physical displacement; the core permits general native targets.
bool absoluteMoveInEnvelope(const App& a, const MotorControlRS::MoveRequest& request, uint64_t now) {
    const auto& value=request.position.value;
    if (value.numerator<0 || !value.denominator) return false;
    const uint64_t numerator=static_cast<uint64_t>(value.numerator);
    const uint64_t integral=numerator/value.denominator;
    if (integral>250 || (integral==250 && numerator%value.denominator)) return false;
    const auto& feedback=a.stateCache.blocks[static_cast<uint8_t>(ESS::StateBlock::FEEDBACK)];
    return !request.position.relative && request.position.frame == MotorControlRS::CoordinateFrame::NATIVE &&
        request.position.unit == MotorControlRS::PositionUnit::STEPS && motionProfileBound(a) && motionProfileStationary(a,now) &&
        Probe::fresh(feedback,a.axis.target,now,5000000) && feedback.value.pairKnown &&
        !feedback.value.rawSpeed && feedback.value.rawPosition<=250;
}
bool moveRequirements(const App& a, const MotorControlRS::MoveRequest& request,
                                uint64_t now, ESS::MovePrerequisites& out) {
    using namespace MotorControlRS;
    if (!motionProfileBound(a) || !a.motionProfile.ok || a.motionProfile.pending ||
        !a.motionProfile.closureQualified || now < a.motionProfile.closureEarliestUs || now-a.motionProfile.closureEarliestUs>30000000 ||
        request.position.wrapped || request.position.frame != CoordinateFrame::NATIVE ||
        request.position.unit != PositionUnit::STEPS || (request.position.relative ? request.position.value.numerator <= 0 : request.position.value.numerator < 0) ||
        request.position.basis != RelativeBasis::ACTUAL || !motionProfileStationary(a,now)) return false;
    const auto& raw = a.configuration.raw;
    const auto& io = a.stateCache.blocks[static_cast<uint8_t>(ESS::StateBlock::IO)];
    const auto& motion = a.stateCache.blocks[static_cast<uint8_t>(ESS::StateBlock::MOTION)];
    if (!Probe::fresh(io,a.axis.target,now,5000000) || io.value.unknownInputBits ||
        raw.softLimitEnable != 0 || !a.configuration.wordOrderKnown ||
        motion.value.released || a.motionProfile.current[0] > request.speedRpm ||
        a.motionProfile.current[1] > 2000 || a.motionProfile.current[2] > 2000) return false;
    // Existing inactive origin/limit assignments remain intact. Unconnected does
    // not establish inactivity; the checked logical input report does.
    for (uint8_t i=0;i<4;++i)
        if (raw.inputFunctions[i] > 3 || (raw.inputFunctions[i] && io.value.inputs[i])) return false;
    out = ESS::MovePrerequisites(); out.target = a.axis.target; out.configurationGeneration = a.axis.generation;
    // Finite positive native words use the documented serial position command.
    // Native words require no physical scale or undocumented algorithm claim.
    // Physical conversions retain their separate scale/reference requirements.
    out.commandUnitsVerified = out.relativeBasisVerified = true;
    out.configuredRampVerified = out.serialInputsPermit = out.readinessQualified = true;
    out.accelerationTime = a.motionProfile.current[1]; out.decelerationTime = a.motionProfile.current[2];
    out.startSpeedKnown = true; out.startSpeed = a.motionProfile.current[0];
    if (!request.position.relative) {
        if (!absoluteMoveInEnvelope(a,request,now)) return false;
    }
    out.maximumAgeUs = 3000000;
    return true;
}
bool submitMotionProfile(App& a, bool write, uint64_t now) {
    auto& f=a.motionProfile; Rtu::BusRequest request;
    if (write) {
        ESS::PositionProfile original;
        original.startSpeed=f.original[0]; original.accelerationTime=f.original[1];
        original.decelerationTime=f.original[2]; original.speed=f.original[3];
        if (!ESS::decodeUint32(f.original+4,2,a.configuration.wordOrder,original.targetBits)) return false;
        request.wire.length=ESS::buildWritePositionProfile(f.address,original,a.configuration.wordOrder,f.tx,sizeof(f.tx));
    } else request.wire.length=ESS::buildReadPositionProfile(f.address,f.tx,sizeof(f.tx));
    f.txLength=request.wire.length; request.wire.bytes=f.tx;
    request.wire.replyLength=write?ESS::WRITE_RESPONSE_LEN:ESS::expectedReadRegistersLen(6);
    request.wire.responseTimeoutUs=a.serial.timing.responseTimeoutUs; request.wire.replyGapUs=a.serial.timing.replyGapUs;
    request.wire.deadlineUs=a.motionProfileDeadline;
    request.expected.address=f.address; request.expected.target=f.address;
    request.expected.targetGeneration=a.bindingGeneration; request.expected.function=write?16:3;
    request.expected.first=write?0x0021:0x0020; request.expected.count=write?5:6;
    request.validator=Rtu::essValidator();
    return request.wire.length && a.owner.admit(request,now,a.motionProfileRequest)==Rtu::BusAdmission::ACCEPTED;
}
Probe::Action motionProfileCommand(void* context, Probe::MotionProfileCommand command, Probe::MotionProfileView& out) {
    App& a=*static_cast<App*>(context); auto& f=a.motionProfile;
    out=f;
    if (command==Probe::MotionProfileCommand::INSPECT) return Probe::Action::OK;
    if (!platformReady || !a.serial.activeKnown || a.serial.blocked || a.owner.needsRecovery() || uart.needsRecovery())
        return Probe::Action::RECOVERY_REQUIRED;
    if (f.pending || a.owner.active() || a.owner.pending() || a.owner.configurationOwned() || a.owner.commissioningOwned() ||
        reading(a) || acting(a) || a.monitorState.settings.enabled) return Probe::Action::BUSY;
    if (!a.configuration.operationId || !Probe::sameTarget(a.configuration.target,a.axis.target) ||
        !(a.knownTargets[a.axis.target.address/8] & (1U<<(a.axis.target.address%8)))) return Probe::Action::UNAVAILABLE;
    const bool restore=command==Probe::MotionProfileCommand::RESTORE;
    if (f.saved && !motionProfileEndpoint(a)) return Probe::Action::UNAVAILABLE; // Preserve the original, never silently replace it.
    if (restore && (!motionProfileEndpoint(a) || !motionProfileStationary(a,nowUs()) || axisReserved(a,a.axis.target.address)))
        return Probe::Action::UNAVAILABLE;
    if (!f.saved) a.motionProfileConfig=a.configuration.raw;
    f.address=a.axis.target.address; f.generation=a.axis.generation; f.serialGeneration=a.serial.generation;
    a.motionProfileBinding=a.bindingGeneration; a.motionProfileDeadline=nowUs()+REQUEST_US; f.deadlineUs=a.motionProfileDeadline;
    f.phase=restore?2:1; f.pending=true; f.ok=false; f.restored=false; f.error="none";
    f.rxLength=0; f.writeReplyLength=0; f.writeTxLength=0; f.writeTxAccepted=0;
    f.txComplete=f.writeTxComplete=false; f.writeQualified=false; f.writeExecutionUnknown=false; f.writeEarliestUs=f.writeLatestUs=f.writeDeliveredUs=0; f.deliveredUs=0; f.executionUnknown=false; f.txAccepted=0;
    f.closureQualified=false; f.closureEarliestUs=f.closureLatestUs=0;
    if (!submitMotionProfile(a,restore,nowUs())) { f.pending=false; f.error="admission"; out=f; return Probe::Action::FAILED; }
    out=f; return Probe::Action::OK;
}
void serviceMotionProfile(App& a, uint64_t now) {
    auto& f=a.motionProfile; if (!f.pending) return;
    const auto* result=a.owner.result(a.motionProfileRequest); if (!result) return;
    f.txComplete=result->transport.txComplete; f.deliveredUs=now; f.txAccepted=result->transport.txAccepted; f.executionUnknown=f.executionUnknown || result->executionUnknown;
    f.rxLength=std::min<std::size_t>(result->transport.rxLength,sizeof(f.rx)); std::memcpy(f.rx,result->raw,f.rxLength);
    f.closureQualified=result->transport.closureQualified;
    f.closureEarliestUs=result->transport.closureEarliestUs; f.closureLatestUs=result->transport.closureLatestUs;
    const bool valid=result->outcome==Rtu::Outcome::SUCCESS && f.closureQualified &&
        f.closureLatestUs<=a.motionProfileDeadline && result->transport.txComplete;
    a.owner.release(a.motionProfileRequest); a.motionProfileRequest=Rtu::RequestId();
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
        if (!submitMotionProfile(a,false,now)) { f.pending=false; f.error="readback_admission"; }
        return;
    }
    ESS::PositionProfile parsed;
    if (!ESS::parsePositionProfile(f.rx,f.rxLength,f.address,a.configuration.wordOrder,parsed)) {
        f.pending=false; f.error="decode"; return;
    }
    uint16_t words[6]={parsed.startSpeed,parsed.accelerationTime,parsed.decelerationTime,parsed.speed,0,0};
    if (!ESS::encodeUint32(parsed.targetBits,a.configuration.wordOrder,words+4,2)) {
        f.pending=false; f.error="word_order"; return;
    }
    std::memcpy(f.current,words,sizeof(words));
    if (!f.saved) { std::memcpy(f.original,words,sizeof(words)); f.saved=true; }
    f.pending=false;
    if (f.phase==3 && std::memcmp(f.original,f.current,sizeof(f.original))) { f.error="readback_mismatch"; return; }
    f.restored=f.phase==3; f.ok=true;
}
