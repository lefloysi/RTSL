param(
    [string]$TaskPath = ".codex/TASK.md",
    [string]$TemplatePath = ".codex/TASK_TEMPLATE.md"
)

$ErrorActionPreference = "Stop"

if (!(Test-Path -LiteralPath $TemplatePath)) {
    Write-Error "Task template not found: $TemplatePath"
}

Copy-Item -LiteralPath $TemplatePath -Destination $TaskPath -Force
Write-Host "Initialized task ledger: $TaskPath"
