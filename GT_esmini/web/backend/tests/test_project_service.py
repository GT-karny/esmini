"""Tests for services/project_service.py (feature:F7 audit
"web backend APIの18ファイル中14ファイルがテスト0件" #3 残り項目: `api/projects.py`
(20エンドポイント) + `service/project_service.py`(830行)が監査時点で**最大の
未テストサービス**、0件だった。「path traversal対策・413/403/404/409分岐が
丸ごと無検証」という指摘に対応する。

APIレイヤ(`api/projects.py`)の薄いHTTPステータス変換は`test_projects_api.py`
で別途カバーする。ここではビジネスロジック本体（サービス層）を対象に:
  - プロジェクトCRUD + builtin保護 + ファイルシステム同期(sync_projects)
  - ZIP展開（単一ルート剥離・不正ZIPでのクリーンアップ）
  - ファイル管理のpath traversal防御（upload/get/delete全経路）
  - シナリオ/パラメータのパース連携
  - パラメータプリセットのYAML CRUD（一意化・rename衝突・破損検知）
"""

from __future__ import annotations

import zipfile
from io import BytesIO
from pathlib import Path

import pytest

from GT_esmini.web.backend.db import database
from GT_esmini.web.backend.models.project import (
    ProjectCreateRequest,
)
from GT_esmini.web.backend.services import annotation_store, project_service


@pytest.fixture
def tmp_db(monkeypatch, tmp_path):
    """Redirect DB_PATH to a per-test file and disable the best-effort
    verification-registry warm scan (see test_database.py's identical
    fixture)."""
    db_path = tmp_path / "data" / "gt_sim.db"
    monkeypatch.setattr(database, "DB_PATH", db_path)

    async def _noop_scan(force: bool = False):
        return None

    monkeypatch.setattr(annotation_store, "scan_registry", _noop_scan)
    return db_path


@pytest.fixture
def projects_dir(monkeypatch, tmp_path) -> Path:
    """project_service calls get_projects_dir() as a bare name resolved in its
    OWN module globals (imported via `from config import get_projects_dir`) --
    patch it there, not on the config module, or the patch has no effect."""
    d = tmp_path / "projects"
    d.mkdir()
    monkeypatch.setattr(project_service, "get_projects_dir", lambda: d)
    return d


async def _init(tmp_db):
    await database.init_db()


# ---------------------------------------------------------------------------
# builtin project + filesystem sync
# ---------------------------------------------------------------------------


async def test_ensure_builtin_project_registers_once_and_is_idempotent(tmp_db):
    await _init(tmp_db)
    await project_service.ensure_builtin_project()
    await project_service.ensure_builtin_project()  # must not raise / duplicate

    proj = await project_service.get_project(project_service.BUILTIN_PROJECT_ID)
    assert proj is not None
    assert proj.is_builtin is True


async def test_sync_projects_auto_registers_new_folder(tmp_db, projects_dir):
    await _init(tmp_db)
    (projects_dir / "my_scenarios").mkdir()

    projects = await project_service.list_projects()  # calls sync_projects()

    names = {p.name for p in projects}
    assert "my_scenarios" in names


async def test_sync_projects_skips_hidden_directories(tmp_db, projects_dir):
    await _init(tmp_db)
    (projects_dir / ".hidden").mkdir()

    projects = await project_service.list_projects()

    assert ".hidden" not in {p.name for p in projects}


async def test_sync_projects_removes_stale_db_entry_when_folder_deleted(
    tmp_db, projects_dir
):
    await _init(tmp_db)
    folder = projects_dir / "gone_soon"
    folder.mkdir()
    await project_service.list_projects()  # registers it
    assert any(p.name == "gone_soon" for p in await project_service.list_projects())

    import shutil

    shutil.rmtree(folder)
    projects = await project_service.list_projects()

    assert not any(p.name == "gone_soon" for p in projects)


async def test_sync_projects_never_touches_builtin(tmp_db, projects_dir, monkeypatch):
    await _init(tmp_db)
    monkeypatch.setattr(project_service, "RESOURCES_DIR", projects_dir.parent / "resources")
    (project_service.RESOURCES_DIR).mkdir(parents=True, exist_ok=True)
    await project_service.ensure_builtin_project()

    await project_service.list_projects()  # sync_projects() runs inside

    proj = await project_service.get_project(project_service.BUILTIN_PROJECT_ID)
    assert proj is not None  # still there, untouched by the folder-diff logic


