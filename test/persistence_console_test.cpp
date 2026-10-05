// SPDX-License-Identifier: MIT
#include "../examples/probe_cli/ProbeConsole.h"
#include <cassert>
#include <cstring>
#include <limits>
#include <string>
#include <vector>
namespace Probe=MotorControlRSExample::Probe;
namespace Ess=MotorControlRS::ESS_RS;
namespace {
struct Fake {
    std::vector<std::string> lines;
    unsigned calls=0,writes=0;
    Probe::PersistenceCommand request;
    Probe::PersistenceView view;
    Ess::PersistenceContext retained;
    Probe::Action result=Probe::Action::OK;
    static bool emit(void* p,const char* data,std::size_t size) {
        assert(size<Probe::OUTPUT_CAPACITY && data[0]=='{' && data[size-1]=='}');
        static_cast<Fake*>(p)->lines.emplace_back(data,size);return true;
    }
    static void snapshot(void*,Probe::Snapshot& out){out.address=7;}
    static Probe::Action persistence(void* p,const Probe::PersistenceCommand* c,Probe::PersistenceView& out) {
        auto& f=*static_cast<Fake*>(p);++f.calls;
        if(c){f.request=*c;if(c->kind==Probe::PersistenceCommandKind::BEGIN)++f.writes;}
        out=f.view;return f.result;
    }
    static Probe::Action probe(void*,uint32_t,uint8_t,uint32_t&){return Probe::Action::UNAVAILABLE;}
    static Probe::Action recover(void*,uint32_t,uint32_t&){return Probe::Action::UNAVAILABLE;}
    static void reset(void*){}
    Probe::Host hooks(){Probe::Host h;h.context=this;h.emitLine=emit;h.snapshot=snapshot;h.persistence=persistence;h.startProbe=probe;h.recover=recover;h.resetStats=reset;return h;}
    bool has(const char* s)const{return !lines.empty() && lines.back().find(s)!=std::string::npos;}
};
void send(Probe::Console& c,const std::string& s){for(char v:s)c.feed(v);c.feed('\n');}
void extreme(Ess::ReadStepObservation& p) {
    p.first=65535;p.count=65535;p.length=Ess::READ_MAX_REPLY_BYTES;p.receivedLength=p.length;
    std::memset(p.raw,0xFF,sizeof(p.raw));
    p.attemptedUs=p.earliestUs=p.latestUs=p.deliveredUs=std::numeric_limits<uint64_t>::max();
}
}
int main(){
    Fake f;Probe::Console c(f.hooks(), Probe::Format::JSON);
    send(c,"help persistence");assert(f.has("host before") && f.has("plan|begin"));
    send(c,"persistence");assert(f.has("\"action\":-1") && f.has("\"context\":null"));
    const unsigned before=f.calls;
    for(const char* bad:{"begin","begin save extra","begin all","plan factory","verify extra","finish extra","host requested","host","snapshot extra","save","begin save 1"}){
        send(c,std::string("persistence ")+bad);assert(f.has("\"ok\":false"));
    }
    assert(f.calls==before && !f.writes);
    send(c,"persistence plan save");assert(f.request.kind==Probe::PersistenceCommandKind::PREVIEW && f.request.request==Ess::PersistenceKind::SAVE);
    assert(f.has("\"register\":45,\"value\":66") && f.has("\"durability\":\"unverified\""));
    send(c,"persistence plan factory-restore");assert(f.request.request==Ess::PersistenceKind::FACTORY_RESTORE && f.has("\"value\":65"));
    assert(!f.writes);
    f.result=Probe::Action::UNAVAILABLE;send(c,"persistence begin save");assert(f.has("\"ok\":false") && f.has("\"route_ready\":false"));
    f.result=Probe::Action::OK;
    const char* phases[]={"snapshot","verify","host before","finish"};
    const Probe::PersistenceCommandKind kinds[]={Probe::PersistenceCommandKind::SNAPSHOT,Probe::PersistenceCommandKind::VERIFY,Probe::PersistenceCommandKind::SELECT_BEFORE,Probe::PersistenceCommandKind::FINISH};
    for(unsigned n=0;n<4;++n){send(c,std::string("persistence ")+phases[n]);assert(f.request.kind==kinds[n]);}
    f.view.context=&f.retained;f.view.owned=true;f.view.parentSession=true;f.view.finished=true;f.view.invocations=2;
    f.retained.operationId=0xFFFFFFFFu;f.retained.effects=f.retained.uncertain=true;f.retained.verificationKnown=true;
    f.retained.startedUs=f.retained.deadlineUs=std::numeric_limits<uint64_t>::max();
    for(auto& p:f.retained.before.beforeConfig.provenance)extreme(p);
    extreme(f.retained.before.beforeIdentity.provenance);extreme(f.retained.before.stationary.provenance);
    f.retained.verification.config=f.retained.before.beforeConfig;f.retained.verification.identity=f.retained.before.beforeIdentity;f.retained.verification.stationary=f.retained.before.stationary;
    for(auto& field:f.retained.fields){field.reg=field.before=field.readback=65535;field.sourceAccess="RW/S";field.readbackKnown=field.survivedRestart=true;}
    f.retained.writeEvidence.length=Ess::ACTION_MAX_REPLY_BYTES;std::memset(f.retained.writeEvidence.raw,0xFF,sizeof(f.retained.writeEvidence.raw));
    f.retained.writeEvidence.earliestUs=f.retained.writeEvidence.latestUs=f.retained.writeEvidence.deliveredUs=std::numeric_limits<uint64_t>::max();
    send(c,"persistence inspect");assert(f.has("\"context\":{") && !f.has("output_full"));assert(f.has("18446744073709551615"));
    assert(f.has("\"parent_session\":true,\"finished\":true,\"invocations\":2"));
    // A fresh snapshot after the historical invocation must remain visible. Use
    // the actual reviewed window sizes and maximum numeric formatting widths.
    Ess::PersistencePrerequisites next=f.retained.before;
    next.beforeConfig.operationId=next.beforeIdentity.operationId=next.stationary.operationId=0xFFFFFFFFu;
    next.configurationGeneration=0xFFFFFFFFu;
    next.beforeConfig.target.id=next.beforeConfig.target.generation=0xFFFFFFFFu;next.beforeConfig.target.address=247;
    next.beforeConfig.raw.direction=next.beforeConfig.raw.subdivision=next.beforeConfig.raw.customNode=next.beforeConfig.raw.baud=next.beforeConfig.raw.format=65535;
    next.beforeConfig.raw.overLimitStop=next.beforeConfig.raw.softLimitEnable=next.beforeConfig.raw.wordOrder=next.beforeConfig.raw.inputPolarity=65535;
    next.beforeConfig.raw.algorithm=next.beforeConfig.raw.encoderResolution=65535;
    for(auto& input:next.beforeConfig.raw.inputFunctions)input=65535;
    next.beforeIdentity.rawModel=next.beforeIdentity.rawVersion=next.beforeIdentity.rawActiveNode=next.beforeIdentity.rawDip=65535;
    next.stationary.rawAlarm=next.stationary.rawMotion=65535;
    const uint8_t counts[]={2,3,3,5,2};
    for(unsigned i=0;i<5;++i)next.beforeConfig.provenance[i].length=5+2*counts[i];
    next.beforeIdentity.provenance.length=13;next.stationary.provenance.length=9;
    f.retained.before=next;f.retained.verification.config=next.beforeConfig;
    f.retained.verification.identity=next.beforeIdentity;f.retained.verification.stationary=next.stationary;
    f.view.baseline=&next;f.view.snapshotKnown=true;f.view.snapshotFailed=true;f.view.verificationAttempts=2;
    send(c,"persistence inspect");assert(f.has("\"baseline\":{") && !f.has("output_full"));
    assert(f.has("\"snapshot_failed\":true,\"verification_attempts\":2"));
    const auto writes=f.writes;send(c,"persistence inspect");assert(f.writes==writes);
    Fake absent;auto h=absent.hooks();h.persistence=nullptr;Probe::Console legacy(h, Probe::Format::JSON);
    send(legacy,"help");assert(!absent.has("\"persistence\""));send(legacy,"persistence inspect");assert(absent.has("unavailable") && !absent.calls);
    send(c,"caps");assert(f.has("\"persistence\":true"));
    return 0;
}
