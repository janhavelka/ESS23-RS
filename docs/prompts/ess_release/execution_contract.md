# ESS release execution contract

Read this entire file for every dispatched prompt, together with root
[AGENTS.md](../../../AGENTS.md), this set's [README](README.md), the active prompt
and its named contracts. Implement only the dispatched block. No prompt
automatically starts the next one. These instructions do not add a universal
motor framework, another manufacturer's implementation, or CANopen support.

## Preflight and actual dependencies

1. Inspect branch, upstream, dirty files and current definitions/callers/tests.
   Fetch/check remote state; synchronize ordinary changes without rewriting
   published history. Preserve unrelated work. One root agent owns commits.
2. Read predecessor handoffs and verify their actual production APIs and tests.
   A filename, green test count or documentation checkbox is not proof of the
   prerequisite. Rebase historical paths and assumptions against the tree.
3. List the existing helpers to reuse and redundant paths to remove. Proposed
   new filenames/types in prompts are suggestions, not already available APIs.
   Adapt names once at the owning step and update downstream prompts/handoffs.
4. Read the reference inventory and relevant original PDF pages before changing
   motor semantics. Preserve vendor bytes. Generated `Types.h`, `Registers.h`,
   catalogue and access policy come from the ESS ledger/generator; do not
   hand-edit generated output. New handwritten operation types belong in a
   real, separately owned header when needed. Do not create placeholder APIs.
5. Audit the actual changed code and its caller/test chain before and after
   implementation. Reinspect relevant FieldCore module/owner/backend/CLI code
   read-only, using the [source audit](../../reports/2026-10-03_promptset_audit.md)
   as a starting map. Record conventions reused and intentional differences;
   old reports do not substitute for current code. For blocks with no matching
   FieldCore path, explain that scope instead of forcing an abstraction match.

All previous numbered steps must have a recorded disposition before the next
is dispatched. Required software contracts must exist and pass their tests.
Hardware status is separate: **PASS, FAIL, NOT RUN, or NOT APPLICABLE with a
reason**. Missing instruments, fixtures or firmware semantics do not become
PASS through a simulated test. They can permit independent software work to
continue, but block dependent hardware actions and qualification claims.
Fix bounded prerequisite defects in their owner and document the correction;
do not absorb an entire missing future subsystem into the selected block.

## Architecture, simplicity and reuse

- Keep public units, preparation, codecs and bounded sequencing in the
  framework-independent core. No Arduino/IDF/FreeRTOS/UART, clock, logging,
  heap allocation, retries or application health policy enters that core.
- Keep queueing, transport, DE, timestamps, scheduling, caches and health in
  the application. Reuse `RtuRunner`, `E2Uart`, the fake SDK/wire schedule,
  `ProbeConsole`, units, ledger and ESS codecs. Extend actual call paths rather
  than maintaining a second implementation just for tests.
- FieldCore is read-only reference throughout this set. Follow its ownership
  ideas without copying its task or creating another UART owner. Integration
  there remains a separately scoped task, outside this sequence.
  Borrow bounded admission/result reservation, exact request identity, passive
  snapshots and cooperative module work. Do not copy both of its ingress/owner
  queues into this single-owner example, its eight-byte TX limit, automatic
  request-prefix echo stripping, float measurement payloads or sensor retries.
  Its E2 bus abstraction is not this repository's physical E2 board label.
- Prefer a small number of cohesive types, direct functions and explicit
  state transitions. Use short engineering names and Doxygen comments for
  public invariants, lifetime, units, errors and effects. Refactor duplicated
  validation/state ownership when it simplifies the complete path. Do not
  add a registry, base-class hierarchy, generic event engine, template layer,
  compatibility alias or extra task merely for hypothetical future reuse.
- Every native device feature goes through a typed public profile operation;
  common and CLI routes call that operation. Raw register writes do not count
  as typed coverage. Surface unsupported, unresolved and unimplemented reasons
  distinctly, before yielding traffic. No success-shaped placeholders.
- Preserve exact values, target/configuration generations, raw unknown bits,
  codec errors, alarms, acknowledgement, completion and execution uncertainty.
  Do not infer a stop from timeout, a ready axis from a probe, or non-execution
  from a lost acknowledgement. No automatic replay of uncertain motor writes.
- Allocate fixed storage with explicit ownership. Put larger task buffers,
  traces and retained results in PSRAM where valid. Keep required ISR/driver
  memory and stacks internal. Record sizes and runtime watermarks after changes.

## Subagents and final independent review

Spawn subagents for the two roles named in each prompt, within the session's
actual concurrency limit. Assign disjoint edit paths; agree shared types before
delegating edits. The lead owns integration, build metadata, generated files,
the bench and final documentation. Useful roles are source/manual analysis,
independent test design and a reviewer of the integrated result. Agents use the
same model policy as the session; these prompts impose no model selection.

