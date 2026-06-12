#!/usr/bin/env python
"""validate_catalog.py — M-A minimal catalog validator.

Checks every generated artifact under:
  road_catalog/generated/*.xodr
  scenario_templates/generated/*.xosc

Per artifact checks:
  (a) XML is well-formed (lxml parse).
  (b) Companion meta yaml exists and carries required fields:
        road:     catalog_id, kind, generator, generated_at_commit
        scenario: catalog_id, kind, generator, generated_at_commit,
                  road_ref, phase, evaluation
  (c) catalog_id in meta yaml == artifact filename stem.

Also deletes any *.temp.xosc droppings (esmini sanitizer) before validating.

Writes validate_report.md in resources/scenario_authoring/ (regenerated on every run;
NOT committed — see root .gitignore).

Exits non-zero if any check fails.

TODO (M-D): add in-process esmini execution check (hook below).
"""
from __future__ import annotations

import sys
from pathlib import Path
from typing import Any

import yaml
from lxml import etree

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------

_HERE = Path(__file__).resolve().parent
_ROAD_GEN = _HERE / "road_catalog" / "generated"
_SCENE_GEN = _HERE / "scenario_templates" / "generated"
_REPORT = _HERE / "validate_report.md"

# Required meta fields by artifact kind.
_REQUIRED_ROAD_FIELDS = {"catalog_id", "kind", "generator", "generated_at_commit"}
_REQUIRED_SCENARIO_FIELDS = _REQUIRED_ROAD_FIELDS | {"road_ref", "phase", "evaluation"}


# ---------------------------------------------------------------------------
# esmini execution hook (M-D)
# ---------------------------------------------------------------------------

def _check_esmini_run(xosc_path: Path) -> tuple[bool, str]:
    """TODO (M-D): run esmini headless and verify EXIT==0.

    Placeholder so that M-D can fill this in without changing the rest of the
    validator logic.  Currently always returns (True, 'skipped (M-D TODO)').
    """
    return True, "skipped (M-D TODO)"


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
# Per-artifact checks
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


def _check_meta_fields(
    meta: dict[str, Any], required: set[str]
) -> tuple[bool, str]:
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
# Validate one artifact
# ---------------------------------------------------------------------------

def validate_artifact(
    art_path: Path,
    meta_path: Path,
    required_meta_fields: set[str],
) -> dict[str, Any]:
    """Run all checks on one artifact.  Returns a result dict."""
    results: dict[str, Any] = {"artifact": art_path.name, "checks": {}, "pass": True}

    # (a) XML well-formed
    ok, msg = _check_xml_wellformed(art_path)
    results["checks"]["xml_wellformed"] = {"ok": ok, "detail": msg}
    if not ok:
        results["pass"] = False

    # (b) meta present + fields
    meta_ok, meta_msg, meta = _load_meta(meta_path)
    results["checks"]["meta_exists"] = {"ok": meta_ok, "detail": meta_msg}
    if not meta_ok:
        results["pass"] = False
    else:
        field_ok, field_msg = _check_meta_fields(meta, required_meta_fields)
        results["checks"]["meta_fields"] = {"ok": field_ok, "detail": field_msg}
        if not field_ok:
            results["pass"] = False

        # (c) catalog_id == stem
        id_ok, id_msg = _check_id_matches_stem(meta, art_path.stem)
        results["checks"]["id_matches_stem"] = {"ok": id_ok, "detail": id_msg}
        if not id_ok:
            results["pass"] = False

    return results


# ---------------------------------------------------------------------------
# Report writer
# ---------------------------------------------------------------------------

def write_report(
    all_results: list[dict[str, Any]],
    removed_temps: list[Path],
) -> None:
    lines: list[str] = [
        "# Catalog Validation Report",
        "",
    ]

    if removed_temps:
        lines.append("## Cleaned up temp artifacts")
        for p in removed_temps:
            lines.append(f"- deleted: `{p.name}`")
        lines.append("")

    if not all_results:
        lines.append("_No generated artifacts found._")
        lines.append("")
    else:
        lines.append("## Results")
        lines.append("")
        lines.append("| Artifact | xml_wellformed | meta_exists | meta_fields | id_matches_stem | PASS/FAIL |")
        lines.append("|---|---|---|---|---|---|")

        for r in all_results:
            def cell(key: str) -> str:
                c = r["checks"].get(key)
                if c is None:
                    return "—"
                return f"{'OK' if c['ok'] else 'FAIL'}: {c['detail']}"

            status = "**PASS**" if r["pass"] else "**FAIL**"
            lines.append(
                f"| `{r['artifact']}` "
                f"| {cell('xml_wellformed')} "
                f"| {cell('meta_exists')} "
                f"| {cell('meta_fields')} "
                f"| {cell('id_matches_stem')} "
                f"| {status} |"
            )
        lines.append("")

    overall = all(r["pass"] for r in all_results) if all_results else True
    lines.append(f"**Overall: {'PASS' if overall else 'FAIL'}**")
    lines.append("")

    _REPORT.write_text("\n".join(lines), encoding="utf-8")
    print(f"[report] -> {_REPORT}")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> int:
    # Clean up esmini sanitizer droppings across the whole scenario_authoring tree.
    removed_temps = _delete_temp_xosc(_HERE)
    for p in removed_temps:
        print(f"[cleanup] deleted {p}")

    all_results: list[dict[str, Any]] = []

    # Validate road xodrs.
    for xodr_path in sorted(_ROAD_GEN.glob("*.xodr")):
        meta_path = _ROAD_GEN / f"{xodr_path.stem}.road.meta.yaml"
        result = validate_artifact(xodr_path, meta_path, _REQUIRED_ROAD_FIELDS)
        all_results.append(result)
        status = "PASS" if result["pass"] else "FAIL"
        print(f"[{status}] {xodr_path.name}")

    # Validate scenario xoscs.
    for xosc_path in sorted(_SCENE_GEN.glob("*.xosc")):
        meta_path = _SCENE_GEN / f"{xosc_path.stem}.meta.yaml"
        result = validate_artifact(xosc_path, meta_path, _REQUIRED_SCENARIO_FIELDS)
        all_results.append(result)
        status = "PASS" if result["pass"] else "FAIL"
        print(f"[{status}] {xosc_path.name}")

    write_report(all_results, removed_temps)

    overall_ok = all(r["pass"] for r in all_results) if all_results else True
    if not overall_ok:
        print("[ERROR] Validation failed — see validate_report.md for details.", file=sys.stderr)
        return 1

    print("[OK] All artifacts passed validation.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
