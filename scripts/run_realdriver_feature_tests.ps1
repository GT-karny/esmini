param(
    [string]$BuildDir = "build",
    [string]$VenvDir = "venv",
    [string]$PythonExe = "",
    [string]$MatrixPath = "GT_esmini/test/validation/pythondriver_feature_matrix.yaml",
    [string]$ThresholdPath = "GT_esmini/test/validation/kpi_thresholds.yaml",
    [string]$OutputRoot = "artifacts/pythondriver_features",
    [switch]$UpdateGolden,
    [string]$GoldenRoot = "golden/pythondriver_features",
    [string]$SimPath = "",
    [int]$Hz = 100,
    [bool]$NoRealtime = $true,
    [bool]$EnableVideo = $true,
    [ValidateSet("off", "balanced", "fastest")]
    [string]$VideoMode = "fastest",
    [int]$VideoOutputFps = 0,
    [string]$VideoPreset = "",
    [int]$VideoCrf = -1,
    [ValidateSet("auto", "libx264", "h264_nvenc")]
    [string]$VideoEncoder = "auto",
    [int]$VideoParallelJobs = 0,
    [ValidateSet("all", "fail_only", "list")]
    [string]$GenerateVideoFor = "all",
    [string]$VideoFeatureIds = "",
    [bool]$KeepRawFrames = $false,
    [switch]$KeepFrames,
    [string]$WindowSize = "1280x720",
    [bool]$EnableDriverScript = $false,
    [string]$DriverScriptPath = "DriverScript/pythondriver/examples/scenario_drive_embedded.py",
    [string]$DriverScriptExtraArgs = "",
    [int]$DriverStartupWaitSec = 2,
    [string]$DriverScriptOsiReceiverIp = "127.0.0.1",
    [int]$DriverScriptOsiPort = 48198
)

$ErrorActionPreference = "Stop"

# NOTE:
# GT_esmini now requires embedded Python support (PythonDriverController is mandatory).
# The GT_ENABLE_EMBEDDED_PYTHON option is removed; specifying it is rejected at configure time.

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

function Resolve-VenvPython {
    param(
        [string]$RepoRoot,
        [string]$VenvDir,
        [string]$RequestedPython
    )

    $venvRoot = if ([System.IO.Path]::IsPathRooted($VenvDir)) { $VenvDir } else { Join-Path $RepoRoot $VenvDir }
    $venvPy = Join-Path $venvRoot "Scripts/python.exe"
    if (-not (Test-Path $venvPy)) {
        throw "venv python not found: $venvPy. Run scripts/setup_realdriver_validation_venv.ps1 first."
    }

    $resolvedVenvPy = (Resolve-Path $venvPy).Path
    if ([string]::IsNullOrWhiteSpace($RequestedPython)) {
        return $resolvedVenvPy
    }

    if (-not (Test-Path $RequestedPython)) {
        throw "Requested -PythonExe not found: $RequestedPython"
    }
    $resolvedRequested = (Resolve-Path $RequestedPython).Path
    if ($resolvedRequested -ne $resolvedVenvPy) {
        throw "Python must be venv python. Expected: $resolvedVenvPy, got: $resolvedRequested"
    }
    return $resolvedRequested
}

function Resolve-VideoEncoder {
    param(
        [string]$RequestedEncoder,
        [string]$FfmpegPath
    )

    if ($RequestedEncoder -ne "auto") {
        return $RequestedEncoder
    }

    if (-not $FfmpegPath) {
        return "libx264"
    }

    try {
        $encText = & $FfmpegPath -hide_banner -encoders 2>$null | Out-String
        if ($encText -match "h264_nvenc") {
            return "h264_nvenc"
        }
    }
    catch {
        # fall back to CPU encoder
    }
    return "libx264"
}

