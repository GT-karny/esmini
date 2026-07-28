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
                     resources/xosc/verification/car_following_traffic_control_batch.yaml \
                     --out <OutDir>

               Step 2 REQUIRES a completed Release build: the harness loads
               build/GT_esmini/Release/GT_esminiLib.dll (gt_lib.py DEFAULT_DLL).
               It also needs a Python venv with pyyaml + osi3 (+ matplotlib for
               keyframe PNGs). DriverScript/.venv satisfies all of these and is
               the documented verification venv (verification_environment.md
               2.4.1); GT_esmini/web/.venv works too but lacks matplotlib.

               The batch runs, then scripts/check_regression_baseline.py compares
               the per-scenario/per-matcher verdict against the COMMITTED baseline
               (GT_esmini/test/regression_baseline/car_following_traffic_control_expected.yaml). This is
               PRECISE: the two discriminating cases that fail by design at the
               current stage are recorded in the baseline and do NOT trip the
               gate -- only a real DEVIATION (a new fail, or a known fail turning
               pass) does. The batch's own exit code is intentionally NOT the gate
               (it is 1 whenever overall=fail, which the phase3 batch does by
               design). A per-scenario deviation is SURFACED but, by default,
               treated as a WARNING; use -FailOnBehavioral to make it fail the
               gate (recommended once Phase 3a-c is considered locked). After an
               INTENTIONAL behavior change, refresh the baseline with:
                 check_regression_baseline.py --batch-out <OutDir> --update

               WARN covers exactly one thing: "measured, and it differs".
               NOT MEASURED is always a HARD failure and -FailOnBehavioral has
               no say in it -- a scenario that errored, a batch that wrote no
               readable verdict, and a whole gate run that drove zero scenarios
               are all failures. This is not theoretical: on 2026-07-27 the
               packaged app held the listener ports, all 22 scenarios died on
               WinError 10013, and the gate printed PASS. A port preflight now
               refuses to start in that situation, and the summary refuses to
               call a zero-scenario run green whatever the cause (held ports,
               missing build, empty manifest).

      Step 2.6 - AEB safety batch (reported gate, skippable)
               Same recipe as Step 2 (shared Invoke-BehavioralBatch) on a
               SEPARATE manifest and baseline:

                 python GT_esmini/scripts/verification/gt_sim_test.py batch \
                     resources/xosc/verification/aeb_safety_batch.yaml \
                     --out <AebOutDir>
                 python scripts/check_regression_baseline.py \
                     --batch-out <AebOutDir> --baseline <AebBaseline>

               Covers the AEB safety tier in BOTH directions: REQ-AD-001
               (mitigation on an unavoidable cut-in + hard brake) and REQ-AD-013
               (no emergency braking when nothing is on a collision course).
               Both directions matter -- gating on only one of them lets a
               regression trade one for the other.

               Kept apart from Step 2 so a red says WHICH claim broke, and so
               Step 2's recorded known-red cannot mask an AEB regression.
               Currently WARN by default like Step 2 (-FailOnBehavioral makes
               both hard); the intent is to promote AEB to hard once it has a
               few green runs on record.

.PARAMETER Config
    Build configuration for Step 1 ctest. Default: Release.

.PARAMETER BuildDir
    CMake build directory. Default: build.

.PARAMETER SkipOdr
    Skip Step 1.5 (OpenDRIVE conformance, quick profile) entirely.

.PARAMETER SkipBehavioral
    Skip Step 2 entirely (e.g. when no Release build / venv is available).

