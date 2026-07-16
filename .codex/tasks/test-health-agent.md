# test-health-agent

Objective: Inspect current test/build setup and verification health for RTSL v0.1 tests, focusing on vertex/fragment release-surface coverage, failing/flaky tests, and small test/harness cleanups only.

Architectural rule: Keep changes in tests/build harness only. Parser tests should assert syntax/AST shape, compiler tests should assert semantic/lowering/artifact behavior, linker tests should assert program/library validation, and backend-facing expectations should be verified through normalized artifact reflection rather than source-text policy.

Documentation consulted:
- AGENTS.md
- spec/language.md
- spec/backend-contract.md
- spec/compiler-pipeline.md

Impact map:

Confirmed violations:
- Current source rebuild fails in core implementation code, not tests: `rtsl/src/ir/ir.cpp(2212,41)` cannot aggregate-initialize `PendingStage` because copy-list-initializing `std::string` from the pending input is rejected by MSVC.
- Existing CTest binaries pass, so current red state is build-from-source, not previously built test execution.
- Linker coverage had duplicate vertex-stage validation but lacked the symmetric duplicate fragment-stage validation.

Suspicious related locations:
- CMakeLists.txt test registration and rtslc smoke fixture wiring.
- tests/basic.cpp lexer/source-manager smoke tests.
- tests/parser_grammar.cpp parser syntax and AST coverage.
- tests/parser_negative.cpp compiler-driven parser recovery diagnostics.
- tests/compiler.cpp compile/sema/lowering/link/ABI coverage.
- tests/artifact.cpp artifact serialization coverage.
- tests/shaders/graphics_program.rtsl CLI smoke shader.
- rtsl/src/artifact/linker.cpp stage validation implementation, inspected through searches only because current direction is tests/build code only.

Inspected locations ruled out:
- spec/language.md, spec/backend-contract.md, spec/compiler-pipeline.md consulted only; no edits allowed by current direction.
- tests/artifact.cpp has basic stage interface serialization coverage, including type name and field round trip.
- tests/shaders/graphics_program.rtsl exercises a minimal vertex+fragment CLI smoke path.

Pre-change invariants:
- Do not edit docs/spec files.
- Do not revert concurrent work.
- Prefer tests for documented v0.1 vertex/fragment behavior over compiler feature work.
- Keep test additions focused and release-surface oriented.

Checklist:
- [x] Inspect CMake/CTest registration.
- [x] Inspect parser, semantic/compiler, artifact, linker, ABI, and smoke shader tests.
- [x] Consult v0.1 docs without editing docs/spec.
- [x] Run configured build.
- [x] Run available CTest verification.
- [x] Add or tighten tiny test/harness coverage if clearly beneficial.
- [x] Repeat repository search for release-surface gaps after changes.
- [x] Review diff for docs/spec edits and special-case test hacks.
- [x] Record post-change invariants, verification results, final searches, blockers, and result.

Changes made:
- tests/compiler.cpp: added `program link rejects duplicate fragment stage entries`, symmetric with the existing duplicate vertex-stage linker validation test.

Verification commands and results:
- `cmake --build out\\build --config Debug` inside sandbox: failed during SDK lookup due denied access to `C:\\Users\\lefloysi\\AppData\\Local\\Microsoft SDKs`.
- `cmake --build out\\build --config Debug` escalated: failed compiling `rtsl/src/ir/ir.cpp(2212,41)` with MSVC C2440 while initializing `PendingStage`.
- Repeated `cmake --build out\\build --config Debug` after test change: same `rtsl/src/ir/ir.cpp(2212,41)` failure.
- `ctest --test-dir out\\build -C Debug --output-on-failure` escalated: 5/5 tests passed (`rtsl-tests`, `rtslc-smoke-prepare`, `rtslc-smoke-compile`, `rtslc-smoke-link`, `rtslc-smoke-dump`). This used existing binaries because source rebuild is blocked.

Post-change invariants:
- No docs/spec files edited by this agent.
- Only test coverage and the required task ledger were changed by this agent.
- New test asserts linker behavior through public compile/link path; no implementation workaround or feature-specific harness bypass added.

Final searches:
- `rg "stage|fragment|vertex|link_program|StageRole|StageFieldPlacement|InterpolationKind|rtslc-smoke|add_test|FIXTURES" CMakeLists.txt tests rtsl\\src -n`
- `rg "duplicate fragment|fragment bare vec4|missing a fragment|missing a vertex|StageRole::varying|StageRole::output|member_index|rtslc-smoke" tests CMakeLists.txt rtsl\\src -n`
- `rg "TODO|FIXME|temporary|workaround|duplicate fragment|fragment stage|vertex stage|stage output|varying" tests CMakeLists.txt -n`

Prioritized release-surface test gaps:
1. Rebuild must be restored first; the added duplicate-fragment test cannot be proven against current source until `rtsl/src/ir/ir.cpp(2212)` compiles.
2. Add linker diagnostics/code assertions for missing vertex vs missing fragment once diagnostic-code expectations are stable enough for tests.
3. Add artifact round-trip coverage for full stage interface field metadata: interpolation, placement, location, and member_index, not just type/name.
4. Add CLI smoke assertions for produced object/program artifact kind or dump contents; current smoke only checks command success.
5. Add parser/sema tests for duplicate or conflicting stage attributes if not already covered by another agent's pending changes.

Blockers:
- Current source build is blocked by non-test implementation compile error in `rtsl/src/ir/ir.cpp(2212,41)`. Per direction change, this was not edited.

Result: DONE WITH BUILD BLOCKER RECORDED