# ---------------------------------------------------------------------------
# CRUD + builtin protection
# ---------------------------------------------------------------------------


async def test_create_get_project_roundtrip(tmp_db, projects_dir):
    await _init(tmp_db)
    detail = await project_service.create_project(
        ProjectCreateRequest(name="Foo", description="bar")
    )

    assert Path(detail.root_path).is_dir()
    fetched = await project_service.get_project(detail.project_id)
    assert fetched.name == "Foo"
    assert fetched.description == "bar"
    assert fetched.is_builtin is False


async def test_get_project_returns_none_for_unknown_id(tmp_db):
    await _init(tmp_db)
    assert await project_service.get_project("does-not-exist") is None


async def test_update_project_changes_name_and_description(tmp_db, projects_dir):
    await _init(tmp_db)
    detail = await project_service.create_project(ProjectCreateRequest(name="A"))

    ok = await project_service.update_project(detail.project_id, "B", "desc")

    assert ok is True
    fetched = await project_service.get_project(detail.project_id)
    assert fetched.name == "B"
    assert fetched.description == "desc"


async def test_update_project_rejects_builtin(tmp_db, projects_dir, monkeypatch):
    await _init(tmp_db)
    monkeypatch.setattr(project_service, "RESOURCES_DIR", projects_dir.parent / "resources")
    project_service.RESOURCES_DIR.mkdir(parents=True, exist_ok=True)
    await project_service.ensure_builtin_project()

    ok = await project_service.update_project(
        project_service.BUILTIN_PROJECT_ID, "Hacked", None
    )

    assert ok is False


async def test_update_project_rejects_unknown_id(tmp_db):
    await _init(tmp_db)
    assert await project_service.update_project("nope", "X", None) is False


async def test_delete_project_removes_row_and_directory(tmp_db, projects_dir):
    await _init(tmp_db)
    detail = await project_service.create_project(ProjectCreateRequest(name="ToDelete"))
    root = Path(detail.root_path)
    assert root.is_dir()

    ok = await project_service.delete_project(detail.project_id)

    assert ok is True
    assert not root.exists()
    assert await project_service.get_project(detail.project_id) is None


async def test_delete_project_rejects_builtin(tmp_db, projects_dir, monkeypatch):
    await _init(tmp_db)
    monkeypatch.setattr(project_service, "RESOURCES_DIR", projects_dir.parent / "resources")
    project_service.RESOURCES_DIR.mkdir(parents=True, exist_ok=True)
    await project_service.ensure_builtin_project()

    ok = await project_service.delete_project(project_service.BUILTIN_PROJECT_ID)

    assert ok is False
    assert await project_service.get_project(project_service.BUILTIN_PROJECT_ID) is not None


async def test_delete_project_rejects_unknown_id(tmp_db):
    await _init(tmp_db)
    assert await project_service.delete_project("nope") is False


# ---------------------------------------------------------------------------
# ZIP import
# ---------------------------------------------------------------------------


def _zip_bytes(entries: dict[str, bytes]) -> bytes:
    buf = BytesIO()
    with zipfile.ZipFile(buf, "w") as zf:
        for name, data in entries.items():
            zf.writestr(name, data)
    return buf.getvalue()


async def test_create_project_from_zip_strips_single_common_root(tmp_db, projects_dir):
    await _init(tmp_db)
    data = _zip_bytes(
        {
            "my_project/xosc/a.xosc": b"<A/>",
            "my_project/xodr/b.xodr": b"<B/>",
        }
    )

    detail = await project_service.create_project_from_zip(data, "Zipped")

    root = Path(detail.root_path)
    assert (root / "xosc" / "a.xosc").is_file()
    assert not (root / "my_project").exists()


async def test_create_project_from_zip_keeps_layout_without_common_root(
    tmp_db, projects_dir
):
    await _init(tmp_db)
    data = _zip_bytes({"xosc/a.xosc": b"<A/>", "other/b.txt": b"x"})

    detail = await project_service.create_project_from_zip(data, "Multi")

    root = Path(detail.root_path)
    assert (root / "xosc" / "a.xosc").is_file()
    assert (root / "other" / "b.txt").is_file()


