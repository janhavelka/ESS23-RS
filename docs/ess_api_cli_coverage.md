# API and console coverage handoff

Prompt25's Arduino/native-IDF consumers compile the same complete application
and command inventory. No native family is omitted by framework selection.
[Build and read-only evidence](reports/ess_release_25_2026-10-05.md) cover startup,
console pressure, probe/typed reads and core header isolation; cross-platform
motion/load qualification remains for26 and existing native-family gaps remain
in this inventory.

Prompt23 reconciles the implemented surface. The installed library remains
independent of the console, runner and platform. The command metadata in
`examples/probe_cli/ProbeConsole.cpp` supplies names, help, effects and dispatch;
callback availability also controls help and capability reporting. It is a
bounded application inventory, not a register schema or another command engine.

## Installed API roles

[Prompt26](reports/ess_release_26_2026-10-05.md) qualifies the shared Arduino/native
IDF S3 paths for read/state/control, loaded capture, finite relative motion,
normal/direct stop and reversible lock-delay updates. Other entries retain their
individual unresolved/unsupported/unimplemented and fixture dispositions; S2
compile-only consumption does not create another qualified motor adapter.

The operation inventory classifies every installed free-function declaration separately
from device obligations. Run `python scripts/check_ess_operations.py --details`
to inspect all 221 source records, 135 named choices, producing operation routes,
classified helpers and named gaps. Member APIs are accounted below. Static checking reads production command
metadata and known leaf grammar; it cannot establish callback admission or
physical effects. Native console/application tests verify runtime parity.
Device rows use producing `cli_commands`; helper rows use local `cli_topics`
to identify related command families without claiming additional device access.

| Public role | Existing callable surface | Console relationship |
| --- | --- | --- |
| Typed device operations | ESS read/action/move/velocity/home/settings/communication/persistence preparations; common wrappers reuse ESS preparations | Producing common/profile routes call these functions through one application owner |
| Typed position profile | `buildReadPositionProfile`, `parsePositionProfile`, `buildWritePositionProfile` | `motion-profile read/inspect/restore/forget` and matching `profile ess_rs motion-profile ...` manage an original six-word snapshot; restore writes only `0x0021/5`, excluding observed start speed; arbitrary candidate/archive staging remains a named gap; forget is local release |
| Sequencers | `next*`, `advance*`, `serviceVelocity`, settings write deadline and persistence verification | Cooperatively executed by the owner; inspecting a context is not a second device operation |
| Observations | Checked `getIdentity/getConfig/getStateBlock`, settings observations, `checkProbe`, `getHomeReference` | Terminal/result/status formatting uses retained evidence; reads and cached inspection remain separate |
| Host configuration | `validateAxisConfig/configureAxis/setAxisOrigin/invalidateAxisReference`; explicitly assumed bench units | Axis commands change host interpretation, with idle/reference/generation rules; no implicit device write |
| Conversion | Exact position/velocity preparation, numeric parsing and unit conversions | `prepare`, axis configuration and motion unit arguments; helpers need no separate device command |
| Metadata | Source catalogue, bounded indexed lookups, method descriptors, read/discovery capability queries, tuning parameter metadata, status labels | `caps`, `profile list`, `home methods` and help are local inspection |
| Raw codecs | Checked FC03/06/10 builders/parsers, CRC, access/length checks, paired scalar encode/decode | Used beneath typed paths; raw wire helpers do not count as semantic device coverage |
| Diagnostics | `TrafficCapture`, traffic labels and checked ESS traffic decoder | `debug off/raw/decoded` observes copied owner traffic; display loss does not consume protocol bytes or retained outcomes |

`TrafficCapture` also exposes bounded member operations for caller-owned
diagnostic storage, copied TX/RX/event records, passive lookup and counters.
`Status` exposes pure result inspection. These member APIs have no independent
device-command obligation. Contexts and output buffers remain caller-owned;
the installed core performs no UART, clock, retry, queue, heap or logging work.

## Callable routes