.PARAMETER FailOnBehavioral
    Make a Step 2 per-scenario baseline DEVIATION fail the whole gate. Without
    this, a deviation is reported as a warning and does not change the exit code.
    (The meaning sharpened in F4: it now gates on a precise per-scenario deviation
    vs the committed baseline, not on the batch's coarse overall=fail.)

.PARAMETER Baseline
    Committed per-scenario expectation baseline compared by
    scripts/check_regression_baseline.py. Default:
    GT_esmini/test/regression_baseline/car_following_traffic_control_expected.yaml.

.PARAMETER SkipAeb
    Skip Step 2.6 (AEB safety batch) only, leaving Step 2 in place.

.PARAMETER AebBatch
    Path to the AEB safety batch manifest for Step 2.6. Default:
    resources/xosc/verification/aeb_safety_batch.yaml.

.PARAMETER AebOutDir
    Output directory for Step 2.6 batch artifacts. Default:
    test_results/regression/aeb_safety.

.PARAMETER AebBaseline
    Committed baseline for Step 2.6. Default:
    GT_esmini/test/regression_baseline/aeb_safety_expected.yaml.

.PARAMETER TelemetryGolden
    OPTIONAL (P6 S0 oracle; default OFF keeps the gate byte-compatible). After
    Step 2, run scripts/telemetry_golden.py diff for the phase3 batch (and the
    catalog batch when GT_esmini/test/telemetry_goldens/catalog exists) and
    treat any per-frame telemetry deviation beyond the same-build noise floor
    as a HARD gate failure. This is the continuous value-level motion-invariance
    check for VJ stages S4-S6.

.PARAMETER Python
    Path to the Python interpreter for Step 2. Default: auto-detect
    DriverScript/.venv then GT_esmini/web/.venv.

.PARAMETER Batch
    Path to the gt_sim_test batch manifest. Default:
    resources/xosc/verification/car_following_traffic_control_batch.yaml.

.PARAMETER OutDir
    Output directory for batch artifacts. Default: test_results/regression/car_following_traffic_control.

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
    [switch]$TelemetryGolden,
    [string]$Python = "",
    [string]$Batch = "resources/xosc/verification/car_following_traffic_control_batch.yaml",
    [string]$OutDir = "test_results/regression/car_following_traffic_control",
    [string]$Baseline = "GT_esmini/test/regression_baseline/car_following_traffic_control_expected.yaml",
    [switch]$SkipAeb,
    [string]$AebBatch = "resources/xosc/verification/aeb_safety_batch.yaml",
    [string]$AebOutDir = "test_results/regression/aeb_safety",
    [string]$AebBaseline = "GT_esmini/test/regression_baseline/aeb_safety_expected.yaml",
    [switch]$SkipAnticipation,
    [string]$AntBatch = "resources/xosc/verification/anticipation_driving_batch.yaml",
    [string]$AntOutDir = "test_results/regression/anticipation_driving",
    [string]$AntBaseline = "GT_esmini/test/regression_baseline/anticipation_driving_expected.yaml",
    [switch]$SkipHandoff,
    [string]$HandoffBatch = "resources/xosc/verification/scenario_handoff_batch.yaml",
    [string]$HandoffOutDir = "test_results/regression/scenario_handoff",
    [string]$HandoffBaseline = "GT_esmini/test/regression_baseline/scenario_handoff_expected.yaml",
    [string]$Dll = ""
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
function Resolve-RepoPath([string]$p) {
    if ([System.IO.Path]::IsPathRooted($p)) { return $p }
    return (Join-Path $repoRoot $p)
}

$overallOk = $true
# Coverage accounting for the behavioural steps (2 / 2.6 / 2.7). These ACCUMULATE
# across every batch: an earlier version assigned instead of adding, so the final
# summary line reported only the LAST batch (5/5) while 22 scenarios had run --
# the same class of under-reporting as the bug this whole block exists to prevent.
$script:BehavioralRan      = 0      # scenarios that produced a verdict
$script:BehavioralTotal    = 0      # scenarios attempted (ran + errored)
$script:BehavioralMeasured = $false # at least one batch yielded parseable counts

# ----------------------------------------------------------------------------
# Port preflight -- refuse to run rather than produce a fake result
#
# 2026-07-27: the packaged app was left running while this gate was started.
# Its gt_sim_web held the listener ports, all 22 scenarios died on WinError
# 10013, and the gate printed PASS. The user-facing workflow now EXPECTS the
# packaged app to be running while gates are executed, so this collision is
# routine rather than exceptional.
#
# Two distinct hazards are checked here, and BOTH are hard aborts:
#
#   (a) COLLISION -- the gate itself binds the port. gt_sim_test.py binds UDP
#       48198 as its in-process OSI receiver (gt_sim_test.py:167-171); that is
#       the exact socket that raised WinError 10013 on 2026-07-27.
#
#   (b) CONTAMINATION -- the gate SENDS to the port unconditionally and someone
#       else is listening. GT_VirtualDriverReporter is initialised with no
#       condition even under --headless (GT_esminiLib.cpp:1391-1393) and there
#       is no switch to suppress the send, so a packaged app listening on 48202
#       records OUR frames into the USER's telemetry.jsonl. Neither side errors.
#       Refusing to start is currently the only defence (audit
#       test_results/f7_audit_port_contention.md P-1); note that occupancy
#       detection does NOT make UDP sending safe in general -- a send succeeds
#       with no listener at all -- it only covers the case where the collision
#       is observable up front.
#
# 9100 is the manual-drive / VirtualDriver network input port, bound by the DLL
# under input_type=network and by 10 headless harnesses as a probe socket
# (same audit, P-2).
#
# LAYERING (feature:F7 gate hardening, 2026-07-28): this check only protects
# invocations that go through THIS .ps1. CI's workflow step, a skill, or an
# ad-hoc terminal run all call gt_sim_test.py's run()/batch() directly and
# never reach this function -- that was a second, independent hole (the audit
# that found it: those paths had NO port defence at all). The authoritative
# check has since moved to gt_sim_test.py itself
# (check_gate_ports_free()/_require_gate_ports_free(), called from run()/
# batch() -- the actual common choke point every invocation path funnels
# through regardless of launcher). This .ps1 copy is now belt-and-suspenders:
# it fails faster and with a nicer human-readable message for the .ps1 path
# specifically, but is no longer the only line of defence, and removing it
# would not reopen the hole -- gt_sim_test.py aborting still leaves no
# batch_verdict.json, which the existing "no verdict file = FAIL" hardening
# below (commit 8b006cff) already treats as NOT MEASURED. Keep the two port
# tables in sync if either changes.
# ----------------------------------------------------------------------------
function Test-GatePortsFree {
    $ports = @(
        @{ Port = 8000;  What = 'web backend (packaged GT_Sim / gt_sim_web)'; Why = 'collision' },
        @{ Port = 48198; What = 'OSI ground-truth (gt_sim_test binds this)';  Why = 'collision' },
        @{ Port = 48199; What = 'HostVehicleData';                            Why = 'contamination' },
        @{ Port = 48200; What = 'ScenarioVariables';                          Why = 'contamination' },
        @{ Port = 48202; What = 'VirtualDriver telemetry';                    Why = 'contamination' },
        @{ Port = 9100;  What = 'manual-drive / VD network input';            Why = 'collision' },
        @{ Port = 9105;  What = 'HeadlessFfbSink pushback';                   Why = 'collision' }
    )
    $busy = @()
    foreach ($p in $ports) {
        $inUse = $false
        try {
            if (Get-NetTCPConnection -State Listen -LocalPort $p.Port -ErrorAction SilentlyContinue) { $inUse = $true }
        } catch { }
        try {
            $udp = Get-NetUDPEndpoint -LocalPort $p.Port -ErrorAction SilentlyContinue
            if ($udp) { $inUse = $true }
        } catch { }
        if ($inUse) { $busy += $p }
    }
    if ($busy.Count -eq 0) { return $true }

    Write-Host "============================================================" -ForegroundColor Red
    Write-Host "REGRESSION GATE: ABORTED -- required ports are already in use" -ForegroundColor Red
    foreach ($b in $busy) {
        Write-Host ("  port {0,-6} {1,-45} [{2}]" -f $b.Port, $b.What, $b.Why) -ForegroundColor Red
    }
    Write-Host "" -ForegroundColor Red
    Write-Host "  collision    -- this gate binds the port; scenarios die with WinError 10013" -ForegroundColor Red
    Write-Host "                  and the deviation check then finds zero deviations over zero" -ForegroundColor Red
    Write-Host "                  runs. A green gate here would be a lie." -ForegroundColor Red
    Write-Host "  contamination-- this gate SENDS to the port unconditionally. Whoever is" -ForegroundColor Red
    Write-Host "                  listening records our frames as if they were theirs, with no" -ForegroundColor Red
    Write-Host "                  error on either side." -ForegroundColor Red
    Write-Host "" -ForegroundColor Red
    Write-Host "  Stop the packaged app (or whatever holds these ports) and re-run." -ForegroundColor Red
    Write-Host "  To see what holds them:" -ForegroundColor Red
    Write-Host "    Get-Process | Where-Object { `$_.ProcessName -match 'GT_Sim|gt_sim_web' }" -ForegroundColor Red
    Write-Host "  There is deliberately NO override switch: an override is how a run that" -ForegroundColor Red
    Write-Host "  could not measure anything gets reported as green." -ForegroundColor Red
    Write-Host "============================================================" -ForegroundColor Red
    return $false
}

# Only the behavioural steps touch these sockets, so -SkipBehavioral (unit +
# ODR only) must not be blocked by a running packaged app.
if (-not $SkipBehavioral) {
    if (-not (Test-GatePortsFree)) { exit 2 }
}

# ----------------------------------------------------------------------------
# Shared behavioral-batch runner (Steps 2 and 2.6)
#
# Runs a gt_sim_test batch manifest, then compares the per-scenario /
# per-matcher verdict against a COMMITTED baseline. Returns $true when the step
# should be treated as passing, $false when it must fail the gate.
#
# The batch's own exit code is deliberately NOT the verdict: gt_sim_test exits 1
# whenever overall=fail, which some batches do BY DESIGN (a discriminating case
# recorded as a known red in the baseline). The DEVIATION vs baseline is the gate.
# ----------------------------------------------------------------------------
function Invoke-BehavioralBatch {
    param(
        [string]$Label,
        [string]$BatchPath,
        [string]$OutPath,
        [string]$BaselinePath,
        [string]$PyExe,
        [string]$Harness,
        [string]$DllPath
    )

    $argList = @($Harness, "batch", $BatchPath, "--out", $OutPath)
    if (-not [string]::IsNullOrWhiteSpace($Dll)) { $argList += @("--dll", $DllPath) }
    Write-Host "${Label}: $PyExe $($argList -join ' ')" -ForegroundColor Cyan
    & $PyExe @argList

    $verdictFile = Join-Path $OutPath "batch_verdict.json"
    $verdictText = "(no batch_verdict.json)"
    $ranCount = $null; $errCount = $null; $totalCount = $null
    if (Test-Path $verdictFile) {
        try {
            $v = Get-Content $verdictFile -Raw | ConvertFrom-Json
            $s = $v.summary
            $verdictText = "overall=$($v.overall) (pass=$($s.pass) fail=$($s.fail) needs-review=$($s.'needs-review') error=$($s.error))"
            $ranCount   = [int]$s.pass + [int]$s.fail + [int]$s.'needs-review'
            $errCount   = [int]$s.error
            $totalCount = $ranCount + $errCount
        } catch { $verdictText = "(could not parse batch_verdict.json)" }
    }

    # Accumulate coverage BEFORE any verdict is reached, and on every path, so
    # the counts the summary prints are what actually happened -- including for
    # a batch that goes on to fail or warn.
    if ($null -ne $totalCount) {
        $script:BehavioralRan      += $ranCount
        $script:BehavioralTotal    += $totalCount
        $script:BehavioralMeasured = $true
    }

    # No verdict file at all, or an unparseable one, means the batch did not get
    # far enough to say anything. That is "not measured", not "no deviation" --
    # and check_regression_baseline.py's own exit 2 for this case is only a WARN
    # by default, which would let it through. Fail here instead.
    if ($null -eq $totalCount) {
        Write-Host "${Label}: FAIL -- NOT MEASURED: $verdictText" -ForegroundColor Red
        Write-Host "    The batch produced no readable verdict, so nothing was compared." -ForegroundColor Red
        Write-Host "    Expected: $verdictFile" -ForegroundColor Red
        return $false
    }
    # A verdict that reports zero scenarios is equally unmeasured (empty or
    # mis-pathed manifest).
    if ($totalCount -eq 0) {
        Write-Host "${Label}: FAIL -- NOT MEASURED: the batch reported 0 scenarios. $verdictText" -ForegroundColor Red
        Write-Host "    Check the manifest path: $BatchPath" -ForegroundColor Red
        return $false
    }

    # ------------------------------------------------------------------
    # "Did not run" is NOT "no deviation". 2026-07-27: the packaged app was
    # left running, every one of 22 scenarios died on the same WinError 10013
    # (socket access denied) giving pass=0 error=22 -- and because the
    # behavioural step is WARN-by-default, the gate still printed
    # "REGRESSION GATE: PASS". Nothing had executed. The gate, which is the
    # last thing protecting everything else, was reporting a failure as a
    # success.
    #
    # A deviation check over zero executed scenarios trivially finds zero
    # deviations, so check_regression_baseline.py's exit 0 cannot be trusted
    # on its own. These two conditions are therefore HARD failures: they are
    # not "within tolerance" (the thing WARN exists to express), they are
    # "not measured", and -FailOnBehavioral must not be able to downgrade them.
    # ------------------------------------------------------------------
    if ($ranCount -eq 0) {
        Write-Host "${Label}: FAIL -- NOT MEASURED: 0 of $totalCount scenarios executed ($errCount errored). $verdictText" -ForegroundColor Red
        Write-Host "    A deviation check over zero runs proves nothing. This is not a WARN." -ForegroundColor Red
        Write-Host "    Most likely cause: something else is holding the UDP/TCP listener ports" -ForegroundColor Red
        Write-Host "    (the packaged GT_Sim / gt_sim_web). See the port preflight at the top of this script." -ForegroundColor Red
        return $false
    }
    if ($errCount -gt 0) {
        Write-Host "${Label}: FAIL -- $errCount of $totalCount scenarios errored (could not be measured). $verdictText" -ForegroundColor Red
        Write-Host "    Errored scenarios are unmeasured, not passing. Fix the errors, then re-run." -ForegroundColor Red
        return $false
    }

    # Exit 0 = no deviation, 1 = deviation(s), 2 = setup error (baseline or
    # batch output missing). Writes <OutPath>/regression_report.md.
    $checker = Resolve-RepoPath "scripts/check_regression_baseline.py"
    $regArgs = @($checker, "--batch-out", $OutPath, "--baseline", $BaselinePath)
    Write-Host "${Label}: $PyExe $($regArgs -join ' ')" -ForegroundColor Cyan
    & $PyExe @regArgs
    $reg = $LASTEXITCODE

    if ($reg -eq 0) {
        # Always state how many actually ran. "PASS" alone is what let a
        # 0-of-22 run read as green at a glance.
        Write-Host "${Label}: PASS ($ranCount/$totalCount scenarios ran, no per-scenario deviation vs baseline)  $verdictText" -ForegroundColor Green
        return $true
    }

    # Both the deviation case and the setup-error case are reported; they only
    # fail the gate under -FailOnBehavioral (never silently pass).
    $what = if ($reg -eq 2) { "regression-check setup error" } else { "per-scenario deviation vs baseline" }
    if ($FailOnBehavioral) {
        Write-Host "${Label}: FAIL ($what)  $verdictText" -ForegroundColor Red
        return $false
    }
    Write-Host "${Label}: WARN ($what, not gating)  $verdictText" -ForegroundColor Yellow
    if ($reg -ne 2) {
        Write-Host "    Report: $(Join-Path $OutPath 'regression_report.md')" -ForegroundColor Yellow
        Write-Host "    Pass -FailOnBehavioral to make this fail the gate." -ForegroundColor Yellow
        Write-Host "    If the change is intentional, refresh the baseline:" -ForegroundColor Yellow
        Write-Host "      $PyExe $checker --batch-out $OutPath --baseline $BaselinePath --update" -ForegroundColor Yellow
    }
    return $true
}

# ----------------------------------------------------------------------------
# Step 0 - build/config drift check (HARD gate)
# ----------------------------------------------------------------------------
# The anticipation_driving batch (and any other batch whose scenarios lack a
# <Property name="policies">) runs the raw xosc with NO per-run config
# generation, so the VirtualDriverController reads whatever is currently in
# build/GT_esmini/config/virtual_driver.json (and manual_drive.json). Any hand-
# edit left over from real-machine testing (input_type=sdl2_wheel,
# ffb_target_track_enabled=true, override_lateral=scenario, etc.) leaks
# straight into the batch behaviour and shows up as a phantom "regression"
# that has nothing to do with the code under test.
#
# Root-caused during F7b real-machine iteration (post-f8a5ce56): PM's
# independent gate run showed 10 deviations on anticipation_driving; 3-way
# validation (pre-F7b + shipped / F7b + shipped / F7b + drifted-build-config)
# proved the code was innocent and the drift was 100% of the regression.
# This step exists to fail loudly on that class of hygiene bug before the
# behavioural batches ever run.
Write-Host "==== Step 0: build/config drift check ====" -ForegroundColor Cyan
$configFiles = @("virtual_driver.json", "manual_drive.json", "auto_light.json")
$driftDetected = $false
foreach ($cf in $configFiles) {
    # Portability notes for Windows PowerShell 5.1 (PM's invocation):
    #  - Join-Path takes only Path+ChildPath positionally under 5.1; pwsh 7 accepts
    #    a third positional via -AdditionalChildPath. Use nested Join-Path so this
    #    step works under BOTH powershell.exe and pwsh.exe.
    #  - Get-FileHash is missing from some minimal / locked-down 5.1 installations
    #    (encountered locally). Use .NET [System.IO.File]::ReadAllText for the
    #    equality check — every one of our config files is text (JSON), and 5.1's
    #    .NET binding is universally available.
    $src = Join-Path "GT_esmini/config" $cf
    $bld = Join-Path (Join-Path $BuildDir "GT_esmini/config") $cf
    if (-not (Test-Path $src)) { continue }
    if (-not (Test-Path $bld)) {
        Write-Host "  MISSING $bld -- restaging from source" -ForegroundColor Yellow
        Copy-Item $src $bld -Force
        continue
    }
    $srcTxt = [System.IO.File]::ReadAllText((Resolve-Path $src).ProviderPath)
    $bldTxt = [System.IO.File]::ReadAllText((Resolve-Path $bld).ProviderPath)
    if ($srcTxt -ne $bldTxt) {
        Write-Host "  DRIFT   $cf : build differs from source (hand-edit leak?)" -ForegroundColor Red
        Write-Host "          diff:" -ForegroundColor Red
        Compare-Object (Get-Content $src) (Get-Content $bld) | Select-Object -First 20 |
            ForEach-Object { Write-Host ("          " + $_.SideIndicator + " " + $_.InputObject) -ForegroundColor Red }
        $driftDetected = $true
    }
}
if ($driftDetected) {
    Write-Host "Step 0: FAIL -- build/config drifted from source." -ForegroundColor Red
    Write-Host "        Restage before rerunning:" -ForegroundColor Red
    Write-Host "          Copy-Item GT_esmini/config/*.json $BuildDir/GT_esmini/config/ -Force" -ForegroundColor Red
    Write-Host "        (Or accept the drift into source if intentional.)" -ForegroundColor Red
    exit 1
}
Write-Host "Step 0: PASS (build/config matches source)" -ForegroundColor Green

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
# Shared prerequisites for Steps 2 / 2.6. Resolved once, OUTSIDE the step
# bodies, so Step 2.6 does not depend on Step 2 having run (a -SkipBehavioral
# run would otherwise leave $pyExe/$dllPath undefined and Test-Path would throw
# under $ErrorActionPreference = "Stop").
#
# Verification venv python: prefer the documented DriverScript venv (has
# pyyaml+osi3+matplotlib), fall back to the web venv.
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
$harness = Resolve-RepoPath "GT_esmini/scripts/verification/gt_sim_test.py"

if ($SkipBehavioral) {
    Write-Host "==== Step 2/2: VirtualDriver behavioral batch - SKIPPED (-SkipBehavioral) ====" -ForegroundColor Yellow
} else {
    Write-Host "==== Step 2/2: VirtualDriver behavioral batch (gt_sim_test) ====" -ForegroundColor Cyan

    $batchPath = Resolve-RepoPath $Batch
    $outPath = Resolve-RepoPath $OutDir

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
        Write-Host "    NOTE: skipping here is not a pass. If no behavioral scenario runs at all," -ForegroundColor Yellow
        Write-Host "          the gate fails at the summary. Use -SkipBehavioral to say so on purpose." -ForegroundColor Yellow
    } else {
        # Steps 2 and 2.6 run the identical batch -> baseline-compare recipe on
        # different manifests, so it lives in one function (Invoke-BehavioralBatch,
        # defined above) instead of being duplicated per step.
        if (-not (Invoke-BehavioralBatch -Label "Step 2" -BatchPath $batchPath -OutPath $outPath -BaselinePath (Resolve-RepoPath $Baseline) -PyExe $pyExe -Harness $harness -DllPath $dllPath)) {
            $overallOk = $false
        }
    }
}

