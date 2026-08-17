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
VERIFICATION_BUILDER = (
    REPO_ROOT
    / "GT_esmini"
    / "scripts"
    / "verification"
    / "build_verification_project.py"
)

# Files to copy from build/GT_esmini/Release/
# GT_RoadGen.exe: parallel OpenDRIVE->.osgb road-mesh generator. GT_esminiLib spawns it (co-located
# in bin/) to pre-generate + cache the road model, so it MUST ship alongside GT_Sim.exe.
# GT_WheelProbe.exe (feature:F8): read-only SDL2 axis probe. The web UI's axis
# mapping panel spawns it from bin/ for the live readout and the "Detect" button,
# and config.py resolves it as PACKAGE_ROOT/bin/GT_WheelProbe.exe. Its absence is
# WARNED about rather than fatal (see check_prerequisites): the panel degrades to
# hand-editing and the API returns an explanatory 503, and the probe only exists
# in a GT_ENABLE_SDL2=ON build.
#
# NOTE FOR WHOEVER ADDS THE NEXT EXECUTABLE: exes are enumerated here one by one
# (only DLLs/PYDs are globbed), and build_package.ps1 has its OWN --target list
# plus its own staging copy. All three have to be updated, and the failure mode of
# forgetting this one is silent -- the package builds fine and the feature is just
# missing at runtime (exactly what happened to GT_WheelProbe on its first
# packaging, 2026-08-17).
BIN_GLOBS = ["GT_Sim.exe", "GT_RoadGen.exe", "GT_WheelProbe.exe", "*.dll", "*.pyd"]

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
    # feature:F7 per-domain split (lateral=ManualDrive / longitudinal=VirtualDriver).
    # Three ManualDrive variants selected per-scenario via the ConfigFile property,
    # so none of them replaces manual_drive.json: _stub has no input device and no
    # sockets (the CI-safe regression asset), _udp takes a steering signature over
    # port 9100 (developer harness only — it would contend for the port), and
    # _realwheel_split drives the lateral half from a physical G29.
    (
        "GT_esmini/config/manual_drive_headless_stub.json",
        "config/manual_drive_headless_stub.json",
    ),
    (
        "GT_esmini/config/manual_drive_headless_udp.json",
        "config/manual_drive_headless_udp.json",
    ),
    (
        "GT_esmini/config/manual_drive_realwheel_split.json",
        "config/manual_drive_realwheel_split.json",
    ),
    # _realwheel_reverse: the reverse split (steering is the AD's, pedals are the
    # human's). Separate from _realwheel_split because the FFB target-track servo
    # is ON here — with the AD steering, driving the physical wheel to the AD's
    # commanded angle is the whole point, whereas in the forward split it would
    # fight the driver for a domain the driver already holds.
    (
        "GT_esmini/config/manual_drive_realwheel_reverse.json",
        "config/manual_drive_realwheel_reverse.json",
    ),
    # _headless_udp_override: the reverse split's headless counterpart. Feeds a
    # synthesized push-back and the AUTO_RESUME/TAKE_MANUAL buttons over UDP so
    # the latch and the return path can be exercised without a physical wheel.
    # Developer harness, same standing as manual_drive_headless_udp.json above.
    (
        "GT_esmini/config/manual_drive_headless_udp_override.json",
        "config/manual_drive_headless_udp_override.json",
    ),
    # virtual_driver_realwheel.json: the FORWARD real-wheel configuration, where
    # VirtualDriver itself opens the G29 and the target-track servo drives the
    # wheel to the AD-commanded angle. This is the config the physical override
    # and the AUTO<->MANUAL toggle button are actually driven from, so shipping
    # virtual_driver.json alone leaves the real-wheel feature unreachable.
    (
        "GT_esmini/config/virtual_driver_realwheel.json",
        "config/virtual_driver_realwheel.json",
    ),
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
    # feature:F8 -- warn, do not fail: the axis-mapping panel works without the
    # probe (hand-edited values, plus an explanatory 503 from /api/wheel-probe),
    # and the probe only exists in a GT_ENABLE_SDL2=ON build. A silent omission is
    # what this check exists to prevent -- the first packaged build shipped
    # without it and nothing anywhere said so.
    if not (BUILD_RELEASE / "GT_WheelProbe.exe").exists():
        print(
            f"[WARN] GT_WheelProbe.exe not found at {BUILD_RELEASE} -- the wheel "
            "axis mapping panel will have no live axis readout and no Detect "
            "button (build with -DGT_ENABLE_SDL2=ON to include it)."
        )
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


def _build_verification_project(pkg_dir: Path) -> None:
    """Bundle VD verification scenarios into their own packaged GUI project.

    Written to data/projects/ (not resources/xosc/) so ``sync_projects()``
    (GT_esmini/web/backend/services/project_service.py) auto-registers it as a
    project separate from the Built-in Samples one on first launch, instead of
    the verification scenarios sitting invisible/mixed inside resources/xosc/.
    """
    out_dir = pkg_dir / "data" / "projects" / "VirtualDriver Verification"
    result = subprocess.run(
        [sys.executable, str(VERIFICATION_BUILDER), "--out", str(out_dir)],
        cwd=str(REPO_ROOT),
    )
    if result.returncode != 0:
        print("[FAIL] VirtualDriver Verification project build failed.")
        sys.exit(1)


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

    # 7c. data/projects/VirtualDriver Verification — packaged as its own GUI
    # project (see _build_verification_project docstring), distinct from the
    # Built-in Samples project backed by resources/.
    _build_verification_project(pkg_dir)
    log("data/projects/: VirtualDriver Verification project bundled")

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
data/          - Runtime data, simulation results, projects
docs/          - Documentation

Projects
--------
The app lists two projects on first launch:
  Built-in Samples          - general esmini/GT_esmini sample scenarios (resources/)
  VirtualDriver Verification - VirtualDriver regression/verification scenarios
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
