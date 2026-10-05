# Prompt30 fresh independent audit

Audited baseline: `7cf31b9a9ff8f9edefeda58327f2a7c83f97ec00`.
Its [four required CI jobs](https://github.com/janhavelka/ESS23-RS/actions/runs/37329271156)
passed. The core candidate remains source commit
`fe3f795877aca3f23c4e97a2124d6d970394b6b5`, version0.6.0; this audit changes
documentation only. No FieldCore edit, runtime image change, tag or publication.

## Confirmed findings and fixes

| Finding | Actual evidence | Correction |
| --- | --- | --- |
| Prompt-set row30 still describes verification as pending | Actual index row contradicted pinned candidate artifacts/report and completed CI | Record delivered/verified partial disposition and link the real handoff/evidence |
| FieldCore result lifetime omitted explicit reclamation | `Rs485RuntimeIngress.h:294` can reclaim unfinished work or discard a terminal completed at or after its caller-supplied cutoff; `completeDevice` discards reclaimed work's late completion. Sensor result grace is1000ms | Correct the reservation description; require separate bounded motor retention, non-consuming inspection, reserved completion/recovery storage and explicit release, preserving sensor behavior |
| Recovery handoff did not explicitly explain identical late RTU replies | RTU has no wire request ID; host correlation is not response attribution | State that IDs/generations/finite idle guards cannot remove this ambiguity, with no replay or invented certainty |

The root checked each finding against actual code. New independent package/
release-claims and FieldCore source reviewers read the original prompt, integrated
diff, current sources and raw evidence. They found no core/runtime defect.
FieldCore remains clean/read-only at
`d19758865852f4e215ea05c6572d060edc4f0c66`; its existing RTU/ASCII owner and
sensor policies are preserved in the proposed regression plan.

## Verification and remaining scope

Both shipped ZIP hashes match [the candidate pin](ess_release_30_candidate.json).
All55 core members equal the pinned Git blobs; all41 original evidence entries
match their manifest, with no unlisted members. Original software/export and
bench failures remain retained separately from their corrections. The original
Arduino aggregate in29 remains FAIL; its separate prerequisite correction stays
PASS. Nine native-family gaps,18 guarded pairs and physical/endurance cases stay
in the [candidate gate table](../release_candidate.md).

Fresh full verifier PASS:20 required command groups,74 registered Release CTest
checks,28 isolated headers across strict C++11/C++17 source/install consumers,
codec-only linking without catalogue strings and eight embedded builds. Source
ZIP reproduces SHA-256
`89a3475fa8ab7fb6a7a8351f8591ab3339d4f4139e21f857ca8330829752b120`.
Independently staged README/getting-started consumers compile/link/run; current
FieldCore contracts plus installed motor headers compile on Xtensa GNU C++17/no
RTTI/no exceptions, and the host consumer links/runs. This is header/source
coexistence, not a full FieldCore firmware build or motor integration.

Commands use a new clean canonical staging tree from the audited baseline with
only the audit-owned documentation corrections copied in:

```powershell
git -c core.autocrlf=false archive --format=tar -o build/p30/audit-source.tar 7cf31b9a9ff8f9edefeda58327f2a7c83f97ec00
# Extract into empty build/p30-audit/source; apply the documented audit doc diff.
# Export the pinned SDK environment from docs/esp_idf_probe.md.
python build/p30-audit/source/scripts/verify.py --mode full --build-dir build/p30-audit/full --idf-python build/p25/idf-python/Scripts/python.exe
python scripts/bench_scenarios.py --port COM13 --scenario quick --count 10 --out build/bench/p30-audit-readonly-01
python build/p30-audit/final_state.py
```

The helper reuses `bench_session.run_recorded/checked` and
`bench_motion.profile_snapshot`; it sends reads and cached diagnostics only.
Quick/read-only PASS3.5s plus final inspection PASS1.234s;39 new checked frames
(79 to118), zero transport/RX/capture failures or timeouts. No reset, motor write,
upload or automatic recovery occurred. Final profile `[30,100,100,60,0,250]`,
control raw `[3,4000,5600,100,40,100,40,200]`, position4881/alarm0/non-running/
speed0, node1/1152008N1 (reported115211). DE released; pending/retained/reserved0,
monitor/debug/load off. CPU sampling unavailable in the short ending inspection
(`cpu_valid:false`), rather than an invented load measurement.

Ending internal free/minimum336656/331496 bytes, PSRAM free/minimum8177196 bytes,
owner stack headroom2724 and worker3268 bytes; lifetime owner-gap maximum1675us,
capture maximum56us under the85us guard. Console input drops/output blocks0;
trace overwrites2242 remain bounded diagnostic-history loss. These are observed
short-run/lifetime counters, not endurance, saturation or physical stop proof.
Console version0.6.0 cannot attest an image hash (`firmware_artifact:null`);
the previously recorded Arduino timer image remains unchanged, with no flash.

[Audit evidence ZIP](ess_release_30_audit_evidence.zip):130645 bytes, SHA-256
`ae121d2608df40f93244e50c1b0a8f4d6d3b342b3b1f6ef1f99f5302fe23aa75`.
Its34-entry per-file hash manifest covers raw/structured bench checks, exact
consumer sources/commands, all20 verifier logs/summary and baseline CI. The
original candidate/evidence archives remain unchanged. Independent reviewers rechecked the final index and
reclamation correction, including equality at the cutoff. Root owns final
commit/push and exact-commit required CI closure; final delivery names that
commit and run without a self-referential commit hash in this report.
