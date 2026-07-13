# Status: IN PROGRESS

## Objective
Broad clarity pass over RTSL. Treat function attributes and return-boundary
tags as examples, not the whole scope. Search the compiler for parser/sema/IR/
artifact/backend boundary violations, unnecessary special cases, stale docs,
and cleanup opportunities that move v0.1 toward a coherent language design.

Follow-up v0.1 language-shape correction: pointers are not source syntax;
references are source syntax and must be represented/validated correctly.
Define the v0.1 type-system and standard-library surface without requiring
stdlib lowering yet.

Follow-up compiler-quality pass: audit whether parser, semantic analysis, IR
lowering, linking, docs, and tests agree on the current v0.1 language shape.
Fix concrete mismatches rather than widening the language.

## Architectural Rule
The parser records syntax. Semantic analysis interprets contextual language
meaning such as known function attributes and stage-boundary tags. IR lowering
and artifact code consume normalized semantic metadata.

## Documentation Consulted
- docs/language.md
- docs/backend-contract.md
- docs/compiler-architecture.md

## Impact Map

### Confirmed Violations
- src/frontend/parser.cpp maps `@vertex` and `@fragment` directly to
  `StageKind`.
- src/frontend/parser.cpp maps `clip`, `smooth`, and `flat` directly to
  `InterpolationKind`.
- Initial pass was too narrow and did not audit the full repository as required.
- Builtin value type spellings were duplicated between sema and IR.
- `resource_types.def` listed resource spellings that `resource_binding_kind`
  did not classify, so accepted source types could fail later as resources.
- C API reflection had a parallel resource spelling check for buffer kinds.
- Parser currently accepts `Type*` and silently strips it; pointers are not part
  of RTSL.
- References are parsed as a side flag on parameters only, not as a coherent
  type property across the source surface.
- Current request asks for another compiler pass because the language now looks
  plausible but implementation correctness is uncertain.
- IR call lowering rebuilt user-call targets from value argument types only, so
  sema could accept calls to `const T&` functions or scalar-compatible
  overloads that link/inlining could not resolve to the callee definition.
- Stage entry parameters could be references even though stage interfaces and
  backend contracts describe value payloads.

### Suspicious Related Locations
- src/ir/ir.cpp consumes normalized `StageKind` and stage interfaces.
- src/artifact/artifact.cpp serializes normalized stage/interface metadata.
- src/driver/compiler.cpp reflects stage entries from normalized functions.
- tests/parser_grammar.cpp has parser-level assertions for stage/tag meaning.
- `sample` remains a directly recognized primitive in sema and IR. It is a
  real v0.1 primitive rule, but should eventually move behind a named primitive
  signature table before the standard library grows.
- Type spelling plus side flags is likely too weak for a full type system, but
  v0.1 can first make reference-vs-value explicit and reject pointers.
- Stage signature validation and reference parameter lowering may still accept
  source shapes that do not have coherent backend semantics.

### Inspected And Ruled Out
- src/runtime/package.hpp/cpp mirrors serialized artifact enums; no source
  parsing belongs there.
- src/api/rtsl.cpp only adapts normalized artifact metadata to C ABI structs.

## Invariants

### Before
- `@vertex` and `@fragment` are parser-known stage attributes.
- Return-boundary tags are parser-known and immediately normalized.

### After
- Function attributes are stored as generic authored attributes in the AST.
- Return-boundary tags are stored as generic authored tags in the AST.
- Sema resolves known attributes and tags to the same normalized metadata.
- Parser tests assert syntax capture; compiler tests assert semantic rejection.
- IR call targets for functions declared in the current semantic module use the
  selected callable's canonical parameter identity, including const/reference
  qualifiers.
- Stage entry parameters are value payloads; reference parameters remain valid
  for ordinary functions and constructors.

