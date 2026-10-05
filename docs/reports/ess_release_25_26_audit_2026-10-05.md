# Fresh independent audit of prompts 25 and 26

Audited baseline `b4f91b98d7147562a46306454ae158bb86e4a9e0`, rereading both
original prompts and the execution contract. This audit fixes one native SDK
configuration defect, strengthens shared failure tests and corrects qualification
wording. It executes neither prompt 27 nor another motor family.

## Findings and corrections

1. Native `IdfPlatform.cpp` rejected ordinary SDK logging but accepted
   `CONFIG_GPTIMER_ENABLE_DEBUG_LOG`. In the pinned IDF 5.5.5 source,
   `gptimer_priv.h` forces local VERBOSE logging and `gptimer_common.c` overrides
   runtime filtering. Driver output could therefore corrupt the correlated USB
   console despite global logging level 0. The same small guard now rejects both
   settings; defaults explicitly disable the override. The registered compiler
   regression compiles the actual adapter successfully with valid settings and
   rejects the override. A separate real-SDK generated configuration with global
   level 0/debug 1 fails compilation with the intended diagnostic. Normal clean
   firmware builds and runs; no logging workaround, parser relaxation or retry.
2. Added an actual shared application regression for ordinary CLI cancel/reset
   during a transmitted position trigger with missing acknowledgement and blocked
   USB output. Both Arduino and IDF variants verify physical TX/DE settlement,
   retained UNKNOWN execution, required-recovery interlock, same-axis conflict,
   unchanged generations and no extra motor/recovery/persistence work. Draining
   output does not replay a request. Existing full-output stop and init/PSRAM
   failure scenarios remain exercised through the same production application.
3. Corrected an evidence overclaim: `drv.input_dropped` counts console/parser
   admission discards. IDF's USB RX ISR ignores failed insertion into its 256-byte
   ring, and the fake input string does not model that overflow. Zero console
   discards does not prove loss-free USB reception. Incoming USB saturation
   remains unqualified; TX backpressure and strict correlation are tested. No
   larger-ring workaround or invented SDK loss counter was added.
4. Current architecture/board notes still described implemented discovery/CLI
   and available motion/load qualification as future work. Those statements and
   build guidance now match the actual implementation and bounded evidence.

The two requested independent reviewers inspected shared application semantics
and SDK/dependency/memory behavior in parallel. Root checked findings against
actual sources, SDK code, diffs, callers, tests and archives. The parity reviewer
authored the reset test; the platform reviewer independently reviewed it and
requested the recovery-interlock assertions, which pass. Both independently
reviewed the small logging correction. No duplicated workflow or public framework
dependency was found. Hardware setup assumptions remain example configuration.

## Requirement coverage

| Requirement | Actual implementation and verification | Disposition |
| --- | --- | --- |
| Real native consumer and board selection | Native CMake application/component, example Kconfig pins/topology, static owner startup; clean IDF 5.5.5 S3 build | PASS |
| Shared workflows and CLI/API semantics | Thin Arduino/native startup plus one `ProbeApp.cpp`, console, owner, validators, operations and caches; shared move/control failure tests on both USB wrappers | PASS; other native-family tests run the shared application without duplicating every suite per wrapper |
| UART/DE/capture ownership | Same single S3 adapter and runner, actual SDK guards, 20 us GPTimer sampling, retained interval/watermark evidence | PASS within short software/functional envelope; electrical timing unmeasured |
| PSRAM/internal state and failure handling | Explicit 206528-byte App PSRAM allocation without large internal fallback; UART 1720/load 4816 and native stack 8192/TCB 352 internal; ELF/map/config plus labelled actual-startup SDK failures | PASS; no real missing-PSRAM board claim |
| Loaded owner/console, memory and stack | Both images: ten 7-byte and ten 37-byte replies under 2000 us worker/5000 us owner delay/128-byte diagnostic load | PASS; incoming USB overflow and endurance remain open |
| Motion/stop/native settings parity | Both images: commands of 100 native increments at 60 RPM, normal/direct stop during separate finite 250-increment moves, lock delay 200→201→200; exact position-profile restoration | PASS as drive-reported functional evidence |
| Transport fault and cleanup | Deliberate stopped host9600 mismatch, retained negative result/interlock, explicit recovery and restoration115200 8N1, fresh checked reads | PASS planned negative scenario; malformed traffic source remains unresolved |
| Installed/public-header isolation | Clean desktop installed all 27-header runtime consumer and clean staged core-only S3/S2 consumers; no examples/vendor resources in core package | PASS; embedded fixtures compile/link only |
| S2 portable application pieces | Clean maintained IDF S2 target compiles real portable runner/owner/validator/console with fixed storage and unavailable port; excludes S3 UART | Compile/link PASS; S2 hardware/adapter NOT RUN |
| Ending state | Original Arduino timer image restored; fresh state/config/identity, load/debug/monitor off, DE released, owner/results empty, no recovery | PASS fresh drive reports; independent shaft measurement unmeasured |

