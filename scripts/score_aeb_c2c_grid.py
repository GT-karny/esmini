#!/usr/bin/env python
"""score_aeb_c2c_grid - AEB car-to-car (C2C) grid scoring / NCAP-style matrix.

Consumes the output of `gt_sim_test.py batch` (a directory of per-scenario
`<stem>/telemetry.jsonl` + `<stem>/meta.json` + `batch_verdict.json`) for an
AEB car-to-car scenario grid and produces:

  - a Markdown report: an NCAP colour-band-style matrix (CCRs/CCRm rows x ego
    speed columns, plus a CCRb 2x2) with a raw-metrics table underneath, and
  - a machine-readable YAML with every cell's raw metrics + band.

Cell-name contract (固定契約, agreed with the grid-generation side):
  ccrs_ego{V}            stationary lead, nominal closing = V km/h
  ccrm_ego{V}_lead20      lead at 20 km/h, nominal closing = (V-20) km/h
  ccrb_hw{H}_d{D}         ego=lead=50 km/h braking test; nominal closing is
                          pinned to the 50 km/h worst-case reference (see
                          parse_cell_name), independent of H/D.

Band rules (fixed, documented again in the rendered report header):
  avoid    = no contact ever (OBB separation never <= 0). Sub-labelled
             avoid(aeb) / avoid(no_aeb) depending on whether AEB actually
             fired (gt.aeb.triggered / a policy "aeb" constraint) during the
             run, or the approach was simply comfortable enough on its own.
  mitigate = contact occurred, but EITHER the closing speed was cut by
             >= 5.56 m/s (20 km/h) from the nominal, OR the impact speed is
             <= 50% of the nominal closing speed.
  fail     = contact occurred and neither mitigate condition holds.
  crash    = the cell could not be measured at all (telemetry.jsonl missing /
             0 frames / batch_verdict recorded an 'error' status for it).
             Kept strictly separate from behavioral 'fail' -- mixing the two
             pollutes the matrix (project-standing lesson).

Reuses GT_esmini/web/backend/services/vd_metrics.py (imported flat, following
the sys.path convention gt_sim_test.py itself uses) for the actual geometry:
obb_separation() (SAT-based OBB anti-collision test, ~line 61) and
_closing_speed() (impact-speed projection, ~line 414) -- the same primitives
vd_metrics' own min_obb_separation_above / impact_speed_below matchers use.
This script does not reimplement that math; it drives those two functions
frame-by-frame to get the raw numbers (min separation, first-contact impact
speed) instead of a pass/fail verdict.

Usage:
  DriverScript/.venv/Scripts/python.exe scripts/score_aeb_c2c_grid.py \
      --batch-out test_results/aeb_c2c/<run_id> \
      --out-md    test_results/aeb_c2c/<run_id>/matrix.md \
      --out-yaml  test_results/aeb_c2c/<run_id>/matrix.yaml
"""

from __future__ import annotations

import argparse
import json
import math
import re
import sys
from dataclasses import dataclass
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO_ROOT / "GT_esmini" / "web" / "backend" / "services"))
import vd_metrics as _vd  # noqa: E402  (single source of truth for OBB / closing-speed math)

# ---------------------------------------------------------------------------
# constants (band thresholds + the contractual ccrs/ccrm speed range)
# ---------------------------------------------------------------------------

KMH_TO_MPS = 1.0 / 3.6
MITIGATE_MIN_REDUCTION_MPS = 20.0 * KMH_TO_MPS  # 5.555... m/s
MITIGATE_MAX_IMPACT_FRACTION = 0.5
CCRB_NOMINAL_KMH = 50.0
CONTACT_SEP_M = 0.0  # OBB separation <= this counts as "contact" (spec §c)

CCRS_EGO_SPEEDS_KMH = tuple(range(10, 71, 10))  # V in 10..70 (固定契約)
CCRM_EGO_SPEEDS_KMH = tuple(range(30, 71, 10))  # V in 30..70 (固定契約)
CCRB_CELLS = ("ccrb_hw12_d2", "ccrb_hw12_d6", "ccrb_hw40_d2", "ccrb_hw40_d6")


