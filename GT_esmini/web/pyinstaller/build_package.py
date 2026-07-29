"""Build GT_Sim Web distributable package.

Usage:
    GT_esmini\\web\\.venv\\Scripts\\python.exe GT_esmini/web/pyinstaller/build_package.py \\
        --version 0.1.0 --output dist/

Steps:
    1. Verify prerequisites (build artifacts, frontend dist)
    2. (Optional) Build frontend
    3. Run PyInstaller to create frozen web server
    4. Assemble release directory structure
    5. Create zip archive
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]

# Source paths
BUILD_RELEASE = REPO_ROOT / "build" / "GT_esmini" / "Release"
EMBEDDED_PYTHON = (
    REPO_ROOT / "thirdparty" / "python-embed" / "python-3.12.10-embed-amd64"
)
FRONTEND_DIR = REPO_ROOT / "GT_esmini" / "web" / "frontend"
FRONTEND_DIST = FRONTEND_DIR / "dist"
PYINSTALLER_DIR = Path(__file__).resolve().parent
SPEC_FILE = PYINSTALLER_DIR / "gt_sim_web.spec"

# Files to copy from build/GT_esmini/Release/
# GT_RoadGen.exe: parallel OpenDRIVE->.osgb road-mesh generator. GT_esminiLib spawns it (co-located
# in bin/) to pre-generate + cache the road model, so it MUST ship alongside GT_Sim.exe.
BIN_GLOBS = ["GT_Sim.exe", "GT_RoadGen.exe", "*.dll", "*.pyd"]

# Extra files from the embedded Python distribution (thirdparty/python-embed/), not
# build/GT_esmini/Release/: CMake links GT_esminiLib against Python3::Python when
# GT_ENABLE_EMBEDDED_PYTHON=ON (distribution packages configure with it ON -- see
# GT_esmini/CMakeLists.txt) but does not stage python312.dll anywhere, and BIN_GLOBS
# above only globs build/GT_esmini/Release/, which never contains it either.
# python312.dll is a hard (non-delay-load) import of GT_esminiLib.dll -- confirmed via
# `dumpbin /dependents` -- so its absence is not "PythonDriverController scenarios fail",
# it is STATUS_DLL_NOT_FOUND (0xC0000135) at process creation for EVERY bin/GT_Sim.exe
# invocation, headless or not. python312.zip is required alongside it: python312._pth
# (below) references it plus `.`/Lib/Lib/site-packages as the interpreter's sys.path.
EMBED_FILES = ["python312._pth", "python312.dll", "python312.zip"]

# Config mappings: (source relative to REPO_ROOT, dest relative to package)
# Keep this list in sync with GT_esmini/config/*.json on disk.  The
# check_config_coverage() guard (called from verify_prerequisites) enforces
# that every tracked *.json is present here — add new configs here first,
# then to disk, to keep the guard green.
CONFIG_FILES = [
    ("GT_esmini/config/real_vehicle_params.json", "config/real_vehicle_params.json"),
    ("GT_esmini/config/host_vehicle_config.json", "config/host_vehicle_config.json"),
    ("GT_esmini/config/manual_drive.json", "config/manual_drive.json"),
    ("GT_esmini/config/kinematic_controller.json", "config/kinematic_controller.json"),
    # virtual_driver.json: loaded by simulation_runner.py; missing → silent {} fallback
    ("GT_esmini/config/virtual_driver.json", "config/virtual_driver.json"),
    # route_drive_controller.json: resolved beside the exe by GT_esminiLib.cpp
    (
        "GT_esmini/config/route_drive_controller.json",
        "config/route_drive_controller.json",
    ),
    # auto_light.json: F6 environment-driven headlights, resolved beside the exe (v0.13)
    ("GT_esmini/config/auto_light.json", "config/auto_light.json"),
    ("GT_esmini/test/comparison_thresholds.yaml", "config/comparison_thresholds.yaml"),
]

IGNORE_PATTERNS = shutil.ignore_patterns(
    "__pycache__",
    "*.pyc",
    "*.pyo",
    "*.proto",
    "*.temp.xosc",
    ".git",
)


def log(msg: str) -> None:
    print(f"  {msg}")


def verify_prerequisites() -> None:
    """Check that all required build artifacts exist."""
    errors: list[str] = []

    if not (BUILD_RELEASE / "GT_Sim.exe").exists():
        errors.append(
            f"GT_Sim.exe not found at {BUILD_RELEASE}. Run CMake build first."
        )
    if not (BUILD_RELEASE / "GT_esminiLib.dll").exists():
        errors.append(f"GT_esminiLib.dll not found at {BUILD_RELEASE}.")
    if not (BUILD_RELEASE / "GT_RoadGen.exe").exists():
        errors.append(
            f"GT_RoadGen.exe not found at {BUILD_RELEASE} (parallel road-mesh generator). Run CMake build first."
        )
    if not FRONTEND_DIST.is_dir() or not (FRONTEND_DIST / "index.html").exists():
        errors.append(f"Frontend not built. Run 'npm run build' in {FRONTEND_DIR}.")
    if not EMBEDDED_PYTHON.is_dir():
        errors.append(f"Embedded Python not found at {EMBEDDED_PYTHON}.")
    else:
        # python312.dll/.zip are a hard GT_esminiLib.dll dependency when built with
        # GT_ENABLE_EMBEDDED_PYTHON=ON (distribution default) -- their absence must
        # fail the package build loudly, not ship a bin/GT_Sim.exe that cannot start
        # (_copy_files() below silently skips a missing source file otherwise).
        for fname in ("python312.dll", "python312.zip"):
            if not (EMBEDDED_PYTHON / fname).exists():
                errors.append(f"{fname} not found at {EMBEDDED_PYTHON}.")

    if errors:
        print("[FAIL] Prerequisites check failed:")
        for e in errors:
            print(f"  - {e}")
        sys.exit(1)

    check_config_coverage()

    print("[OK] All prerequisites verified.")


def check_config_coverage() -> None:
    """Abort if any *.json in GT_esmini/config/ is not listed in CONFIG_FILES.

    This is a build-time guard that prevents silently shipping a package with
    missing runtime configuration (e.g. the virtual_driver.json / gains loss
    that triggered MSC-3).
    """
    config_src_dir = REPO_ROOT / "GT_esmini" / "config"
    covered = {
        (REPO_ROOT / src_rel).resolve()
        for src_rel, _ in CONFIG_FILES
        if src_rel.endswith(".json")
    }
    missing: list[str] = []
    for json_file in sorted(config_src_dir.glob("*.json")):
        if json_file.resolve() not in covered:
            missing.append(json_file.name)
    if missing:
        print("[FAIL] CONFIG_FILES coverage check failed.")
        print("  The following config files exist on disk but are not in CONFIG_FILES:")
        for name in missing:
            print(f"    - GT_esmini/config/{name}")
        print("  Add them to CONFIG_FILES in build_package.py before packaging.")
        sys.exit(1)


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
        sys.executable,
        "-m",
        "PyInstaller",
        "--clean",
        "--noconfirm",
        str(SPEC_FILE),
        "--distpath",
        str(PYINSTALLER_DIR / "dist"),
        "--workpath",
        str(PYINSTALLER_DIR / "build_temp"),
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


def _copy_licenses(pkg_dir: Path) -> None:
    """Copy all required license texts into the package.

    Critic-1 fix: the distributable ZIP bundles MPL-2.0 (esmini, OSI),
    EPL-2.0 (SUMO), BSD (protobuf), OSG, embedded Python, SDL2 and others.
    License texts must be included to comply with each library's terms.

    Layout inside the package:
      LICENSE                        — repo root LICENSE (esmini / MPL-2.0)
      3rd_party_terms_and_licenses/  — all tracked third-party license files
      licenses/PYTHON-LICENSE.txt    — embedded Python license
    """
    # Repo root LICENSE -> package root LICENSE
    repo_license = REPO_ROOT / "LICENSE"
    if repo_license.exists():
        shutil.copy2(repo_license, pkg_dir / "LICENSE")

    # 3rd_party_terms_and_licenses/ (entire tracked dir) -> same name at package root
    third_party_src = REPO_ROOT / "3rd_party_terms_and_licenses"
    if third_party_src.is_dir():
        shutil.copytree(
            third_party_src,
            pkg_dir / "3rd_party_terms_and_licenses",
            ignore=IGNORE_PATTERNS,
        )

    # Embedded Python license -> licenses/PYTHON-LICENSE.txt
    python_license = EMBEDDED_PYTHON / "LICENSE.txt"
    if python_license.exists():
        licenses_dir = pkg_dir / "licenses"
        licenses_dir.mkdir(exist_ok=True)
        shutil.copy2(python_license, licenses_dir / "PYTHON-LICENSE.txt")


def assemble_package(version: str, output_dir: Path) -> Path:
    """Assemble the complete release directory."""
    pkg_name = f"GT_Sim_v{version}"
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

    # 1b. bin/ — python312.dll/.zip/._pth from the embedded Python distribution
    # (CMake does not stage these; see EMBED_FILES's comment for why they're
    # mandatory, not just for the frozen PythonDriverController feature)
    _copy_files(EMBEDDED_PYTHON, bin_dir, EMBED_FILES)
    log(f"bin/: added {', '.join(EMBED_FILES)} from embedded Python")

    # 2. server/ — PyInstaller output
    pyinstaller_output = PYINSTALLER_DIR / "dist" / "gt_sim_web"
    if not pyinstaller_output.is_dir():
        print(f"[FAIL] PyInstaller output not found at {pyinstaller_output}")
        sys.exit(1)
    shutil.copytree(pyinstaller_output, pkg_dir / "server")
    log("server/: copied PyInstaller output")

    # 3. resources/
    res_dest = pkg_dir / "resources"
    for subdir in ["xosc", "xodr", "models", "sumo_inputs"]:
        src = REPO_ROOT / "resources" / subdir
        if src.is_dir():
            shutil.copytree(src, res_dest / subdir, ignore=IGNORE_PATTERNS)
    log("resources/: copied (xosc, xodr, models, sumo_inputs)")

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

    # 7a. verification tools — annotation_match.py backs /api/verification/match.
    # It is loaded at runtime by file path (annotation_store._load_annotation_match),
    # which looks under PACKAGE_ROOT/GT_esmini/scripts/verification/, so ship it there.
    vdir = pkg_dir / "GT_esmini" / "scripts" / "verification"
    vdir.mkdir(parents=True, exist_ok=True)
    am_src = (
        REPO_ROOT / "GT_esmini" / "scripts" / "verification" / "annotation_match.py"
    )
    if am_src.exists():
        shutil.copy2(am_src, vdir / "annotation_match.py")
        log("GT_esmini/scripts/verification/: annotation_match.py (match API)")

    # 7b. docs/
    docs_src = REPO_ROOT / "GT_esmini" / "docs"
    if docs_src.is_dir():
        shutil.copytree(docs_src, pkg_dir / "docs", ignore=IGNORE_PATTERNS)
        log("docs/: copied")

    # 8. Licenses (MPL-2.0 / EPL-2.0 / BSD / OSG / Python / SDL2 …)
    # Critic-1: the ZIP must ship all bundled-library license texts.
    _copy_licenses(pkg_dir)
    log("licenses/: LICENSE + 3rd_party_terms_and_licenses/ + PYTHON-LICENSE.txt")

    # 9. README
    _write_readme(pkg_dir, version)
    log("README.txt: created")

    return pkg_dir


def _write_readme(pkg_dir: Path, version: str) -> None:
    content = f"""GT_Sim v{version}
{'=' * (len(f'GT_Sim v{version}'))}

