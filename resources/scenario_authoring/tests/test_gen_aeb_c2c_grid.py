"""TDD tests for gen_aeb_c2c_grid.py — AEB car-to-car parametric grid generator.

Run (venv, from repo root):
    DriverScript/.venv/Scripts/python.exe -m pytest \
        resources/scenario_authoring/tests/test_gen_aeb_c2c_grid.py -v

Covers the RED->GREEN contract from the task:
  1) cell count == 16 and filename-stem set matches the fixed contract
  2) representative-cell parameter injection (ccrs_ego70 / ccrb_hw12_d6) via
     xosc parse (initial speeds, gap, brake dynamics)
  3) two generations are byte-identical (determinism)
  4) manifest entry count == 16, osi:true, policies == [lead, aeb]
  5) main() actually writes the 16 xosc + manifest to the fixed contract
     locations (resources/xosc/verification/aeb_c2c_grid/ and
     resources/xosc/verification/aeb_c2c_grid_batch.yaml)
"""

from __future__ import annotations

import math
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

import yaml

_REPO_ROOT = Path(__file__).resolve().parents[3]
_AUTHORING_ROOT = _REPO_ROOT / "resources" / "scenario_authoring"
_TEMPLATES_DIR = _AUTHORING_ROOT / "scenario_templates"
for _p in (_AUTHORING_ROOT, _TEMPLATES_DIR):
    if str(_p) not in sys.path:
        sys.path.insert(0, str(_p))

import gen_aeb_c2c_grid as gen  # noqa: E402

OUT_DIR = _REPO_ROOT / "resources" / "xosc" / "verification" / "aeb_c2c_grid"
MANIFEST_PATH = (
    _REPO_ROOT / "resources" / "xosc" / "verification" / "aeb_c2c_grid_batch.yaml"
)

EXPECTED_STEMS = {
    "ccrs_ego10",
    "ccrs_ego20",
    "ccrs_ego30",
    "ccrs_ego40",
    "ccrs_ego50",
    "ccrs_ego60",
    "ccrs_ego70",
    "ccrm_ego30_lead20",
    "ccrm_ego40_lead20",
    "ccrm_ego50_lead20",
    "ccrm_ego60_lead20",
    "ccrm_ego70_lead20",
    "ccrb_hw12_d2",
    "ccrb_hw12_d6",
    "ccrb_hw40_d2",
    "ccrb_hw40_d6",
}


# ---------------------------------------------------------------------------
# xosc parse helpers
# ---------------------------------------------------------------------------


def _teleport_s(root: ET.Element, entity: str) -> float:
    el = root.find(
        f".//Storyboard/Init/Actions/Private[@entityRef='{entity}']"
        "//TeleportAction/Position/LanePosition"
    )
    assert el is not None, f"no TeleportAction/LanePosition for {entity}"
    return float(el.get("s"))


def _init_speed(root: ET.Element, entity: str) -> float:
    el = root.find(
        f".//Storyboard/Init/Actions/Private[@entityRef='{entity}']"
        "//LongitudinalAction/SpeedAction/SpeedActionTarget/AbsoluteTargetSpeed"
    )
    assert el is not None, f"no init SpeedAction for {entity}"
    return float(el.get("value"))


# ---------------------------------------------------------------------------
# 1) cell count / filename contract
# ---------------------------------------------------------------------------


def test_cell_count_and_names():
    cells = gen.build_all_cells()
    assert len(cells) == 16
    assert {c.stem for c in cells} == EXPECTED_STEMS


# ---------------------------------------------------------------------------
# 2) representative cells
# ---------------------------------------------------------------------------


def test_ccrs_ego70_params(tmp_path):
    cells = {c.stem: c for c in gen.build_all_cells()}
    cell = cells["ccrs_ego70"]
    out = tmp_path / "ccrs_ego70.xosc"
    gen.write_scenario(cell.scenario, out)
    root = ET.parse(out).getroot()

    ego_speed = _init_speed(root, "Ego")
    lead_speed = _init_speed(root, "Lead")
    assert math.isclose(ego_speed, 70.0 / 3.6, rel_tol=1e-6)
    assert lead_speed == 0.0  # CCRs: stopped lead

    ego_s = _teleport_s(root, "Ego")
    lead_s = _teleport_s(root, "Lead")
    # TTC0=6s against closing speed, floored at 40m (generator rounds to 3dp
    # internally — bumper_gap_m is the documented public formula, so reuse it
    # rather than a hand-recomputed value that would drift on rounding).
    expected_gap = gen.bumper_gap_m(70.0 / 3.6)
    assert math.isclose(lead_s - ego_s, expected_gap + gen.VEHICLE_LENGTH, rel_tol=1e-6)


