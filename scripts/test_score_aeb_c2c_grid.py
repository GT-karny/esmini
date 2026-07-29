"""Tests for score_aeb_c2c_grid.py (AEB car-to-car grid scoring/matrix generator).

TDD: written before the implementation exists (RED), so the first run is
expected to fail at import time (ModuleNotFoundError for score_aeb_c2c_grid) or
via AttributeError for missing functions once the module is stubbed in.

Covers:
  - cell-name parsing -> nominal closing speed (the固定契約 in the task prompt)
  - band classification (avoid/mitigate/fail) pure logic
  - crash vs behavioral-fail separation (telemetry missing/empty/batch_verdict
    error must all resolve to band == "crash", never "fail")
  - end-to-end score_cell() over hand-built fixture telemetry.jsonl (scene
    schema matches vd_metrics' object dict: x,y,h,length,width,is_host,vx,vy)
  - report rendering (md matrix + yaml) and the main() CLI wrapper

Run: DriverScript/.venv/Scripts/python.exe -m pytest scripts/test_score_aeb_c2c_grid.py -v
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import score_aeb_c2c_grid as grid  # noqa: E402

# ---------------------------------------------------------------------------
# fixture helpers
# ---------------------------------------------------------------------------


def _obj(id_, name, x, vx, is_host, length=4.5, width=1.8):
    return {
        "id": id_,
        "name": name,
        "x": x,
        "y": 0.0,
        "h": 0.0,
        "speed": abs(vx),
        "vx": vx,
        "vy": 0.0,
        "length": length,
        "width": width,
        "is_host": is_host,
    }


def _frame(
    t,
    ego_x,
    ego_vx,
    lead_x,
    lead_vx,
    triggered=False,
    aeb_constraint=False,
    ttc=None,
    gap=None,
    a_req=None,
):
    detail = {
        "gt.aeb.triggered": "true" if triggered else "false",
    }
    if ttc is not None:
        detail["gt.aeb.ttc_s"] = str(ttc)
    if gap is not None:
        detail["gt.aeb.gap_m"] = str(gap)
    if a_req is not None:
        detail["gt.aeb.a_req_mps2"] = str(a_req)
    constraints = []
    if aeb_constraint:
        constraints.append({"kind": "stop_at_s", "source": "aeb", "tier": "safety"})
    return {
        "sim_time": t,
        "policy": {"valid": True, "constraints": constraints, "detail": detail},
        "scene": {
            "objects": [
                _obj(0, "Ego", ego_x, ego_vx, True),
                _obj(1, "Lead", lead_x, lead_vx, False),
            ]
        },
    }


def _write_cell(batch_out: Path, name: str, frames: list[dict]) -> Path:
    cell_dir = batch_out / name
    cell_dir.mkdir(parents=True, exist_ok=True)
    lines = [json.dumps(fr) for fr in frames]
    (cell_dir / "telemetry.jsonl").write_text(
        "\n".join(lines) + ("\n" if lines else ""), encoding="utf-8"
    )
    return cell_dir


# Avoid: ego closes to a minimum 1.0 m OBB clearance (final frame: gap =
# 40.0-34.5 = 5.5 m, bodies 4.5 m long -> separation = 5.5 - 4.5 = 1.0 m)
# then never overlaps.
AVOID_FRAMES = [
    _frame(0.0, 0.0, 8.33, 40.0, 0.0, triggered=True, ttc=3.0),
    _frame(0.5, 15.0, 4.0, 40.0, 0.0, triggered=True, ttc=1.2),
    _frame(1.0, 34.0, 0.5, 40.0, 0.0, triggered=True, ttc=0.4),
    _frame(1.5, 34.5, 0.0, 40.0, 0.0, triggered=False, ttc=99.0),
]

# Mitigate: contact occurs, but the closing speed at first contact (3.0 m/s)
# is far below both the nominal (ccrs_ego50 -> 13.89 m/s) and its 50% floor.
MITIGATE_FRAMES = [
    _frame(0.0, 0.0, 13.89, 50.0, 0.0, triggered=True, ttc=3.6),
    _frame(1.0, 13.0, 8.0, 50.0, 0.0, triggered=True, ttc=1.0),
    # separation = (50-45.5) - 4.5 = 0.0 -> contact; ego vx=3.0, lead vx=0.0
    _frame(1.8, 45.5, 3.0, 50.0, 0.0, triggered=True, ttc=0.1, aeb_constraint=True),
    _frame(2.0, 45.8, 0.0, 50.0, 0.0, triggered=False),
]

# Fail: contact at high closing speed (12.0 m/s), close to the nominal 13.89
# m/s -> neither the >=5.56 m/s reduction nor the 50% floor is met.
FAIL_FRAMES = [
    _frame(0.0, 0.0, 13.89, 50.0, 0.0, triggered=False, ttc=3.6),
    _frame(1.0, 13.5, 13.0, 50.0, 0.0, triggered=False, ttc=1.5),
    # separation = (50-45.5) - 4.5 = 0.0 -> contact; ego vx=12.0, lead vx=0.0
    _frame(1.4, 45.5, 12.0, 50.0, 0.0, triggered=False),
]


# ---------------------------------------------------------------------------
# parse_cell_name
# ---------------------------------------------------------------------------


def test_parse_cell_name_ccrs():
    spec = grid.parse_cell_name("ccrs_ego50")
    assert spec.family == "ccrs"
    assert spec.ego_kmh == 50
    assert spec.nominal_closing_mps == pytest.approx(50 / 3.6, rel=1e-6)


def test_parse_cell_name_ccrm():
    spec = grid.parse_cell_name("ccrm_ego50_lead20")
    assert spec.family == "ccrm"
    assert spec.ego_kmh == 50
    assert spec.lead_kmh == 20
    assert spec.nominal_closing_mps == pytest.approx((50 - 20) / 3.6, rel=1e-6)


def test_parse_cell_name_ccrb():
    spec = grid.parse_cell_name("ccrb_hw40_d6")
    assert spec.family == "ccrb"
    assert spec.hw_m == pytest.approx(40.0)
    assert spec.decel_mps2 == pytest.approx(6.0)
    # CCRb's nominal reference is the 50 km/h worst-case closing speed,
    # independent of the specific hw/d cell (task prompt §固定契約).
    assert spec.nominal_closing_mps == pytest.approx(50 / 3.6, rel=1e-6)


def test_parse_cell_name_invalid_raises():
    with pytest.raises(ValueError):
        grid.parse_cell_name("not_a_known_cell")


# ---------------------------------------------------------------------------
# classify_band (pure logic)
# ---------------------------------------------------------------------------


def test_classify_band_avoid_no_aeb():
    # "no_aeb" ではなく "comfort" と呼ぶと、AEB 非発火だが IDM 経路で 8+ m/s²
    # 制動したセル（実測 ccrb_hw12_d6）の中身を偽る（命名規約3）。
    band, label = grid.classify_band(
        13.89, contact=False, impact_speed_mps=None, aeb_active=False
    )
    assert band == "avoid"
    assert label == "avoid(no_aeb)"


def test_classify_band_avoid_aeb():
    band, label = grid.classify_band(
        13.89, contact=False, impact_speed_mps=None, aeb_active=True
    )
    assert band == "avoid"
    assert label == "avoid(aeb)"


def test_classify_band_mitigate_by_speed_reduction():
    # nominal 13.89, impact 3.0 -> reduction 10.89 >= 5.56
    band, label = grid.classify_band(
        13.89, contact=True, impact_speed_mps=3.0, aeb_active=True
    )
    assert band == "mitigate"
    assert "mitigate" in label


def test_classify_band_mitigate_by_half_nominal():
    # nominal 10.0, impact 4.5 <= 50% of nominal (5.0), but reduction (5.5) < 5.56
    band, label = grid.classify_band(
        10.0, contact=True, impact_speed_mps=4.5, aeb_active=True
    )
    assert band == "mitigate"


def test_classify_band_fail():
    # nominal 13.89, impact 12.0 -> reduction 1.89 < 5.56, impact > 50% of nominal
    band, label = grid.classify_band(
        13.89, contact=True, impact_speed_mps=12.0, aeb_active=False
    )
    assert band == "fail"
    assert "fail" in label


# ---------------------------------------------------------------------------
# compute_cell_metrics (pure, frames already in memory)
# ---------------------------------------------------------------------------


def test_compute_cell_metrics_avoid_has_no_contact():
    m = grid.compute_cell_metrics(AVOID_FRAMES)
    assert m["contact"] is False
    assert m["impact_speed_mps"] is None
    assert m["min_sep_m"] == pytest.approx(1.0, abs=0.05)
    assert m["triggered"] is True
    assert m["min_ttc_s"] == pytest.approx(0.4, abs=1e-6)
    # ego |v|: 8.33 -> 4.0 over 0.5 s = 8.66 m/s² が最大（avoid でも実制動の強さ
    # を行列に出す — "no_aeb" が快適制動とは限らないため）
    assert m["max_ego_decel_mps2"] == pytest.approx((8.33 - 4.0) / 0.5, abs=0.01)


def test_compute_cell_metrics_mitigate_reports_impact_speed():
    m = grid.compute_cell_metrics(MITIGATE_FRAMES)
    assert m["contact"] is True
    assert m["impact_speed_mps"] == pytest.approx(3.0, abs=1e-6)
    assert m["aeb_constraint_seen"] is True


# ---------------------------------------------------------------------------
# score_cell (through telemetry.jsonl fixtures on disk) -> 4-way classification
# ---------------------------------------------------------------------------


def test_score_cell_avoid(tmp_path):
    _write_cell(tmp_path, "ccrs_ego30", AVOID_FRAMES)
    rec = grid.score_cell("ccrs_ego30", tmp_path, {})
    assert rec["band"] == "avoid"
    assert rec["label"] == "avoid(aeb)"
    assert rec["frames"] == len(AVOID_FRAMES)


def test_score_cell_mitigate(tmp_path):
    _write_cell(tmp_path, "ccrs_ego50", MITIGATE_FRAMES)
    rec = grid.score_cell("ccrs_ego50", tmp_path, {})
    assert rec["band"] == "mitigate"
    assert rec["impact_speed_mps"] == pytest.approx(3.0, abs=1e-6)
    assert rec["speed_reduction_mps"] == pytest.approx(13.89 - 3.0, abs=0.05)


def test_score_cell_fail(tmp_path):
    _write_cell(tmp_path, "ccrs_ego50", FAIL_FRAMES)
    rec = grid.score_cell("ccrs_ego50", tmp_path, {})
    assert rec["band"] == "fail"
    assert rec["impact_speed_mps"] == pytest.approx(12.0, abs=1e-6)


def test_score_cell_crash_missing_directory(tmp_path):
    # the cell was never run at all -> must be "crash", never silently "fail"
    rec = grid.score_cell("ccrs_ego60", tmp_path, {})
    assert rec["band"] == "crash"
    assert rec["note"]


def test_score_cell_crash_zero_frames(tmp_path):
    _write_cell(tmp_path, "ccrs_ego70", [])
    rec = grid.score_cell("ccrs_ego70", tmp_path, {})
    assert rec["band"] == "crash"
    assert rec["frames"] == 0


def test_score_cell_crash_from_batch_verdict_error(tmp_path):
    # telemetry exists and looks fine, but the batch marked this scenario as an
    # 'error' status (init failure etc.) -> crash must win over any measurement.
    _write_cell(tmp_path, "ccrs_ego40", MITIGATE_FRAMES)
    verdict_index = {"ccrs_ego40": {"error": "GT_InitWithArgs failed (rc=1)"}}
    rec = grid.score_cell("ccrs_ego40", tmp_path, verdict_index)
    assert rec["band"] == "crash"
    assert "GT_InitWithArgs" in rec["note"]


def test_score_cell_never_confuses_crash_with_behavioral_fail(tmp_path):
    # regression guard for the project's "crash vs fail must never mix" rule
    _write_cell(tmp_path, "ccrs_ego50", FAIL_FRAMES)
    fail_rec = grid.score_cell("ccrs_ego50", tmp_path, {})
    crash_rec = grid.score_cell("ccrs_ego99_missing", tmp_path, {})
    assert fail_rec["band"] == "fail"
    assert crash_rec["band"] == "crash"
    assert fail_rec["band"] != crash_rec["band"]


# ---------------------------------------------------------------------------
# discover_cells: robust to an incomplete/absent batch-out
# ---------------------------------------------------------------------------


def test_discover_cells_enumerates_exact_16_cell_contract(tmp_path):
    # nothing on disk at all -> the full contractual 16-cell grid must still
    # appear (so a cell that never ran is scored "crash" rather than silently
    # omitted): ccrs ego 10..70, ccrm ego 30..70 (NOT 10/20), ccrb fixed 4.
    names = grid.discover_cells(tmp_path, None)
    for v in range(10, 71, 10):
        assert f"ccrs_ego{v}" in names
    for v in range(30, 71, 10):
        assert f"ccrm_ego{v}_lead20" in names
    assert "ccrm_ego10_lead20" not in names
    assert "ccrm_ego20_lead20" not in names
    for cell in ("ccrb_hw12_d2", "ccrb_hw12_d6", "ccrb_hw40_d2", "ccrb_hw40_d6"):
        assert cell in names
    assert len(names) == 16


def test_discover_cells_picks_up_extra_dirs_beyond_contract(tmp_path):
    (tmp_path / "ccrb_hw20_d8").mkdir(parents=True)
    names = grid.discover_cells(tmp_path, None)
    assert "ccrb_hw20_d8" in names
    assert "ccrb_hw40_d6" in names  # contractual cell present without a dir


# ---------------------------------------------------------------------------
# report rendering
# ---------------------------------------------------------------------------


def test_render_markdown_contains_band_rules_and_cell_labels():
    cells = [
        grid.score_cell("ccrs_ego30", Path("__nonexistent__"), {}),
    ]
    # give it one concrete scored cell instead
    cells = []
    md = grid.render_markdown(
        [
            {
                "cell": "ccrs_ego30",
                "family": "ccrs",
                "nominal_closing_mps": 8.33,
                "band": "avoid",
                "label": "avoid(aeb)",
                "note": None,
                "min_sep_m": 1.5,
                "impact_speed_mps": None,
                "min_ttc_s": 0.4,
                "triggered": True,
                "aeb_constraint_seen": False,
                "aeb_fired": True,
                "speed_reduction_mps": None,
                "frames": 4,
            },
            {
                "cell": "ccrs_ego50",
                "family": "ccrs",
                "nominal_closing_mps": 13.89,
                "band": "fail",
                "label": "fail 12.0m/s",
                "note": None,
                "min_sep_m": 0.0,
                "impact_speed_mps": 12.0,
                "min_ttc_s": 0.1,
                "triggered": False,
                "aeb_constraint_seen": False,
                "aeb_fired": False,
                "speed_reduction_mps": 1.89,
                "frames": 3,
            },
            {
                "cell": "ccrs_ego60",
                "family": "ccrs",
                "nominal_closing_mps": 16.67,
                "band": "crash",
                "label": "crash",
                "note": "run directory / telemetry.jsonl missing",
                "min_sep_m": None,
                "impact_speed_mps": None,
                "min_ttc_s": None,
                "triggered": False,
                "aeb_constraint_seen": False,
                "aeb_fired": False,
                "speed_reduction_mps": None,
                "frames": 0,
            },
        ]
    )
    assert "avoid(aeb)" in md
    assert "fail 12.0m/s" in md or "fail" in md
    assert "crash" in md
    assert "5.56" in md  # band rule documented in the header
    assert "ccrs_ego60" in md  # crash cell listed for re-run


def test_build_yaml_doc_has_all_cells_with_band_and_raw_metrics():
    cells = [
        {
            "cell": "ccrs_ego30",
            "family": "ccrs",
            "nominal_closing_mps": 8.33,
            "band": "avoid",
            "label": "avoid(aeb)",
            "note": None,
            "min_sep_m": 1.5,
            "impact_speed_mps": None,
            "min_ttc_s": 0.4,
            "triggered": True,
            "aeb_constraint_seen": False,
            "aeb_fired": True,
            "speed_reduction_mps": None,
            "frames": 4,
        }
    ]
    doc = grid.build_yaml_doc(cells)
    assert "cells" in doc
    assert doc["cells"][0]["cell"] == "ccrs_ego30"
    assert doc["cells"][0]["band"] == "avoid"
    assert doc["cells"][0]["min_sep_m"] == pytest.approx(1.5)


# ---------------------------------------------------------------------------
# main() CLI end-to-end
# ---------------------------------------------------------------------------


def test_main_writes_md_and_yaml_outputs(tmp_path):
    batch_out = tmp_path / "batch"
    _write_cell(batch_out, "ccrs_ego50", MITIGATE_FRAMES)
    out_md = tmp_path / "report.md"
    out_yaml = tmp_path / "report.yaml"

    rc = grid.main(
        [
            "--batch-out",
            str(batch_out),
            "--out-md",
            str(out_md),
            "--out-yaml",
            str(out_yaml),
        ]
    )

    assert rc == 0
    assert out_md.is_file()
    assert out_yaml.is_file()
    md_text = out_md.read_text(encoding="utf-8")
    assert "ccrs_ego50" in md_text
    import yaml as _yaml

    doc = _yaml.safe_load(out_yaml.read_text(encoding="utf-8"))
    names = {c["cell"] for c in doc["cells"]}
    assert "ccrs_ego50" in names
    # the full contractual ccrs range must be present too (as crash entries)
    assert "ccrs_ego10" in names
