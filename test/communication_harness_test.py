"""Communication grammar, evidence correlation and finite no-replay procedures."""
import copy
import importlib.util
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("bench_communication", ROOT / "scripts/bench_probe.py")
bench = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bench)


def endpoint(address=1, baud=115200):
    return dict(target=1,address=address,generation=9,baud=baud,data_bits=8,parity=1,stop_bits=1,serial_known=True)


def observation(step=0, *, failed=False):
    raw=bytes.fromhex("010600140003") if step==0 else bytes.fromhex("0103020003")
    raw+=bench.wire_crc(raw).to_bytes(2,"little")
    wire=dict(step=step,event=1 if failed else 0,tx_accepted=8,tx_complete=True,response_confirmed=not failed,
              qualified=not failed,execution_unknown=failed,earliest_us=0 if failed else 1100,
              latest_us=0 if failed else 1150,delivered_us=1200,frame_error=0,status="TIMEOUT" if failed else "OK",
              raw_hex="" if failed else raw.hex(),received_length=0 if failed else len(raw))
    return dict(eligible_us=1000,deadline_us=20000,wire=wire,endpoint=endpoint(),readback_known=bool(step and not failed),readback=3 if step else 0)


def context(**changes):
    c=dict(operation_id=5,field="baud",register=20,previous=0,requested=3,readback=0,readback_known=False,
           observed_active_known=False,activation_unknown=True,effects=True,uncertain=False,execution=1,state=2,
           outcome=1,write_outcome=1,step=0,confirmations=0,save="unresolved",restart="required",status="OK",detail=0,
           deadline_us=20000,before=endpoint(),requested_endpoint=endpoint(baud=9600),observed_active=endpoint(),
           write_evidence=observation(),confirmation_evidence=[])
    c.update(changes)
    return c


def response(tokens=(), **changes):
    expected=bench.communication_arguments(tokens)
    plan=None
    if expected["action"] in (0,1):
        plan={k:v for k,v in expected.items() if k!="action"}
        plan.setdefault("address",1)
        address=plan["field"]=="address"
        plan.update(writes=1,save="required" if address else "unresolved",restart="unresolved" if address else "required",activation="unresolved")
    item=dict(type="reply",profile="ess_rs",id=1,command="communication",ok=True,result="done",action=expected["action"],
              pending=False,owned=False,route_ready=True,save_sent=False,restart_performed=False,write_replayed=False,plan=plan,context=None,
              host=dict(active=dict(baud=115200,format="8N1"),active_known=True,blocked=False,serial_generation=1,actual_baud=115200,failure="none"))
    item.update(changes)
    return item


class Clock:
    def __init__(self):self.now=0
    def __call__(self):return self.now
    def sleep(self,duration):self.now+=duration


class Port:
    def __init__(self, factory):self.factory=factory;self.data=b"";self.sent=[]
    def write(self,data):
        self.sent.append(data)
        tokens=data.decode().strip().split()
        item=self.factory(tuple(tokens[4:]))
        item["id"]=int(tokens[0][1:])
        self.data+=json.dumps(item).encode()+b"\n"
        return len(data)
    def read(self,size):
        out,self.data=self.data[:size],self.data[size:]
        return out


class Campaign:
    def __init__(self, fail=None, route=True):
        self.clock=Clock();self.sleep=self.clock.sleep;self.calls=[];self.events=[];self.fail=fail;self.route=route;self.emit=lambda *a,**k:self.events.append((a,k))
    def command(self,name,*,host_args,timeout_s):
        assert name=="communication"
        self.calls.append(host_args)
        item=response(host_args,route_ready=self.route)
        if host_args[0]=="begin":item.update(owned=True,pending=self.fail=="pending",context=context())
        if host_args[0]=="inspect":item.update(owned=True,pending=self.fail=="pending",context=context())
        if host_args[0]=="confirm":item.update(owned=True,context=context(observed_active_known=True))
        if self.fail==host_args[0]:item.update(ok=False,result="failed")
        if self.fail=="lost_ack" and host_args[0]=="begin":item.update(context=context(state=3,status="TIMEOUT",uncertain=True))
        return item


