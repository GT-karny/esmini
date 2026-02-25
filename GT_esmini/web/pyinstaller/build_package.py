"""Build GT_Sim Web distributable package.

Usage:
    DriverScript\\.venv\\Scripts\\python.exe GT_esmini/web/pyinstaller/build_package.py \\
        --version 0.1.0 --output dist/

Steps:
    1. Verify prerequisites (build artifacts, frontend dist)
    2. (Optional) Build frontend
    3. Run PyInstaller to create frozen web server
    4. Assemble release directory structure
    5. Bootstrap pip in embedded Python
    6. Create zip archive
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import urllib.request
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]

# Source paths
BUILD_RELEASE = REPO_ROOT / "build" / "GT_esmini" / "Release"
EMBEDDED_PYTHON = REPO_ROOT / "thirdparty" / "python-embed" / "python-3.12.10-embed-amd64"
FRONTEND_DIR = REPO_ROOT / "GT_esmini" / "web" / "frontend"
FRONTEND_DIST = FRONTEND_DIR / "dist"
PYINSTALLER_DIR = Path(__file__).resolve().parent
SPEC_FILE = PYINSTALLER_DIR / "gt_sim_web.spec"

# Files to copy from build/GT_esmini/Release/
BIN_GLOBS = ["GT_Sim.exe", "*.dll", "*.pyd", "python312.zip"]

# Extra files from embedded Python distribution
EMBED_FILES = ["python.exe", "pythonw.exe", "python312._pth"]

# Scripts to copy
SCRIPTS_FILES = ["scenario_generator.py", "dat.py", "comparison_kpis.py"]
SCRIPTS_DIRS = ["osi3"]

# DriverScript files/dirs
DRIVERSCRIPT_FILES = ["runtime_api.py"]
DRIVERSCRIPT_DIRS = ["pythondriver", "examples", "realdriver", "osi3"]

# Config mappings: (source relative to REPO_ROOT, dest relative to package)
CONFIG_FILES = [
    ("GT_esmini/config/real_vehicle_params.json", "config/real_vehicle_params.json"),
    ("GT_esmini/config/host_vehicle_config.json", "config/host_vehicle_config.json"),
    ("GT_esmini/test/comparison_thresholds.yaml", "config/comparison_thresholds.yaml"),
]

IGNORE_PATTERNS = shutil.ignore_patterns(
    "__pycache__", "*.pyc", "*.pyo", "*.proto", "*.temp.xosc", ".git",
)


def log(msg: str) -> None:
    print(f"  {msg}")


def verify_prerequisites() -> None:
    """Check that all required build artifacts exist."""
    errors: list[str] = []

    if not (BUILD_RELEASE / "GT_Sim.exe").exists():
        errors.append(f"GT_Sim.exe not found at {BUILD_RELEASE}. Run CMake build first.")
    if not (BUILD_RELEASE / "GT_esminiLib.dll").exists():
        errors.append(f"GT_esminiLib.dll not found at {BUILD_RELEASE}.")
    if not FRONTEND_DIST.is_dir() or not (FRONTEND_DIST / "index.html").exists():
        errors.append(f"Frontend not built. Run 'npm run build' in {FRONTEND_DIR}.")
    if not EMBEDDED_PYTHON.is_dir():
        errors.append(f"Embedded Python not found at {EMBEDDED_PYTHON}.")

    if errors:
        print("[FAIL] Prerequisites check failed:")
        for e in errors:
            print(f"  - {e}")
        sys.exit(1)

    print("[OK] All prerequisites verified.")


def build_frontend() -> None:
    """Build the frontend with npm."""
    print("[BUILD] Building frontend...")
    result = subprocess.run(
        ["npm", "run", "build"],
        cwd=str(FRONTEND_DIR),
        shell=True,
    )
    if result.returncode != 0:
        print("[FAIL] Frontend build failed.")
        sys.exit(1)
    print("[OK] Frontend built.")


def run_pyinstaller() -> None:
    """Execute PyInstaller with the spec file."""
    print("[BUILD] Running PyInstaller...")
    cmd = [
        sys.executable, "-m", "PyInstaller",
        "--clean",
        "--noconfirm",
        str(SPEC_FILE),
        "--distpath", str(PYINSTALLER_DIR / "dist"),
        "--workpath", str(PYINSTALLER_DIR / "build_temp"),
    ]
    result = subprocess.run(cmd, cwd=str(REPO_ROOT))
    if result.returncode != 0:
        print("[FAIL] PyInstaller build failed.")
        sys.exit(1)
    print("[OK] PyInstaller build succeeded.")


def _copy_glob(src_dir: Path, dst_dir: Path, patterns: list[str]) -> int:
    """Copy files matching glob patterns from src to dst. Returns count."""
    count = 0
    for pattern in patterns:
        for f in src_dir.glob(pattern):
            if f.is_file():
                shutil.copy2(f, dst_dir / f.name)
                count += 1
    return count


def _copy_files(src_dir: Path, dst_dir: Path, filenames: list[str]) -> None:
    """Copy specific files from src to dst."""
    for fname in filenames:
        src = src_dir / fname
        if src.exists():
            shutil.copy2(src, dst_dir / fname)


def _copy_dirs(src_dir: Path, dst_dir: Path, dirnames: list[str]) -> None:
    """Copy directories from src to dst."""
    for dname in dirnames:
        src = src_dir / dname
        if src.is_dir():
            shutil.copytree(src, dst_dir / dname, ignore=IGNORE_PATTERNS)


def bootstrap_pip(bin_dir: Path) -> None:
    """Bootstrap pip into the embedded Python's site-packages."""
    python_exe = bin_dir / "python.exe"
    if not python_exe.exists():
        print("[WARN] python.exe not found in bin/, skipping pip bootstrap.")
        return

    print("[PIP] Bootstrapping pip...")

    # Try ensurepip first
    result = subprocess.run(
        [str(python_exe), "-m", "ensurepip", "--upgrade"],
        capture_output=True,
        text=True,
    )
    if result.returncode == 0:
        print("[OK] pip bootstrapped via ensurepip.")
        return

    # Fallback: download get-pip.py
    print("[PIP] ensurepip unavailable, trying get-pip.py...")
    get_pip_path = bin_dir / "get-pip.py"
    try:
        urllib.request.urlretrieve(
            "https://bootstrap.pypa.io/get-pip.py",
            str(get_pip_path),
        )
    except Exception as e:
        print(f"[WARN] Failed to download get-pip.py: {e}")
        print("[WARN] pip will not be available. Users can manually install later.")
        return

    result = subprocess.run(
        [str(python_exe), str(get_pip_path)],
        capture_output=True,
        text=True,
    )
    if result.returncode == 0:
        print("[OK] pip bootstrapped via get-pip.py.")
    else:
        print(f"[WARN] pip bootstrap failed: {result.stderr}")
        print("[WARN] pip will not be available. Users can manually install later.")

    # Clean up get-pip.py
    get_pip_path.unlink(missing_ok=True)