# ---------------------------------------------------------------------------
# cell-name parsing
# ---------------------------------------------------------------------------


@dataclass
class CellSpec:
    name: str
    family: str  # "ccrs" | "ccrm" | "ccrb"
    ego_kmh: float | None
    lead_kmh: float | None
    hw_m: float | None
    decel_mps2: float | None
    nominal_closing_mps: float


_RE_CCRS = re.compile(r"^ccrs_ego(\d+(?:\.\d+)?)$")
_RE_CCRM = re.compile(r"^ccrm_ego(\d+(?:\.\d+)?)_lead(\d+(?:\.\d+)?)$")
_RE_CCRB = re.compile(r"^ccrb_hw(\d+(?:\.\d+)?)_d(\d+(?:\.\d+)?)$")


def parse_cell_name(name: str) -> CellSpec:
    """Parse a grid cell name into its family + nominal closing speed, per the
    固定契約 in the module docstring. Raises ValueError for anything that
    doesn't match one of the three known patterns."""
    m = _RE_CCRS.match(name)
    if m:
        ego = float(m.group(1))
        return CellSpec(
            name=name,
            family="ccrs",
            ego_kmh=ego,
            lead_kmh=None,
            hw_m=None,
            decel_mps2=None,
            nominal_closing_mps=ego * KMH_TO_MPS,
        )

    m = _RE_CCRM.match(name)
    if m:
        ego = float(m.group(1))
        lead = float(m.group(2))
        return CellSpec(
            name=name,
            family="ccrm",
            ego_kmh=ego,
            lead_kmh=lead,
            hw_m=None,
            decel_mps2=None,
            nominal_closing_mps=max(0.0, ego - lead) * KMH_TO_MPS,
        )

    m = _RE_CCRB.match(name)
    if m:
        hw = float(m.group(1))
        d = float(m.group(2))
        return CellSpec(
            name=name,
            family="ccrb",
            ego_kmh=None,
            lead_kmh=None,
            hw_m=hw,
            decel_mps2=d,
            nominal_closing_mps=CCRB_NOMINAL_KMH * KMH_TO_MPS,
        )

    raise ValueError(
        f"unrecognized AEB C2C cell name: {name!r} "
        "(expected ccrs_ego{V} / ccrm_ego{V}_lead{L} / ccrb_hw{H}_d{D})"
    )


# ---------------------------------------------------------------------------
# band classification (pure)
# ---------------------------------------------------------------------------


def classify_band(
    nominal_closing_mps: float,
    contact: bool,
    impact_speed_mps: float | None,
    aeb_active: bool,
) -> tuple[str, str]:
    """-> (band, label). band in {"avoid", "mitigate", "fail"}. Crash is not
    handled here -- score_cell() short-circuits to "crash" before ever
    reaching this function, since crash is a measurement failure, not a
    behavioral outcome."""
    if not contact:
        return ("avoid", "avoid(aeb)" if aeb_active else "avoid(no_aeb)")

    impact = impact_speed_mps if impact_speed_mps is not None else 0.0
    reduction = nominal_closing_mps - impact
    half_nominal = MITIGATE_MAX_IMPACT_FRACTION * nominal_closing_mps
    eps = 1e-9
    if reduction >= MITIGATE_MIN_REDUCTION_MPS - eps or impact <= half_nominal + eps:
        return ("mitigate", f"mitigate {impact:.1f}m/s")
    return ("fail", f"fail {impact:.1f}m/s")


# ---------------------------------------------------------------------------
# per-cell raw metrics (frame-by-frame, reusing vd_metrics geometry)
# ---------------------------------------------------------------------------


