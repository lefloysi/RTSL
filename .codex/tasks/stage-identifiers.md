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
Replace hardcoded RTSL stage enums and `@vertex` / `@fragment` stage syntax
with identifier-based stage metadata using `@stage : <identifier>`. The
compiler should preserve arbitrary authored stage identifiers while the
graphics release path gives backend/link-time special treatment to known stage
names such as `vertex` and `fragment`.

## Architectural Rule
The parser owns attribute syntax only. Semantic analysis owns recognition of
`@stage : <identifier>` as stage-entry meaning and validation of general stage
entry rules. Linker/backend-owned code may apply graphics pipeline policy to
known stage identifiers, but frontend and generic IR code must not encode a
closed `StageKind` enum or treat vertex/fragment as source-language keywords.

## Documentation Consulted
- docs/language.md
- docs/backend-contract.md
- docs/compiler-architecture.md

## Impact Map

### Confirmed Violations
- docs/language.md documents `@vertex` and `@fragment` as recognized function
  attributes.
- src/frontend/ast.hpp defines `StageKind` with `vertex` and `fragment`.
- src/sema/stage_attributes.def and src/sema/stage_metadata.hpp centralize a
  closed stage table and generated entry names.
- src/sema/sema.cpp resolves attributes by matching `vertex` and `fragment`.
- src/ir/ir.hpp, src/ir/ir.cpp, src/ir/text_rtir.cpp, src/artifact/artifact.*,
  src/artifact/linker.cpp, src/driver/compiler.cpp, src/api/rtsl.cpp, and
  src/sema/mangler.* store or switch on `StageKind`.
- bindings/c/include/rtsl.h exposes a closed `rtsl_stage` enum.
- tests and sample shaders use `@vertex` / `@fragment`.

### Suspicious Related Locations
- src/runtime/spirv.hpp still references a separate `stage_kind` enum despite
  runtime package files being deleted in the current worktree.
- Stage-interface role and field-placement enums may still be real metadata
  concepts, but they need review so this cleanup does not preserve accidental
  enum use by inertia.
- `stage_entry_name` currently maps source stages to 4-letter backend names;
  this likely belongs as backend/link metadata derived from stage identifiers.

### Inspected And Ruled Out
- The parser already stores authored function attributes as textual
  `Attribute::name`, so it does not need a lexer keyword for vertex/fragment.

## Invariants

### Before
- Stage entry source syntax is `@vertex fn ...` or `@fragment fn ...`.
- `StageKind` is a closed enum with `none`, `vertex`, and `fragment`.
- Linking a program requires exactly one vertex and one fragment stage.

### After
- Stage entry source syntax is `@stage : name fn ...`.
- Stage identifiers are preserved as source-authored strings in semantic, IR,
  artifact, reflection, and backend-facing metadata.
- User-authored stage identifiers are open-ended.
- The graphics program release path still requires exactly one `vertex` and one
  `fragment` entry when linking a final graphics program.
- Vertex-specific return boundary treatment is selected from the stage
  identifier `vertex`, not a frontend enum.

## Checklist
- [x] Read v0.1 docs and identify stage syntax/backend-contract mismatch.
- [x] Search the repository for stage enum and hardcoded vertex/fragment use.
- [x] Map affected layers and files before editing.
- [x] Update parser attribute grammar for `@stage : identifier` while keeping
      attributes textual.
- [x] Replace closed `StageKind` storage with open stage identifiers.
- [x] Remove `stage_attributes.def` and `stage_metadata.hpp` if no longer
      needed.
- [x] Move graphics-known stage naming/policy to named helpers that consume
      stage identifiers.
- [x] Update artifacts, ABI reflection, linker validation, and text RTIR.
- [x] Update docs, tests, and sample shaders to the new syntax.
- [x] Move `rtslc` CLI source to `runtime/rtslc` and keep `src/` library-only.
- [x] Remove obsolete runtime package reader files from this repo.
- [x] Run targeted and release-path verification.
- [x] Repeat repository search for residual hardcoded stage enums, old syntax,
      and accidental parallel policy.
- [x] Review final diff for newly introduced special-case logic and phase leaks.

## Verification

### Commands
- `cmd.exe /c 'call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 && cmake -S . -B out\build\x64-Debug -DCMAKE_BUILD_TYPE=Debug && cmake --build out\build\x64-Debug --config Debug --target rtsl-tests rtslc'`
- `cmd.exe /c 'call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 && ctest --test-dir out\build\x64-Debug -C Debug --output-on-failure'`

### Results
- Configure and build succeeded after moving `rtslc` to `runtime/rtslc`.
- CTest passed: 5/5 tests.

## Final Search
- `rg -n "StageKind|stage_kind|rtsl_stage\b|RTSL_STAGE_(NONE|VERTEX|FRAGMENT)|stage_entry_name|stage_attributes|stage_metadata|@vertex|@fragment|package\.hpp|package\.cpp|runtime/package|src/runtime|driver/rtslc|src\\driver\\rtslc|TODO|HACK|workaround|bypass|temporary" src tests docs bindings runtime README.md workspace architecture.md CMakeLists.txt .codex scripts`
  leaves only historical mentions in this ledger and unrelated prose about
  temporary debug/source strings or coding policy.
- `rg -n "package\.hpp|package\.cpp|runtime/package|src/runtime|driver/rtslc|src\\driver\\rtslc" .`
  reports no stale package-reader or old CLI-source references.

## Blockers

## Continuation Notes
Task handling now uses explicit agent-scoped ledgers under `.codex/tasks/`;
`scripts/task_init.ps1` and `scripts/task_validate.ps1` require an agent id
unless a direct task path is supplied, and initialization refuses to overwrite
an existing ledger without `-Force`.

Stage identity is now open string metadata from `@stage : identifier`. The
graphics release path recognizes `vertex` and `fragment` through
`src/sema/stage_rules.hpp`; other stage identifiers can flow through semantic,
IR, artifact, text RTIR, and ABI reflection without adding enum cases.

`rtslc` now lives under `runtime/rtslc`; `src/driver` contains only the library
compiler orchestration. The obsolete runtime package reader files are removed
from this repo per user direction.
