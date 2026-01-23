<#
.SYNOPSIS
    GT_esmini Build Script

.DESCRIPTION
    Builds the GT_esmini project using CMake and Visual Studio 2022.

.PARAMETER Config
    Build configuration: Release (default) or Debug

.PARAMETER Clean
    Clean the build directory before building

.PARAMETER Configure
    Run CMake configure step (required for first build or after CMakeLists.txt changes)

.EXAMPLE
    .\build.ps1
    Build Release configuration

.EXAMPLE
    .\build.ps1 -Config Debug
    Build Debug configuration

.EXAMPLE
    .\build.ps1 -Configure -Clean
    Clean and reconfigure before building
#>

param(
    [ValidateSet("Release", "Debug")]
    [string]$Config = "Release",

    [switch]$Clean,
    [switch]$Configure
)

# Get script directory (project root)
$ProjectRoot = $PSScriptRoot
$BuildDir = Join-Path $ProjectRoot "build"

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  GT_esmini Build Script" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Project Root: $ProjectRoot"
Write-Host "Build Dir:    $BuildDir"
Write-Host "Config:       $Config"
Write-Host ""

# Clean build directory if requested
if ($Clean) {
    if (Test-Path $BuildDir) {
        Write-Host "Cleaning build directory..." -ForegroundColor Yellow
        Remove-Item -Recurse -Force $BuildDir
    }
}

# Create build directory if it doesn't exist
if (-not (Test-Path $BuildDir)) {
    Write-Host "Creating build directory..." -ForegroundColor Yellow
    New-Item -ItemType Directory -Path $BuildDir | Out-Null
    $Configure = $true  # Force configure if build dir is new
}

# Configure with CMake if requested or if build dir is new
if ($Configure -or -not (Test-Path (Join-Path $BuildDir "CMakeCache.txt"))) {
    Write-Host ""
    Write-Host "Configuring with CMake..." -ForegroundColor Green
    Write-Host "----------------------------------------"

    Push-Location $BuildDir
    try {
        cmake -S $ProjectRoot -B $BuildDir -G "Visual Studio 17 2022" -A x64
        if ($LASTEXITCODE -ne 0) {
            Write-Host "CMake configure failed!" -ForegroundColor Red
            exit $LASTEXITCODE
        }
    }
    finally {
        Pop-Location
    }
}

# Build
Write-Host ""
Write-Host "Building ($Config)..." -ForegroundColor Green
Write-Host "----------------------------------------"

cmake --build $BuildDir --config $Config --parallel
if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "Build failed!" -ForegroundColor Red
    exit $LASTEXITCODE
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Green
Write-Host "  Build completed successfully!" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green
Write-Host ""
Write-Host "Output binaries: $BuildDir\GT_esmini\$Config\" -ForegroundColor Cyan
