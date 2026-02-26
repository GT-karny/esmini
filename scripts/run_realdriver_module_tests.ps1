param(
    [string]$BuildDir = "build",
    [string]$VenvDir = "venv",
    [string]$PythonExe = "",
    [string]$Config = "Debug"
)
$ErrorActionPreference = "Stop"

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

$repoRoot = (Get-Location).Path
$PythonExe = Resolve-VenvPython -RepoRoot $repoRoot -VenvDir $VenvDir -RequestedPython $PythonExe
Write-Host "[ModuleTests] Python (venv): $PythonExe"

Write-Host "[ModuleTests] Running C++ realdriver-focused unit tests"
$ctestDir = Join-Path $BuildDir "GT_esmini/test"
if (Test-Path $ctestDir) {
    Push-Location $ctestDir
    try {
        ctest -C $Config -R "test_ScenarioReaderParsing" --output-on-failure
    }
    finally {
        Pop-Location
    }
} else {
    Write-Warning "CTest directory not found: $ctestDir"
}

Write-Host "[ModuleTests] Running DriverScript pytest suite"
Push-Location DriverScript
try {
    $env:PYTHONPATH = (Resolve-Path "..").Path
    & $PythonExe -m pytest tests -q
}
finally {
    Pop-Location
}
