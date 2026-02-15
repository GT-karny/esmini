param(
    [string]$BuildDir = "build",
    [string]$PythonExe = "python",
    [string]$MatrixPath = "GT_esmini/test/validation/realdriver_feature_matrix.yaml",
    [string]$ThresholdPath = "GT_esmini/test/validation/kpi_thresholds.yaml",
    [string]$OutputRoot = "artifacts/realdriver_features",
    [switch]$UpdateGolden,
    [string]$GoldenRoot = "golden/realdriver_features",
    [string]$SimPath = "",
    [int]$Hz = 100,
    [bool]$EnableVideo = $true,
    [switch]$KeepFrames,
    [string]$WindowSize = "1280x720"
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $MatrixPath)) { throw "Matrix file not found: $MatrixPath" }
if (-not (Test-Path $ThresholdPath)) { throw "Threshold file not found: $ThresholdPath" }

if (-not $SimPath) {
    $candidates = @(
        (Join-Path $BuildDir "GT_esmini/Release/GT_Sim.exe"),
        (Join-Path $BuildDir "GT_esmini/Release/GT_Sim"),
        (Join-Path $BuildDir "GT_esmini/Debug/GT_Sim.exe"),
        (Join-Path $BuildDir "GT_esmini/Debug/GT_Sim"),
        "bin/GT_Sim.exe",
        "bin/GT_Sim"
    )
    foreach ($c in $candidates) {
        if (Test-Path $c) { $SimPath = $c; break }
    }
}
if (-not $SimPath -or -not (Test-Path $SimPath)) {
    throw "GT_Sim not found. Specify -SimPath explicitly."
}
if ($Hz -le 0) {
    throw "Invalid Hz: $Hz (must be > 0)"
}

$windowMatch = [regex]::Match($WindowSize, '^(?<w>\d+)x(?<h>\d+)$')
if (-not $windowMatch.Success) {
    throw "Invalid WindowSize '$WindowSize'. Use WIDTHxHEIGHT format, e.g. 1280x720."
}
$windowWidth = $windowMatch.Groups["w"].Value
$windowHeight = $windowMatch.Groups["h"].Value

$ffmpegPath = $null
if ($EnableVideo) {
    $ffmpegCmd = Get-Command "ffmpeg" -ErrorAction SilentlyContinue
    if ($ffmpegCmd) {
        $ffmpegPath = $ffmpegCmd.Source
    }
}

$runId = Get-Date -Format "yyyyMMdd-HHmmss"
$runDir = Join-Path $OutputRoot $runId
New-Item -ItemType Directory -Force -Path $runDir | Out-Null

$matrix = Get-Content $MatrixPath -Raw | ConvertFrom-Yaml
$features = $matrix.features

Write-Host "[RealDriver] Run ID: $runId"
Write-Host "[RealDriver] GT_Sim: $SimPath"
Write-Host "[RealDriver] Frequency: $Hz Hz"
if ($EnableVideo) {
    if ($ffmpegPath) {
        Write-Host "[RealDriver] Video: enabled ($WindowSize, ffmpeg=$ffmpegPath)"
    }
    else {
        Write-Host "[RealDriver] Video: enabled ($WindowSize), ffmpeg not found (MP4 conversion will be skipped)"
    }
}
else {
    Write-Host "[RealDriver] Video: disabled"
}

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
        "--osc", $absScenario,
        "--hz", "$Hz",
        "--path", (Resolve-Path "resources/xodr").Path,
        "--path", (Resolve-Path "resources/xosc").Path,
        "--path", (Resolve-Path "resources").Path
    )
    if ($EnableVideo) {
        $args += @(
            "--window", "0", "0", "$windowWidth", "$windowHeight",
            "--headless",
            "--capture_screen"
        )
    }
    if ($f.run_args) {
        $args += $f.run_args.Split(' ', [System.StringSplitOptions]::RemoveEmptyEntries)
    }

    Push-Location $fdir
    try {
        & (Resolve-Path $SimPath).Path @args *> $logPath
        $simExit = $LASTEXITCODE
        if ($simExit -ne 0) {
            "GT_Sim exited with code $simExit" | Out-File -Encoding utf8 (Join-Path $fdir "runner_error.txt")
        }
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
d.save_csv(extended=True)
d.close()
"@
        & $PythonExe -c $py *> (Join-Path $fdir "dat_convert.log")
    }

    if ($EnableVideo) {
        $frames = Get-ChildItem -Path $fdir -Filter "screen_shot_*.tga" -File -ErrorAction SilentlyContinue
        if ($frames -and $frames.Count -gt 0) {
            if ($ffmpegPath) {
                Push-Location $fdir
                try {
                    & $ffmpegPath -y -framerate "$Hz" -i "screen_shot_%05d.tga" -c:v libx264 -vf "format=yuv420p" -crf 18 "result.mp4" *> "video_encode.log"
                    if ($LASTEXITCODE -ne 0) {
                        "ffmpeg conversion failed with code $LASTEXITCODE" | Out-File -Encoding utf8 (Join-Path $fdir "video_error.txt")
                    }
                }
                finally {
                    Pop-Location
                }
            }
            else {
                "ffmpeg not found. MP4 conversion skipped." | Out-File -Encoding utf8 (Join-Path $fdir "video_error.txt")
            }

            if (-not $KeepFrames) {
                Remove-Item -Path (Join-Path $fdir "screen_shot_*.tga") -Force -ErrorAction SilentlyContinue
            }
        }
        elseif ($ffmpegPath) {
            "No screen capture frames found. Check GT_Sim output and capture options." | Out-File -Encoding utf8 (Join-Path $fdir "video_error.txt")
        }
    }
}

$u = @()
if ($UpdateGolden) { $u += '--update-golden' }
& $PythonExe scripts/validate_realdriver_feature_results.py --matrix $MatrixPath --thresholds $ThresholdPath --run-dir $runDir --golden-root $GoldenRoot @u
$validateExit = $LASTEXITCODE
& $PythonExe scripts/render_realdriver_report.py --run-dir $runDir
Write-Host "[RealDriver] Completed. Report: $runDir/report.html"
if ($validateExit -ne 0) {
    exit $validateExit
}
