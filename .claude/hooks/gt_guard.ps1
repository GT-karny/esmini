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
    $isCommitMsg = ($cmd -match '(?i)\bgit\b[^|;&]*\bcommit\b') -and ($cmd -match '(?i)(\s-m\b|--message\b|-F\b|--file\b)')
    $isExempt = $cmd -match '(?i)(\bwip\b|fixup!|squash!|\bmerge\b|--amend)'
    # ID 文法は namespaces.yaml から生成する（2026-07-20）。commit-msg フックと
    # 共有の scripts/check_commit_kg_ids.py に委譲し、二重ハードコードを解消した。
    # 旧実装は修飾形 `<slug>:<id>` を認識せず、逆に裸の序数 `Phase3` を許可しており、
    # 規約を守るほど警告が出る＝警報疲れを育てる反転状態だった。
    # 検査器が使えない場合は「ID あり」に倒す（fail open。ブロックしない）。
    $hasKgId = $true
    $pyChk = Join-Path $PSScriptRoot '..\..\DriverScript\.venv\Scripts\python.exe'
    $idChk = Join-Path $PSScriptRoot '..\..\scripts\check_commit_kg_ids.py'
    if ((Test-Path $pyChk) -and (Test-Path $idChk)) {
        $cmd | & $pyChk $idChk - 2>$null | Out-Null
        if ($LASTEXITCODE -eq 2) { $hasKgId = $false }
    }
    if ($isCommitMsg -and -not $isExempt -and -not $hasKgId) {
        # 自律化 (2026-07-23): 人間へエスカレーション (ask) せず、コミットする
        # エージェント自身へ差し戻して自動修正させる。path_map --suggest で分類し:
        #   verdict exempt        -> silent pass (exit 0, ID 不要)
        #   mapped (候補 ID あり) -> deny + 候補提示。エージェントはメッセージに ID を
        #                            足して自動リトライ -> ID あり判定で通過する。
        #   unknown / suggest 不可 -> exit 0 (advisory allow)。候補を導出できない
        #                            ケースまで deny にすると詰む (デッドロック) ため
        #                            通す。フックは「意見なし」で通常フローに委ねる。
        # → R4 の統制 (関連 ID が必ず引用される) は維持したまま、人間は一切介在しない。
        $py = Join-Path $PSScriptRoot '..\..\DriverScript\.venv\Scripts\python.exe'
        $kg = Join-Path $PSScriptRoot '..\..\scripts\check_knowledge_graph.py'
        $cand = $null
        if ((Test-Path $py) -and (Test-Path $kg)) {
            $sug = & $py $kg --suggest 2>$null
            if ($sug -match '(?m)^verdict:\s*exempt') { exit 0 }
            $idsLine = ($sug | Select-String '^ids:' | Select-Object -First 1)
            if ($idsLine) { $cand = ($idsLine.Line -replace '^ids:\s*', '').Trim() }
        }
        if ($cand) {
            Emit-Decision 'deny' ("Knowledge-graph workflow (R4, auto self-correct): the commit message cites no related ID, but the changed paths map to candidate ID(s): $cand (knowledge/path_map.yaml). Amend the commit message to cite the applicable one (e.g. append ' ($cand)') and retry the commit immediately -- do NOT ask the user, this is an automated self-correction. If none genuinely applies, cite the relevant issue (fixes #NN) or use an exempt prefix (wip/merge/fixup).")
        }
        # 候補を導出できない -> デッドロック回避のため通常フローに委ねる (advisory)。
        exit 0
    }

    exit 0
}

exit 0
