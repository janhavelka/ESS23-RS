# One complete console help menu - 2026-10-05

Follow-up to `fbd225692755e390e0fc7ab65e7138509d096daa`.

Removed the separately maintained simple help and abbreviated `help move` text.
One existing command catalogue now generates the complete grouped human menu.
`help`, `?` and compatibility `help advanced` produce identical output. Commands
are listed once: `?`, `ver`, `ping` and `reset` remain accepted spellings, while
the inventory uses `help`, `version`, `probe` and `stats [reset]`. JSON help uses
the same canonical-entry selection; existing command execution and result schemas
are unchanged. Alias detail requests resolve to the canonical syntax. Repeated
`profile ess_rs ...` alternatives are removed from individual syntax descriptions;
the profile entry describes that compatibility route once.

Normal help now includes every available command, including settings, motion,
units, drive settings, results and diagnostics. Detailed `help move` exposes the
real advanced grammar directly. There is no hidden second menu.

`stop normal` uses configured deceleration; `stop direct` is the ESS emergency
command without that ramp. Help explains reserved priority after an in-flight
transaction settles and its dependence on working RS485 communication. It does
not equate a serial command with a hardwired emergency-stop circuit. Stop
preparation, transmission, observation and uncertainty behavior are unchanged.
Existing source evidence is in `docs/ess_actions.md`; no new motor semantics or
command alias was invented.

Relevant FieldCore owner source was inspected read-only. All changes stay in
example presentation/tests/docs; no core, owner, platform or FieldCore changes.

## Verification

PASS: full verifier, 77 native checks, source/install C++ consumers and all eight
Arduino/ESP-IDF firmware builds:

```text
python scripts/verify.py --mode full --build-dir build/unified_help/verify-final --idf-path C:/pio/packages/framework-espidf --idf-python build/p25/idf-python/Scripts/python.exe
```

Existing human-console tests now verify identical menus, unique canonical rows,
no standalone compatibility aliases, explicit emergency-stop policy/priority,
canonical alias detail and zero motor activity from help. Existing priority-stop,
blocked-console/output-pressure and native-alias execution tests still pass.
The first full run found one obsolete help assertion expecting the formerly
repeated `profile ess_rs caps` syntax; it now checks the single native-route
syntax. That failed log remains separate from the passing final verifier.

Built Arduino timer image: `build/unified_help/pio/bench_s3_load_timer/firmware.bin`,
621584 bytes, SHA-256
`adee86da8022ea5aba250967df9d99043a1a6c4428530b5a14fad87539276d12`.

Hardware regression **NOT RUN**: upload could not open COM13 (`PermissionError`,
access denied / port busy). No upload or motor command took place. The previously
recorded settings-follow-up image and stopped bench state are the last established
hardware evidence; this new image is not claimed to be installed. The user was
asked to release the serial monitor. The prepared finite runner checks matching
menus, unique entries, stop/move/driver help, subdivision display, identity,
stopped state and three probes; it sends zero motor writes.

[Retained verification, failed upload and prepared runner](2026-10-05_unified_console_help_evidence.zip).
Archive SHA-256:
`1779210202c3f85ac109accf552c677f1831f7f7f1d039f2fc8cb10167a9596c`.
Core package remains unchanged at SHA-256
`2ad1e52eed8b2f66418fd13e21ed722b12571e471161b7e766ee0ccd4a8b3af8`.

The candidate is the commit containing this report; no release is published.
Final response records commit synchronization and hosted CI. Physical stop
semantics and their previous qualification limits are unchanged by this UI edit.
