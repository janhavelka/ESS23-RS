# Fresh independent audit of prompts 17 and 18 — 2026-10-04

Baseline: `a077b97` on `main`, synchronized with `origin/main`. Final revision
is the commit containing this report. Only prompts 17/18 and bounded defects in
their shared settings path were changed; prompt 19 remains unexecuted.

Both full prompts, execution contract, prerequisite production paths, original
ledger and manual pages were reread. Three independent reviewers inspected
source/model/ledger coverage, settings effects and lifecycle, and CLI/Python
parity. Findings were reproduced against code and actual formatter output.
The source reviewer audited the final integrated fixes independently of their
authors. No remaining production defect was found within the dispatched scope.

## Confirmed findings and corrections

1. Writes checked previous-settings freshness only at preparation. A delayed
   update could outlive that snapshot while stationary evidence remained fresh.
   Every copied settings window now caps the immutable write budget. The public
   `driverWriteDeadline(const DriverContext&)` supplies the identical budget to
   sequencing and console diagnostics, removing a duplicated calculation that
   final review found had drifted. READ keeps its original operation deadline.
2. Copied baselines checked bytes and some flags but omitted parts of their
   transport envelope. Publication/admission now also requires FRAME, matching
   step, complete eight-byte request TX, no execution uncertainty, and complete
   retained RX length. Rejected preparation leaves the caller's output unchanged.
3. A failed multiwindow refresh could retain readiness even when its completed
   checked prefix proved a changed encoder scale or arrival threshold. The
   application now invalidates the baseline and dependent settings, reference,
   readiness and prepared-operation assumptions. Previous raw values and
   historical results retain their original context. An unchanged checked
   prefix keeps previous validity; repeated settlement does not increment the
   generation again.
4. Python accepted some forged parser-failure labels and contradictory timing
   outcomes. One bounded reply helper now matches the actual codec's validation
   order for FC03/FC06, including exceptions and echoes. Terminal failure priority
   follows qualified closure/deadline evidence before parser errors. Successful
   READ evidence cannot retain execution uncertainty. Stored-write settlement
   deliberately retains source-unconfirmed write execution as UNKNOWN.
5. The sequence overview still described prompt 18 as unexecuted. Its disposition
   and current evidence links were corrected.

## Requirements and evidence

| Requirement / failure scenario | Production API/path | Native/build evidence | Hardware result | Remaining proof |
| --- | --- | --- | --- | --- |
| Algorithm, configured encoder, effective/closed/base/open/lock currents, lock delay | `ControlSettings.h`, checked `prepareControlSettings`, `getControl`, shared `DriverContext` | All eight descriptors, enum/range/model/current-tuple dependencies, stale context, partial/cancel/unknown results; actual app and 23 formatter/parity cases PASS | All eight reads and bounded lock-delay stored restoration PASS | Mode, feedback, current and torque effects NOT RUN; exact current formula/model ceiling unresolved |
| Input/pulse filters, deviation, arrival window/time | `Tuning.h`, `prepareTuningValue`, `prepareTuningSettings`, `getTuning` | Resolved masks/ranges, grouped candidates and changed completion assumptions PASS | All six reads and input-filter stored restoration PASS | Electrical filter delay and arrival/deviation behavior NOT RUN |
| Current-loop Kp multiplier/Kp/Ki/Kc; two LA Kp/Kv/node stages, Kvf and position Ki | Same bounded tuning API and checked native CLI | All twelve descriptors, stage boundaries, invalid index/range/no-TX and partial failures PASS | All twelve raw reads PASS | Gain perturbations and physical scaling NOT RUN; no engineering-unit formula claimed |
| Later collision threshold/current; earlier conflicting locations | Reviewed later descriptors; existing access policy guards earlier words | Legal later ranges, forbidden gaps/access, no aliases and no-TX guards PASS | Later raw values remain `0,0`, known mask zero | Firmware applicability remains unresolved; earlier `0x003B/0x003C` unspecified access remains unavailable |
| Snapshot expiry and forged baseline envelope | Shared core deadline/decode | Earliest first/second settings window, expiry before later write, late echo UNKNOWN, immutable rejected output PASS | Final-image settings regression PASS | Deadline fault injection is native evidence |
| Failed partial refresh and cache/reference effects | Actual `main.cpp` owner/settlement path | Changed/unchanged prefixes, CRC-failed later window, repeat service and unchanged historical contexts PASS | No deliberate wire corruption injected | Native fault evidence remains separate from physical behavior |
| Console/API/Python correlation and parser settlement | Existing console and `bench_probe.py` | 23 control + 66 tuning real formatter cases and mutation checks PASS | Exact command/response lines retained | Electrical response-source qualification remains open |

All original control/tuning source ranges were checked against physical function
manual pages 73, 77–79. Encoder zero remains a raw documented value, unsuitable
as a mathematical scale. Observed algorithm 3 and firmware `0x0029` remain
unknown. The ESS23-RS20 peak-current specification does not establish the
effective-current register formula. Arrival time, LA nodes and gain values keep
native units when scaling is unresolved. No generated type, ledger, vendor PDF,
forbidden register access, universal default or commanded torque mode was added.

