# Codex Workspace Operation

Use this workflow for nontrivial RTSL tasks:

1. Read `AGENTS.md` and `.codex/TASK.md`.
2. Start the ledger with `pwsh -File scripts/task_init.ps1`.
3. Treat the reported location as a clue. Infer the general architectural rule,
   consult `docs/language.md`, `docs/backend-contract.md`, and
   `docs/compiler-architecture.md`, then populate the impact map before editing.
4. Keep checklist items and verification evidence current while working.
5. Run targeted verification with `pwsh -File scripts/verify.ps1 -Scope targeted`.
6. Run full release-path verification with `pwsh -File scripts/verify.ps1 -Scope full`.
7. Before claiming completion, mark the ledger `DONE` and run
   `pwsh -File scripts/task_validate.ps1`.

Completion validation is mechanically enforced only when the validator or
wrapper command is run. The installed Codex CLI reports version
`0.137.0-alpha.4` and exposes hook trust flags, but its local help and doctor
output do not document a repository stop-hook configuration format. No
undocumented hook file is configured here.

For unattended Codex runs, use a wrapper command that runs Codex and then the
validator, for example:

```powershell
codex exec -C . --sandbox workspace-write (Get-Content .codex/TASK_START_PROMPT.txt -Raw)
pwsh -File scripts/task_validate.ps1
```

To resume after context loss, read `.codex/TASK.md`, continue from the next
unchecked checklist item, and preserve the existing impact map and verification
history. To record a blocker, set status to `BLOCKED`, explain the exact
blocker under `## Blockers`, preserve evidence in verification or continuation
notes, and leave independent completed items checked.

If Codex repeats a recognizable failure mode, update `AGENTS.md` with the
smallest durable architectural rule and keep operational details in this file.
