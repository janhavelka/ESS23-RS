# Passive RS485 traffic observation

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

- `sniff`: show mode, retained/copy/display/drop counters.
- `sniff raw`: display copied bytes with direction, correlation and software timing.
- `sniff decoded`: also display checked function, register names and values.
- `sniff off`: stop observation/display without changing normal operations.

These commands send no motor traffic. JSONL `type:"traffic"` events can appear
between ordinary command replies and operation results; they have their own
sequence/transaction identity. The Python harness validates and logs them
without consuming a command result. `bench_motion.py --sniff raw|decoded`
exercises the same regular API with observation enabled.

The example retains16 records in application storage and formats at most one
copy per service loop. It reserves output room for ordinary replies. If console
output is blocked it drops diagnostic display copies and reports the loss,
leaving bus work and operation results intact. Mode changes skip old display
copies without clearing the underlying records for other readers.

Existing `drv`, `load`, `memory` and retained transaction fields supply HIL
software measurements: service/capture gaps, TX completion, RX timing bounds,
faults, CPU-section time, buffer loss and memory watermarks. Wiring is supplied
by the user; the firmware checks its configured timing/echo contract. Electrical
qualification is not an admission switch or an alternate execution mode.
