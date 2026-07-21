#!/usr/bin/env python
"""Bundle the VirtualDriver verification scenarios into a self-contained GUI
project (loadable in the Web / Electron app).

The web GUI lists scenarios from ``<project>/xosc/*.xosc`` and resolves a
scenario's ``<LogicFile>`` (xodr) / ``model3d`` paths relative to the xosc file.
The verification scenarios under ``resources/xosc/verification`` reference shared
assets with ``../../../xodr/...`` / ``../../../models/...`` paths that only work
in place, so this builder:

  1. copies each verification xosc into ``<project>/xosc/<NN>_<stem>.xosc``,
  2. copies every referenced xodr / model into ``<project>/{xodr,models}/`` and
     rewrites the paths to be project-relative,
  3. tags 03/04/06 scenarios with a ``policies`` Property so a GUI VirtualDriver
     run enables the matching Phase-3 policy (the web runner reads it — opt-in;
     05 anticipation needs no policy),
  4. writes a README and (optionally) a .zip for the GUI "Upload ZIP" flow.

By default it writes the project straight into the dev projects dir
(``test_results/web/projects``) where ``sync_projects`` auto-registers it, so it
shows up in the GUI immediately. Run via DriverScript/.venv.

    py GT_esmini/scripts/verification/build_verification_project.py
"""

from __future__ import annotations

import argparse
import shutil
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
SRC = REPO_ROOT / "resources" / "xosc" / "verification"
DEFAULT_PROJECTS_DIR = REPO_ROOT / "test_results" / "web" / "projects"

# Category folder prefix -> the traffic policy a GUI run should enable for it.
# 05 anticipation relies on the mid/long planner only (no traffic policy).
CATEGORY_POLICY = {
    "03": "traffic_light",
    "04": "stop_yield",
    "05": None,
    "06": "lead",
}
CATEGORIES = (
    "03_traffic_signals",
    "04_traffic_signs",
    "05_anticipation",
    "06_lead_vehicle",
)


def _collect_and_rewrite(
    root: ET.Element, xosc_dir: Path
) -> dict[str, tuple[str, str]]:
    """Rewrite asset refs to project-relative and return {abs_src: (subdir, name)}."""
    copies: dict[str, tuple[str, str]] = {}

    def remap(abs_or_rel: str, subdir: str) -> str:
        p = Path(abs_or_rel)
        src = p if p.is_absolute() else (xosc_dir / p)
        src = src.resolve()
        name = src.name
        copies[str(src)] = (subdir, name)
        # Paths are resolved by esmini relative to the xosc file's own directory
        # (<project>/xosc/), so reference the sibling asset dirs via "../".
        return f"../{subdir}/{name}"

    for el in root.iter("LogicFile"):
        if el.get("filepath"):
            el.set("filepath", remap(el.get("filepath"), "xodr"))
    for el in root.iter("SceneGraphFile"):
        if el.get("filepath"):
            el.set("filepath", remap(el.get("filepath"), "models"))
    for veh in root.iter("Vehicle"):
        if veh.get("model3d"):
            veh.set("model3d", remap(veh.get("model3d"), "models"))
    return copies


def _strip_init_activation(root: ET.Element) -> None:
    """Remove any Init ActivateControllerAction from the project copy.

    The web runner (services.simulation_runner) injects its OWN
    ActivateControllerAction when it assigns the VirtualDriverController. The
    verification scenarios already carry one (nested in <ControllerAction>), and
    keeping both double-activates the Longitudinal domain — which esmini
    deactivates under OSC < v1.3, killing the controller (no policy, no stop). We
    keep the <ObjectController> itself so the runner can read the `policies` hint
    before stripping it; only the Init activation is removed (the runner re-adds
    a single one)."""
    for private in root.findall(".//Init/Actions/Private"):
        for pa in list(private.findall("PrivateAction")):
            if pa.find(".//ActivateControllerAction") is not None:
                private.remove(pa)


