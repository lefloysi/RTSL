# README documentation task

## Objective

Write a production-quality README for RTSL/Rutile that documents intended
design, architecture, usage, development workflow, contribution expectations,
and license without treating the current implementation as the sole source of
truth.

## Architectural Rule

The README must describe the project design and stable surfaces. Implementation
details are supporting evidence only. If source and documentation disagree on an
architectural point, clarify before documenting that point.

## Documentation Consulted

- `AGENTS.md`
- `README.md`
- `architecture.md`
- `docs/language.md`
- `docs/compiler-architecture.md`
- `docs/backend-contract.md`
- `docs/artifacts.md`
- `docs/linking.md`
- `docs/rtir.md`
- tracked `bindings/README.md`
- tracked `cmake/Rtsl.cmake`
- tracked `vcpkg.json`
- tracked `scripts/verify.ps1`

## Impact Map

### Confirmed Violations

- `CMakeLists.txt` included `cmake/Rtsl.cmake`, but `cmake/` was deleted in the
  working tree. Restored `cmake/Rtsl.cmake`.
- `CMakeLists.txt` declared `rtsl-sdk` include path, but `rtsl-sdk/` was absent
  in the working tree. Added SDK headers for shared basic types and artifact
  constants.
- `README.md` and docs describe v0.1 and current implementation-stage details;
  user requested avoiding implementation-stage framing.

### Suspicious Related Locations

- `architecture.md` names `rtsl-sdk/` and `cmake/` as repository directories.
  User clarified that `rtsl-sdk` is the shared core and `rtsl` is the compiler.
- Build/test scripts and `vcpkg.json` are deleted in the working tree but still
  tracked in `HEAD`.
- Several tests and tool files are deleted in the working tree, affecting
  verification of build/development instructions.

### Inspected Locations Ruled Out

- `docs/backend-contract.md` consistently defines backend ownership outside
  this repository.
- `docs/artifacts.md` consistently defines artifact kinds and payload role.
- `docs/linking.md` consistently defines object, library, and program linking.
- `docs/rtir.md` consistently defines RTIR as backend-neutral typed SSA.

## Pre-change Invariants

- Frontend owns syntax.
- Semantic analysis owns source-language meaning and validation.
- RTIR owns backend-neutral representation.
- Artifact code owns serialization.
- Backends consume linked program artifacts and own target-specific lowering.
- C ABI is the stable embedding boundary.

## Checklist

- [x] Inventory repository and existing docs.
- [x] Search for architecture/build/API/test evidence.
- [x] Record contradictions before README edits.
- [x] Combine subagent findings.
- [x] Ask clarification if intent remains ambiguous.
- [x] Write README only after unresolved architecture questions are answered.
- [x] Re-search repository for contradiction with final README.
- [x] Review diff for speculation and implementation-stage framing.
- [x] Run relevant verification or document why it could not run.

## Verification Commands And Results

- `rg --files`: repository inventory collected.
- `git -c safe.directory=... status --short`: working tree has many tracked
  deletions and untracked build/IDE outputs.
- `Test-Path rtsl-sdk`: `False`.
- `Test-Path cmake`: `False`.
- `cmake -S . -B out/readme-check`: failed in sandbox because MSBuild could
  not read Windows SDK metadata under the user profile; succeeded with
  escalation. CLI11 and Catch2 were not found in this build tree, so `rtslc` and
  `rtsl-tests` were skipped.
- `cmake --build out/readme-check --config Debug --target rtsl`: failed in
  sandbox for the same Windows SDK metadata access; succeeded with escalation
  after adding `rtsl-sdk/include/rtsl/basic_types.hpp` and
  `rtsl-sdk/include/rtsl/artifact.hpp`.
- `rg "v0\.1|currently|at the moment|shared `rtsl`|rtsl is the shared|intended
  to be distributed" README.md`: no matches.

## Blockers

No README blocker remains. CLI and test-target verification requires CLI11 and
Catch2 to be discoverable by the selected CMake configure.

## Continuation Notes

Do not restore unrelated deleted files unless explicitly requested. README work
now documents `rtsl` as the compiler and `rtsl-sdk` as the shared core.

## Layout Follow-up

Objective: restructure repository layout so each project has its own folder
shape instead of source files directly under the project root.

Architectural rule: `rtsl` is the compiler project. `rtsl-sdk` is the shared
core used by compiler and transpilers. The C ABI binding belongs inside the
compiler project folder as its public include surface.

Planned moves:

- `rtsl/{api,artifact,driver,frontend,ir,sema,support}` -> `rtsl/src/...`
- `bindings/c/include/rtsl.h` -> `rtsl/include/rtsl.h`
- `rtslc/rtslc.cpp` -> `rtslc/src/rtslc.cpp`

Checklist:

- [x] Map current include dependencies.
- [x] Move files without deleting unrelated user changes.
- [x] Update CMake source and include roots.
- [x] Update docs/README layout references.
- [x] Configure and build compiler target.
- [x] Record verification result.

Verification:

- `rg "bindings/c/include|rtslc/rtslc\.cpp|rtsl/(api|artifact|driver|frontend|ir|sema|support)" README.md architecture.md docs CMakeLists.txt cmake tests rtsl rtslc -n`: only intentional SDK include remained.
- `cmake -S . -B out/layout-check`: failed in sandbox due Windows SDK metadata access, succeeded with escalation. CLI11 and Catch2 were not found, so `rtslc` and `rtsl-tests` were skipped in this build tree.
- `cmake --build out/layout-check --config Debug --target rtsl`: succeeded with escalation.
