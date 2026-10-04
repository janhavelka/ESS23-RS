# Prompt 07 fresh independent audit

Audited the complete [original prompt](../prompts/ess_release/07_coordinates_and_target_preparation.md),
execution contract, current production source, callers, tests and implementation
diff `7cf8a9c..dd129fc`. Baseline `dd129fc`; the final commit is the commit
introducing this report. Only prompt 07 corrections were implemented.
Software: **PASS**. Available read-only COM13 checks: **PASS**.
Physical origins, machine travel, encoder, motion/stop and independent electrical
qualification remain **NOT RUN**.

[Audit data](ess_release_07_audit_2026-10-04.json) retains current image identity,
raw readback, host replies and measurements. The
[evidence archive](ess_release_07_audit_2026-10-04_evidence.zip) preserves baseline
reproductions, final logs/image/source, independent oracle scripts and a SHA-256
manifest. The [original implementation report](ess_release_07_2026-10-04.md)
and its evidence remain unchanged historical records.

## Findings and corrections

Four confirmed defects were reproduced against the original code and corrected:

1. A supplied native reference could bypass the requested relative displacement's
   range check. With native range [-2,2], requested 5/2, rounding toward zero and
   reference -1, the API accepted effective delta 2 / endpoint 1; the same request
   without reference correctly rejected. `preparePosition` now checks requested
   displacement before adding the reference, then separately checks requested
   and effective endpoints. Nonzero radian displacement intervals follow the
   same rule. Signed exact/radian counterexamples reject with unchanged output.
2. Zero radians acquired artificial origin/reference uncertainty. EXACT, FLOOR
   and CEIL could reject an exact integer target; a zero displacement at a soft
   boundary could fail its limit check. Zero radians now use the existing exact
   integer path, retain original input and report zero numerical error with
   `exactArithmetic=true`. Default EXACT needs no approximation flag/budget for
   zero. Ordinary frame, scale, source, origin, basis, generation and limit
   checks remain in force. Tests cover all rounding modes, signed 64-bit origins,
   a referenced soft boundary, selected binary64 input and API/CLI parity.
3. Binary64 arithmetic treated a normal radian conversion cancelling its origin
   onto zero as underflow. Underflow is now checked on converted displacement
   before origin addition. A valid cancellation retains its approximate provenance
   and nonzero error bound; actual conversion underflow still rejects.
4. Decimal trailing-zero normalization turned malformed fractions `1/2.0` and
   `1/2.000` into valid `1/2`. The public parser now rejects decimal fraction
   operands before normalization. Core and actual console tests cover preparation,
   configuration and error-allowance arguments, unchanged rejection outputs and
   rejection before the host callback. Valid `INT64_MAX.000` still parses exactly.

No new type, conversion path, task, queue, driver operation or wire setting was
added. Shared `UnitFactors` remains the only spatial factor planner. Generated
enums/catalogue/access policy and device-operation inventory are unchanged.
Fixed uint64 exact intermediates still reject overflow rather than approximate.

## Requirement and failure coverage

| Requirement / failure scenario | Production path and actual API | Native/build evidence | Hardware result and artifact | Remaining proof |
| --- | --- | --- | --- | --- |
| Exact/rational native/motor/load preparation, identified encoder and travel dependencies | Installed `Axis.h`, `preparePosition`, shared unit factors | Eight axis groups; signed extrema, factor cancellation, gear/lead/source, origin overflow and stale references PASS | Exact large native/degree previews PASS | Physical scale and encoder source remain unresolved |
| Requested/effective displacement and endpoint limits | Pre-reference requested check, endpoint/start checks | Positive/negative exact and radian bypass regressions PASS | Reference-dependent checks remain unavailable on this drive | No native feedback relation is inferred from raw zero |
| All rounding policies, zero displacement and numerical provenance | Exact quantizer and zero-radian integer path | Every zero rounding mode, INT64 origins/reference boundary, normal radian cancellation and genuine underflow PASS | Default zero-radian preview succeeds exactly; nonzero radians remain approximate | Nonzero radians remain bounded binary64 approximation |
| Exact error allowances and broad numerical boundaries | Rational cancellation and fraction/binary64 comparison | Independent 20,000 frame/scale/basis/origin cases plus 25,000 large-rational allowance cases PASS | Explicit bounded policies remain unchanged | Products exceeding fixed storage reject |
| Strict exact parsing and API/CLI parity | Public `parseExactNumber`, actual Console callback | Malformed fraction operands/allowances reject before callback; full-width decimals and parity PASS | Old image accepted malformed fraction with no TX; new image rejects it | Console text bound remains 127 bytes |
| Evidence-gated host changes and generation invalidation | `configureAxis`, `setAxisOrigin`, application witness/invalidation | Existing stale/busy/unknown-reference/atomic rejection/sticky-exhaustion regressions PASS | No-witness host change and unavailable origin/soft limits reject; assumptions remain labelled | Drive-reported non-running does not measure shaft standstill |
| Independent preferences, public/package/platform boundaries and no implicit writes | Existing UnitSettings, installed core, example host adapter | 20 native suites, 109 Python cases, installed C++11 consumer and four builds PASS | Host preferences read back independently; device settings unchanged | Ramp encoding and move/action execution remain later work |

Three parallel reviewers independently inspected numerical boundaries, API/CLI
semantics and full requirements/simplicity. They used the actual source/diff;
the lead verified counterexamples and corrections. A reviewer who did not author
the fixes re-audited the final integrated code and tests and found no remaining
confirmed defect within this prompt. No speculative encoder-origin setter or
full-motion reporting API was added to this pure preview block.

