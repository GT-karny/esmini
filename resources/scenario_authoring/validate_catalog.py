#!/usr/bin/env python
"""validate_catalog.py — catalog validator (M-A static checks + M-D execution checks).

Checks every generated artifact under:
  road_catalog/generated/*.xodr
  scenario_templates/generated/*.xosc

STATIC checks (M-A, always run):
  (a) XML is well-formed (lxml parse).
  (b) Companion meta yaml exists and carries required fields:
        road:     catalog_id, kind, generator, generated_at_commit
        scenario: catalog_id, kind, generator, generated_at_commit,
                  road_ref, phase, evaluation
  (c) catalog_id in meta yaml == artifact filename stem.
  (d) scenarios with evaluation: annotation must carry a companion
        <catalog_id>.annotation_required.yaml.

EXECUTION checks (M-D, run unless --skip-run):
  roads (*.xodr):
      auto-generate a temp probe xosc (a Default-controller vehicle on a main
      lane, 5 s) in a temp dir, run esmini --headless --fixed_timestep 0.05,
      require EXIT == 0.  (Mirrors the M-B road-probe pattern.)
  scenarios (*.xosc):
      invoke gt_sim_test.py `run <xosc> --out <tmp> --dt 0.05 --max-time 35
      --snapshots 0 --dll <dll>` as a subprocess (venv python), require exit 0
      (== VirtualDriver telemetry frames > 0).

CLI:
  --dll PATH      GT_esminiLib.dll       (default build/GT_esmini/Release/GT_esminiLib.dll)
  --esmini PATH   esmini.exe             (default build/.../esmini/Release/esmini.exe)
  --skip-run      static checks only (no esmini / DLL execution)

Also deletes any *.temp.xosc droppings (esmini sanitizer) before validating.

Writes validate_report.md in resources/scenario_authoring/ (regenerated on every
run; NOT committed — see root .gitignore).

Exits non-zero if any check fails.
"""
from __future__ import annotations

import argparse
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any

import yaml
from lxml import etree

# Make resources/scenario_authoring/ importable as the authoring package root.
_HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(_HERE))

from authoring_common import (  # noqa: E402
    PINNED_XOSC_DATE,
    make_ego_vehicle,
    repo_root,
)
from scenariogeneration import xosc  # noqa: E402

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------

_ROAD_GEN = _HERE / "road_catalog" / "generated"
_SCENE_GEN = _HERE / "scenario_templates" / "generated"
_REPORT = _HERE / "validate_report.md"

_REPO = repo_root()

# Shared failure-cause extractor (canonical copy lives in the web backend; this
# script only ever runs from the repo checkout, so import it via the repo root
# rather than duplicating the logic — audit PY-3).
if str(_REPO) not in sys.path:
    sys.path.insert(0, str(_REPO))
from GT_esmini.web.backend.services.log_extract import extract_failure  # noqa: E402

_DEFAULT_DLL = _REPO / "build" / "GT_esmini" / "Release" / "GT_esminiLib.dll"
_DEFAULT_ESMINI = (
    _REPO / "build" / "EnvironmentSimulator" / "Applications" / "esmini" / "Release" / "esmini.exe"
)
_GT_SIM_TEST = _REPO / "GT_esmini" / "scripts" / "verification" / "gt_sim_test.py"
_VENV_PY = _REPO / "DriverScript" / ".venv" / "Scripts" / "python.exe"

# Required meta fields by artifact kind.
_REQUIRED_ROAD_FIELDS = {"catalog_id", "kind", "generator", "generated_at_commit"}
_REQUIRED_SCENARIO_FIELDS = _REQUIRED_ROAD_FIELDS | {"road_ref", "phase", "evaluation"}

# Per-check timeouts (seconds).
_ROAD_RUN_TIMEOUT = 60
_SCEN_RUN_TIMEOUT = 240


# ---------------------------------------------------------------------------
# Cleanup
# ---------------------------------------------------------------------------

def _delete_temp_xosc(root: Path) -> list[Path]:
    """Delete *.temp.xosc files under *root* (esmini sanitizer droppings)."""
    removed: list[Path] = []
    for p in root.rglob("*.temp.xosc"):
        p.unlink()
        removed.append(p)
    return removed


# ---------------------------------------------------------------------------
# Static per-artifact checks
# ---------------------------------------------------------------------------

def _check_xml_wellformed(path: Path) -> tuple[bool, str]:
    try:
        etree.parse(str(path))
        return True, "well-formed"
    except etree.XMLSyntaxError as exc:
        return False, f"XML error: {exc}"


