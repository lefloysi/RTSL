# C++ API/style cleanup

Objective: Find and fix low-risk C++ API/style violations in code/tests only, without changing ABI or behavior.

Architectural rule: Preserve compiler ownership boundaries and make only local style/API improvements: borrowed text as `std::string_view`, borrowed contiguous ranges as `std::span`, brace initialization for local construction, and direct includes for used library facilities.

Documentation consulted:
- AGENTS.md project rules from prompt.
- No docs/spec files edited per user direction.

Impact map:

Confirmed violations:
- `rtsl/src/ir/ir.cpp`: read-only `std::string` references used where `std::string_view` is sufficient.
- `rtsl/src/support/io.cpp`: local stream construction used parenthesized initialization.
- `rtslc/src/rtslc.cpp`: local `Linker` construction used parenthesized initialization.
- `rtsl/src/api/language_service.cpp`: local compiler phase object construction used parenthesized initialization.
- `rtsl/src/driver/compiler.cpp`: local compiler phase object construction used parenthesized initialization.
- `tests/compiler.cpp`: local stream and `Linker` construction used parenthesized initialization.
- `tests/basic.cpp` and `tests/parser_grammar.cpp`: local lexer/parser construction used parenthesized initialization.

Suspicious related locations:
- `rtslc/src/rtslc.cpp`: `std::span<const std::string>` parameters are internal and range-like, but changing them to views would require conversion/storage churn and is higher risk.
- `rtsl/src/frontend/preprocessor.hpp/.cpp`: `std::span<const std::string>` represents current owned defines storage from `CompilerInvocation`; left unchanged to avoid wider API churn.
- Several classes own compiler state or maintain invariants (`Lexer`, `Parser`, `Linker`, `CompilerInstance`, `TypeRegistry`, `FunctionLowerer`) and are not stateless-class cleanup candidates.

Inspected locations ruled out:
- `rtsl/include/rtsl.h`: public C ABI, intentionally unchanged.
- `rtsl-sdk/include/rtsl/*.hpp`: public SDK surface, no low-risk borrowed-reference hit found.
- `tests/artifact.cpp`: no local parenthesized construction in the inspected file header area.

Pre-change invariants:
- No public ABI signature changes.
- No docs/spec edits.
- No behavior changes to parsing, lowering, artifact serialization, linking, or tests.

Checklist:
- [x] Search code/tests for `const std::string&` and `const std::vector<T>&`.
- [x] Search code/tests for simple parenthesized local construction.
- [x] Inspect touched files before editing because the worktree has concurrent changes.
- [x] Apply narrow code/test edits only.
- [x] Run focused build/tests.
- [x] Repeat final repository searches.
- [x] Review diff for accidental docs/spec edits or special-case logic.

Verification commands and results:
- `cmake --build out\build --config Debug --target rtsl-tests rtslc` failed in sandbox because MSBuild could not access `C:\Users\lefloysi\AppData\Local\Microsoft SDKs`.
- Escalated rerun reached compilation and failed because `std::string_view stage` required an explicit copy into `PendingStage::stage`; fixed with `std::string{ stage }`.
- `cmake --build out\build --config Debug --target rtsl-tests rtslc` escalated rerun failed on MSVC shared PDB contention for `rtsl-tests`.
- `cmake --build out\build --config Debug --target rtsl-tests rtslc -- /m:1` passed.
- `ctest --test-dir out\build -C Debug --output-on-failure` passed: 5/5 tests.

Final searches:
- `rg -n "const std::string&|const std::vector<[^\n]+>&" rtsl rtslc rtsl-sdk tests`: no matches.
- `rg -n "\b(std::ifstream|std::ofstream|Lexer|Parser|Linker|Sema|TypeRegistry|Lex|CompilerInstance|DiagnosticEngine|SourceManager)\s+[A-Za-z_][A-Za-z0-9_]*\([^;{}]*\);" rtsl rtslc tests`: no matches.
- `git diff -- docs spec README.md architecture.md AGENTS.md` shows pre-existing concurrent documentation changes; no docs/spec edits made by this task.

Blockers:
- None.

Result:
- DONE. Low-risk cleanup applied to code/tests only, with public ABI untouched.
