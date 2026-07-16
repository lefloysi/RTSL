# Practical top-level docs

Objective: create practical top-level documentation for RTSL without editing `docs/spec` or `spec`.

Architectural rule: document public integration surfaces from repository evidence only. Keep SDK, CLI, ABI, CMake, compiler, and backend boundaries explicit.

Documentation consulted:
- `README.md`
- `architecture.md`
- `AGENTS.md`

Impact map:

Confirmed violations:
- Top-level `docs/` had no practical user docs despite README links and requested files.

Suspicious related locations:
- `rtslc/src/rtslc.cpp` for CLI behavior.
- `cmake/Rtsl.cmake` for CMake helper behavior.
- `rtsl/include/rtsl.h` and `rtsl/src/api/rtsl.cpp` for C ABI and lifetime behavior.
- `CMakeLists.txt` for targets, dependencies, and tests.
- `rtsl-sdk/include/rtsl/*.hpp` for shared artifact-facing constants/types.
- `tests/*.cpp` for executable examples of compiler/linker/ABI behavior.

Inspected locations ruled out:
- `docs/spec` and `spec` are intentionally untouched.
- `tools/` was not needed for the requested practical docs.

Pre-change invariants:
- Do not edit source code.
- Do not edit `docs/spec` or `spec`.
- Do not invent unclear workflow.
- Preserve repository ownership boundaries.

Checklist:
- [x] Architectural search.
- [x] Affected layers and files identified.
- [x] Important invariants recorded.
- [x] Add practical docs for getting started, CLI, CMake, C ABI, and contributing.
- [x] Keep docs concise and evidence-backed.
- [x] Repeated repository search after implementation.
- [x] Final diff review for accidental edits outside allowed docs.
- [x] Repository-defined verification considered.
- [x] Ledger evidence recorded.

Verification commands and results:
- `rg --files`: passed, identified repo shape.
- `rg "rtsl|RTSL|rtslc|rtsl_add_program|rtsl-sdk|artifact" -n . --glob '!docs/spec/**' --glob '!spec/**'`: passed, gathered evidence.
- `git status --short`: blocked by safe-directory ownership; no Git state used.
- Final `rg --files docs`: passed.
- Final `rg "docs/spec|spec/|spec\\" docs`: no matches.
- Final `rg "TODO|TBD|unclear|maybe|probably" docs`: no matches.
- Final `Get-Content docs/*.md`: passed, reviewed practical docs.

Post-change invariants:
- Only top-level `docs/*.md` files added.
- No source files, `docs/spec`, or `spec` edited.
- Docs describe existing public surfaces and unresolved workflow gaps.

Blockers:
- Git status/diff unavailable without configuring safe.directory.
- No clear install/export/package workflow is present in the repo.
- `CMakeLists.txt` references `tests/shaders/graphics_program.rtsl`, but `rg --files` did not show that fixture.

Continuation notes:
- If install/package support is added later, update `docs/getting-started.md` and `docs/cmake-integration.md`.
- If `rtsl-sdk` grows artifact readers beyond shared constants/types, update `docs/getting-started.md` and `docs/c-abi.md`.

Status: DONE.