def _load_meta(meta_path: Path) -> tuple[bool, str, dict[str, Any]]:
    if not meta_path.exists():
        return False, f"missing ({meta_path.name})", {}
    try:
        data = yaml.safe_load(meta_path.read_text(encoding="utf-8")) or {}
        return True, "present", data
    except yaml.YAMLError as exc:
        return False, f"YAML error: {exc}", {}


def _check_meta_fields(meta: dict[str, Any], required: set[str]) -> tuple[bool, str]:
    missing = required - set(meta.keys())
    if missing:
        return False, f"missing fields: {sorted(missing)}"
    return True, "fields OK"


def _check_id_matches_stem(meta: dict[str, Any], stem: str) -> tuple[bool, str]:
    cid = meta.get("catalog_id", "")
    if cid == stem:
        return True, f"id={cid}"
    return False, f"catalog_id '{cid}' != stem '{stem}'"


# ---------------------------------------------------------------------------
# Execution checks (M-D)
# ---------------------------------------------------------------------------

def _write_road_probe(xodr_path: Path, tmp_dir: Path) -> Path:
    """Write a minimal Default-controller probe xosc for *xodr_path*.

    A single car on road 0 (a main approach leg) lane -1 cruising at 10 m/s for
    5 s. The LogicFile uses the ABSOLUTE xodr path so the temp xosc resolves
    regardless of where it lives (mirrors the M-B road-probe pattern).
    """
    entities = xosc.Entities()
    entities.add_scenario_object("Ego", make_ego_vehicle())  # no controller -> Default

    init = xosc.Init()
    step = xosc.TransitionDynamics(xosc.DynamicsShapes.step, xosc.DynamicsDimension.time, 0.0)
    init.add_init_action("Ego", xosc.TeleportAction(xosc.LanePosition(10.0, 0.0, "-1", "0")))
    init.add_init_action("Ego", xosc.AbsoluteSpeedAction(10.0, step))

    stop = xosc.ValueTrigger(
        "stop", 0.0, xosc.ConditionEdge.rising,
        xosc.SimulationTimeCondition(5.0, xosc.Rule.greaterThan), triggeringpoint="stop",
    )
    sb = xosc.StoryBoard(init, stop)
    rn = xosc.RoadNetwork(roadfile=str(xodr_path.resolve()))
    sc = xosc.Scenario(
        f"probe_{xodr_path.stem}", "GT_esmini-validate", xosc.ParameterDeclarations(),
        entities, sb, rn, xosc.Catalog(), osc_minor_version=1, creation_date=PINNED_XOSC_DATE,
    )
    out = tmp_dir / f"probe_{xodr_path.stem}.xosc"
    sc.write_xml(str(out))
    return out


def _run_road(xodr_path: Path, esmini: Path) -> tuple[bool, str]:
    """Generate a probe xosc and run esmini headless; require EXIT == 0."""
    if not esmini.is_file():
        return False, f"esmini not found: {esmini}"
    with tempfile.TemporaryDirectory(prefix="gtcat_road_") as td:
        tmp = Path(td)
        try:
            probe = _write_road_probe(xodr_path, tmp)
        except Exception as exc:  # probe build failure is a real road problem
            return False, f"probe build failed: {exc}"
        # Per-run --logfile_path: with --disable_stdout the cause only ever
        # lands in the file log (audit CORE-5 / PY-3).
        log_txt = tmp / "log.txt"
        cmd = [str(esmini), "--osc", str(probe), "--headless",
               "--fixed_timestep", "0.05", "--disable_stdout",
               "--logfile_path", str(log_txt)]
        try:
            proc = subprocess.run(cmd, capture_output=True, text=True, timeout=_ROAD_RUN_TIMEOUT)
        except subprocess.TimeoutExpired:
            return False, f"esmini timed out (> {_ROAD_RUN_TIMEOUT}s)"
        # Clean any sanitizer dropping the run may have written next to the probe.
        for p in tmp.glob("*.temp.xosc"):
            p.unlink()
        if proc.returncode == 0:
            return True, "esmini headless EXIT=0"
        stdout_txt = tmp / "stdout.txt"
        stdout_txt.write_text(proc.stdout or "", encoding="utf-8")
        cause = extract_failure(log_txt, stdout_txt, exit_code=proc.returncode)
        return False, f"esmini EXIT={proc.returncode}: {cause.summary}"


