param(
    [string]$AgentId = $env:RTSL_AGENT_ID,
    [string]$TaskPath = ""
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

if (!(Test-Path -LiteralPath $TaskPath)) {
    Write-Error "Task ledger not found: $TaskPath"
}

$text = Get-Content -LiteralPath $TaskPath -Raw
$failures = [System.Collections.Generic.List[string]]::new()

if ($text -notmatch "(?m)^# Status:\s*(\S+)\s*$") {
    $failures.Add("missing '# Status: <STATE>' header")
} else {
    $status = $Matches[1]
    $allowed = @("IDLE", "PLANNING", "IN_PROGRESS", "VERIFYING", "BLOCKED", "DONE")
    if ($allowed -notcontains $status) {
        $failures.Add("unknown status '$status'")
    }
}

function Get-Section {
    param([string]$Text, [string]$Heading)
    $pattern = "(?ms)^## $([regex]::Escape($Heading))\s*(.*?)(?=^## |\z)"
    if ($Text -match $pattern) {
        return $Matches[1].Trim()
    }
    return $null
}

function Require-NonEmpty {
    param([string]$Name, [string]$Value)
    if ([string]::IsNullOrWhiteSpace($Value)) {
        $script:failures.Add("$Name is empty")
    }
}

if ($failures.Count -eq 0) {
    if ($status -eq "IDLE") {
        Write-Host "Task ledger is IDLE."
        exit 0
    }

    $blockers = Get-Section $text "Blockers"
    $continuation = Get-Section $text "Continuation Notes"

    if ($status -eq "BLOCKED") {
        Require-NonEmpty "Blockers" $blockers
        Require-NonEmpty "Continuation Notes" $continuation
    } elseif ($status -eq "DONE") {
        if ($text -match "(?m)^\s*-\s*\[\s\]") {
            $failures.Add("unchecked checklist items remain")
        }
        Require-NonEmpty "Verification Commands" (Get-Section $text "Verification")
        if ((Get-Section $text "Verification") -notmatch "(?ms)^### Results\s+\S") {
            $failures.Add("verification results are empty")
        }
        Require-NonEmpty "Final Search" (Get-Section $text "Final Search")
        if (![string]::IsNullOrWhiteSpace($blockers)) {
            $failures.Add("blockers must be empty for DONE")
        }
        Require-NonEmpty "Continuation Notes" $continuation
    } else {
        $failures.Add("status is '$status', not DONE, BLOCKED, or IDLE")
    }
}

if ($failures.Count -ne 0) {
    foreach ($failure in $failures) {
        Write-Error $failure -ErrorAction Continue
    }
    exit 1
}

Write-Host "Task ledger validation passed for status $status."
