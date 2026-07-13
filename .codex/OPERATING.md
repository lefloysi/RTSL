# Codex Workspace Operation

Use this workflow for nontrivial RTSL tasks:

1. Read `AGENTS.md`.
2. Pick a stable agent id for this worker. Use a short id that distinguishes
   concurrent agents, such as `stage-identifiers`, `codex-a`, or a
   thread/worktree id.
3. Start the agent-scoped ledger with
   `pwsh -File scripts/task_init.ps1 -AgentId <agent-id>`.
   The ledger path is `.codex/tasks/<agent-id>.md`. If `-AgentId` is omitted,
   scripts use `$env:RTSL_AGENT_ID`; one of them must be set.
4. Treat the reported location as a clue. Infer the general architectural rule,
   consult `docs/language.md`, `docs/backend-contract.md`, and
   `docs/compiler-architecture.md`, then populate the impact map before editing.
5. Keep checklist items and verification evidence current while working.
6. Run targeted verification with `pwsh -File scripts/verify.ps1 -Scope targeted`.
7. Run full release-path verification with `pwsh -File scripts/verify.ps1 -Scope full`.
8. Before claiming completion, mark the ledger `DONE` and run
   `pwsh -File scripts/task_validate.ps1 -AgentId <agent-id>`.

Completion validation is mechanically enforced only when the validator or
wrapper command is run. The installed Codex CLI reports version
`0.137.0-alpha.4` and exposes hook trust flags, but its local help and doctor
output do not document a repository stop-hook configuration format. No
undocumented hook file is configured here.

For unattended Codex runs, use a wrapper command that runs Codex and then the
validator, for example:

```powershell
$env:RTSL_AGENT_ID = "codex-a"
codex exec -C . --sandbox workspace-write (Get-Content .codex/TASK_START_PROMPT.txt -Raw)
pwsh -File scripts/task_validate.ps1 -AgentId $env:RTSL_AGENT_ID
```

To resume after context loss, read `.codex/tasks/<agent-id>.md`, continue from
the next unchecked checklist item, and preserve the existing impact map and
verification history. To record a blocker, set status to `BLOCKED`, explain the
exact blocker under `## Blockers`, preserve evidence in verification or
continuation notes, and leave independent completed items checked.

If Codex repeats a recognizable failure mode, update `AGENTS.md` with the
smallest durable architectural rule and keep operational details in this file.