## Final verification

- C++11 Release native build with existing warning/error policy: **45/45 suites PASS**.
- Installed core consumer without an example include path: **1/1 PASS**, including
  the new deadline accessor.
- Python transport: **167 PASS**; generator: **18 PASS**; inventory mutations:
  **21 PASS**. Version, 221-descriptor/242-word generation, five preserved
  contrast references and `git diff --check` PASS.
- PlatformIO `bench_s3_units`, `bench_s3_probe`, `bench_s3_load_poll` and
  `bench_s3_load_timer`: **4/4 PASS**.

The intermediate test run while newly coordinated parity counts preceded their
rebuilt fixture executable failed only the two fixture-count checks. After the
fixture build, the complete 45-suite run passed; no test assertion was relaxed.

## Final COM13 image and finite campaign

[Final raw evidence](ess_release_17_18_audit_2026-10-04.json) retains all 200 exact
sent command lines, 252 received lines, 1,115 events and 59 snapshots. USB
`303A:1001`, serial `3C:0F:02:CD:6B:98`; ESP32-S3 UART2 TX47/RX48/DE21 active-high,
115200 8N1, address 1, raw model `0x4EEA`, firmware `0x0029`. Motor bolted to the
table, shaft free, only power/RS485 connected. Load and monitoring off; no fault
recovery, statistics reset, automatic retry or motion was performed.

Final timer image: **502,912 bytes**, SHA-256
`2f4237cd50c3d46a4214b5fe8907aab8aaa2606a87c15bf62f03b2467d49b856`.
Archived locally as `build/bench/prompt17_18_audit_final_timer_firmware.bin`;
build/upload/native logs use the same `prompt17_18_audit_final_` prefix.
Static RAM 29,368 bytes; flash 497,024 bytes. The
[preliminary campaign](ess_release_17_18_audit_preliminary_2026-10-04.json)
records the preceding audit image before the final shared formatter correction,
not current-image qualification; both named images are preserved.

**104 frames / 1,092 RX bytes**, zero failed transactions, timeout, cancellation,
discard, RX/capture fault or sample-gap violation. Seventeen rejected gates
produced no TX (sixteen device admission checks and one strict host range check).
All eight control and twenty tuning words were read before/after. Stored input
filter `2→3→2` and lock delay `200→201→200` were explicitly restored. Each FC06
keeps acknowledgement false and source-unconfirmed/UNKNOWN execution; a separate
confirmed FC03 establishes matching stored readback, not active behavior.
Update/restore delivery intervals: filter 15,175/15,181 us; lock 15,238/15,266 us.

Control raw `[3,4000,5600,100,40,100,40,200]`; filters
`[2,5,4000,5,10,512]`; current loop `[6553,1024,102,409]`; LA
`[2560,256,20,2560,128,100,10,2]`; later collision `[0,0]`. All originals match
after restoration. No conflicting address or unknown value was normalized.
State remains alarm 0, stationary/enabled/arrived, speed/position 0 and logical
input/output levels 0. This is register evidence, not an independent shaft or
torque measurement.

Ten one-attempt unloaded probes PASS: 5,443–5,710 us, mean 5,523.7 us. Maximum
owner/capture gaps 275/55 us; capture high-water 2. Internal free/min/largest
336,656/331,496/278,516 bytes; PSRAM free/min/largest
8,201,772/8,201,772/8,126,452 bytes. Owner/work stack free 3,396/3,268 bytes.
No storage layout changed: App 180,792, Record 7,760, Console 18,432,
DriverContext 3,744, DriverRequest 136, DriverPrerequisites 1,448 and
TuningObservation 240 bytes. Larger contexts/cache stay in PSRAM; required
capture/driver state and stacks remain internal.

Ending pending/retained/reserved counts zero, DE released, output empty, no
blocked/short writes or dropped inputs, no recovery requirement. Physical gain,
arrival/deviation, collision, filter-delay, feedback/current/torque and lock
transition tests remain **NOT RUN**: no suitable qualified finite effects
experiment or external fixture was supplied. No feedback disabling or current
increase was used to close coverage.

## Integration and handoff

Current FieldCore-node `6debce7c44af9bc36f00fbcaccdfa47631846cce` RS485 ingress,
task, backend and CLI were inspected read-only. Its bounded request/work/result
and serialized owner vocabulary is compatible; sensor payloads, borrowed
observations, eight-byte transaction limits and echo handling deliberately do
not replace copied ESS expectations, retained frames, settings invalidation or
physical TX settlement. No FieldCore files were edited.

Public settings operations remain caller-owned and allocation/I/O-free. The
application owns transport, fixed results, generations and cache invalidation;
results keep their original target, deadline, raw bytes and partial progress
until explicit release. Exact model/effects qualification is an admission input,
not a framework default. Later prompts may reuse this verified path while the
explicit firmware/access/physical-effect gaps remain in the coverage inventory.
