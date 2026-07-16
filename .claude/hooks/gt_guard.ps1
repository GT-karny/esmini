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

    # Rule 3: gh write operations must pin the repo AND the pinned repo must be
    # the fork. gh resolves the upstream parent (esmini/esmini) in fresh clones,
    # and issues/PRs created on the public upstream cannot be deleted afterwards
    # (only closed) — so both the missing flag and a non-fork value are guarded.
    $ghWrite = $cmd -match '(?i)\bgh\s+(pr|release|issue)\s+(create|merge|edit|delete|close|reopen|comment|upload|upload-asset)\b'
    if ($ghWrite) {
        $hasRepoFlag = $cmd -match '(?i)(\s-R[\s=]|--repo[\s=])'
        if (-not $hasRepoFlag) {
            Emit-Decision 'deny' ('gh write operations must specify the fork explicitly: add -R GT-karny/esmini (gh may resolve the upstream esmini/esmini parent repo in fresh clones).')
        }
        if ($cmd -notmatch '(?i)(\s-R[\s=]+|--repo[\s=]+)[\x22\x27]?GT-karny/esmini\b') {
            Emit-Decision 'ask' ('gh write operation targets a repo other than the fork GT-karny/esmini. Issues/PRs on the public upstream (esmini/esmini) cannot be deleted once created, only closed. Approve only if this cross-repo write is deliberate (e.g. an intentional upstream bug report).')
        }
    }

    # Rule 3b: gh api can mutate any repo and bypasses the verb matcher above.
    # A mutating gh api call that mentions the upstream repo requires approval.
    $ghApi = $cmd -match '(?i)\bgh\s+api\b'
    $apiMutates = $cmd -match '(?i)((-X|--method)[\s=]+(POST|PATCH|PUT|DELETE)|\s-[fF]\s|--(raw-)?field[\s=]|--input[\s=])'
    if ($ghApi -and $apiMutates -and $cmd -match '(?i)esmini/esmini') {
        Emit-Decision 'ask' ('Mutating gh api call referencing upstream esmini/esmini. Approve only if writing to the upstream repo is deliberate.')
    }

    # Rule 5: issue filing carries knowledge-graph context (CLAUDE.md R4).
    # gh issue create must cite at least one namespaced ID (slug:local-id) in
    # its body so the issue is connected to the graph from birth. Inline
    # --body text is part of the command string; --body-file contents are read
    # from disk. Registered slugs come from namespaces.yaml (single source of
    # truth). 'ask', not 'deny'.
    if ($cmd -match '(?i)\bgh\s+issue\s+create\b') {
        $searchText = $cmd
        if ($cmd -match '(?i)--body-file[\s=]+[\x22\x27]?([^\x22\x27\s]+)') {
            $bf = $Matches[1]
            if (Test-Path $bf) { $searchText += "`n" + (Get-Content -Raw $bf) }
        }
        $nsFile = Join-Path $PSScriptRoot '..\..\GT_esmini\docs\knowledge\namespaces.yaml'
        $slugs = @(Select-String -Path $nsFile -Pattern '^\s*- slug:\s*(\S+)' |
                   ForEach-Object { $_.Matches[0].Groups[1].Value })
        if ($slugs.Count -gt 0) {
            $slugAlt = ($slugs | ForEach-Object { [regex]::Escape($_) }) -join '|'
            if ($searchText -notmatch "\b($slugAlt):[A-Za-z0-9_#]") {
                Emit-Decision 'ask' ('Knowledge-graph workflow (R4): the issue body cites no namespaced knowledge-graph ID (e.g. feature:F2, policy:conflict, audit-debt:CTL-3, commit:<sha>). Add related IDs so the issue is connected to the graph (see /kg), or approve to file it unconnected.')
            }
        }
    }

    # Rule 4: knowledge-graph workflow (CLAUDE.md R4). Commits should cite at
    # least one knowledge-graph ID so the commit->ID edge is extractable
    # (scripts/check_knowledge_graph.py --extract-commits). Only enforceable
    # when the message is inline (-m / here-string); wip / merge / fixup /
    # --amend are exempt. 'ask' not 'deny': ID-less commits are legitimate
    # for work with genuinely no related ID -- the user decides.
    $isCommitMsg = ($cmd -match '(?i)\bgit\b[^|;&]*\bcommit\b') -and ($cmd -match '(?i)(\s-m\b|--message\b)')
    $isExempt = $cmd -match '(?i)(\bwip\b|fixup!|squash!|\bmerge\b|--amend)'
    $hasKgId = $cmd -match '(\bF[1-6]\b|\bR[0-5](-U[1-4])?\b|\bPhase ?[0-4][a-e]?\b|\b(CTL|SUB|VD|CORE|WEB|FE|SCR|BLD|TST|BND|MSC|Critic|GT|PY|XSD|ES)-[0-9]+\b|\b(proposal|plan)\s+P[0-9]+\b|#[0-9]+|\bGT_ODR\b|\bGT_LHT\b)'
    if ($isCommitMsg -and -not $isExempt -and -not $hasKgId) {
        # Unified workflow (R4): consult path_map via --suggest before asking.
        # verdict exempt -> silent pass; mapped -> ask WITH candidate IDs;
        # unknown / suggest unavailable -> generic ask (fail open to ask).
        $reason = 'Knowledge-graph workflow (R4): the commit message cites no related ID -- e.g. (F6), (SUB-1), (proposal P13), fixes #30. Cite one so the commit->ID edge is machine-extractable (see /kg), or approve to commit without a reference.'
        $py = Join-Path $PSScriptRoot '..\..\DriverScript\.venv\Scripts\python.exe'
        $kg = Join-Path $PSScriptRoot '..\..\scripts\check_knowledge_graph.py'
        if ((Test-Path $py) -and (Test-Path $kg)) {
            $sug = & $py $kg --suggest 2>$null
            if ($sug -match '(?m)^verdict:\s*exempt') { exit 0 }
            $idsLine = ($sug | Select-String '^ids:' | Select-Object -First 1)
            if ($idsLine) {
                $cand = ($idsLine.Line -replace '^ids:\s*', '')
                $reason = "Knowledge-graph workflow (R4): no ID cited, but the changed paths map to candidates: $cand (from knowledge/path_map.yaml). Cite the applicable one, or approve to commit without a reference."
            }
        }
        Emit-Decision 'ask' $reason
    }

    exit 0
}

exit 0