| Surface | Console | Shared implementation |
| --- | --- | --- |
| Presence and inventory | `profile list`, `probe`/`ping`, `discover` | `Discovery.h`, ESS probe codec; application `DiscoveryScan` |
| Identity/configuration/state | `read identity|config|state`, `health check`; `profile ess_rs identity|config|state` | ESS `Reads.h` preparations, decoders and supplied-event sequence |
| Passive observations | `status`, `health`, `config`/`settings`, `drv`, `memory`, `stats` | Cached application snapshots; no motor reads |
| Axis configuration and preview | `axis config`, `axis config set ...`, `axis origin`, `prepare` | `Axis.h`/`Units.h`; exact-number parsing and one conversion path |
| Explicit actions | `enable`, `motor-release`, `alarm-clear`, `position-clear`, `stop normal|direct`; matching `profile ess_rs` actions | Common action preparation and ESS `Actions.h` |
| Finite positioning | `move relative|absolute|angle`; native `move-relative|move-absolute|move-angle` under `profile ess_rs` | Shared target preparation and ESS position sequence |
| Velocity | `velocity`; `profile ess_rs velocity` | Common target preparation and ESS velocity sequence; unresolved acceleration mapping rejected |
| Homing | `home methods`, `home ...`; `profile ess_rs home ...` | ESS method descriptors and homing sequence; only methods33–35 implemented |
| Driver and optional I/O | `driver read|set`, `io read|set`; matching profile routes | ESS whole-candidate driver preparation and checked readback |
| Stored records | `segment position|speed|start INDEX read|set`; matching profile route | One indexed helper per real layout; no serial record trigger |
| Control and tuning | `control read|set`, `tuning GROUP read|set`; matching profile routes | ESS typed parameter validation and common settings sequence |
| Commissioning/persistence | `communication ...`, `persistence ...`; matching profile routes | Explicit public preparations and exclusive application sessions |
| Host session and wiring | `useaddr ADDRESS`, `host ...`, `wiring [x0..x3|y0..y1 unknown|unconnected|connected]` | Idle local target selection, explicit wiring declarations and settled UART tuple callback; no drive setting change |
| Local lifecycle | `result [ID]`, `release ID`, `cancel [ID]`, `monitor ...`, `reset`, `recover` | Non-consuming retained inspection, explicit release/cancel, finite polling, host recovery |
| Diagnostics/profile snapshot | `debug off|raw|decoded`, `motion-profile read|inspect|restore|forget`, `profile ess_rs motion-profile ...`, `load ...`, `capture-read` | Installed traffic capture/decoder, typed position-profile codecs, bounded application adapters |

`help COMMAND` describes exact syntax. Parameter group aliases call the same
parser and callback as their profile routes. Relative previews require an
explicit basis. Integers/rational values are checked before admission; a
generic bare-address setter is not provided. Only ESS is implemented, so there
is no second profile to select. `flush` remains unavailable; recovery is the
explicit settled host discard path.

## Context and retained outcomes

All callbacks run in the cooperative owner task. Inputs are copied by admission;
borrowed result views last through synchronous formatting. Commands have host
correlation IDs, separate from operation IDs. Nine ordinary correlations and
one reserved stop are bounded independently of retained operation storage.
Results/status are non-consuming; `release` explicitly frees a terminal slot.
Counter reset and recovery preserve unread results, raw traffic, original tuple
and generations, and uncertain execution. They never replay a motor write.

`monitor` inspection and `monitor off` remain local during host faults or
configuration ownership. Off cancels the continuation and settles any active
physical TX. `recover` can interrupt work, but it settles TX/DE before discarding
RX and never resumes old queued work. Cancellation is not a physical stop.

`useaddr` requires idle work, disabled monitoring and no exclusive session. A
changed address advances checked binding/configuration generations, clears
dependent scales/origins/observations and marks the new endpoint's wiring
unknown. Unit preferences and historical results survive. Same-address
selection is a no-op. Delayed old-generation delivery cannot restore confidence.
The bounded identity cache holds the last successful typed identity, with its
explicit target. Reading another address may replace it; a selected settings
update then needs fresh matching identity evidence. Public preparations reject
the other target, and retained historical results remain unchanged.
An old saved motion profile cannot restore into a new binding. Explicit idle
`motion-profile forget` releases that snapshot without clearing uncertain
operations, transport faults or physical reservations; archive/restore originals
before discarding a snapshot that is still needed. Both bare and explicit ESS
routes use the same checked snapshot/restoration callback. The public builder
can encode valid caller-supplied profiles, but the console currently supplies
only its retained original snapshot; it has no arbitrary staging/archive import.

