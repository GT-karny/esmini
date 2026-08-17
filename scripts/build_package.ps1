<#
.SYNOPSIS
    GT_Sim 配布パッケージをフルビルドする。

.DESCRIPTION
    C++ビルド → PyInstaller サーバー → Electron デスクトップアプリの順で
    パッケージを作成し、ZIP アーカイブにまとめる。

.PARAMETER Version
    パッケージバージョン文字列 (必須)。例: 0.9.0

.PARAMETER SkipCMake
    CMake configure + C++ ビルドをスキップする。

.PARAMETER SkipFrontend
    フロントエンドビルド (npm run build) をスキップする。

.PARAMETER SkipPyInstaller
    PyInstaller ビルドをスキップする。

.PARAMETER SkipElectron
    Electron ビルド + パッケージ化をスキップする。

.PARAMETER NoZip
    ZIP アーカイブ作成をスキップする。

.EXAMPLE
    .\scripts\build_package.ps1 -Version 0.9.0
    .\scripts\build_package.ps1 -Version 0.9.0 -SkipCMake -SkipFrontend
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Version,

    [switch]$SkipCMake,
    [switch]$SkipFrontend,
    [switch]$SkipPyInstaller,
    [switch]$SkipElectron,
    [switch]$NoZip
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# ── Paths ──────────────────────────────────────────────────────────
$RepoRoot       = (Resolve-Path "$PSScriptRoot\..").Path
$BuildDir       = Join-Path $RepoRoot "build"
$BuildRelease   = Join-Path $BuildDir "GT_esmini\Release"
$RMLibRelease   = Join-Path $BuildDir "EnvironmentSimulator\Libraries\esminiRMLib\Release"
$SDL2Dll        = Join-Path $RepoRoot "thirdparty\SDL2\lib\x64\SDL2.dll"
$DriverBin      = Join-Path $RepoRoot "DriverScript\bin"
# Build venv (PyInstaller + web backend deps). Created via setup_web_venv.ps1.
$VenvPython     = Join-Path $RepoRoot "GT_esmini\web\.venv\Scripts\python.exe"
$EmbedPython    = Join-Path $RepoRoot "thirdparty\python-embed\python-3.12.10-embed-amd64"
$FrontendDir    = Join-Path $RepoRoot "GT_esmini\web\frontend"
$ElectronDir    = Join-Path $RepoRoot "GT_esmini\web\electron"
$BuildPackagePy = Join-Path $RepoRoot "GT_esmini\web\pyinstaller\build_package.py"
$DistDir        = Join-Path $RepoRoot "dist"
$PackageDir     = Join-Path $DistDir "GT_Sim_v$Version"

# ── Helpers ────────────────────────────────────────────────────────
function Write-Step([string]$Step, [string]$Message) {
    Write-Host "`n========== [$Step] $Message ==========" -ForegroundColor Cyan
}

function Invoke-Checked([string]$Description, [scriptblock]$Block) {
    Write-Host "  -> $Description" -ForegroundColor Gray
    & $Block
    if ($LASTEXITCODE -and $LASTEXITCODE -ne 0) {
        Write-Host "  !! FAILED: $Description (exit code $LASTEXITCODE)" -ForegroundColor Red
        exit $LASTEXITCODE
    }
}

# ── Prerequisites ──────────────────────────────────────────────────
Write-Step "Pre" "Checking prerequisites"

if (-not (Test-Path $EmbedPython)) {
    Write-Host "ERROR: Embedded Python not found at $EmbedPython" -ForegroundColor Red
    exit 1
}
if (-not (Test-Path $VenvPython)) {
    Write-Host "ERROR: build venv Python not found at $VenvPython" -ForegroundColor Red
    Write-Host "       Run: .\scripts\setup_web_venv.ps1" -ForegroundColor Yellow
    exit 1
}
if (-not (Get-Command node -ErrorAction SilentlyContinue)) {
    Write-Host "ERROR: Node.js is not installed or not in PATH" -ForegroundColor Red
    exit 1
}

Write-Host "  OK: All prerequisites met" -ForegroundColor Green

# ── Step 0+1: CMake Configure + C++ Build ─────────────────────────
if (-not $SkipCMake) {
    Write-Step "0" "CMake Configure"
    Invoke-Checked "cmake configure" {
        cmake -S $RepoRoot -B $BuildDir -G "Visual Studio 17 2022" -A x64 `
            -DUSE_OSG=ON `
            -DUSE_OSI=ON `
            -DUSE_SUMO=ON `
            -DUSE_IMPLOT=ON `
            -DGT_ENABLE_SDL2=ON `
            -DGT_ENABLE_EMBEDDED_PYTHON=ON  # distribution keeps the v0.8-frozen PythonDriver; dev default is OFF
    }

    Write-Step "1" "C++ Build (Release)"
    Invoke-Checked "cmake build" {
        cmake --build $BuildDir --config Release --target GT_Sim GT_esminiLib esminiRMLib GT_RoadGen GT_WheelProbe
    }
} else {
    Write-Host "`n  -- Skipping CMake configure + C++ build --" -ForegroundColor Yellow
}