def _tag_policies(root: ET.Element, policy: str | None) -> None:
    if not policy:
        return
    for so in root.iter("ScenarioObject"):
        oc = so.find("ObjectController")
        if oc is None:
            continue
        ctrl = oc.find("Controller")
        if ctrl is None or ctrl.get("name") != "VirtualDriverController":
            continue
        props = ctrl.find("Properties")
        if props is None:
            props = ET.SubElement(ctrl, "Properties")
        if not any(p.get("name") == "policies" for p in props.findall("Property")):
            pe = ET.SubElement(props, "Property")
            pe.set("name", "policies")
            pe.set("value", policy)
        return


def build(out_dir: Path, make_zip: bool) -> Path:
    if out_dir.exists():
        shutil.rmtree(out_dir)
    (out_dir / "xosc").mkdir(parents=True)
    (out_dir / "xodr").mkdir(parents=True)
    (out_dir / "models").mkdir(parents=True)

    missing: list[str] = []
    listed: list[tuple[str, str | None]] = []

    for cat in CATEGORIES:
        cat_dir = SRC / cat
        if not cat_dir.is_dir():
            continue
        num = cat.split("_", 1)[0]
        policy = CATEGORY_POLICY.get(num)
        for xosc in sorted(cat_dir.glob("*.xosc")):
            tree = ET.parse(xosc)
            root = tree.getroot()
            copies = _collect_and_rewrite(root, xosc.parent)
            _tag_policies(root, policy)
            _strip_init_activation(root)  # the web runner injects its own activation

            for src_str, (subdir, name) in copies.items():
                src = Path(src_str)
                dst = out_dir / subdir / name
                if dst.exists():
                    continue
                if src.is_file():
                    shutil.copy2(src, dst)
                else:
                    missing.append(src_str)

            out_name = f"{num}_{xosc.stem}.xosc"
            tree.write(
                out_dir / "xosc" / out_name, encoding="utf-8", xml_declaration=True
            )
            listed.append((out_name, policy))

    _write_readme(out_dir, listed)

    if missing:
        print("[build] WARNING: missing referenced assets:", file=sys.stderr)
        for m in sorted(set(missing)):
            print(f"  - {m}", file=sys.stderr)

    n_xodr = len(list((out_dir / "xodr").glob("*")))
    n_mdl = len(list((out_dir / "models").glob("*")))
    print(
        f"[build] {len(listed)} scenarios, {n_xodr} xodr, {n_mdl} models -> {out_dir}"
    )

    if make_zip:
        zip_base = out_dir.parent / out_dir.name.replace(" ", "_")
        archive = shutil.make_archive(
            str(zip_base), "zip", root_dir=out_dir.parent, base_dir=out_dir.name
        )
        print(f"[build] zip -> {archive}")
    return out_dir


def _write_readme(out_dir: Path, listed: list[tuple[str, str | None]]) -> None:
    lines = [
        "# VirtualDriver Verification",
        "",
        "Self-contained GUI project bundling the VirtualDriver verification scenarios",
        "(generated by `GT_esmini/scripts/verification/build_verification_project.py`).",
        "",
        "Categories: 03 traffic signals (3b), 04 traffic signs (3c), 05 anticipation (Phase 2),",
        "06 lead vehicle (3a). Each scenario embeds the VirtualDriverController; 03/04/06 carry a",
        "`policies` Property so a GUI VirtualDriver run enables the matching Phase-3 policy",
        "(opt-in — other projects are unaffected). Run with OSI enabled to see the scene,",
        "stop markers, and the policy timeline panel.",
        "",
        "| scenario | policy enabled |",
        "| :-- | :-- |",
    ]
    for name, policy in listed:
        lines.append(f"| {name} | {policy or '— (mid/long only)'} |")
    lines += ["", "xosc/ — scenarios · xodr/ — roads · models/ — 3D models"]
    (out_dir / "README.md").write_text("\n".join(lines) + "\n", encoding="utf-8")


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    p.add_argument(
        "--out",
        type=Path,
        default=DEFAULT_PROJECTS_DIR / "VirtualDriver Verification",
        help="project output dir (default: dev projects dir, auto-registered by the GUI)",
    )
    p.add_argument(
        "--zip",
        action="store_true",
        help="also write a .zip for the GUI Upload-ZIP flow",
    )
    args = p.parse_args(argv)
    build(args.out.resolve(), args.zip)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
