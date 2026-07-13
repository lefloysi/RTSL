# Status: DONE

Allowed statuses: `IDLE`, `PLANNING`, `IN_PROGRESS`, `VERIFYING`, `BLOCKED`,
`DONE`.

State flow:

```text
IDLE -> PLANNING -> IN_PROGRESS -> VERIFYING -> DONE
                         |
                         -> BLOCKED
```

`DONE` is allowed only when there are no unchecked checklist items, all
required verification commands have results, final repository-wide search is
recorded, blockers are empty, and continuation notes summarize the resulting
architecture.

## Objective
Add an RTSL SDK layer as the embeddable project-facing surface for reading RTSL
artifacts and exposing the data backend headers need for target-specific
lowering.
Keep `src/` as the compiler/library implementation and keep `runtime/rtslc` as
the CLI only.

## Architectural Rule
Compiler/library code lives under `src/` and builds as `rtsl`. CLI code lives
under `runtime/rtslc` and builds as `rtslc`. SDK code lives under `sdk/` and
builds as `rtsl-sdk`; it is the layer downstream projects consume for artifact
inspection. Backend headers own target-specific transpilation. The SDK must not
pull frontend/compiler ownership back into runtime users or pick backend
targets itself.

## Documentation Consulted
- architecture.md
- CMakeLists.txt
- docs/backend-contract.md
- docs/compiler-architecture.md

## Impact Map

### Confirmed Violations
- CMake currently builds only `rtsl`, `rtslc`, and tests; there is no SDK layer.
- architecture.md says runtime package readers are not part of this repo, but
  does not define the new SDK replacement surface.
- README and docs still describe backends consuming `.rtslp` directly, without
  naming the SDK as the embeddable project layer.

### Suspicious Related Locations
- cmake/Rtsl.cmake invokes `rtslc`; it may later need SDK integration but does
  not need it for this structural pass.
- `src/sema/stage_rules.hpp` currently contains backend entry naming helpers;
  future SDK/backend work may move backend-specific naming out of sema.

### Inspected And Ruled Out
- `runtime/rtslc/rtslc.cpp` is CLI-specific and should remain outside SDK.
- `src/driver/compiler.*` is library orchestration and should stay in `src`.

## Invariants

### Before
Projects either consumed `.rtslp` directly or would have needed ad hoc runtime
package helpers. The repository had no dedicated SDK target.

### After
Projects can depend on an `rtsl-sdk` target under `sdk/`. The SDK is a separate
C-compatible embeddable layer from the compiler library and CLI. Initial SDK
code establishes a public C header and artifact-view vocabulary without naming
or choosing backend targets.

## Checklist
- [x] Create explicit SDK task ledger.
- [x] Inspect current build and architecture docs after the CLI move.
- [x] Add `sdk/` source/include layout.
- [x] Add an `rtsl-sdk` build target.
- [x] Document the SDK layer in architecture and README.
- [x] Correct SDK API to a C-compatible artifact-view layer, not a C++ transpiler API.
- [x] Keep package-reader references removed.
- [x] Reconfigure, build, and run tests.
- [x] Final search for stale runtime/package wording and old build paths.
- [x] Mark ledger DONE only with verification evidence.

## Verification

### Commands
- `cmd.exe /c 'call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 && cmake -S . -B out\build\x64-Debug -DCMAKE_BUILD_TYPE=Debug && cmake --build out\build\x64-Debug --config Debug --target rtsl-sdk rtsl-tests rtslc && ctest --test-dir out\build\x64-Debug -C Debug --output-on-failure'`

### Results
- Configure succeeded.
- `rtsl-sdk` built as a C static library from `sdk/src/rtsl_sdk.c`.
- `rtsl-tests` and `rtslc` targets were current.
- CTest passed: 5/5 tests.

## Final Search
- `rg -n "rtsl::sdk|namespace rtsl|sdk\.hpp|TargetFormat|Transpile|transpile\(|supports\(|std::span|std::vector|std::string|uint16_t major|sdk/src/.*cpp|SDK transpiler|SDK-facing artifact inspection and transpilation" sdk README.md architecture.md docs .codex\tasks\sdk-layer.md CMakeLists.txt`
  finds no SDK C++ API or SDK-owned transpilation surface. Remaining hits are
  unrelated general C++ style documentation outside the SDK.
- Earlier package-reader and old CLI path searches found no stale
  `package.hpp`, `package.cpp`, `runtime/package`, `src/runtime`, or
  `driver/rtslc` references.

## Blockers

## Continuation Notes
The SDK boundary is deliberately plain C and backend-neutral:
`sdk/include/rtsl_sdk.h` exposes version, artifact kind, artifact view, result,
and `rtslSdkReadArtifact`. It does not name SPIR-V, target formats, backend
support checks, or transpilation entry points. Backend headers should consume
SDK artifact views and own target-specific lowering.
