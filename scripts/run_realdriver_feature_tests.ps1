param(
    [string]$BuildDir = "build",
    [string]$PythonExe = "python",
    [string]$MatrixPath = "GT_esmini/test/validation/realdriver_feature_matrix.yaml",
    [string]$ThresholdPath = "GT_esmini/test/validation/kpi_thresholds.yaml",
    [string]$OutputRoot = "artifacts/realdriver_features",
    [switch]$UpdateGolden,
    [string]$GoldenRoot = "golden/realdriver_features",
    [string]$LoaderPath = ""
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $MatrixPath)) { throw "Matrix file not found: $MatrixPath" }
if (-not (Test-Path $ThresholdPath)) { throw "Threshold file not found: $ThresholdPath" }

if (-not $LoaderPath) {
    $candidates = @(
        (Join-Path $BuildDir "GT_esmini/test/GT_Loader.exe"),
        (Join-Path $BuildDir "GT_esmini/test/GT_Loader"),
        "bin/GT_Loader.exe",
        "bin/GT_Loader"
    )
    foreach ($c in $candidates) {
        if (Test-Path $c) { $LoaderPath = $c; break }
    }
}
if (-not $LoaderPath -or -not (Test-Path $LoaderPath)) {
    throw "GT_Loader not found. Specify -LoaderPath explicitly."
}

$runId = Get-Date -Format "yyyyMMdd-HHmmss"
$runDir = Join-Path $OutputRoot $runId
New-Item -ItemType Directory -Force -Path $runDir | Out-Null

$matrix = Get-Content $MatrixPath -Raw | ConvertFrom-Yaml
$features = $matrix.features

Write-Host "[RealDriver] Run ID: $runId"
Write-Host "[RealDriver] Loader: $LoaderPath"

foreach ($f in $features) {
    $fid = $f.id
    $scenario = $f.scenario
    $fdir = Join-Path $runDir $fid
    New-Item -ItemType Directory -Force -Path $fdir | Out-Null

    if (-not (Test-Path $scenario)) {
        "Missing scenario: $scenario" | Out-File -Encoding utf8 (Join-Path $fdir "runner_error.txt")
        continue
    }

    $absScenario = (Resolve-Path $scenario).Path
    $logPath = Join-Path $fdir "stdout.txt"
    $args = @(
        $absScenario,
        "--path", (Resolve-Path "resources/xodr").Path,
        "--path", (Resolve-Path "resources/xosc").Path,
        "--path", (Resolve-Path "resources").Path
    )
    if ($f.run_args) {
        $args += $f.run_args.Split(' ', [System.StringSplitOptions]::RemoveEmptyEntries)
    }

    Push-Location $fdir
    try {
        & (Resolve-Path $LoaderPath).Path @args *> $logPath
    }
    finally {
        Pop-Location
    }

    if (Test-Path (Join-Path $fdir "sim.dat")) {
        $py = @"
import os, sys
sys.path.insert(0, os.path.abspath('scripts'))
from dat import DATFile
fn = os.path.join(r'$((Resolve-Path $fdir).Path)','sim.dat')
d = DATFile(fn)
d.save_csv()
d.close()
"@
        & $PythonExe -c $py *> (Join-Path $fdir "dat_convert.log")
    }
}

$u = @()
if ($UpdateGolden) { $u += '--update-golden' }
& $PythonExe scripts/validate_realdriver_feature_results.py --matrix $MatrixPath --thresholds $ThresholdPath --run-dir $runDir --golden-root $GoldenRoot @u
& $PythonExe scripts/render_realdriver_report.py --run-dir $runDir
Write-Host "[RealDriver] Completed. Report: $runDir/report.html"