def assemble_package(version: str, output_dir: Path) -> Path:
    """Assemble the complete release directory."""
    pkg_name = f"GT_Sim_Web_v{version}"
    pkg_dir = output_dir / pkg_name

    if pkg_dir.exists():
        print(f"[CLEAN] Removing existing {pkg_dir}")
        shutil.rmtree(pkg_dir)

    pkg_dir.mkdir(parents=True)
    print(f"[ASSEMBLE] Building package at {pkg_dir}")

    # 1. bin/ — GT_Sim.exe + DLLs + PYDs from build
    bin_dir = pkg_dir / "bin"
    bin_dir.mkdir()
    count = _copy_glob(BUILD_RELEASE, bin_dir, BIN_GLOBS)
    log(f"bin/: {count} files from build output")

    # 1b. bin/ — python.exe, python312._pth from embedded Python
    _copy_files(EMBEDDED_PYTHON, bin_dir, EMBED_FILES)
    log("bin/: added python.exe + python312._pth")

    # 1c. bin/Lib/site-packages/ from embedded Python
    embed_site = EMBEDDED_PYTHON / "Lib" / "site-packages"
    if embed_site.is_dir():
        shutil.copytree(embed_site, bin_dir / "Lib" / "site-packages", ignore=IGNORE_PATTERNS)
        log("bin/Lib/site-packages/: copied from embedded Python")

    # 1d. Bootstrap pip
    bootstrap_pip(bin_dir)

    # 2. server/ — PyInstaller output
    pyinstaller_output = PYINSTALLER_DIR / "dist" / "gt_sim_web"
    if not pyinstaller_output.is_dir():
        print(f"[FAIL] PyInstaller output not found at {pyinstaller_output}")
        sys.exit(1)
    shutil.copytree(pyinstaller_output, pkg_dir / "server")
    log("server/: copied PyInstaller output")

    # 3. scripts/
    scripts_dest = pkg_dir / "scripts"
    scripts_dest.mkdir()
    _copy_files(REPO_ROOT / "scripts", scripts_dest, SCRIPTS_FILES)
    _copy_dirs(REPO_ROOT / "scripts", scripts_dest, SCRIPTS_DIRS)
    log("scripts/: copied")

    # 4. DriverScript/
    ds_dest = pkg_dir / "DriverScript"
    ds_dest.mkdir()
    _copy_files(REPO_ROOT / "DriverScript", ds_dest, DRIVERSCRIPT_FILES)
    _copy_dirs(REPO_ROOT / "DriverScript", ds_dest, DRIVERSCRIPT_DIRS)
    log("DriverScript/: copied")

    # 5. resources/
    res_dest = pkg_dir / "resources"
    for subdir in ["xosc", "xodr", "models"]:
        src = REPO_ROOT / "resources" / subdir
        if src.is_dir():
            shutil.copytree(src, res_dest / subdir, ignore=IGNORE_PATTERNS)
    log("resources/: copied (xosc, xodr, models)")

    # 6. config/
    config_dest = pkg_dir / "config"
    config_dest.mkdir()
    for src_rel, dst_rel in CONFIG_FILES:
        src = REPO_ROOT / src_rel
        dst = pkg_dir / dst_rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        if src.exists():
            shutil.copy2(src, dst)
    log("config/: copied")

    # 7. data/ placeholder
    data_dir = pkg_dir / "data"
    data_dir.mkdir()
    (data_dir / "results").mkdir()
    log("data/: created empty")

    # 8. Launcher batch files
    _write_launcher(pkg_dir)
    log("GT_Sim_Web.bat: created")

    _write_pip_helper(pkg_dir)
    log("pip_install.bat: created")

    # 9. README
    _write_readme(pkg_dir, version)
    log("README.txt: created")

    return pkg_dir