# ----------------------------------------------------------------------------
# Step 2.6 - AEB safety batch (reported gate, skippable)
#
# WHY A SEPARATE STEP AND NOT MORE SCENARIOS IN STEP 2:
# the two batches answer different questions and are read by different people
# when they go red. Step 2 = traffic-policy behaviour (signals / signs / lead
# car); Step 2.6 = the AEB safety tier (REQ-AD-001 mitigation + REQ-AD-013
# misfire suppression). Separate manifests + separate baselines mean a red tells
# you WHICH claim broke without opening the report. It also keeps the existing
# baseline (which carries a known, recorded red) from masking an AEB regression.
#
# Before this step existed, NO matcher judging AEB ran in any always-on gate:
# AEB was implemented, unit-green and OSI-wired, yet a regression in it would
# have reached master unnoticed (capability_model.md §5.1).
# ----------------------------------------------------------------------------
if ($SkipBehavioral) {
    Write-Host "==== Step 2.6: AEB safety batch - SKIPPED (-SkipBehavioral) ====" -ForegroundColor Yellow
} elseif ($SkipAeb) {
    Write-Host "==== Step 2.6: AEB safety batch - SKIPPED (-SkipAeb) ====" -ForegroundColor Yellow
} else {
    Write-Host "==== Step 2.6: AEB safety batch (gt_sim_test) ====" -ForegroundColor Cyan

    # Same prerequisites as Step 2 (venv + Release DLL); reuse its resolution.
    $aebBatchPath = Resolve-RepoPath $AebBatch
    $aebOutPath = Resolve-RepoPath $AebOutDir

    $aebMissing = @()
    if ([string]::IsNullOrWhiteSpace($pyExe) -or -not (Test-Path $pyExe)) {
        $aebMissing += "verification venv python (DriverScript/.venv or GT_esmini/web/.venv)"
    }
    if (-not (Test-Path $dllPath)) {
        $aebMissing += "GT_esminiLib.dll at $dllPath (requires a completed $Config build)"
    }
    if (-not (Test-Path $aebBatchPath)) { $aebMissing += "batch manifest $aebBatchPath" }

    if ($aebMissing.Count -gt 0) {
        Write-Host "Step 2.6: SKIPPED - prerequisites missing:" -ForegroundColor Yellow
        foreach ($m in $aebMissing) { Write-Host "    - $m" -ForegroundColor Yellow }
    } else {
        if (-not (Invoke-BehavioralBatch -Label "Step 2.6" -BatchPath $aebBatchPath -OutPath $aebOutPath -BaselinePath (Resolve-RepoPath $AebBaseline) -PyExe $pyExe -Harness $harness -DllPath $dllPath)) {
            $overallOk = $false
        }
    }
}

