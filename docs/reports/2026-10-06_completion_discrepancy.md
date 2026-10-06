# High-subdivision completion discrepancy and interleaved host observations

## Disposition

The original user move failed to establish completion. This remains a real
failed outcome, not a USB failure or a result-storage lock. The library's refusal
to admit another move while execution remains unresolved is intentional. Fast
stop produced a separate checked stopped result and removed that interlock.
The cause inside the drive (firmware defect versus undocumented parameter
interaction/limit) remains unresolved; no guessed speed cap or false completion
rule was added. Small passing tests do not qualify all legal parameter combinations.

No firmware image or motor codec changed in this block. The installed Arduino
image remains SHA256 `30a9bc5125620c27b54e9a47abc5644ec77ba86b3241d785649d9a4ef2ac7033`.
The Python observer had an independently confirmed accounting bug, fixed below.

## Original retained evidence

[Raw records, inputs and scripts](2026-10-06_completion_discrepancy_evidence.zip)
preserve every attempt. Read-only inspection before any reset captured result219:

- Relative900 degrees, assumed host scale51200 ->128000 native increments.
  Ramps100/100, native speed2000; FC10 words `[100,100,2000,1,62464]` followed by
  FC06 `010600270001F801`. Setup and trigger had qualified checked acknowledgements.
- Deadline30s,61 polls, first and last status `01030400000004FBF0`: alarm0,
  running bit set, arrival absent. There was no lost RTU reply. At inspection,
  lifetime697/697 frames, zero transport failures/timeouts/capture faults/RX errors.
- A fresh read still reported running, raw speed2000 and changing position. The
  user's independent observation was that the shaft spun quickly and appeared
  to make the correct number of turns. This is an informal observation, not a
  measured endpoint or speed; it does not establish the meaning of every flag.
- An explicit normal stop reached its observation limit without stopped proof.
  Fast stop231 then returned raw motion0; fresh state showed raw speed0.
  Normal-stop failure is retained separately from successful fast-stop cleanup.
- Original function PDF physical16 documents relative startbit0 and bit2=0.
  Physical70 documents speed0..3000, ramps0..2000 and high/low pulse words.
  Both actual pages were reviewed. The captured request matches this encoding;
  contradictory/default and physical-ramp semantics remain as previously recorded.
- Fresh identity is raw model0x4EEA, firmware0x0029, address1; no undocumented
  model/firmware string mapping was invented.

`moveto 900 deg` means an absolute coordinate, so changing `speed` alone does
not require another move to a previously satisfied endpoint. At the later
relative admission, raw position190320, origin62345 and scale51200 were retained;
the requested absolute900-degree target would be190345. The no-op wrapper215
had already been replaced, so its exact former endpoint witness is not reconstructed.

## Controlled comparisons

All use the ordinary native position API, checked state reads, explicit fast
stop and zero-speed/non-running verification; motor power was not cycled.

| Subdivision | Native distance | Speed field | Result |
|---|---:|---:|---|
|51200|1280|60|Completion observed, then stopped|
|51200|1280|2000|Completion observed, then stopped|
|51200|128000|60|Completion observed in about2.7s|
|51200|128000|2000|Failure reproduced: running remains set after bulk movement|
|1600|4000|2000|Completion observed in about0.5s, then stopped|

128000/51200 and4000/1600 are the same assumed2.5-turn request. In the failing
repeat, feedback went from592675 to about721418 in~0.3s, then increased only
slowly (721788 at~4.8s), while status remained4 and raw speed2000. Do not treat
that raw speed as independently measured shaft RPM or equate arrival to an echo.
No broad frequency, speed or acceleration formula is inferred from these cases.
The practical tested alternatives are subdivision1600 at2000, or51200 at60.

## Host observer correction and failed attempts

`Console._consume` charged every valid interleaved state reply to every pending
command. Observing a longer move therefore exhausted its32KB response budget
using unrelated, correctly correlated replies. The fix charges each structured
reply only to its own command ID; unframed noise still charges all pending
budgets. Existing maximum line/input limits, deadline handling and unknown/stale
ID rejection remain enforced. A regression leaves one command pending across
more than32KB of other checked replies and then completes the original command.
All241 Python transport tests pass, including noise/size/correlation failures.
The current FieldCore RS485 owner source was inspected read-only; no code changed there.

The first longer comparison failed on that accounting bug; its framing-failure
cleanup stayed unknown in the original record. A separately initiated fast-stop
session confirmed cleanup. The second comparison used a5s host deadline but
sampled for longer, expiring the pending2000-rpm handle before its planned stop.
The subsequent explicit stop347 was admitted, but the old move316 terminal
arrived on the new session and was correctly rejected as an unsolicited ID.
A later explicit inspection of retained stop347 confirmed completion. Successful
fresh-state verification followed after archived test records were explicitly
reviewed/released. None of those host campaign failures is relabeled PASS.
The final1600 comparison used a15s host deadline with18 bounded samples and
completed with normal correlation and explicit cleanup. No uncertain movement
was automatically replayed.

## Final bench and verification

Subdivision was restored to the user's51200; that explicit change set the new
RAM-only zero to raw728310, host scale51200. Fresh final state: enabled,
non-running, arrival true, speed/alarm0; load/monitor/debug off, no queued bus
work, COM13 closed. User result219 remains retained. Reviewed successful test359
was explicitly released after archival. Current staged profile is
`[30,100,100,2000,1,62464]`; no trigger is pending. Host speed preference remains
2000, so use `subdivision 1600` before repeating that speed. This is a tested
alternative, not a claim that51200/2000 is now fixed.

Final lifetime1321/1321 frames, zero RTU failures/timeouts/capture faults/RX
errors. This does not erase operation-level failures. Maximum owner/capture gaps
2015/57us, internal minimum330572B, PSRAM free8177196B, owner stack headroom1540B.
No board reset or flash was needed. Hosted GCC/Clang/Arduino/IDF checks are
required on the pushed commit; local transport tests and repository checks run
before push. A vendor explanation or further reviewed drive-specific experiment
is needed to resolve the high-subdivision completion/normal-stop discrepancy.