Have a reviewer who did not author the affected implementation audit the final
diff against the whole prompt, actual callers and failure cases. Require
specific defects or proof gaps, not a generic approval. Fix findings and rerun
affected checks; request a focused re-review of fixes. If agents are unavailable,
continue with a separate explicit self-audit and record that independent review
was unavailable rather than claiming it happened. Subagent output is evidence
to inspect, not authority to skip the lead's review.

Only one process owns COM13. Only one owner runs PlatformIO builds/uploads
against shared `.pio` state. Agents must not flash, open ports or change drive
configuration without coordination with the lead.

## Native, build and hardware verification

Start with focused tests that reproduce the required behavior and failures.
Test unchanged outputs/no TX on rejected requests, ownership/lifetime, bounded
memory and service work, cancellation, delayed events, stale generations and
uncertain writes where relevant. Do not add tests that only mirror a helper.
Register all new native suites in CMake. Run the full existing suite once after
integration; repeat broader checks only for new changes or unresolved concerns.

The current commands, from the repository root, are:

```powershell
cmake -S . -B build/native -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/native
ctest --test-dir build/native --output-on-failure
python test/generator_test.py
python test/bench_probe_test.py
python scripts/generate_version.py check
python scripts/generate_ess_registers.py --check
python scripts/prepare_serial_contrasts.py --check
.\scripts\pio.cmd run -e e2_s3_probe -e e2_s3_load_poll -e e2_s3_load_timer
git diff --check
```

Use installed compilers/generators and include Python's checks even though
CMake currently makes Python optional. Build affected firmware environments;
include the units preview when changing public units/headers. Do not replace
build failures with weaker compiler warnings. Prompt 27 will add one reusable
verification entry point; it does not exist at this baseline. Do not copy
FieldCore verification commands that have no counterpart here.

Before live testing read [bench notes](../../hardware_bench.md), the current
report and [probe guide](../../e2_probe.md). Inspect actual USB/firmware identity,
pins, image and settings; COM13 and the recorded values are candidates until
checked. Preserve the original firmware backup and record each uploaded image.
Retain one-attempt read-only probes as a quick regression:

```powershell
python scripts/bench_probe.py --port COM13 --log build/bench/<new-run>.jsonl stress --count 10 --interval 0.05
```

Replace the log placeholder with a new descriptive filename. Check and record
the current load configuration; do not silently recover or clear prior faults
to obtain a passing baseline. Set workload explicitly when a scenario calls for
it. Extend Python feature tests as operations become real; prompt 24 consolidates
them, it does not postpone testing until then.

Run a focused hardware feature check and a short existing-function regression
whenever behavior reaches the board. Reproduce hardware failures, add bounded
diagnostics to distinguish causes, establish the culprit, apply the simplest
proper fix/refactor, and rerun both the failure and regression. Keep failed
attempts. Never turn missing evidence into a claimed fix, silently relax timing,
or let diagnostic overhead invalidate the experiment unnoticed.

Existing free-shaft bench authorization persists. Use documented operations
and small finite scenarios within that setup without repeatedly asking for
permission already given. A needed limit switch, physical power interruption,
external shaft measurement or oscilloscope is a concrete fixture requirement;
do not simulate it and claim physical proof. Record blocked dependent cases
and continue independent work. Before any moving test establish usable stop,
units, configuration and the prompt's actual bench envelope. Use bounded stop
cleanup on exit or error and verify standstill before reporting cleanup success.
If communication fails, retain an unknown outcome; software cannot guarantee
that an autonomous motion stopped. Deliberate link-loss tests during continuous
motion require an identified independent bench stop mechanism.

## Audit, evidence and commit/sync

For each execution maintain `docs/reports/ess_release_NN_<date>.md` with:

| Requirement / failure scenario | Production path and actual API | Native/build evidence | Hardware result and artifact | Remaining proof |
| --- | --- | --- | --- | --- |

Record baseline/final commit, exact firmware hash, configuration, command
limits, test versions, latency/errors/gaps, relevant RAM/PSRAM/stack, observed
ending state and cleanup outcome. Label simulated vs physical evidence and
historical vs current-image results. Reports keep observations; contracts keep
design truth; the register ledger keeps source facts. Avoid competing copies.

Re-audit the integrated change for bugs, contradictions, stale/duplicate code,
missing behavior, unbounded work and unnecessary abstractions. Remove redundant
paths within scope and rerun affected checks. Unresolved vendor ambiguities
remain in the coverage denominator with explicit reasons; do not invent a
register, alias, unit or serial trigger to make the matrix green.

Update the roadmap/backlog, relevant API/CLI docs, coverage and this set's index
with separate implementation/hardware dispositions. Complete new core source
registration, package exports and version generation as needed; do not force
a version bump for every documentation-only edit. Review and stage only owned
files, validate, commit, push and verify upstream synchronization. No empty
commit for a read-only dispatch or unsupported claim of successful sync.

Handoff: state implemented behavior, actual headers/types and lifetime,
commands/scenarios run, defects fixed, independent review, commit/push, remaining
qualification and the exact next prompt dependency. No hardware runs are
required merely to prepare or edit this prompt set.
