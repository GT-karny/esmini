<#
.SYNOPSIS
    Full GT_esmini pre-merge regression gate (for humans AND Claude sessions).

.DESCRIPTION
    Two-step gate that should pass before merging GT_esmini changes:

      Step 1 - Unit + integration tests (HARD gate)
               Delegates to scripts/run_gt_tests.ps1, which runs the GT ctest
               suite (test_ScenarioReaderParsing, test_PythonDriverBridge,
               GT_esmini_Integration_*). A failure here fails the whole gate.

      Step 1.5 - OpenDRIVE conformance, quick profile (HARD gate)
               Runs scripts/run_odr_conformance.py --profile quick (schema layer
               + esminiRMLib RM probe layer + XFAIL/XPASS semantics; exit 0 iff
               no FAIL/XPASS). Uses the same verification venv as Step 2. Official
               ASAM fixtures auto-SKIP when the thirdparty zips are absent; the RM
               layer needs the built esminiRMLib.dll. A nonzero exit fails the
               gate. Skip with -SkipOdr.

      Step 2 - VirtualDriver behavioral batch (reported gate, skippable)
               Runs the gt_sim_test phase-3 traffic-policy batch in-process via
               the GT C-API (GT_esminiLib.dll) and reports the verdict:

                 python GT_esmini/scripts/verification/gt_sim_test.py batch \
                     resources/xosc/verification/phase3_batch.yaml \
                     --out <OutDir>

               Step 2 REQUIRES a completed Release build: the harness loads
               build/GT_esmini/Release/GT_esminiLib.dll (gt_lib.py DEFAULT_DLL).
               It also needs a Python venv with pyyaml + osi3 (+ matplotlib for
               keyframe PNGs). DriverScript/.venv satisfies all of these and is
               the documented verification venv (verification_environment.md
               2.4.1); GT_esmini/web/.venv works too but lacks matplotlib.

               The batch verdict is SURFACED but, by default, treated as a
               WARNING rather than a hard failure: phase3_batch.yaml is the
               evolving behavioral check and its header documents that some
               discriminating cases are expected to fail at certain stages.
               Use -FailOnBehavioral to make a behavioral 'fail' fail the gate
               (recommended once Phase 3a-c is considered locked).

.PARAMETER Config
    Build configuration for Step 1 ctest. Default: Release.

.PARAMETER BuildDir
    CMake build directory. Default: build.

.PARAMETER SkipOdr
    Skip Step 1.5 (OpenDRIVE conformance, quick profile) entirely.

.PARAMETER SkipBehavioral
    Skip Step 2 entirely (e.g. when no Release build / venv is available).

.PARAMETER FailOnBehavioral
    Make a Step 2 behavioral 'fail' verdict fail the whole gate. Without this,
    Step 2 failures are reported as warnings and do not change the exit code.

.PARAMETER Python
    Path to the Python interpreter for Step 2. Default: auto-detect
    DriverScript/.venv then GT_esmini/web/.venv.

.PARAMETER Batch
    Path to the gt_sim_test batch manifest. Default:
    resources/xosc/verification/phase3_batch.yaml.

.PARAMETER OutDir
    Output directory for batch artifacts. Default: test_results/regression/phase3.

.PARAMETER Dll
    Optional GT_esminiLib.dll override passed to gt_sim_test (use when your build
    is not at build/GT_esmini/Release).

.EXAMPLE
    pwsh scripts/run_regression_gate.ps1
    pwsh scripts/run_regression_gate.ps1 -SkipBehavioral
    pwsh scripts/run_regression_gate.ps1 -SkipOdr
    pwsh scripts/run_regression_gate.ps1 -FailOnBehavioral