class CommunicationTests(unittest.TestCase):
    def console(self, factory=response):
        clock=Clock();port=Port(factory);console=bench.Console(port,clock=clock,sleeper=clock.sleep);console.identified=True
        return console,port
    def test_grammar_is_finite(self):
        for tokens in [("begin","address","0"),("begin","baud","57600"),("plan","format","8E2"),("save",),("host","all"),("confirm","random"),("begin","baud","9600","248")]:
            with self.subTest(tokens=tokens),self.assertRaises(ValueError):bench.communication_arguments(tokens)
        for i,name in enumerate(bench.HOST_FORMATS):self.assertEqual(bench.communication_arguments(("plan","format",name))["value"],i)
    def test_real_console_wire_is_correlated_and_not_generic_operation(self):
        c,p=self.console()
        r=c.command("communication",host_args=("plan","baud","9600","1"),timeout_s=.1)
        self.assertTrue(r["ok"]);self.assertEqual(p.sent,[b"@1 profile ess_rs communication plan baud 9600 1\n"])
        self.assertFalse(c.operations)
    def test_rejects_wrong_plan_and_action_and_false_activation(self):
        for mutate in [lambda r:r.update(action=6),lambda r:r["plan"].update(value=0),lambda r:r.update(save_sent=True),
                       lambda r:r.update(context=context(activation_unknown=False))]:
            def factory(tokens):
                r=response(tokens);mutate(r);return r
            c,p=self.console(factory)
            with self.assertRaises(bench.BenchError):c.command("communication",host_args=("plan","baud","9600","1"),timeout_s=.1)
            self.assertEqual(len(p.sent),1)
    def test_refused_begin_preserves_diagnostics(self):
        c,p=self.console(lambda t:response(t,ok=False,result="unavailable",route_ready=False))
        r=c.command("communication",host_args=("begin","baud","9600"),timeout_s=.1)
        self.assertFalse(r["ok"]);self.assertFalse(r["route_ready"]);self.assertEqual(len(p.sent),1)
    def test_retained_candidates_cannot_change_on_inspection(self):
        retained=context()
        c,p=self.console(lambda t:response(t,owned=True,context=copy.deepcopy(retained)))
        c.command("communication",host_args=("inspect",),timeout_s=.1)
        retained["before"]["generation"]+=1
        with self.assertRaises(bench.BenchError):c.command("communication",host_args=("inspect",),timeout_s=.1)
        self.assertEqual(len(p.sent),2)
    def test_preview_never_writes(self):
        c=Campaign();bench.communication_campaign(c,field="baud",value="9600",address=1,timeout_s=1)
        self.assertEqual(c.calls,[("plan","baud","9600","1")])
    def test_lost_write_budget_cannot_change_after_confirmation(self):
        retained=context(state=3,outcome=6,status="TIMEOUT",write_outcome=6,execution=3,uncertain=True,write_evidence=observation(failed=True))
        c,p=self.console(lambda t:response(t,owned=True,context=copy.deepcopy(retained)))
        c.command("communication",host_args=("inspect",),timeout_s=.1)
        retained.update(confirmations=1,step=1,deadline_us=40000,
                        confirmation_evidence=[observation(1,failed=True)])
        c.command("communication",host_args=("inspect",),timeout_s=.1)
        retained["write_evidence"]["deadline_us"]=40000
        with self.assertRaises(bench.BenchError):c.command("communication",host_args=("inspect",),timeout_s=.1)
        self.assertEqual(len(p.sent),3)
    def test_completed_confirmation_budget_is_immutable(self):
        retained=context(confirmations=1,step=1,state=3,outcome=6,status="TIMEOUT",
            confirmation_evidence=[observation(1,failed=True)])
        c,p=self.console(lambda t:response(t,owned=True,context=copy.deepcopy(retained)))
        c.command("communication",host_args=("inspect",),timeout_s=.1)
        retained["confirmation_evidence"][0]["eligible_us"]+=1
        with self.assertRaises(bench.BenchError):c.command("communication",host_args=("inspect",),timeout_s=.1)
        self.assertEqual(len(p.sent),2)
    def test_fabricated_confirmation_or_provenance_is_rejected(self):
        valid=context(step=1,confirmations=1,outcome=3,readback_known=True,readback=3,
            observed_active_known=True,confirmation_evidence=[observation(1)])
        mutations=[lambda r:r.update(write_evidence={}),lambda r:r.update(confirmations=0),
            lambda r:r.update(outcome=1),lambda r:r["observed_active"].update(address=4),
            lambda r:r["confirmation_evidence"][0]["endpoint"].update(generation=10),
            lambda r:r["confirmation_evidence"][0]["wire"].update(raw_hex="01030200030000"),
            lambda r:r["confirmation_evidence"][0].update(readback=2),
            lambda r:r["confirmation_evidence"][0]["wire"].update(response_confirmed=False)]
        for mutate in mutations:
            broken=copy.deepcopy(valid);mutate(broken)
            c,p=self.console(lambda t:response(t,owned=True,context=broken))
            with self.subTest(broken=broken),self.assertRaises(bench.BenchError):c.command("communication",host_args=("inspect",),timeout_s=.1)
            self.assertEqual(len(p.sent),1)
        c,p=self.console(lambda t:response(t,owned=True,context=valid))
        self.assertTrue(c.command("communication",host_args=("inspect",),timeout_s=.1)["ok"])
    def test_finish_requires_proven_matching_host_context(self):
        confirmed=context(step=1,confirmations=1,outcome=3,readback_known=True,readback=3,
            observed_active_known=True,confirmation_evidence=[observation(1)])
        for retained,bad_host in ((context(),False),(confirmed,True)):
            def factory(tokens):
                r=response(tokens,context=retained)
                if bad_host:r["host"]["active"]["baud"]=9600
                return r
            c,p=self.console(factory)
            with self.assertRaises(bench.BenchError):c.command("communication",host_args=("finish",),timeout_s=.1)
            self.assertEqual(len(p.sent),1)
        c,p=self.console(lambda t:response(t,context=confirmed))
        self.assertTrue(c.command("communication",host_args=("finish",),timeout_s=.1)["ok"])
    def test_missing_route_no_begin(self):
        c=Campaign(route=False)
        with self.assertRaises(bench.BenchError):bench.communication_campaign(c,field="baud",value="9600",address=1,timeout_s=1,execute=True)
        self.assertEqual(len(c.calls),1)
    def test_explicit_success_one_write_one_read_finish(self):
        c=Campaign();bench.communication_campaign(c,field="baud",value="9600",address=1,timeout_s=1,execute=True,confirm="before",finish=True)
        self.assertEqual([x[0] for x in c.calls],["plan","begin","host","confirm","finish"])
    def test_failure_has_no_replay_or_automatic_cleanup(self):
        for failure in ("begin","lost_ack","host","confirm","pending"):
            c=Campaign(fail=failure)
            with self.subTest(failure=failure),self.assertRaises(bench.BenchError):
                bench.communication_campaign(c,field="baud",value="9600",address=1,timeout_s=.1,execute=True,confirm="requested",finish=True)
            self.assertEqual(sum(x[0]=="begin" for x in c.calls),1)
            self.assertLessEqual(sum(x[0]=="confirm" for x in c.calls),1)
            self.assertNotIn(("finish",),c.calls)
            if failure in ("begin","lost_ack","pending"):self.assertNotIn(("host","requested"),c.calls)


if __name__=="__main__":unittest.main()
