"""Tests for services/scenario_service.py (feature:F7 audit #3: `api/scenarios.py`
+ `services/scenario_service.py` had zero coverage; audit named "アップロードの
エラー系...が無検証" and grouped this with roads.py/annotation.py as medium
priority, one dimension short of the top-4).
"""

from __future__ import annotations

import xml.etree.ElementTree as ET

import pytest

from GT_esmini.web.backend.services import scenario_service

_XOSC_WITH_ROAD_AND_CONTROLLER = """<?xml version="1.0" encoding="UTF-8"?>
<OpenSCENARIO>
  <RoadNetwork>
    <LogicFile filepath="xodr/road.xodr"/>
  </RoadNetwork>
  <Entities>
    <ScenarioObject name="Ego">
      <Vehicle name="car"/>
      <ObjectController>
        <Controller name="VirtualDriverController"/>
      </ObjectController>
    </ScenarioObject>
  </Entities>
</OpenSCENARIO>
"""

_MALFORMED_XOSC = "<OpenSCENARIO><Unclosed>"


@pytest.fixture
def dirs(monkeypatch, tmp_path):
    scenarios_dir = tmp_path / "resources" / "xosc"
    scenarios_dir.mkdir(parents=True)
    temp_dir = tmp_path / "data" / "_temp_scenarios"
    monkeypatch.setattr(scenario_service, "SCENARIOS_DIR", scenarios_dir)
    monkeypatch.setattr(scenario_service, "TEMP_SCENARIOS_DIR", temp_dir)
    monkeypatch.setattr(scenario_service, "REPO_ROOT", tmp_path)
    return scenarios_dir, temp_dir


# ---------------------------------------------------------------------------
# list_scenarios
# ---------------------------------------------------------------------------


def test_list_scenarios_empty_when_dir_missing(monkeypatch, tmp_path):
    monkeypatch.setattr(scenario_service, "SCENARIOS_DIR", tmp_path / "does-not-exist")
    assert scenario_service.list_scenarios() == []


def test_list_scenarios_lists_xosc_files_sorted(dirs):
    scenarios_dir, _ = dirs
    (scenarios_dir / "b_scene.xosc").write_text("<A/>")
    (scenarios_dir / "a_scene.xosc").write_text("<A/>")
    (scenarios_dir / "not_a_scenario.txt").write_text("x")

    results = scenario_service.list_scenarios()

    assert [r.id for r in results] == ["a_scene", "b_scene"]


def test_list_scenarios_search_is_case_insensitive_substring(dirs):
    scenarios_dir, _ = dirs
    (scenarios_dir / "LaneChange.xosc").write_text("<A/>")
    (scenarios_dir / "CutIn.xosc").write_text("<A/>")

    results = scenario_service.list_scenarios(search="lane")

    assert [r.id for r in results] == ["LaneChange"]


# ---------------------------------------------------------------------------
# get_scenario_detail
# ---------------------------------------------------------------------------


def test_get_scenario_detail_none_when_missing(dirs):
    assert scenario_service.get_scenario_detail("nope") is None


def test_get_scenario_detail_parses_road_and_controller(dirs):
    scenarios_dir, _ = dirs
    (scenarios_dir / "scene.xosc").write_text(_XOSC_WITH_ROAD_AND_CONTROLLER, encoding="utf-8")

    detail = scenario_service.get_scenario_detail("scene")

    assert detail.road_file == "xodr/road.xodr"
    assert detail.has_controller is True
    assert len(detail.entities) == 1
    assert detail.entities[0].name == "Ego"
    assert detail.entities[0].controller == "VirtualDriverController"


def test_get_scenario_detail_falls_back_to_bare_detail_on_parse_error(dirs):
    scenarios_dir, _ = dirs
    (scenarios_dir / "broken.xosc").write_text(_MALFORMED_XOSC, encoding="utf-8")

    detail = scenario_service.get_scenario_detail("broken")

    assert detail.id == "broken"
    assert detail.road_file is None
    assert detail.entities == []