def _write_launcher(pkg_dir: Path) -> None:
    content = r"""@echo off
title GT_Sim Web Server
echo ====================================
echo   GT_Sim Web Server
echo ====================================
echo.
echo Starting server at http://127.0.0.1:8000
echo Press Ctrl+C to stop the server.
echo.
cd /d "%~dp0"
server\gt_sim_web.exe --host 127.0.0.1 --port 8000
pause
"""
    (pkg_dir / "GT_Sim_Web.bat").write_text(content, encoding="utf-8")


def _write_pip_helper(pkg_dir: Path) -> None:
    content = r"""@echo off
REM Install Python packages for PythonDriverController scripts.
REM Usage: pip_install.bat <package_name> [...]
REM Example: pip_install.bat numpy pandas requests
cd /d "%~dp0"
bin\python.exe -m pip install %*
"""
    (pkg_dir / "pip_install.bat").write_text(content, encoding="utf-8")


def _write_readme(pkg_dir: Path, version: str) -> None:
    content = f"""GT_Sim Web v{version}
{'=' * (len(f'GT_Sim Web v{version}'))}

Quick Start
-----------
1. Double-click GT_Sim_Web.bat
2. Browser opens at http://127.0.0.1:8000
3. Press Ctrl+C in the console to stop

Directory Structure
-------------------
bin/           - GT_Sim simulation engine, Embedded Python, DLLs
server/        - Web server (do not modify)
scripts/       - Utility scripts (scenario generation, DAT conversion)
DriverScript/  - Python controller scripts (editable)
resources/     - Scenarios (.xosc), Roads (.xodr), 3D Models
config/        - Configuration files (editable)
data/          - Runtime data, simulation results

Adding Python Packages
----------------------
To install additional Python packages for controller scripts:

    pip_install.bat numpy pandas requests

Installed packages are available to all PythonDriverController scripts.

Customization
-------------
- Edit DriverScript/pythondriver/*.py for custom controllers
- Add .xosc files to resources/xosc/ for new scenarios
- Edit config/real_vehicle_params.json for vehicle parameters
"""
    (pkg_dir / "README.txt").write_text(content, encoding="utf-8")


def create_archive(pkg_dir: Path) -> Path:
    """Create a zip archive of the package."""
    archive_path = pkg_dir.parent / pkg_dir.name
    print(f"[ZIP] Creating {archive_path}.zip ...")
    shutil.make_archive(str(archive_path), "zip", str(pkg_dir.parent), pkg_dir.name)
    zip_path = Path(f"{archive_path}.zip")
    size_mb = zip_path.stat().st_size / (1024 * 1024)
    print(f"[OK] Archive created: {zip_path} ({size_mb:.1f} MB)")
    return zip_path


def main() -> None:
    parser = argparse.ArgumentParser(description="Build GT_Sim Web distributable package")
    parser.add_argument("--version", default="0.1.0", help="Version string (default: 0.1.0)")
    parser.add_argument("--output", default=str(REPO_ROOT / "dist"), help="Output directory")
    parser.add_argument("--skip-pyinstaller", action="store_true", help="Skip PyInstaller step")
    parser.add_argument("--no-zip", action="store_true", help="Skip zip creation")
    parser.add_argument("--build-frontend", action="store_true", help="Run npm build first")
    args = parser.parse_args()

    output_dir = Path(args.output).resolve()
    output_dir.mkdir(parents=True, exist_ok=True)

    print()
    print("=" * 50)
    print("  GT_Sim Web Package Builder")
    print("=" * 50)
    print()

    verify_prerequisites()

    if args.build_frontend:
        build_frontend()

    if not args.skip_pyinstaller:
        run_pyinstaller()

    pkg_dir = assemble_package(args.version, output_dir)

    if not args.no_zip:
        create_archive(pkg_dir)

    print()
    print(f"[DONE] Package ready at: {pkg_dir}")
    print()


if __name__ == "__main__":
    main()
