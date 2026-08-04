#!/usr/bin/env python
"""Bundle the VirtualDriver verification scenarios into a self-contained GUI
project (loadable in the Web / Electron app).

The web GUI lists scenarios from ``<project>/xosc/*.xosc`` and resolves a
scenario's ``<LogicFile>`` (xodr) / ``model3d`` paths relative to the xosc file.
The verification scenarios under ``resources/xosc/verification`` reference shared
assets with ``../../../xodr/...`` / ``../../../models/...`` paths that only work
in place, so this builder:

  1. auto-discovers every category subfolder under ``resources/xosc/verification``
     and copies each scenario into ``<project>/xosc/<category>_<stem>.xosc``,
  2. copies every referenced xodr / model into ``<project>/{xodr,models}/`` and
     rewrites the paths to be project-relative,
  3. tags scenarios per CATEGORY_POLICY with a ``policies`` Property so a GUI
     VirtualDriver run enables the matching Phase-3 policy (the web runner
     reads it — opt-in; categories without an entry get no auto-enabled policy),
  4. copies each scenario's sibling ``<stem>.md`` into ``<project>/docs/`` under
     the *renamed* stem, which is where the GUI's scenario-detail panel reads the
     description from (``GET /api/projects/{id}/scenarios/{file}/docs`` resolves
     ``<project>/docs/<xosc stem>.md``, so the doc name must track the renamed
     scenario, not the source one),
  5. writes a README and (optionally) a .zip for the GUI "Upload ZIP" flow.

By default it writes the project straight into the dev projects dir
(``test_results/web/projects``) where ``sync_projects`` auto-registers it, so it
shows up in the GUI immediately. Run via DriverScript/.venv.

``build_package.py`` also calls this at packaging time, writing straight into
the distributed package's ``data/projects/`` so the packaged app ships a
"VirtualDriver Verification" project separate from the Built-in Samples one.

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

# Category directory name -> the traffic policy a GUI run should enable for it
# (keys must match _VD_POLICY_FLAG in services/simulation_runner.py). Categories
# not listed here (or added later without an entry) default to None — the
# scenario still ships and runs, it just doesn't get an opt-in policy toggled
# on automatically. Keyed by the FULL directory name, not a numeric prefix:
# 06_lead_vehicle and 06_route_lane share the same leading digit, so truncating
# to "06" would make them collide.
CATEGORY_POLICY: dict[str, str | None] = {
    "03_traffic_signals": "traffic_light",
    "04_traffic_signs": "stop_yield",
    "05_anticipation": None,
    "06_lead_vehicle": "lead",
    "06_route_lane": None,
    "07_aeb": "aeb",
    "07_oncoming_yield": "conflict",
    "08_handoff": None,
    "08_overtake": "lead",
    "08_unsignalized_junction": "junction_priority",
    "09_crosswalk_pedestrian": "crosswalk",
    "aeb_c2c_grid": "aeb",
    "p6_virtual_junction": "junction_priority",
    "01_vehicle_model": None,
    "02_basic_control": None,
}


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
    (out_dir / "docs").mkdir(parents=True)

    missing: list[str] = []
    listed: list[tuple[str, str | None]] = []
    unmapped: list[str] = []
    undocumented: list[str] = []

    categories = sorted(p.name for p in SRC.iterdir() if p.is_dir())
    for cat in categories:
        cat_dir = SRC / cat
        if cat not in CATEGORY_POLICY:
            unmapped.append(cat)
        policy = CATEGORY_POLICY.get(cat)
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

            # Prefixed with the full category name (not a truncated leading
            # digit) so scenarios from different categories can never collide
            # in the project's flat xosc/ dir.
            out_name = f"{cat}_{xosc.stem}.xosc"
            tree.write(
                out_dir / "xosc" / out_name, encoding="utf-8", xml_declaration=True
            )
            listed.append((out_name, policy))

            # The description panel keys off the *project* scenario's stem, so
            # the sibling <stem>.md has to follow the same rename.
            doc_src = xosc.with_suffix(".md")
            if doc_src.is_file():
                shutil.copy2(doc_src, out_dir / "docs" / f"{cat}_{xosc.stem}.md")
            else:
                undocumented.append(f"{cat}/{xosc.name}")

    if unmapped:
        print(
            "[build] NOTE: no CATEGORY_POLICY entry for: "
            f"{', '.join(sorted(set(unmapped)))} (defaulted to no auto-enabled policy)",
            file=sys.stderr,
        )

    _write_readme(out_dir, listed)

    if missing:
        print("[build] WARNING: missing referenced assets:", file=sys.stderr)
        for m in sorted(set(missing)):
            print(f"  - {m}", file=sys.stderr)

    if undocumented:
        print(
            f"[build] NOTE: {len(undocumented)} scenario(s) without a sibling .md "
            "(the GUI detail panel shows its placeholder for these):",
            file=sys.stderr,
        )
        for u in sorted(undocumented):
            print(f"  - {u}", file=sys.stderr)

    n_xodr = len(list((out_dir / "xodr").glob("*")))
    n_mdl = len(list((out_dir / "models").glob("*")))
    n_doc = len(list((out_dir / "docs").glob("*.md")))
    print(
        f"[build] {len(listed)} scenarios, {n_xodr} xodr, {n_mdl} models, "
        f"{n_doc} docs -> {out_dir}"
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
    lines += [
        "",
        "xosc/ — scenarios · xodr/ — roads · models/ — 3D models · "
        "docs/ — per-scenario descriptions shown in the GUI detail panel",
    ]
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
