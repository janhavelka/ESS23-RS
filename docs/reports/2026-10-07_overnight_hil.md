# Overnight finite motion and live USB diagnosis, 6ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Å“7 October 2026

## Disposition

The endurance campaign **FAILED**, despite successful individual motor cases.
New motion stopped at 08:35 CEST, before the requested 09:00 cutoff, when the USB
console stopped delivering replies. It was not restarted automatically.
The two sessions completed 9,325 matrix cases: 7,460 completed finite moves and
1,865 deliberate stop interruptions. Another 2,799 simple absolute commands
completed, including 933 already-satisfied targets (no new movement).
There were no recorded drive alarms or failed RS485 transactions during these
endurance sessions. These counts do not include the deliberately failing pilot
experiments and do not turn either failed aggregate into PASS.

## Scope and exact image

COM13, ESS23-RS20, address 1, host 115200 8N1 (adapter actual 115211), free shaft,
no external inputs/loads. Arduino 3.3.11 / IDF 5.5.5, GPTimer capture, image
`a0629c4cc4a726d586fb040951a97c8919f2e5c78be89679e5c209d02ef163c6`
(631,328 bytes), source `62f1fe1ad04b94fea1407efa86e49390cbce22f5`.
No save/factory restore, current increase, feedback disable, tuning sweep,
unbounded velocity, or moving link-loss experiment was performed.
Independent shaft/electrical measurements remain unavailable.

The archived experiment uses one port owner, finite motion, explicit checked
stops, fresh state, incremental bounded evidence, and no uncertain write replay.
Its 60-case plan varies subdivision 400/1600/6400 through 3000rpm,
12800 through 1200rpm,25600 through 600rpm and51200 through 300rpm, quarter/full
turns and interrupted 2.5turn moves. Native ramps rotate 100/500/2000.
Each cycle includes read-only competing work, 5000us owner delays, bounded console
load, off/raw/decoded observation, health and typed settings reads. Every case
checks transport counters, heap and stack. These are tested combinations, not
all possible motor settings or a universal operating envelope.

## Confirmed defects and limits

1. **Normal-stop observation budget:** the old 500ms/20poll budget could expire
   before a documented 500ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Å“2000ms deceleration finished. Shared application
   normal stop now uses 3s/64polls/50ms; fast-stop and wire timeouts are unchanged.
   [Cause, original failure, corrected hardware/native tests and CI](2026-10-06_normal_stop_budget.md).
2. **Windows heartbeat publication:** a monitor could hold the JSON heartbeat
   open while the writer replaced it, causing WinError 5. A disposable reader
   reproduced it. The experiment now retries only publication of already-written
   evidence for at most 1s; a permanent lock still fails. No motor write is retried.
   The first session remains FAIL with confirmed stop/restoration.
3. **High-subdivision completion:** native running stayed asserted for particular
   high-speed/high-subdivision profiles after apparent target travel. Pilot
  51200/900rpm quarter-turn failed with 100/100 ramps; 500ms acceleration allowed
   some shorter cases but did not qualify longer moves. The105-case grid failed
   at 25600/2000rpm/64000 increments with 500/500 ramps. The exact drive mechanism
   remains unresolved; increasing a timeout or interpreting arrival alone as
   completion would hide it. No guessed correction was applied.
4. **Very short fast moves:** a 10-increment move at400 subdivision/3000rpm changed
   feedback by 10 but no RUNNING sample was captured. The operation honestly ended
   uncertain, then explicit stop succeeded. Fresh arrival alone does not satisfy
   the current completion contract. This pilot was retained and excluded from
   endurance; it is not evidence of a motor fault or a fixed completion API.
5. **USB transmit stall:** diagnosed live below. No memory leak, drive alarm,
   CPU deadlock or reboot explains the captured failure state.

## Endurance records

| Session | Local interval | Matrix cases | Result |
| --- | --- | ---: | --- |
| A | 6 Oct 21:14ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Å“7 Oct 00:21 | 2,568 | FAIL: Windows heartbeat replacement; cleanup confirmed |
| B | 7 Oct 00:22ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Å“08:35 | 6,757 | FAIL: unanswered cached stats; automatic cleanup unavailable |

Session B maintained internal free 336,208 bytes, minimum 331,048; PSRAM free
8,177,196 bytes; owner stack low-water 1540 and load-worker 3268 bytes.
Maximum measured owner gap 8037us under injected load; capture gap 67us below
its 85us limit. CPU1 busy samples 9ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Å“37%, CPU0 0ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Å“1%. Diagnostic load lines could
be deliberately dropped (up to 117 per window); protocol/correlated output drops
were zero before the fault. Observed stop results ranged 25,048ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Å“1,906,167us;
these are host evidence times, not physical deceleration measurements.

## Live USB evidence and recovery

The last unanswered request was cached `stats`, command 313729, at
2026-10-07T06:35:28.271Z. The preceding normal stop 110669 was acknowledged and
observed; fresh state showed speed 0, running false, alarm 0. Owner pending,
reserved and retained counts were zero. No new motion was issued afterward.

At approximately 09:29 CEST, USB JTAG was attached specifically to board serial
`3C:0F:02:CD:6B:98` without resetting it. Both CPU backtraces were idle, while
`loopTask` was in the ordinary application delay. Successive RTC witnesses
showed continuing loop progress and incoming commands were consumed.
The HWCDC 1024-byte TX ring was completely full, starting with the original
stats 313729 reply. The USB FIFO was writable and empty, IN_EMPTY interrupt
was enabled, but its raw/status bit was zero. Connection status was true.

