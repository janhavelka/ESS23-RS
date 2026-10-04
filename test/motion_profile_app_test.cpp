// SPDX-License-Identifier: MIT
#include "../examples/probe_cli/main.cpp"
#include <cassert>
#include <cstdlib>
#include <string>
#include <vector>
FakeSerial Serial;
namespace {
using namespace MotorControlRS;
void step() { advanceHardware(hardware.time+10); loop(); }
std::vector<uint8_t> crc(std::vector<uint8_t> v) {
    const auto c=ESS::calcCrc16(v.data(),v.size()); v.push_back(c); v.push_back(c>>8); return v;
}
std::vector<uint8_t> words(std::initializer_list<uint16_t> w) {
    std::vector<uint8_t> v={1,3,static_cast<uint8_t>(2*w.size())};
    for (auto x:w) { v.push_back(x>>8); v.push_back(x); } return crc(v);
}
void refresh() {
    auto& c=app->configuration; c.operationId=1; c.target=app->axis.target; c.wordOrderKnown=true;
    c.raw.subdivision=1000; c.raw.inputFunctions[0]=1; c.raw.inputFunctions[1]=2; c.raw.inputFunctions[2]=3;
    for (auto& b:app->stateCache.blocks) {
        b.valid=true; b.value.target=app->axis.target; b.observedEarliestUs=b.observedLatestUs=hardware.time;
        b.invalidatedUs=0;
    }
    app->stateCache.blocks[2].value.pairKnown=true;
    app->stateCache.blocks[0].value.rawMotion=1; app->knownTargets[0]|=2;
}
void reply(const Rtu::RequestId& id, const std::vector<uint8_t>& bytes) {
    for (unsigned i=0;i<25000 && !app->owner.txAccepted(id);++i) step();
    assert(app->owner.txAccepted(id));
    scheduleReply(std::max(hardware.writeStarted+hardware.tx.size()*87+1000,hardware.time+1000),bytes);
}
void fixtureReply(const std::vector<uint8_t>& bytes) {
    const auto phase=app->motionProfile.view.phase; reply(app->motionProfile.request,bytes);
    for (unsigned i=0;i<25000 && app->motionProfile.view.pending && app->motionProfile.view.phase==phase;++i) step();
    assert(!app->motionProfile.view.pending || app->motionProfile.view.phase!=phase);
}
}
int main() {
    writeResponseConfirmed=false; // Alternate unknown-echo topology exercises normal API observation.
    resetHardware(); setup(); assert(app); hardware.txCharacterUs=87;
    assert(uart.startCapture(20,timing().holdUs)); refresh();
    Probe::MotionProfileView v;
    v.address = 77;
    unsigned char savedProfile[sizeof(app->motionProfile)];
    std::memcpy(savedProfile,&app->motionProfile,sizeof(savedProfile));
    const auto writesBeforeInvalidCommand = hardware.writes;
    assert(motionProfileCommand(app,static_cast<Probe::MotionProfileCommand>(255),v)==Probe::Action::INVALID);
    assert(v.address==77 && hardware.writes==writesBeforeInvalidCommand && !app->owner.pending());
    assert(!std::memcmp(savedProfile,&app->motionProfile,sizeof(savedProfile)));

    assert(motionProfileCommand(app,Probe::MotionProfileCommand::SNAPSHOT,v)==Probe::Action::OK && v.pending);
    fixtureReply(words({10,100,100,30,0,7}));
    assert(app->motionProfile.view.saved && app->motionProfile.view.ok && !writeResponseConfirmed);
    auto request=MoveRequest(); request.position.value=Rational(100); request.position.configurationGeneration=app->axis.generation;
    request.speedRpm=60; request.ramp=MoveRamp::VERIFIED_CONFIGURED;
    ESS::MovePrerequisites p;
    assert(moveRequirements(*app,request,hardware.time,p) && !p.negativeTwosComplementVerified);
    request.position.value=Rational(-100); assert(!moveRequirements(*app,request,hardware.time,p));
    request.position.value=Rational(100); app->stateCache.blocks[1].value.inputs[1]=true;
    assert(!moveRequirements(*app,request,hardware.time,p)); app->stateCache.blocks[1].value.inputs[1]=false;
    uint32_t operation=0;
    assert(startMove(app,2,1,request,operation)==Probe::Action::OK);
    auto* r=findRecord(*app,operation); assert(r);
    // A copied-traffic observer under full console pressure cannot consume or
    // mutate the admitted move, its result or its bus queue.
    Probe::DebugSnapshot debug; Probe::DebugMode mode=Probe::DebugMode::RAW;
    assert(debugCommand(app,&mode,debug)==Probe::Action::OK);
    const auto queued=app->owner.pending(); const auto originalRequest=r->requestId;
    const auto outputCount=app->outputCount; const bool pendingOutput=app->console.outputPending();
    const auto dropped=app->debug.state.dropped;
    const uint8_t diagnostic[]={1,3,0,0,0,1,0x84,0x0A};
    app->debug.capture.begin(hardware.time); app->debug.capture.transmitted(diagnostic,sizeof(diagnostic),true,hardware.time);
    app->outputCount=OUTPUT_LINES-2; serviceDebug(*app); app->outputCount=outputCount;
    assert(app->debug.state.dropped==dropped+1 && app->owner.pending()==queued);
    assert(r->requestId.owner==originalRequest.owner && r->requestId.generation==originalRequest.generation && r->requestId.slot==originalRequest.slot);
    assert(app->owner.result(r->requestId)==nullptr && app->console.outputPending()==pendingOutput);

    // Ring loss and deliberate mode-change skips are distinct from copies that
    // were observed but could not be displayed. Changing mode flushes partial RX
    // before skipping it, preserving it for independent readers.
    const auto beforeDebug=app->debug.state;
    for (unsigned i=0;i<20;++i) app->debug.capture.event(TrafficKind::END,hardware.time);
    app->outputCount=OUTPUT_LINES-2; serviceDebug(*app); app->outputCount=outputCount;
    assert(app->debug.state.missed==beforeDebug.missed+4);
    assert(app->debug.state.observed==beforeDebug.observed+1);
    assert(app->debug.state.dropped==beforeDebug.dropped+1);
    app->debug.capture.received(0xAB,hardware.time-1,hardware.time,0,hardware.time);
    mode=Probe::DebugMode::DECODED;
    assert(debugCommand(app,&mode,debug)==Probe::Action::OK);
    assert(debug.skipped==beforeDebug.skipped+16 && !app->debug.requestKnown);
    assert(debug.emitted+debug.dropped==debug.observed);
    assert(debug.observed+debug.missed+debug.skipped==debug.cursor);
    uint64_t independent=0; TrafficRecord copied; bool partialPreserved=false;
    while (app->debug.capture.copyAfter(independent,copied))
        if (copied.kind==TrafficKind::RX && copied.length==1 && copied.bytes[0]==0xAB)
            partialPreserved=!copied.complete;
    assert(partialPreserved && app->owner.pending()==queued);

    reply(r->requestId,crc({1,16,0,0x21,0,5}));
    for (unsigned i=0;i<25000 && r->move.step==0;++i) step();
    assert(r->move.step==1);
    // An unconfirmed FC06 echo advances only to observations, retaining UNKNOWN.
    reply(r->requestId,crc({1,6,0,0x27,0,1}));
    for (unsigned i=0;i<25000 && r->move.step==1;++i) step();
    assert(r->move.execution==ActionExecution::UNKNOWN && r->move.step==2);
    reply(r->requestId,words({0,4}));
    for (unsigned i=0;i<25000 && r->move.step==2;++i) step();
    reply(r->requestId,words({0,1}));
    for (unsigned i=0;i<25000 && r->move.state==ActionState::ACTIVE;++i) step();
    assert(r->move.state==ActionState::SUCCEEDED && r->move.execution==ActionExecution::UNKNOWN);
    // Retained results are explicit; clear via observed urgent stop, never replay.
    ActionRequest stop; stop.kind=ActionKind::STOP; stop.stop.behavior=StopBehavior::CONFIGURED_DECELERATION; uint32_t stopped=0;
    assert(startAction(app,3,1,stop,stopped)==Probe::Action::OK);
    auto* st=findRecord(*app,stopped); reply(st->requestId,crc({1,6,0,0x27,1,0}));
    for (unsigned i=0;i<25000 && st->action.step==0;++i) step();
    reply(st->requestId,words({0,1}));
    for (unsigned i=0;i<25000 && st->action.state==ActionState::ACTIVE;++i) step();
    assert(st->action.state==ActionState::SUCCEEDED && st->action.execution==ActionExecution::UNKNOWN);
    r->delivered=st->delivered=true; // Direct callback fixture owns terminal consumption.
    assert(release(app,operation)==Probe::Action::OK); assert(release(app,stopped)==Probe::Action::OK);
    refresh();
    app->stateCache.blocks[2].value.rawPosition=99;
    request.position.relative=false; request.position.value=Rational(0);
    assert(absoluteMoveInEnvelope(*app,request,hardware.time));
    assert(moveRequirements(*app,request,hardware.time,p));
    app->stateCache.blocks[2].value.rawSpeed=23;
    assert(!absoluteMoveInEnvelope(*app,request,hardware.time));
    app->stateCache.blocks[2].value.rawSpeed=0;
    app->stateCache.blocks[2].value.rawPosition=0;
    assert(absoluteMoveInEnvelope(*app,request,hardware.time));
    app->stateCache.blocks[2].value.rawPosition=251;
    assert(!absoluteMoveInEnvelope(*app,request,hardware.time));
    p.startSpeed = 777;
    unsigned char savedPrerequisites[sizeof(p)]; std::memcpy(savedPrerequisites,&p,sizeof(p));
    assert(!moveRequirements(*app,request,hardware.time,p));
    assert(!std::memcmp(savedPrerequisites,&p,sizeof(p)));

    app->stateCache.blocks[2].value.rawPosition=99;
    request.position.value=Rational(251);
    assert(!absoluteMoveInEnvelope(*app,request,hardware.time));
    uint32_t returning=0;
    assert(startMove(app,4,1,request,returning)==Probe::Action::UNAVAILABLE && returning==0);
    request.position.value=Rational(501,2);
    assert(!absoluteMoveInEnvelope(*app,request,hardware.time));
    request.position.value=Rational(100);
    assert(absoluteMoveInEnvelope(*app,request,hardware.time));
    assert(startMove(app,4,1,request,returning)==Probe::Action::OK);
    assert(!findRecord(*app,returning)->move.prepared.displacementKnown);
    assert(!findRecord(*app,returning)->move.reference.nativeKnown);
    assert(findRecord(*app,returning)->move.prepared.effectiveNative==100);
    assert(cancel(app,returning)==Probe::Action::OK);
    for (unsigned i=0;i<1000 && !terminal(*app,*findRecord(*app,returning));++i) step();
    findRecord(*app,returning)->delivered=true;
    assert(release(app,returning)==Probe::Action::OK);
    assert(motionProfileCommand(app,Probe::MotionProfileCommand::RESTORE,v)==Probe::Action::OK);
    fixtureReply(crc({1,16,0,0x21,0,5})); assert(app->motionProfile.view.phase==Probe::MotionProfilePhase::READBACK);
    fixtureReply(words({10,100,100,30,0,7})); assert(app->motionProfile.view.restored && app->motionProfile.view.ok);
    // Refresh preserves originals; a second profile read cannot overwrite them.
    assert(motionProfileCommand(app,Probe::MotionProfileCommand::SNAPSHOT,v)==Probe::Action::OK);
    fixtureReply(words({10,100,100,60,0,100})); assert(app->motionProfile.view.original[3]==30 && app->motionProfile.view.current[3]==60);
    assert(motionProfileCommand(app,Probe::MotionProfileCommand::RESTORE,v)==Probe::Action::OK);
    fixtureReply(crc({1,16,0,0x21,0,5})); fixtureReply(words({10,100,100,60,0,100}));
    assert(!app->motionProfile.view.ok && !app->motionProfile.view.restored && !std::strcmp(app->motionProfile.view.error,"readback_mismatch"));
    Serial.input="@99 motion-profile inspect\n";
    for (unsigned i=0;i<1000;++i) step();
    assert(Serial.output.find("\"command\":\"motion-profile\"")!=std::string::npos);
}
