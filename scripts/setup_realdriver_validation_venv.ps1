param(
    [string]$VenvDir = "venv",
    [string]$BasePython = "python",
    [switch]$InstallDriverScriptRequirements
)

$ErrorActionPreference = "Stop"

$repoRoot = (Get-Location).Path
$venvRoot = if ([System.IO.Path]::IsPathRooted($VenvDir)) { $VenvDir } else { Join-Path $repoRoot $VenvDir }
$venvPython = Join-Path $venvRoot "Scripts/python.exe"

if (-not (Test-Path $venvPython)) {
    Write-Host "[venv] Creating validation venv: $venvRoot"
    & $BasePython -m venv $venvRoot
}
else {
    Write-Host "[venv] Reusing existing venv: $venvRoot"
}

if (-not (Test-Path $venvPython)) {
    throw "venv python not found after creation: $venvPython"
}

$reqPath = Join-Path $repoRoot "scripts/requirements_realdriver_validation.txt"
if (-not (Test-Path $reqPath)) {
    throw "requirements file not found: $reqPath"
}

Write-Host "[venv] Upgrading pip/setuptools/wheel"
& $venvPython -m pip install --upgrade pip setuptools wheel

Write-Host "[venv] Installing validation requirements: $reqPath"
& $venvPython -m pip install -r $reqPath

if ($InstallDriverScriptRequirements) {
    $driverReq = Join-Path $repoRoot "DriverScript/requirements.txt"
    if (-not (Test-Path $driverReq)) {
        throw "DriverScript requirements not found: $driverReq"
    }
    Write-Host "[venv] Installing DriverScript requirements: $driverReq"
    & $venvPython -m pip install -r $driverReq
}

Write-Host "[venv] Ready: $venvPython"
