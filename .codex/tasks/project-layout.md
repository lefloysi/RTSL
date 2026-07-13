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
Restructure the repository into three top-level project folders: `rtsl/` for
the compiler library, `rtslc/` for the CLI, and `rtsl-sdk/` for shared SDK code
used by both the compiler and backend/transpiler code. Remove the obsolete
single-project `src/` layout.

## Architectural Rule
The compiler library, CLI, and SDK are separate projects with explicit folder
ownership. Build configuration and docs must name those folders directly.
Shared artifact-format knowledge belongs in `rtsl-sdk/` and is consumed by
`rtsl/`; target-specific backend lowering remains outside the compiler.

## Documentation Consulted
- architecture.md
- CMakeLists.txt
- docs/compiler-architecture.md
- docs/artifacts.md

## Impact Map

### Confirmed Violations
- The repository still had a top-level `src/` directory containing the compiler
  library implementation.
- CMake still used old paths for `src/`, `runtime/rtslc`, and `sdk/include`.
- architecture.md still documented the old `src/`, `runtime/rtslc`, and `sdk/`
  layout.
- docs/compiler-architecture.md and docs/artifacts.md referenced `src/`.

### Suspicious Related Locations
- cmake/Rtsl.cmake uses `build/rtsl` as an output directory; this is not a
  source-layout reference.
- Completed historical task ledgers still mention old paths as evidence.

### Inspected And Ruled Out
- Include directives inside compiler code are still rooted at the compiler
  include root and do not need a `rtsl/` prefix.

## Invariants

### Before
Compiler library, CLI, and SDK work were split across `src/`, `runtime/rtslc`,
and `sdk`, with docs still describing a single source tree.

### After
The top-level project folders are `rtsl/`, `rtslc/`, and `rtsl-sdk/`. CMake
builds `rtsl`, `rtslc`, and `rtsl-sdk` from those folders. No top-level `src/`,
`runtime/`, or `sdk/` project folder remains.

## Checklist
- [x] Create explicit project-layout ledger.
- [x] Move `src/` to `rtsl/`.
- [x] Move `runtime/rtslc/` to `rtslc/`.
- [x] Move `sdk/` to `rtsl-sdk/`.
- [x] Update CMake project paths.
- [x] Update architecture and source-layout docs.
- [x] Reconfigure, build, and run tests.
- [x] Final search for old project-layout paths.
- [x] Mark ledger DONE with verification evidence.

## Verification

### Commands
- `cmd.exe /c 'call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 && cmake -S . -B out\build\x64-Debug -DCMAKE_BUILD_TYPE=Debug && cmake --build out\build\x64-Debug --config Debug --target rtsl-tests rtslc'`
- `cmd.exe /c 'call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 && ctest --test-dir out\build\x64-Debug -C Debug --output-on-failure'`

### Results
- Configure/build completed for the new `rtsl/`, `rtslc/`, and `rtsl-sdk/`
  paths. The build command timed out in the shell after compiling, but
  `.ninja_log` showed `rtsl-sdk.lib`, `rtsl.lib`, `rtslc.exe`, and
  `rtsl-tests.exe` were produced from the new folders.
- CTest passed: 5/5 tests.

## Final Search
- `rg -n "is_artifact_kind|fail\(|ok\(|artifact_kind_from_wire|rtsl/sdk/artifact\.hpp|rtsl_sdk\.c|runtime/rtslc|\bsdk/|\bsrc/" CMakeLists.txt README.md architecture.md docs rtsl rtslc rtsl-sdk`
  found no old project-folder paths or rejected SDK helper shapes in live
  source/docs. Matches for `rtsl-sdk/` are the intended top-level project name.

## Blockers

## Continuation Notes
The repository now has the requested top-level project layout:
`rtsl/`, `rtslc/`, and `rtsl-sdk/`. The old top-level `src/`, `runtime/`, and
`sdk/` project folders are gone. `rtsl` links `rtsl-sdk`, so shared artifact
header parsing is used by the compiler and is available to backend/transpiler
code.
