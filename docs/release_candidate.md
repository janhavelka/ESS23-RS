# MotorControl-RS partial release candidate

This is an **unpublished 0.6.0 development candidate**, with incomplete ESS
native coverage and bounded functional qualification. Prompt30 prepares its
package and integration handoff; it does not publish a tag/release or implement
FieldCore. The [candidate record](reports/ess_release_30_2026-10-05.md) pins the
exact source commit, artifact hashes, commands, CI and final bench disposition.
Version alone cannot distinguish earlier0.6.0 development images.

## What is available

The installed core provides framework-independent C++11 units/target preparation,
checked codecs, typed identity/configuration/state observations, bounded actions,
finite position/velocity/homing contexts and reviewed settings operations.
Public headers document prerequisites, native units, outputs and lifetimes.
The [API/CLI inventory](ess_api_cli_coverage.md) maps actual callable routes;
the [operation ledger](reference/ess_rs_operations.json) retains every obligation.
One application-owned bus executes supplied work, preserves request context,
reserves stop/results and never automatically replays an uncertain write.

Start with [core/console getting started](getting_started.md),
[Arduino](esp32_probe.md) or [native ESP-IDF](esp_idf_probe.md). Both firmware
consumers share the same application and typed operations. The core ZIP is
independent of examples, Python, SDKs, vendor downloads and FieldCore; its README
contains a self-contained CMake/install/consumer example. Full checkout tooling
and documentation are separate resources, not core build dependencies.

## Supported scope and evidence

| Item | Disposition |
| --- | --- |
| ESS23-RS20 | User-identified physical bench; raw model4EEA/version0029 retained, universal model/firmware mapping unresolved |
| ESS23-RS10 | Shared documented target; physical qualification NOT RUN |
| Other manufacturers, CANopen | Unimplemented; contrasts do not imply support |
| Desktop core | Strict C++11 and C++17/noRTTI source/install consumers; no platform or firmware include dependency |
| ESP32-S3 Arduino/native IDF | Shared firmware, pinned IDF5.5.5; Arduino3.3.11/pioarduino55.03.311; bounded read/load/positive-native-move/stop/settings subset recorded in29 |
| ESP32-S2 | Core/portable compile-only checks; no S2 motor UART adapter or hardware qualification |
| FieldCore | Current-source handoff and isolated consumer compatibility; no motor module/integration/runtime qualification |
| Serial tuple | Motor bench node1/1152008N1;16 adapter tuples supported, alternate motor tuples unqualified |

The [29 measured matrix](reports/ess_release_29_2026-10-05.md) preserves all1,200
planned loaded reads and1,760 checked frames. The first Arduino aggregate remains
**FAIL**: stale/wrong-generation identity correctly rejected its settings write
before TX. Its reproduced prerequisite correction is a separate PASS; matched
IDF and restoration checks have their own records. Passing frames or software
tests do not establish the untested rows below.

## Release gates

| Gate | Current result / remaining work |
| --- | --- |
| Core packaging, generation, public headers, CI | Repeatable verifier and clean consumers; exact candidate result is pinned in30's record |
| Source/native coverage | Partial:221 records/242 words and135 choices;81 implemented,52 missing/unresolved,1 unsupported,1 no-op;49 producing operation rows |
| Nine owning-prompt gaps | See explicit list below; no complete ESS coverage claim |
| Paired access |18 guarded setters: two soft-limit pairs and16 stored pulse targets; no split-FC06 workaround |
| Functional motion/stop | Small positive native relative moves and normal/direct stops on both named S3 images; echo/activity/standstill evidence is separate from independent physical measurement |
| Frame qualification | FC03 one/16-word7/37-byte replies and FC10 position0x0021/5 physically exercised; other three FC10 shapes and physical exception injection NOT RUN |
| Units/model/firmware | Negative encoding, full feedback/source calibration, wider ramp/sign/model interpretations and engineering acceleration mapping unresolved/unqualified |
| Endurance/faults | Multi-hour run deferred; physical disconnect/partial-frame/ack-loss/power-cycle fixtures NOT RUN; native injections remain simulated |
| Electrical/shaft | Independent TX/RX/DE timing, shaft motion/accuracy and stop latency unmeasured; not required for the authorized short functional subset |
| I/O/homing/mechanics | External switches/loads absent; physical homing, external segments and linear travel unqualified |
| Persistence/commissioning | Restart/backup/route-back fixture unavailable; save/factory durability and changed communication activation NOT RUN |
| Historical discrepancies | Malformed wrong-host-tuple traffic and shared PT/PV starting-speed readback remain unresolved; later passing reads do not fix them |
| Runtime limits |20us capture,85us starvation guard, about20–21% one-core capture cost; flash/cache-off capture unsupported; saturated USB RX loss unmeasured |

The nine named gaps remain assigned to their owning prompts:

1. Arbitrary five-word position-profile candidates and archived restoration (09/11).
2. Positioning start-speed setter (09/11).
3. Three standalone velocity-parameter reads (11).
4. Six standalone homing-parameter reads (14).
5. Homing auxiliary options (14).
6. Remaining homing methods:27 unimplemented and five unresolved (14).
7. Nonzero homing offset and its paired semantics (12/14).
8. Early collision access at0x003B/0x003C (18).
9. Native position ignore-versus-interrupt bit (09).

Stored settings are not torque/current motion support. External-input records
do not acquire a fictional serial start. Unknown source semantics stay in the
denominator. No industrial, emergency-stop or communication-loss safety claim
follows from this candidate.

## Separately dispatched work

Close named family gaps in their owning blocks and qualify affected physical
cases. Add missing fixtures or retain their explicit NOT RUN status. Schedule
endurance separately. Integrate a future motor module through
[FieldCore's existing RS485 owner](fieldcore_handoff.md), preserving its other
framing modes; this repository does not edit FieldCore. Human-console automatic
prerequisite refresh/result management and detailed rejection reporting remain
usability work, not a request to weaken readiness validation.

Publication of a tag/GitHub release requires a separate explicit dispatch after
review. Canonical metadata remains prepared for `janhavelka/MotorControl-RS`;
the working remote stays at the available endpoint under the
[rename guide](repository_rename.md). Vendor originals/hashes/licensing remain
separate; no vendor material enters the MIT core ZIP.
