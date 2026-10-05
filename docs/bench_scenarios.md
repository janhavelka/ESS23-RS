# Finite Python bench scenarios

`scripts/bench_scenarios.py` selects one finite experiment on the ordinary
firmware. Its default `quick` performs ten one-attempt probes, typed
identity/configuration/state reads and passive diagnostics. It sends no motor
write. Existing `bench_probe.py` feature checks remain usable; `bench_motion.py`
uses the same session/evidence owner. All use the existing strict `Console`
parser and public firmware commands. No second serial protocol engine exists.

```powershell
python scripts/bench_scenarios.py --list
python scripts/bench_scenarios.py --out build/bench/quick-new
python scripts/bench_scenarios.py --scenario position --phase forward --debug decoded --out build/bench/move-new --plan-only
```

`--list` and `--plan-only` open no port. Remove `--plan-only` to execute the
selected scenario. Every run reserves new `.json` and `.jsonl` files before
opening COM13; existing files are never overwritten. Use a different prefix for
a deliberate rerun and retain failures. Optional `--firmware FILE` records the
supplied image's hash; console version does not attest the flashed image.

| Scenario | Exact selection / limits | Cleanup and qualification |
| --- | --- | --- |
| `quick` | Default; `--count 1..100`, `--interval 0..1` seconds | Motor reads only; requires unloaded host |
| `observations` | Same bounded count/interval | Explicit state/health refresh and passive age checks |
| `load` | Explicit `--load WORK_US OWNER_DELAY_US CONSOLE_BYTES`; existing fixture bounds, count/interval | Require measured work/lines/service delay; retain measured window before restoring original host load |
| `configuration` | One typed configuration and I/O read | No settings write |
| `native-read` | `--native driver\|io\|control\|segment\|tuning`; `--native-args` use existing typed grammar | Reject setter arguments; inspect and release correlated result |
| `settings` | `--setting input-filter\|lock-delay --value N`; exactly one native-unit difference | Save raw group, refresh existing prerequisites, change once, read back, rebuild invalidated evidence, restore once and compare the complete raw group |
| `position` | `--phase forward\|absolute\|return`; node1, 100 native increments at 60rpm, configured ramps | Same-session stop, fresh standstill and original profile restoration; absolute/return require the existing raw fixture window |
| `actions` | One explicit initially-enabled stationary release/enable cycle | Ends enabled; initial released state refuses before an action write |
| `stop` | `--stop-policy normal\|direct`; one finite250-increment move | Require new RUNNING report, interrupted move result, stop and standstill, then restore original profile |
| `angle` | Explicit `--arguments` matching public wrapped-angle grammar; optional stop policy | Existing coordinate/firmware gates; one stop and profile restoration only after qualified standstill |
| `velocity` | Explicit public `--arguments`, finite duration and stop policy; timeout must include duration | Existing sign/ramp/stop gates; no autonomous hardware test in this qualification. Standalone native-parameter backup remains owning gap11 |
| `homing-prerequisites` | One caps/wiring/I/O check | No homing execution; method-specific admission still required. Existing explicit finite `bench_probe.py home` campaign remains available behind its gates |
| `discovery` | `--discovery` with existing bounded candidate/budget grammar; default selected ESS/current tuple | Require COMPLETE, retained findings and actual original host tuple/selection after restoration; incomplete scan fails without implicit recovery |
| `persistence-plan` | Explicit `--persistence-kind save\|factory-restore` | Local plan; no nonvolatile write or durability claim |
| `persistence` | Same explicit kind; existing backup/recommissioning gates | One deliberate action and verification; no replay, automatic restart or inferred durability |

Single-attempt scenarios report count1 and reject `--count`/`--interval`.
Scenario-specific arguments cannot silently enter another scenario. No stress
mode sweeps setters, persistence or motion. `--address` is 1..247 for supported
read operations; fixed qualified motion phases select node1. `--timeout` is a
finite command bound up to60seconds; existing operation/request deadlines are
unchanged. The reusable motion phases use their fixed five-second command bounds.

