# Canonical console commands and fast stop - 2026-10-05

The console now uses `stop fast` for the existing ESS emergency stop without
the configured deceleration ramp. Core stop preparation, reserved priority,
framing, observation and uncertainty handling are unchanged.

Removed executable aliases: `?`, `ver`, `ping`, `reset`, `health check`,
`help advanced`, and all `profile ess_rs OPERATION` wrappers. `profile list`
remains the unique local profile inventory. Help is one complete grouped menu.
The regular and advanced motion commands have different preparation options;
all continue through the same public API and owner.

Python tooling and maintained documentation use canonical commands. Console
protocol 3 rejects older protocol clients rather than silently changing their
stop policy. Historical reports preserve their original commands and evidence.
The core library ABI and vendor `DIRECT` stop enum have not changed.

FieldCore's current `src/rs485/Rs485Task.cpp` was inspected read-only. Changes
remain within the standalone console, its callers, tests and documentation.

## Verification and board disposition

Baseline: `14c54da74007fd8fe568c627ff1063eaf36d27fd`. The final candidate is
the commit containing this report; the evidence includes changed-source hashes.
No core library code or drive setup changed. The package remains version 0.6.0;
the standalone console protocol is 3.

PASS: all 20 stages of the full release verifier, 77 native checks, source and
installed C++ consumers, all four Arduino and four ESP-IDF firmware builds:

```text
python scripts/verify.py --mode full --build-dir build/fast_cli/verify --idf-path C:/pio/packages/framework-espidf --idf-python build/p25/idf-python/Scripts/python.exe
ctest --test-dir build/fast_cli/verify/native --output-on-failure
scripts\pio.cmd run -e bench_s3_load_timer -t upload --upload-port COM13
python build/fast_cli/check_console.py build/fast_cli/hil-final
```

The final native rerun includes protocol-1/2 rejection before motor commands,
removed alias rejection, fast-stop policy mapping, retained result/priority
pressure and Arduino/IDF plus Python command parity. Initial native failures
were obsolete alias assertions and fake-transport argument offsets; these were
migrated to canonical syntax, with redundant alias cases replaced by rejection
checks. Original failed logs remain separate from final passing logs.

COM13 upload PASS, flash data hash verified. Exact timer image: 619216 bytes,
SHA-256 `bfa1d01cdfafcea2d72b0ffedcfeaac5dec62eff5d3f625275866f66e1339ea1`.
Checked model word `0x4EEA`, firmware `0x0029`, node 1, 115200 8N1;
subdivision 1000, raw positioning profile `[30,100,100,60,0,100]` retained.

The finite final regression passed in 2.86 seconds: unique grouped help;
help for both stop policies; nine removed invocations rejected with unchanged
RS485 counters; actual settings including subdivision; identity/config/state;
one stopped-state fast stop; fresh stopped feedback; three model probes.
It used 23 checked frames / 236 RX bytes, zero failures, timeouts, RX errors or
capture faults. The stop result was ACKNOWLEDGED and completion OBSERVED,
with FC06 reply `01060027020038A1`, no unknown execution and 25.116 ms from
admission to observed terminal. This is software/drive evidence at standstill,
not measured dynamic shaft stopping latency.

The first board run also completed its stop, but the test script then raised
`KeyError: uncertain`: that field belongs to move results. Inspection of its
retained action showed acknowledged execution, observed completion and
`write_evidence.execution_unknown=false`. The runner was corrected to check
those actual action fields and deliberately rerun at standstill. No firmware
fix, relaxed guard or automatic uncertain-write replay was needed. The failed
run and its raw evidence are retained separately.

Final drive state: enabled, not running, raw speed 0, alarm 0, position 10465.
No move, parameter, persistence or communication-setting write was sent.
Owner pending/retained/reserved/output counts were all zero, DE released,
no recovery required; load, monitor and debug off. Maximum recorded active
owner gap 253 us, capture gap 55 us; stack free 2612 bytes, internal free
336656 bytes (minimum 331496), PSRAM free 8177196 bytes. These short checks
add no electrical, shaft, dynamic-stop or endurance qualification.

[Upload, raw board traffic, runner, native/build logs and firmware image](2026-10-05_fast_stop_console_evidence.zip).
Evidence archive SHA-256: `247fcb041c18e84227244280383367e32a94e40c26851077be93d56e9eb179fd`.
Core package SHA-256 remains
`2ad1e52eed8b2f66418fd13e21ed722b12571e471161b7e766ee0ccd4a8b3af8`.

Commit synchronization and exact-commit hosted checks are reported at handoff;
no tag or release is published.