def compute_cell_metrics(
    frames: list[dict], contact_sep: float = CONTACT_SEP_M
) -> dict:
    """Pure function over already-loaded telemetry frames (see
    vd_metrics.load_telemetry). Mirrors the frame loops of vd_metrics'
    min_obb_separation_above / impact_speed_below matchers, but returns raw
    numbers instead of a pass/fail verdict, and additionally harvests the
    gt.aeb.* policy detail (present, as strings, on every gated frame -- see
    module background) and the AEB policy-constraint flag."""
    min_sep: float | None = None
    contact_idx: int | None = None
    contact_ego: dict | None = None
    contact_obj: dict | None = None
    triggered = False
    aeb_constraint_seen = False
    ttc_values: list[float] = []

    # max ego deceleration over the whole run (|v| from scene vx/vy), so the
    # matrix shows how hard a non-AEB "avoid" actually braked. Repeated
    # end-of-run frames share a sim_time -> guard dt > 0.
    max_ego_decel = 0.0
    prev_t: float | None = None
    prev_speed: float | None = None
    for fr in frames:
        scene = fr.get("scene")
        if not scene:
            continue
        ego = next((o for o in (scene.get("objects") or []) if o.get("is_host")), None)
        if ego is None:
            continue
        t = fr.get("sim_time")
        if not isinstance(t, (int, float)):
            continue
        speed = math.hypot(float(ego.get("vx", 0.0)), float(ego.get("vy", 0.0)))
        if prev_t is not None and t > prev_t:
            decel = (prev_speed - speed) / (t - prev_t)
            if decel > max_ego_decel:
                max_ego_decel = decel
        prev_t, prev_speed = t, speed

    for i, fr in enumerate(frames):
        policy = fr.get("policy") or {}
        constraints = policy.get("constraints") or []
        if any(c.get("source") == "aeb" for c in constraints):
            aeb_constraint_seen = True
        detail = policy.get("detail") or {}
        if str(detail.get("gt.aeb.triggered", "")).strip().lower() == "true":
            triggered = True
        ttc_raw = detail.get("gt.aeb.ttc_s")
        if ttc_raw is not None:
            try:
                ttc_values.append(float(ttc_raw))
            except (TypeError, ValueError):
                pass

        if contact_idx is not None:
            continue  # first contact already found; keep scanning only for aeb/ttc

        scene = fr.get("scene")
        if not scene:
            continue
        objects = scene.get("objects") or []
        ego = next((o for o in objects if o.get("is_host")), None)
        if ego is None:
            continue
        frame_min: float | None = None
        frame_min_obj: dict | None = None
        for o in objects:
            if o.get("is_host"):
                continue
            sep = _vd.obb_separation(ego, o)
            if frame_min is None or sep < frame_min:
                frame_min = sep
                frame_min_obj = o
        if frame_min is None:
            continue
        if min_sep is None or frame_min < min_sep:
            min_sep = frame_min
        if frame_min <= contact_sep:
            contact_idx = i
            contact_ego, contact_obj = ego, frame_min_obj

    impact_speed_mps = None
    if contact_idx is not None:
        impact_speed_mps = _vd._closing_speed(contact_ego, contact_obj)

    return {
        "min_sep_m": min_sep,
        "contact": contact_idx is not None,
        "contact_frame_idx": contact_idx,
        "impact_speed_mps": impact_speed_mps,
        "min_ttc_s": min(ttc_values) if ttc_values else None,
        "triggered": triggered,
        "aeb_constraint_seen": aeb_constraint_seen,
        "max_ego_decel_mps2": max_ego_decel,
    }


# ---------------------------------------------------------------------------
# per-cell orchestration: crash detection + metrics + band
# ---------------------------------------------------------------------------


def _empty_record(name: str) -> dict:
    return {
        "cell": name,
        "family": None,
        "nominal_closing_mps": None,
        "band": "crash",
        "label": "crash",
        "note": None,
        "min_sep_m": None,
        "impact_speed_mps": None,
        "min_ttc_s": None,
        "triggered": False,
        "aeb_constraint_seen": False,
        "aeb_fired": False,
        "speed_reduction_mps": None,
        "max_ego_decel_mps2": None,
        "frames": 0,
    }


