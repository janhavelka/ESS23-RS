// SPDX-License-Identifier: MIT
#include <MotorControlRS/profiles/ess_rs/Segments.h>
#include <MotorControlRS/profiles/ess_rs/Registers.h>
#include <cassert>
#include <cstring>
#include <vector>
using namespace MotorControlRS;
namespace E = MotorControlRS::ESS_RS;
namespace {
ReadTarget target() { ReadTarget t; t.id=4; t.address=1; t.generation=7; return t; }
uint32_t field(E::DriverField f) { return static_cast<uint32_t>(f); }
template<class T> struct Saved {
    unsigned char bytes[sizeof(T)];
    explicit Saved(const T& v) { std::memcpy(bytes, &v, sizeof(v)); }
    void check(const T& v) const { assert(!std::memcmp(bytes, &v, sizeof(v))); }
};
void crc(std::vector<uint8_t>& bytes) {
    uint16_t v=0xFFFF;
    for (auto b:bytes) { v ^= b; for (unsigned i=0;i<8;++i) v=(v&1)?(v>>1)^0xA001:v>>1; }
    bytes.push_back(static_cast<uint8_t>(v)); bytes.push_back(static_cast<uint8_t>(v>>8));
}
std::vector<uint8_t> reply(std::initializer_list<uint16_t> words) {
    std::vector<uint8_t> b={1,3,static_cast<uint8_t>(words.size()*2)};
    for (auto w:words) { b.push_back(static_cast<uint8_t>(w>>8)); b.push_back(static_cast<uint8_t>(w)); }
    crc(b); return b;
}
ActionEvent event(const E::DriverContext& c, ReadEventKind kind) {
    ActionEvent e; e.transport.target=c.target; e.transport.operationId=c.operationId;
    e.transport.step=c.step; e.transport.kind=kind; return e;
}
ActionEvent frame(const E::DriverContext& c, const std::vector<uint8_t>& b, uint64_t at) {
    auto e=event(c, ReadEventKind::FRAME); e.transport.frame=b.data(); e.transport.length=b.size();
    e.transport.txAccepted=8; e.txComplete=e.responseConfirmed=e.transport.qualified=true;
    e.transport.earliestUs=at; e.transport.latestUs=at+1; return e;
}
void supply(E::DriverContext& c, const std::vector<uint8_t>& b, bool confirmed=true) {
    auto e=frame(c,b,c.servicedUs+2); e.responseConfirmed=confirmed;
    assert(E::advanceDriver(c,e,c.servicedUs+4));
}
E::PreparedDriver work(const E::DriverContext& c) {
    E::PreparedDriver w; assert(E::nextDriver(c,c.servicedUs,w)); return w;
}
E::DriverContext read(E::SegmentKind kind=E::SegmentKind::POSITION, uint8_t index=1) {
    E::DriverContext c; assert(E::prepareSegmentRead(c,target(),11,17,100,10000,kind,index)); return c;
}
E::DriverObservation snapshot(E::SegmentKind kind=E::SegmentKind::POSITION, uint8_t index=1) {
    auto c=read(kind,index);
    supply(c,kind==E::SegmentKind::POSITION ? reply({0x1234,0x5678,120,50,50}) :
        kind==E::SegmentKind::SPEED ? reply({100,50,50}) : reply({0}));
    E::DriverObservation v; assert(E::getDriver(c,v)); return v;
}
E::DriverRequest request(E::DriverGroup group=E::DriverGroup::POSITION_SEGMENT,uint8_t index=1) {
    E::DriverRequest r; r.group=group; r.segmentIndex=index; r.configurationGeneration=17;
    r.fields=field(group==E::DriverGroup::SEGMENT_START_SPEED?E::DriverField::SEGMENT_START_SPEED:E::DriverField::SEGMENT_SPEED);
    r.segmentSpeed=121; r.segmentStartSpeed=1; r.segmentAcceleration=51; r.segmentDeceleration=52; return r;
}
E::DriverPrerequisites prerequisites(const E::DriverRequest& r) {
    E::DriverPrerequisites p; p.configurationGeneration=17;
    p.previous=snapshot(r.group==E::DriverGroup::POSITION_SEGMENT ? E::SegmentKind::POSITION :
        r.group==E::DriverGroup::SPEED_SEGMENT ? E::SegmentKind::SPEED : E::SegmentKind::START_SPEED,r.segmentIndex);
    p.stationaryQualified=true; p.stationaryTarget=target(); p.rawMotion=1;
    p.stationaryEarliestUs=110; p.stationaryLatestUs=120; p.maxAgeUs=10000;
    p.externalTriggerInhibitedQualified=true; p.triggerTarget=target(); p.triggerConfigurationGeneration=17;
    p.triggerEarliestUs=111; p.triggerLatestUs=121; p.qualifiedSegment=r; return p;
}
E::DriverContext update(const E::DriverRequest& r,bool echo=false,uint64_t deadline=9000) {
    auto p=prerequisites(r); p.allowEchoReadback=echo;
    E::DriverContext c; assert(E::prepareSegmentSettings(c,target(),12,r,p,200,deadline)); return c;
}
void ack(E::DriverContext& c,bool confirmed=true) {
    auto w=work(c); assert(w.write); supply(c,std::vector<uint8_t>(w.bytes,w.bytes+w.length),confirmed);
}
void reject(const E::DriverRequest& r,const E::DriverPrerequisites& p,Err code=Err::INVALID_CONFIG) {
    E::DriverContext c; c.operationId=999; Saved<E::DriverContext> saved(c);
    auto s=E::prepareSegmentSettings(c,target(),12,r,p,200,9000); assert(!s&&s.code==code); saved.check(c);
}
void layouts() {
    for (uint8_t i=1;i<=16;++i) {
        const E::SegmentKind kinds[]={E::SegmentKind::POSITION,E::SegmentKind::SPEED,E::SegmentKind::START_SPEED};
        for (auto k:kinds) {
            auto c=read(k,i); const auto w=work(c); const auto repeat=work(c);
            assert(!w.write&&w.length==8&&!std::memcmp(w.bytes,repeat.bytes,8));
            const auto* descriptor=k==E::SegmentKind::POSITION?E::positionSegmentRegister(i,E::PositionSegmentField::PULSES):
                k==E::SegmentKind::SPEED?E::speedSegmentRegister(i,E::SpeedSegmentField::SPEED):E::segmentStartSpeedRegister(i);
            assert(descriptor&&w.reg==descriptor->address&&w.count==(k==E::SegmentKind::POSITION?5:k==E::SegmentKind::SPEED?3:1));
            supply(c,k==E::SegmentKind::POSITION?reply({0xFFFF,0x8000,3000,2000,2000}):k==E::SegmentKind::SPEED?reply({3000,2000,2000}):reply({180}));
            E::SegmentObservation out; assert(E::getSegment(c,out)&&out.index==i&&out.kind==k&&out.provenance.count==w.count);
            if (k==E::SegmentKind::POSITION) {
                assert(!out.pairOrderKnown&&out.pulseBits==0&&out.pulseWords[0]==0xFFFF);
                assert(E::getSegment(c,out,true,E::WordOrder::HIGH_WORD_FIRST)&&out.pulseBits==0xFFFF8000);
                assert(E::getSegment(c,out,true,E::WordOrder::LOW_WORD_FIRST)&&out.pulseBits==0x8000FFFF);
                Saved<E::SegmentObservation> saved(out); assert(!E::getSegment(c,out,true,static_cast<E::WordOrder>(2))); saved.check(out);
                const auto* reserve=E::positionSegmentRegister(i,E::PositionSegmentField::RESERVED);
                assert(reserve&&!E::validateReadRegistersRequest(1,w.reg,6));
                assert(!E::validateWriteSingleRegisterRequest(1,reserve->address,0));
                assert(!E::validateWriteSingleRegisterRequest(1,w.reg,0));
                assert(!E::validateWriteSingleRegisterRequest(1,w.reg+1,0));
                const uint16_t pair[]={1,2};
                assert(!E::validateWriteMultipleRegistersRequest(1,w.reg,pair,2));
            }
        }
    }
    for (uint8_t index:{uint8_t(0),uint8_t(17),uint8_t(255)}) {
        E::DriverContext c; c.operationId=999; Saved<E::DriverContext> saved(c);
        for (auto k:{E::SegmentKind::POSITION,E::SegmentKind::SPEED,E::SegmentKind::START_SPEED}) {
            assert(!E::prepareSegmentRead(c,target(),11,17,100,10000,k,index)); saved.check(c);
        }
    }
    E::DriverContext c; assert(!E::prepareSegmentRead(c,target(),11,17,100,10000,static_cast<E::SegmentKind>(3),1));
    assert(!E::prepareSegmentSettings(c,target(),11,E::DriverRequest(),E::DriverPrerequisites(),100,10000));
    for (uint8_t i=3;i<16;++i) assert(field(E::driverFieldAt(i,E::DriverGroup::POSITION_SEGMENT))==0);
}
void rangesAndPolicies() {
    for (auto group:{E::DriverGroup::POSITION_SEGMENT,E::DriverGroup::SPEED_SEGMENT,E::DriverGroup::SEGMENT_START_SPEED}) {
        for (uint8_t index:{uint8_t(1),uint8_t(16)}) {
            auto r=request(group,index); const int32_t maximum=group==E::DriverGroup::SEGMENT_START_SPEED?180:3000;
            for (int32_t value:{int32_t(0),maximum}) {
                r.segmentSpeed=r.segmentStartSpeed=value; auto c=update(r); assert(work(c).value==value);
            }
            r.segmentSpeed=r.segmentStartSpeed=maximum+1; reject(r,prerequisites(r));
            r.segmentSpeed=r.segmentStartSpeed=2147483647; reject(r,prerequisites(r));
            r.segmentSpeed=r.segmentStartSpeed=(-2147483647-1); reject(r,prerequisites(r),group==E::DriverGroup::POSITION_SEGMENT?Err::INVALID_CONFIG:Err::UNSUPPORTED);
            r.segmentSpeed=r.segmentStartSpeed=-1; reject(r,prerequisites(r),group==E::DriverGroup::POSITION_SEGMENT?Err::INVALID_CONFIG:Err::UNSUPPORTED);
            r=request(group,index); auto p=prerequisites(r);
            p.externalTriggerInhibitedQualified=false; reject(r,p);
            p=prerequisites(r); p.triggerTarget.generation++; reject(r,p);
            p=prerequisites(r); p.triggerConfigurationGeneration++; reject(r,p);
            p=prerequisites(r); p.triggerEarliestUs=201; reject(r,p);
            p=prerequisites(r); p.triggerLatestUs=201; reject(r,p);
            p=prerequisites(r); p.maxAgeUs=80; reject(r,p);
            p=prerequisites(r); p.qualifiedSegment.segmentIndex++; reject(r,p);
            p=prerequisites(r); p.qualifiedSegment.segmentSpeed+=65536; p.qualifiedSegment.segmentStartSpeed+=65536; reject(r,p);
            p=prerequisites(r); p.qualifiedSegment.segmentSpeed-=65536; p.qualifiedSegment.segmentStartSpeed-=65536; reject(r,p);
            p=prerequisites(r); p.previous.segmentIndex=index==1?2:1; reject(r,p);
            p=prerequisites(r); p.previous.raw[0]++; reject(r,p);
            p=prerequisites(r); p.stationaryQualified=false; reject(r,p);
            p=prerequisites(r); p.rawMotion|=4; reject(r,p);
            p=prerequisites(r); p.rawAlarm=1; reject(r,p);
        }
    }
    auto r=request(); r.fields|=field(E::DriverField::SEGMENT_PULSE_TARGET); reject(r,prerequisites(r),Err::UNSUPPORTED);
    r=request(); r.fields|=field(E::DriverField::CUSTOM_OUTPUT); reject(r,prerequisites(r));
    r=request(); r.fields|=field(E::DriverField::SEGMENT_ACCELERATION); r.segmentAcceleration=2001; reject(r,prerequisites(r));
    r=request(); r.fields|=field(E::DriverField::SEGMENT_DECELERATION); r.segmentDeceleration=2001; reject(r,prerequisites(r));
    r=request(); r.fields=field(E::DriverField::SEGMENT_ACCELERATION)|field(E::DriverField::SEGMENT_DECELERATION);
    r.segmentAcceleration=0; r.segmentDeceleration=2000; auto c=update(r); assert(work(c).value==0);
    ack(c); supply(c,reply({0})); assert(work(c).value==2000); ack(c); supply(c,reply({2000})); assert(c.state==ReadState::SUCCEEDED);
    auto p=prerequisites(r); p.qualifiedSegment.segmentDeceleration--; reject(r,p);
    for (uint8_t i:{uint8_t(0),uint8_t(17)}) { r.segmentIndex=i; reject(r,p); }
}
void progressAndFailure() {
    auto r=request(); r.fields|=field(E::DriverField::SEGMENT_ACCELERATION)|field(E::DriverField::SEGMENT_DECELERATION);
    for (unsigned boundary=0;boundary<6;++boundary) {
        auto c=update(r);
        for (unsigned i=0;i<boundary;++i) { const auto w=work(c); if(w.write) ack(c); else supply(c,reply({w.value})); }
        const auto before=c.completedSteps; auto cancel=event(c,ReadEventKind::CANCEL);
        assert(E::advanceDriver(c,cancel,c.servicedUs+1));
        assert(c.outcome==E::DriverOutcome::CANCELLED&&c.completedSteps==before);
        Saved<E::DriverContext> saved(c); assert(!E::advanceDriver(c,cancel,c.servicedUs+1)); saved.check(c);
        assert(work(c).kind==E::ActionWork::DONE&&!work(c).length);
    }
    auto c=update(r); E::PreparedDriver w; ack(c); supply(c,reply({121}));
    assert(c.progress[0].acknowledged&&c.progress[0].readbackKnown&&!c.uncertain);
    auto lost=event(c,ReadEventKind::TRANSPORT_FAILURE); lost.transport.txAccepted=8; lost.txComplete=true; lost.transport.executionUnknown=true;
    assert(E::advanceDriver(c,lost,c.servicedUs+1));
    assert(c.outcome==E::DriverOutcome::TRANSPORT_ERROR&&c.uncertain);
    assert(c.effects==(field(E::DriverField::SEGMENT_SPEED)|field(E::DriverField::SEGMENT_ACCELERATION)));
    assert(c.progress[0].readback==121&&c.progress[1].execution==ActionExecution::UNKNOWN&&c.progress[2].execution==ActionExecution::NOT_TRANSMITTED);
    c=update(request()); auto interrupted=event(c,ReadEventKind::CANCEL); interrupted.transport.txAccepted=8;
    interrupted.txComplete=true; interrupted.transport.executionUnknown=true;
    assert(E::advanceDriver(c,interrupted,c.servicedUs+1)&&c.uncertain&&c.effects==field(E::DriverField::SEGMENT_SPEED));
    c=update(request()); ack(c); supply(c,reply({122})); assert(c.outcome==E::DriverOutcome::READBACK_MISMATCH&&c.uncertain);
    c=update(request()); ack(c,false); assert(c.outcome==E::DriverOutcome::UNCONFIRMED_RESPONSE&&c.uncertain);
    c=update(request(),true); ack(c,false);
    assert(c.state==ReadState::ACTIVE&&!c.progress[0].acknowledged&&c.progress[0].execution==ActionExecution::UNKNOWN&&c.uncertain);
    supply(c,reply({121})); assert(c.state==ReadState::SUCCEEDED&&!c.uncertain&&!c.progress[0].acknowledged&&c.progress[0].execution==ActionExecution::UNKNOWN);
    assert(!c.progress[0].activeKnown&&!c.observations[0].responseConfirmed&&c.observations[1].responseConfirmed);
    c=update(request(),true); ack(c,false); supply(c,reply({121}),false); assert(c.outcome==E::DriverOutcome::UNCONFIRMED_RESPONSE&&c.uncertain);
    c=update(request(),true); w=work(c); auto malformed=std::vector<uint8_t>(w.bytes,w.bytes+8); malformed[4]^=1;
    supply(c,malformed,false); assert(c.outcome==E::DriverOutcome::REPLY_ERROR&&c.completedSteps==0);
    c=update(request(),true); auto exception=std::vector<uint8_t>{1,0x86,2}; crc(exception); supply(c,exception,false);
    assert(c.outcome==E::DriverOutcome::REPLY_ERROR&&c.progress[0].execution==ActionExecution::UNKNOWN);
    c=update(request()); supply(c,exception); assert(c.progress[0].execution==ActionExecution::REJECTED&&!c.uncertain);
    c=update(request()); w=work(c); auto bytes=std::vector<uint8_t>(w.bytes,w.bytes+8); auto e=frame(c,bytes,210);
    e.transport.target.generation++; Saved<E::DriverContext> saved(c); assert(!E::advanceDriver(c,e,212)); saved.check(c);
    e=frame(c,bytes,210); e.transport.step++; assert(!E::advanceDriver(c,e,212)); saved.check(c);
    auto q=prerequisites(request()); q.maxAgeUs=120; q.triggerEarliestUs=100; q.triggerLatestUs=110;
    E::DriverContext timed; assert(E::prepareSegmentSettings(timed,target(),12,request(),q,200,9000));
    assert(work(timed).deadlineUs==220); assert(!E::nextDriver(timed,220,w));
    auto deadline=event(timed,ReadEventKind::DEADLINE); assert(E::advanceDriver(timed,deadline,220)&&timed.outcome==E::DriverOutcome::DEADLINE);
    c=update(request()); w=work(c); auto foreignEcho=std::vector<uint8_t>(w.bytes,w.bytes+6);
    foreignEcho[5]++; crc(foreignEcho); supply(c,foreignEcho); assert(c.outcome==E::DriverOutcome::REPLY_ERROR&&c.uncertain);
    c=update(request(),false,230); ack(c); auto rb=reply({121}); e=frame(c,rb,230);
    assert(E::advanceDriver(c,e,232)&&c.outcome==E::DriverOutcome::DEADLINE&&c.uncertain);
    c=update(request(),false,230); ack(c); e=frame(c,rb,225);
    assert(E::advanceDriver(c,e,1000)&&c.state==ReadState::SUCCEEDED&&c.deadlineUs==230);
}
void readFailuresAndLifetime() {
    auto c=read(); auto b=reply({0x1234,0x5678,0xFFFF,50,50}); supply(c,b); b.assign(b.size(),0);
    E::SegmentObservation out; assert(E::getSegment(c,out)&&out.speed==0xFFFF&&!(out.knownFields&field(E::DriverField::SEGMENT_SPEED)));
    Saved<E::SegmentObservation> saved(out);
    for (unsigned failure=0;failure<5;++failure) {
        c=read(); auto bad=reply({0,0,120,50,50});
        if(failure==0) bad.back()^=1;
        if(failure==1) { bad[0]=2; bad.resize(bad.size()-2); crc(bad); }
        if(failure==2) bad=reply({0,120,50,50});
        if(failure==3) { bad={1,0x83,2}; crc(bad); }
        if(failure==4) { bad[1]=4; bad.resize(bad.size()-2); crc(bad); }
        supply(c,bad); assert(c.state==ReadState::FAILED&&!E::getSegment(c,out)); saved.check(out);
    }
    c=read(E::SegmentKind::SPEED); supply(c,reply({0xFFFF,2001,0})); assert(E::getSegment(c,out)&&out.speed==0xFFFF&&out.acceleration==2001);
    assert(!(out.knownFields&field(E::DriverField::SEGMENT_SPEED))&&!(out.knownFields&field(E::DriverField::SEGMENT_ACCELERATION)));
}
}
int main() { layouts(); rangesAndPolicies(); progressAndFailure(); readFailuresAndLifetime(); }
