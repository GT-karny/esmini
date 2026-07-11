<#
.SYNOPSIS
    Canonical GT_esmini test gate (ctest).

.DESCRIPTION
    Runs the GT_esmini-specific tests that are registered in ctest but were
    previously never executed by CI (audit TST-1).

    Default scope = the GREEN gate: test_ScenarioReaderParsing (gtest binary
    compiling all 8 active GT unit test sources incl. the VirtualDriver policy
    tests; GT_esmini/test/CMakeLists.txt:18). It registers via the unittest()
    macro -> add_test(NAME <target>), so the ctest name is the bare target name
    (support/cmake/common/unittest.cmake:55-57).

    Opt-in scopes (kept out of the default gate for runtime, not health):
      - -IncludeIntegration: GT_esmini_Integration_* (GT_Loader, explicit
        per-scenario registration with assertions in
        GT_esmini/test/CMakeLists.txt). Re-authored 2026-07 (audit R3/TST):
        21 tests (6 AutoLight/LightStateAction + 15 ControllerRealDriver) are
        expected GREEN; each asserts init rc==0, entity presence, run
        completion and, where applicable, light-state changes/movement
        (audit TST-3). The 17 frozen pythondriver_* scenarios are only
        registered when the build has GT_ENABLE_EMBEDDED_PYTHON=ON
        (audit MSC-5) and are not part of any green gate.
      - -IncludeFrozen: test_PythonDriverBridge (embedded-Python bridge tests
        for the v0.8-frozen PythonDriver feature; only registered with
        GT_ENABLE_EMBEDDED_PYTHON=ON).

.PARAMETER Config
    Build configuration to test (Release/Debug). Default: Release.

.PARAMETER BuildDir
    CMake build directory (where ctest's test registry lives). Default: build.

.PARAMETER Filter
    Optional extra ctest -R regex, AND-combined with the gate pattern so you can
    narrow to a subset.

.EXAMPLE
    pwsh scripts/run_gt_tests.ps1
    pwsh scripts/run_gt_tests.ps1 -Config Debug -BuildDir build_debug
    pwsh scripts/run_gt_tests.ps1 -IncludeIntegration -Filter autolight
#>
[CmdletBinding()]
param(
    [string]$Config = "Release",
    [string]$BuildDir = "build",
    [string]$Filter = "",
    [switch]$IncludeIntegration,
    [switch]$IncludeFrozen
)

$ErrorActionPreference = "Stop"

# Resolve the repo root from this script's location so it works from any cwd.
$repoRoot = Split-Path -Parent $PSScriptRoot
$buildPath = if ([System.IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $repoRoot $BuildDir }

if (-not (Test-Path $buildPath)) {
    Write-Host "GT TESTS: FAIL - build directory not found: $buildPath" -ForegroundColor Red
    Write-Host "  Run Protocol A first: cmake -S . -B build -G 'Visual Studio 17 2022' -A x64; cmake --build build --config $Config"
    exit 1
}

# GT test name pattern, verified against the CMake sources (see .DESCRIPTION).
$parts = @("test_ScenarioReaderParsing")
if ($IncludeIntegration) { $parts += "GT_esmini_Integration_" }
if ($IncludeFrozen)      { $parts += "test_PythonDriverBridge" }
$gtPattern = "(" + ($parts -join "|") + ")"

# An extra -Filter is AND-combined with the GT pattern via two -R arguments
# (ctest applies multiple -R as logical AND).
$ctestArgs = @(
    "--test-dir", $buildPath,
    "-C", $Config,
    "-R", $gtPattern,
    "--output-on-failure"
)
if (-not [string]::IsNullOrWhiteSpace($Filter)) {
    $ctestArgs += @("-R", $Filter)
}

Write-Host "GT TESTS: ctest $($ctestArgs -join ' ')" -ForegroundColor Cyan
& ctest @ctestArgs
$exitCode = $LASTEXITCODE

if ($exitCode -eq 0) {
    Write-Host "GT TESTS: PASS (config=$Config, pattern=$gtPattern$(if ($Filter) { " AND $Filter" }))" -ForegroundColor Green
} else {
    Write-Host "GT TESTS: FAIL (ctest exit $exitCode, config=$Config)" -ForegroundColor Red
}

exit $exitCode
