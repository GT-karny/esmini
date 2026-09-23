"""Tests for the scenarios API HTTP-status translation (api/scenarios.py),
feature:F7 audit #3: zero coverage, "アップロードのエラー系...が無検証"
specifically named for scenarios/roads/annotation.

Service-layer logic is covered in test_scenario_service.py; here we isolate
the API layer (content-type / decode / parse-error -> 4xx) via a lightweight
fake Request (duck-typing .headers.get() and async .body()), matching this
suite's direct-function-call convention (no TestClient).
"""

from __future__ import annotations

import xml.etree.ElementTree as ET
from pathlib import Path
from types import SimpleNamespace

import pytest
from fastapi import HTTPException

from GT_esmini.web.backend.api import scenarios
from GT_esmini.web.backend.services import road_geometry_service, scenario_service


class _FakeRequest:
    def __init__(self, body: bytes, content_type: str = "application/xml"):
        self._body = body
        self.headers = {"content-type": content_type}

    async def body(self) -> bytes:
        return self._body


# ---------------------------------------------------------------------------
# list_scenarios / get_scenario
# ---------------------------------------------------------------------------


async def test_list_scenarios_passthrough(monkeypatch):
    monkeypatch.setattr(scenario_service, "list_scenarios", lambda search=None: ["x"])
    assert await scenarios.list_scenarios() == ["x"]


async def test_get_scenario_404_when_missing(monkeypatch):
    monkeypatch.setattr(scenario_service, "get_scenario_detail", lambda sid: None)

    with pytest.raises(HTTPException) as exc_info:
        await scenarios.get_scenario("nope")

    assert exc_info.value.status_code == 404


# ---------------------------------------------------------------------------
# get_road_geometry (scenario-based)
# ---------------------------------------------------------------------------


def _detail(road_file=None):
    return SimpleNamespace(road_file=road_file)


async def test_get_road_geometry_404_when_scenario_missing(monkeypatch):
    monkeypatch.setattr(scenario_service, "get_scenario_detail", lambda sid: None)

    with pytest.raises(HTTPException) as exc_info:
        await scenarios.get_road_geometry("nope")

    assert exc_info.value.status_code == 404


async def test_get_road_geometry_404_when_no_road_file(monkeypatch):
    monkeypatch.setattr(
        scenario_service, "get_scenario_detail", lambda sid: _detail(None)
    )

    with pytest.raises(HTTPException) as exc_info:
        await scenarios.get_road_geometry("scene")

    assert exc_info.value.status_code == 404
    assert "no road file" in exc_info.value.detail.lower()


async def test_get_road_geometry_404_when_road_file_not_on_disk(monkeypatch, tmp_path):
    monkeypatch.setattr(
        scenario_service,
        "get_scenario_detail",
        lambda sid: _detail("xodr/missing.xodr"),
    )
    monkeypatch.setattr(scenarios, "SCENARIOS_DIR", tmp_path)

    with pytest.raises(HTTPException) as exc_info:
        await scenarios.get_road_geometry("scene")

    assert exc_info.value.status_code == 404


async def test_get_road_geometry_returns_extracted_geometry(monkeypatch, tmp_path):
    road_abs = tmp_path / "road.xodr"
    road_abs.write_text("<OpenDRIVE/>")
    monkeypatch.setattr(
        scenario_service, "get_scenario_detail", lambda sid: _detail(str(road_abs))
    )
    monkeypatch.setattr(
        road_geometry_service, "extract_road_geometry", lambda path: {"lanes": []}
    )

    result = await scenarios.get_road_geometry("scene")

    assert result == {"lanes": []}


# ---------------------------------------------------------------------------
# upload_scenario
# ---------------------------------------------------------------------------


async def test_upload_scenario_415_on_wrong_content_type():
    req = _FakeRequest(b"<A/>", content_type="application/json")

    with pytest.raises(HTTPException) as exc_info:
        await scenarios.upload_scenario(req)

    assert exc_info.value.status_code == 415


async def test_upload_scenario_400_on_bad_utf8():
    req = _FakeRequest(b"\xff\xfe\x00bad", content_type="application/xml")

    with pytest.raises(HTTPException) as exc_info:
        await scenarios.upload_scenario(req)

    assert exc_info.value.status_code == 400


async def test_upload_scenario_400_on_xml_parse_error(monkeypatch):
    req = _FakeRequest(b"<Unclosed>", content_type="text/xml")

    def _raise_parse_error(xml_content):
        raise ET.ParseError("no element found")

    monkeypatch.setattr(scenario_service, "save_temp_scenario", _raise_parse_error)

    with pytest.raises(HTTPException) as exc_info:
        await scenarios.upload_scenario(req)

    assert exc_info.value.status_code == 400


async def test_upload_scenario_accepts_octet_stream_and_text_plain():
    for ct in ("application/octet-stream", "text/plain"):
        req = _FakeRequest(b"<OpenSCENARIO/>", content_type=ct)
        # must not raise 415 -- reaching save_temp_scenario is the real assertion,
        # so a downstream failure there would surface as something other than 415
        try:
            await scenarios.upload_scenario(req)
        except HTTPException as e:
            assert e.status_code != 415


async def test_upload_scenario_returns_service_result_on_success(monkeypatch):
    req = _FakeRequest(b"<OpenSCENARIO/>", content_type="application/xml")
    monkeypatch.setattr(
        scenario_service,
        "save_temp_scenario",
        lambda xml: {
            "scenario_id": "tmp_x",
            "entities": [],
            "road_file": None,
            "expires_at": "t",
        },
    )

    result = await scenarios.upload_scenario(req)

    assert result["scenario_id"] == "tmp_x"


# ---------------------------------------------------------------------------
# delete_uploaded_scenario
# ---------------------------------------------------------------------------


