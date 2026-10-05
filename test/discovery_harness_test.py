"""Bounded ESS discovery grammar, immutable wire evidence and no replay."""
import copy
import importlib.util
import json
import subprocess
import sys
from pathlib import Path
import unittest

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location("bench_discovery",ROOT/"scripts/bench_probe.py")
bench=importlib.util.module_from_spec(spec);spec.loader.exec_module(bench)
CONSOLE_BINARY=Path(sys.argv.pop(1)) if len(sys.argv)>1 and not sys.argv[1].startswith("-") else None

def wire(prefix):
    raw=bytes(prefix);return (raw+bench.wire_crc(raw).to_bytes(2,"little")).hex()

def finding(address=1,op=8,identity=False,outcome=1):
    raw=wire([address,3,2,3,5]) if outcome==1 else wire([address,0x83,2]) if outcome==2 else ""
    evidence=[0,1,0 if outcome in (1,2) else 3,raw,len(bytes.fromhex(raw)),outcome in (1,2),100,101 if raw else 0,102 if raw else 0,103,0,8,False,
              "OK" if outcome==1 else "EXCEPTION" if outcome==2 else "ILLEGAL_VALUE",2 if outcome==2 else 0,10 if outcome==2 else 0]
    ie=[0,4,0,wire([address,3,8,3,5,0,41,0,address,0,0]),13,True,110,111,112,113,0,8,False,"OK",0,0] if identity else [0,0,0,"",0,False,0,0,0,0,0,0,False,"OK",0,0]
    return dict(profile="ess_rs",target=[address,address,9],operation_id=op,serial=[True,115200,8,1,1],
                tx_hex=wire([address,3,0,0,0,1]),started_us=100,deadline_us=500100,outcome=outcome,
                confidence=2 if outcome==1 else 1 if outcome==2 else 0,raw_model_known=outcome==1,raw_model=773 if outcome==1 else 0,
                probe=evidence,identity_attempted=identity,identity_admission=[op+1,500110] if identity else [0,0],
                identity_known=identity,identity_ambiguous=False,collision_excluded=False,
                identity=[773,41,address,0] if identity else [0,0,0,0],identity_evidence=ie)

def scan(*,phase=5,outcome=1,identity=False,findings=None,released=False):
    findings=[finding(identity=identity)] if findings is None else findings
    return dict(operation_id=7,phase=phase,outcome=outcome,owned=phase!=5,restored=phase==5,released=released,cancel_requested=False,
                requests=len(findings)*(2 if identity else 1),count=len(findings),tuple_index=0,address=1,started_us=100,deadline_us=5000100,
                finished_us=200 if phase==5 else 0,original_target=[1,1,9],original_tuple=dict(baud=115200,format="8N1"),original_serial_generation=1,
                settings=dict(first=1,last=3,query_ms=500,overall_ms=5000,request_limit=16,result_limit=8,identity=identity,tuples=[dict(baud=115200,format="8N1")]),
                evidence_columns=bench.DISCOVERY_EVIDENCE_COLUMNS,findings=findings)

def response(tokens=(),**changes):
    action=bench.discovery_arguments(tokens)["action"]
    r=dict(type="reply",profile="ess_rs",id=1,command="discover",ok=True,result="accepted",
           action={"begin":0,"cancel":1,"restore":2,"finish":3,"inspect":-1}[action],scan=scan())
    r.update(changes);return r

class Clock:
    def __init__(self):self.now=0
    def __call__(self):return self.now
    def sleep(self,value):self.now+=value

class Port:
    def __init__(self,factory):self.factory=factory;self.data=b"";self.sent=[]
    def write(self,data):
        self.sent.append(data);parts=data.decode().strip().split();r=self.factory(tuple(parts[2:]));r["id"]=int(parts[0][1:]);self.data+=json.dumps(r).encode()+b"\n";return len(data)
    def read(self,size):r,self.data=self.data[:size],self.data[size:];return r

class Campaign:
    def __init__(self,phase=5):self.clock=Clock();self.sleep=self.clock.sleep;self.calls=[];self.phase=phase;self.synchronized=True;self.events=[]
    def emit(self,*a,**k):self.events.append((a,k))
    def command(self,name,*,host_args,timeout_s):
        assert name=="discover";self.calls.append(host_args)
        return response(host_args,scan=scan(phase=self.phase,outcome=1 if self.phase==5 else 6))

