# Bounded ESS discovery

The installed [Discovery.h](../include/MotorControlRS/Discovery.h) provides
`discoveryProfileCount`, `getDiscoveryProfile`, `getDiscoveryCapabilities`,
`prepareProbe` and `checkProbe`. Only STEPPERONLINE / ESS-RS is implemented.
Inventory and capability calls perform no I/O. A probe emits exactly FC03 model
word `0x0000/1`; optional refinement uses the existing typed identity read
`0x0000/4`. Original function-manual physical p68 marks these fields read-only;
no consuming effect or ESS communication-watchdog interaction is documented.

`PreparedProbe` copies bytes and immutable target, operation ID, absolute
deadline and supplied active tuple. `checkProbe` consumes a correlated supplied
event, copies at most 37 raw bytes and retains full received length, accepted TX,
uncertainty and qualified closure bounds. Rejected envelopes leave output
unchanged. An on-time qualified closure may be delivered later. Unknown model
values, including `0x4EEA`, remain responder evidence with unresolved mapping.
Checked exceptions, malformed/mismatched replies, no response, deadline and
transport/timing failures remain separate. A valid reply never excludes collision
or proves manufacturer, exact model, readiness, scale or motion completion.

The standalone [DiscoveryApp.h](../examples/probe_cli/DiscoveryApp.h) owns one
cooperative scan using the existing UART and bus owner. It has no second queue,
task, synchronized ingress or generic schema engine. Storage lives in the
application's PSRAM allocation; driver/capture state and stack remain internal.
The public core has no example, platform, clock or I/O dependency.

```
profile list
probe [ADDRESS]                    # ping is the same minimal query
read identity [ADDRESS]
discover [profile ess_rs | manufacturer stepperonline]
         [addresses FIRST LAST] [tuple BAUD FORMAT]...
         [query-ms N] [overall-ms N] [requests N] [results N] [identity]
discover inspect|cancel|restore|finish
```

Default scope is the bound ESS axis at the actual current host tuple, query500ms,
overall5000ms,16 admitted requests and8 findings. Direct application configuration
accepts addresses1..247, up to4 distinct adapter-supported tuples, query1..5000ms,
overall1..60000ms, requests1..256 and findings1..8. The console retains its128-byte
line and20-token limits; omit default options to fit a command. Tuple formats are
8N1/8N2/8E1/8O1 at9600/19200/38400/115200. Support means host setup capability;
alternate motor tuples remain unqualified. Only the recorded1152008N1 bench
tuple has the304us first-response exception.

Each admitted probe consumes one finding and one request. Identity refinement
consumes another request and attaches evidence to that finding. All attempts,
including exceptions and failures, count against bounds. Queries use the smaller
of their absolute deadline and the remaining overall deadline; setup time counts.
Progress and raw evidence survive cancellation, deadlines, capacity exhaustion,
recovery and result inspection. Findings never update the bound axis or its cache.
Refinement disagreement between model or active node and the probe/target is
explicit identity ambiguity. Multiple reads are not an atomic identity snapshot.

BEGIN refuses active motion, axis uncertainty/reservations, monitoring, unsettled
transport, host configuration and commissioning. During the scan ordinary work
and host/configuration mutations are excluded. Existing eligible urgent owner work
takes precedence. An admitted stop for the original known axis preempts further
queries and waits for in-flight transport plus original-tuple restoration; it
retains its original deadline and dedicated frontend/result capacity. If that
deadline expires or transport cannot be repaired, no stop transmission or physical
stop is claimed. Urgent work cannot bypass recovery or use a mismatched tuple.

Cancellation cancels unsent requests and lets physical TX/reception settle.
`cancel SCAN_OPERATION_ID`, or bare `cancel` while a scan owns the session, uses
the same cancellation policy. Faults stop with partial results; there is no
automatic recovery, retry or continuation. Healthy endings restore the exact
starting tuple, rather than startup defaults, before ordinary work resumes.
Failed restoration keeps an explicit interlock. Use `recover` for a transaction
fault, then `discover restore`; a failed host setup uses `discover restore` for
explicit repair. These actions never resume scanning. Logical target/configuration
generations are separate from transient host selection; historical evidence keeps
its original context. Explicit recovery still invalidates logical confidence.

`inspect` is non-consuming. One terminal scan remains until `finish` releases its
retention, then the next BEGIN may replace it. Generic transaction `result/release`
remain for ordinary transactions; scan evidence uses `discover inspect/finish`.
Console command IDs correlate each control separately from scan/query operation
IDs. Output remains bounded8192bytes with existing nonblocking backpressure.
Python validates the same grammar, correlation, immutable evidence and deadlines;
`discovery-check` performs one finite scan and reports an interlock without
automatically repairing it. Serial provenance is re-queried after scans.

[Prompt22 evidence](reports/ess_release_22_2026-10-05.md) records native failure
coverage, actual COM13 scans/restoration and the remaining qualification limits.