def test_ccrb_hw12_d6_params(tmp_path):
    cells = {c.stem: c for c in gen.build_all_cells()}
    cell = cells["ccrb_hw12_d6"]
    out = tmp_path / "ccrb_hw12_d6.xosc"
    gen.write_scenario(cell.scenario, out)
    root = ET.parse(out).getroot()

    ego_speed = _init_speed(root, "Ego")
    lead_speed = _init_speed(root, "Lead")
    assert math.isclose(ego_speed, 50.0 / 3.6, rel_tol=1e-6)
    assert math.isclose(lead_speed, 50.0 / 3.6, rel_tol=1e-6)

    ego_s = _teleport_s(root, "Ego")
    lead_s = _teleport_s(root, "Lead")
    assert math.isclose(lead_s - ego_s, 12.0 + gen.VEHICLE_LENGTH, rel_tol=1e-6)

    brake_speed_action = root.find(
        ".//Storyboard/Story/Act/ManeuverGroup/Maneuver/Event/Action"
        "/PrivateAction/LongitudinalAction/SpeedAction"
    )
    assert brake_speed_action is not None, "no Lead brake SpeedAction found"
    dyn = brake_speed_action.find("SpeedActionDynamics")
    assert dyn.get("dynamicsShape") == "linear"
    assert dyn.get("dynamicsDimension") == "rate"
    assert math.isclose(float(dyn.get("value")), 6.0, rel_tol=1e-6)
    target = brake_speed_action.find("SpeedActionTarget/AbsoluteTargetSpeed")
    assert math.isclose(float(target.get("value")), 0.0, abs_tol=1e-9)

    trigger_val = root.find(
        ".//Storyboard/Story/Act/ManeuverGroup/Maneuver/Event/StartTrigger"
        "/ConditionGroup/Condition/ByValueCondition/SimulationTimeCondition"
    )
    assert trigger_val is not None
    assert math.isclose(float(trigger_val.get("value")), 2.0, rel_tol=1e-6)


# ---------------------------------------------------------------------------
# 3) determinism
# ---------------------------------------------------------------------------


def test_determinism_byte_identical(tmp_path):
    out1 = tmp_path / "run1"
    out2 = tmp_path / "run2"
    out1.mkdir()
    out2.mkdir()

    for c in gen.build_all_cells():
        gen.write_scenario(c.scenario, out1 / f"{c.stem}.xosc")
    for c in gen.build_all_cells():  # fresh objects, second pass
        gen.write_scenario(c.scenario, out2 / f"{c.stem}.xosc")

    for stem in EXPECTED_STEMS:
        b1 = (out1 / f"{stem}.xosc").read_bytes()
        b2 = (out2 / f"{stem}.xosc").read_bytes()
        assert b1 == b2, f"{stem} not byte-identical across regenerations"


# ---------------------------------------------------------------------------
# 4) manifest contract
# ---------------------------------------------------------------------------


def test_manifest_contract():
    cells = gen.build_all_cells()
    text = gen.manifest_text(cells)
    spec = yaml.safe_load(text)

    assert len(spec["scenarios"]) == 16
    assert spec["defaults"]["osi"] is True
    for entry in spec["scenarios"]:
        assert entry["policies"] == ["lead", "aeb"]
        assert "expectations" not in entry
        assert "baseline" not in entry


# ---------------------------------------------------------------------------
# 5) actual generation to the fixed contract locations
# ---------------------------------------------------------------------------


def test_main_writes_expected_files_to_fixed_locations():
    gen.main()

    assert OUT_DIR.is_dir()
    written_stems = {p.stem for p in OUT_DIR.glob("*.xosc")}
    assert written_stems == EXPECTED_STEMS

    assert MANIFEST_PATH.is_file()
    spec = yaml.safe_load(MANIFEST_PATH.read_text(encoding="utf-8"))
    assert len(spec["scenarios"]) == 16
