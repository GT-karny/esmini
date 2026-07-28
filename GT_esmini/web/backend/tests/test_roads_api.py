"""Tests for road file management (api/roads.py + services/road_service.py),
feature:F7 audit #3: zero coverage on both, grouped with scenarios.py/
annotation.py as medium priority.
"""

from __future__ import annotations

import pytest
from fastapi import HTTPException

from GT_esmini.web.backend.api import roads
from GT_esmini.web.backend.services import road_service


class _FakeRequest:
    def __init__(self, body: bytes, content_type: str = "application/xml"):
        self._body = body
        self.headers = {"content-type": content_type}

    async def body(self) -> bytes:
        return self._body


# ---------------------------------------------------------------------------
# services/road_service.py
# ---------------------------------------------------------------------------


@pytest.fixture
def temp_roads_dir(monkeypatch, tmp_path):
    d = tmp_path / "_temp_roads"
    monkeypatch.setattr(road_service, "TEMP_ROADS_DIR", d)
    return d


def test_save_temp_road_writes_file_with_tmp_road_prefix(temp_roads_dir):
    result = road_service.save_temp_road("<OpenDRIVE/>")

    assert result["road_id"].startswith("tmp_road_")
    path = temp_roads_dir / f"{result['road_id']}.xodr"
    assert path.is_file()
    assert path.read_text(encoding="utf-8") == "<OpenDRIVE/>"
    assert result["road_path"] == str(path)


def test_delete_temp_road_rejects_wrong_prefix(temp_roads_dir):
    assert road_service.delete_temp_road("not_a_temp_road") is False


def test_delete_temp_road_removes_existing(temp_roads_dir):
    result = road_service.save_temp_road("<OpenDRIVE/>")
    road_id = result["road_id"]
    assert (temp_roads_dir / f"{road_id}.xodr").is_file()

    ok = road_service.delete_temp_road(road_id)

    assert ok is True
    assert not (temp_roads_dir / f"{road_id}.xodr").exists()


def test_delete_temp_road_false_when_missing(temp_roads_dir):
    assert road_service.delete_temp_road("tmp_road_never_existed") is False


def test_cleanup_expired_roads_removes_only_expired(temp_roads_dir, monkeypatch):
    road_id = road_service.save_temp_road("<OpenDRIVE/>")["road_id"]
    monkeypatch.setattr(road_service, "TEMP_FILE_TTL_SECONDS", -1)

    count = road_service.cleanup_expired_roads()

    assert count == 1
    assert not (temp_roads_dir / f"{road_id}.xodr").exists()


def test_cleanup_expired_roads_keeps_fresh(temp_roads_dir, monkeypatch):
    road_id = road_service.save_temp_road("<OpenDRIVE/>")["road_id"]
    monkeypatch.setattr(road_service, "TEMP_FILE_TTL_SECONDS", 3600)

    count = road_service.cleanup_expired_roads()

    assert count == 0
    assert (temp_roads_dir / f"{road_id}.xodr").is_file()


def test_cleanup_expired_roads_ignores_non_xodr_files(temp_roads_dir, monkeypatch):
    temp_roads_dir.mkdir(parents=True, exist_ok=True)
    (temp_roads_dir / "stray.txt").write_text("not a road")
    monkeypatch.setattr(road_service, "TEMP_FILE_TTL_SECONDS", -1)

    count = road_service.cleanup_expired_roads()

    assert count == 0
    assert (temp_roads_dir / "stray.txt").is_file()


def test_cleanup_expired_roads_empty_dir_returns_zero(tmp_path, monkeypatch):
    monkeypatch.setattr(road_service, "TEMP_ROADS_DIR", tmp_path / "does-not-exist")
    assert road_service.cleanup_expired_roads() == 0


# ---------------------------------------------------------------------------
# api/roads.py
# ---------------------------------------------------------------------------


async def test_upload_road_415_on_wrong_content_type():
    req = _FakeRequest(b"<OpenDRIVE/>", content_type="application/json")

    with pytest.raises(HTTPException) as exc_info:
        await roads.upload_road(req)

    assert exc_info.value.status_code == 415


async def test_upload_road_400_on_bad_utf8():
    req = _FakeRequest(b"\xff\xfe\x00bad")

    with pytest.raises(HTTPException) as exc_info:
        await roads.upload_road(req)

    assert exc_info.value.status_code == 400


async def test_upload_road_returns_service_result(monkeypatch):
    req = _FakeRequest(b"<OpenDRIVE/>")
    monkeypatch.setattr(
        road_service,
        "save_temp_road",
        lambda xml: {"road_id": "tmp_road_x", "road_path": "p"},
    )

    result = await roads.upload_road(req)

    assert result == {"road_id": "tmp_road_x", "road_path": "p"}


async def test_delete_uploaded_road_400_when_wrong_prefix():
    with pytest.raises(HTTPException) as exc_info:
        await roads.delete_uploaded_road("not_a_temp_road")

    assert exc_info.value.status_code == 400


async def test_delete_uploaded_road_404_when_service_returns_false(monkeypatch):
    monkeypatch.setattr(road_service, "delete_temp_road", lambda rid: False)

    with pytest.raises(HTTPException) as exc_info:
        await roads.delete_uploaded_road("tmp_road_nope")

    assert exc_info.value.status_code == 404


async def test_delete_uploaded_road_success(monkeypatch):
    monkeypatch.setattr(road_service, "delete_temp_road", lambda rid: True)

    result = await roads.delete_uploaded_road("tmp_road_abc")

    assert result == {"deleted": "tmp_road_abc"}