All 69 CTest suites, 233 Python probe cases, generator/version/register/contrast
checks and three Arduino firmware builds pass. The desktop installed consumer
passes its runtime test with warnings treated as errors. Clean S3 firmware,
S3/S2 core-only and S2 portable consumers pass. Actual staged/header compile
commands and configurations are retained. The installed core still contains no
ESP32, Arduino, IDF, board or FieldCore headers.

## Exact images and repeated bench evidence

| Image | SHA-256 | SDK/framework | Bytes |
| --- | --- | --- | --- |
| Arduino S3 timer | `4f6d5bc82af7ade90689be5d2405c0bba22e69bbded3c364e7ca7072afadbb11` | Arduino 3.3.11/pioarduino 55.03.311, IDF 5.5.5 |580608 |
| Final native S3 | `ab6f5c6543891a96c4ec198984eaf918d972125144bcca9243ce6af742f107d9` | Native IDF 5.5.5 |495248 |

Requested tuple is node 1/115200 8N1, divider-reported baud 115211. Both use TX 47,
RX 48, DE 21 active high and declared receiver-disabled-during-TX topology. Native
defaults and actual generated SDK configuration are retained separately from
Arduino's effective qio_opi SDK header. The prior whole-flash backup remains
unchanged at SHA-256`09b0700e6b4f392e9bc3dbf4d935029cdacd0d2fe3d96d2be1613614680ea408`;
whole-flash rollback was not tested.

Final native qualification uses only `idf2-*` records. Earlier `idf-*` records
are retained separately: a final source-comment rebuild changed the ELF/image
hash, so the archived final binary was flashed and the complete bounded subset
repeated. The intermediate binary was not separately preserved and is not used
to qualify the final hash. Flash logs and immutable final image/ELF/map accompany
its actual tests.

| Matched measurement | Arduino | Final native IDF |
| --- | --- | --- |
| Unloaded10 probe latency min/max/mean us |5456/5696/5545.8 |5529/5589/5558.4 |
| Loaded 7-byte latency min/max/mean us |14571/17646/16207.6 |13843/17677/15843.4 |
| Loaded 37-byte latency min/max/mean us |17184/20402/18754.8 |16524/20325/18574.2 |
| Loaded 7/37 owner maximum gap us |8033/8035 |8040/8051 |
| Loaded 7/37 capture maximum gap us |57/57 |58/58 |
| Sample gap limit/breach |85 us/no breach |85us/no breach |
| Capture high water7/37 |7/37 of 64 |7/37 of 64 |
| Loaded 7/37 diagnostic lines sent/dropped |341/54;360/56 |359/43;356/59 |
| Console-level input discards |0 |0 |
| USB transport RX loss |Unmeasured |Unmeasured |
| Loaded core 1 task busy sample |34% |34% |
| Final internal free/minimum/largest bytes |336656/331496/278516 |369335/332848/270336 |
| Final PSRAM free/largest bytes |8177196/8126452 |8177408/8126464 |
| Final owner/worker stack headroom bytes |2724/3268 |2532/3272 |

