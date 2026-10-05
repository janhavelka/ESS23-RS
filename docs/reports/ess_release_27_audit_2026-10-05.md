# Fresh prompt27 audit

Baseline: `795ea47`. This independent audit rereads the entire prompt27 and
execution contract, actual CMake/test registration, package consumers, verifier,
repository checker, generators, metadata and CI. Only27 is audited;28–30 remain
separate. No core, motor operation, framework/runtime setting or vendor byte changes.

| Requirement / failure scenario | Actual correction or audited path | Verification | Hardware / remaining proof |
| --- | --- | --- | --- |
| Required checks must actually execute | `check_inventory` validates complete native paths, interpreter/script identity, generator check modes and bounded fixture arguments | Adversarial verifier tests and actual72-check CTest listing | Host tooling only |
| Assertions and failing exits remain effective | Reject per-test Python optimization, including Windows case-insensitive environment names, and CTest success overrides | Upper/lowercase environment and exit-inversion regressions; reviewer reproduced old false PASS with actual CTest | No runtime effect |
| Build the delivered source package | Verify every ZIP member and byte before extraction; source/install/IDF consumers use the extracted ZIP | Corrupt/missing/unexpected-member fixtures plus clean archive and installed consumers | Package excludes examples, Python, vendor files and FieldCore |
| Maintained local documentation links | List prose, multiline/nested labels, real code/fence boundaries |18 checker regressions and maintained-tree scan | Limited local-link scanner; no external URL/anchor availability claim |
| Honest tool/platform scope | Document qualified GCC/Clang with GNU/LLVM `nm` | Existing local/hosted compiler matrix and symbol inspections | MSVC verifier remains unqualified |
| Required input removal | Disposable staged version generator temporarily renamed, then restored | Actual verifier exits1 before creating a build; `build/audit27/missing-check.json` | No missing checks treated as skipped |

The independent package reviewer found no excluded core sources, missing private
helpers, package identity drift or framework leakage. The original implementation
built the pre-archive stage; the audit makes the exported archive itself the
consumer input. The verifier reviewer reproduced wrong-command/check-mode and
optimized-assertion false passes. Root inspected their actual source findings,
corrected the verifier and tests, and owned all builds/Git. The checker reviewer
corrected the two disjoint checker files; the package reviewer independently
retested their final list/fence/label cases. Both reviewed the integrated changes
they did not author; no further confirmed gap remained.

FieldCore's current verifier and RS485 task/owner sources were inspected read-only.
We retain small command composition and explicit failure evidence. Its product
manifest, all-gates-after-failure policy, application types and sensor workflows
are deliberately not dependencies here. MotorControl-RS retains fail-fast command
composition with complete logs, exact test registration and no hardware CI claims.

Focused tests:10 verifier cases and18 checker cases PASS. The extra cases exercise
false PASS/failure paths rather than duplicate motor behavior. No new suite is
needed: both existing suites remain registered in the unchanged72-check inventory.

Final clean full verification results are recorded below; the pushed audit
commit supplies the subsequent hosted CI evidence. The audit commit contains
this report; root verifies its push
against the configured upstream. Version remains0.6.0 and the canonical metadata
URL remains `janhavelka/MotorControl-RS`, with the working upstream preserved.

No shipped/runtime setting changed or firmware was flashed, so a hardware retest
is NOT APPLICABLE. COM13, existing firmware/settings, evidence and flash backups
are untouched. This audit adds no new standstill, physical measurement, memory,
endurance or motor qualification claim. Previous unperformed physical cases
retain their owning dispositions.

## Final clean verification

`build/audit27/final-full/summary.json`: PASS,20 successful composed commands,
72/72 Release repository checks, source and installed consumers each3/3 PASS,
108 individual public-header translation units compiled in strict C++11 and
C++17/noRTTI, and allfour Arduino plus allfour native-IDF builds PASS. Actual
codec binaries exclude catalogue symbols and sentinel strings. The53-file ZIP
is unchanged: SHA-256
`2cf9b085f9d5ea907e21a614ef69a824fc08abdc880ad5c0c987e4a15a3173c6`.

Invocation:
`python build/audit27/final-clean-repo/scripts/verify.py --mode full --build-dir build/audit27/final-full --idf-python <absolute build/p25/idf-python/Scripts/python.exe>`.
The SDK environment is exported as documented in25/26. Host tools are
Python3.12.10, WinLibs GCC15.1.0, CMake/CTest4.0.1, Ninja1.12.1,
PlatformIO6.1.19, pioarduino55.03.311 and native IDF5.5.5. No required local
build/check was unavailable; other compilers and physical tests remain outside
this qualification.

The first full audit run also passes and remains in `build/audit27/full`; the
final fresh run includes the Windows lowercase-environment correction found
during that run. Both missing-generator attempts are retained in
`build/audit27/missing-check.json` and `final-missing-check.json`, with restored
fixtures. No passing rerun substitutes for an unresolved failure.

The isolated final source snapshot excludes unrelated application/console work
that appeared concurrently in the shared workspace. Those edits are preserved
and excluded from this audit commit. The audit changes host verification only.
