# Debugging normal RS485 operation

Debug mode uses the ordinary preparations, sequences, bus owner and checked
parsers. Turn it on with `debug raw` or `debug decoded`, run the normal command
you want to investigate, inspect its retained result, then use `debug off`.
There is one execution path and one UART owner. Changing the display mode sends
no motor command and changes no admission rule, timeout, recovery or retry policy.

| Need | Command | Effect |
| --- | --- | --- |
| Current debug/owner/capture/memory overview | `debug` | Cached host snapshot; no bus traffic |
| Copied bytes and software events | `debug raw` | Enable bounded diagnostic display |
| Checked function/register interpretation | `debug decoded` | Decode the same copied traffic |
| Stop diagnostic capture/display | `debug off` | Ordinary operations continue |
| Exact retained request/reply/outcome | `result operation_id` | Non-consuming inspection |
| Current owner, queue and output state | `drv` | Cached host snapshot |
| Transport failures and capture gaps | `stats` | Cached host counters |
| Capture-section time, scheduler estimates, service gaps | `load` | Cached HIL fixture measurements, if built |
| Memory and stack watermarks | `memory` | Host measurements |
| Bounded injected CPU/service/output load | `load work_us delay_us bytes` | Explicit host fixture change, only while settled |
| Long checked read for capture diagnostics | `capture-read [address]` | Fixed non-consuming FC03, 16 reviewed words |
| Position parameters and restoration | `motion-profile read|inspect|restore` | Ordinary typed read / cached inspect / explicit write and readback |

The low-level views preserve raw bytes, CRC/parser errors, TX completion,
direction, RX bounds and execution uncertainty. Device actions and parameter
changes continue to use their normal typed commands. Debugging does not introduce
unchecked register writes or a second motion implementation.

The console command previously named `sniff` is now `debug`; update host scripts.
The installed library API remains `TrafficCapture` plus the ESS traffic decoder.

`MotorControlRS::TrafficCapture` is an installed, framework-independent observer.
It copies traffic supplied by the application transport into caller-owned fixed
storage. It never reads a UART, removes bytes from a receive queue, modifies a
transaction buffer, sends a request or calls a logger. Normal operations and
their parsers continue to own the original bytes.

```cpp
#include <MotorControlRS/Traffic.h>
#include <MotorControlRS/profiles/ess_rs/Traffic.h>

MotorControlRS::TrafficRecord records[16];
MotorControlRS::TrafficCapture capture(records, 16);
capture.setEnabled(true);
// The transport owner supplies begin/transmitted/received/event observations.
// The standalone Runner can attach this capture directly with setTrafficCapture.
uint64_t cursor = 0;
MotorControlRS::TrafficRecord copy;
if (capture.copyAfter(cursor, copy)) {
    // Format raw bytes, or call ESS_RS::decodeTraffic with the matching TX copy.
}
```

Readers have independent cursors. `copyAfter` does not remove even the diagnostic
record, so another reader can inspect the same traffic. Overflow overwrites old
copies and increments a counter; it does not delay the bus. Storage is exclusive
to the capture and must not overlap live transport buffers. Calls are serialized
by its owning application; the core adds no threads, locks, clocks or allocation.

Records distinguish accepted TX bytes, RX bytes, physical-TX-completion reports,
direction changes and transaction end/reason. TX enqueue is not a wire timestamp.
RX intervals retain the adapter's uncertainty. Partial, rejected and discarded
traffic stays visible. These are software observations of the local transport,
not measurements of voltage, termination or an independent recording of every
electrical event on a bus.

`ESS_RS::decodeTraffic` validates copied FC03/FC06/FC10 requests and responses
using the checked profile codecs. RX translation requires the exact matching
request, rather than guessing the register address from response bytes. Output
is unchanged on errors except a checked Modbus exception, which publishes its
exception code explicitly. Unknown, incomplete, uncorrelated and invalid frames
remain available as raw traffic. Translation does not turn a write echo into
motion-completion evidence. Basic codec use remains independent of catalogue
strings; the console uses register names for display.

The regular console supports:

- `debug`: show mode, diagnostic counters and the ordinary owner/capture/memory snapshot.
- `debug raw`: display copied bytes with direction, correlation and software timing.
- `debug decoded`: also display checked function, register names and values.
- `debug off`: stop observation/display without changing normal operations.

These commands send no motor traffic. JSONL `type:"traffic"` events can appear
between ordinary command replies and operation results; they have their own
sequence/transaction identity. The Python harness validates and logs them
without consuming a command result. `bench_motion.py --debug raw|decoded`
exercises the same regular API with observation enabled.

The example retains16 records in application storage and formats at most one
copy per service loop. It reserves output room for ordinary replies. If console
output is blocked it drops diagnostic display copies and reports the loss,
leaving bus work and operation results intact. Mode changes skip old display
copies without clearing the underlying records for other readers. Counters
separate observed copies, emitted lines, display drops, missed overwritten
records and intentionally skipped mode-change records. `observed = emitted +
dropped`; missed and skipped copies are separate. Historical ring overwrites
are not automatically display losses if that reader already saw the record.

Existing `drv`, `load`, `memory` and retained transaction fields supply HIL
software measurements: service/capture gaps, TX completion, RX timing bounds,
faults, CPU-section time, buffer loss and memory watermarks. Wiring is supplied
by the user; the firmware checks its configured timing/echo contract. Electrical
qualification is not an admission switch or an alternate execution mode.

For a finite read-only campaign on the existing bench:

```powershell
python scripts/bench_probe.py --port COM13 --debug decoded --log build/bench/debug_reads.jsonl stress --count 10 --interval 0.05
```

The harness enables observation on the same connection as the ordinary campaign,
validates diagnostic events separately from command results, and restores the
previous mode afterward. `bench_motion.py --debug raw|decoded` supplies the same
observation for its explicitly selected bounded motion phases. See the
[short motion procedure](functional_bench.md). Debug display is best effort;
retained operation results remain the authority for transaction outcomes.