## Checklist
- [x] Architectural search for stage attributes and boundary tags.
- [x] Add generic AST representation for function attributes and field tags.
- [x] Move function attribute resolution to sema.
- [x] Move boundary tag resolution to sema.
- [x] Update parser docs/comments and tests.
- [x] Run repository-defined verification.
- [x] Repeat repository search for remaining parser special cases.
- [x] Review diff for accidental new feature-specific branches in generic code.
- [x] Centralize resource binding classification from `resource_types.def`.
- [x] Centralize builtin value type spellings from `value_types.def`.
- [x] Replace stage attribute/tag branches with sema-owned definition tables.
- [x] Remove pointer parsing from the type grammar and tests.
- [x] Preserve/reference-check `Type&` consistently in parse/sema docs.
- [x] Add v0.1 type-system and stdlib surface documentation.
- [x] Add parser/compiler coverage for rejected pointers and accepted references.
- [x] Add sema-owned stdlib signature table for current v0.1 callable surface.
- [x] Audit parser/sema/IR/linker agreement on current language surface.
- [x] Fix concrete acceptance/lowering mismatches.
- [x] Add focused regression tests for every fixed mismatch.
- [x] Rebuild, run tests, and record final search.

## Verification

### Commands
- `cmake --build out\build --config Debug`
- `ctest --test-dir out\build -C Debug --output-on-failure`
- `rg` searches for duplicated resource/value/stage mappings and parser-owned
  stage/tag policy.

### Results
- Build succeeded after rerunning with escalation for Visual Studio/Windows SDK
  access outside the sandbox.
- CTest passed: 5/5 tests.
- Second broad pass build succeeded and CTest passed again after shared
  resource/value/stage definition tables.
- Pointer/reference/type-system follow-up build succeeded.
- Pointer/reference/type-system follow-up CTest passed: 5/5 tests.
- Compiler-quality pass build succeeded.
- Compiler-quality pass CTest passed: 5/5 tests.

## Final Search
- `rg -n "vertex|fragment|clip|smooth|flat|StageKind|InterpolationKind" src\frontend`
  shows no parser-side semantic conversion. Remaining frontend references are
  token/context comments and normalized data declarations.
- `rg -n "parse_stage_attribute|parse_interpolation|unknown attribute|unknown pipeline tag|field\.interpolation =|StageKind::vertex|StageKind::fragment" src tests docs README.md`
  shows resolution in sema and consumption in IR/artifact/linker/API/runtime.
- `rg -n "resource_types\.def|value_types\.def|stage_attributes\.def|stage_boundary_tags\.def|resource_binding_kind|uniform_kind_from_type|kBuiltinValueTypes|resource_binding_kinds" src tests docs README.md`
  shows centralized tables and no old duplicated builtin/resource tables.
- `rg -n "pointer marker stripped|pointers are not RTSL|has_pointer|is_reference" src tests docs`
  shows pointer syntax is diagnosed and references are preserved only in
  parameter declarations.
- `rg -n "callee ==|sample|stdlib.def|RTSL_STDLIB_FN" src tests docs`
  shows `sample` is declared in the sema stdlib table; only IR lowering still
  has operation-specific lowering for the existing primitive.
- `rg -n "mangle_rtsl\(|parameter_identity|resolve_call_target|callable_targets|FunctionCall|is_reference|stage entry parameters" src tests docs .codex\TASK.md`
  shows call target identity now flows through semantic function parameter
  qualifiers; the remaining argument-based mangle is the import fallback for
  declarations without local signature detail.
- `rg -n "callee ==|sample|vertex_entry\(const|const Point&|Type\*|has_pointer|pointers are not" src tests docs`
  shows the only stdlib-specific IR branch is the existing `sample` primitive,
  and reference/pointer coverage is present in parser/compiler tests.

## Blockers
- None.

## Continuation Notes
- The worktree already contained many unrelated dirty files before this pass.
  This pass intentionally touched only the attribute/tag ownership cleanup,
  docs, tests, and ledger.
- User correctly rejected the narrow scope. Continue with a repository-wide
  search and only keep prior edits if they remain part of the broader cleanup.
