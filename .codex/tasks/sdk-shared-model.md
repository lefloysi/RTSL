# Status: IN_PROGRESS

Allowed statuses: `IDLE`, `PLANNING`, `IN_PROGRESS`, `VERIFYING`, `BLOCKED`,
`DONE`.

## Objective
Move artifact wire-format model ownership into `rtsl-sdk` so the compiler and
backend/transpiler code consume the same SDK contract.

## Architectural Rule
Shared artifact constants and artifact kind definitions belong in `rtsl-sdk`.
The compiler artifact writer/reader depends on that SDK model so artifact
production and external artifact consumption stay aligned.

## Documentation Consulted
- docs/language.md
- docs/backend-contract.md
- docs/compiler-architecture.md
- docs/artifacts.md
- architecture.md

## Impact Map

### Confirmed Violations
- The previous cleanup attempt documented SDK as external-only, which conflicts
  with the intended shared-model architecture.
- `rtsl` temporarily stopped linking `rtsl-sdk`, separating the compiler from
  the SDK model.

### Suspicious Related Locations
- `rtsl/frontend/ast.hpp` owns source/semantic reflection structs such as
  `UniformBinding`; these are not SDK model types while they still carry source
  spellings and compiler-only lowering fields.

### Inspected Locations Ruled Out
- Stage interface role is a closed compiler model enum, not a string.

## Invariants

### Before
The compiler and SDK both knew parts of the artifact wire contract, and the
wrong correction tried to remove the compiler's SDK dependency.

### After
The compiler intentionally depends on `rtsl-sdk` for artifact wire constants
and artifact kind definitions. Docs describe SDK as the shared artifact model
layer.

## Checklist
- [x] Restore compiler use of SDK artifact constants.
- [x] Restore public `rtsl` -> `rtsl-sdk` link.
- [x] Restore docs to describe SDK as shared model.
- [x] Identify remaining compiler-owned artifact/reflection model types that
  should move into SDK.
- [x] Move the appropriate shared model into SDK without widening frontend
  syntax ownership unnecessarily.
- [ ] Run repository verification.
- [ ] Repeat repository search for duplicated artifact constants/model.
- [ ] Review diff for accidental boundary reversal.
- [ ] Mark ledger DONE or BLOCKED with evidence.

## Verification

### Commands

### Results

## Final Search

## Blockers

## Continuation Notes
