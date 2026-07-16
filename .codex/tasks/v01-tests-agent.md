# v01-tests-agent

Objective: inspect parser/sema/lowering/linker tests for missing RTSL v0.1 vertex/fragment release coverage, add focused tests, and make tiny implementation fixes only when tests expose required compiler behavior.

Architectural rule: parser preserves syntax, semantic analysis owns language meaning and validation, lowering emits backend-neutral metadata, artifact code preserves serialized state, linker validates complete programs, and C ABI reflects linked artifacts without reconstructing frontend policy.

Documentation consulted:
- `spec/language.md`
- `spec/backend-contract.md`
- `spec/compiler-pipeline.md`

Impact map:

Confirmed violations:
- `rtsl/src/frontend/parser.cpp`: `parse_translation_unit()` assigned a `TranslationUnit*` to the local `TranslationUnit` value because the local variable shadowed the parser member pointer. This blocked the build.

Suspicious related locations:
- `tests/compiler.cpp`: broad compiler/linker/C ABI coverage, but exact linked entry metadata and C ABI stage output field details need tightening.
- `tests/artifact.cpp`: artifact roundtrip coverage exists, but complete stage field metadata should be covered directly.
- `rtsl/src/artifact/linker.cpp`: multi-input graphics pipeline merge/validation is release-critical.
- `rtsl/src/api/rtsl.cpp`: C ABI stage output and entry reflection are release-critical.

Inspected locations ruled out:
- `tests/parser_grammar.cpp`: already covers stage attributes, return boundary syntax, contextual tags, and excluded input interface syntax.
- `tests/parser_negative.cpp`: already covers malformed parser recovery.
- `tests/basic.cpp`: basic source/lexer/diagnostics only.
- `tests/shaders/graphics_program.rtsl`: smoke shader only.

Pre-change invariants:
- A program link requires at least one stage entry.
- Any graphics program must contain exactly one vertex entry and exactly one fragment entry.
- Linked graphics backend entry names are `vert` and `frag`, while authored stages are preserved.
- Stage interfaces are authoritative for field placement, interpolation, locations, and member indices.
- Fragment bare `vec4` output reflects as `color` at location 0.

Checklist:
- [x] Read v0.1 language, backend, and compiler-pipeline specs.
- [x] Inspect parser, compiler, artifact, linker, and C ABI tests.
- [x] Inspect relevant implementation boundaries before adding assertions.
- [x] Add focused tests for linked graphics entry metadata and stage interface roundtrip.
- [x] Add focused tests for C ABI stage output reflection details.
- [x] Add cross-object graphics pipeline/linker coverage.
- [x] Run repository-defined verification.
- [x] Repeat repository search after implementation.
- [x] Review final diff for special-case logic and unrelated edits.
- [x] Record verification results and final status.

Verification commands and results:
- `cmake --build out\build --config Debug --target rtsl-tests`: failed in sandbox because MSBuild could not access `C:\Users\lefloysi\AppData\Local\Microsoft SDKs`.
- Escalated `cmake --build out\build --config Debug --target rtsl-tests`: failed on parser member/local shadowing in `parse_translation_unit()`.
- Escalated `cmake --build out\build --config Debug --target rtsl-tests --clean-first`: passed.
- Escalated `ctest --test-dir out\build -C Debug --output-on-failure`: first run failed on attempted fragment-only object test; that test implied a broader compile/link phase change and was replaced.
- Escalated `cmake --build out\build --config Debug --target rtsl-tests`: passed.
- Escalated `ctest --test-dir out\build -C Debug --output-on-failure`: passed, 5/5 tests.
- Final search: `rg "rtslModuleGetStageVariable|stage variable|input interface|fragment-only|split_vertex|split_fragment|FunctionCall|backend_entry_name|StageFieldPlacement" tests rtsl\src -n`.

Blockers:
- `git status` requires per-command `safe.directory`; using `git -c safe.directory=...`.
- The worktree has many existing edits from other agents, including some in files touched here. I did not revert them.

Remaining release gaps:
- Fragment-only object compilation still cannot be tested as a split-stage pipeline because fragment input varying validation currently happens during object lowering and requires the varying interface in the same translation unit.
- Linker diagnostics are covered for missing, duplicate, unresolved, and stale interfaces, but exact diagnostic text/code coverage is still thin.
- C ABI reflects stage outputs and entries, but it does not expose full stage input/varying interface metadata.
- Artifact tests now cover complete stage field metadata for one interface; broader linked-program artifact shape and resource layout metadata still need more dense cases.

Continuation notes:
- Do not revert existing edits from other agents.
- Prefer a follow-up design decision before moving fragment input/varying validation from object lowering to program link.
