# Cleanup Coordination

## Objective

Coordinate parallel cleanup agents for RTSL without touching documentation after the user explicitly requested hands off documentation.

## Architectural Rule

Keep cleanup changes code/test/build focused. Do not revert or overwrite existing user/worktree edits.

## Documentation Consulted

- AGENTS.md instructions provided in the conversation.

## Impact Map

### Confirmed Violations

- Stale `auto` keyword/type handling remained in frontend/parser code even though v0.1 treats it as an ordinary identifier/unknown type.
- `_rtsl_module_paths` in `cmake/Rtsl.cmake` was unused bookkeeping.
- Several local object constructions still used declaration-style parentheses.
- Read-only helper signatures still used `const std::string&` where `std::string_view` was sufficient.
- `workspace/rtslc.exe` was a stale copied executable and is ignored/regenerable.

### Suspicious Related Locations

- Existing modified files in source/tests/build may already contain active work.
- TODO/dead comments in `rtsl/src/artifact/artifact.cpp` and `rtsl/src/ir/ir.hpp` need inspection before changing.

### Inspected Locations Ruled Out

- Documentation/spec cleanup is out of scope by user request.
- Constructor elision comments in IR/artifact code describe intentional live/dead artifact behavior, not stale code.

## Pre-change Invariants

- Do not edit `docs/`, `spec/`, README, architecture, or prose documentation.
- Do not revert existing modified files.
- Subagents must avoid documentation edits.

## Checklist

- [x] Spawn at least three subagents with distinct cleanup topics.
- [x] Redirect subagents away from documentation after user correction.
- [x] Inspect local build/test layout for safe cleanup opportunities.
- [x] Review subagent results.
- [x] Integrate only non-conflicting code/test/build changes.
- [x] Run relevant verification.
- [x] Record final repository search and result.

## Verification Commands

- `cmake --build out\build --config Debug --target rtsl-tests rtslc -- /m:1`
  - First sandboxed run failed due denied access to `C:\Users\lefloysi\AppData\Local\Microsoft SDKs`.
  - Escalated rerun passed and built `rtsl.lib`, `rtsl-tests.exe`, and `rtslc.exe`.
- `ctest --test-dir out\build -C Debug --output-on-failure`
  - Passed: 5/5 tests.

## Final Repository Search

- `rg -n "kw_Auto|RTSL_KEYWORD\(Auto\)|const std::string&|const std::vector<|_rtsl_module_paths" rtsl rtslc rtsl-sdk tests cmake`
  - No matches.

## Blockers

- None.

## Result

DONE. Four subagents completed distinct cleanup tracks. Documentation/spec files were not edited after the user correction. Build and test verification are green.

## Continuation Notes

- Use `git -c safe.directory=C:/Users/lefloysi/Documents/GameDev/OutpostsOfOdyssey/leaf-framework/Rutile/RTSL` for git commands due Windows ownership mismatch.