# ----------------------------------------------------------------------------
# Step 2.7 - Anticipation-driving batch (reported gate, skippable)
#
# Same recipe as Steps 2 / 2.6 (shared Invoke-BehavioralBatch) on the mid/long
# ANTICIPATION matchers: smooth deceleration for a curve / turn / speed-limit
# drop, target speed reached by the landmark, lane keeping, steering non-
# saturation, no spurious junction constraint. A SEPARATE step for the same
# reason AEB is: it answers a different question (does the driver read the road
# ahead and act early?) and is read by different people when it goes red, so a
# red names the broken claim without opening the report.
#
# Before this step existed, these matchers ran only in a manual 05_anticipation
# batch — a regression in the anticipation planner would have reached master
# unnoticed (capability_model.md §5.1, the same gap AEB had). Runs with osi:true
# so the ego anchor is the FACE-1 path (deceleration_profile_smooth a=osi),
# whose baseline was taken after the ego-anchor-face1 migration.
# NOTE: covers 5 of the 6 previously-ungated anticipation matchers; lane_change_count
# is still ungated (no scenario in this batch performs a lane change).
# ----------------------------------------------------------------------------
if ($SkipBehavioral) {
    Write-Host "==== Step 2.7: Anticipation-driving batch - SKIPPED (-SkipBehavioral) ====" -ForegroundColor Yellow
} elseif ($SkipAnticipation) {
    Write-Host "==== Step 2.7: Anticipation-driving batch - SKIPPED (-SkipAnticipation) ====" -ForegroundColor Yellow
} else {
    Write-Host "==== Step 2.7: Anticipation-driving batch (gt_sim_test) ====" -ForegroundColor Cyan

    # Same prerequisites as Steps 2 / 2.6 (venv + Release DLL); reuse resolution.
    $antBatchPath = Resolve-RepoPath $AntBatch
    $antOutPath = Resolve-RepoPath $AntOutDir

    $antMissing = @()
    if ([string]::IsNullOrWhiteSpace($pyExe) -or -not (Test-Path $pyExe)) {
        $antMissing += "verification venv python (DriverScript/.venv or GT_esmini/web/.venv)"
    }
    if (-not (Test-Path $dllPath)) {
        $antMissing += "GT_esminiLib.dll at $dllPath (requires a completed $Config build)"
    }
    if (-not (Test-Path $antBatchPath)) { $antMissing += "batch manifest $antBatchPath" }

    if ($antMissing.Count -gt 0) {
        Write-Host "Step 2.7: SKIPPED - prerequisites missing:" -ForegroundColor Yellow
        foreach ($m in $antMissing) { Write-Host "    - $m" -ForegroundColor Yellow }
    } else {
        if (-not (Invoke-BehavioralBatch -Label "Step 2.7" -BatchPath $antBatchPath -OutPath $antOutPath -BaselinePath (Resolve-RepoPath $AntBaseline) -PyExe $pyExe -Harness $harness -DllPath $dllPath)) {
            $overallOk = $false
        }
    }
}

