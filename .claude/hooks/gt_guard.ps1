# gt_guard.ps1 - GT_esmini PreToolUse guard hook (project policy)
#
# Enforces, deterministically, three rules that CLAUDE.md states but memory
# alone cannot guarantee:
#   1. R1 Clean Core  : direct edits under EnvironmentSimulator/ or OSMP_FMU/
#                       require explicit user approval (fork budget discipline).
#   2. venv policy    : bare python/pip/py invocations are rejected with a
#                       pointer to the project venvs (CLAUDE.md section 4).
#   3. gh repo safety : gh write operations without -R/--repo are rejected so
#                       nothing is ever created on upstream esmini/esmini.
#
# Contract: reads the PreToolUse JSON payload on stdin; on a rule hit prints a
# hookSpecificOutput permissionDecision JSON and exits 0. Silent exit 0 = no
# opinion (normal permission flow applies). Never exits nonzero on bad input.

$ErrorActionPreference = 'SilentlyContinue'

$raw = [Console]::In.ReadToEnd()
if (-not $raw) { exit 0 }
try { $evt = $raw | ConvertFrom-Json } catch { exit 0 }
if (-not $evt) { exit 0 }

function Emit-Decision {
    param([string]$Decision, [string]$Reason)
    $payload = @{
        hookSpecificOutput = @{
            hookEventName            = 'PreToolUse'
            permissionDecision       = $Decision
            permissionDecisionReason = $Reason
        }
    }
    [Console]::Out.WriteLine(($payload | ConvertTo-Json -Depth 5 -Compress))
    exit 0
}

$tool = "$($evt.tool_name)"

# ---- Rule 1: R1 Clean Core (file-editing tools) ----------------------------
if ($tool -match '^(Edit|Write|MultiEdit|NotebookEdit)$') {
    $p = "$($evt.tool_input.file_path)" -replace '\\', '/'
    if ($p -match '(^|/)(EnvironmentSimulator|OSMP_FMU)/') {
        Emit-Decision 'ask' ('R1 Clean Core: this file is inside the pristine upstream core (EnvironmentSimulator/ or OSMP_FMU/). Prefer GT_esmini/ extension points or the documented fork/resync flow (docs/odr_resync_checklist.md, fork budget ledger). Approve only if this upstream edit is intentional.')
    }
    exit 0
}

# ---- Rules 2 & 3: shell command tools ---------------------------------------
if ($tool -match '^(Bash|PowerShell)$') {
    $cmd = "$($evt.tool_input.command)"
    if (-not $cmd) { exit 0 }

    # Rule 2: system Python is forbidden (CLAUDE.md section 4).
    # Skip commands that cannot start a Python process or that reference an
    # approved interpreter (venvs, embedded python, emsdk toolchain).
    $isDiagnostic = $cmd -match '(?i)^\s*(where(\.exe)?|Get-Command|git|gh)\b'
    $usesApproved = $cmd -match '(?i)(\.venv|venv[\\/]+Scripts|python-embed|emsdk)'
    $invokesPython = $cmd -match '(?i)(^|[;&|(]|\s)(python3?|py|pip3?)(\.exe)?\s'
    if ($invokesPython -and -not $usesApproved -and -not $isDiagnostic) {
        Emit-Decision 'deny' ('System Python is forbidden in this repo (CLAUDE.md section 4). Use DriverScript/.venv/Scripts/python.exe (verification/scripts) or GT_esmini/web/.venv/Scripts/python.exe (web/packaging) instead.')
    }

    # Rule 3: gh write operations must pin the repo. gh resolves the upstream
    # parent (esmini/esmini) in fresh clones; -R GT-karny/esmini is the safety
    # belt even though `gh repo set-default` was run in this clone.
    $ghWrite = $cmd -match '(?i)\bgh\s+(pr|release|issue)\s+(create|merge|edit|delete|close|reopen|comment|upload|upload-asset)\b'
    $hasRepoFlag = $cmd -match '(?i)(\s-R\s|--repo[\s=])'
    if ($ghWrite -and -not $hasRepoFlag) {
        Emit-Decision 'deny' ('gh write operations must specify the fork explicitly: add -R GT-karny/esmini (gh may resolve the upstream esmini/esmini parent repo in fresh clones).')
    }

    exit 0
}

exit 0