async def test_create_project_from_zip_bad_zip_raises_and_cleans_up(
    tmp_db, projects_dir
):
    await _init(tmp_db)
    before = set(projects_dir.iterdir())

    with pytest.raises(ValueError):
        await project_service.create_project_from_zip(b"not a zip", "Bad")

    after = set(projects_dir.iterdir())
    assert after == before, "the half-created project directory must be removed on BadZipFile"


# ---------------------------------------------------------------------------
# file listing / classification
# ---------------------------------------------------------------------------


async def test_list_files_skips_hidden_entries_and_classifies_types(
    tmp_db, projects_dir
):
    await _init(tmp_db)
    detail = await project_service.create_project(ProjectCreateRequest(name="Files"))
    root = Path(detail.root_path)
    (root / "xosc").mkdir()
    (root / "xosc" / "scene.xosc").write_text("<A/>")
    (root / "xodr").mkdir()
    (root / ".git").mkdir()
    (root / ".git" / "config").write_text("x")

    files = await project_service.list_files(detail.project_id)

    paths = {f.path for f in files}
    assert "xosc/scene.xosc" in paths
    assert not any(p.startswith(".git") for p in paths)
    scene = next(f for f in files if f.path == "xosc/scene.xosc")
    assert scene.type == "xosc"


async def test_list_files_returns_none_for_unknown_project(tmp_db):
    await _init(tmp_db)
    assert await project_service.list_files("nope") is None


# ---------------------------------------------------------------------------
# upload / download / delete file — path traversal defense
# ---------------------------------------------------------------------------


async def test_upload_file_writes_within_project_root(tmp_db, projects_dir):
    await _init(tmp_db)
    detail = await project_service.create_project(ProjectCreateRequest(name="Up"))

    ok = await project_service.upload_file(detail.project_id, "xosc/new.xosc", b"<A/>")

    assert ok is True
    assert (Path(detail.root_path) / "xosc" / "new.xosc").read_bytes() == b"<A/>"


async def test_upload_file_rejects_path_traversal(tmp_db, projects_dir):
    await _init(tmp_db)
    detail = await project_service.create_project(ProjectCreateRequest(name="Trav"))
    outside_marker = projects_dir.parent / "escaped.txt"

    ok = await project_service.upload_file(
        detail.project_id, "../../escaped.txt", b"pwned"
    )

    assert ok is False
    assert not outside_marker.exists()


async def test_upload_file_rejects_builtin(tmp_db, projects_dir, monkeypatch):
    await _init(tmp_db)
    monkeypatch.setattr(project_service, "RESOURCES_DIR", projects_dir.parent / "resources")
    project_service.RESOURCES_DIR.mkdir(parents=True, exist_ok=True)
    await project_service.ensure_builtin_project()

    ok = await project_service.upload_file(
        project_service.BUILTIN_PROJECT_ID, "x.xosc", b"data"
    )

    assert ok is False


async def test_get_file_path_rejects_path_traversal(tmp_db, projects_dir):
    await _init(tmp_db)
    detail = await project_service.create_project(ProjectCreateRequest(name="Dl"))
    secret = projects_dir.parent / "secret.txt"
    secret.write_text("top secret")

    resolved = await project_service.get_file_path(detail.project_id, "../secret.txt")

    assert resolved is None


async def test_get_file_path_returns_none_for_missing_file(tmp_db, projects_dir):
    await _init(tmp_db)
    detail = await project_service.create_project(ProjectCreateRequest(name="Dl2"))
    assert await project_service.get_file_path(detail.project_id, "nope.xosc") is None


async def test_get_file_path_resolves_existing_file(tmp_db, projects_dir):
    await _init(tmp_db)
    detail = await project_service.create_project(ProjectCreateRequest(name="Dl3"))
    (Path(detail.root_path) / "a.xosc").write_text("<A/>")

    resolved = await project_service.get_file_path(detail.project_id, "a.xosc")

    assert resolved == (Path(detail.root_path) / "a.xosc").resolve()


async def test_delete_file_rejects_path_traversal(tmp_db, projects_dir):
    await _init(tmp_db)
    detail = await project_service.create_project(ProjectCreateRequest(name="Del"))
    victim = projects_dir.parent / "victim.txt"
    victim.write_text("do not delete me")

    ok = await project_service.delete_file(detail.project_id, "../victim.txt")

    assert ok is False
    assert victim.exists()