#>
[CmdletBinding()]
param(
    [string]$Config = "Release",
    [string]$BuildDir = "build",
    [switch]$SkipOdr,
    [switch]$SkipBehavioral,
    [switch]$FailOnBehavioral,
    [string]$Python = "",
    [string]$Batch = "resources/xosc/verification/phase3_batch.yaml",
    [string]$OutDir = "test_results/regression/phase3",
    [string]$Dll = ""
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
function Resolve-RepoPath([string]$p) {
    if ([System.IO.Path]::IsPathRooted($p)) { return $p }
    return (Join-Path $repoRoot $p)
}

$overallOk = $true

# ----------------------------------------------------------------------------
# Step 1 - Unit + integration tests (HARD gate)
# ----------------------------------------------------------------------------
Write-Host "==== Step 1/2: GT unit + integration tests (ctest) ====" -ForegroundColor Cyan
$gtScript = Join-Path $PSScriptRoot "run_gt_tests.ps1"
# Dot-invoke via the current PowerShell engine (works under pwsh 7 and Windows
# PowerShell 5.1 alike); the child script calls `exit`, so run it in a child
# scope by spawning the same host to preserve our own session.
$psHost = (Get-Process -Id $PID).Path
& $psHost -NoProfile -File $gtScript -Config $Config -BuildDir $BuildDir
$step1 = $LASTEXITCODE
if ($step1 -ne 0) {
    Write-Host "Step 1: FAIL (exit $step1)" -ForegroundColor Red
    $overallOk = $false
} else {
    Write-Host "Step 1: PASS" -ForegroundColor Green
}

# ----------------------------------------------------------------------------
# Step 1.5 - OpenDRIVE conformance, quick profile (HARD gate)
# ----------------------------------------------------------------------------
if ($SkipOdr) {
    Write-Host "==== Step 1.5: OpenDRIVE conformance (quick) - SKIPPED (-SkipOdr) ====" -ForegroundColor Yellow
} else {
    Write-Host "==== Step 1.5: OpenDRIVE conformance (quick) ====" -ForegroundColor Cyan

    # Resolve the verification venv python the same way Step 2 does (needs
    # xmlschema/lxml/pyyaml; DriverScript/.venv satisfies these).
    $odrPy = $Python
    if ([string]::IsNullOrWhiteSpace($odrPy)) {
        foreach ($cand in @("DriverScript/.venv/Scripts/python.exe",
                            "GT_esmini/web/.venv/Scripts/python.exe")) {
            $full = Resolve-RepoPath $cand
            if (Test-Path $full) { $odrPy = $full; break }
        }
    }

    $odrHarness = Resolve-RepoPath "scripts/run_odr_conformance.py"

    if ([string]::IsNullOrWhiteSpace($odrPy) -or -not (Test-Path $odrPy)) {
        Write-Host "Step 1.5: FAIL - verification venv python not found (DriverScript/.venv or GT_esmini/web/.venv)" -ForegroundColor Red
        Write-Host "    (create the venv, pass -Python, or skip with -SkipOdr)" -ForegroundColor Yellow
        $overallOk = $false
    } else {
        Write-Host "Step 1.5: $odrPy $odrHarness --profile quick" -ForegroundColor Cyan
        & $odrPy $odrHarness --profile quick
        $step15 = $LASTEXITCODE
        if ($step15 -ne 0) {
            Write-Host "Step 1.5: FAIL (exit $step15)" -ForegroundColor Red
            $overallOk = $false
        } else {
            Write-Host "Step 1.5: PASS" -ForegroundColor Green
        }
    }
}

# ----------------------------------------------------------------------------
# Step 2 - VirtualDriver behavioral batch (reported gate, skippable)
# ----------------------------------------------------------------------------
if ($SkipBehavioral) {
    Write-Host "==== Step 2/2: VirtualDriver behavioral batch - SKIPPED (-SkipBehavioral) ====" -ForegroundColor Yellow
} else {
    Write-Host "==== Step 2/2: VirtualDriver behavioral batch (gt_sim_test) ====" -ForegroundColor Cyan

    # Resolve the verification venv python: prefer the documented DriverScript
    # venv (has pyyaml+osi3+matplotlib), fall back to the web venv.
    $pyExe = $Python
    if ([string]::IsNullOrWhiteSpace($pyExe)) {
        foreach ($cand in @("DriverScript/.venv/Scripts/python.exe",
                            "GT_esmini/web/.venv/Scripts/python.exe")) {
            $full = Resolve-RepoPath $cand
            if (Test-Path $full) { $pyExe = $full; break }
        }
    }

    $dllPath = Resolve-RepoPath "build/GT_esmini/$Config/GT_esminiLib.dll"
    if (-not [string]::IsNullOrWhiteSpace($Dll)) { $dllPath = Resolve-RepoPath $Dll }

    $batchPath = Resolve-RepoPath $Batch
    $outPath = Resolve-RepoPath $OutDir
    $harness = Resolve-RepoPath "GT_esmini/scripts/verification/gt_sim_test.py"

    $missing = @()
    if ([string]::IsNullOrWhiteSpace($pyExe) -or -not (Test-Path $pyExe)) {
        $missing += "verification venv python (DriverScript/.venv or GT_esmini/web/.venv)"
    }
    if (-not (Test-Path $dllPath)) {
        $missing += "GT_esminiLib.dll at $dllPath (requires a completed $Config build)"
    }
    if (-not (Test-Path $batchPath)) { $missing += "batch manifest $batchPath" }

    if ($missing.Count -gt 0) {
        Write-Host "Step 2: SKIPPED - prerequisites missing:" -ForegroundColor Yellow
        foreach ($m in $missing) { Write-Host "    - $m" -ForegroundColor Yellow }
        Write-Host "    (build Release, or pass -SkipBehavioral / -Python / -Dll)" -ForegroundColor Yellow
    } else {
        $argList = @($harness, "batch", $batchPath, "--out", $outPath)
        if (-not [string]::IsNullOrWhiteSpace($Dll)) { $argList += @("--dll", $dllPath) }
        Write-Host "Step 2: $pyExe $($argList -join ' ')" -ForegroundColor Cyan
        & $pyExe @argList
        $step2 = $LASTEXITCODE

        # gt_sim_test batch returns 0 for overall in {pass, needs-review}, 1 for
        # overall=fail. Read batch_verdict.json for the precise summary if present.
        $verdictFile = Join-Path $outPath "batch_verdict.json"
        $verdictText = "(no batch_verdict.json)"
        if (Test-Path $verdictFile) {
            try {
                $v = Get-Content $verdictFile -Raw | ConvertFrom-Json
                $s = $v.summary
                $verdictText = "overall=$($v.overall) (pass=$($s.pass) fail=$($s.fail) needs-review=$($s.'needs-review') error=$($s.error))"
            } catch { $verdictText = "(could not parse batch_verdict.json)" }
        }

        if ($step2 -eq 0) {
            Write-Host "Step 2: PASS  $verdictText" -ForegroundColor Green
        } else {
            if ($FailOnBehavioral) {
                Write-Host "Step 2: FAIL  $verdictText" -ForegroundColor Red
                $overallOk = $false
            } else {
                Write-Host "Step 2: WARN (behavioral fail, not gating)  $verdictText" -ForegroundColor Yellow
                Write-Host "    Pass -FailOnBehavioral to make this fail the gate." -ForegroundColor Yellow
            }
        }
    }
}

# ----------------------------------------------------------------------------
# Summary
# ----------------------------------------------------------------------------
Write-Host "============================================================"
if ($overallOk) {
    Write-Host "REGRESSION GATE: PASS" -ForegroundColor Green
    exit 0
} else {
    Write-Host "REGRESSION GATE: FAIL" -ForegroundColor Red
    exit 1
}