# ----------------------------------------------------------------------------
# Step 2.8 - Scenario-driven control handover batch (reported gate, skippable)
#
# Same recipe as Steps 2 / 2.6 / 2.7 (shared Invoke-BehavioralBatch) on a
# SEPARATE manifest + baseline (design doc Fact M: this must not touch the
# existing 3 pairs). Covers feature:F7 scenario-driven handover
# (docs/virtualdriver/scenario_control_handoff_design.md): an
# ActivateControllerAction lateral="false" longitudinal="false" mid-run must
# make VirtualDriverController actually relinquish control (telemetry.vd_active
# false) and never resume it on its own. No OSI capture needed (osi:false).
# ----------------------------------------------------------------------------
if ($SkipBehavioral) {
    Write-Host "==== Step 2.8: Scenario handoff batch - SKIPPED (-SkipBehavioral) ====" -ForegroundColor Yellow
} elseif ($SkipHandoff) {
    Write-Host "==== Step 2.8: Scenario handoff batch - SKIPPED (-SkipHandoff) ====" -ForegroundColor Yellow
} else {
    Write-Host "==== Step 2.8: Scenario handoff batch (gt_sim_test) ====" -ForegroundColor Cyan

    # Same prerequisites as Steps 2 / 2.6 / 2.7 (venv + Release DLL); reuse resolution.
    $handoffBatchPath = Resolve-RepoPath $HandoffBatch
    $handoffOutPath = Resolve-RepoPath $HandoffOutDir

    $handoffMissing = @()
    if ([string]::IsNullOrWhiteSpace($pyExe) -or -not (Test-Path $pyExe)) {
        $handoffMissing += "verification venv python (DriverScript/.venv or GT_esmini/web/.venv)"
    }
    if (-not (Test-Path $dllPath)) {
        $handoffMissing += "GT_esminiLib.dll at $dllPath (requires a completed $Config build)"
    }
    if (-not (Test-Path $handoffBatchPath)) { $handoffMissing += "batch manifest $handoffBatchPath" }

    if ($handoffMissing.Count -gt 0) {
        Write-Host "Step 2.8: SKIPPED - prerequisites missing:" -ForegroundColor Yellow
        foreach ($m in $handoffMissing) { Write-Host "    - $m" -ForegroundColor Yellow }
    } else {
        if (-not (Invoke-BehavioralBatch -Label "Step 2.8" -BatchPath $handoffBatchPath -OutPath $handoffOutPath -BaselinePath (Resolve-RepoPath $HandoffBaseline) -PyExe $pyExe -Harness $harness -DllPath $dllPath)) {
            $overallOk = $false
        }
    }
}