A single debugger write of 1 to `USB_SERIAL_JTAG.ep1_conf.val` submitted an empty
USB packet. The queued original replies immediately drained, including stats
with 368,603 started/368,603 frames, zero failures/timeouts/capture faults/RX
errors. This establishes a lost TX wakeup in the HWCDC transmit path and
excludes a whole-application crash at inspection. It does **not** identify the
specific preceding race/event that lost that wakeup. Pinned upstream HWCDC
already includes its interrupt-lock/recheck changes; blaming their absence
would be incorrect. No periodic debugger kick or reset is a production fix.

The raw diagnostic serial listener used pyserial's default line controls; a
subsequent USB reset (reason 11) occurred after queued replies had been recovered.
Its RTC record still showed normal idle progress and an empty application output
queue. That diagnostic reset is separate from the overnight fault. The later
fresh session therefore no longer possessed the original RAM profile backup.
The first postrun restore attempt was refused before a write because stop had
invalidated the observation context; it remains a failed cleanup attempt.
A separate explicit cleanup confirmed standstill and restored subdivision 51200,
with load/debug off. Its retained profile was500/500/60/32000, not the original
pre-night 100/100/2000/128000. No original-profile restoration is claimed.

## Evidence

All archives contain exact source/inputs, raw and structured records and SHA-256
manifests; failed attempts are retained alongside passes.

- [Pilot investigations](2026-10-07_overnight_investigations.zip),3,426,217 bytes,
  SHA-256 `e733b571b31b48ec14022cff44c3a1fb8b0343521700226eff5e6865126a3da1`.
- [Session A](2026-10-07_overnight_endurance_a.zip),34,596,138 bytes,
  SHA-256 `71519141e4b37218fc4c9e59badec4d62d21b111bcfc39e66428f8fc6089b203`.
- [Session B](2026-10-07_overnight_endurance_b.zip),91,195,344 bytes,
  SHA-256 `f20d44db4e2ebad937fa06dc95563751efb5a011421e4775c8bc19c81d400da1`.

## Shared-driver correction

Both Arduino and native IDF now compile `examples/probe_cli/Esp32UsbConsole.cpp`,
extracted from the existing IDF adapter. Arduino probe builds disable HWCDC
startup; the units-preview example retains its own existing startup. The shared
console uses one SDK owner, zero-tick 64-byte writes and existing bounded
application output. There is no periodic FIFO kick, auto-reset, reconnect or
motor replay. Current FieldCore SerialCli uses the same SDK transport; it was
inspected read-only and remains unchanged.

Source review found a concrete matching HWCDC race: it checks FIFO writability
**before** clearing IN_EMPTY. A host pickup after a negative check but before
that clear erases the newly arrived event. IDF 5.5.5 clears the old event **before**
checking; a later pickup survives for the next ISR. A deterministic experiment
compiled the unchanged TX ISR excerpts from both pinned SDK sources against
minimal peripheral/RTOS fakes. Injecting that interleaving reproduces the exact
HWCDC stuck state; IDF retains the event and drains the queued 64 bytes. This
proves the source defect and correction for that interleaving. The initiating
interleaving of the actual overnight occurrence was not traced.

The correction removes about 30 KB from the Arduino image rather than adding a
second USB state machine. Native tests now run both startup wrappers against
the shared driver adapter, including install failure, pending boot bytes,
exclusive ownership, zero-tick backpressure, short-write injection and continued
owner service. A compile-negative check rejects competing Arduino CDC startup.

## Final software and hardware verification

- Repository quick verifier: all 77 registered native/Python checks, generated
  consistency, strict source/install consumers and package checks PASS.
- Arduino timer firmware and native IDF 5.5.5 firmware builds PASS. An initial
  IDF build invocation lacked its documented IDF_TOOLS_PATH; using the existing
  constrained SDK environment corrected this host setup failure.
- Deterministic pinned TX ISR experiment: original HWCDC STALL reproduced,
  IDF event ordering PASS. Separate from physical evidence.
- New Arduino image: 601,568 bytes, SHA-256
  `6b9d6544787c7d2170547cac1ec1726646b188c8a79ea446c955e718fc5555be`.
  COM13 smoke PASS: 10,000 cached correlated queries, 10 delayed reads,
  90 deg forward/return, repeat-zero no-op, fast stop and exact cycle-profile
  restoration. 115.062 s, 48 checked motor frames, zero transport failures.
  Final internal free 347,368 bytes/min 342,212; PSRAM 8,177,196; owner stack 2612.
- Pending/reserved/retained/output queues zero, DE low, no recovery required.
  Subdivision 51200. Readback profile `[30,500,500,60,0,32000]`; host preferences
  60 rpm/100ms/100ms apply only to the next requested move. No motor save.
  Current stationary feedback is the new RAM session zero; no NVS origin.

The failed overnight result remains FAIL. A new multi-hour run on this image,
independent shaft/electrical measurement, high-subdivision drive behavior and
very short completion remain open. No claim of exhaustive testing is made.

A separate close/reopen plus 20 decoded probes under 2000us worker load, 5000us
owner delay and 128-byte console pressure passed. Final read-only state after
restoring load/debug off again confirmed no running, speed 0 and alarm 0; COM13
is closed and no test process or autonomous motor work remains.

[Live diagnosis, extracted ISR experiment, build logs and final regression](2026-10-07_usb_diagnosis_and_regression.zip),
554,205 bytes, SHA-256
`26bd0516fb55e97d720e7505faa4f421e7aac8f331e966422da0764cc2f59ea2`.
The extracted Espressif ISR snippets retain Apache-2.0 provenance and license.
Their minimal fakes inject a particular interleaving; they are not a complete
USB hardware emulator. Root and independent reviewer both reproduced the result.
