param(
    [string]$AgentId = $env:RTSL_AGENT_ID,
    [string]$TaskPath = "",
    [string]$TemplatePath = ".codex/TASK_TEMPLATE.md",
    [switch]$Force
)

$ErrorActionPreference = "Stop"

function Get-SafeAgentId {
    param([string]$Value)

    if ([string]::IsNullOrWhiteSpace($Value)) {
        Write-Error "AgentId is required. Pass -AgentId <agent-id> or set RTSL_AGENT_ID."
    }

    if ($Value -notmatch "^[A-Za-z0-9][A-Za-z0-9._-]*$") {
        Write-Error "AgentId must start with a letter or digit and contain only letters, digits, '.', '_', or '-': $Value"
    }

    return $Value
}

if ([string]::IsNullOrWhiteSpace($TaskPath)) {
    $safeAgentId = Get-SafeAgentId $AgentId
    $TaskPath = ".codex/tasks/$safeAgentId.md"
}

if (!(Test-Path -LiteralPath $TemplatePath)) {
    Write-Error "Task template not found: $TemplatePath"
}

$taskDirectory = Split-Path -Parent $TaskPath
if (![string]::IsNullOrWhiteSpace($taskDirectory)) {
    New-Item -ItemType Directory -Force -Path $taskDirectory | Out-Null
}

if ((Test-Path -LiteralPath $TaskPath) -and !$Force) {
    Write-Error "Task ledger already exists: $TaskPath. Use -Force to reset it intentionally."
}

Copy-Item -LiteralPath $TemplatePath -Destination $TaskPath -Force:$Force
Write-Host "Initialized task ledger: $TaskPath"
