# ESS bounded homing API

[Homing.h](../include/MotorControlRS/profiles/ess_rs/Homing.h) provides
`ESS_RS::prepareHome`, `nextHome`, `advanceHome` and `getHomeReference`.
The common `MotorControlRS::prepareHome` calls the same profile operation.
These functions perform no I/O, allocation, retry, clock read or implicit
input/configuration change. Caller-owned `HomeContext` retains bounded state
and copied evidence; treat its fields as read-only between API calls.

`homeMethod` and `homeMethodAt` expose all 35 documented method dispositions.
Methods 33 and 34 implement negative/positive internal-Z search, and 35 uses
the current mechanical position. Other switch methods remain unimplemented;
method 18 and collision methods −1 through −4 have unresolved source semantics.
See the [complete method table](ess_homing_methods.md) for trajectories,
prerequisites, source conflicts and the prompt 15 fixture handoff.

## Admission and yielded work

`HomeRequest` supplies a method, configuration generation, native search and
return speed words, native shared ramp word and offset. Only offset zero is
available. Conservative implementation bounds are 5–3000 for search, 5–300 for
return and 30–2000 for ramp; the documented physical units/defaults do not
resolve their wire scaling. Applications qualify the exact native values and
their mechanical effects before admission.

`HomePrerequisites` binds qualification to the exact target and configuration
generation. It requires qualified method/mechanics/direction, rates, ramp,
zero offset, enabled stationary alarm-free readiness and actual interfering
input configuration. `qualifiedMethod`, `qualifiedSearchSpeed`,
`qualifiedReturnSpeed` and `qualifiedRampTime` retain the exact independently
qualified request. Preparation compares all four witnesses before publishing
work: changing a method or native word cannot reuse another request's true
qualification flags. The application copies these witnesses from its retained
qualification, without deriving them from incoming intent. Terminal JSON keeps
them in `prerequisites.qualified_parameters`; Python checks them against the
staged method/rate/ramp words.

Methods 33/34 additionally need qualified internal Z;
35 needs neither Z nor an external sensor. Unknown input assignments are not
qualified by unplugged wiring. Existing active auxiliary `0x0030` must already
be qualified as `KEEP_POSITION_SET_ZERO` (7). Homing never writes that auxiliary
setting or changes input functions/polarities.

Rejected preparation leaves the output unchanged and yields no traffic. Valid
preparation yields exactly FC10 `0x0031/6`, containing method/rates/ramp and two
zero offset words, followed by FC06 `0x0027 = 0x0010`. Offset subsets, split
writes, nonzero offsets and collision aliases are unavailable. No staging
atomicity, rollback, activation or persistence guarantee is inferred.

The application reserves the same axis across setup, trigger, waits and reads.
`nextHome` repeatedly yields the same token until an event advances the state.
Readiness limits cap setup/trigger deadlines without renewal; the immutable
operation deadline and `ActionOptions` poll interval/count bound observation
work. `PreparedHome` owns its 21-byte request buffer. Applications preserve
the supplied token's target, operation, step, expectation and deadline through
queueing, physical transmission and response validation.

`advanceHome` accepts `ActionEvent` with explicit transmission count/completion,
checked closure intervals and confirmed response provenance. Wrong correlation
or invalid envelopes leave the context unchanged. Valid events copy bounded
raw evidence. A matching start echo acknowledges the command only; a missing
echo after accepted transmission leaves execution unknown and never permits
automatic replay. Partial or uncertain setup remains visible on failure.

## Completion, stop and reference

Methods 33/34 require fresh post-trigger RUNNING and HOMED-low observations,
then stopped/in-position HOMED-high. Method 35 requires HOMED-low to high; its
fresh admission baseline may supply low because no search motion is required.
Old HOMED-high, a missed transition, start echo or interrupted search cannot
establish a new reference. Retained low/activity/completion evidence records
the relevant observations. A subsequent checked current-position pair must
contain two zero words before completion is published.

Alarm, released state or soft-limit flags fail the operation. Cancellation ends
local sequencing and does not stop the motor. The application settles any
in-flight transaction before executing explicit priority stop through the
existing action API. Timeout, recovery and stop interruption preserve execution
uncertainty; none proves standstill. No new switch transitions or return path
are claimed by the internal-Z/current-position subset.
Method 35 also fails if a fresh report shows RUNNING, since activity contradicts
its nonmoving current-position procedure and cannot establish the reference.

`getHomeReference` additionally requires caller-qualified correspondence
between homing zero, feedback zero and actual command coordinates. It returns
a native-zero `AxisReference`, bound to the admitted target/configuration and
aged from the completing status request's eligibility, rather than response
delivery or the later zero read. Errors leave its output unchanged. It changes
neither host origin nor generation. Explicit device-position clear and host
origin setting remain separate operations.

The standalone application invalidates host origins/reference/derived limits
once homing setup transmission is accepted, retains that operation's historical
generation, and reconciles only a successful fresh reference into the current
derived cache. Failed/uncertain writes retain a same-address conflict until an
explicit checked stop reconciles it. Local result release and host transport
recovery do not erase that conflict. A new state refresh supplies the stationary
cache evidence needed by subsequent host-origin/configuration operations.
After completion, a shared cached RUNNING/released report whose latest
observation bound is strictly before the new reference's observation bound
cannot erase that newer reference. Overlapping or newer cached activity still
invalidates coordinate confidence. Retaining the reference does not promote
that older shared cache into current stationary evidence.
The first shared feedback refresh also compares against current evidence:
invalidated feedback captured before homing cannot turn a new zero into a
false external-motion change. Fresh nonzero raw words invalidate the qualified
native-zero witness even when pair word order is unresolved; later changes
between current feedback observations still invalidate coordinate confidence.
Generation exhaustion cancels further homing work and prevents reference
publication; any accepted write and its uncertainty remain retained.

## Console and finite host scenarios

The command calls the public preparation/execution API:

```text
home methods
home 35 60 30 100 zero [address]
home 33 60 30 100 zero [address]
```

The parameter names in help are `search_native`, `return_native` and
`ramp_native`; the example numbers do not assert qualified physical rates.
`home methods` reports implemented, unimplemented and unresolved choices with
input/index/movement prerequisites. Terminal results and retained `result`
inspection preserve acknowledgement, completion, raw flags, generations,
uncertainty and bounded evidence.

The finite Python `home` scenario offers method 35 with an explicit normal/direct
cleanup stop, retained-result inspection and final state/health reads. Methods
33/34 need independently qualified index/mechanics before a physical campaign.
The current firmware keeps write timing/echo and method qualifications closed;
no physical homing or independent zero/return measurement is established by
native tests or a read-only bench regression.
