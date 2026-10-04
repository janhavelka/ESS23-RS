"""Actual C++ segment core/formatter records checked by strict Python transport."""
import copy
import json
from pathlib import Path
import subprocess
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"scripts"))
from bench_probe import BenchError, Console, MAX_LINE, segment_arguments
rows=subprocess.check_output([sys.argv[1],"--segment-fixtures"],text=True,timeout=10).splitlines()
assert len(rows)==18
for row in rows:
    fixture=json.loads(row); r=fixture["record"]
    Console._check_driver(r,1,None,"segment")
    assert len(row)<MAX_LINE
    kind={"position_segment":"position","speed_segment":"speed","segment_start_speed":"start"}[r["driver_group"]]
    args=(kind,str(r["segment_index"]),"read") if r["driver_kind"]=="read" else (kind,str(r["segment_index"]),"set","value","60") if kind=="start" else (kind,str(r["segment_index"]),"set","speed","60","acceleration","90","deceleration","80")
    Console._check_driver(r,1,args,"segment")
    for mutate in (lambda b:b.update(segment_index=0),lambda b:b.update(segment_index=17),lambda b:b.update(active_settings_known=True),lambda b:b.update(driver_group="io"),lambda b:b.update(execution_path="serial"),lambda b:b.update(echo_readback_policy=False) if b["driver_kind"]=="update" else b["observation"].update(raw=[0]*5)):
        broken=copy.deepcopy(r); mutate(broken)
        try: Console._check_driver(broken,1,args,"segment")
        except BenchError: pass
        else: raise AssertionError((fixture["case"],"invalid evidence accepted"))
    wrong=(kind,str(17-r["segment_index"]),*args[2:])
    try: Console._check_driver(r,1,wrong,"segment")
    except BenchError: pass
    else: raise AssertionError("record correlation bypass")
for args in (("position","0","read"),("speed","17","read"),("start","1","set","value","1/1"),("speed","1","set","speed","2147483648")):
    try: segment_arguments(args)
    except ValueError: pass
    else: raise AssertionError("invalid grammar")
print("PASS: 18 actual segment core/console terminals and mutated correlation/evidence")