# ── Step 1b: Stage build artifacts (always runs, even with -SkipCMake) ─
# These copies must run every build so build_package.py's *.dll glob picks
# up esminiRMLib.dll and SDL2.dll on packaging.
Write-Step "1b" "Stage build artifacts into BuildRelease + DriverScript/bin"
if (-not (Test-Path $DriverBin)) { New-Item -ItemType Directory -Path $DriverBin -Force | Out-Null }
Copy-Item "$BuildRelease\*.dll" $DriverBin -Force -ErrorAction SilentlyContinue
Copy-Item "$BuildRelease\GT_Sim.exe" $DriverBin -Force -ErrorAction SilentlyContinue
# GT_RoadGen.exe: parallel OpenDRIVE->.osgb road-mesh generator. GT_esminiLib spawns it from bin/
# to pre-generate + cache the road model (skips the slow single-threaded core generation), so it
# MUST be built (Step 1 target) and bundled alongside GT_Sim.exe.
if (Test-Path "$BuildRelease\GT_RoadGen.exe") {
    Copy-Item "$BuildRelease\GT_RoadGen.exe" $DriverBin -Force -ErrorAction SilentlyContinue
} else {
    Write-Host "  !! WARNING: GT_RoadGen.exe not found at $BuildRelease — large OpenDRIVE road generation will be slow / may hang" -ForegroundColor Yellow
}
# GT_WheelProbe.exe (feature:F8): the web UI's axis-assignment panel spawns it to
# show live axis values. Without it the panel can still edit the mapping by hand
# but has no way to tell the user WHICH axis is which on their wheel — the whole
# point of the feature. Named in the --target list above for the same reason
# GT_RoadGen is: this script builds specific targets, not ALL_BUILD.
if (Test-Path "$BuildRelease\GT_WheelProbe.exe") {
    Copy-Item "$BuildRelease\GT_WheelProbe.exe" $DriverBin -Force -ErrorAction SilentlyContinue
} else {
    Write-Host "  !! WARNING: GT_WheelProbe.exe not found at $BuildRelease — the wheel axis mapping panel will have no live axis readout" -ForegroundColor Yellow
}
# esminiRMLib.dll lives under EnvironmentSimulator/Libraries/esminiRMLib/Release
# — stage into $BuildRelease so build_package.py's *.dll glob picks it up.
if (Test-Path "$RMLibRelease\esminiRMLib.dll") {
    Copy-Item "$RMLibRelease\esminiRMLib.dll" $BuildRelease -Force
    Copy-Item "$RMLibRelease\esminiRMLib.dll" $DriverBin -Force
} else {
    Write-Host "  !! WARNING: esminiRMLib.dll not found at $RMLibRelease — 2D Viewer will show no roads" -ForegroundColor Yellow
}
# SDL2.dll is required at runtime when built with GT_ENABLE_SDL2=ON.
#
# Copied only when the destination differs. A running GT_Sim (or the axis probe)
# holds SDL2.dll open, and an unconditional Copy-Item then aborts the whole
# packaging run — which is what happened while a simulation from a parallel
# session was up. The source is a fixed thirdparty binary, so "already identical"
# is the normal case and skipping it loses nothing; a genuine mismatch that
# cannot be written is still reported, because shipping a stale SDL2.dll would be
# a silent runtime failure.
function Copy-IfDifferent($src, $dstDir, $label) {
    $dst = Join-Path $dstDir (Split-Path $src -Leaf)
    # Size comparison, not Get-FileHash: that cmdlet did not resolve in the host
    # this script is launched from (module auto-loading is not available there),
    # and it is enough here -- the source is a fixed thirdparty binary, so the
    # only realistic states are "byte-identical" and "absent/replaced wholesale".
    if ((Test-Path $dst) -and (Get-Item $src).Length -eq (Get-Item $dst).Length) {
        return
    }
    try {
        Copy-Item $src $dstDir -Force -ErrorAction Stop
    } catch {
        Write-Host "  !! WARNING: could not update $label at $dstDir (file in use by a running process?) — $($_.Exception.Message)" -ForegroundColor Yellow
    }
}
if (Test-Path $SDL2Dll) {
    Copy-IfDifferent $SDL2Dll $BuildRelease "SDL2.dll"
    Copy-IfDifferent $SDL2Dll $DriverBin "SDL2.dll"
} else {
    Write-Host "  !! WARNING: SDL2.dll not found at $SDL2Dll — ManualDrive wheel input will not work" -ForegroundColor Yellow
}
Write-Host "  OK: Artifacts staged" -ForegroundColor Green

