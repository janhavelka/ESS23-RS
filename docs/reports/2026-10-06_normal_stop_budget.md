# Normal-stop observation budget correction

During the user-authorized overnight campaign on 6 October 2026, a normal stop
failed even at subdivision1600/native speed60. This was a confirmed application
policy bug, separate from the unresolved high-subdivision completion discrepancy.

## Cause and correction

The shared application used the generic500ms action deadline and20 observations
at10ms intervals. With transaction time included, observations ended after about
370–380ms. That cannot cover the documented ESS position-deceleration range
0–2000ms (original function-manual physical page70). Physical page25 distinguishes
decelerated STOP from immediate EMERGENCY STOP. No guessed acceleration conversion
or different motion bit is needed.

The drive acknowledged the normal stop and subsequently stopped. Fresh samples
on the original image showed RUNNING clearing around0.55s for a500 ramp and1.98s
for a2000 ramp; zero reported speed followed around0.72s and2.16s. These are
host observation bounds from separate reads, not independent shaft measurements.
The failed action had already terminated before those observations arrived.

Normal stop now has a bounded3s application observation deadline: the maximum
2s native ramp plus two500ms transaction/settlement margins. Polling is50ms with
at most64 reads. The priority write is still immediate and sent once. RTU
response/framing budgets and fast-stop behavior are unchanged. The finite velocity
normal-stop path uses the same budget and50ms declared observation/service policy;
its production qualification gates remain unchanged. Core callers still supply
their own deadlines/options. Expiry remains uncertain and cannot imply standstill.

## Verification

| Case | Original image | Corrected image |
| --- | --- | --- |
| Native deceleration500 | observation limit after0.382104s/20polls | observed after0.486227s/8polls |
| Native deceleration2000 | observation limit after0.371204s/20polls | observed after1.895192s/32polls |

Each experiment separately checked zero reported speed and issued its explicit
fast-stop cleanup. A later success does not overwrite either failed result.
All77 registered tests passed after adding actual application-path regressions
for a2s ramp, one stop write, deadline failure and the nested velocity stop.
The delayed-service velocity test still requires a missed-service outcome; its
injected delay is now beyond the declared50ms interval. Full quick verification
also covers generated/reference checks and clean source/install consumers.

The original image and retained records were archived before flash. Corrected
Arduino3.3.11/IDF5.5.5 timer image:631328bytes, SHA256
`a0629c4cc4a726d586fb040951a97c8919f2e5c78be89679e5c209d02ef163c6`.
Build/upload: `PLATFORMIO_BUILD_DIR=build/ownership_fix/pio`,
`scripts/pio.cmd run -e bench_s3_load_timer -t upload --upload-port COM13`.
The existing FieldCore RS485 owner was inspected read-only; no FieldCore edits.

[Exact failed/passing records, commands and flash log](2026-10-06_normal_stop_budget_evidence.zip)
include a per-file hash manifest. Archive SHA256:
`5898633ae1eac9ea4c7d9f4b4e55bf3bbf1d5c243ca7f90d8098ed9ab5dba997`.

This is an intermediate overnight finding, not an endurance PASS. The motor was
confirmed stopped after the reruns; subsequent tests continue on this image.
The earlier high-subdivision/rate/distance/ramp-dependent persistent RUNNING
issue remains unresolved. A longer observation budget does not fix that issue.