async def test_delete_file_removes_existing_file(tmp_db, projects_dir):
    await _init(tmp_db)
    detail = await project_service.create_project(ProjectCreateRequest(name="Del2"))
    target = Path(detail.root_path) / "a.xosc"
    target.write_text("<A/>")

    ok = await project_service.delete_file(detail.project_id, "a.xosc")

    assert ok is True
    assert not target.exists()


async def test_delete_file_returns_false_for_missing_file(tmp_db, projects_dir):
    await _init(tmp_db)
    detail = await project_service.create_project(ProjectCreateRequest(name="Del3"))
    assert await project_service.delete_file(detail.project_id, "nope.xosc") is False


async def test_delete_file_rejects_builtin(tmp_db, projects_dir, monkeypatch):
    await _init(tmp_db)
    monkeypatch.setattr(project_service, "RESOURCES_DIR", projects_dir.parent / "resources")
    project_service.RESOURCES_DIR.mkdir(parents=True, exist_ok=True)
    await project_service.ensure_builtin_project()

    ok = await project_service.delete_file(project_service.BUILTIN_PROJECT_ID, "x.xosc")

    assert ok is False


# ---------------------------------------------------------------------------
# scenarios / params
# ---------------------------------------------------------------------------

_MINIMAL_XOSC = """<?xml version="1.0" encoding="UTF-8"?>
<OpenSCENARIO>
  <ParameterDeclarations>
    <ParameterDeclaration name="Speed" parameterType="double" value="10"/>
  </ParameterDeclarations>
</OpenSCENARIO>
"""


async def test_list_scenarios_returns_none_for_unknown_project(tmp_db):
    await _init(tmp_db)
    assert await project_service.list_scenarios("nope") is None


async def test_list_scenarios_empty_without_xosc_subdir(tmp_db, projects_dir):
    await _init(tmp_db)
    detail = await project_service.create_project(ProjectCreateRequest(name="NoXosc"))
    assert await project_service.list_scenarios(detail.project_id) == []


async def test_list_scenarios_parses_files_in_xosc_subdir(tmp_db, projects_dir):
    await _init(tmp_db)
    detail = await project_service.create_project(ProjectCreateRequest(name="HasXosc"))
    xosc_dir = Path(detail.root_path) / "xosc"
    xosc_dir.mkdir()
    (xosc_dir / "scene.xosc").write_text(_MINIMAL_XOSC, encoding="utf-8")

    scenarios = await project_service.list_scenarios(detail.project_id)

    assert len(scenarios) == 1
    assert scenarios[0].file == "xosc/scene.xosc"


async def test_get_scenario_params_returns_none_for_missing_file(tmp_db, projects_dir):
    await _init(tmp_db)
    detail = await project_service.create_project(ProjectCreateRequest(name="P"))
    assert await project_service.get_scenario_params(detail.project_id, "nope.xosc") is None


async def test_get_scenario_params_extracts_declarations(tmp_db, projects_dir):
    await _init(tmp_db)
    detail = await project_service.create_project(ProjectCreateRequest(name="P2"))
    (Path(detail.root_path) / "scene.xosc").write_text(_MINIMAL_XOSC, encoding="utf-8")

    params = await project_service.get_scenario_params(detail.project_id, "scene.xosc")

    assert len(params) == 1
    assert params[0].name == "Speed"
    assert params[0].value == "10"


# ---------------------------------------------------------------------------
# parameter presets
# ---------------------------------------------------------------------------


async def test_create_preset_then_list(tmp_db, projects_dir):
    await _init(tmp_db)
    detail = await project_service.create_project(ProjectCreateRequest(name="Pr"))

    created = await project_service.create_preset(
        detail.project_id, "xosc/scene.xosc", "fast", {"Speed": "80"}, description="d"
    )
    assert created.preset_id == "fast"

    presets = await project_service.list_presets(detail.project_id, "xosc/scene.xosc")
    assert len(presets) == 1
    assert presets[0].values == {"Speed": "80"}


async def test_create_preset_auto_dedupes_name_collision(tmp_db, projects_dir):
    await _init(tmp_db)
    detail = await project_service.create_project(ProjectCreateRequest(name="Pr2"))

    first = await project_service.create_preset(
        detail.project_id, "scene.xosc", "fast", {"Speed": "80"}
    )
    second = await project_service.create_preset(
        detail.project_id, "scene.xosc", "fast", {"Speed": "90"}
    )

    assert first.preset_id == "fast"
    assert second.preset_id == "fast_2"