Quick Start
-----------
1. Launch GT_Sim.exe
2. Browser opens at http://127.0.0.1:8000

Directory Structure
-------------------
bin/           - Simulation engine (GT_Sim.exe) and DLLs
server/        - Web server (do not modify)
resources/     - Scenarios (.xosc), Roads (.xodr), 3D Models
config/        - Configuration files (editable)
data/          - Runtime data, simulation results
docs/          - Documentation
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
    parser = argparse.ArgumentParser(
        description="Build GT_Sim Web distributable package"
    )
    parser.add_argument(
        "--version", default="0.1.0", help="Version string (default: 0.1.0)"
    )
    parser.add_argument(
        "--output", default=str(REPO_ROOT / "dist"), help="Output directory"
    )
    parser.add_argument(
        "--skip-pyinstaller", action="store_true", help="Skip PyInstaller step"
    )
    parser.add_argument(
        "--skip-frontend", action="store_true", help="Skip frontend build"
    )
    parser.add_argument("--no-zip", action="store_true", help="Skip zip creation")
    args = parser.parse_args()

    output_dir = Path(args.output).resolve()
    output_dir.mkdir(parents=True, exist_ok=True)

    print()
    print("=" * 50)
    print("  GT_Sim Web Package Builder")
    print("=" * 50)
    print()

    verify_prerequisites()

    if not args.skip_frontend:
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