def score_cell(
    name: str, batch_out: Path, verdict_index: dict[str, dict] | None
) -> dict:
    """Score one grid cell. Never raises: any measurement failure (unparseable
    name, missing run dir, missing/empty telemetry, or a batch_verdict-level
    'error' status) resolves to band == "crash" with an explanatory note,
    strictly distinct from a behavioral band == "fail"."""
    verdict_index = verdict_index or {}
    rec = _empty_record(name)

    try:
        spec = parse_cell_name(name)
    except ValueError as e:
        rec["note"] = f"unparseable cell name: {e}"
        return rec
    rec["family"] = spec.family
    rec["nominal_closing_mps"] = spec.nominal_closing_mps

    vrec = verdict_index.get(name)
    if vrec and vrec.get("error"):
        rec["note"] = f"batch_verdict error: {vrec['error']}"
        return rec

    cell_dir = batch_out / name
    try:
        frames = _vd.load_telemetry(cell_dir)
    except FileNotFoundError:
        rec["note"] = (
            "run directory / telemetry.jsonl missing (needs single-cell re-run)"
        )
        return rec

    rec["frames"] = len(frames)
    if not frames:
        rec["note"] = "0 frames captured (needs single-cell re-run)"
        return rec

    metrics = compute_cell_metrics(frames)
    rec["min_sep_m"] = metrics["min_sep_m"]
    rec["impact_speed_mps"] = metrics["impact_speed_mps"]
    rec["min_ttc_s"] = metrics["min_ttc_s"]
    rec["triggered"] = metrics["triggered"]
    rec["aeb_constraint_seen"] = metrics["aeb_constraint_seen"]
    rec["aeb_fired"] = bool(metrics["triggered"] or metrics["aeb_constraint_seen"])
    rec["max_ego_decel_mps2"] = metrics["max_ego_decel_mps2"]

    band, label = classify_band(
        spec.nominal_closing_mps,
        metrics["contact"],
        metrics["impact_speed_mps"],
        rec["aeb_fired"],
    )
    rec["band"] = band
    rec["label"] = label
    if metrics["contact"] and metrics["impact_speed_mps"] is not None:
        rec["speed_reduction_mps"] = (
            spec.nominal_closing_mps - metrics["impact_speed_mps"]
        )
    return rec


# ---------------------------------------------------------------------------
# cell discovery (robust to an incomplete/missing --batch-out)
# ---------------------------------------------------------------------------

_CELL_PREFIX_RE = re.compile(r"^ccr[smb]_")


def build_verdict_index(batch_verdict: dict | None) -> dict[str, dict]:
    idx: dict[str, dict] = {}
    if not batch_verdict:
        return idx
    for s in batch_verdict.get("scenarios", []):
        stem = Path(s["scenario"]).stem
        idx[stem] = s
    return idx


def discover_cells(batch_out: Path, batch_verdict: dict | None) -> list[str]:
    """Union of: the full contractual 16-cell grid (ccrs ego 10..70, ccrm ego
    30..70, ccrb fixed 4 — so a cell that never ran still shows up as "crash"
    instead of silently vanishing from the matrix), any ccr{s,m,b}_*
    subdirectory actually present under batch_out, and any ccr{s,m,b}_*
    scenario stem batch_verdict.json recorded (covers the case where a
    scenario errored before its run directory was even created)."""
    names: set[str] = set()
    names.update(f"ccrs_ego{v}" for v in CCRS_EGO_SPEEDS_KMH)
    names.update(f"ccrm_ego{v}_lead20" for v in CCRM_EGO_SPEEDS_KMH)
    names.update(CCRB_CELLS)

    if batch_out.is_dir():
        for child in batch_out.iterdir():
            if child.is_dir() and _CELL_PREFIX_RE.match(child.name):
                names.add(child.name)

    if batch_verdict:
        for s in batch_verdict.get("scenarios", []):
            stem = Path(s["scenario"]).stem
            if _CELL_PREFIX_RE.match(stem):
                names.add(stem)

    return sorted(names)


