<#
.SYNOPSIS
    GT_Sim Web backend / PyInstaller build 用の専用 venv をセットアップする。

.DESCRIPTION
    GT_esmini/web/.venv を作成し、以下をインストールする:
        - GT_esmini/web/pyproject.toml の依存（fastapi, watchdog, etc.）
        - DriverScript/ パッケージ（spec の hiddenimports で realdriver を参照するため）
        - PyInstaller, grpcio-tools（パッケージビルド用ツール）

    DriverScript/.venv は DriverScript ランタイム専用に分離されるため、
    本スクリプトは DriverScript/.venv を変更しない。

.PARAMETER PythonExe
    venv 作成に使う Python 3.12 実行ファイル。省略時は `py -3.12` を使用。

.PARAMETER Recreate
    既存の web/.venv を削除して作り直す。

.EXAMPLE
    .\scripts\setup_web_venv.ps1
    .\scripts\setup_web_venv.ps1 -Recreate
#>

[CmdletBinding()]
param(
    [string]$PythonExe = "",
    [switch]$Recreate
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$RepoRoot   = (Resolve-Path "$PSScriptRoot\..").Path
$VenvDir    = Join-Path $RepoRoot "GT_esmini\web\.venv"
$WebDir     = Join-Path $RepoRoot "GT_esmini\web"
$DriverDir  = Join-Path $RepoRoot "DriverScript"
$VenvPython = Join-Path $VenvDir "Scripts\python.exe"

function Write-Step([string]$Msg) {
    Write-Host "`n========== $Msg ==========" -ForegroundColor Cyan
}

function Invoke-Checked([string]$Description, [scriptblock]$Block) {
    Write-Host "  -> $Description" -ForegroundColor Gray
    & $Block
    if ($LASTEXITCODE -and $LASTEXITCODE -ne 0) {
        Write-Host "  !! FAILED: $Description (exit $LASTEXITCODE)" -ForegroundColor Red
        exit $LASTEXITCODE
    }
}

# Resolve base Python
if (-not $PythonExe) {
    $PythonExe = "py"
    $pythonArgs = @("-3.12")
} else {
    $pythonArgs = @()
}

Write-Step "Setup web venv"
Write-Host "  Target: $VenvDir"

if ($Recreate -and (Test-Path $VenvDir)) {
    Write-Host "  -> Removing existing venv" -ForegroundColor Yellow
    Remove-Item -Recurse -Force $VenvDir
}

if (-not (Test-Path $VenvPython)) {
    Invoke-Checked "Create venv (Python 3.12)" {
        & $PythonExe @pythonArgs -m venv $VenvDir
    }
} else {
    Write-Host "  -> Reusing existing venv at $VenvDir" -ForegroundColor Gray
}

Invoke-Checked "Upgrade pip" {
    & $VenvPython -m pip install --upgrade pip
}

Invoke-Checked "Install GT_esmini/web (editable, with deps from pyproject.toml)" {
    & $VenvPython -m pip install -e $WebDir
}

# DriverScript provides realdriver/ and pythondriver/ packages referenced by
# the PyInstaller spec via hiddenimports + pathex. Install (non-editable is
# fine; we just need importability at build time).
if (Test-Path (Join-Path $DriverDir "setup.py")) {
    Invoke-Checked "Install DriverScript (for realdriver bundling)" {
        & $VenvPython -m pip install $DriverDir
    }
} else {
    Write-Host "  !! WARNING: DriverScript/setup.py not found — realdriver bundling may fail" -ForegroundColor Yellow
}

Invoke-Checked "Install build tooling (pyinstaller, grpcio-tools)" {
    & $VenvPython -m pip install "pyinstaller>=6.0" "grpcio-tools>=1.78"
}

Write-Host "`n  OK: web venv ready at $VenvDir" -ForegroundColor Green
Write-Host "      Use: $VenvPython"
