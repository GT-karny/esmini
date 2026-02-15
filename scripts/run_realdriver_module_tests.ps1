param(
    [string]$BuildDir = "build",
    [string]$PythonExe = "python",
    [string]$Config = "Debug"
)
$ErrorActionPreference = "Stop"

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