function Invoke-VideoEncode {
    param(
        [string]$FfmpegPath,
        [string]$FeatureDir,
        [int]$InputHz,
        [int]$OutputFps,
        [string]$Encoder,
        [string]$Preset,
        [int]$Crf
    )

    $vf = if ($OutputFps -gt 0) { "fps=$OutputFps,format=yuv420p" } else { "format=yuv420p" }
    $args = @("-y", "-framerate", "$InputHz", "-i", "screen_shot_%05d.tga", "-vf", $vf)

    if ($Encoder -eq "h264_nvenc") {
        # Keep this conservative for broad compatibility.
        $args += @("-c:v", "h264_nvenc", "-preset", "fast", "-cq", "$Crf")
    }
    else {
        $args += @("-c:v", "libx264", "-preset", $Preset, "-crf", "$Crf")
    }
    $args += "result.mp4"

    Push-Location $FeatureDir
    try {
        & $FfmpegPath @args *> "video_encode.log"
        return $LASTEXITCODE
    }
    finally {
        Pop-Location
    }
}

if (-not (Test-Path $MatrixPath)) { throw "Matrix file not found: $MatrixPath" }
if (-not (Test-Path $ThresholdPath)) { throw "Threshold file not found: $ThresholdPath" }

$repoRoot = (Get-Location).Path
$PythonExe = Resolve-VenvPython -RepoRoot $repoRoot -VenvDir $VenvDir -RequestedPython $PythonExe
$outputRootAbs = [System.IO.Path]::GetFullPath((Join-Path $repoRoot $OutputRoot))
$resourcesRoot = Join-Path $repoRoot "resources"
$xodrRoot = Join-Path $resourcesRoot "xodr"
$xoscRoot = Join-Path $resourcesRoot "xosc"
$catalogRoot = Join-Path $xoscRoot "Catalogs"
$driverScriptAbs = if ([System.IO.Path]::IsPathRooted($DriverScriptPath)) { $DriverScriptPath } else { Join-Path $repoRoot $DriverScriptPath }
$driverWorkDir = Join-Path $repoRoot "DriverScript"
$driverXodrPath = Join-Path $resourcesRoot "xodr/fabriksgatan.xodr"
$osiLightCollectorScript = Join-Path $repoRoot "scripts/collect_osi_light_metrics.py"
$embeddedPythonHome = Join-Path $repoRoot "thirdparty/python-embed/python-3.12.10-embed-amd64"

if (-not (Test-Path $resourcesRoot)) { throw "Resources directory not found: $resourcesRoot" }
if (-not (Test-Path $xodrRoot)) { throw "OpenDRIVE resource directory not found: $xodrRoot" }
if (-not (Test-Path $xoscRoot)) { throw "OpenSCENARIO resource directory not found: $xoscRoot" }
if (-not (Test-Path $catalogRoot)) {
    Write-Warning "Catalog root not found: $catalogRoot (scenario loading may fail if catalogs are referenced)."
}
if ($EnableDriverScript) {
    if (-not (Test-Path $driverScriptAbs)) { throw "DriverScript entrypoint not found: $driverScriptAbs" }
    if (-not (Test-Path $driverWorkDir)) { throw "DriverScript working directory not found: $driverWorkDir" }
    if (-not (Test-Path $driverXodrPath)) { throw "OpenDRIVE map for DriverScript not found: $driverXodrPath" }
}
if (-not (Test-Path $osiLightCollectorScript)) {
    throw "OSI light collector script not found: $osiLightCollectorScript"
}