# ---------------------------------------------------------------------------
# report rendering
# ---------------------------------------------------------------------------

_BAND_RULES_MD = f"""## バンド判定規則（固定・変更時はこの表を直す）

- **avoid**: 走行中一度も接触なし（OBB分離が常に > 0）。
  `avoid(aeb)` = AEB発火あり（gt.aeb.triggered=true または policy.constraints に
  source=="aeb"）／ `avoid(no_aeb)` = AEB非発火で無接触。**快適回避とは限らない**
  （AD層のIDM経路は快適天井を迂回して強制動できる）— 実際の制動強度は
  生値テーブルの max_ego_decel を見ること。
- **mitigate**: 接触したが、下記のいずれかを満たす。
  - 速度低減 >= {MITIGATE_MIN_REDUCTION_MPS:.2f} m/s (20 km/h) （公称閉じ速度 - 衝突速度）
  - 衝突速度 <= 公称閉じ速度の {int(MITIGATE_MAX_IMPACT_FRACTION * 100)}%
- **fail**: 接触し、上記のいずれも満たさない。
- **crash**: 採点不能（telemetry欠落／フレーム数0／batch_verdictのerrorステータス）。
  挙動 fail とは厳密に区別する（混ぜると行列が汚染される）。単独再実行で要確認。
"""


def _fmt(v, unit="", nd=2):
    if v is None:
        return "-"
    return f"{v:.{nd}f}{unit}"


def _cell_map(cells: list[dict]) -> dict[str, dict]:
    return {c["cell"]: c for c in cells}


def render_markdown(cells: list[dict]) -> str:
    by_name = _cell_map(cells)
    lines: list[str] = []
    lines.append("# AEB Car-to-Car グリッド採点結果")
    lines.append("")
    lines.append(_BAND_RULES_MD)

    # --- CCRs / CCRm matrix: rows = family, cols = ego speed ---
    lines.append("## CCRs / CCRm マトリクス（行=ファミリ、列=ego速度 km/h）")
    lines.append("")
    header = ["family"] + [str(v) for v in CCRS_EGO_SPEEDS_KMH]
    lines.append("| " + " | ".join(header) + " |")
    lines.append("| " + " | ".join(":--" for _ in header) + " |")
    for family, name_fn, speeds in (
        ("ccrs", lambda v: f"ccrs_ego{v}", CCRS_EGO_SPEEDS_KMH),
        ("ccrm_lead20", lambda v: f"ccrm_ego{v}_lead20", CCRM_EGO_SPEEDS_KMH),
    ):
        row = [family]
        for v in CCRS_EGO_SPEEDS_KMH:
            if v not in speeds:
                row.append("—")  # 契約外セル（ccrm は ego 30..70 のみ）
                continue
            c = by_name.get(name_fn(v))
            row.append(c["label"] if c else "crash")
        lines.append("| " + " | ".join(row) + " |")
    lines.append("")

    # --- CCRb matrix: 2D over whatever hw/d values are present ---
    ccrb_cells = [c for c in cells if c.get("family") == "ccrb"]
    lines.append("## CCRb マトリクス")
    lines.append("")
    if not ccrb_cells:
        lines.append("_CCRb セルなし（--batch-out に ccrb_* が含まれていない）_")
    else:
        hws = sorted({c["cell"] for c in ccrb_cells})
        # derive hw/d axis values from the cell names themselves
        hw_vals: list[float] = []
        d_vals: list[float] = []
        for c in ccrb_cells:
            spec = parse_cell_name(c["cell"])
            if spec.hw_m not in hw_vals:
                hw_vals.append(spec.hw_m)
            if spec.decel_mps2 not in d_vals:
                d_vals.append(spec.decel_mps2)
        hw_vals.sort()
        d_vals.sort()
        header = ["hw \\ d"] + [str(d) for d in d_vals]
        lines.append("| " + " | ".join(header) + " |")
        lines.append("| " + " | ".join(":--" for _ in header) + " |")
        for hw in hw_vals:
            row = [str(hw)]
            for d in d_vals:
                name = f"ccrb_hw{hw:g}_d{d:g}"
                c = by_name.get(name)
                row.append(c["label"] if c else "crash")
            lines.append("| " + " | ".join(row) + " |")
    lines.append("")

    # --- raw metrics table (all cells) ---
    lines.append("## 生値テーブル（全セル）")
    lines.append("")
    cols = [
        "cell",
        "band",
        "min_sep_m",
        "impact_v_mps",
        "min_ttc_s",
        "triggered",
        "max_ego_decel_mps2",
        "speed_reduction_mps",
        "note",
    ]
    lines.append("| " + " | ".join(cols) + " |")
    lines.append("| " + " | ".join(":--" for _ in cols) + " |")
    for c in sorted(cells, key=lambda c: c["cell"]):
        lines.append(
            "| "
            + " | ".join(
                [
                    c["cell"],
                    c["band"],
                    _fmt(c["min_sep_m"], "m"),
                    _fmt(c["impact_speed_mps"], "m/s"),
                    _fmt(c["min_ttc_s"], "s"),
                    "yes" if c["triggered"] else "no",
                    _fmt(c.get("max_ego_decel_mps2"), "m/s2"),
                    _fmt(c["speed_reduction_mps"], "m/s"),
                    c["note"] or "",
                ]
            )
            + " |"
        )
    lines.append("")

    # --- crash list ---
    crashed = [c["cell"] for c in cells if c["band"] == "crash"]
    lines.append("## crash セル一覧（単独再実行で要確認）")
    lines.append("")
    if crashed:
        for name in sorted(crashed):
            note = by_name.get(name, {}).get("note") or ""
            lines.append(f"- `{name}`: {note}")
    else:
        lines.append("_crash セルなし_")
    lines.append("")

    return "\n".join(lines)


