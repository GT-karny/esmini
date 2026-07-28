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
    monkeypatch.setattr(scenario_service, "get_scenario_detail", lambda sid: _detail(None))

    with pytest.raises(HTTPException) as exc_info:
        await scenarios.get_road_geometry("scene")

    assert exc_info.value.status_code == 404
    assert "no road file" in exc_info.value.detail.lower()


async def test_get_road_geometry_404_when_road_file_not_on_disk(monkeypatch, tmp_path):
    monkeypatch.setattr(
        scenario_service, "get_scenario_detail", lambda sid: _detail("xodr/missing.xodr")
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
        lambda xml: {"scenario_id": "tmp_x", "entities": [], "road_file": None, "expires_at": "t"},
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
