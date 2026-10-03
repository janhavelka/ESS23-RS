# Typed non-changing ESS reads

[Reads.h](../include/MotorControlRS/profiles/ess_rs/Reads.h) implements identity
and the bounded configuration subset needed before motion. The installed core
depends only on its own headers. It performs no I/O, allocation, clock reads,
configuration changes or retries. Generated `Types.h` remains generated from
the original register ledger.

`prepareIdentity` snapshots a `ReadTarget` (application ID, unicast address and
binding generation), nonzero operation ID, supplied time, absolute deadline
and independently observed `ActiveSerialTuple`. `prepareConfig` also snapshots
four application `InputWiring` declarations; absent declarations mean UNKNOWN.
The caller owns one `ReadContext`. Treat its fields as read-only between calls.
`nextRead` copies the current `PreparedRead`; repeated calls yield the same work
without advancing it. The application must admit that step only once.

`advanceRead` consumes a correlated `ReadEvent`. It copies the supplied frame
or failed prefix, up to 37 bytes, and retains the full received length, codec
status, accepted TX count, uncertainty and application-defined transport detail.
No borrowed frame pointer survives the call. Wrong target, generation, operation
or step, malformed envelopes and backwards time leave the context unchanged.
An accepted event returns OK even when the operation becomes FAILED: inspect
`state`, `outcome` and `status`. The application validates transport framing
before supplying FRAME; the core checks target, function, exact count/length
and CRC again before publishing data.

Only complete success permits `getIdentity` or `getConfig` to replace an
observation. Atomic publication does not make five separate physical reads simultaneous; retain each window's timing provenance. All rejected, partial and failed operations leave the previous
observation unchanged. A checked exception is retained as REPLY_ERROR with its
EXCEPTION status/detail; malformed or foreign frames are separate failures.
Local CANCEL never commands a motor stop. One absolute deadline covers every
window, queue residence and execution. Qualified final closure at/before the
deadline can succeed when delivered later. An intermediate result serviced
after expiry cannot yield another request. Closure includes the final RTU gap,
not merely the last received byte.

| Operation | Reviewed FC03 windows (first/count) | Maximum success replies |
| --- | --- | --- |
| Identity | `0x0000/4` | 13 bytes |
| Configuration | `0x0010/2`, `0x0013/3`, `0x0017/3`, `0x0040/5`, `0x0100/2` | 9, 11, 11, 15, 9 bytes |

These windows deliberately exclude ledger gaps. No read exceeds 16 words;
there are no writes or action/latched-state reads. Register constants come from
the generated descriptors. Identity preserves raw model, version, active node
and DIP values. Unknown `0x4EEA` and firmware encoding are not mapped to a model
or version. The function and hardware manuals disagree on DIP layout, so the
raw DIP word is retained without interpreting a node address from it.

Configuration preserves unknown enum codes and reserved polarity bits while
marking typed values unknown. It includes direction, subdivision, stored node,
baud/format, limit settings, paired-word order, input assignments/polarity,
algorithm and configured encoder resolution. A known word order is required
before future paired payload decoding; these operations do not read a pair.
Stored serial codes are distinct from the actual host tuple used successfully
for these reads. They do not prove that pending settings are active or saved.
Subdivision remains SCALE_UNRESOLVED; it never replaces a command scale with
READBACK merely because its raw value resembles a documented default. Nonzero
encoder configuration supplies READBACK scale metadata; zero remains raw and
unusable for conversion. Neither proves physical accuracy, encoder manufacturer,
chip, interface or measured resolution. The rest of `UnitConfig` stays unknown.

Input assignments, declared wiring and observed levels are independent. Function
0 is the documented no-function assignment. UNKNOWN or UNCONNECTED wiring never
disables an assigned function. Input levels are unknown because this step does
not read state. Serial-only reads require no external I/O. Future homing,
limit, enable/stop and trigger operations must check only their relevant input
assignments, wiring and state; this operation changes none of them.

The standalone application maps retained Runner/BusOwner evidence to these
portable events and admits every window with `essValidator()`. It retains eight
frontend operations plus a separate recovery result. One current request ID per
read prevents duplicate admission. Intermediate owner results are explicitly
released only after their evidence is copied into the context. Another eligible
request may use the bus between windows. The frontend slot remains reserved
until explicit `release`; result inspection never consumes it. Recovery increments
the application binding generation and cancels old continuations, preserving
their outcomes and raw evidence. A bit-identical delayed RTU response still has
no wire request ID; host generations do not eliminate that ambiguity.

The existing console supports `read identity [address]`, `read config [address]`,
`profile ess_rs identity [address]`, `profile ess_rs config [address]`, `caps` and
`profile ess_rs caps`. Accepted records use canonical `read-identity` and
`read-config` names. One later `type:read` terminal record carries copied
per-window TX/RX data and timing. `result` inspects the same retained operation;
`cancel` and `release` use its operation ID, independently of command correlation.
Input/output remain bounded and serviced during active transactions. Capabilities
advertise only implemented non-changing reads, never motion or exact-model
qualification. `config` is a local host report; it labels device settings cached
only after a complete configuration observation, with target/generation IDs.

The operation coverage inventory is
[ess_rs_operations.json](reference/ess_rs_operations.json). Its record IDs are
existing ledger names. `python scripts/check_ess_operations.py --details` derives
the complete denominator, access, pages, source issues and independent read,
write/action, implementation, CLI and evidence dispositions without creating
another address catalogue. Reads never satisfy write/action obligations.

Current native/public-package and hardware evidence, memory sizes and remaining
motion prerequisites are in the [prompt 05 handoff](reports/ess_release_05_2026-10-04.md).