# ----------------------------------------------------------------------------
# Step 2.5 - Telemetry tolerance goldens (optional, -TelemetryGolden; P6 S0)
# ----------------------------------------------------------------------------
if ($TelemetryGolden) {
    Write-Host "==== Step 2.5: telemetry golden diff (-TelemetryGolden) ====" -ForegroundColor Cyan

    # Same venv resolution as Steps 1.5/2 (telemetry_golden re-runs the batch
    # in-process, so it needs the same pyyaml+osi3 venv and the Release DLL).
    $tgPy = $Python
    if ([string]::IsNullOrWhiteSpace($tgPy)) {
        foreach ($cand in @("DriverScript/.venv/Scripts/python.exe",
                            "GT_esmini/web/.venv/Scripts/python.exe")) {
            $full = Resolve-RepoPath $cand
            if (Test-Path $full) { $tgPy = $full; break }
        }
    }
    $tgScript = Resolve-RepoPath "scripts/telemetry_golden.py"

    if ([string]::IsNullOrWhiteSpace($tgPy) -or -not (Test-Path $tgPy)) {
        Write-Host "Step 2.5: FAIL - verification venv python not found (DriverScript/.venv or GT_esmini/web/.venv)" -ForegroundColor Red
        $overallOk = $false
    } else {
        # Tolerances = measured same-build noise floor with margin (see
        # telemetry_golden.py docstring): the sim is NOT bit-reproducible across
        # process runs (~<=1e-3 position, one-frame speed transients up to a*dt).
        # Position stays the tight discriminator; a real VJ regression moves x/y
        # far beyond 5 mm.
        $tgTol = @("--tol-pos", "5e-3", "--tol-h", "1e-3", "--tol-v", "5e-2")
        $tgTargets = @(
            @{ Label = "phase3"; Batch = (Resolve-RepoPath $Batch) }
        )
        $catalogBatch = Resolve-RepoPath "resources/scenario_authoring/scenario_templates/generated/catalog_batch.yaml"
        $catalogGoldens = Resolve-RepoPath "GT_esmini/test/telemetry_goldens/catalog"
        if ((Test-Path $catalogGoldens) -and (Test-Path $catalogBatch)) {
            $tgTargets += @{ Label = "catalog"; Batch = $catalogBatch }
        }
        foreach ($tg in $tgTargets) {
            $tgArgs = @($tgScript, "diff", "--batch", $tg.Batch, "--label", $tg.Label) + $tgTol
            if (-not [string]::IsNullOrWhiteSpace($Dll)) { $tgArgs += @("--dll", (Resolve-RepoPath $Dll)) }
            Write-Host "Step 2.5: $tgPy $($tgArgs -join ' ')" -ForegroundColor Cyan
            & $tgPy @tgArgs
            if ($LASTEXITCODE -ne 0) {
                Write-Host "Step 2.5: FAIL (telemetry golden diff, label=$($tg.Label))" -ForegroundColor Red
                $overallOk = $false
            } else {
                Write-Host "Step 2.5: PASS (label=$($tg.Label))" -ForegroundColor Green
            }
        }
    }
}

