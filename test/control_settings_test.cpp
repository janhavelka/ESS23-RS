// SPDX-License-Identifier: MIT
#include <MotorControlRS/profiles/ess_rs/ControlSettings.h>
#include <MotorControlRS/profiles/ess_rs/Registers.h>
#include <cassert>
#include <cstring>
#include <vector>
using namespace MotorControlRS;
namespace E = MotorControlRS::ESS_RS;
namespace {
ReadTarget target() { ReadTarget t; t.id=4; t.address=1; t.generation=7; return t; }
uint32_t bit(uint8_t i) { return static_cast<uint32_t>(E::driverFieldAt(i,E::DriverGroup::CONTROL_SETTINGS)); }
template<class T> struct Saved {
    unsigned char bytes[sizeof(T)];
    explicit Saved(const T& v) { std::memcpy(bytes,&v,sizeof(v)); }
    void check(const T& v) const { assert(!std::memcmp(bytes,&v,sizeof(v))); }
};
std::vector<uint8_t> reply(std::initializer_list<uint16_t> words) {
    std::vector<uint8_t> b={1,3,static_cast<uint8_t>(words.size()*2)};
    for (auto w:words) { b.push_back(static_cast<uint8_t>(w>>8)); b.push_back(static_cast<uint8_t>(w)); }
    const uint16_t crc=E::calcCrc16(b.data(),b.size()); b.push_back(static_cast<uint8_t>(crc)); b.push_back(static_cast<uint8_t>(crc>>8)); return b;
}
ActionEvent local(const E::DriverContext& c,ReadEventKind kind) {
    ActionEvent e; e.transport.target=c.target; e.transport.operationId=c.operationId;
    e.transport.step=c.step; e.transport.kind=kind; return e;
}
ActionEvent frame(const E::DriverContext& c,const std::vector<uint8_t>& b,uint64_t at) {
    auto e=local(c,ReadEventKind::FRAME); e.transport.frame=b.data(); e.transport.length=b.size();
    e.transport.txAccepted=8; e.txComplete=e.transport.qualified=e.responseConfirmed=true;
    e.transport.earliestUs=at; e.transport.latestUs=at+1; return e;
}
void supply(E::DriverContext& c,const std::vector<uint8_t>& b,bool confirmed=true) {
    auto e=frame(c,b,c.servicedUs+2); e.responseConfirmed=confirmed; assert(E::advanceDriver(c,e,c.servicedUs+4));
}
E::PreparedDriver work(const E::DriverContext& c) {
    E::PreparedDriver w; assert(E::nextDriver(c,c.servicedUs,w)); return w;
}
E::DriverObservation snapshot(uint16_t algorithm=2,uint16_t encoder=4000,uint16_t maximum=1000,uint16_t closed=100,uint16_t lock=50,uint16_t open=100) {
    E::DriverContext c; assert(E::prepareControlRead(c,target(),11,17,100,10000));
    supply(c,reply({algorithm,encoder,maximum,closed})); supply(c,reply({40,open,lock,4000}));
    E::DriverObservation o; assert(E::getDriver(c,o)); return o;
}
E::IdentityObservation identity() {
    E::ReadContext c; assert(E::prepareIdentity(c,target(),9,80,10000));
    auto bytes=reply({0x4EEA,0x29,1,0}); ReadEvent e; e.target=target(); e.operationId=9;
    e.txAccepted=8; e.frame=bytes.data(); e.length=bytes.size(); e.qualified=true; e.earliestUs=90; e.latestUs=91;
    assert(E::advanceRead(c,e,92)); E::IdentityObservation o; assert(E::getIdentity(c,o)); return o;
}
E::DriverRequest request(uint8_t field=7) {
    E::DriverRequest r; r.group=E::DriverGroup::CONTROL_SETTINGS; r.configurationGeneration=17; r.fields=bit(field);
    r.controlAlgorithm=E::ControlAlgorithm::ALGORITHM_1; r.encoderResolution=4000; r.maximumEffectiveCurrentMa=1000;
    r.closedMaximumPercent=100; r.closedBasePercent=40; r.openMaximumPercent=100; r.lockPercent=50; r.lockDelayMs=4001; return r;
}
E::DriverPrerequisites prerequisites(const E::DriverRequest& r) {
    E::DriverPrerequisites p; p.configurationGeneration=17; p.previous=snapshot(); p.stationaryQualified=true;
    p.stationaryTarget=target(); p.rawMotion=1; p.stationaryEarliestUs=110; p.stationaryLatestUs=120; p.maxAgeUs=10000;
    p.controlIdentity=identity(); p.exactModelQualified=true; p.modelSourceId=2;
    p.qualifiedModelCode=0x4EEA; p.qualifiedFirmwareCode=0x29;
    p.nativeCurrentLimitQualified=true; p.maximumEffectiveLimitMa=2000; p.currentPercentBaseQualified=true;
    p.controlEffectsQualifiedFields=r.fields; p.qualifiedControl=r; p.controlEarliestUs=110; p.controlLatestUs=120; return p;
}
E::DriverContext update(const E::DriverRequest& r,bool echo=false) {
    auto p=prerequisites(r); p.allowEchoReadback=echo;
    E::DriverContext c; assert(E::prepareControlSettings(c,target(),12,r,p,200,9000)); return c;
}
void ack(E::DriverContext& c,bool confirmed=true) {
    const auto w=work(c); assert(w.write); supply(c,std::vector<uint8_t>(w.bytes,w.bytes+w.length),confirmed);
}
void reject(const E::DriverRequest& r,const E::DriverPrerequisites& p,Err code=Err::INVALID_CONFIG) {
    E::DriverContext c; c.operationId=999; Saved<E::DriverContext> saved(c);
    const auto s=E::prepareControlSettings(c,target(),12,r,p,200,9000); assert(!s&&s.code==code); saved.check(c);
}
void set(E::DriverRequest& r,uint8_t index,uint16_t value) {
    switch(index) {
    case 0:r.controlAlgorithm=static_cast<E::ControlAlgorithm>(value);break;
    case 1:r.encoderResolution=value;break;
    case 2:r.maximumEffectiveCurrentMa=value;break;
    case 3:r.closedMaximumPercent=value;break;
    case 4:r.closedBasePercent=value;break;
    case 5:r.openMaximumPercent=value;break;
    case 6:r.lockPercent=value;break;
    case 7:r.lockDelayMs=value;break;
    }
}
void readAndPreservation() {
    E::DriverContext c; assert(E::prepareControlRead(c,target(),11,17,100,10000));
    auto w=work(c); assert(w.reg==E::Registers::CONTROL_ALGORITHM&&w.count==4&&w.length==8&&!w.write);
    const auto repeat=work(c); assert(!std::memcmp(w.bytes,repeat.bytes,8));
    supply(c,reply({3,0,5601,151})); w=work(c); assert(w.reg==E::Registers::BASE_CURRENT_PERCENT&&w.count==4);
    auto bytes=reply({76,101,101,20001}); supply(c,bytes); bytes.assign(bytes.size(),0);
    E::ControlObservation out; assert(E::getControl(c,out));
    assert(out.raw[0]==3&&!out.algorithmKnown&&!out.encoderScaleUsable&&!out.knownFields);
    assert(out.maximumEffectiveCurrentMa==5601&&out.lockDelayMs==20001&&!out.percentBaseKnown&&!out.activeSettingsKnown);
    Saved<E::ControlObservation> old(out);
    for(unsigned fault=0;fault<3;++fault) {
        assert(E::prepareControlRead(c,target(),11,17,100,10000)); supply(c,reply({2,4000,1000,100}));
        auto bad=reply({40,100,50,4000});
        if(fault==0) bad.back()^=1;
        if(fault==1) bad=reply({40,100,50});
        if(fault==2) { bad={1,0x83,2}; const auto crc=E::calcCrc16(bad.data(),bad.size()); bad.push_back(static_cast<uint8_t>(crc));bad.push_back(static_cast<uint8_t>(crc>>8)); }
        supply(c,bad); assert(c.state==ReadState::FAILED&&!E::getControl(c,out)); old.check(out);
    }
    assert(E::prepareControlRead(c,target(),11,17,100,130)); supply(c,reply({2,65535,5600,150}));
    bytes=reply({75,100,100,20000}); auto e=frame(c,bytes,125);
    assert(E::advanceDriver(c,e,1000)&&E::getControl(c,out)); assert(out.algorithmKnown&&out.encoderScaleUsable&&out.knownFields==0x7F800000u);
    for(uint8_t i=8;i<16;++i) assert(!bit(i));
}
void rangesAndWholeCandidate() {
    const uint16_t maximum[]={2,65535,5600,150,75,100,100,20000};
    for(uint8_t i=0;i<8;++i) {
        auto r=request(i); auto p=prerequisites(r); p.maximumEffectiveLimitMa=5600; p.qualifiedControl=r;
        for(uint16_t value:{uint16_t(i<2?1:0),maximum[i]}) {
            set(r,i,value); p.qualifiedControl=r;
            E::DriverContext c; assert(E::prepareControlSettings(c,target(),12,r,p,200,9000)); assert(work(c).value==value);
        }
        if(i!=1) {set(r,i,maximum[i]+1);p.qualifiedControl=r;reject(r,p);}
    }
    auto r=request(0); r.controlAlgorithm=static_cast<E::ControlAlgorithm>(0); reject(r,prerequisites(r));
    r=request(1);r.encoderResolution=0;reject(r,prerequisites(r));
    r=request();r.fields|=bit(1);r.encoderResolution=0;reject(r,prerequisites(r));
    r=request();r.fields|=static_cast<uint32_t>(E::DriverField::DIRECTION);reject(r,prerequisites(r));
    r=request();r.fields=0;reject(r,prerequisites(r));
    r=request();auto p=prerequisites(r);p.previous=snapshot(3);p.nativeCurrentLimitQualified=false;p.currentPercentBaseQualified=false;
    E::DriverContext c;assert(E::prepareControlSettings(c,target(),12,r,p,200,9000)); // Independent delay with unknown algorithm/current base.
    r=request(1);p=prerequisites(r);p.previous=snapshot(3);p.nativeCurrentLimitQualified=false;p.currentPercentBaseQualified=false;
    assert(E::prepareControlSettings(c,target(),12,r,p,200,9000)); // Nonzero configured encoder raw setter has independent effects.
    r=request(0);p=prerequisites(r);p.previous=snapshot(1,0);reject(r,p);
    r.fields|=bit(1);r.encoderResolution=4000;p=prerequisites(r);p.previous=snapshot(1,0);reject(r,p); // Algorithm is staged first; later positive encoder cannot rescue it.
    r=request();p=prerequisites(r);p.previous=snapshot(2,0);
    assert(E::prepareControlSettings(c,target(),12,r,p,200,9000)); // Delay has no unrelated encoder prerequisite.
    r=request(3);p=prerequisites(r);p.previous=snapshot(3);reject(r,p,Err::UNSUPPORTED);
    p=prerequisites(r);p.currentPercentBaseQualified=false;reject(r,p,Err::UNSUPPORTED);
    p=prerequisites(r);p.nativeCurrentLimitQualified=false;reject(r,p,Err::UNSUPPORTED);
    p=prerequisites(r);p.maximumEffectiveLimitMa=0;reject(r,p,Err::UNSUPPORTED);
    p=prerequisites(r);p.maximumEffectiveLimitMa=5601;reject(r,p,Err::UNSUPPORTED);
}
void identityEffectsAndBounds() {
    auto r=request();auto p=prerequisites(r);
    p.exactModelQualified=false;reject(r,p);
    p=prerequisites(r);p.modelSourceId=0;reject(r,p);
    p=prerequisites(r);p.qualifiedModelCode++;reject(r,p);
    p=prerequisites(r);p.qualifiedFirmwareCode++;reject(r,p);
    p=prerequisites(r);p.controlIdentity.target.generation++;reject(r,p);
    p=prerequisites(r);p.controlIdentity.rawDip++;reject(r,p);
    p=prerequisites(r);p.controlIdentity.provenance.receivedLength++;reject(r,p);
    p=prerequisites(r);p.controlIdentity.provenance.executionUnknown=true;reject(r,p);
    p=prerequisites(r);p.controlIdentity.provenance.raw[5]^=1;reject(r,p);
    p=prerequisites(r);p.controlEffectsQualifiedFields=0;reject(r,p,Err::UNSUPPORTED);
    p=prerequisites(r);p.qualifiedControl.lockDelayMs++;reject(r,p);
    p=prerequisites(r);p.controlLatestUs=201;reject(r,p);
    p=prerequisites(r);p.previous.configurationGeneration++;reject(r,p);
    p=prerequisites(r);p.previous.raw[2]++;reject(r,p);
    p=prerequisites(r);p.stationaryQualified=false;reject(r,p);
    p=prerequisites(r);p.rawAlarm=1;reject(r,p);
    p=prerequisites(r);p.maxAgeUs=80;reject(r,p);
    p=prerequisites(r);p.maxAgeUs=110;reject(r,p); // Identity alone has expired.
    r=request(2);r.maximumEffectiveCurrentMa=2001;p=prerequisites(r);reject(r,p);
    r=request(3);r.closedMaximumPercent=150;p=prerequisites(r);p.previous=snapshot(2,4000,1500);reject(r,p);
    r=request(2);r.maximumEffectiveCurrentMa=2000;r.fields|=bit(3);r.closedMaximumPercent=50;
    p=prerequisites(r);p.previous=snapshot(2,4000,1000,150);reject(r,p); // Final1000mA, intermediate3000mA; reject before TX.
    r=request(4);r.closedBasePercent=75;p=prerequisites(r);p.previous=snapshot(2,4000,1000,50);
    E::DriverContext c;assert(E::prepareControlSettings(c,target(),12,r,p,200,9000)); // No invented base<=max rule.
    r=request(6);r.lockPercent=75;p=prerequisites(r);p.previous=snapshot(1,4000,1000,100,50,50);
    assert(E::prepareControlSettings(c,target(),12,r,p,200,9000));
}
void fullCapacity() {
    auto r=request(); r.fields=0x7F800000u; r.maximumEffectiveCurrentMa=2300;
    r.closedMaximumPercent=100; r.closedBasePercent=50; r.openMaximumPercent=100;
    r.lockPercent=50; r.encoderResolution=4000; r.lockDelayMs=1000;
    auto p=prerequisites(r); p.maximumEffectiveLimitMa=4000;
    for (unsigned cancelAt : {14u,15u,16u}) {
        E::DriverContext c; assert(E::prepareControlSettings(c,target(),12,r,p,200,9000));
        assert(c.fieldCount==8);
        for (unsigned i=0;i<cancelAt;++i) {
            const auto w=work(c); assert(w.step==i&&w.reg==0x100+i/2);
            if(w.write) ack(c); else supply(c,reply({w.value}));
        }
        assert(c.completedSteps==cancelAt);
        if(cancelAt==16) assert(c.state==ReadState::SUCCEEDED&&!c.uncertain&&c.effects==r.fields);
        else {
            const auto cancel=local(c,ReadEventKind::CANCEL);
            assert(E::advanceDriver(c,cancel,c.servicedUs+1)&&c.outcome==E::DriverOutcome::CANCELLED);
            assert(c.uncertain==(cancelAt==15));
            assert(c.effects==(cancelAt==15?r.fields:r.fields&~bit(7)));
            Saved<E::DriverContext> saved(c);assert(!E::advanceDriver(c,cancel,c.servicedUs+1));saved.check(c);
        }
        for(uint8_t i=0;i<7;++i) assert(c.progress[i].acknowledged&&c.progress[i].readbackKnown&&!c.progress[i].activeKnown);
        assert(c.progress[2].readback==2300&&c.progress[4].readback==50);
        if(cancelAt==16) assert(c.observations[15].reg==0x107&&!c.observations[15].write&&c.progress[7].readback==1000);
        assert(!c.observations[16].count&&!c.observations[17].count);
        assert(c.prerequisites.previous.raw[2]==1000&&c.prerequisites.previous.raw[7]==4000);
    }
}
void deadlineEvidence() {
    auto c=update(request());
    const auto w=work(c); auto echo=std::vector<uint8_t>(w.bytes,w.bytes+w.length);
    auto late=frame(c,echo,9001);
    assert(E::advanceDriver(c,late,9003)&&c.outcome==E::DriverOutcome::DEADLINE);
    assert(!c.progress[7].acknowledged&&c.progress[7].execution==ActionExecution::UNKNOWN&&c.uncertain&&c.effects==bit(7));
    assert(c.observations[0].status&&c.observations[0].responseConfirmed&&c.observations[0].latestUs==9002);
    std::vector<uint8_t> exception={1,0x86,2};
    auto crc=E::calcCrc16(exception.data(),exception.size());exception.push_back(static_cast<uint8_t>(crc));exception.push_back(static_cast<uint8_t>(crc>>8));
    c=update(request());late=frame(c,exception,9001);
    assert(E::advanceDriver(c,late,9003)&&c.outcome==E::DriverOutcome::DEADLINE);
    assert(c.progress[7].execution==ActionExecution::UNKNOWN&&c.uncertain&&c.effects==bit(7));
    assert(c.observations[0].status.code==Err::EXCEPTION&&c.observations[0].length==exception.size()&&!std::memcmp(c.observations[0].raw,exception.data(),exception.size()));
    c=update(request());const auto ontime=frame(c,exception,8998);
    assert(E::advanceDriver(c,ontime,10000)&&c.outcome==E::DriverOutcome::REPLY_ERROR);
    assert(c.progress[7].execution==ActionExecution::REJECTED&&!c.uncertain&&c.deadlineUs==9000);
    c=update(request());auto expired=local(c,ReadEventKind::DEADLINE);expired.transport.txAccepted=4;expired.transport.executionUnknown=true;
    assert(E::advanceDriver(c,expired,9000)&&c.outcome==E::DriverOutcome::DEADLINE&&c.uncertain&&c.effects==bit(7));
    auto r=request(1);r.fields|=bit(7);c=update(r);ack(c);supply(c,reply({4000}));
    expired=local(c,ReadEventKind::DEADLINE);
    assert(E::advanceDriver(c,expired,9000)&&c.outcome==E::DriverOutcome::DEADLINE&&!c.uncertain);
    assert(c.progress[1].readbackKnown&&c.effects==bit(1)&&c.progress[7].execution==ActionExecution::NOT_TRANSMITTED&&c.deadlineUs==9000);
}
void previousSettingsBudget() {
    auto r=request(6); r.fields|=bit(7);
    auto p=prerequisites(r); p.maxAgeUs=1000;
    p.previous.provenance[0].attemptedUs=50;
    E::DriverContext c;
    assert(E::prepareControlSettings(c,target(),12,r,p,200,9000));
    auto w=work(c); assert(w.deadlineUs==1050&&c.deadlineUs==9000);
    ack(c); supply(c,reply({w.value}));
    assert(c.step==2&&c.effects==bit(6)&&!c.uncertain);
    E::PreparedDriver untouched; untouched.reg=123;
    Saved<E::PreparedDriver> savedWork(untouched); Saved<E::DriverContext> savedContext(c);
    assert(!E::nextDriver(c,1050,untouched)); savedWork.check(untouched); savedContext.check(c);
    assert(E::advanceDriver(c,local(c,ReadEventKind::DEADLINE),1050));
    assert(c.outcome==E::DriverOutcome::DEADLINE&&c.effects==bit(6)&&!c.uncertain);
    assert(c.progress[6].readbackKnown&&c.progress[7].execution==ActionExecution::NOT_TRANSMITTED);

    assert(E::prepareControlSettings(c,target(),12,r,p,200,9000));
    w=work(c); const auto bytes=std::vector<uint8_t>(w.bytes,w.bytes+w.length);
    const auto expired=frame(c,bytes,1050);
    assert(E::advanceDriver(c,expired,1052)&&c.outcome==E::DriverOutcome::DEADLINE);
    assert(c.effects==bit(6)&&c.uncertain&&!c.progress[6].acknowledged);
}
void previousSettingsEnvelope() {
    const auto r=request();
    for (uint8_t step=0;step<2;++step) for (uint8_t fault=0;fault<6;++fault) {
        auto p=prerequisites(r); auto& evidence=p.previous.provenance[step];
        switch (fault) {
        case 0: evidence.event=ReadEventKind::CANCEL; break;
        case 1: ++evidence.step; break;
        case 2: evidence.txAccepted=0; break;
        case 3: evidence.txComplete=false; break;
        case 4: evidence.executionUnknown=true; break;
        case 5: ++evidence.receivedLength; break;
        }
        E::DriverContext c; c.operationId=999; Saved<E::DriverContext> retained(c);
        const auto status=E::prepareControlSettings(c,target(),12,r,p,200,9000);
        assert(!status&&status.detail==static_cast<int32_t>(E::DriverError::STALE_SETTINGS));
        retained.check(c);
    }
}
void lifecycle() {
    auto r=request(1);r.fields|=bit(7);r.encoderResolution=8000;
    auto c=update(r);ack(c);supply(c,reply({8000}));assert(c.progress[1].readbackKnown&&!c.uncertain);
    auto e=local(c,ReadEventKind::TRANSPORT_FAILURE);e.transport.txAccepted=8;e.txComplete=true;e.transport.executionUnknown=true;
    assert(E::advanceDriver(c,e,c.servicedUs+1)&&c.uncertain&&c.effects==(bit(1)|bit(7)));
    assert(c.progress[1].readback==8000&&c.progress[7].execution==ActionExecution::UNKNOWN);
    for(unsigned step=0;step<4;++step) {
        c=update(r);for(unsigned i=0;i<step;++i) {const auto w=work(c);if(w.write)ack(c);else supply(c,reply({w.value}));}
        e=local(c,ReadEventKind::CANCEL);assert(E::advanceDriver(c,e,c.servicedUs+1)&&c.outcome==E::DriverOutcome::CANCELLED);
        Saved<E::DriverContext> saved(c);assert(!E::advanceDriver(c,e,c.servicedUs+1));saved.check(c);
    }
    c=update(request(),true);ack(c,false);assert(c.state==ReadState::ACTIVE&&c.uncertain&&!c.progress[7].acknowledged);
    supply(c,reply({4001}));assert(c.state==ReadState::SUCCEEDED&&!c.uncertain&&!c.progress[7].acknowledged&&c.progress[7].execution==ActionExecution::UNKNOWN&&!c.progress[7].activeKnown);
    c=update(request());ack(c,false);assert(c.outcome==E::DriverOutcome::UNCONFIRMED_RESPONSE&&c.uncertain);
    c=update(request(),true);ack(c,false);supply(c,reply({4000}));assert(c.outcome==E::DriverOutcome::READBACK_MISMATCH&&c.uncertain);
    c=update(request());const auto w=work(c);auto bytes=std::vector<uint8_t>(w.bytes,w.bytes+8);e=frame(c,bytes,210);e.transport.target.generation++;
    Saved<E::DriverContext> saved(c);assert(!E::advanceDriver(c,e,212));saved.check(c);
    auto p=prerequisites(request());p.maxAgeUs=130;p.controlEarliestUs=90;p.controlLatestUs=100;
    assert(E::prepareControlSettings(c,target(),12,request(),p,200,9000));assert(work(c).deadlineUs==210); // Older identity eligibility bounds write budget.
}
}
static void testOptionalAgePolicy() {
    const auto r = request(); auto p = prerequisites(r); p.maxAgeUs = 0;
    E::DriverContext c;
    assert(E::prepareControlSettings(c, target(), 12, r, p, 100000, 110000));
    assert(work(c).deadlineUs == 110000);
    const Saved<E::DriverContext> saved(c);
    p.maxAgeUs = 10000;
    assert(!E::prepareControlSettings(c, target(), 12, r, p, 100000, 110000)); saved.check(c);
    p.maxAgeUs = 0; p.controlIdentity.target.generation++;
    assert(!E::prepareControlSettings(c, target(), 12, r, p, 100000, 110000)); saved.check(c);
    p = prerequisites(r); p.maxAgeUs = 0; p.controlIdentity.operationId = 0;
    assert(!E::prepareControlSettings(c, target(), 12, r, p, 100000, 110000)); saved.check(c);
    p = prerequisites(r); p.maxAgeUs = 0; p.controlLatestUs = 100001;
    assert(!E::prepareControlSettings(c, target(), 12, r, p, 100000, 110000)); saved.check(c);
}
int main(){
    testOptionalAgePolicy();readAndPreservation();rangesAndWholeCandidate();identityEffectsAndBounds();fullCapacity();deadlineEvidence();previousSettingsBudget();previousSettingsEnvelope();lifecycle();}
