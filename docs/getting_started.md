# Getting started with the partial candidate

Use the [candidate scope and limits](release_candidate.md) before selecting a
physical operation. The core builds with C++11 and CMake3.16+; its exported README
contains an offline `find_package` consumer. Include
`MotorControlRS/MotorControlRS.h` for common intent and the concrete ESS header
for an operation. No firmware type alias or example include path is required.

## A direct operation

This pure preparation uses the same public API as the console:

```cpp
#include <MotorControlRS/profiles/ess_rs/Discovery.h>
int main() {
    MotorControlRS::ReadTarget target;
    target.id = 1;
    target.address = 1;
    target.generation = 1;
    MotorControlRS::ESS_RS::PreparedProbe probe;
    MotorControlRS::Status status = MotorControlRS::prepareProbe(
        probe, MotorControlRS::DriveProfile::ESS_RS, target, 1, 100, 100000);
    // On success, probe.bytes[0..probe.length) is one FC03 request. No I/O occurred.
    return status && probe.length == 8 ? 0 : 1;
}
```

The application admits/copies those bytes, executes transport and retains its
original target, tuple, ID, generation, expectations and absolute deadline.
Map qualified closure bounds and accepted TX count into `ReadEvent`, then call
`checkProbe`. A valid frame means a responder; unknown model stays unresolved.
For multiple reads, `prepareIdentity/prepareConfig/prepareState`, `nextRead`
and `advanceRead` yield/consume bounded steps. For motion, supply actual qualified
configuration/reference/readiness to `prepareMoveRelative/Absolute/Angle`,
advance the profile context with real events and preserve uncertainty. Never
invent prerequisite booleans to force admission. See [API inventory](ess_api_cli_coverage.md)
and [FieldCore mapping](fieldcore_handoff.md) for the event/result boundary.

## Standalone console

Build/flash commands and backup/recovery are in the
[Arduino guide](esp32_probe.md) and [native-IDF guide](esp_idf_probe.md).
Builds alone do not flash. The bench pins are example configuration, not public
library defaults. Only one program can own COM13; close the terminal before
starting Python. Opening a new session may restart the board and lose volatile
snapshots while leaving motor settings intact.

Start with `help`, `version`, `host`, `drv` and `result`. These inspect the host.
`useaddr 1` selects locally while idle; it changes no drive address and a changed
selection invalidates dependent evidence. On this known bench, declare X0–X3
unconnected explicitly with `wiring x0 unconnected` etc. That writes no motor
assignment. Do not declare wiring for an unknown fixture by copying the bench.

`probe` tests presence. `read identity`, `read config`, `read state` refresh their
own observations. `status`/`health` are cached; `health check` performs reads.
Each admitted read/action has an operation ID and retained terminal record.
Inspect with `result ID` and explicitly free a settled record with `release ID`.
**`release ID` frees host storage; `motor-release` releases the drive.** Eight
ordinary terminal slots are finite; stop has reserved capacity. Command IDs are
separate correlation identifiers. Neither inspection nor statistics reset
consumes outcomes. Monitor is opt-in; `monitor off` is not a motor stop.

For a short read-only regression on the known unloaded bench:

```powershell
python scripts/bench_scenarios.py --port COM13 --scenario quick --count 10 --out build/bench/quick-new
```

Use a new prefix for every attempt. For one already qualified positive finite
move, first review the no-I/O plan:

```powershell
python scripts/bench_motion.py --port COM13 --phase forward --out build/bench/move-new --plan-only
```

Remove `--plan-only` only to run that explicit experiment. The existing harness
keeps one session, refreshes prerequisites immediately, performs one100-native-
increment move at60rpm/configured ramps, prepares stop/standstill and restores
the exact original profile only after a successful stop and fresh non-running/
zero-speed evidence. Failed/unknown cleanup retains the backup and failure;
closing the connection is not restoration. It does not replay uncertain movement.
These are drive-reported functional results, not calibrated shaft degrees/travel. Broader
units/methods require their own evidence.

## Common refusals

| Result | Meaning and next step |
| --- | --- |
| `results_full` | Inspect/archive unread terminal results; `release ID` frees each settled host record |
| `unavailable` / `invalid`, operation0 | No operation admitted; readiness/configuration/reference or a parameter failed validation. Inspect caps/config/state/profile and syntax before trying a new command |
| Busy/fault/recovery required | Settle existing work/DE first; preserve outcomes. `recover` repairs host transport and cancels old queued work, never motor execution |
| Unknown execution | Some bytes may have reached the drive; inspect evidence and explicitly stop/reconcile as appropriate. Do not repeat a relative move or save |
| COM13 access denied | Another terminal/program owns the port; close that connection before a new session |

Manual `move relative 100 steps native 60 configured 1` currently needs a
matching `motion-profile read` within30s and fresh stationary state/I/O/config
context. The mover's consumed readiness has a3s budget; waiting3–5s after a
state read can produce `invalid`, and older input/motion evidence can produce
`unavailable`. Settings writes also need matching identity/configuration within
their own freshness bounds (typically5s). A successful probe alone establishes
none of these. Result release preserves cache values but does not refresh age.

The human console currently lacks automatic prerequisite refresh and detailed
motion rejection reasons; operation0 and the coarse error do not uniquely
identify the failed check. The finite harness avoids manual typing races without
weakening validation. Future guided commands should perform the same checked
preparation, manage their own retained results and expose the actual rejection.