An accepted restore reserves the axis until matching readback settles it.
`restore_unsettled` remains visible after failed/uncertain transport and blocks
forget, rebinding and replay. After explicit transport repair, fresh configuration
and stationary reads, `motion-profile read` performs read-only reconciliation.
An uncertain write needs stationary evidence later than its delivery. Its raw
frame, UNKNOWN outcome, original deadline and configuration/tuple/binding
generations remain immutable while the new read has its own deadline/context.
Successful stored reconciliation does not manufacture an acknowledgement.

The snapshot is volatile application storage. On this bench, opening another
USB session reset that storage while the motor retained its staged parameters.
The prompt23 report preserves the original target5000 and final stopped
target100 separately. Session restoration cannot recover an archive lost on
host restart; arbitrary archived parameter preparation belongs to09/11.

The local `wiring` command queries declarations or explicitly changes one
terminal's declaration while idle. It supplies operation prerequisites without
assigning drive functions or observing a voltage. Its changes have no device
read/write credit in the operation inventory. Input reports distinguish declared wiring (`unknown`, `unconnected`,
`connected`), assignment (`unresolved`, `disabled`, `assigned`) and observed
levels. Typed `io set x0 none`/`y0 none` use the same checked function setter
with documented value0. Neither polarity nor unconnected wiring disables an
assignment, and disabled outputs do not promise an electrical level.

## Coverage and explicit gaps

Prompt24 [finite scenarios](bench_scenarios.md) exercise these same typed routes
and retain strict result/parser evidence. Same-session backup restoration closes
a harness lifetime defect; it does not implement the arbitrary archived/native
parameter gaps below or grant missing physical qualification.

[The complete inventory](reference/ess_rs_operations.json) retains every ledger
record and named choice. It separately classifies installed public helpers,
operation sequences, metadata, diagnostics and CLI routes. Helpers/builders are
not independent device actions. The checker rejects missing installed function
classifications and invented routes; native parity tests check actual requests.

Substantial prerequisite gaps remain assigned to their original prompts:

| Owning prompt | Open obligation | Actual boundary |
| --- | --- | --- |
| 09/11 | Native positioning start-speed setter | Profile snapshot reads start speed; restoration never writes it |
| 09/11 | Arbitrary native five-word profile candidates and archived restoration | Typed builder exists, but CLI restoration accepts only the volatile original snapshot; finite-move staging is not standalone parameter access |
| 11 | Standalone typed velocity parameter reads | Finite velocity stages qualified supplied words; that is write sequencing, not a parameter-read API |
| 14 | Standalone typed homing parameter reads and auxiliary setter | Existing homing requires independently qualified active auxiliary 7 |
| 14 | Remaining homing trajectories | Three methods implemented, 27 unimplemented, five unresolved; I/O configuration does not implement a switch trajectory |
| 12/14 | Nonzero homing offset and additional paired-write forms | Zero-only reviewed staging does not resolve offset order, signedness or units |
| 12/13/16 | Limit and stored-position paired setters | Reviewed write restrictions stay in the source denominator; no split FC06 workaround |
| 18 | Earlier collision entries `0x003B/0x003C` | Unspecified access stays unresolved; no alias to the later collision pair |

Unsupported torque/current motion and serial segment start remain distinct from
those gaps. Settings access does not establish those motion capabilities.
Physical qualification, missing external fixtures, persistence/restart proof,
engineering ramp conversion and endurance remain separate from implementation.
Prompt23 delivers integration and this handoff to24–26; it does not declare the
full native family denominator or the release complete.

FieldCore RS485 CLI/owner/backend remain read-only integration references.
Passive snapshots, explicit request correlation and bounded owner progression
are useful conventions. This console intentionally retains non-consuming
result inspection, explicit release and motor-write uncertainty independently
of FieldCore's sensor result/trace consumption and recovery policies. No
FieldCore types or sensor replay policy enter the reusable core.