# Ensure python312.dll can be resolved when GT_ENABLE_EMBEDDED_PYTHON=ON builds are used.
if (Test-Path (Join-Path $embeddedPythonHome "python312.dll")) {
    $env:PATH = "$embeddedPythonHome;$env:PATH"
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

$keepFramesEffective = $KeepRawFrames -or $KeepFrames.IsPresent
$videoEnabledEffective = $EnableVideo -and ($VideoMode -ne "off")

if ($videoEnabledEffective) {
    if ($VideoMode -eq "fastest") {
        if ($VideoOutputFps -le 0) { $VideoOutputFps = 20 }
        if ([string]::IsNullOrWhiteSpace($VideoPreset)) { $VideoPreset = "ultrafast" }
        if ($VideoCrf -lt 0) { $VideoCrf = 28 }
    }
    elseif ($VideoMode -eq "balanced") {
        if ($VideoOutputFps -le 0) { $VideoOutputFps = 30 }
        if ([string]::IsNullOrWhiteSpace($VideoPreset)) { $VideoPreset = "veryfast" }
        if ($VideoCrf -lt 0) { $VideoCrf = 23 }
    }

    if ($VideoOutputFps -le 0) { $VideoOutputFps = [Math]::Min($Hz, 30) }
    if ($VideoCrf -lt 0) { $VideoCrf = 23 }
    if ([string]::IsNullOrWhiteSpace($VideoPreset)) { $VideoPreset = "veryfast" }
}

if ($VideoParallelJobs -le 0) {
    $logicalCpus = [Environment]::ProcessorCount
    $VideoParallelJobs = [Math]::Min(4, [Math]::Max(1, [int][Math]::Floor($logicalCpus / 2)))
}

$runId = Get-Date -Format "yyyyMMdd-HHmmss"
$runDir = Join-Path $outputRootAbs $runId
New-Item -ItemType Directory -Force -Path $runDir | Out-Null

$matrix = Read-StructuredFile -Path $MatrixPath
$features = $matrix.features
$resolvedVideoEncoder = "libx264"

Write-Host "[RealDriver] Run ID: $runId"
Write-Host "[RealDriver] GT_Sim: $simExe"
Write-Host "[RealDriver] Python (venv): $PythonExe"
if (Test-Path (Join-Path $embeddedPythonHome "python312.dll")) {
    Write-Host "[RealDriver] Embedded Python DLL path enabled: $embeddedPythonHome"
}
Write-Host "[RealDriver] Frequency: $Hz Hz"
Write-Host "[RealDriver] Execution mode: $(if ($NoRealtime) { "fastest (no realtime pacing)" } else { "realtime pacing" })"
if ($videoEnabledEffective) {
    $resolvedVideoEncoder = Resolve-VideoEncoder -RequestedEncoder $VideoEncoder -FfmpegPath $ffmpegPath
    $videoConfig = [ordered]@{
        enabled = $true
        mode = $VideoMode
        output_fps = $VideoOutputFps
        preset = $VideoPreset
        crf = $VideoCrf
        encoder_requested = $VideoEncoder
        encoder_resolved = $resolvedVideoEncoder
        parallel_jobs = $VideoParallelJobs
        generate_for = $GenerateVideoFor
        feature_ids = $VideoFeatureIds
    }
    ($videoConfig | ConvertTo-Json -Depth 4) | Out-File -Encoding utf8 (Join-Path $runDir "video_config.json")
    if ($ffmpegPath) {
        Write-Host "[RealDriver] Video: enabled ($VideoMode, encoder=$resolvedVideoEncoder, fps=$VideoOutputFps, jobs=$VideoParallelJobs)"
        Write-Host "[RealDriver] Video backend: GT_Sim direct capture ($WindowSize, ffmpeg=$ffmpegPath)"
    }
    else {
        Write-Host "[RealDriver] Video: enabled ($WindowSize), ffmpeg not found (MP4 conversion will be skipped)"
    }
}
elseif ($EnableVideo) {
    Write-Host "[RealDriver] Video: disabled by VideoMode=off"
}
else {
    Write-Host "[RealDriver] Video: disabled"
}
Write-Host "[RealDriver] DriverScript: $(if ($EnableDriverScript) { "enabled ($driverScriptAbs)" } else { "disabled" })"

$videoJobs = New-Object System.Collections.ArrayList

foreach ($f in $features) {
    $fid = $f.id
    $scenario = $f.scenario
    $fdir = Join-Path $runDir $fid
    New-Item -ItemType Directory -Force -Path $fdir | Out-Null

    if (-not (Test-Path $scenario)) {
        "Missing scenario: $scenario" | Out-File -Encoding utf8 (Join-Path $fdir "runner_error.txt")
        continue
    }

    $collectOsiLightMetrics = $false
    if ($null -ne $f.collect_osi_light_metrics) {
        $collectOsiLightMetrics = [bool]$f.collect_osi_light_metrics
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
    if ($NoRealtime) {
        $args += "--no_realtime"
    }
    if (Test-Path $catalogRoot) {
        $catalogDirs = Get-ChildItem -Path $catalogRoot -Directory -ErrorAction SilentlyContinue
        foreach ($catalogDir in $catalogDirs) {
            $args += @("--path", $catalogDir.FullName)
        }
    }
    if ($videoEnabledEffective) {
        $args += @(
            "--video_capture",
            "--video_window", "$windowWidth", "$windowHeight",
            "--video_frames", "-1",
            "--video_prefix", "screen_shot_",
            "--video_headless"
        )
    }
    if (($EnableDriverScript -or $collectOsiLightMetrics) -and $DriverScriptOsiReceiverIp) {
        $args += @("--osi", $DriverScriptOsiReceiverIp)
    }
    if ($f.run_args) {
        $args += $f.run_args.Split(' ', [System.StringSplitOptions]::RemoveEmptyEntries)
    }

    $driverProc = $null
    $osiCollectorProc = $null
    if ($collectOsiLightMetrics) {
        $collectorOut = Join-Path $fdir "osi_collector_stdout.txt"
        $collectorErr = Join-Path $fdir "osi_collector_stderr.txt"
        $collectorArgs = @(
            "-u",
            $osiLightCollectorScript,
            "--host", $DriverScriptOsiReceiverIp,
            "--port", "$DriverScriptOsiPort",
            "--timeout", "0.5",
            "--duration", "12.0",
            "--json-out", (Join-Path $fdir "osi_light_metrics.json"),
            "--csv-out", (Join-Path $fdir "osi_lights.csv")
        )
        $osiCollectorProc = Start-Process -FilePath $PythonExe -ArgumentList $collectorArgs -WorkingDirectory $repoRoot -RedirectStandardOutput $collectorOut -RedirectStandardError $collectorErr -PassThru
        Start-Sleep -Milliseconds 500
        if ($osiCollectorProc.HasExited) {
            "OSI collector exited early with code $($osiCollectorProc.ExitCode)" | Out-File -Encoding utf8 (Join-Path $fdir "runner_error.txt")
        }
    }

    if ($EnableDriverScript) {
        $driverOut = Join-Path $fdir "python_stdout.txt"
        $driverErr = Join-Path $fdir "python_stderr.txt"
        $driverArgs = @(
            "-u",
            $driverScriptAbs,
            "--mode", "udp",
            "--xodr_path", $driverXodrPath,
            "--port", "53995",
            "--target_speed_port", "54995",
            "--osi_port", "$DriverScriptOsiPort"
        )
        if ($collectOsiLightMetrics) {
            $driverArgs += @(
                "--collect_osi_light_metrics",
                "--light_metrics_out", (Join-Path $fdir "osi_light_metrics.json"),
                "--light_metrics_csv_out", (Join-Path $fdir "osi_lights.csv"),
                "--max_runtime_s", "14.0"
            )
        }
        if ($DriverScriptExtraArgs) {
            $driverArgs += $DriverScriptExtraArgs.Split(' ', [System.StringSplitOptions]::RemoveEmptyEntries)
        }
        if ($f.driverscript_extra_args) {
            $driverArgs += $f.driverscript_extra_args.Split(' ', [System.StringSplitOptions]::RemoveEmptyEntries)
        }
        $driverProc = Start-Process -FilePath $PythonExe -ArgumentList $driverArgs -WorkingDirectory $driverWorkDir -RedirectStandardOutput $driverOut -RedirectStandardError $driverErr -PassThru
        Start-Sleep -Seconds $DriverStartupWaitSec
        if ($driverProc.HasExited) {
            "DriverScript exited early with code $($driverProc.ExitCode)" | Out-File -Encoding utf8 (Join-Path $fdir "runner_error.txt")
        }
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
        if ($driverProc) {
            if (-not $driverProc.WaitForExit(5000)) {
                Stop-Process -Id $driverProc.Id -Force -ErrorAction SilentlyContinue
                Start-Sleep -Milliseconds 250
            }
            try {
                if ($driverProc.HasExited) {
                    "$($driverProc.ExitCode)" | Out-File -Encoding ascii (Join-Path $fdir "driverscript_exit_code.txt")
                }
                else {
                    "terminated" | Out-File -Encoding ascii (Join-Path $fdir "driverscript_exit_code.txt")
                }
            }
            catch {
                "unknown" | Out-File -Encoding ascii (Join-Path $fdir "driverscript_exit_code.txt")
            }
        }
        if ($osiCollectorProc) {
            if (-not $osiCollectorProc.WaitForExit(20000)) {
                Stop-Process -Id $osiCollectorProc.Id -Force -ErrorAction SilentlyContinue
                Start-Sleep -Milliseconds 200
            }
            try {
                if ($osiCollectorProc.HasExited) {
                    "$($osiCollectorProc.ExitCode)" | Out-File -Encoding ascii (Join-Path $fdir "osi_collector_exit_code.txt")
                }
                else {
                    "terminated" | Out-File -Encoding ascii (Join-Path $fdir "osi_collector_exit_code.txt")
                }
            }
            catch {
                "unknown" | Out-File -Encoding ascii (Join-Path $fdir "osi_collector_exit_code.txt")
            }
        }
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

    if ($videoEnabledEffective) {
        $frames = Get-ChildItem -Path $fdir -Filter "screen_shot_*.tga" -File -ErrorAction SilentlyContinue
        $frameCount = if ($frames) { $frames.Count } else { 0 }
        "$frameCount" | Out-File -Encoding ascii (Join-Path $fdir "frame_count.txt")

        $videoErrorPath = Join-Path $fdir "video_error.txt"
        if (Test-Path $videoErrorPath) {
            Remove-Item -Path $videoErrorPath -Force -ErrorAction SilentlyContinue
        }

        $job = [ordered]@{
            id = $fid
            feature_dir = (Resolve-Path $fdir).Path
            frame_count = $frameCount
            can_convert = ($frameCount -gt 0 -and -not [string]::IsNullOrWhiteSpace($ffmpegPath))
        }
        [void]$videoJobs.Add([pscustomobject]$job)

        if ($frameCount -eq 0) {
            "No captured frames found from GT_Sim direct capture." | Out-File -Encoding utf8 $videoErrorPath
        }
        elseif (-not $ffmpegPath) {
            "ffmpeg not found. MP4 conversion skipped." | Out-File -Encoding utf8 $videoErrorPath
        }
    }
}

$u = @()
if ($UpdateGolden) { $u += '--update-golden' }

if ($videoEnabledEffective -and $videoJobs.Count -gt 0) {
    ($videoJobs | ConvertTo-Json -Depth 4) | Out-File -Encoding utf8 (Join-Path $runDir "video_jobs.json")

    $targetIds = New-Object System.Collections.Generic.HashSet[string]
    if ($GenerateVideoFor -eq "all") {
        foreach ($v in $videoJobs) { [void]$targetIds.Add([string]$v.id) }
    }
    elseif ($GenerateVideoFor -eq "list") {
        foreach ($id in ($VideoFeatureIds -split ",")) {
            $trimmed = $id.Trim()
            if (-not [string]::IsNullOrWhiteSpace($trimmed)) {
                [void]$targetIds.Add($trimmed)
            }
        }
    }
    elseif ($GenerateVideoFor -eq "fail_only") {
        & $PythonExe archive/frozen_python_verification/scripts/validate_realdriver_feature_results.py --matrix $MatrixPath --thresholds $ThresholdPath --run-dir $runDir --golden-root $GoldenRoot
        $preSummaryPath = Join-Path $runDir "summary.json"
        if (Test-Path $preSummaryPath) {
            $preSummary = Get-Content -Raw -Path $preSummaryPath | ConvertFrom-Json
            foreach ($r in $preSummary.results) {
                if (-not [bool]$r.pass) {
                    [void]$targetIds.Add([string]$r.id)
                }
            }
        }
    }

    $convertJobs = @()
    foreach ($v in $videoJobs) {
        $featureDir = [string]$v.feature_dir
        $videoErrorPath = Join-Path $featureDir "video_error.txt"
        $shouldGenerate = $targetIds.Contains([string]$v.id)
        $canConvert = [bool]$v.can_convert

        if (-not $shouldGenerate) {
            "Video generation skipped by policy: $GenerateVideoFor" | Out-File -Encoding utf8 $videoErrorPath
            continue
        }
        if (-not $canConvert) {
            if (-not (Test-Path $videoErrorPath)) {
                "Video generation skipped (conversion prerequisites not met)." | Out-File -Encoding utf8 $videoErrorPath
            }
            continue
        }

        $convertJobs += $v
    }

        if ($convertJobs.Count -gt 0) {
            $running = @()
            $completedResults = @()
            foreach ($jobDef in $convertJobs) {
                while ($running.Count -ge $VideoParallelJobs) {
                    $finished = Wait-Job -Job $running -Any
                    $jobResult = Receive-Job -Job $finished
                    Remove-Job -Job $finished -Force
                    if ($jobResult) {
                        $completedResults += $jobResult
                    }
                    $running = @($running | Where-Object { $_.Id -ne $finished.Id })
                }

            $psJob = Start-Job -ScriptBlock {
                param($ffmpegPathArg, $featureDirArg, $hzArg, $videoOutputFpsArg, $encoderArg, $presetArg, $crfArg)
                $exitCode = 0
                Push-Location $featureDirArg
                try {
                    $vf = if ($videoOutputFpsArg -gt 0) { "fps=$videoOutputFpsArg,format=yuv420p" } else { "format=yuv420p" }
                    $args = @("-y", "-framerate", "$hzArg", "-i", "screen_shot_%05d.tga", "-vf", $vf)
                    if ($encoderArg -eq "h264_nvenc") {
                        $args += @("-c:v", "h264_nvenc", "-preset", "fast", "-cq", "$crfArg")
                    } else {
                        $args += @("-c:v", "libx264", "-preset", $presetArg, "-crf", "$crfArg")
                    }
                    $args += "result.mp4"
                    & $ffmpegPathArg @args *> "video_encode.log"
                    $exitCode = $LASTEXITCODE
                }
                finally {
                    Pop-Location
                }
                [pscustomobject]@{
                    feature_dir = $featureDirArg
                    exit_code = $exitCode
                    encoder = $encoderArg
                }
            } -ArgumentList $ffmpegPath, ([string]$jobDef.feature_dir), $Hz, $VideoOutputFps, $resolvedVideoEncoder, $VideoPreset, $VideoCrf

            $running += $psJob
        }

            if ($running.Count -gt 0) {
                Wait-Job -Job $running | Out-Null
                foreach ($j in $running) {
                    $result = Receive-Job -Job $j
                    Remove-Job -Job $j -Force
                    if ($result) {
                        $completedResults += $result
                    }
                }
            }

            foreach ($result in $completedResults) {
                $featureDir = [string]$result.feature_dir
                $videoErrorPath = Join-Path $featureDir "video_error.txt"
                $logPath = Join-Path $featureDir "video_encode.log"
                if ([int]$result.exit_code -ne 0) {
                    if ([string]$result.encoder -eq "h264_nvenc") {
                        Add-Content -Path $logPath -Value "`n[run_realdriver_feature_tests] NVENC failed, retrying with libx264."
                        $fallbackExit = Invoke-VideoEncode -FfmpegPath $ffmpegPath -FeatureDir $featureDir -InputHz $Hz -OutputFps $VideoOutputFps -Encoder "libx264" -Preset $VideoPreset -Crf $VideoCrf
                        if ($fallbackExit -ne 0) {
                            "ffmpeg conversion failed with code $fallbackExit (fallback libx264)" | Out-File -Encoding utf8 $videoErrorPath
                        }
                    }
                    else {
                        "ffmpeg conversion failed with code $($result.exit_code)" | Out-File -Encoding utf8 $videoErrorPath
                    }
                }
            }
        }

    if (-not $keepFramesEffective) {
        foreach ($v in $videoJobs) {
            Remove-Item -Path (Join-Path ([string]$v.feature_dir) "screen_shot_*.tga") -Force -ErrorAction SilentlyContinue
        }
    }
}

& $PythonExe archive/frozen_python_verification/scripts/validate_realdriver_feature_results.py --matrix $MatrixPath --thresholds $ThresholdPath --run-dir $runDir --golden-root $GoldenRoot @u
$validateExit = $LASTEXITCODE
& $PythonExe scripts/render_realdriver_report.py --run-dir $runDir
Write-Host "[RealDriver] Completed. Report: $runDir/report.html"
if ($validateExit -ne 0) {
    exit $validateExit
}
