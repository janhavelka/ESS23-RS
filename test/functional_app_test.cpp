// SPDX-License-Identifier: MIT
#define MOTORCONTROLRS_FUNCTIONAL_BENCH 1
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
    const auto phase=app->functional.phase; reply(app->functionalRequest,bytes);
    for (unsigned i=0;i<25000 && app->functional.pending && app->functional.phase==phase;++i) step();
    assert(!app->functional.pending || app->functional.phase!=phase);
}
}
int main() {
    resetHardware(); setup(); assert(app); hardware.txCharacterUs=87;
    assert(uart.startCapture(20,timing().holdUs)); refresh();
    Probe::FunctionalView v;
    assert(functionalCommand(app,Probe::FunctionalCommand::SNAPSHOT,v)==Probe::Action::OK && v.pending);
    fixtureReply(words({10,100,100,30,0,7}));
    assert(app->functional.saved && app->functional.ok && !actionTimingQualified);
    auto request=MoveRequest(); request.position.value=Rational(100); request.position.configurationGeneration=app->axis.generation;
    request.speedRpm=60; request.ramp=MoveRamp::VERIFIED_CONFIGURED;
    ESS::MovePrerequisites p;
    assert(functionalMovePrerequisites(*app,request,hardware.time,p) && !p.negativeTwosComplementVerified);
    request.position.value=Rational(-100); assert(!functionalMovePrerequisites(*app,request,hardware.time,p));
    request.position.value=Rational(100); app->stateCache.blocks[1].value.inputs[1]=true;
    assert(!functionalMovePrerequisites(*app,request,hardware.time,p)); app->stateCache.blocks[1].value.inputs[1]=false;
    uint32_t operation=0;
    assert(startMove(app,2,1,request,operation)==Probe::Action::OK);
    auto* r=findRecord(*app,operation); assert(r && r->move.options.allowUnconfirmedWriteObservation);
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
    assert(functionalZeroEnvelope(*app,hardware.time));
    request.position.relative=false; request.position.value=Rational(0);
    assert(functionalMovePrerequisites(*app,request,hardware.time,p) && p.nativeZeroEnvelopeVerified);
    app->stateCache.blocks[2].value.rawSpeed=23;
    assert(!functionalZeroEnvelope(*app,hardware.time));
    app->stateCache.blocks[2].value.rawSpeed=0;
    app->stateCache.blocks[2].value.rawPosition=0;
    assert(!functionalZeroEnvelope(*app,hardware.time));
    app->stateCache.blocks[2].value.rawPosition=251;
    assert(!functionalZeroEnvelope(*app,hardware.time));
    app->stateCache.blocks[2].value.rawPosition=99;
    uint32_t returning=0;
    assert(startMove(app,4,1,request,returning)==Probe::Action::OK);
    assert(!findRecord(*app,returning)->move.prepared.displacementKnown);
    assert(!findRecord(*app,returning)->move.reference.nativeKnown);
    assert(findRecord(*app,returning)->move.prepared.effectiveNative==0);
    assert(cancel(app,returning)==Probe::Action::OK);
    for (unsigned i=0;i<1000 && !terminal(*app,*findRecord(*app,returning));++i) step();
    findRecord(*app,returning)->delivered=true;
    assert(release(app,returning)==Probe::Action::OK);
    assert(functionalCommand(app,Probe::FunctionalCommand::RESTORE,v)==Probe::Action::OK);
    fixtureReply(crc({1,16,0,0x21,0,5})); assert(app->functional.phase==3);
    fixtureReply(words({10,100,100,30,0,7})); assert(app->functional.restored && app->functional.ok);
    // Refresh preserves originals; a second profile read cannot overwrite them.
    assert(functionalCommand(app,Probe::FunctionalCommand::SNAPSHOT,v)==Probe::Action::OK);
    fixtureReply(words({10,100,100,60,0,100})); assert(app->functional.original[3]==30 && app->functional.current[3]==60);
    assert(functionalCommand(app,Probe::FunctionalCommand::RESTORE,v)==Probe::Action::OK);
    fixtureReply(crc({1,16,0,0x21,0,5})); fixtureReply(words({10,100,100,60,0,100}));
    assert(!app->functional.ok && !app->functional.restored && !std::strcmp(app->functional.error,"readback_mismatch"));
    Serial.input="@99 motion-bench inspect\n";
    for (unsigned i=0;i<1000;++i) step();
    assert(Serial.output.find("\"command\":\"motion-bench\"")!=std::string::npos);
    assert(Serial.output.find("\"electrical_qualified\":false")!=std::string::npos);
}