# ---------------------------------------------------------------------------
# get_scenario_path
# ---------------------------------------------------------------------------


def test_get_scenario_path_resolves_regular_scenario(dirs):
    scenarios_dir, _ = dirs
    (scenarios_dir / "scene.xosc").write_text("<A/>")

    path = scenario_service.get_scenario_path("scene")

    assert path == scenarios_dir / "scene.xosc"


def test_get_scenario_path_none_when_missing(dirs):
    assert scenario_service.get_scenario_path("nope") is None


def test_get_scenario_path_resolves_temp_scenario(dirs):
    _, temp_dir = dirs
    tmp_id = "tmp_abc123"
    (temp_dir / tmp_id).mkdir(parents=True)
    (temp_dir / tmp_id / f"{tmp_id}.xosc").write_text("<A/>")

    path = scenario_service.get_scenario_path(tmp_id)

    assert path == temp_dir / tmp_id / f"{tmp_id}.xosc"


# ---------------------------------------------------------------------------
# save_temp_scenario / delete_temp_scenario / cleanup_expired_scenarios
# ---------------------------------------------------------------------------


def test_save_temp_scenario_writes_file_with_tmp_prefix_and_absolutizes_paths(dirs):
    scenarios_dir, temp_dir = dirs

    result = scenario_service.save_temp_scenario(_XOSC_WITH_ROAD_AND_CONTROLLER)

    assert result["scenario_id"].startswith("tmp_")
    assert result["road_file"] == "xodr/road.xodr"  # original, pre-absolutization value
    assert result["entities"] == [{"name": "Ego", "model": "car"}]
    assert "expires_at" in result

    written_path = temp_dir / result["scenario_id"] / f"{result['scenario_id']}.xosc"
    assert written_path.is_file()
    tree = ET.parse(written_path)
    logic_filepath = tree.getroot().find(".//RoadNetwork/LogicFile").get("filepath")
    assert logic_filepath == str((scenarios_dir / "xodr" / "road.xodr").resolve())


def test_delete_temp_scenario_rejects_non_tmp_prefix(dirs):
    assert scenario_service.delete_temp_scenario("not_tmp_prefixed") is False


def test_delete_temp_scenario_removes_existing(dirs):
    result = scenario_service.save_temp_scenario("<OpenSCENARIO/>")
    scenario_id = result["scenario_id"]
    _, temp_dir = dirs
    assert (temp_dir / scenario_id).is_dir()

    ok = scenario_service.delete_temp_scenario(scenario_id)

    assert ok is True
    assert not (temp_dir / scenario_id).exists()


def test_delete_temp_scenario_false_when_missing(dirs):
    assert scenario_service.delete_temp_scenario("tmp_never_existed") is False


def test_cleanup_expired_scenarios_removes_only_expired(dirs, monkeypatch):
    _, temp_dir = dirs
    fresh = scenario_service.save_temp_scenario("<OpenSCENARIO/>")["scenario_id"]

    # TTL of -1: "older than TTL" is true for anything with a non-negative age,
    # i.e. everything that already exists -- deterministic without touching
    # file timestamps (ctime isn't settable via os.utime on Windows).
    monkeypatch.setattr(scenario_service, "TEMP_FILE_TTL_SECONDS", -1)

    count = scenario_service.cleanup_expired_scenarios()

    assert count == 1
    assert not (temp_dir / fresh).exists()


def test_cleanup_expired_scenarios_keeps_fresh_entries(dirs, monkeypatch):
    _, temp_dir = dirs
    fresh = scenario_service.save_temp_scenario("<OpenSCENARIO/>")["scenario_id"]

    monkeypatch.setattr(scenario_service, "TEMP_FILE_TTL_SECONDS", 3600)

    count = scenario_service.cleanup_expired_scenarios()

    assert count == 0
    assert (temp_dir / fresh).is_dir()


def test_cleanup_expired_scenarios_empty_dir_returns_zero(dirs):
    assert scenario_service.cleanup_expired_scenarios() == 0
