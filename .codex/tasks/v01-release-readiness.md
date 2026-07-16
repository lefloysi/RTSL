# v0.1 Release Readiness

## Objective

Bring RTSL closer to v0.1 release readiness across SDK ownership, code hygiene, docs, and verification.

## Architectural Rule

The SDK owns stable shared metadata used by the compiler and future transpilers. Compiler-only syntax and diagnostics stay internal. Avoid large mechanical churn unless it removes a real release blocker.

## Documentation Consulted

- AGENTS.md instructions from the conversation.
- Existing `spec/*`, `docs/*`, README, and implementation/tests as workers inspect them.

## Impact Map

### Confirmed Violations

- SDK only exposed artifact kind/version and basic integers; stable reflection metadata was still compiler-internal.
- Frontend lexer/parser private members used trailing underscores, a style the user explicitly rejected.
- C ABI had no direct version query.

### Suspicious Related Locations

- Public C ABI mirrors SDK metadata but is intentionally a C bridge.
- `rtsl/src/frontend/ast.hpp` mixes parser-only syntax nodes with shared reflection records.
- Existing docs/spec changes may be stale or aspirational and need a pass.

### Inspected Locations Ruled Out

- Heavy IR instruction internals are not moved to SDK in this slice; transpiler-facing RTIR API needs a deliberate design pass.

## Pre-change Invariants

- Keep v0.1 focused on vertex/fragment graphics pipeline.
- Do not break C ABI names in this pass.
- Keep compiler internals buildable after SDK ownership changes.

## Checklist

- [x] Spawn subagents for docs/spec, tests/release behavior, and API/SDK hygiene.
- [x] Redirect API worker to SDK split.
- [x] Move stable shared reflection metadata into SDK.
- [x] Remove trailing member suffixes from the pointed-out frontend lexer/parser code.
- [x] Add direct C ABI v0.1 version accessors.
- [x] Review subagent outputs.
- [x] Integrate non-conflicting changes.
- [x] Run build and CTest.
- [x] Record final searches and release gaps.

## Verification Commands

- `cmake --build out\build --config Debug --target rtsl-tests rtslc -- /m:1`
  - Passed after escalation for Windows SDK access.
- `ctest --test-dir out\build -C Debug --output-on-failure`
  - Passed: 5/5 tests.

## Final Repository Search

- `rg -n "RTSL_KEYWORD\(Auto\)|kw_Auto|_rtsl_module_paths|sources_|diagnostics_|file_id_|input_|tokens_|cursor_|unit_\b" rtsl\src\frontend rtsl-sdk tests cmake`
  - No stale `auto` keyword handling.
  - No lexer/parser trailing member suffix hits.
  - Only unrelated CMake variable names with underscores remain.
- `rg -n "rtslGetVersionMajor|rtslGetVersionMinor|PayloadKind|StageInterface|UniformBinding" rtsl-sdk\include rtsl\include rtsl\src tests docs spec README.md`
  - Confirms SDK-owned artifact/stage/resource metadata and C ABI version accessor usage.

## Blockers

- None yet.

## Remaining v0.1 Release Gaps

- Full `Artifact`/`IRModule` backend-consumer API still needs an SDK design pass; current move only covers stable metadata and container constants.
- C ABI exposes stage outputs but not full input/varying interfaces.
- Split-object vertex/fragment linking still lacks good positive coverage because object lowering validates fragment varyings before link-time combination.
- Linker diagnostic code/text assertions remain thin.
- Resource layout metadata and linked-program artifact shape deserve denser roundtrip coverage.

## Result

DONE for this pass. The project is materially closer to v0.1: SDK owns shared metadata/constants, docs are less aspirational, tests cover more release behavior, frontend member suffixes were removed in the pointed-out area, and build/CTest are green.