Examples of explicit qualified inputs used in [prompt24 evidence](reports/ess_release_24_2026-10-05.md):

```powershell
python scripts/bench_scenarios.py --scenario observations --count 2 --out build/bench/state-new
python scripts/bench_scenarios.py --scenario native-read --native segment --native-args position 15 read --out build/bench/record-new
python scripts/bench_scenarios.py --scenario load --load 1000 0 64 --count 10 --out build/bench/load-new
python scripts/bench_scenarios.py --scenario settings --setting input-filter --value 3 --out build/bench/filter-new
python scripts/bench_scenarios.py --scenario position --phase forward --debug decoded --out build/bench/position-new
python scripts/bench_scenarios.py --scenario stop --stop-policy normal --debug decoded --out build/bench/stop-new
python scripts/bench_scenarios.py --scenario discovery --discovery addresses 1 1 tuple 115200 8N1 requests 2 results 2 --out build/bench/scan-new
```

These are separate experiments, not a command chain to execute blindly.
The filter example applies only when the checked original value is2 and the
existing passive/unconnected-terminal policy permits2/3. Unknown wiring is not
overridden. Settings refreshes must observe the changed native group before
rebuilding dependent I/O/configuration evidence; otherwise cache invalidation
correctly refuses restoration. Lock-delay experiments obey the existing exact
model and stopped-state policy. A failed or uncertain update keeps its backup
and exact progress; the tool does not automatically restore an unknown update.

One session owns connection, debug selection and artifacts until cleanup ends.
Before/after snapshots contain fixed local diagnostics plus three explicit typed
read kinds (discovery uses passive snapshots). Local console configuration and
drive configuration occupy separate keys. CPU availability/percentages, resource
watermarks, capture/owner gaps, protocol/parser evidence, operation IDs and
latencies retain their original meanings. Missing workload evidence fails.
Successful probes also require the recorded request/reply bytes to agree with
the CRC, address, model window, decoded model and reported parser disposition.
Ordinary read/configuration/motion scenarios compare before/after configuration;
a deliberate factory restore instead preserves its changed readback.

The JSONL stream flushes each record and defaults to20,000 records/16MiB, with
a 1MiB per-record bound. CLI maxima are200,000 records/64MiB. Capacity exhaustion,
short file writes and disk/flush errors interlock subsequent evidence/traffic.
The independent summary retains fixed snapshots, cumulative failure counts,
the last16 campaign records and last32 motion-command records; no unbounded raw
history is held in memory. Raw lines and traffic events stream incrementally.
Asynchronous debug display drops remain separate from protocol failures.

On failure, no new experiment work or success-only motor reads are issued.
Only passive diagnostics are collected while framing remains intact. An accepted
finite move may receive one preplanned stop; a stop already attempted is never
replayed. Standstill requires fresh non-running, alarm-clear and zero raw-speed
reports, using at most ten reads under one observation deadline. Stop/restoration
failure remains failed even if another cleanup step succeeds. Broken framing,
disconnect/restart, stale IDs or evidence failure leave cleanup unknown and
prevent reuse of that connection. Host recovery is a separate explicit operation.
An interrupted startup read also blocks further traffic. Failure while closing
the owned port makes the session fail; simultaneous work/startup and closure
errors retain the primary `error` and separate `owner_error`.

The [fresh prompt24 audit](reports/ess_release_24_audit_2026-10-05.md) records
these failure regressions and the repeated COM13 finite functional checks.

Uncertain movement is never retried. A volatile profile backup is preserved by
same-session restoration, but cannot survive a board restart. Historical archive
imports, missing native parameter APIs, switch-dependent methods, unsupported
pair writes and restart/persistence fixtures remain the named owning gaps in
[the coverage matrix](ess_api_cli_coverage.md). Functional reports do not become
independent physical shaft or electrical measurements.