# ----------------------------------------------------------------------------
# Summary
# ----------------------------------------------------------------------------
Write-Host "============================================================"
# The final line must carry how much actually executed. "REGRESSION GATE: PASS"
# on its own is precisely what let a 0-of-22 run pass for green on 2026-07-27;
# the per-scenario counts were available but only in a line above it, and the
# gate relied on a human reading further. It should not.
#
# This block is also the LAST-RESORT structural guarantee: a gate run that
# executed zero scenarios cannot report PASS, no matter which per-step check
# did or did not catch it, and regardless of -FailOnBehavioral. The invariant
# is deliberately stated once, at the end, where nothing can bypass it.
$coverage = ""
if ($SkipBehavioral) {
    $coverage = "  [behavioral: SKIPPED (-SkipBehavioral)]"
} elseif ($script:BehavioralMeasured -and $script:BehavioralRan -gt 0) {
    $coverage = "  [behavioral: $($script:BehavioralRan)/$($script:BehavioralTotal) scenarios executed]"
} else {
    $coverage = "  [behavioral: NOT MEASURED - 0 scenarios executed]"
    if ($overallOk) {
        Write-Host "NOT MEASURED: the behavioral batches executed 0 scenarios." -ForegroundColor Red
        Write-Host "  Every other step may have passed, but nothing was actually driven, so" -ForegroundColor Red
        Write-Host "  this run proves nothing about behaviour. Forcing FAIL." -ForegroundColor Red
        Write-Host "  Causes seen in practice: ports held by the packaged app (see the preflight)," -ForegroundColor Red
        Write-Host "  and missing prerequisites (no Release build) reported as 'SKIPPED' above." -ForegroundColor Red
        Write-Host "  If you meant to run without them, say so explicitly: -SkipBehavioral." -ForegroundColor Red
        $overallOk = $false
    }
}
if ($overallOk) {
    Write-Host "REGRESSION GATE: PASS$coverage" -ForegroundColor Green
    exit 0
} else {
    Write-Host "REGRESSION GATE: FAIL$coverage" -ForegroundColor Red
    exit 1
}
