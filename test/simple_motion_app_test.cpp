// SPDX-License-Identifier: MIT
// Reuse the real application's platform/wire fixture; no alternative executor.
#define main advancedMoveFixtureMain
#include "move_app_test.cpp"
#undef main
namespace {
void simpleReadStep(uint16_t rawSpeed = 0, uint16_t subdivision = 1000, uint32_t position = 0) {
    for (unsigned i=0;i<100 && !app->simple.view.readOperationId;++i) step();
    const auto id=app->simple.view.readOperationId; assert(id);
    const auto kind=findRecord(*app,id)->read.kind;
    const auto token=findRecord(*app,id)->read.step;
    std::vector<uint8_t> reply;
    if (kind==ESS::ReadKind::CONFIG) {
        switch(token) {
        case 0: reply=registers(app->simple.view.address,{0,subdivision}); break;
        case 1: reply=registers(app->simple.view.address,{1,3,0}); break;
        case 2: reply=registers(app->simple.view.address,{0,0,0}); break;
        case 3: reply=registers(app->simple.view.address,{0,1,2,3,0}); break;
        case 4: reply=registers(app->simple.view.address,{3,4000}); break;
        default: assert(false);
        }
    } else {
        assert(kind==ESS::ReadKind::STATE);
        switch(token) {
        case 0: reply=registers(app->simple.view.address,{0,1}); break;
        case 1: reply=registers(app->simple.view.address,{0,0}); break;
        case 2: reply=registers(app->simple.view.address,{uint16_t(position>>16),uint16_t(position),rawSpeed}); break;
        default: assert(false);
        }
    }
    waitTx(id); assert(hardware.tx[1]==3);
    scheduleReply(std::max(hardware.writeStarted+hardware.tx.size()*87+1000,hardware.time+1000),reply);
    for(unsigned i=0;i<25000;++i) {
        step(); const auto* r=findRecord(*app,id);
        if(!r || r->read.step!=token || r->read.state!=ReadState::ACTIVE) return;
    }
    assert(false);
}
void prepareSimple(bool serviceAfterAdmission = true) {
    for(unsigned i=0;i<20 && app->simple.view.pending && app->simple.view.phase!=Probe::SimpleMotionPhase::PROFILE;++i)
        simpleReadStep();
    assert(app->simple.view.pending && app->simple.view.phase==Probe::SimpleMotionPhase::PROFILE);
    auto& profile=app->simple.view.settingsOnly ? app->simple.settingsProfile : app->motionProfile;
    if (!profile.view.pending) {
        if (serviceAfterAdmission) pump();
        else serviceSimpleMotion(*app,hardware.time);
        return;
    }
    const auto id=profile.request;
    for(unsigned i=0;i<25000 && !app->owner.txAccepted(id);++i) step();
    assert(app->owner.txAccepted(id) && hardware.tx[1]==3 && hardware.tx[3]==0x20);
    scheduleReply(std::max(hardware.writeStarted+hardware.tx.size()*87+1000,hardware.time+1000),registers(app->simple.view.address,{30,100,100,60,0,5000}));
    for(unsigned i=0;i<25000 && profile.view.pending;++i) step();
    if (serviceAfterAdmission) pump();
}
void completeSimple() {
    const auto child=app->simple.view.moveOperationId; assert(child);
    if (findRecord(*app,child)->move.request.setup != MoveSetup::USE_STORED) moveStep(child);
    moveStep(child);
    moveStep(child,registers(1,{0,4})); moveStep(child,registers(1,{0,1})); pump(1000);
}
void testSettingsColdReadRetainsActualValuesAndReclaims() {
    fresh(); command("@1 speed 90\n@2 accel 125\n@3 decel 175\n@4 settings\n");
    const auto first=app->simple.view.operationId;
    assert(first && app->simple.view.settingsOnly && !app->simple.view.moveAdmitted);
    prepareSimple(); pump(1000);
    const auto& result=*view(first).simpleMotion;
    assert(result.ok && result.delivered && !result.pending && result.settingsOnly);
    assert(hardware.writes==6 && !result.moveOperationId && !result.readOperationId);
    assert(!app->motionProfile.view.saved && !app->rememberedMoveGeneration);
    for(const auto& block : app->stateCache.blocks) assert(!block.valid); // No STATE transaction occurred.
    assert(result.configKnown && result.subdivision==1000 && result.encoderResolution==4000);
    assert(result.algorithm==3 && !result.algorithmKnown && result.wordOrderKnown);
    const uint16_t actual[]={30,100,100,60,0,5000};
    assert(result.profileKnown && !std::memcmp(result.profile,actual,sizeof(actual)));
    assert(result.profileEvidence==&app->simple.settingsProfile.view);
    assert(result.profileEvidence->ok && result.profileEvidence->closureQualified &&
        result.profileEvidence->txLength==8 && result.profileEvidence->rxLength==17);
    assert(result.speedRpm==90 && result.acceleration==125 && result.deceleration==175);
    assert(result.execution==ActionExecution::NOT_TRANSMITTED && !result.uncertain);
    assert(Serial.output.find("\"type\":\"motor_settings\"")!=std::string::npos);
    assert(Serial.output.find("\"subdivision\":1000")!=std::string::npos);
    assert(Serial.output.find("\"profile\":[30,100,100,60,0,5000]")!=std::string::npos);
    // Retained wrapper copies remain historical when the desired settings change.
    command("@5 speed 120\n"); assert(view(first).simpleMotion->speedRpm==90);
    command("@6 settings\n");
    const auto second=app->simple.view.operationId; assert(second!=first);
    Probe::ResultView gone; assert(!lookup(app,first,gone));
    assert(app->simple.view.phase==Probe::SimpleMotionPhase::CONFIG);
    prepareSimple(); pump(1000);
    assert(app->simple.view.ok && app->simple.view.delivered && hardware.writes==12);
    assert(app->simple.view.speedRpm==120 && app->simple.view.profile[3]==60);
    for(const auto& block : app->stateCache.blocks) assert(!block.valid);
    command("@7 moveby 100\n");
    assert(!app->simple.view.settingsOnly && app->simple.view.phase==Probe::SimpleMotionPhase::CONFIG);
    assert(!lookup(app,second,gone));
    prepareSimple(); completeSimple();
    assert(app->simple.view.ok && app->simple.view.move->request.speedRpm==120);
}
void testSettingsFailureRemainsExplicitAndOwned() {
    for(const bool failProfile : {false,true}) {
        fresh(); command("@1 settings\n");
        const auto wrapper=app->simple.view.operationId;
        if(failProfile) for(unsigned i=0;i<5;++i) simpleReadStep();
        const auto child=app->simple.view.readOperationId;
        const auto request=failProfile ? app->simple.settingsProfile.request : findRecord(*app,child)->requestId;
        for(unsigned i=0;i<25000 && !app->owner.txAccepted(request);++i) step();
        assert(app->owner.txAccepted(request) && hardware.tx[1]==3);
        scheduleReply(std::max(hardware.writeStarted+hardware.tx.size()*87+1000,hardware.time+1000),crc({1,0x83,2}));
        for(unsigned i=0;i<25000 && view(wrapper).pending;++i) step();
        pump(1000);
        const auto& result=*view(wrapper).simpleMotion;
        assert(result.settingsOnly && !result.ok && result.delivered && !result.pending);
        assert(result.configKnown==failProfile && !result.profileKnown && !result.moveAdmitted);
        assert(!result.moveOperationId && hardware.writes==(failProfile?6u:1u));
        for(const auto& block : app->stateCache.blocks) assert(!block.valid);
        if(!failProfile) assert(view(child).typedRead->status.code==Err::EXCEPTION);
        else {
            assert(result.profileEvidence==&app->simple.settingsProfile.view);
            assert(!result.profileEvidence->ok && result.profileEvidence->rxLength==5);
        }
        command("@2 settings\n");
        assert(app->simple.view.operationId!=wrapper);
        prepareSimple(); pump(1000);
        assert(app->simple.view.ok && app->simple.view.delivered);
    }
}
void testSettingsPreserveRestorationAfterConfigurationAndTargetChanges() {
    fresh(); command("@1 moveby 100\n"); prepareSimple(); completeSimple();
    assert(app->motionProfile.view.saved);
    unsigned char original[sizeof(app->motionProfile)];
    std::memcpy(original,&app->motionProfile,sizeof(original));
    command("@2 settings\n");
    for(unsigned i=0;i<5;++i) simpleReadStep(0,2000);
    assert(app->simple.view.phase==Probe::SimpleMotionPhase::PROFILE);
    prepareSimple(); pump(1000);
    assert(app->simple.view.ok && app->simple.view.subdivision==2000);
    assert(app->simple.settingsProfile.configuration.subdivision==2000);
    assert(!std::memcmp(original,&app->motionProfile,sizeof(original)));

    command("@3 settings\n");
    for(unsigned i=0;i<5;++i) simpleReadStep(0,2000);
    const auto wrapper=app->simple.view.operationId;
    const auto request=app->simple.settingsProfile.request;
    for(unsigned i=0;i<25000 && !app->owner.txAccepted(request);++i) step();
    assert(app->owner.txAccepted(request));
    scheduleReply(std::max(hardware.writeStarted+hardware.tx.size()*87+1000,hardware.time+1000),crc({1,0x83,2}));
    for(unsigned i=0;i<25000 && app->simple.view.pending;++i) step();
    pump(1000);
    assert(!app->simple.view.ok && app->simple.view.delivered);
    assert(!std::memcmp(original,&app->motionProfile,sizeof(original)));
    assert(release(app,wrapper)==Probe::Action::OK);
    assert(selectTarget(app,2)==Probe::Action::OK);
    command("@4 settings\n"); prepareSimple(); pump(1000);
    assert(app->simple.view.ok && app->simple.view.address==2 && app->simple.view.profileKnown);
    assert(!std::memcmp(original,&app->motionProfile,sizeof(original)));
}
void testSettingsPrivateProfileSettlesBeforeCancelledWrapperRelease() {
    fresh(); command("@1 settings\n");
    const auto wrapper=app->simple.view.operationId;
    for(unsigned i=0;i<5;++i) simpleReadStep();
    const auto profile=app->simple.settingsProfile.request;
    for(unsigned i=0;i<25000 && !app->owner.txAccepted(profile);++i) step();
    assert(app->owner.txAccepted(profile));
    assert(cancel(app,wrapper)==Probe::Action::OK && release(app,wrapper)==Probe::Action::BUSY);
    assert(view(wrapper).simpleMotion->profileEvidence->pending);
    scheduleReply(std::max(hardware.writeStarted+hardware.tx.size()*87+1000,hardware.time+1000),registers(1,{30,100,100,60,0,5000}));
    for(unsigned i=0;i<25000 && view(wrapper).pending;++i) step();
    pump(1000);
    assert(!view(wrapper).pending && !app->simple.view.ok && app->simple.view.delivered);
    assert(app->simple.view.outcome==ActionOutcome::CANCELLED && hardware.writes==6);
    assert(!app->simple.view.profileEvidence->pending && app->simple.view.profileEvidence->rxLength==17);
    assert(!app->motionProfile.view.saved && !app->rememberedMoveGeneration);
    assert(release(app,wrapper)==Probe::Action::OK && !app->simple.settingsProfile.request.owner);
}
void testSpeedBelowDriveStartHasSpecificFailure() {
    fresh(); command("@1 speed 1\n@2 moveby 100\n");
    assert(app->simple.desired.speedRpm==1);
    prepareSimple(); pump(1000);
    assert(!app->simple.view.ok && !app->simple.view.moveAdmitted && hardware.writes==9);
    assert(!std::strcmp(app->simple.view.error,"Requested speed is below the drive starting speed; see settings"));
}
void testSimpleAbsoluteMoveUsesOrdinaryAbsoluteTrigger() {
    fresh(); command("@1 moveto 100 steps\n");
    const auto wrapper=app->simple.view.operationId;
    assert(wrapper && !app->simple.view.relative);
    prepareSimple();
    const auto child=app->simple.view.moveOperationId;
    assert(child && app->simple.view.moveAdmitted);
    const auto& move=findRecord(*app,child)->move;
    assert(!move.request.position.relative && move.request.position.frame==CoordinateFrame::MOTOR);
    assert(move.prepared.endpointKnown && move.prepared.effectiveNative==100);
    assert(move.reference.nativeKnown && move.prepared.displacementKnown);
    moveStep(child); // Checked ordinary 0x0021/5 staging.
    assert(hardware.tx[1]==16 && hardware.tx[3]==0x21);
    moveStep(child); // Absolute-position bit plus position-start bit.
    assert(hardware.tx.size()==8 && hardware.tx[1]==6 && hardware.tx[3]==0x27);
    assert(hardware.tx[4]==0 && hardware.tx[5]==5);
    moveStep(child,registers(1,{0,4}));
    moveStep(child,registers(1,{0,1})); pump(1000);
    assert(!view(wrapper).pending && app->simple.view.ok && app->simple.view.delivered);
    assert(app->simple.view.runningObserved && app->simple.view.completion==ActionCompletion::OBSERVED);
    assert(app->simple.view.execution==ActionExecution::ACKNOWLEDGED);
}
void testBootDefaultsManufacturerRangeAndLongerMove() {
    fresh();
    assert(app->simple.desired.speedRpm==60 && app->simple.desired.acceleration==100 &&
        app->simple.desired.deceleration==100 && app->simple.desired.accelerationKnown && app->simple.desired.decelerationKnown);
    assert(app->axis.units.commandStepsPerMotorTurn.numerator==1000 &&
        app->axis.units.commandStepsPerMotorTurn.source==ScaleSource::ASSUMED);
    command("@1 speed 90\n@2 moveby 1 turn\n"); prepareSimple();
    const auto child=app->simple.view.moveOperationId; assert(child);
    auto& move=findRecord(*app,child)->move;
    assert(move.request.speedRpm==90 && move.prepared.effectiveNative==1000);
    assert(move.deadlineUs-move.startedUs==30000000 && move.options.pollIntervalUs==20000);
    moveStep(child); moveStep(child);
    const auto started=hardware.time;
    moveStep(child,registers(1,{0,4}));
    assert(move.runningObserved && move.options.pollIntervalUs==500000);
    for(unsigned i=0;i<13;++i) moveStep(child,registers(1,{0,4}));
    assert(hardware.time-started>5000000 && view(child).pending);
    moveStep(child,registers(1,{0,1})); pump(1000);
    assert(app->simple.view.ok && app->simple.view.delivered);

    command("@3 speed 3000\n"); assert(app->simple.desired.speedRpm==3000);
    command("@4 speed 3001\n"); assert(app->simple.desired.speedRpm==3000);
    command("@5 speed 0\n"); assert(app->simple.desired.speedRpm==0);
    command("@6 moveby 100\n"); prepareSimple();
    pump(1000);
    assert(!app->simple.view.pending && !app->simple.view.moveAdmitted);
}
void testColdSettingsReadPreparationAndRepeat() {
    fresh();
    command("@1 speed 50\n@2 accel 125\n@3 decel 175\n");
    assert(hardware.writes==0 && app->simple.desired.speedRpm==50);
    command("@4 moveby 100\n"); const auto wrapper=app->simple.view.operationId;
    assert(wrapper && view(wrapper).simpleMotion && axisReserved(*app,1));
    prepareSimple();
    const auto child=app->simple.view.moveOperationId;
    assert(child && child!=wrapper && app->simple.view.moveAdmitted);
    assert(findRecord(*app,child)->commandId == 4); // Full evidence retains the user's command correlation.
    const auto& move=findRecord(*app,child)->move;
    assert(move.words[0]==125 && move.words[1]==175 && move.words[2]==50 && move.words[4]==100);
    assert(app->motionProfile.view.current[1]==100 && app->motionProfile.view.original[1]==100);
    completeSimple();
    assert(app->simple.view.ok && app->simple.view.delivered && app->simple.view.runningObserved);
    assert(view(wrapper).simpleMotion && view(child).moveContext);
    command("@8 speed 45\n");
    assert(view(wrapper).simpleMotion->speedRpm==50 && app->simple.desired.speedRpm==45);
    command("@9 speed 50\n");
    command("@5 moveby 50\n");
    assert(app->simple.view.operationId!=wrapper && app->simple.view.pending);
    assert(!findRecord(*app,child)); Probe::ResultView missing; assert(!lookup(app,wrapper,missing));
    prepareSimple(); completeSimple(); assert(app->simple.view.ok);
    const auto before=hardware.writes;
    command("@6 motion stored\n@7 moveby 50\n");
    assert(app->simple.view.phase==Probe::SimpleMotionPhase::STATE);
    prepareSimple(); assert(app->simple.view.moveAdmitted);
    assert(app->simple.view.move->request.setup==MoveSetup::USE_STORED);
    completeSimple(); assert(app->simple.view.ok && hardware.writes==before+6);
}
void testUnknownScaleAndExplicitColdScale() {
    fresh(0,UnitConfig()); command("@1 moveby 36 deg\n"); prepareSimple(); pump(1000);
    assert(!app->simple.view.moveAdmitted && !app->simple.view.ok && app->simple.view.delivered);
    assert(hardware.writes==9); // Five config + three state + one profile, all FC03.
    const auto failed=app->simple.view.operationId;
    // A conversion rejected before staging must not require manual release.
    command("@3 stepsperturn 1000\n"); assert(hardware.writes==9);
    command("@4 moveby 36 deg\n"); assert(app->simple.view.operationId!=failed); prepareSimple();
    assert(app->simple.view.moveAdmitted);
    assert(app->simple.view.move->prepared.effectiveNative==100);
    assert(app->axis.units.commandStepsPerMotorTurn.source==ScaleSource::ASSUMED);
    completeSimple();
}
void testBootZeroSurvivesCompletedMovesAndInvalidatesOnLoss() {
    fresh(); command("@1 moveby 90 deg\n");
    for(unsigned i=0;i<8;++i) simpleReadStep(0,1000,49890);
    prepareSimple();
    assert(app->axis.originKnown && app->axis.originNative==49890);
    assert(app->axis.originSource==ScaleSource::ASSUMED && !app->bootOriginPending);
    completeSimple();
    assert(app->axis.originKnown && app->axis.originNative==49890);
    command("@2 moveto 0 deg\n"); prepareSimple();
    assert(app->simple.view.moveAdmitted && app->simple.view.move->prepared.effectiveNative==49890);
    completeSimple();
    // The same completed target must not retrigger a sub-arrival-window
    // correction merely because reported feedback differs by one increment.
    const unsigned writes = hardware.writes;
    command("@20 moveto 0 steps\n");
    for(unsigned i=0;i<3;++i) simpleReadStep(0,1000,49889);
    pump(1000);
    assert(app->simple.view.alreadyAtTarget && app->simple.view.ok && !app->simple.view.moveAdmitted);
    assert(hardware.writes == writes + 3); // Only the mandatory new STATE read.
    invalidateAxis(*app); // release/unknown motion/configuration loss cannot re-zero.
    command("@3 moveto 0 deg\n"); prepareSimple(); pump(1000);
    assert(!app->simple.view.moveAdmitted && !app->axis.originKnown);
    const auto rejected=app->simple.view.operationId;
    command("@4 moveby 90 deg\n"); prepareSimple();
    assert(app->simple.view.operationId!=rejected && app->simple.view.moveAdmitted);
    completeSimple();
}
void testRejectedAndAcceptedStopDuringPreparation() {
    fresh(); command("@1 moveby 100\n");
    ActionRequest stop; stop.kind=ActionKind::STOP; stop.stop.behavior=StopBehavior::CONFIGURED_DECELERATION;
    uint32_t stopId=0;
    assert(startAction(app,2,1,stop,stopId)==Probe::Action::UNAVAILABLE);
    assert(!app->simple.cancelled && app->simple.view.pending);
    simpleReadStep(); // First checked reply establishes the physical target.
    assert(startAction(app,3,1,stop,stopId)==Probe::Action::OK);
    assert(app->simple.cancelled && app->simple.view.interruptedByStop);
    // Settle the already admitted read (possibly cancelled unsent), then stop.
    for(unsigned i=0;i<25000 && app->simple.view.pending;++i) step();
    assert(!app->simple.view.pending && !app->simple.view.moveAdmitted);
    assert(app->simple.view.outcome==ActionOutcome::CANCELLED);
    assert(!app->simple.view.moveOperationId);
    actionStep(stopId); actionStep(stopId,registers(1,{0,1}));
    pump(); assert(!app->simple.view.moveAdmitted);
}
void testCancelledWrapperKeepsOwnerUntilReadSettles() {
    fresh(); command("@1 moveby 100\n"); const auto wrapper=app->simple.view.operationId;
    const auto read=app->simple.view.readOperationId; waitTx(read);
    assert(cancel(app,wrapper)==Probe::Action::OK);
    assert(view(wrapper).pending && release(app,wrapper)==Probe::Action::BUSY);
    scheduleReply(std::max(hardware.writeStarted+hardware.tx.size()*87+1000,hardware.time+1000),registers(1,{0,1000}));
    for(unsigned i=0;i<25000 && view(wrapper).pending;++i) step();
    pump(1000);
    assert(!view(wrapper).pending && !app->simple.view.moveAdmitted && !app->simple.view.interruptedByStop);
    assert(hardware.writes==1 && release(app,wrapper)==Probe::Action::OK);
}
void refreshStateForExplicitAxisConfiguration() {
    uint32_t id=0;
    assert(startTypedRead(app,50,1,ESS::ReadKind::STATE,id,false)==Probe::Action::OK);
    findRecord(*app,id)->simpleChild=true; // Fixture owns this ordinary read's delivery.
    for (const auto& response : {registers(1,{0,1}),registers(1,{0,0}),registers(1,{0,0,0})}) {
        const auto token=findRecord(*app,id)->read.step;
        waitTx(id);
        scheduleReply(std::max(hardware.writeStarted+hardware.tx.size()*87+1000,hardware.time+1000),response);
        for (unsigned i=0;i<25000 && findRecord(*app,id)->read.state==ReadState::ACTIVE &&
             findRecord(*app,id)->read.step==token;++i) step();
    }
    pump(); assert(findRecord(*app,id)->read.state==ReadState::SUCCEEDED);
    assert(release(app,id)==Probe::Action::OK);
}
void testExplicitAxisScaleSupersedesSimpleDeclaration() {
    for (const bool cli : {false, true}) {
        fresh(); command("@1 stepsperturn 1000\n@2 moveby 36 deg\n");
        prepareSimple(); completeSimple();
        const auto first=app->simple.view.operationId;
        refreshStateForExplicitAxisConfiguration();
        const auto before=hardware.writes;
        if (cli) command("@3 axis config set command 500\n");
        else {
            Probe::AxisCommand change; change.kind=Probe::AxisCommandKind::CONFIGURE;
            change.field=Probe::AxisField::COMMAND_SCALE; change.value=Rational(500);
            Probe::AxisView result; assert(axisCommand(app,change,result));
        }
        assert(hardware.writes==before && app->simple.desired.scaleKnown);
        assert(app->simple.desired.stepsPerTurn.numerator==500);
        assert(view(first).simpleMotion->stepsPerTurn.numerator==1000);
        command("@4 moveby 36 deg\n"); prepareSimple();
        assert(app->simple.view.moveAdmitted && app->simple.view.move->prepared.effectiveNative==50);
        completeSimple(); refreshStateForExplicitAxisConfiguration();
        if (cli) command("@5 axis config set command none\n");
        else {
            Probe::AxisCommand change; change.kind=Probe::AxisCommandKind::CONFIGURE;
            change.field=Probe::AxisField::COMMAND_SCALE; change.clear=true;
            Probe::AxisView result; assert(axisCommand(app,change,result));
        }
        assert(!app->simple.desired.scaleKnown && !app->axis.units.commandStepsPerMotorTurn.numerator);
        command("@6 moveby 36 deg\n"); prepareSimple(); pump(1000);
        assert(!app->simple.view.moveAdmitted && !app->simple.view.ok);
        assert(!app->axis.units.commandStepsPerMotorTurn.numerator);
    }
}
void testDesiredScaleDoesNotCrossTargets() {
    fresh(); command("@1 stepsperturn 1000\n@2 speed 50\n");
    assert(app->simple.desired.scaleKnown);
    assert(selectTarget(app,2)==Probe::Action::OK);
    Probe::SimpleMotionView settings;
    assert(simpleMotion(app,3,nullptr,settings)==Probe::Action::OK);
    assert(!settings.scaleKnown && settings.speedRpm==50 && hardware.writes==0);
}
void testFailedReadIsInspectableAndOwned() {
    fresh(); command("@1 moveby 100\n");
    const auto wrapper=app->simple.view.operationId, child=app->simple.view.readOperationId;
    waitTx(child);
    scheduleReply(std::max(hardware.writeStarted+hardware.tx.size()*87+1000,hardware.time+1000),crc({1,0x83,2}));
    for(unsigned i=0;i<25000 && view(wrapper).pending;++i) step();
    pump(1000);
    assert(!view(wrapper).pending && app->simple.view.readOperationId==child);
    assert(view(child).typedRead && view(child).typedRead->status.code==Err::EXCEPTION);
    assert(release(app,child)==Probe::Action::BUSY);
    assert(release(app,wrapper)==Probe::Action::OK && !findRecord(*app,child));
}
void testReportedArrivalWaitsForStoppedFeedback() {
    fresh(); command("@1 moveby 100\n");
    for(unsigned i=0;i<7;++i) simpleReadStep();
    simpleReadStep(4); // ARRIVED flag with residual speed is not stationary.
    assert(app->simple.view.phase==Probe::SimpleMotionPhase::STATE);
    assert(app->simple.view.pending && !app->simple.view.moveAdmitted && hardware.writes==8);
    prepareSimple(); assert(app->simple.view.moveAdmitted && hardware.writes==12);
    completeSimple(); assert(app->simple.view.ok);

    fresh(); command("@1 moveby 100\n");
    for(unsigned i=0;i<7;++i) simpleReadStep();
    simpleReadStep(4);
    app->simple.deadlineUs=hardware.time;
    serviceSimpleMotion(*app,hardware.time); pump(1000);
    assert(!app->simple.view.pending && !app->simple.view.moveAdmitted && hardware.writes==8);
}
void testRetainedChildAndBlockedTerminal() {
    fresh(); command("@1 moveby 100\n"); prepareSimple();
    const auto wrapper=app->simple.view.operationId, child=app->simple.view.moveOperationId;
    assert(release(app,child)==Probe::Action::BUSY);
    // Saturate the real application sink, then hold an ordinary console reply.
    Serial.writeCapacity=0;
    while(app->outputCount<OUTPUT_LINES) assert(emit(app,"blocked",7));
    for (const char c : std::string("@2 status\n")) app->console.feed(c);
    assert(app->console.outputPending());
    completeSimple();
    assert(!view(wrapper).pending && app->simple.view.ok && !app->simple.view.delivered);
    assert(view(child).moveContext && release(app,child)==Probe::Action::BUSY);
    Probe::SimpleMotionCommand next; next.kind=Probe::SimpleMotionCommandKind::MOVE_BY;
    next.position.value=Rational(10); Probe::SimpleMotionView unchanged;
    assert(simpleMotion(app,3,&next,unchanged)==Probe::Action::BUSY);
    assert(app->simple.view.operationId==wrapper);
    Serial.writeCapacity=64; pump(6000);
    assert(app->simple.view.delivered && view(child).moveContext);
    assert(release(app,wrapper)==Probe::Action::OK && !findRecord(*app,child));
}
void testBoundsAndUncertainResultNotReplayed() {
    fresh(); command("@1 moveby 2147483648\n"); prepareSimple(); pump(1000);
    assert(!app->simple.view.moveAdmitted && hardware.writes==9);
    fresh(); command("@1 moveby 100\n"); prepareSimple();
    const auto child=app->simple.view.moveOperationId;
    moveStep(child); waitTx(child);
    for(unsigned i=0;i<50000 && view(child).pending;++i) step();
    pump(1000); assert(app->simple.view.uncertain && !app->simple.view.ok);
    const auto previous=app->simple.view.operationId, writes=hardware.writes;
    const auto retained=*view(child).moveContext;
    command("@2 moveby 100\n");
    assert(app->simple.view.operationId==previous && hardware.writes==writes);
    command("@3 stats reset\n");
    assert(app->simple.view.uncertain && axisReserved(*app,1) && hardware.writes==writes);
    uint32_t recovery=0; assert(recover(app,4,recovery)==Probe::Action::OK);
    for(unsigned i=0;i<80000 && app->owner.recovering();++i) step();
    assert(!app->owner.needsRecovery() && axisReserved(*app,1));
    command("@5 moveby 100\n");
    assert(Serial.output.find("axis_conflict")!=std::string::npos);
    assert(app->simple.view.operationId==previous && hardware.writes==writes);
    assert(view(child).moveContext->uncertain && view(child).moveContext->outcome==retained.outcome);
    assert(view(child).moveContext->execution==retained.execution);
}
void testCancelledUnsentAndRejectedStageAllowNextSession() {
    for (const bool rejectStage : {false, true}) {
        fresh(); command("@1 moveby 100\n"); prepareSimple(false);
        const auto wrapper=app->simple.view.operationId, child=app->simple.view.moveOperationId;
        assert(child && !app->owner.txAccepted(findRecord(*app,child)->requestId));
        if (rejectStage) moveStep(child,crc({1,0x90,2}));
        else assert(cancel(app,wrapper)==Probe::Action::OK);
        pump(1000);
        assert(app->simple.view.delivered && !app->simple.view.ok);
        assert(app->simple.view.uncertain==rejectStage);
        if (rejectStage) {
            // FC10 exceptions do not establish atomic rejection of parameters.
            command("@20 moveby 50\n");
            assert(app->simple.view.operationId==wrapper && axisReserved(*app,1));
            assert(Serial.output.find("axis_conflict")!=std::string::npos);
            command("@21 stop fast\n"); const auto stopping=app->latestOperationId;
            actionStep(stopping); actionStep(stopping,registers(1,{0,1})); pump(1000);
        }
        assert(!axisReserved(*app,1));
        const auto retained=*view(child).moveContext;
        command("@2 moveby 50\n");
        assert(app->simple.view.operationId!=wrapper && app->simple.view.pending);
        assert(findRecord(*app,child) && !findRecord(*app,child)->simpleChild);
        assert(view(child).moveContext->outcome==retained.outcome);
        assert(view(child).moveContext->execution==retained.execution);
        Probe::ResultView gone; assert(!lookup(app,wrapper,gone));
        prepareSimple(); completeSimple();
        assert(app->simple.view.ok && release(app,child)==Probe::Action::OK);
    }
}
void testStoppedSimpleMoveRetainsFailureAndAllowsNextSession() {
    for (const bool triggerInFlight : {false, true}) {
        fresh(); command("@1 moveto 100\n"); prepareSimple();
        const auto wrapper=app->simple.view.operationId, child=app->simple.view.moveOperationId;
        moveStep(child);
        if (triggerInFlight) waitTx(child);
        else { moveStep(child); moveStep(child,registers(1,{0,4})); }
        command("@2 stop fast\n");
        const auto stopping=app->latestOperationId;
        assert(stopping!=wrapper && view(stopping).actionContext);
        if (triggerInFlight) moveStep(child);
        pump(1000);
        assert(app->simple.view.delivered && app->simple.view.interruptedByStop);
        const auto retained=*view(child).moveContext;
        assert(retained.uncertain && axisReserved(*app,1));
        command("@3 moveby 50\n"); assert(app->simple.view.operationId==wrapper);
        actionStep(stopping); // A checked stop echo still does not prove stopping.
        command("@4 moveby 50\n"); assert(app->simple.view.operationId==wrapper);
        actionStep(stopping,registers(1,{0,1})); pump(1000);
        assert(!axisReserved(*app,1));
        assert(app->axis.originKnown && app->axis.originNative==0);
        command("@5 moveto 50\n");
        assert(app->simple.view.operationId!=wrapper && app->simple.view.pending);
        assert(view(child).moveContext->uncertain && view(child).interruptedByStop);
        assert(view(child).moveContext->execution==retained.execution);
        assert(view(child).moveContext->outcome==retained.outcome);
        assert(view(child).moveContext->completion==retained.completion);
        prepareSimple();
        assert(app->simple.view.move->prepared.effectiveNative==50);
        completeSimple();
        assert(app->simple.view.ok && release(app,child)==Probe::Action::OK);
    }
}
void testRetainedFailedMovesReportCapacityAndCanBeReleased() {
    fresh();
    for (unsigned i=0;i<REQUEST_CAPACITY;++i) {
        command("@"+std::to_string(i+1)+" moveby 100\n"); prepareSimple(false);
        assert(cancel(app,app->simple.view.operationId)==Probe::Action::OK); pump(1000);
        assert(app->simple.view.delivered && !axisReserved(*app,1));
    }
    const auto wrapper=app->simple.view.operationId;
    Probe::SimpleMotionCommand next; next.kind=Probe::SimpleMotionCommandKind::MOVE_BY;
    next.position.value=Rational(10); Probe::SimpleMotionView result;
    assert(simpleMotion(app,50,&next,result)==Probe::Action::RESULTS_FULL);
    assert(app->simple.view.operationId==wrapper && app->simple.view.delivered);
    const auto old=app->records[0].operationId;
    assert(old && old!=app->simple.view.moveOperationId && release(app,old)==Probe::Action::OK);
    command("@51 moveby 10\n"); prepareSimple(); completeSimple();
    assert(app->simple.view.ok);
}
void testFailedStopAndBlockedResultDoNotReleaseSimpleSession() {
    fresh(); command("@1 moveby 100\n"); prepareSimple();
    const auto wrapper=app->simple.view.operationId, child=app->simple.view.moveOperationId;
    moveStep(child); moveStep(child); moveStep(child,registers(1,{0,4}));
    command("@2 stop fast\n"); const auto failedStop=app->latestOperationId;
    actionStep(failedStop,crc({1,0x86,2})); pump(1000);
    assert(view(failedStop).actionContext->completion!=ActionCompletion::OBSERVED);
    assert(app->simple.view.delivered && app->simple.view.uncertain && axisReserved(*app,1));
    command("@3 moveby 50\n"); assert(app->simple.view.operationId==wrapper);
    assert(Serial.output.find("axis_conflict")!=std::string::npos);
    assert(release(app,failedStop)==Probe::Action::OK);
    command("@4 stop fast\n"); const auto stopped=app->latestOperationId;
    actionStep(stopped); actionStep(stopped,registers(1,{0,1})); pump(1000);
    assert(!axisReserved(*app,1));
    const auto retained=*view(child).moveContext;
    command("@5 moveby 50\n"); prepareSimple(false);
    const auto nextWrapper=app->simple.view.operationId;
    const auto unsent=app->simple.view.moveOperationId;
    Serial.writeCapacity=0;
    while(app->outputCount<OUTPUT_LINES) assert(emit(app,"blocked",7));
    for(const char c : std::string("@6 status\n")) app->console.feed(c);
    assert(cancel(app,nextWrapper)==Probe::Action::OK); pump(1000);
    assert(!app->simple.view.pending && !app->simple.view.delivered && !axisReserved(*app,1));
    Probe::SimpleMotionCommand next; next.kind=Probe::SimpleMotionCommandKind::MOVE_BY;
    next.position.value=Rational(10); Probe::SimpleMotionView result;
    assert(simpleMotion(app,7,&next,result)==Probe::Action::BUSY);
    assert(release(app,nextWrapper)==Probe::Action::BUSY && findRecord(*app,unsent));
    Serial.writeCapacity=64; pump(6000);
    assert(app->simple.view.delivered);
    // The displayed child ID also releases its current delivered wrapper.
    assert(release(app,unsent)==Probe::Action::OK && !app->simple.view.operationId);
    command("@8 moveby 10\n"); prepareSimple(); completeSimple();
    assert(app->simple.view.ok && app->simple.view.operationId!=nextWrapper);
    assert(view(child).moveContext->outcome==retained.outcome && view(child).moveContext->uncertain);
    assert(release(app,child)==Probe::Action::OK);
}
}
int main() {
    testCancelledUnsentAndRejectedStageAllowNextSession();
    testStoppedSimpleMoveRetainsFailureAndAllowsNextSession();
    testRetainedFailedMovesReportCapacityAndCanBeReleased();
    testFailedStopAndBlockedResultDoNotReleaseSimpleSession();
    testBootZeroSurvivesCompletedMovesAndInvalidatesOnLoss();
    testColdSettingsReadPreparationAndRepeat();
    testSettingsColdReadRetainsActualValuesAndReclaims();
    testSettingsFailureRemainsExplicitAndOwned();
    testSettingsPreserveRestorationAfterConfigurationAndTargetChanges();
    testSpeedBelowDriveStartHasSpecificFailure();
    testSettingsPrivateProfileSettlesBeforeCancelledWrapperRelease();
    testBootDefaultsManufacturerRangeAndLongerMove();
    testSimpleAbsoluteMoveUsesOrdinaryAbsoluteTrigger();
    testUnknownScaleAndExplicitColdScale();
    testRejectedAndAcceptedStopDuringPreparation();
    testCancelledWrapperKeepsOwnerUntilReadSettles();
    testBoundsAndUncertainResultNotReplayed();
    testRetainedChildAndBlockedTerminal();
    testReportedArrivalWaitsForStoppedFeedback();
    testDesiredScaleDoesNotCrossTargets();
    testExplicitAxisScaleSupersedesSimpleDeclaration();
    testFailedReadIsInspectableAndOwned();
    std::puts("Simple motion application tests passed");
}
