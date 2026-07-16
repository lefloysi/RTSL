# spec-drift-agent

## Objective

Code-only cleanup after direction change: inspect source/test/build consistency around stale v0.1 behavior without editing `spec/*` or docs.

## Architectural Rule

Keep the RTSL v0.1 source surface narrow. Syntax belongs to the parser, contextual language meaning belongs to semantic analysis, and tests should pin released behavior instead of preserving obsolete conveniences.

## Documentation Consulted

- `spec/language.md` was read before the direction change.
- `spec/backend-contract.md` was read before the direction change.
- `spec/compiler-pipeline.md` was read before the direction change.
- No documentation or spec files were edited.

## Impact Map

### Confirmed Violations

- `rtsl/src/frontend/tokens.def` reserved `auto`.
- `rtsl/src/frontend/parser.cpp` accepted `auto` as a type atom even though v0.1 built-in types do not include it and no released behavior depends on it.

### Suspicious Related Locations

- `tests/basic.cpp` lexer keyword/contextual-word coverage did not assert `auto` stayed ordinary.
- `tests/compiler.cpp` unknown-type coverage did not explicitly pin `auto` as outside v0.1.

### Inspected Locations Ruled Out

- `tests/parser_grammar.cpp`: no positive `auto` tests found.
- `tests/parser_negative.cpp`: no obsolete `auto` behavior found.
- `rtsl/src/sema/stage_rules.hpp`: vertex/fragment stage names matched current tests.
- `rtsl/src/sema/stage_boundary_tags.def`: only `smooth`, `flat`, and `clip` are accepted boundary tags.
- `CMakeLists.txt`: test target includes current test files and shader smoke path.

## Pre-Change Invariants

- Vertex and fragment stages remain the only graphics stages.
- `using uniform` remains rejected.
- Stage entry reference parameters remain rejected.
- Unknown source type spellings are diagnosed in semantic analysis.

## Checklist

- [x] Read required specs before direction change.
- [x] Avoid editing `spec/*` and docs after direction change.
- [x] Search for obsolete syntax/examples/TODO-like markers around v0.1 vertex/fragment behavior.
- [x] Inspect parser/sema/test ownership points for stale source surface.
- [x] Remove stale `auto` keyword/type acceptance.
- [x] Add tests for the general rule that `auto` is not a v0.1 type spelling.
- [x] Run focused tests.
- [x] Repeat final repository search.
- [x] Review diff for broad or feature-specific special cases.
- [x] Record verification result.

## Verification

- `cmake --build out\build --config Debug --target rtsl-tests`
  - Initial sandbox run failed because MSBuild could not access `C:\Users\lefloysi\AppData\Local\Microsoft SDKs`.
  - Escalated rerun succeeded.
- `ctest --test-dir out\build -C Debug --output-on-failure -R rtsl-tests`
  - Passed: 1/1 tests.

## Final Searches

- `rg -n "kw_Auto|RTSL_KEYWORD\(Auto|\| 'auto'|consume\(TokenKind::kw_Auto\)|TokenKind::kw_Auto" rtsl tests`
  - No remaining matches.

## Result

DONE. Removed stale `auto` keyword/type acceptance and added focused tests that keep `auto` outside the v0.1 type surface. No `spec/*` or documentation files were modified.

## Blockers

None.

## Continuation Notes

None.
