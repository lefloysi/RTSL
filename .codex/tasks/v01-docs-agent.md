# v0.1 Docs Agent

## Objective

Make README, docs, specs, architecture, and contribution docs concise,
accurate, and useful for the RTSL v0.1 release target.

## Architectural Rule

Documentation must describe the current compiler surface and intended v0.1
contracts without moving ownership boundaries. Syntax belongs to frontend docs,
source-language meaning to semantic/spec docs, artifact/linking behavior to
artifact and linker specs, and backend handoff to backend docs.

## Documentation Consulted

- `README.md`
- `architecture.md`
- `docs/README.md`
- `docs/getting-started.md`
- `docs/rtslc.md`
- `docs/cmake-integration.md`
- `docs/c-abi.md`
- `docs/contributing.md`
- `spec/README.md`
- `spec/language.md`
- `spec/compiler-pipeline.md`
- `spec/backend-contract.md`
- `spec/cli.md`
- `spec/artifact-format.md`
- `spec/linking.md`
- `spec/rtir.md`
- `spec/c-abi.md`
- `spec/sdk-model.md`

## Implementation And Tests Consulted

- `tests/parser_grammar.cpp`
- `tests/compiler.cpp`
- `rtslc/src/rtslc.cpp`
- `cmake/Rtsl.cmake`
- `rtsl/src/frontend/tokens.def`
- `rtsl/src/frontend/value_types.def`
- `rtsl/src/frontend/resource_types.def`
- `rtsl/src/sema/stage_boundary_tags.def`

## Impact Map

### Confirmed Violations

- `docs/cmake-integration.md` documented quoted import scanning as if it were
  useful source syntax, but parser tests and `spec/language.md` support
  `import <path>;` only.
- `docs/contributing.md` read like a general contribution workflow even though
  this repo does not document an external contribution process yet.

### Suspicious Related Locations

- `README.md`, `docs/README.md`, and `spec/README.md` needed wording checked so
  they describe v0.1 contracts instead of broad aspirations.
- Specs with open questions needed to remain explicit rather than hiding
  incomplete release decisions.

### Inspected Locations Ruled Out

- `spec/language.md` matches parser/compiler tests for current top-level
  declarations, references, uniforms, layouts, stages, and rejected old syntax.
- `spec/cli.md` matches `rtslc/src/rtslc.cpp`.
- `spec/backend-contract.md`, `spec/linking.md`, `spec/artifact-format.md`,
  `spec/c-abi.md`, and `spec/rtir.md` match the tested public surfaces at this
  documentation level.
- `architecture.md` matches the current source layout and layer ownership.

## Pre-change Invariants

- Docs must not claim backend code exists in this repository.
- Docs must not describe quoted imports, `using uniform`, `input` interface
  blocks, or compute stages as v0.1 source syntax.
- Contribution docs must not invent PR, branching, review, or release process.
- Open spec gaps should stay visible as open questions.

## Checklist

- [x] Inspect dirty worktree without reverting others' edits.
- [x] Inspect implementation/tests before editing docs.
- [x] Read relevant v0.1 docs/specs.
- [x] Identify confirmed documentation drift.
- [x] Patch scoped documentation files.
- [x] Run focused doc consistency searches.
- [x] Review diff for accidental source edits or invented process.
- [x] Record verification results and final risks.

## Verification Commands And Results

- `git -c safe.directory=... status --short`: repository is dirty with many
  existing edits from other agents/users; only docs and this ledger will be
  touched by this task.
- `rg -n 'import "|pull request|PR|branch|placeholder|TBD|TODO|aspirational'
  README.md docs spec architecture.md`: no matches.
- `rg -n 'Contributing|contribution|contributing' README.md docs spec
  architecture.md`: remaining hits are `docs/contributing.md` filename/link and
  text explicitly stating no external contribution process is documented.
- `git -c safe.directory=... diff -- README.md docs/README.md
  docs/contributing.md docs/cmake-integration.md spec/README.md
  .codex/tasks/v01-docs-agent.md`: reviewed scoped documentation changes.

## Final Repository Search

No stale quoted-import syntax, placeholder process terms, or invented PR/branch
workflow language remains in the searched documentation set. The specs still
intentionally contain open questions for unresolved v0.1 contract details.

## Post-change Invariants

- Practical docs describe `import <path>;` as the supported source import form.
- Contribution docs are framed as internal development notes, not as a public
  contribution process.
- v0.1 spec index states the release-target role without hiding open questions.
- README points to development guidance and avoids contribution-process claims.

## Blockers

None.

## Continuation Notes

Keep the pass documentation-only unless a later source inspection reveals a
hard contradiction. Do not run a heavy build unless documentation edits require
code verification.