# ── Step 2: build_package.py (Frontend + PyInstaller + Assembly) ──
Write-Step "2" "build_package.py (Frontend + PyInstaller + Assembly)"

$buildPkgArgs = @($BuildPackagePy, "--version", $Version, "--output", $DistDir, "--no-zip")
if ($SkipFrontend)    { $buildPkgArgs += "--skip-frontend" }
if ($SkipPyInstaller) { $buildPkgArgs += "--skip-pyinstaller" }

Invoke-Checked "build_package.py" {
    & $VenvPython @buildPkgArgs
}

# ── Step 3: Electron Build + Package ──────────────────────────────
if (-not $SkipElectron) {
    Write-Step "3" "Electron Build"

    Push-Location $ElectronDir
    try {
        Invoke-Checked "npm install" { npm install }
        Invoke-Checked "npm run build" { npm run build }

        Write-Host "  -> Running @electron/packager ..." -ForegroundColor Gray
        Invoke-Checked "electron packager" {
            npx @electron/packager . GT_Sim `
                --platform=win32 --arch=x64 `
                --out=release --overwrite `
                --ignore="node_modules" `
                --ignore="src" `
                --ignore="scripts" `
                --ignore="tsconfig" `
                --ignore="electron-builder.yml"
        }
    } finally {
        Pop-Location
    }

    # Merge Electron output into package directory
    $electronOut = Join-Path $ElectronDir "release\GT_Sim-win32-x64"
    if (Test-Path $electronOut) {
        Write-Host "  -> Merging Electron build into $PackageDir" -ForegroundColor Gray
        Copy-Item "$electronOut\*" $PackageDir -Recurse -Force
        Write-Host "  OK: Electron merged" -ForegroundColor Green
    } else {
        Write-Host "  !! Electron output not found at $electronOut" -ForegroundColor Red
        exit 1
    }

    # Step 3b: Strip unnecessary locales
    Write-Step "3b" "Electron locale cleanup"
    $localesDir = Join-Path $PackageDir "locales"
    if (Test-Path $localesDir) {
        Get-ChildItem $localesDir -File | Where-Object { $_.Name -ne "en-US.pak" } | Remove-Item -Force
        Write-Host "  OK: Kept only en-US.pak" -ForegroundColor Green
    }
} else {
    Write-Host "`n  -- Skipping Electron build --" -ForegroundColor Yellow
}

# ── Step 4: ZIP ───────────────────────────────────────────────────
if (-not $NoZip) {
    Write-Step "4" "Creating ZIP archive"
    $zipPath = "$PackageDir.zip"
    if (Test-Path $zipPath) { Remove-Item $zipPath -Force }
    Compress-Archive -Path $PackageDir -DestinationPath $zipPath -Force
    Write-Host "  OK: $zipPath" -ForegroundColor Green
}

# ── Summary ────────────────────────────────────────────────────────
Write-Host "`n"
Write-Host "==========================================" -ForegroundColor Green
Write-Host "  Build Complete: GT_Sim v$Version" -ForegroundColor Green
Write-Host "==========================================" -ForegroundColor Green

if (Test-Path $BuildRelease) {
    Write-Host "`n  C++ Artifacts:" -ForegroundColor White
    Get-ChildItem "$BuildRelease\*.dll", "$BuildRelease\*.exe" -ErrorAction SilentlyContinue |
        ForEach-Object { Write-Host "    $($_.Name)  ($([math]::Round($_.Length / 1MB, 1)) MB)" }
}

if (Test-Path $PackageDir) {
    Write-Host "`n  Package Directory: $PackageDir" -ForegroundColor White
    $totalSize = (Get-ChildItem $PackageDir -Recurse -File | Measure-Object -Property Length -Sum).Sum
    Write-Host "    Total size: $([math]::Round($totalSize / 1MB, 1)) MB"
}

$zipPath = "$PackageDir.zip"
if (Test-Path $zipPath) {
    $zipSize = (Get-Item $zipPath).Length
    Write-Host "`n  ZIP: $zipPath ($([math]::Round($zipSize / 1MB, 1)) MB)" -ForegroundColor White
}

Write-Host "`n  Launch: $PackageDir\GT_Sim.exe" -ForegroundColor Cyan
Write-Host ""