class DiscoveryTests(unittest.TestCase):
    def console(self,factory=response):
        clock=Clock();port=Port(factory);c=bench.Console(port,clock=clock,sleeper=clock.sleep);c.identified=True;return c,port
    def test_strict_finite_candidate_grammar(self):
        parsed=bench.discovery_arguments(("manufacturer","stepperonline","addresses","1","3","tuple","115200","8N1","identity"))
        self.assertEqual(parsed["tuples"],[dict(baud=115200,format="8N1")]);self.assertTrue(parsed["identity"])
        for t in (("addresses","0","1"),("addresses","2","1"),("addresses","1","248"),("tuple","57600","8N1"),
                  ("tuple","9600","8E2"),("manufacturer","leadshine"),("profile","iem_rs"),("profile","ess_rs","manufacturer","stepperonline"),
                  ("query-ms","0"),("query-ms","5001"),("overall-ms","60001"),("requests","257"),("results","9"),("identity","identity"),
                  ("tuple","9600","8N1","tuple","9600","8N1"),("cancel","extra"),("save",),("broadcast",)):
            with self.subTest(tokens=t),self.assertRaises(ValueError):bench.discovery_arguments(t)
    def test_actual_wire_controls_are_local_correlated_commands(self):
        c,p=self.console();r=c.command("discover",host_args=("inspect",),timeout_s=.1)
        self.assertEqual(p.sent,[b"@1 discover inspect\n"]);self.assertFalse(c.operations);self.assertEqual(r["scan"]["findings"][0]["raw_model"],773)
    @unittest.skipUnless(CONSOLE_BINARY,"native console fixture supplied by CTest")
    def test_production_console_json_checks_against_public_probe_path(self):
        item=json.loads(subprocess.check_output([str(CONSOLE_BINARY),"--json"],text=True))
        c,p=self.console(lambda t:copy.deepcopy(item));r=c.command("discover",host_args=("inspect",),timeout_s=.1)
        self.assertEqual(r["scan"]["findings"][0]["raw_model"],773)
        self.assertEqual(p.sent,[b"@1 discover inspect\n"])
    def test_refinement_keeps_raw_identity_and_mapping_ambiguity(self):
        c,p=self.console(lambda t:response(t,scan=scan(identity=True)))
        r=c.command("discover",host_args=("identity",),timeout_s=.1)
        self.assertEqual(r["scan"]["findings"][0]["confidence"],2);self.assertFalse(r["scan"]["findings"][0]["identity_ambiguous"]);self.assertEqual(len(p.sent),1)
    def test_refinement_needs_counted_admission_and_on_time_closure(self):
        for mutate in (lambda s:s.update(requests=1), lambda s:s["findings"][0]["identity_evidence"].__setitem__(8,500111),
                       lambda s:s["findings"][0]["identity_evidence"].__setitem__(6,5000100),
                       lambda s:s["findings"][0].update(identity_admission=[8,500110]),
                       lambda s:s["findings"][0].update(identity_admission=[9,500111])):
            retained=scan(identity=True);mutate(retained)
            retained["findings"][0]["identity_evidence"][9]=max(retained["findings"][0]["identity_evidence"][6],retained["findings"][0]["identity_evidence"][8])
            c,p=self.console(lambda t:response(t,scan=retained))
            with self.subTest(retained=retained),self.assertRaises(bench.BenchError):c.command("discover",host_args=("inspect",),timeout_s=.1)
            self.assertEqual(len(p.sent),1)
    def test_checked_exception_and_timeout_remain_different(self):
        for outcome in (2,5):
            c,p=self.console(lambda t:response(t,scan=scan(findings=[finding(outcome=outcome)])))
            r=c.command("discover",host_args=("inspect",),timeout_s=.1)
            self.assertEqual(r["scan"]["findings"][0]["outcome"],outcome);self.assertFalse(r["scan"]["findings"][0]["raw_model_known"])
    def test_forged_checked_raw_and_uniqueness_claims_reject_without_replay(self):
        mutations=(lambda s:s["findings"][0].update(tx_hex=wire([1,6,0,45,0,66])),
                   lambda s:s["findings"][0].update(target=[1,4,9]),lambda s:s["findings"][0].update(raw_model=5),
                   lambda s:s["findings"][0].update(collision_excluded=True),lambda s:s["findings"][0].update(serial=[True,9600,8,1,1]),
                   lambda s:s["findings"][0]["probe"].__setitem__(3,wire([2,3,2,3,5])),
                   lambda s:s["findings"][0]["probe"].__setitem__(8,500101),lambda s:s["findings"][0]["probe"].__setitem__(5,False),
                   lambda s:s.update(requests=17),lambda s:s.update(count=9),lambda s:s.update(restored=False),
                   lambda s:s.update(evidence_columns=[]),lambda s:s["findings"][0].update(confidence=1),
                   lambda s:s["findings"][0]["identity_evidence"].__setitem__(3,"0103"))
        for mutate in mutations:
            s=scan();mutate(s);c,p=self.console(lambda t:response(t,scan=copy.deepcopy(s)))
            with self.subTest(scan=s),self.assertRaises(bench.BenchError):c.command("discover",host_args=("inspect",),timeout_s=.1)
            self.assertEqual(len(p.sent),1)
    def test_limits_and_candidates_must_match_admission(self):
        c,p=self.console()
        with self.assertRaises(bench.BenchError):c.command("discover",host_args=("addresses","2","3"),timeout_s=.1)
        self.assertEqual(len(p.sent),1)
    def test_retained_partial_findings_cannot_disappear_or_change(self):
        retained=scan(phase=4,outcome=6);c,p=self.console(lambda t:response(t,scan=copy.deepcopy(retained)))
        c.command("discover",host_args=("inspect",),timeout_s=.1);retained["findings"][0]["probe"][9]+=1
        with self.assertRaises(bench.BenchError):c.command("discover",host_args=("inspect",),timeout_s=.1)
        self.assertEqual(len(p.sent),2)
    def test_duplicate_attempt_ids_and_premature_release_reject(self):
        for retained in (scan(findings=[finding(),finding(address=2)]),scan(phase=4,outcome=6,released=True)):
            c,p=self.console(lambda t:response(t,scan=retained))
            with self.assertRaises(bench.BenchError):c.command("discover",host_args=("inspect",),timeout_s=.1)
    def test_distinct_addresses_and_budget_end_keep_partial_results(self):
        retained=scan(outcome=4,findings=[finding(),finding(address=2,op=9)])
        c,p=self.console(lambda t:response(t,scan=retained));r=c.command("discover",host_args=("inspect",),timeout_s=.1)
        self.assertEqual(r["scan"]["count"],2);self.assertEqual(r["scan"]["outcome"],4)
    def test_interlock_is_explicit_and_cannot_claim_restored(self):
        retained=scan(phase=4,outcome=6);c,p=self.console(lambda t:response(t,scan=copy.deepcopy(retained)))
        self.assertTrue(c.command("discover",host_args=("inspect",),timeout_s=.1)["scan"]["owned"])
        retained["restored"]=True
        with self.assertRaises(bench.BenchError):c.command("discover",host_args=("inspect",),timeout_s=.1)
    def test_successful_campaign_releases_once_and_interlock_never_recovers(self):
        campaign=Campaign();bench.discovery_campaign(campaign,tokens=("addresses","1","3"),timeout_s=1)
        self.assertEqual(campaign.calls,[("addresses","1","3"),("finish",)])
        campaign=Campaign(phase=4)
        with self.assertRaises(bench.BenchError):bench.discovery_campaign(campaign,tokens=(),timeout_s=1)
        self.assertEqual(campaign.calls,[()])
    def test_poll_budget_requests_cancel_once_without_probe_replay(self):
        campaign=Campaign(phase=1)
        with self.assertRaises(bench.BenchError):bench.discovery_campaign(campaign,tokens=("overall-ms","1"),timeout_s=.01)
        self.assertEqual(campaign.calls[0],("overall-ms","1"));self.assertEqual(campaign.calls[-1],("cancel",))
        self.assertEqual(campaign.calls.count(("cancel",)),1)
    def test_control_action_correlation_rejects(self):
        c,p=self.console(lambda t:response(t,action=0))
        with self.assertRaises(bench.BenchError):c.command("discover",host_args=("inspect",),timeout_s=.1)
    def test_bad_correlations_invalidate_session_without_replay(self):
        c,p=self.console(lambda t:response(t,command="probe"))
        with self.assertRaises(bench.BenchError):c.command("discover",host_args=("inspect",),timeout_s=.1)
        self.assertFalse(c.synchronized);self.assertEqual(len(p.sent),1)
    def test_scan_does_not_reuse_original_host_generation_for_next_probe(self):
        class MixedPort(Port):
            def write(self,data):
                if data.decode().split()[1] == "discover":return super().write(data)
                self.sent.append(data);rid=int(data.decode().split()[0][1:])
                common=dict(id=rid,profile="ess_rs",command="probe",ok=True,operation_id=9,address=1)
                admitted=dict(common,type="reply",result="accepted")
                terminal=dict(common,type="probe",command_id=rid,raw_model=773,transport="FRAME",codec="OK",outcome="success",
                              execution_unknown=False,tx_bytes=8,rx_bytes=7,duration_us=100,timing_valid=True,raw_truncated=False,
                              observed_earliest_us=101,observed_latest_us=102,delivered_us=103,
                              host_serial=dict(known=True,baud=115200,format="8N1",generation=3))
                self.data+=(json.dumps(admitted)+"\n"+json.dumps(terminal)+"\n").encode();return len(data)
        clock=Clock();p=MixedPort(response);c=bench.Console(p,clock=clock,sleeper=clock.sleep);c.identified=True
        c.serial=dict(active_known=True,blocked=False,active=dict(baud=115200,format="8N1"),serial_generation=1)
        c.command("discover",host_args=("inspect",),timeout_s=.1)
        self.assertIsNone(c.serial)
        handle=c.begin("probe",timeout_s=.1)
        self.assertEqual(c.wait(handle)["host_serial"]["generation"],3)

if __name__=="__main__":unittest.main()
