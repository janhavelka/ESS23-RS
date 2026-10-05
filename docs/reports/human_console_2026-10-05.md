# Human-readable console — 2026-10-05

Ordinary commands now produce human text on the same shared Arduino/IDF
application path. `help`/`?` groups the callable commands, `help COMMAND` shows
syntax and examples, and failures give usage hints. A bounded startup greeting
points to both human help and the existing `@ID` JSONL interface.

## Design and independent review

The existing command inventory supplies plain-language descriptions. The
presentation renderer uses the existing canonical JSON evidence, fixed borrowed
spans and the existing output buffers; it adds no motor/protocol path, heap
allocation, blocking serial print or ANSI terminal requirement. Raw traffic is
compact; decoded traffic includes checked function/register interpretation.

Each outstanding operation and deferred stop retains its admitted reply format.
Result inspection follows the inspection request's format. Only successful debug
mode selection changes traffic presentation. Human truncation retains outcome,
execution and completion summaries, explicitly marks omitted detail and directs
the reader to retained results rather than repeating writes. JSON schemas remain
unchanged; the help inventory additionally lists `?`.

Separate agents implemented console routing/help, bounded rendering and real
console tests. Independent review checked async correlation, blocked output,
reserved stops, diagnostic pressure and truncation. Root inspected the actual
diff, callers and output. Sibling references were SHZK-PT's command inventory,
INA228's grouped CLI styling, RV3032/TCA9548A/VTN4xx/VibWire help conventions and
FieldCore-node's cached RS485 status labels. FieldCore remains read-only; no
framework types, scheduler or device protocols were imported.

The concurrent prompt27 auditor committed `e838f4e` while this work was in
progress. Its verification implementation was left intact; this block starts
from that landed commit and uses isolated build directories. Its later
`7f8ea15` delivery-policy commit is also preserved; exact-pushed-commit hosted CI
is checked before handoff.

## Verification

- PASS: 74 registered native/Python tests, including new bounded renderer and real
  console human/machine/stop-pressure tests.
- PASS: quick verifier: source and installed core packages, isolated C++11/C++17 public
  headers, exact archive bytes and codec-only symbol checks.
- PASS: three affected Arduino firmware builds, native IDF S3 application and portable
  S2 owner/console consumer. Both application startup/USB fixtures run natively.
- PASS on the tested compact-output image: 28 human console commands and ten
  ordinary Python probes; 44 final-image frames, zero transport failures. Probe
  latency was 5482?5670 us. The last cached stack watermark was 2724 bytes;
  the exact final measurements and firmware SHA-256 are in the companion JSON.

The IDF startup test initially expected the first post-boot record to be JSON;
it now checks the welcome text and a separately delimited correlated JSON reply.
Two temporary hardware-harness assertions used incorrect expected labels
(`frame` instead of `success`, `complete` instead of `succeeded`). Retained raw
output showed successful checked replies; only those assertions were corrected.
No firmware failure, hidden retry or uncertain write replay was involved. Live
inspection also prompted compacting repeated empty traffic fields. The compact-output
image repeats the same human and machine scenarios.

The first S2 build invocation omitted its mandatory staged-core path; the
corrected invocation explicitly uses the verified core export. A documentation
link check ran before this report existed and was repeated after delivery files
were complete. These intermediate failures remain in the evidence archive.

## Scope and remaining limits

The board is left on the final tested ordinary Arduino timer image. Two upload
attempts were refused before flashing while COM13 was busy; after the user
released the port, upload and the same human/machine checks passed. The final
wording also directs formatting failures to retained result inspection. Only motor reads were
performed; raw position remains 3644, reported speed zero and alarm zero. Debug,
monitor and fixture load end off, with DE released and no required recovery.
No motion, parameter write, persistence or host tuple change was needed.

Native IDF hardware execution and electrical/shaft measurements were not repeated
for this presentation change. Existing motion, transport and endurance limits
remain unchanged. Human input retains the strict line grammar; configure a
terminal's local line editing if backspace editing is needed. Full diagnostic
arrays can still be long or explicitly truncated; `@ID` retains bounded JSONL.

See the [console guide](../console.md), [machine-readable record](human_console_2026-10-05.json)
and [raw evidence archive](human_console_2026-10-05_evidence.zip).
