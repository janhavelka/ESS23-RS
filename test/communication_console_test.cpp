// SPDX-License-Identifier: MIT
#include "../examples/probe_cli/ProbeConsole.h"
#include <cassert>
#include <cstring>
#include <limits>
#include <string>
#include <vector>
namespace Probe = MotorControlRSExample::Probe;
namespace Ess = MotorControlRS::ESS_RS;
namespace {
struct Fake {
    std::vector<std::string> lines;
    unsigned calls=0, writes=0;
    Probe::CommunicationCommand request;
    Probe::CommunicationView view;
    Ess::CommunicationContext retained;
    Probe::Action result=Probe::Action::OK;
    static bool emit(void* p,const char* data,std::size_t size) {
        assert(size<Probe::OUTPUT_CAPACITY && data[0]=='{' && data[size-1]=='}');
        static_cast<Fake*>(p)->lines.emplace_back(data,size); return true;
    }
    static void snapshot(void*,Probe::Snapshot& out) { out.address=7; }
    static Probe::Action communication(void* p,const Probe::CommunicationCommand* command,Probe::CommunicationView& out) {
        auto& f=*static_cast<Fake*>(p); ++f.calls;
        if (command) { f.request=*command; if(command->kind==Probe::CommunicationCommandKind::BEGIN) ++f.writes; }
        out=f.view; return f.result;
    }
    static Probe::Action probe(void*,uint32_t,uint8_t,uint32_t&) { return Probe::Action::UNAVAILABLE; }
    static Probe::Action recover(void*,uint32_t,uint32_t&) { return Probe::Action::UNAVAILABLE; }
    static void reset(void*) {}
    Probe::Host hooks() { Probe::Host h;h.context=this;h.emitLine=emit;h.snapshot=snapshot;h.communication=communication;
        h.startProbe=probe;h.recover=recover;h.resetStats=reset;return h; }
    bool has(const char* s) const { return !lines.empty() && lines.back().find(s)!=std::string::npos; }
};
void send(Probe::Console& console,const std::string& command) { for(char c:command)console.feed(c);console.feed('\n'); }
}
int main() {
    Fake f;Probe::Console c(f.hooks(), Probe::Format::JSON);
    send(c,"help communication");assert(f.has("plan|begin"));
    send(c,"profile ess_rs communication");assert(f.has("\"action\":-1"));assert(f.has("\"context\":null"));
    const char* invalid[]={"begin address 0","begin address 248","plan baud 57600","begin format 8E2","begin baud 9600 0","begin baud 9600 248","begin baud 9600 1 extra","begin baud 4294967296","host other","confirm","finish extra","save","plan arbitrary 1"};
    const auto calls=f.calls;
    for(const char* line:invalid){send(c,std::string("profile ess_rs communication ")+line);assert(f.has("\"ok\":false"));}
    assert(f.calls==calls && f.writes==0);
    send(c,"profile ess_rs communication plan address 247 1");
    assert(f.request.kind==Probe::CommunicationCommandKind::PREVIEW && f.request.request.address==247 && f.request.address==1);
    assert(f.has("\"register\":19,\"value\":247"));assert(f.has("\"save\":\"required\",\"restart\":\"unresolved\""));
    assert(f.writes==0);
    send(c,"profile ess_rs communication plan baud 9600");
    assert(f.request.request.baud==Ess::BaudRateCode::BAUD_9600 && f.request.address==7);
    assert(f.has("\"register\":20,\"value\":3"));
    send(c,"profile ess_rs communication plan format 8O1");assert(f.request.request.format==Ess::SerialFormatCode::FORMAT_8O1);
    f.result=Probe::Action::UNAVAILABLE;
    send(c,"profile ess_rs communication begin baud 19200 2");
    assert(f.has("\"ok\":false") && f.has("\"route_ready\":false") && f.has("\"plan\":{\"field\":\"baud\""));
    f.result=Probe::Action::OK;f.view.context=&f.retained;f.view.pending=f.view.owned=true;
    f.retained.operationId=0xFFFFFFFF;f.retained.effects=f.retained.uncertain=true;
    f.retained.confirmations=2;f.retained.beforeTarget.id=0xFFFFFFFF;f.retained.beforeTarget.address=247;f.retained.beforeTarget.generation=0xFFFFFFFF;
    f.retained.writeEvidence.wire.earliestUs=std::numeric_limits<uint64_t>::max();
    f.retained.writeEvidence.eligibleUs=1234567890123ULL;
    f.retained.writeEvidence.deadlineUs=1234567899999ULL;
    f.retained.writeEvidence.wire.latestUs=std::numeric_limits<uint64_t>::max();
    f.retained.writeEvidence.wire.deliveredUs=std::numeric_limits<uint64_t>::max();
    f.retained.writeEvidence.wire.length=Ess::ACTION_MAX_REPLY_BYTES;
    std::memset(f.retained.writeEvidence.wire.raw,0xFF,Ess::ACTION_MAX_REPLY_BYTES);
    for(auto& e:f.retained.confirmationEvidence)e=f.retained.writeEvidence;
    send(c,"profile ess_rs communication inspect");
    assert(f.has("\"pending\":true,\"owned\":true"));assert(f.has("\"effects\":true,\"uncertain\":true"));
    assert(f.has("18446744073709551615"));assert(f.has("\"confirmation_evidence\":["));
    assert(f.has("\"eligible_us\":1234567890123,\"deadline_us\":1234567899999"));
    const char* commands[]={"host before","host requested","confirm before","confirm requested","finish"};
    unsigned kind=2;
    for(const char* line:commands){send(c,std::string("profile ess_rs communication ")+line);assert(static_cast<unsigned>(f.request.kind)==kind++);}
    const auto writes=f.writes;send(c,"profile ess_rs communication inspect");assert(f.writes==writes);
    Fake absent;auto h=absent.hooks();h.communication=nullptr;Probe::Console legacy(h, Probe::Format::JSON);
    send(legacy,"help");assert(!absent.has("\"communication\""));
    send(legacy,"profile ess_rs communication inspect");assert(absent.has("unavailable"));assert(absent.calls==0);
    return 0;
}
