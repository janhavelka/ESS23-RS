// SPDX-License-Identifier: MIT
#include <MotorControlRS/profiles/ess_rs/Tuning.h>
#include <MotorControlRS/profiles/ess_rs/Registers.h>
#include <cassert>
#include <cstring>
#include <vector>
using namespace MotorControlRS;
namespace E = MotorControlRS::ESS_RS;
namespace {
const E::DriverGroup GROUPS[] = {E::DriverGroup::FILTERS, E::DriverGroup::CURRENT_LOOP, E::DriverGroup::LA, E::DriverGroup::COLLISION};
ReadTarget target() { ReadTarget t; t.id=4; t.address=1; t.generation=7; return t; }
template<class T> struct Saved {
    unsigned char bytes[sizeof(T)];
    explicit Saved(const T& value) { std::memcpy(bytes,&value,sizeof(value)); }
    void check(const T& value) const { assert(!std::memcmp(bytes,&value,sizeof(value))); }
};
E::TuningParameterInfo info(E::DriverGroup group,uint8_t slot) {
    E::TuningParameterInfo value; assert(E::tuningParameterInfo(E::tuningParameter(group,slot),value)); return value;
}
std::vector<uint8_t> crc(std::vector<uint8_t> bytes) {
    const uint16_t c=E::calcCrc16(bytes.data(),bytes.size());
    bytes.push_back(static_cast<uint8_t>(c)); bytes.push_back(static_cast<uint8_t>(c>>8)); return bytes;
}
std::vector<uint8_t> reply(const std::vector<uint16_t>& words) {
    std::vector<uint8_t> bytes={1,3,static_cast<uint8_t>(words.size()*2)};
    for(auto w:words) { bytes.push_back(static_cast<uint8_t>(w>>8)); bytes.push_back(static_cast<uint8_t>(w)); }
    return crc(bytes);
}
ActionEvent local(const E::DriverContext& c,ReadEventKind kind) {
    ActionEvent e; e.transport.target=c.target; e.transport.operationId=c.operationId;
    e.transport.step=c.step; e.transport.kind=kind; return e;
}
ActionEvent frame(const E::DriverContext& c,const std::vector<uint8_t>& bytes,uint64_t at) {
    auto e=local(c,ReadEventKind::FRAME); e.transport.frame=bytes.data(); e.transport.length=bytes.size();
    e.transport.txAccepted=8; e.txComplete=e.transport.qualified=e.responseConfirmed=true;
    e.transport.earliestUs=at; e.transport.latestUs=at+1; return e;
}
void supply(E::DriverContext& c,const std::vector<uint8_t>& bytes,bool confirmed=true) {
    auto e=frame(c,bytes,c.servicedUs+2); e.responseConfirmed=confirmed;
    assert(E::advanceDriver(c,e,c.servicedUs+4));
}
E::PreparedDriver work(const E::DriverContext& c) {
    E::PreparedDriver w; assert(E::nextDriver(c,c.servicedUs,w)); return w;
}
E::DriverObservation snapshot(E::DriverGroup group) {
    E::DriverContext c; assert(E::prepareTuningRead(c,target(),11,17,100,10000,group));
    while(c.state==ReadState::ACTIVE) {
        const auto w=work(c); std::vector<uint16_t> values;
        for(uint8_t i=0;i<w.count;++i) values.push_back(info(group,c.step*4+i).minimum);
        supply(c,reply(values));
    }
    E::DriverObservation o; assert(E::getDriver(c,o)); return o;
}
E::IdentityObservation identity() {
    E::ReadContext c; assert(E::prepareIdentity(c,target(),9,80,10000));
    auto bytes=reply({0x4EEA,0x29,1,0}); ReadEvent e; e.target=target(); e.operationId=9;
    e.txAccepted=8; e.frame=bytes.data(); e.length=bytes.size(); e.qualified=true; e.earliestUs=90; e.latestUs=91;
    assert(E::advanceRead(c,e,92)); E::IdentityObservation o; assert(E::getIdentity(c,o)); return o;
}
E::DriverRequest request(E::DriverGroup group,uint32_t fields=0) {
    E::DriverRequest r; r.group=group; r.configurationGeneration=17;
    r.fields=fields ? fields : (1u<<E::tuningFieldCount(group))-1;
    for(uint8_t i=0;i<E::tuningFieldCount(group);++i) r.tuningValues[i]=info(group,i).minimum;
    return r;
}
E::DriverPrerequisites prerequisites(const E::DriverRequest& r) {
    E::DriverPrerequisites p; p.configurationGeneration=17; p.previous=snapshot(r.group);
    p.stationaryQualified=true; p.stationaryTarget=target(); p.rawMotion=1;
    p.stationaryEarliestUs=110; p.stationaryLatestUs=120; p.maxAgeUs=10000;
    p.controlIdentity=identity(); p.exactModelQualified=true; p.modelSourceId=2;
    p.qualifiedModelCode=0x4EEA; p.qualifiedFirmwareCode=0x29;
    p.qualifiedTuning=r; p.tuningEffectsQualifiedFields=r.fields;
    p.tuningEarliestUs=110; p.tuningLatestUs=120; return p;
}
E::DriverContext update(const E::DriverRequest& r,bool echo=false) {
    auto p=prerequisites(r); p.allowEchoReadback=echo;
    E::DriverContext c; assert(E::prepareTuningSettings(c,target(),12,r,p,200,9000)); return c;
}
void reject(const E::DriverRequest& r,const E::DriverPrerequisites& p,Err code=Err::INVALID_CONFIG) {
    E::DriverContext c; c.operationId=999; Saved<E::DriverContext> old(c);
    const auto s=E::prepareTuningSettings(c,target(),12,r,p,200,9000); assert(!s&&s.code==code); old.check(c);
}
void metadataAndAllNativeRanges() {
    unsigned total=0;
    for(auto group:GROUPS) {
        for(uint8_t slot=0;slot<E::tuningFieldCount(group);++slot) {
            const auto d=info(group,slot); ++total;
            assert(d.group==group&&d.slot==slot&&d.accessReviewed&&d.minimum<=d.maximum);
            assert(static_cast<uint32_t>(E::driverFieldAt(slot,group))==(1u<<slot));
            assert(E::validateReadRegistersRequest(1,d.reg,1)&&E::validateWriteSingleRegisterRequest(1,d.reg,d.minimum));
            for(uint32_t value:{static_cast<uint32_t>(d.minimum),static_cast<uint32_t>(d.maximum)}) {
                E::DriverRequest r; r.configurationGeneration=17;
                assert(E::prepareTuningValue(r,E::tuningParameter(group,slot),value));
                assert(r.group==group&&r.fields==(1u<<slot)&&r.tuningValues[slot]==value);
                const auto c=update(r); assert(c.fieldCount==1&&work(c).reg==d.reg&&work(c).value==value);
            }
            E::DriverRequest r; Saved<E::DriverRequest> old(r);
            assert(!E::prepareTuningValue(r,E::tuningParameter(group,slot),static_cast<uint32_t>(d.maximum)+1)); old.check(r);
            if(d.minimum) { assert(!E::prepareTuningValue(r,E::tuningParameter(group,slot),d.minimum-1)); old.check(r); }
        }
        assert(E::tuningParameter(group,E::tuningFieldCount(group))==E::TuningParameter::NONE);
        assert(!static_cast<uint32_t>(E::driverFieldAt(E::tuningFieldCount(group),group)));
    }
    assert(total==20&&!E::isTuningGroup(E::DriverGroup::CONTROL_SETTINGS));
    for(uint8_t stage=1;stage<=2;++stage) for(auto field:{E::LaStageField::KP,E::LaStageField::KV,E::LaStageField::NODE})
        assert(E::laStageParameter(stage,field)==E::tuningParameter(E::DriverGroup::LA,(stage-1)*3+static_cast<uint8_t>(field)));
    assert(E::laStageParameter(0,E::LaStageField::KP)==E::TuningParameter::NONE);
    assert(E::laStageParameter(3,E::LaStageField::KP)==E::TuningParameter::NONE);
    assert(E::laStageParameter(1,static_cast<E::LaStageField>(255))==E::TuningParameter::NONE);
    E::TuningParameterInfo untouched; untouched.reg=123; Saved<E::TuningParameterInfo> saved(untouched);
    assert(!E::tuningParameterInfo(static_cast<E::TuningParameter>(255),untouched)); saved.check(untouched);
    for(auto parameter:{E::TuningParameter::COLLISION_THRESHOLD_003B,E::TuningParameter::COLLISION_CURRENT_003C}) {
        E::TuningParameterInfo d; assert(E::tuningParameterInfo(parameter,d)&&!d.accessReviewed);
        assert(!E::validateReadRegistersRequest(1,d.reg,1)&&!E::validateWriteSingleRegisterRequest(1,d.reg,d.minimum));
        E::DriverRequest r; Saved<E::DriverRequest> old(r);
        assert(E::prepareTuningValue(r,parameter,d.minimum).code==Err::UNSUPPORTED); old.check(r);
    }
}
void readsAndUnknownValues() {
    for(auto group:GROUPS) {
        E::DriverContext c; assert(E::prepareTuningRead(c,target(),11,17,100,10000,group));
        while(c.state==ReadState::ACTIVE) {
            const auto w=work(c); assert(!w.write&&w.count<=4&&w.reg==info(group,c.step*4).reg);
            const auto repeated=work(c); assert(!std::memcmp(w.bytes,repeated.bytes,w.length));
            supply(c,reply(std::vector<uint16_t>(w.count,65535)));
        }
        E::TuningObservation out; assert(E::getTuning(c,out)&&out.group==group&&out.count==E::tuningFieldCount(group));
        for(uint8_t i=0;i<out.count;++i) {
            assert(out.raw[i]==65535);
            assert(bool(out.knownFields&(1u<<i))==(info(group,i).maximum==65535));
        }
        assert(out.provenance[0].length<=E::DRIVER_MAX_REPLY_BYTES);
    }
    E::DriverContext c; assert(E::prepareTuningRead(c,target(),11,17,100,10000,E::DriverGroup::FILTERS));
    supply(c,reply({0,5,4000,5})); auto malformed=reply({10,512}); malformed.back()^=1;
    supply(c,malformed); assert(c.outcome==E::DriverOutcome::REPLY_ERROR);
    E::TuningObservation out; out.operationId=99; Saved<E::TuningObservation> old(out);
    assert(!E::getTuning(c,out)); old.check(out);
}
void wholeCandidateQualifications() {
    for(auto group:GROUPS) {
        auto r=request(group); auto p=prerequisites(r);
        r.fields|=1u<<E::tuningFieldCount(group); reject(r,p);
        r=request(group); p=prerequisites(r); p.tuningEffectsQualifiedFields=0; reject(r,p,Err::UNSUPPORTED);
        p=prerequisites(r); p.qualifiedTuning.tuningValues[0]++; reject(r,p);
        p=prerequisites(r); p.qualifiedTuning.group=E::DriverGroup::DRIVE; reject(r,p);
        p=prerequisites(r); p.previous.raw[0]++; reject(r,p);
        p=prerequisites(r); p.previous.target.generation++; reject(r,p);
        p=prerequisites(r); p.controlIdentity.target.generation++; reject(r,p);
        p=prerequisites(r); p.qualifiedModelCode++; reject(r,p);
        p=prerequisites(r); p.stationaryQualified=false; reject(r,p);
        p=prerequisites(r); p.tuningLatestUs=201; reject(r,p);
        p=prerequisites(r); p.maxAgeUs=110; reject(r,p);
        p=prerequisites(r); p.previous.configurationGeneration++; reject(r,p);
    }
    auto r=request(E::DriverGroup::FILTERS); r.tuningValues[5]=513; auto p=prerequisites(r); reject(r,p);
    E::DriverRequest selected; assert(E::prepareTuningValue(selected,E::TuningParameter::INPUT_FILTER,5));
    Saved<E::DriverRequest> unchanged(selected);
    assert(!E::prepareTuningValue(selected,E::TuningParameter::CURRENT_LOOP_KP,5)); unchanged.check(selected);
}
void fullSequencesAndPartialFailures() {
    for(auto group:GROUPS) {
        const auto r=request(group); auto c=update(r,true);
        while(c.state==ReadState::ACTIVE) {
            const auto w=work(c); assert(w.reg==info(group,c.step/2).reg);
            if(w.write) supply(c,std::vector<uint8_t>(w.bytes,w.bytes+w.length),false);
            else supply(c,reply({w.value}));
        }
        assert(c.outcome==E::DriverOutcome::SUCCESS&&!c.uncertain&&c.effects==r.fields);
        assert(c.completedSteps==E::tuningFieldCount(group)*2);
        for(uint8_t i=0;i<E::tuningFieldCount(group);++i)
            assert(!c.progress[i].acknowledged&&c.progress[i].execution==ActionExecution::UNKNOWN&&c.progress[i].readbackKnown&&!c.progress[i].activeKnown);
        if(group==E::DriverGroup::LA) assert(c.observations[15].reg==E::Registers::LA_POSITION_KI&&!c.observations[16].count);
    }
    auto c=update(request(E::DriverGroup::LA)); auto w=work(c);
    supply(c,std::vector<uint8_t>(w.bytes,w.bytes+w.length)); supply(c,reply({w.value}));
    w=work(c); supply(c,std::vector<uint8_t>(w.bytes,w.bytes+w.length));
    auto cancel=local(c,ReadEventKind::CANCEL); assert(E::advanceDriver(c,cancel,c.servicedUs+1));
    assert(c.outcome==E::DriverOutcome::CANCELLED&&c.effects==3&&c.uncertain&&c.progress[0].readbackKnown&&!c.progress[1].readbackKnown);
    Saved<E::DriverContext> retained(c); assert(!E::advanceDriver(c,cancel,c.servicedUs+1)); retained.check(c);
    c=update(request(E::DriverGroup::FILTERS,1)); w=work(c);
    supply(c,std::vector<uint8_t>(w.bytes,w.bytes+w.length)); supply(c,reply({1}));
    assert(c.outcome==E::DriverOutcome::READBACK_MISMATCH&&c.uncertain);
    c=update(request(E::DriverGroup::CURRENT_LOOP,1)); auto bytes=crc({1,0x86,2});
    supply(c,bytes); assert(c.outcome==E::DriverOutcome::REPLY_ERROR&&!c.uncertain&&c.progress[0].execution==ActionExecution::REJECTED);
    c=update(request(E::DriverGroup::LA,128)); w=work(c);
    bytes=std::vector<uint8_t>(w.bytes,w.bytes+w.length); auto late=frame(c,bytes,9001);
    assert(E::advanceDriver(c,late,9003)&&c.outcome==E::DriverOutcome::DEADLINE&&c.uncertain&&!c.progress[7].acknowledged);
}
void lostTransportAndEffectsBudget() {
    auto c=update(request(E::DriverGroup::CURRENT_LOOP,3));
    const auto first=work(c);
    supply(c,std::vector<uint8_t>(first.bytes,first.bytes+first.length));
    supply(c,reply({first.value}));
    const auto previous=c.prerequisites.previous;
    auto lost=local(c,ReadEventKind::TRANSPORT_FAILURE);
    lost.transport.txAccepted=4; lost.transport.executionUnknown=true; lost.transport.transportDetail=-7;
    assert(E::advanceDriver(c,lost,c.servicedUs+1));
    assert(c.outcome==E::DriverOutcome::TRANSPORT_ERROR&&c.completedSteps==2&&c.effects==3&&c.uncertain);
    assert(c.progress[0].acknowledged&&c.progress[0].readbackKnown&&!c.progress[0].activeKnown);
    assert(c.progress[1].execution==ActionExecution::UNKNOWN&&!c.progress[1].acknowledged&&!c.progress[1].readbackKnown);
    assert(c.observations[2].txAccepted==4&&c.observations[2].transportDetail==-7);
    assert(!std::memcmp(previous.raw,c.prerequisites.previous.raw,sizeof(previous.raw)));

    const auto r=request(E::DriverGroup::FILTERS,1); auto p=prerequisites(r);
    p.maxAgeUs=1000; p.tuningEarliestUs=50; p.tuningLatestUs=120;
    assert(E::prepareTuningSettings(c,target(),12,r,p,200,9000));
    const auto bounded=work(c); assert(bounded.deadlineUs==1050&&c.deadlineUs==9000);
    E::PreparedDriver later; assert(E::nextDriver(c,1000,later)&&later.deadlineUs==1050);
    const auto bytes=std::vector<uint8_t>(bounded.bytes,bounded.bytes+bounded.length);
    const auto late=frame(c,bytes,1050);
    assert(E::advanceDriver(c,late,1053)&&c.outcome==E::DriverOutcome::DEADLINE);
    assert(c.deadlineUs==9000&&c.uncertain&&!c.progress[0].acknowledged&&c.effects==1);
}
} // namespace
int main() {
    metadataAndAllNativeRanges(); readsAndUnknownValues(); wholeCandidateQualifications(); fullSequencesAndPartialFailures();
    lostTransportAndEffectsBudget();
}