async def test_update_preset_rename_conflict_raises(tmp_db, projects_dir):
    await _init(tmp_db)
    detail = await project_service.create_project(ProjectCreateRequest(name="Pr3"))
    await project_service.create_preset(detail.project_id, "s.xosc", "a", {})
    await project_service.create_preset(detail.project_id, "s.xosc", "b", {})

    with pytest.raises(project_service.PresetNameConflictError):
        await project_service.update_preset(detail.project_id, "s.xosc", "a", name="b")


async def test_update_preset_rename_and_value_change(tmp_db, projects_dir):
    await _init(tmp_db)
    detail = await project_service.create_project(ProjectCreateRequest(name="Pr4"))
    await project_service.create_preset(detail.project_id, "s.xosc", "a", {"X": "1"})

    ok = await project_service.update_preset(
        detail.project_id, "s.xosc", "a", name="renamed", values={"X": "2"}
    )

    assert ok is True
    presets = await project_service.list_presets(detail.project_id, "s.xosc")
    assert len(presets) == 1
    assert presets[0].preset_id == "renamed"
    assert presets[0].values == {"X": "2"}


async def test_update_preset_returns_false_for_unknown_id(tmp_db, projects_dir):
    await _init(tmp_db)
    detail = await project_service.create_project(ProjectCreateRequest(name="Pr5"))
    ok = await project_service.update_preset(detail.project_id, "s.xosc", "nope", name="x")
    assert ok is False


async def test_delete_preset_removes_file_when_last_entry_removed(
    tmp_db, projects_dir
):
    await _init(tmp_db)
    detail = await project_service.create_project(ProjectCreateRequest(name="Pr6"))
    await project_service.create_preset(detail.project_id, "s.xosc", "only", {})
    presets_dir = await project_service._get_presets_dir(detail.project_id)
    filepath = project_service._preset_filepath(presets_dir, "s.xosc")
    assert filepath.is_file()

    ok = await project_service.delete_preset(detail.project_id, "s.xosc", "only")

    assert ok is True
    assert not filepath.is_file()


async def test_delete_preset_keeps_file_when_other_entries_remain(
    tmp_db, projects_dir
):
    await _init(tmp_db)
    detail = await project_service.create_project(ProjectCreateRequest(name="Pr7"))
    await project_service.create_preset(detail.project_id, "s.xosc", "keep", {})
    await project_service.create_preset(detail.project_id, "s.xosc", "remove", {})

    ok = await project_service.delete_preset(detail.project_id, "s.xosc", "remove")

    assert ok is True
    presets = await project_service.list_presets(detail.project_id, "s.xosc")
    assert {p.preset_id for p in presets} == {"keep"}


async def test_delete_preset_returns_false_for_unknown_id(tmp_db, projects_dir):
    await _init(tmp_db)
    detail = await project_service.create_project(ProjectCreateRequest(name="Pr8"))
    assert await project_service.delete_preset(detail.project_id, "s.xosc", "nope") is False


async def test_read_presets_file_raises_on_non_mapping_top_level(tmp_db, projects_dir):
    await _init(tmp_db)
    detail = await project_service.create_project(ProjectCreateRequest(name="Pr9"))
    presets_dir = await project_service._get_presets_dir(detail.project_id)
    filepath = project_service._preset_filepath(presets_dir, "s.xosc")
    filepath.parent.mkdir(parents=True, exist_ok=True)
    filepath.write_text("- just\n- a\n- list\n", encoding="utf-8")

    with pytest.raises(project_service.PresetFileCorruptedError):
        await project_service.list_presets(detail.project_id, "s.xosc")


async def test_read_presets_file_raises_on_invalid_yaml(tmp_db, projects_dir):
    await _init(tmp_db)
    detail = await project_service.create_project(ProjectCreateRequest(name="Pr10"))
    presets_dir = await project_service._get_presets_dir(detail.project_id)
    filepath = project_service._preset_filepath(presets_dir, "s.xosc")
    filepath.parent.mkdir(parents=True, exist_ok=True)
    filepath.write_text("key: [unterminated\n", encoding="utf-8")

    with pytest.raises(project_service.PresetFileCorruptedError):
        await project_service.list_presets(detail.project_id, "s.xosc")
