# Codex PreToolUse policy guard.  Claude's original guard remains in .claude/.
$ErrorActionPreference = 'SilentlyContinue'
$raw = [Console]::In.ReadToEnd()
if (-not $raw) { exit 0 }
try { $evt = $raw | ConvertFrom-Json } catch { exit 0 }

function Deny([string]$reason) {
    @{ hookSpecificOutput = @{ hookEventName = 'PreToolUse'; permissionDecision = 'deny'; permissionDecisionReason = $reason } } |
        ConvertTo-Json -Compress
    exit 0
}

$tool = "$($evt.tool_name)"
$toolInput = $evt.tool_input

# Codex apply_patch provides the patch text rather than a Claude-style file_path.
if ($tool -eq 'apply_patch') {
    $patch = "$($toolInput.command)" -replace '\\', '/'
    if ($patch -match '(?m)^(\+\+\+|---)\s+(?:[ab]/)?(?:EnvironmentSimulator|OSMP_FMU)/') {
        Deny 'R1 Clean Core: editing EnvironmentSimulator/ or OSMP_FMU/ requires explicit user approval. Prefer GT_esmini/ extension points.'
    }
    exit 0
}

if ($tool -ne 'Bash') { exit 0 }
$cmd = "$($toolInput.command)"
if (-not $cmd) { exit 0 }

$isDiagnostic = $cmd -match '(?i)^\s*(where(\.exe)?|Get-Command|git|gh)\b'
$usesApproved = $cmd -match '(?i)(\.venv|venv[\\/]+Scripts|python-embed|emsdk)'
$invokesPython = $cmd -match '(?i)(^|[;&|(]|\s)(python3?|py|pip3?)(\.exe)?\s'
if ($invokesPython -and -not $usesApproved -and -not $isDiagnostic) {
    Deny 'System Python is forbidden. Use DriverScript/.venv/Scripts/python.exe or GT_esmini/web/.venv/Scripts/python.exe.'
}

$ghWrite = $cmd -match '(?i)\bgh\s+(pr|release|issue)\s+(create|merge|edit|delete|close|reopen|comment|upload|upload-asset)\b'
if ($ghWrite) {
    if ($cmd -notmatch '(?i)(\s-R[\s=]|--repo[\s=])') {
        Deny 'GitHub write operations must pin the fork: add -R GT-karny/esmini.'
    }
    if ($cmd -notmatch '(?i)(\s-R[\s=]+|--repo[\s=]+)[\x22\x27]?GT-karny/esmini\b') {
        Deny 'GitHub write operation does not target GT-karny/esmini. Obtain explicit user approval before a cross-repository write.'
    }
}