async def test_delete_uploaded_scenario_400_when_not_tmp_prefixed():
    with pytest.raises(HTTPException) as exc_info:
        await scenarios.delete_uploaded_scenario("permanent_scenario")

    assert exc_info.value.status_code == 400


async def test_delete_uploaded_scenario_404_when_service_returns_false(monkeypatch):
    monkeypatch.setattr(scenario_service, "delete_temp_scenario", lambda sid: False)

    with pytest.raises(HTTPException) as exc_info:
        await scenarios.delete_uploaded_scenario("tmp_nope")

    assert exc_info.value.status_code == 404


async def test_delete_uploaded_scenario_success(monkeypatch):
    monkeypatch.setattr(scenario_service, "delete_temp_scenario", lambda sid: True)

    result = await scenarios.delete_uploaded_scenario("tmp_abc")

    assert result == {"deleted": "tmp_abc"}


# ---------------------------------------------------------------------------
# build-from-route: the result has to land somewhere the GUI can run it
# ---------------------------------------------------------------------------


@pytest.fixture
def route_project(monkeypatch, tmp_path):
    """A real on-disk project + DB, so file placement is observed, not mocked."""
    from GT_esmini.web.backend.db import database
    from GT_esmini.web.backend.services import annotation_store, project_service

    monkeypatch.setattr(database, "DB_PATH", tmp_path / "data" / "gt_sim.db")

    async def _noop_scan(force: bool = False):
        return None

    monkeypatch.setattr(annotation_store, "scan_registry", _noop_scan)
    projects = tmp_path / "projects"
    projects.mkdir()
    monkeypatch.setattr(project_service, "get_projects_dir", lambda: projects)
    return projects


def _stub_route(monkeypatch, tmp_path, road_name="town.xodr"):
    """A road on disk and a canned plan, so no DLL is needed."""
    road = tmp_path / "roads" / road_name
    road.parent.mkdir(parents=True, exist_ok=True)
    road.write_text("<OpenDRIVE/>", encoding="utf-8")

    monkeypatch.setattr(
        scenarios.road_service, "resolve_road_path", lambda road_id: road
    )
    monkeypatch.setattr(
        scenarios,
        "plan_route",
        lambda *a, **k: {
            "waypoints": [{"road_id": 1, "lane_id": -1, "s": 5.0}],
            "snapped": [
                {"road_id": 1, "lane_id": -1, "s": 5.0, "x": 0.0, "y": 0.0, "h": 0.0}
            ],
            "lane_changes": [],
            "lane_adjustments": [],
            "length": 100.0,
            "path": [],
            "diagnostic": "ok",
        },
    )
    return road


def _request(road_id="town"):
    return scenarios.BuildFromRouteRequest(
        road_id=road_id, points=[{"x": 0.0, "y": 0.0}, {"x": 10.0, "y": 0.0}]
    )


async def test_build_from_route_saves_into_the_route_project(
    monkeypatch, tmp_path, route_project
):
    """It used to return a tmp_ id, which the GUI has no way to launch.

    The only run UI in the app is the project page, so the scenario has to be a
    project scenario -- at <root>/xosc/*.xosc, which is the only place
    list_scenarios looks.
    """
    from GT_esmini.web.backend.db import database
    from GT_esmini.web.backend.services import project_service

    await database.init_db()
    road = _stub_route(monkeypatch, tmp_path)

    result = await scenarios.build_from_route(_request())

    assert result["project_id"] == project_service.ROUTE_PROJECT_ID
    assert result["scenario_id"] == f"xosc/{result['scenario_file']}"
    root = Path((await project_service.get_project(result["project_id"])).root_path)
    assert (root / "xosc" / result["scenario_file"]).is_file()
    assert (root / "xodr" / road.name).is_file()

    # list_scenarios reports the project-relative path, which is exactly what
    # POST /api/simulations wants as its scenario_id for a project run.
    listed = await project_service.list_scenarios(result["project_id"])
    assert [s.file for s in listed] == [result["scenario_id"]]


async def test_generated_scenario_points_at_the_road_inside_the_project(
    monkeypatch, tmp_path, route_project
):
    """A project that carries an absolute path to wherever the road used to be
    is not a project, it is a bookmark. The relative form is what
    simulation_runner resolves against the scenario's own directory."""
    from GT_esmini.web.backend.db import database
    from GT_esmini.web.backend.services import project_service

    await database.init_db()
    road = _stub_route(monkeypatch, tmp_path)

    result = await scenarios.build_from_route(_request())

    root = Path((await project_service.get_project(result["project_id"])).root_path)
    xosc = root / "xosc" / result["scenario_file"]
    logic = ET.fromstring(xosc.read_text(encoding="utf-8")).find(
        "RoadNetwork/LogicFile"
    )
    assert logic is not None
    assert logic.get("filepath") == f"../xodr/{road.name}"
    # And it resolves to the copy that travelled with the project.
    assert (xosc.parent / logic.get("filepath")).resolve().is_file()


async def test_repeated_builds_accumulate_in_one_project(
    monkeypatch, tmp_path, route_project
):
    """Iterating on a route means pressing the button again. Neither a second
    project nor an overwritten scenario is an acceptable outcome."""
    from GT_esmini.web.backend.db import database
    from GT_esmini.web.backend.services import project_service

    await database.init_db()
    _stub_route(monkeypatch, tmp_path)

    first = await scenarios.build_from_route(_request())
    second = await scenarios.build_from_route(_request())

    assert first["project_id"] == second["project_id"]
    assert first["scenario_file"] != second["scenario_file"]
    assert len(await project_service.list_projects()) == 1
    assert len(await project_service.list_scenarios(first["project_id"])) == 2