## Verification and bench evidence

Full **20/20 CTest suites**, **109 Python cases**, eight generator and eleven
inventory cases, generated version/descriptor checks and five offline contrast
hashes PASS. Axis groups 8, console 22, actual application 43 and load application
45. Fresh exported-header/archive-only installed consumer passes under C++11,
without example include paths. `bench_s3_units`, `bench_s3_probe`,
`bench_s3_load_poll` and `bench_s3_load_timer` build PASS.

The complete axis suite also passes unoptimized `-mlong-double-64` with strict
warnings. Optimized `-mlong-double-64` crashes on both baseline and current MinGW
builds: GDB identifies the supplied runtime's `truncl()` ABI mismatch with that
compiler flag. Failed reproductions and backtraces are retained; this unsupported
flag/runtime combination is not reported as PASS. Normal optimized native builds
and the actual binary64 ESP32 image pass. Independent Python Fraction oracles
compare exact mathematical outputs and adjacent binary64 allowances without
copying the implementation. The lead reran both oracles successfully.

COM13 USB 303A:1001 / serial `3C:0F:02:CD:6B:98` was rechecked. Baseline passive
preflight preserved the previously flashed image's counters, load/monitor state
and malformed-fraction acceptance without bus traffic. Final uploaded timer image:
**390032 bytes**, SHA-256
`88f7cd06757f6f62c13f15f870d0484cbf0fc35de7d9d74fbe00f7e63cee994c`.
Pins TX/RX/DE 47/48/21, active-high DE, node 1, 115200 8N1, timer 20 us,
sample-gap limit 85 us. The existing 304-us turnaround exception and 1750-us
final gap remain bench-specific, electrically unqualified. Original CO2control
backup is preserved and rehashed; no vendor artifact changed.

The final finite campaign sent **23 checked FC03 frames / 209 RX bytes**:
two configuration operations, one state operation and ten single-attempt model
probes. New zero-radian host preparation succeeds with effective displacement 0,
exact provenance and zero error; malformed preparation/configuration fractions
reject without changing host generation/scales. Existing large integer, degree,
fractional/zero, approximate radian, preference and rejection checks pass.
No host preview/change sends motor traffic. No motion/write/save/retry/recover
or statistics reset was performed.

Raw configuration before/after is identical: direction 0, subdivision 1000,
word order 0, input polarity 0, assignments 1/2/3/0, algorithm 3 and encoder
resolution 4000. State remains alarm 0, motion 1, logical I/O 0, unsigned position
0 and speed 0, running false/in-position true/enabled true. Host generation ends
at 7; command 1000 and gear 1 remain explicit operator assumptions, not a wire
unit inference. Origins/encoder relation/soft limits remain unknown. Model
`0x4EEA`, position source/sign/units and ramp semantics remain unresolved.

Configuration operation latency (serviced minus started) was 34.927–34.946 ms;
state 21.179 ms. Exact host round trips and transaction closure/delivery bounds
are retained in the data. Ending starts/frames 23/23, zero failures/timeouts/
cancelled/capture/UART errors; optional trace overwrites 311, capture high-water 2,
maximum owner gap 143 us and capture gap 51 us. Load/monitor off, DE released,
pending/retained/reserved zero, no recovery needed. Driver query after command 59
reports 59 input lines/1360 bytes, zero input drops/output blocked/short writes;
all 62 exact command records are retained. Worker console lines/drops are zero.

| Object / resource | Native bytes | Actual Xtensa bytes / final watermark |
| --- | ---: | ---: |
| App / frontend record / Console | 65264 / 928 / 10000 | 62416 / 752 / 9712 |
| AxisConfig / AxisReference / PositionRequest / PreparedTarget | 184 / 64 / 64 / 192 | 184 / 64 / 64 / 192 |
| Internal free / minimum / largest | — | 336148 / 330988 / 278516 |
| PSRAM free / minimum / largest | — | 8323628 / 8323628 / 8257524 |
| Owner / worker stack headroom | — | 2852 / 3268 |

Layouts/storage ownership are unchanged: caller-owned core values, application
and retained/trace storage in PSRAM, required capture/driver state and stacks
internal. Input 128 bytes including terminator / ten tokens and output 4096 bytes
remain bounded. Existing maximum exercised configuration/status/health records
remain 3562/3850/3685 bytes. This short unloaded regression is not a loaded,
electrical, machine-travel or endurance qualification.

## Integration and handoff

Current FieldCore RS485 module/owner/task/backend/CLI were inspected read-only at
`47f52e5519725ee16a43da576f4ba52d78b50551`. Compatible request/work/result and
retained-observation vocabulary remains intact. Deliberate differences remain
pure supplied-evidence coordinate functions, synchronous no-bus host replies,
portable callbacks/checked validators and exact retained deadlines, with no
firmware framework or sensor retry policy in the core. FieldCore's eight-byte
TX storage and equal-TX echo candidate handling still require separate motor
integration review; no FieldCore files were edited.

The [preparation contract](../axis_preparation.md) now states independent
relative displacement/endpoint checks, exact zero radians and normal cancellation
versus underflow. Public types/signatures/lifetimes and generation rules remain
unchanged. Prompts 08–10 must preserve configuration/request provenance and
generation, resolve profile width/sign/readiness before yielding motor writes,
and keep acknowledgement distinct from physical completion. Physical reference,
machine travel, motion/stop and independent electrical evidence remain their
explicit gates. No later prompt was started.