def build_yaml_doc(cells: list[dict]) -> dict:
    return {
        "band_rules": {
            "avoid": "no contact",
            "mitigate_min_reduction_mps": MITIGATE_MIN_REDUCTION_MPS,
            "mitigate_max_impact_fraction_of_nominal": MITIGATE_MAX_IMPACT_FRACTION,
            "fail": "contact, neither mitigate condition met",
            "crash": "unmeasurable (telemetry missing/empty/batch_verdict error)",
        },
        "cells": sorted(cells, key=lambda c: c["cell"]),
    }


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(
        description="Score an AEB car-to-car grid (gt_sim_test.py batch output) "
        "into an NCAP-style band matrix (md) + raw metrics (yaml)."
    )
    ap.add_argument("--batch-out", required=True, type=Path)
    ap.add_argument("--out-md", required=True, type=Path)
    ap.add_argument("--out-yaml", required=True, type=Path)
    args = ap.parse_args(argv)

    batch_out: Path = args.batch_out
    batch_verdict = None
    bv_path = batch_out / "batch_verdict.json"
    if bv_path.is_file():
        batch_verdict = json.loads(bv_path.read_text(encoding="utf-8"))
    verdict_index = build_verdict_index(batch_verdict)

    names = discover_cells(batch_out, batch_verdict)
    cells = [score_cell(n, batch_out, verdict_index) for n in names]

    md = render_markdown(cells)
    args.out_md.parent.mkdir(parents=True, exist_ok=True)
    args.out_md.write_text(md, encoding="utf-8")

    import yaml  # lazy import, mirrors gt_sim_test.py's convention

    doc = build_yaml_doc(cells)
    args.out_yaml.parent.mkdir(parents=True, exist_ok=True)
    args.out_yaml.write_text(
        yaml.safe_dump(doc, sort_keys=False, allow_unicode=True), encoding="utf-8"
    )

    n_crash = sum(1 for c in cells if c["band"] == "crash")
    print(
        f"[score_aeb_c2c_grid] {len(cells)} cells scored ({n_crash} crash) "
        f"-> {args.out_md} , {args.out_yaml}",
        file=sys.stderr,
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
