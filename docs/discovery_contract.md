# Drive discovery and non-changing probes

Discovery and a minimal non-changing presence probe are required design
considerations for every supported drive profile and manufacturer grouping.
This contract defines the public API and standalone CLI design. The ESS raw
probe codec and [read-only probe CLI](e2_probe.md) are implemented, with bench
response evidence. Common operation preparation, scans, full identity reads
and exact-model/non-changing qualification remain future work.

The [axis API](axis_contract.md) exposes the common operations, the
[profile contract](profile_contract.md) records per-device evidence, and the
[CLI](cli_contract.md) is one consumer. The application owns serial settings,
bus arbitration, scan scheduling, time, cancellation and result storage.

## Three separate operations

| Operation | Meaning | Traffic |
| --- | --- | --- |
| Catalog | List compiled manufacturers/profiles, identification methods and probe availability | None |
| Probe | One smallest reviewed query sufficient to test a selected endpoint's responsiveness | Normally one request/reply; explicitly bounded by profile |
| Discover | Iterate an explicit bounded set of candidate profiles, endpoints and serial tuples; refine identity where documented | Scheduled probe/identity reads only |

`prepareProbe` is distinct from `prepareReadIdentity`. A quick probe can
establish that a responder exists without reporting its complete identity or
state. If the smallest documented identity read is also the best probe, both
operations may share a codec path; they still report different evidence
requirements. A profile with no documented suitable probe returns an explicit
unsupported result before yielding traffic. No placeholder success is allowed.

"Non-changing" means no requested motor action or change to configuration,
coordinates, enable state, alarms, pending commands or persistent storage,
and no consumption of latched status/events. It does not mean a nonfunctional
stub. Probe selection must account for documented read side effects and any
operational effect of communication itself. A nominal read or diagnostic
opcode alone is not evidence of suitability. Do not use stop, enable, jog,
alarm reset, save, guessed invalid writes or read-to-clear fields as probes.

## Implemented ESS probe

`ESS_RS::buildProbe` builds FC03 for read-only model register 0x0000, one word
(function manual physical p68). `parseProbe` checks the complete response and
returns the raw model value. No consuming side effect is documented for this
identity field. This is the smallest holding-register read: eight request
bytes, seven success bytes or five exception bytes. No universal timeout,
confirmed model interpretation or hardware behavior is established yet.

Unknown raw values remain observable. Probe success refreshes only the caller's
responsiveness evidence, not position, readiness, alarm state or completed
motion. The codec performs no scan, retry, setting change or I/O. These helpers
will underpin the planned common `prepareProbe`; they are not that sequencer.
The [serial comparison](reference/08_serial_protocol_review.md) records other
manufacturers' prospective queries and why read-only alone is insufficient.

## Per-profile evidence and API

Extend the profile coverage ledger with:

- Exact supported probe/identity operation, source revision/page and applicable
  models/firmware. Record an explicit unsupported or unresolved reason where
  necessary instead of silently omitting a profile.
- Smallest sufficient request/response, address format, checksum/framing,
  exception behavior and exact validation requirements.
- Read side effects and why the chosen query satisfies the non-changing
  contract; declare any communication-watchdog interaction where documented.
- Expected response bounds, measured timing when available, and a caller-set
  deadline. "Fast" means minimal work with an explicit bound, not one universal
  timeout across all manufacturers.
- Identification fields and confidence that the reply can establish:
  responsiveness, family, exact model, firmware or ambiguity.
- Permitted serial tuples and endpoint ranges for bounded discovery, plus
  compatible candidate sets on which those queries may be used.

The common API provides `getDiscoveryCapabilities` and `prepareProbe`, with
fixed-size descriptions/results and the existing caller-owned operation
contract. Capability inspection performs no I/O. Discovery orchestration
belongs to the consumer and can reuse those operations without parsing CLI
strings or depending on Arduino, ESP-IDF or FieldCore services.

Return structured evidence for a validated responder, identity mismatch,
ambiguous candidates, device exception, no reply, malformed/checksum failure,
unsupported/unresolved operation, busy admission or exhausted budget. Keep
normal operation/transport outcomes separate from identification confidence.
A checked exception can prove a responder rejected the query; it is not a
successful identity read. A familiar response shape does not establish a
manufacturer. An unrelated slave reply does not prove the selected endpoint
responded. A timeout is an observation, not proof that no motor exists.

The application records probe attempt/success times separately. A successful
probe can refresh communication-presence evidence, but cannot refresh motor
position, alarm, homing or readiness observations that were not read. It does
not automatically resolve an earlier uncertain motion command.

## Bounded discovery behavior

The caller selects a profile or manufacturer group and supplies the endpoint
range, baud/format candidates, per-query deadline, overall deadline, request
limit and result capacity. A manufacturer filter expands only to reviewed
profiles in that group; it does not choose a universal manufacturer command.
The default scope is the selected profile and current serial tuple, not all
possible protocols and settings. Broadcast discovery and address reassignment
are outside this read-only contract.

Scanning must not assume that queries harmless to one protocol are harmless
to another. Use explicitly selected, reviewed candidate sets on a compatible
bus or an isolated target. If a safe candidate set is not established, report
the missing prerequisite rather than transmit guessed protocol mixtures.
Supporting multiple profiles in one library does not prove safe coexistence
on one physical bus.

Only one request is in flight on a bus. Advance scans cooperatively, publish
bounded progress and stop on cancellation/deadline/capacity exhaustion with
an explicit incomplete result. Preserve already collected findings. There
are no hidden retries; the default quick probe uses one attempt. Any retry
budget is explicit, allowed only for the reviewed operation, and counted
against the scan limits. Stops and required motion refreshes take priority;
reject a scan if its host-setting changes or timing would conflict with
active operations.

A scan may change host serial settings only at settled transaction boundaries
under application ownership. Preserve the original host selection and tuple,
restore them before resuming ordinary work, and report failed restoration.
Each finding retains the actual tuple/address/profile evidence under which
it was observed. Discovery does not automatically rebind an axis, change
device settings, infer a coordinate scale or mark a motor ready for motion.

Several profiles may accept the same reply. Retain all matching candidates
and refine with documented non-changing identity reads where available;
otherwise report ambiguity. Distinguish malformed/contended traffic from
confirmed identity. Duplicate-address devices or indistinguishable replies
are not automatically repaired, and a valid reply does not prove that only
one physical device is present at that address.

## CLI and qualification

- `profile list` is the local manufacturer/profile catalog; `caps` includes
  probe/discovery availability and gaps.
- `probe` / `ping` prepares the selected profile's minimal query.
- `read identity` explicitly requests supported identity fields.
- `discover [manufacturer <id> | profile <id>] [bounds...]` performs the
  explicit bounded scan. Help specifies address/tuple, time/request and
  result limits. `cancel` stops further scan work without erasing results.

Native tests must establish that every probe/discovery path emits only its
reviewed queries, refuses unsupported/ambiguous candidate prerequisites,
validates complete responses, preserves uncertain identity, respects budgets,
and yields to stop/refresh obligations. Test malformed replies, exceptions,
wrong addresses, late frames, duplicates, cancellation, partial findings and
host-setting restoration. CLI/direct API results must agree.

Hardware qualification records probe latency and behavior with exact model,
firmware and serial settings, plus evidence that the query does not perform
a functional operation or consume state. Each profile remains explicitly
unimplemented/unqualified until its own evidence exists. ESS is first;
Leadshine iEM-RS remains the contrasting design target. No generic probing
command or universal response-time guarantee is presumed.
