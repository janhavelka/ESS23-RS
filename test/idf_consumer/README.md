# Core-only native ESP-IDF consumer

Stage only `CMakeLists.txt`, `include/` and `src/` into a directory named
`MotorControlRS`; do not copy examples, tests or vendor downloads into it.
From an exported ESP-IDF 5.5.5 shell at the repository root:

```powershell
$consumerRepository = (Get-Location).Path
$coreStage = Join-Path $consumerRepository build/core-package/MotorControlRS
New-Item -ItemType Directory -Force $coreStage
Copy-Item CMakeLists.txt $coreStage
Copy-Item include,src $coreStage -Recurse
$consumerBuild = Join-Path $consumerRepository build/idf-core-consumer
idf.py -C test/idf_consumer -B $consumerBuild -D "MOTORCONTROLRS_COMPONENT_PATH=$coreStage" build
```

Use an empty stage/build directory for a clean check. The application compiles
all 27 installed headers, links only the core component, and contains real
checked codec and exact-number preparation calls. Its build does not run
`app_main` or access a motor. Native tests execute the same library behavior.