These are samples for the recorded cases, not worst-case service guarantees.
Both forward commands request exactly 100 native increments; the reported raw
position difference is 99 on each image (Arduino 2870→2969; IDF 3387→3486).
The operation reports new activity/completion and subsequent zero speed; the
stored target/write and profile restoration are checked separately. Physical
feedback mapping and exact positioning accuracy remain unresolved. These
observations do not establish 100 measured shaft increments or diagnose the
one-count difference; no scale, target, threshold or timing default was changed.
Resolving that difference requires established feedback encoding/scale and a
correlated independent shaft-position measurement; current console evidence
cannot distinguish quantization, drive tolerance and physical position.
Task-runtime percentages exclude ISR work; separate capture callback cost is
retained in structured evidence and is not total CPU utilization. Final Arduino
memory/stack figures follow a new boot/read-only ending; earlier loaded/motion
samples remain in their original records. Diagnostic display drops are distinct
from protocol data/result loss. No several-hour soak or calibrated acceleration,
stop-distance, physical encoder mapping or independent shaft measurement is claimed.

Counter segments account for explicit typed reads after the passive stats
sample: Arduino 447 attempts/446 FRAME/4827 RX bytes across three boots;
final native 347 attempts/346 FRAME/3812 RX bytes. Each includes one deliberate
negative host-mismatch transaction. These counters include internal observation
polls; retained raw contexts are a subset, not an exhaustive wire recording.
Capture faults/RX errors/sample-gap breaches are zero. Intermediate native runs
remain archived without being added to those primary totals. Arduino's first
counter segment includes its separately repeated loaded 7-byte check.
The host-fault segment uses a passive after-snapshot and adds no read steps;
the other segment endings include nine explicit read steps/97 RX bytes. The
independent final evidence reviewer caught an interim unconditional nine-read
addition; the summary now derives the adjustment from each actual snapshot.

## Retained rejected attempt and negative traffic

`arduino-position` remains FAIL: motion-profile READ refused the prior restored
snapshot under changed host/binding generations before any motor write. Actual
`MotionProfileApp.h` deliberately preserves that original context. A separate
inspection proved saved/restored, matching original/current words and no unknown
execution; explicit local FORGET released it without motor traffic. The new
checked snapshot and finite motion then passed. No uncertain write was replayed,
no generation check was relaxed, and no production bug was inferred from a
correct admission refusal.

The deliberate Arduino 9600 host mismatch returned LENGTH with raw`F9`; final
native returned NO_RESPONSE. Both retain the failed transaction, refuse host
restoration before explicit recovery and subsequently verify restoration/new
reads. This reproduces the known malformed mismatch disposition on Arduino;
its physical source remains unresolved. It does not indicate a working-tuple
capture failure or prove the earlier native malformed attempt fixed. The earlier
prompt26 failed experiment stays failed. No moving disconnect or automatic
recovery/replay was introduced.

## Handoff and remaining qualification

COM13 ends on the Arduino hash above, node 1/115200 8N1, fresh alarm 0/runningfalse/
raw speed 0, raw position 3644. Profile`[30,100,100,60,0,250]` and lock delay 200 are
restored; load 0/0/0, debug/monitor off, DE released, no pending/reserved/retained
owner work and no recovery. Model/version remain raw`4EEA/0029`; algorithm 3 and
configured encoder 4000 do not resolve physical feedback mapping. External I/O
remains unconnected and its assignments unchanged.

Current FieldCore source at`d19758865852f4e215ea05c6572d060edc4f0c66` was inspected
read-only: actual RS485 transaction/task and USB console. Compatible conventions
are one owner, deadlines, bounded transactions and 64-byte nonblocking output.
Deliberate differences remain standalone TX 32, checked FC06/FC10 replies,
qualified closure evidence and retained non-consuming motor uncertainty versus
FieldCore's TX 8 sensor contract, prefix echo stripping and measurement/retry
workflow. No FieldCore changes or framework types enter the installed core.

[Structured evidence](ess_release_25_26_audit_2026-10-05.json) and the
[manifest-verified archive](ess_release_25_26_audit_2026-10-05_evidence.zip)
retain inputs, incremental raw/structured lines, rejected attempts, metrics,
image/configuration/ELF/map, clean build/negative SDK/test logs and audit scripts.
Earlier25/26 archives were independently hash/manifest checked and preserved.
The supported subset and earlier owning-prompt family gaps are unchanged.
S2 hardware, other SDKs/tuples, external fixtures/homing/segments, linear travel,
negative/continuous velocity, restart/persistence, incoming USB saturation,
independent physical/electrical measurements and endurance remain unqualified.