def _run_scenario(xosc_path: Path, dll: Path) -> tuple[bool, str]:
    """Run a scenario via gt_sim_test (subprocess, venv python); require exit 0."""
    if not dll.is_file():
        return False, f"DLL not found: {dll}"
    if not _VENV_PY.is_file():
        return False, f"venv python not found: {_VENV_PY}"
    with tempfile.TemporaryDirectory(prefix="gtcat_scen_") as td:
        cmd = [
            str(_VENV_PY), str(_GT_SIM_TEST), "run", str(xosc_path),
            "--out", str(Path(td) / "run"), "--dt", "0.05", "--max-time", "35",
            "--snapshots", "0", "--dll", str(dll),
        ]
        try:
            proc = subprocess.run(cmd, capture_output=True, text=True, timeout=_SCEN_RUN_TIMEOUT)
        except subprocess.TimeoutExpired:
            return False, f"gt_sim_test timed out (> {_SCEN_RUN_TIMEOUT}s)"
        if proc.returncode == 0:
            return True, "gt_sim_test run exit=0 (VD telemetry frames > 0)"
        # The harness's own failure (RuntimeError etc.) is the stderr tail; the
        # DLL's cause lands on stdout (audit CORE-1) — mine it with the shared
        # extractor and report both.
        stdout_txt = Path(td) / "stdout.txt"
        stdout_txt.write_text(proc.stdout or "", encoding="utf-8")
        cause = extract_failure(None, stdout_txt, exit_code=proc.returncode)
        stderr_lines = (proc.stderr or "").strip().splitlines()
        parts = [p for p in (stderr_lines[-1] if stderr_lines else "",
                             cause.summary if cause.error_lines else "") if p]
        detail = " | ".join(parts) or cause.summary
        return False, f"gt_sim_test exit={proc.returncode}: {detail}"


# ---------------------------------------------------------------------------
# Validate one artifact
# ---------------------------------------------------------------------------

def validate_road(xodr_path: Path, esmini: Path | None) -> dict[str, Any]:
    results: dict[str, Any] = {"artifact": xodr_path.name, "checks": {}, "pass": True}

    ok, msg = _check_xml_wellformed(xodr_path)
    results["checks"]["xml_wellformed"] = {"ok": ok, "detail": msg}
    if not ok:
        results["pass"] = False

    meta_path = _ROAD_GEN / f"{xodr_path.stem}.road.meta.yaml"
    meta_ok, meta_msg, meta = _load_meta(meta_path)
    results["checks"]["meta_exists"] = {"ok": meta_ok, "detail": meta_msg}
    if not meta_ok:
        results["pass"] = False
    else:
        f_ok, f_msg = _check_meta_fields(meta, _REQUIRED_ROAD_FIELDS)
        results["checks"]["meta_fields"] = {"ok": f_ok, "detail": f_msg}
        id_ok, id_msg = _check_id_matches_stem(meta, xodr_path.stem)
        results["checks"]["id_matches_stem"] = {"ok": id_ok, "detail": id_msg}
        if not (f_ok and id_ok):
            results["pass"] = False

    if esmini is not None:
        run_ok, run_msg = _run_road(xodr_path, esmini)
        results["checks"]["esmini_run"] = {"ok": run_ok, "detail": run_msg}
        if not run_ok:
            results["pass"] = False

    return results


def validate_scenario(xosc_path: Path, dll: Path | None) -> dict[str, Any]:
    results: dict[str, Any] = {"artifact": xosc_path.name, "checks": {}, "pass": True}
    stem = xosc_path.stem

    ok, msg = _check_xml_wellformed(xosc_path)
    results["checks"]["xml_wellformed"] = {"ok": ok, "detail": msg}
    if not ok:
        results["pass"] = False

    meta_path = _SCENE_GEN / f"{stem}.meta.yaml"
    meta_ok, meta_msg, meta = _load_meta(meta_path)
    results["checks"]["meta_exists"] = {"ok": meta_ok, "detail": meta_msg}
    if not meta_ok:
        results["pass"] = False
    else:
        f_ok, f_msg = _check_meta_fields(meta, _REQUIRED_SCENARIO_FIELDS)
        results["checks"]["meta_fields"] = {"ok": f_ok, "detail": f_msg}
        id_ok, id_msg = _check_id_matches_stem(meta, stem)
        results["checks"]["id_matches_stem"] = {"ok": id_ok, "detail": id_msg}
        if not (f_ok and id_ok):
            results["pass"] = False

        # (d) annotation_required.yaml must accompany evaluation: annotation.
        if meta.get("evaluation") == "annotation":
            ann_path = _SCENE_GEN / f"{stem}.annotation_required.yaml"
            ann_ok = ann_path.is_file()
            detail = "present" if ann_ok else f"missing ({ann_path.name})"
            results["checks"]["annotation_required"] = {"ok": ann_ok, "detail": detail}
            if not ann_ok:
                results["pass"] = False

    if dll is not None:
        run_ok, run_msg = _run_scenario(xosc_path, dll)
        results["checks"]["scenario_run"] = {"ok": run_ok, "detail": run_msg}
        if not run_ok:
            results["pass"] = False

    return results


