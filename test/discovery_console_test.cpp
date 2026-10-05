// SPDX-License-Identifier: MIT
#include "../examples/probe_cli/ProbeConsole.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>
namespace Probe = MotorControlRSExample::Probe;
namespace Core = MotorControlRS;
namespace MotorControlRSExample { namespace Probe {
bool discoveryScan(char*,std::size_t,std::size_t&,const DiscoveryScan*);
}}
namespace {
struct Fake {
    std::vector<std::string> lines;
    Probe::DiscoveryCommand request;
    Probe::DiscoveryScan scan;
    unsigned calls=0, begins=0;
    bool blocked=false;
    static bool emit(void* context,const char* data,std::size_t size) {
        auto& f=*static_cast<Fake*>(context);
        if(f.blocked)return false;
        assert(size<Probe::OUTPUT_CAPACITY && data[0]=='{' && data[size-1]=='}');
        f.lines.emplace_back(data,size);return true;
    }
    static Probe::Action discover(void* context,const Probe::DiscoveryCommand* request,Probe::DiscoveryView& out) {
        auto& f=*static_cast<Fake*>(context);++f.calls;
        if(request){f.request=*request;if(request->kind==Probe::DiscoveryCommandKind::BEGIN)++f.begins;}
        out.scan=&f.scan;return Probe::Action::OK;
    }
    static void snapshot(void*,Probe::Snapshot&) {}
    static Probe::Action unavailable(void*,uint32_t,uint8_t,uint32_t&) {return Probe::Action::UNAVAILABLE;}
    static Probe::Action recover(void*,uint32_t,uint32_t&) {return Probe::Action::UNAVAILABLE;}
    static void reset(void*) {}
    Probe::Host hooks() {Probe::Host h;h.context=this;h.emitLine=emit;h.discovery=discover;h.snapshot=snapshot;h.startProbe=unavailable;h.recover=recover;h.resetStats=reset;return h;}
    bool has(const char* text)const{return !lines.empty() && lines.back().find(text)!=std::string::npos;}
};
void send(Probe::Console& c,const std::string& text){for(char value:text)c.feed(value);c.feed('\n');}
}
int main(int argc, char** argv) {
    if(argc==2 && !std::strcmp(argv[1],"--json")) {
        Fake f;auto& s=f.scan;
        s.operationId=7;s.originalTarget.id=s.originalTarget.address=1;s.originalTarget.generation=9;
        s.originalSerialGeneration=1;s.startedUs=100;s.deadlineUs=5000100;s.finishedUs=200;
        s.settings.first=1;s.settings.last=3;s.settings.tupleCount=1;
        s.phase=Probe::DiscoveryPhase::TERMINAL;s.outcome=Probe::DiscoveryOutcome::COMPLETE;
        s.restored=true;s.requests=s.count=s.address=1;
        auto& finding=s.findings[0];Core::ActiveSerialTuple serial;
        serial.known=true;serial.baud=115200;serial.dataBits=8;serial.parity=Core::SerialParity::NONE;serial.stopBits=1;
        assert(Core::prepareProbe(finding.request,Core::DriveProfile::ESS_RS,s.originalTarget,8,100,500100,serial));
        uint8_t raw[]={1,3,2,3,5,0,0};const uint16_t crc=Core::ESS_RS::calcCrc16(raw,5);
        raw[5]=static_cast<uint8_t>(crc);raw[6]=static_cast<uint8_t>(crc>>8);
        Core::ReadEvent event;event.target=s.originalTarget;event.operationId=8;event.frame=raw;event.length=sizeof(raw);
        event.qualified=true;event.earliestUs=101;event.latestUs=102;event.txAccepted=8;
        assert(Core::checkProbe(finding.request,event,103,finding.probe));
        Probe::Console console(f.hooks());send(console,"@1 discover inspect");
        assert(f.lines.size()==1);std::puts(f.lines[0].c_str());return 0;
    }
    Fake f;Probe::Console c(f.hooks());
    send(c,"profile list");assert(f.has("\"command\":\"profile-list\""));assert(f.has("\"bus_traffic\":false"));assert(f.has("\"exact_model\":false"));assert(f.calls==0);
    send(c,"help profile");assert(f.has("\"ok\":true"));assert(f.has("profile list"));
    send(c,"help");assert(f.has("\"profile\""));assert(f.calls==0);
    send(c,"help discover");assert(f.has("query-ms 1..5000"));
    send(c,"@77 discover");assert(f.has("\"id\":77"));assert(f.calls==1 && f.begins==1);
    assert(f.request.settings.first==0 && f.request.settings.last==0 && f.request.settings.tupleCount==0);
    send(c,"discover manufacturer stepperonline addresses 1 3 tuple 115200 8N1 identity");
    assert(f.request.settings.first==1 && f.request.settings.last==3 && f.request.settings.identity);
    assert(f.request.settings.tupleCount==1 && f.request.settings.tuples[0].baud==115200);
    send(c,"discover query-ms 1 overall-ms 60000 requests 256 results 8");
    assert(f.request.settings.queryMs==1 && f.request.settings.overallMs==60000 && f.request.settings.requestLimit==256);
    const char* invalid[]={"addresses 0 1","addresses 2 1","addresses 1 248","tuple 57600 8N1","tuple 9600 8E2","tuple 9600 8N1 tuple 9600 8N1","manufacturer leadshine","profile iem_rs","profile ess_rs manufacturer stepperonline","identity identity","query-ms 0","query-ms 5001","overall-ms 60001","requests 257","results 9","cancel extra","restore extra","finish extra","addresses 1","query-ms 2 query-ms 3","broadcast","save","address 1"};
    const auto before=f.calls;
    for(const char* text:invalid){send(c,std::string("discover ")+text);assert(f.has("\"ok\":false"));}
    assert(f.calls==before);
    send(c,"discover inspect");assert(f.has("\"action\":-1"));assert(f.begins==3);
    send(c,"discover cancel");assert(f.request.kind==Probe::DiscoveryCommandKind::CANCEL);
    send(c,"discover restore");assert(f.request.kind==Probe::DiscoveryCommandKind::RESTORE);
    send(c,"discover finish");assert(f.request.kind==Probe::DiscoveryCommandKind::FINISH);
    // A refined result has a successful seven-byte probe; its failed identity
    // may retain the maximum37-byte diagnostic prefix. Fill eight records with
    // full-width timestamps/correlation and this maximum refined evidence.
    auto& s=f.scan;s.count=Probe::DISCOVERY_MAX_RESULTS;s.requests=16;s.operationId=0xFFFFFFFF;
    s.startedUs=s.deadlineUs=s.finishedUs=std::numeric_limits<uint64_t>::max();
    s.settings.tupleCount=4;s.originalTarget.id=s.originalTarget.generation=0xFFFFFFFF;s.originalTarget.address=247;
    for(auto& finding:s.findings){
        finding.request.operationId=0xFFFFFFFF;finding.request.target=s.originalTarget;
        finding.request.startedUs=finding.request.deadlineUs=std::numeric_limits<uint64_t>::max();
        finding.request.length=sizeof(finding.request.bytes);std::memset(finding.request.bytes,0xFF,sizeof(finding.request.bytes));
        finding.identityAttempted=finding.identityKnown=finding.identityAmbiguous=true;
        finding.identityOperationId=0xFFFFFFFF;finding.identityDeadlineUs=std::numeric_limits<uint64_t>::max();
        finding.identity.rawModel=finding.identity.rawVersion=finding.identity.rawActiveNode=finding.identity.rawDip=65535;
        auto& e=finding.probe.provenance;e.length=e.receivedLength=sizeof(e.raw);std::memset(e.raw,0xFF,sizeof(e.raw));
        e.attemptedUs=e.earliestUs=e.latestUs=e.deliveredUs=std::numeric_limits<uint64_t>::max();
        e.transportDetail=e.status.detail=std::numeric_limits<int32_t>::min();e.status.code=Core::Err::INVALID_CONFIG;finding.identityEvidence=e;
        e.length=e.receivedLength=7;
    }
    char measured[16384];std::size_t measuredLength=0;assert(Probe::discoveryScan(measured,sizeof(measured),measuredLength,&s));
    std::printf("Maximum-width discovery context: %u bytes\n",static_cast<unsigned>(measuredLength));std::fflush(stdout);
    send(c,"discover inspect");assert(f.has("\"findings\":["));assert(f.has("18446744073709551615"));assert(!f.has("output_capacity"));
    std::printf("Maximum-width discovery line: %u/%u bytes\n",static_cast<unsigned>(f.lines.back().size()),static_cast<unsigned>(Probe::OUTPUT_CAPACITY));
    // Cancellation reaches the scan even when a normal reply owns blocked output.
    f.blocked=true;send(c,"discover inspect");assert(c.outputPending());
    const auto pressured=f.calls;send(c,"discover cancel");assert(f.calls==pressured+1);
    assert(f.request.kind==Probe::DiscoveryCommandKind::CANCEL);
    f.blocked=false;assert(c.serviceOutput());
    Fake absent;auto h=absent.hooks();h.discovery=nullptr;Probe::Console unavailable(h);
    send(unavailable,"discover");assert(absent.has("unavailable"));assert(absent.calls==0);
    return 0;
}
