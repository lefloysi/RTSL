# RTSL Cleanup Main

Objective: run a broad but bounded cleanup over RTSL using subagents, preserving v0.1 compiler behavior and avoiding unrelated rewrites.

Architectural rule: cleanup must respect pipeline ownership: parser preserves syntax, sema owns source meaning, lowering owns backend-neutral IR, artifact/linker own serialization and merge, docs describe observable v0.1 behavior.

Documentation consulted:
- `spec/language.md`
- `spec/backend-contract.md`
- `spec/compiler-pipeline.md`

Impact map:

Confirmed violations:
- Pending inspection.

Suspicious related locations:
- `rtsl/src/frontend/*`
- `rtsl/src/sema/*`
- `rtsl/src/ir/*`
- `rtsl/src/artifact/*`
- `tests/*`
- `docs/*`
- `spec/*`

Inspected locations ruled out:
- Pending inspection.

Pre-change invariants:
- Vertex and fragment path remains the release focus.
- Backend policy stays out of frontend/sema/generic IR.
- Public syntax and docs stay aligned.
- Cleanup must not introduce broad behavioral changes without tests.

Checklist:
- [ ] Spawn subagents with disjoint cleanup scopes.
- [ ] Inspect repository status and baseline build/test shape.
- [ ] Review agent changes before integration.
- [ ] Apply coherent local/integrated cleanup patches.
- [ ] Add or update focused tests when behavior changes.
- [ ] Run repository-defined verification.
- [ ] Repeat repository search for cleanup leftovers and special-case logic.
- [ ] Review final diff.
- [ ] Record verification results and final status.

Verification commands and results:
- Pending.

Final repository search:
- Pending.

Blockers:
- None currently.

Continuation notes:
- Main agent coordinates integration and owns final verification.