# ---------------------------------------------------------------------------
# Report writer
# ---------------------------------------------------------------------------

_CHECK_COLUMNS = [
    "xml_wellformed", "meta_exists", "meta_fields", "id_matches_stem",
    "annotation_required", "esmini_run", "scenario_run",
]


def write_report(all_results: list[dict[str, Any]], removed_temps: list[Path], skip_run: bool) -> None:
    lines: list[str] = ["# Catalog Validation Report", ""]
    lines.append(f"_Execution checks: {'SKIPPED (--skip-run)' if skip_run else 'ON'}_")
    lines.append("")

    if removed_temps:
        lines.append("## Cleaned up temp artifacts")
        for p in removed_temps:
            lines.append(f"- deleted: `{p.name}`")
        lines.append("")

    if not all_results:
        lines.append("_No generated artifacts found._")
        lines.append("")
    else:
        # Only include columns that at least one artifact exercised.
        used = [c for c in _CHECK_COLUMNS if any(c in r["checks"] for r in all_results)]
        lines.append("## Results")
        lines.append("")
        lines.append("| Artifact | " + " | ".join(used) + " | PASS/FAIL |")
        lines.append("|---|" + "|".join(["---"] * len(used)) + "|---|")
        for r in all_results:
            def cell(key: str) -> str:
                c = r["checks"].get(key)
                if c is None:
                    return "—"
                return f"{'OK' if c['ok'] else 'FAIL'}: {c['detail']}"
            row = " | ".join(cell(c) for c in used)
            status = "**PASS**" if r["pass"] else "**FAIL**"
            lines.append(f"| `{r['artifact']}` | {row} | {status} |")
        lines.append("")

    n_pass = sum(1 for r in all_results if r["pass"])
    n_fail = len(all_results) - n_pass
    overall = all(r["pass"] for r in all_results) if all_results else True
    lines.append(f"**Overall: {'PASS' if overall else 'FAIL'}**  ({n_pass} pass / {n_fail} fail)")
    lines.append("")

    _REPORT.write_text("\n".join(lines), encoding="utf-8")
    print(f"[report] -> {_REPORT}")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Validate the scenario-authoring catalog.")
    p.add_argument("--dll", type=Path, default=_DEFAULT_DLL,
                   help=f"GT_esminiLib.dll for scenario runs (default {_DEFAULT_DLL}).")
    p.add_argument("--esmini", type=Path, default=_DEFAULT_ESMINI,
                   help=f"esmini.exe for road probes (default {_DEFAULT_ESMINI}).")
    p.add_argument("--skip-run", action="store_true",
                   help="Static checks only (no esmini / DLL execution).")
    return p.parse_args()


def main() -> int:
    args = parse_args()
    dll = None if args.skip_run else args.dll
    esmini = None if args.skip_run else args.esmini

    # Clean up esmini sanitizer droppings across the whole scenario_authoring tree.
    removed_temps = _delete_temp_xosc(_HERE)
    for p in removed_temps:
        print(f"[cleanup] deleted {p}")

    all_results: list[dict[str, Any]] = []

    for xodr_path in sorted(_ROAD_GEN.glob("*.xodr")):
        result = validate_road(xodr_path, esmini)
        all_results.append(result)
        print(f"[{'PASS' if result['pass'] else 'FAIL'}] {xodr_path.name}")

    scen_paths = (p for p in _SCENE_GEN.glob("*.xosc") if not p.name.endswith(".temp.xosc"))
    for xosc_path in sorted(scen_paths):
        result = validate_scenario(xosc_path, dll)
        all_results.append(result)
        print(f"[{'PASS' if result['pass'] else 'FAIL'}] {xosc_path.name}")

    # Final sweep: remove any sanitizer droppings produced during the run.
    for p in _delete_temp_xosc(_HERE):
        removed_temps.append(p)

    write_report(all_results, removed_temps, args.skip_run)

    overall_ok = all(r["pass"] for r in all_results) if all_results else True
    if not overall_ok:
        print("[ERROR] Validation failed — see validate_report.md for details.", file=sys.stderr)
        return 1

    print("[OK] All artifacts passed validation.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
