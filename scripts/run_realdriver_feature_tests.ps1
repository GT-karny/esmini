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

function Read-StructuredFile {
    param([string]$Path)

    $raw = Get-Content -Path $Path -Raw

    try {
        return $raw | ConvertFrom-Json
    }
    catch {
        # continue to YAML parsing
    }

    $yamlCmd = Get-Command ConvertFrom-Yaml -ErrorAction SilentlyContinue
    if ($yamlCmd) {
        return $raw | ConvertFrom-Yaml
    }

    throw "Cannot parse '$Path'. The file is not JSON and ConvertFrom-Yaml is unavailable. Install powershell-yaml module."
}

if (-not (Test-Path $MatrixPath)) { throw "Matrix file not found: $MatrixPath" }
if (-not (Test-Path $ThresholdPath)) { throw "Threshold file not found: $ThresholdPath" }

$repoRoot = (Get-Location).Path
$outputRootAbs = [System.IO.Path]::GetFullPath((Join-Path $repoRoot $OutputRoot))
$resourcesRoot = Join-Path $repoRoot "resources"
$xodrRoot = Join-Path $resourcesRoot "xodr"
$xoscRoot = Join-Path $resourcesRoot "xosc"
$catalogRoot = Join-Path $xoscRoot "Catalogs"

if (-not (Test-Path $resourcesRoot)) { throw "Resources directory not found: $resourcesRoot" }
if (-not (Test-Path $xodrRoot)) { throw "OpenDRIVE resource directory not found: $xodrRoot" }
if (-not (Test-Path $xoscRoot)) { throw "OpenSCENARIO resource directory not found: $xoscRoot" }
if (-not (Test-Path $catalogRoot)) {
    Write-Warning "Catalog root not found: $catalogRoot (scenario loading may fail if catalogs are referenced)."
}

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
$simExe = (Resolve-Path $SimPath).Path
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
$runDir = Join-Path $outputRootAbs $runId
New-Item -ItemType Directory -Force -Path $runDir | Out-Null

$matrix = Read-StructuredFile -Path $MatrixPath
$features = $matrix.features

Write-Host "[RealDriver] Run ID: $runId"
Write-Host "[RealDriver] GT_Sim: $simExe"
Write-Host "[RealDriver] Frequency: $Hz Hz"
if ($EnableVideo) {
    if ($ffmpegPath) {
        Write-Host "[RealDriver] Video: enabled via GT_Sim direct capture ($WindowSize, ffmpeg=$ffmpegPath)"
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
        "--path", (Resolve-Path $xodrRoot).Path,
        "--path", (Resolve-Path $xoscRoot).Path,
        "--path", (Resolve-Path $resourcesRoot).Path
    )
    if (Test-Path $catalogRoot) {
        $catalogDirs = Get-ChildItem -Path $catalogRoot -Directory -ErrorAction SilentlyContinue
        foreach ($catalogDir in $catalogDirs) {
            $args += @("--path", $catalogDir.FullName)
        }
    }
    if ($EnableVideo) {
        $args += @(
            "--video_capture",
            "--video_window", "$windowWidth", "$windowHeight",
            "--video_frames", "-1",
            "--video_prefix", "screen_shot_",
            "--video_headless"
        )
    }
    if ($f.run_args) {
        $args += $f.run_args.Split(' ', [System.StringSplitOptions]::RemoveEmptyEntries)
    }

    Push-Location $fdir
    try {
        & $simExe @args *> $logPath
        $simExit = $LASTEXITCODE
        "$simExit" | Out-File -Encoding ascii (Join-Path $fdir "gtsim_exit_code.txt")
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
        $frameCount = if ($frames) { $frames.Count } else { 0 }
        "$frameCount" | Out-File -Encoding ascii (Join-Path $fdir "frame_count.txt")

        if ($frameCount -eq 0) {
            "No captured frames found from GT_Sim direct capture." | Out-File -Encoding utf8 (Join-Path $fdir "video_error.txt")
        }
        elseif (-not $ffmpegPath) {
            "ffmpeg not found. MP4 conversion skipped." | Out-File -Encoding utf8 (Join-Path $fdir "video_error.txt")
        }

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
        }

        if (-not $KeepFrames) {
            Remove-Item -Path (Join-Path $fdir "screen_shot_*.tga") -Force -ErrorAction SilentlyContinue
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
