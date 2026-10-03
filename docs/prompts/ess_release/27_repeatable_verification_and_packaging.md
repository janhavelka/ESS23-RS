# 27 — Repeatable verification, CI and clean packaging

Execute only this prompt under [the execution contract](execution_contract.md).
Read [the sequence index](README.md); all prerequisite dispositions must be current.

Prerequisite: actual core/example/test inventory from 01–26. This step does not require unperformed physical cases to become PASS.

## Read and reuse

Inspect `CMakeLists.txt`, `library.json`, `platformio.ini`, generators, all native/Python suites and existing scripts. FieldCore's verifier is a design reference only; copy no product-specific commands or dependencies.

## Implement

- Add one small documented verification entry point with clear quick/full modes or equivalent bounded selection. Compose existing commands and fail on missing required tools/checks; do not hide skipped Python/generated tests.
- Cover native Release tests, public-header isolation, generated register/version checks, offline reference consistency, clean source/install consumers and affected Arduino/native-IDF builds.
- Add CI using the same entry point and a justified compiler/platform matrix. Hardware jobs require real named runners and separate evidence; ordinary hosted CI must not pretend to test COM13.
- Verify exported core source packages build without examples, Python, vendor downloads or FieldCore. Verify basic codec linking does not pull descriptive catalogue strings accidentally.
- Preserve framework-neutral C++ requirements and library identity. Follow the repository rename guide for the canonical URL; do not restore a historical URL from an old report. Keep version generation deterministic, metadata/export/install rules consistent and vendor reference licensing separate.
- Compile a strict C++11 core consumer and a C++17 consumer with RTTI disabled, matching the inspected FieldCore build constraint. Recheck current FieldCore flags read-only; do not copy C++17-only declarations or firmware types into the C++11 public API.
- Add documentation/reference-link checks appropriate to this repository rather than assuming FieldCore's Doxygen tooling exists. Do not add a large build orchestration framework.

## Verify

Run the full verifier in a clean repository/staging tree. Separately build and run clean source-package and installed `find_package` consumers without repository-private helpers. Exercise a missing-required-check failure in a disposable repository fixture, then restore that fixture and run the final suite. Audit CI job coverage against local commands; record checks unavailable on the current host. No hardware retest is needed solely for CI prose, but changed shipped/runtime build settings require the usual smoke.

## Subagents and handoff

Assign a package/dependency reviewer and an independent verification/CI reviewer. Audit tests that are present but unregistered, excluded sources and false skips. Deliver repeatable commands and clean consumer evidence for 28–30; complete common report and sync.
