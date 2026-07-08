"""Project management: CRUD operations, file management, scenario parsing."""

from __future__ import annotations

import logging
import os
import shutil
import tempfile
import uuid
import zipfile
from datetime import datetime, timezone
from pathlib import Path

from GT_esmini.web.backend.config import PROJECTS_DIR, RESOURCES_DIR, get_projects_dir
from GT_esmini.web.backend.db.database import get_db
from GT_esmini.web.backend.models.project import (
    ParameterPreset,
    ProjectCreateRequest,
    ProjectDetail,
    ProjectFile,
    ProjectListItem,
    ScenarioInfo,
    ScenarioParam,
)
from GT_esmini.web.backend.services.xosc_parser import parse_xosc

_logger = logging.getLogger(__name__)

BUILTIN_PROJECT_ID = "builtin"


# ---------------------------------------------------------------------------
# Built-in project initialization
# ---------------------------------------------------------------------------

async def ensure_builtin_project() -> None:
    """Register the resources/ directory as a read-only built-in project."""
    db = await get_db()
    try:
        cursor = await db.execute(
            "SELECT project_id FROM projects WHERE project_id = ?",
            (BUILTIN_PROJECT_ID,),
        )
        if await cursor.fetchone() is not None:
            # Update root_path in case repo moved
            await db.execute(
                "UPDATE projects SET root_path = ? WHERE project_id = ?",
                (str(RESOURCES_DIR), BUILTIN_PROJECT_ID),
            )
            await db.commit()
            return
        await db.execute(
            """INSERT INTO projects (project_id, name, description, is_builtin, root_path)
               VALUES (?, ?, ?, 1, ?)""",
            (BUILTIN_PROJECT_ID, "Built-in Samples", "Default scenarios from resources/", str(RESOURCES_DIR)),
        )
        await db.commit()
    finally:
        await db.close()


# ---------------------------------------------------------------------------
# Filesystem ↔ DB sync
# ---------------------------------------------------------------------------

async def sync_projects() -> None:
    """Scan the active projects directory and sync with DB.

    - New subfolders → register as projects (name = folder name).
    - DB entries whose root_path no longer exists → remove.
    - Built-in project is never touched.
    """
    projects_dir = get_projects_dir()
    if not projects_dir.is_dir():
        return

    db = await get_db()
    try:
        # Get all non-builtin projects from DB
        cursor = await db.execute(
            "SELECT project_id, root_path FROM projects WHERE is_builtin = 0"
        )
        db_projects: dict[str, str] = {
            row["root_path"]: row["project_id"] for row in await cursor.fetchall()
        }

        # Scan filesystem: each direct subdirectory is a potential project
        fs_dirs: set[str] = set()
        for child in projects_dir.iterdir():
            if child.is_dir() and not child.name.startswith("."):
                fs_dirs.add(str(child))

        # Register new folders not yet in DB
        now = datetime.now(timezone.utc).isoformat()
        for dir_path in fs_dirs:
            if dir_path not in db_projects:
                project_id = uuid.uuid4().hex[:12]
                folder_name = Path(dir_path).name
                await db.execute(
                    """INSERT INTO projects
                       (project_id, name, description, is_builtin, root_path, created_at, updated_at)
                       VALUES (?, ?, '', 0, ?, ?, ?)""",
                    (project_id, folder_name, dir_path, now, now),
                )
                _logger.info("Auto-registered project '%s' from %s", folder_name, dir_path)

        # Remove DB entries whose folders no longer exist
        for root_path, project_id in db_projects.items():
            if not Path(root_path).is_dir():
                await db.execute(
                    "DELETE FROM projects WHERE project_id = ?", (project_id,)
                )
                _logger.info(
                    "Removed stale project %s (folder gone: %s)", project_id, root_path
                )

        await db.commit()
    finally:
        await db.close()


# ---------------------------------------------------------------------------
# File counting helpers
# ---------------------------------------------------------------------------

def _count_files(root_path: Path) -> tuple[int, int, int]:
    """Count (scenario_count, road_count, total_file_count) in a project directory."""
    if not root_path.is_dir():
        return 0, 0, 0
    scenarios = 0
    roads = 0
    total = 0
    for p in root_path.rglob("*"):
        if p.is_file():
            total += 1
            suffix = p.suffix.lower()
            if suffix == ".xosc":
                scenarios += 1
            elif suffix == ".xodr":
                roads += 1
    return scenarios, roads, total


def _file_type(path: Path) -> str:
    """Classify a file by extension."""
    suffix = path.suffix.lower()
    if suffix == ".xosc":
        return "xosc"
    if suffix == ".xodr":
        return "xodr"
    if suffix in (".osgb", ".fbx", ".obj", ".gltf", ".glb"):
        return "model"
    if suffix in (".json", ".yaml", ".yml", ".xml"):
        return "config"
    return "other"


# ---------------------------------------------------------------------------
# Project CRUD
# ---------------------------------------------------------------------------

async def list_projects() -> list[ProjectListItem]:
    """List all projects including the built-in one."""
    await sync_projects()
    db = await get_db()
    try:
        cursor = await db.execute(
            "SELECT * FROM projects ORDER BY is_builtin DESC, updated_at DESC"
        )
        rows = await cursor.fetchall()
        results = []
        for row in rows:
            root = Path(row["root_path"])
            sc, rd, total = _count_files(root)
            results.append(ProjectListItem(
                project_id=row["project_id"],
                name=row["name"],
                description=row["description"] or "",
                is_builtin=bool(row["is_builtin"]),
                scenario_count=sc,
                road_count=rd,
                file_count=total,
                created_at=row["created_at"],
                updated_at=row["updated_at"],
            ))
        return results
    finally:
        await db.close()


async def get_project(project_id: str) -> ProjectDetail | None:
    """Get project details by ID."""
    db = await get_db()
    try:
        cursor = await db.execute(
            "SELECT * FROM projects WHERE project_id = ?", (project_id,)
        )
        row = await cursor.fetchone()
        if row is None:
            return None
        root = Path(row["root_path"])
        sc, rd, total = _count_files(root)
        return ProjectDetail(
            project_id=row["project_id"],
            name=row["name"],
            description=row["description"] or "",
            is_builtin=bool(row["is_builtin"]),
            root_path=row["root_path"],
            scenario_count=sc,
            road_count=rd,
            file_count=total,
            created_at=row["created_at"],
            updated_at=row["updated_at"],
        )
    finally:
        await db.close()


async def create_project(req: ProjectCreateRequest) -> ProjectDetail:
    """Create a new empty project."""
    project_id = uuid.uuid4().hex[:12]
    project_dir = get_projects_dir() / project_id
    project_dir.mkdir(parents=True, exist_ok=True)

    now = datetime.now(timezone.utc).isoformat()
    db = await get_db()
    try:
        await db.execute(
            """INSERT INTO projects (project_id, name, description, is_builtin, root_path, created_at, updated_at)
               VALUES (?, ?, ?, 0, ?, ?, ?)""",
            (project_id, req.name, req.description, str(project_dir), now, now),
        )
        await db.commit()
    finally:
        await db.close()

    return ProjectDetail(
        project_id=project_id,
        name=req.name,
        description=req.description,
        is_builtin=False,
        root_path=str(project_dir),
        scenario_count=0,
        road_count=0,
        file_count=0,
        created_at=now,
        updated_at=now,
    )


async def create_project_from_zip(zip_data: bytes, name: str, description: str = "") -> ProjectDetail:
    """Create a project by extracting a ZIP archive."""
    project_id = uuid.uuid4().hex[:12]
    project_dir = get_projects_dir() / project_id
    project_dir.mkdir(parents=True, exist_ok=True)

    import io
    try:
        with zipfile.ZipFile(io.BytesIO(zip_data)) as zf:
            # Check for a single root directory in the ZIP
            top_dirs = {n.split("/")[0] for n in zf.namelist() if "/" in n}
            names = zf.namelist()
            # If all files share a common root directory, strip it
            if len(top_dirs) == 1 and all(n.startswith(f"{list(top_dirs)[0]}/") or n == list(top_dirs)[0] + "/" for n in names):
                prefix = list(top_dirs)[0] + "/"
                for member in zf.infolist():
                    if member.is_dir():
                        continue
                    rel = member.filename[len(prefix):]
                    if not rel:
                        continue
                    target = project_dir / rel
                    target.parent.mkdir(parents=True, exist_ok=True)
                    with zf.open(member) as src, open(target, "wb") as dst:
                        dst.write(src.read())
            else:
                zf.extractall(project_dir)
    except zipfile.BadZipFile:
        shutil.rmtree(project_dir, ignore_errors=True)
        raise ValueError("Invalid ZIP file")

    now = datetime.now(timezone.utc).isoformat()
    db = await get_db()
    try:
        await db.execute(
            """INSERT INTO projects (project_id, name, description, is_builtin, root_path, created_at, updated_at)
               VALUES (?, ?, ?, 0, ?, ?, ?)""",
            (project_id, name, description, str(project_dir), now, now),
        )
        await db.commit()
    finally:
        await db.close()

    sc, rd, total = _count_files(project_dir)
    return ProjectDetail(
        project_id=project_id,
        name=name,
        description=description,
        is_builtin=False,
        root_path=str(project_dir),
        scenario_count=sc,
        road_count=rd,
        file_count=total,
        created_at=now,
        updated_at=now,
    )


async def update_project(project_id: str, name: str | None, description: str | None) -> bool:
    """Update project metadata. Returns False if builtin or not found."""
    proj = await get_project(project_id)
    if proj is None or proj.is_builtin:
        return False

    updates = []
    params = []
    if name is not None:
        updates.append("name = ?")
        params.append(name)
    if description is not None:
        updates.append("description = ?")
        params.append(description)
    if not updates:
        return True

    updates.append("updated_at = datetime('now')")
    params.append(project_id)

    db = await get_db()
    try:
        await db.execute(
            f"UPDATE projects SET {', '.join(updates)} WHERE project_id = ?",
            params,
        )
        await db.commit()
    finally:
        await db.close()
    return True


async def delete_project(project_id: str) -> bool:
    """Delete a project and its files. Returns False if builtin or not found."""
    proj = await get_project(project_id)
    if proj is None or proj.is_builtin:
        return False

    root = Path(proj.root_path)
    if root.is_dir():
        shutil.rmtree(root, ignore_errors=True)

    db = await get_db()
    try:
        await db.execute("DELETE FROM projects WHERE project_id = ?", (project_id,))
        await db.commit()
    finally:
        await db.close()
    return True


# ---------------------------------------------------------------------------
# File management
# ---------------------------------------------------------------------------

async def list_files(project_id: str) -> list[ProjectFile] | None:
    """List all files in a project directory."""
    proj = await get_project(project_id)
    if proj is None:
        return None

    root = Path(proj.root_path)
    if not root.is_dir():
        return []

    files: list[ProjectFile] = []
    for p in sorted(root.rglob("*")):
        rel = p.relative_to(root)
        # Skip hidden files/dirs
        if any(part.startswith(".") for part in rel.parts):
            continue
        if p.is_dir():
            files.append(ProjectFile(
                path=str(rel).replace("\\", "/"),
                name=p.name,
                type="directory",
                size=0,
                modified=datetime.fromtimestamp(p.stat().st_mtime, tz=timezone.utc).isoformat(),
                is_dir=True,
            ))
        else:
            stat = p.stat()
            files.append(ProjectFile(
                path=str(rel).replace("\\", "/"),
                name=p.name,
                type=_file_type(p),
                size=stat.st_size,
                modified=datetime.fromtimestamp(stat.st_mtime, tz=timezone.utc).isoformat(),
            ))
    return files


async def upload_file(project_id: str, file_path: str, data: bytes) -> bool:
    """Upload (or replace) a file in a project. Returns False if builtin."""
    proj = await get_project(project_id)
    if proj is None or proj.is_builtin:
        return False

    root = Path(proj.root_path)
    target = (root / file_path).resolve()
    # Security: ensure target is within project root
    if not str(target).startswith(str(root.resolve())):
        return False

    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(data)

    # Touch updated_at
    db = await get_db()
    try:
        await db.execute(
            "UPDATE projects SET updated_at = datetime('now') WHERE project_id = ?",
            (project_id,),
        )
        await db.commit()
    finally:
        await db.close()
    return True


async def get_file_path(project_id: str, file_path: str) -> Path | None:
    """Resolve a project-relative path to an absolute path for download."""
    proj = await get_project(project_id)
    if proj is None:
        return None

    root = Path(proj.root_path)
    target = (root / file_path).resolve()
    if not str(target).startswith(str(root.resolve())):
        return None
    if not target.is_file():
        return None
    return target


async def delete_file(project_id: str, file_path: str) -> bool:
    """Delete a file from a project. Returns False if builtin."""
    proj = await get_project(project_id)
    if proj is None or proj.is_builtin:
        return False

    root = Path(proj.root_path)
    target = (root / file_path).resolve()
    if not str(target).startswith(str(root.resolve())):
        return False
    if not target.exists():
        return False

    if target.is_dir():
        shutil.rmtree(target, ignore_errors=True)
    else:
        target.unlink()

    db = await get_db()
    try:
        await db.execute(
            "UPDATE projects SET updated_at = datetime('now') WHERE project_id = ?",
            (project_id,),
        )
        await db.commit()
    finally:
        await db.close()
    return True


# ---------------------------------------------------------------------------
# Scenario parsing
# ---------------------------------------------------------------------------

async def list_scenarios(project_id: str) -> list[ScenarioInfo] | None:
    """List and parse all xosc files in a project."""
    proj = await get_project(project_id)
    if proj is None:
        return None

    root = Path(proj.root_path)
    if not root.is_dir():
        return []

    # Always scan xosc/ subdirectory
    scan_dir = root / "xosc"
    if not scan_dir.is_dir():
        return []

    scenarios: list[ScenarioInfo] = []
    for xosc in sorted(scan_dir.glob("*.xosc")):
        rel = str(xosc.relative_to(root)).replace("\\", "/")
        info = _parse_xosc(xosc, rel)
        scenarios.append(info)
    return scenarios


def _parse_xosc(xosc_path: Path, rel_path: str) -> ScenarioInfo:
    """Parse an XOSC file to extract entities, road file, and parameters."""
    result = parse_xosc(xosc_path)
    if result is None:
        return ScenarioInfo(file=rel_path, filename=xosc_path.name)

    entities = [
        {"name": e.name, "model": e.vehicle_or_model, "controller": e.controller}
        for e in result.entities
    ]
    params = [
        ScenarioParam(name=p.name, type=p.type, value=p.value)
        for p in result.params
    ]
    return ScenarioInfo(
        file=rel_path,
        filename=xosc_path.name,
        road_file=result.road_file,
        entities=entities,
        params=params,
        has_controller=result.has_controller,
    )


async def get_scenario_params(project_id: str, scenario_file: str) -> list[ScenarioParam] | None:
    """Get ParameterDeclarations from a specific scenario file."""
    proj = await get_project(project_id)
    if proj is None:
        return None

    root = Path(proj.root_path)
    xosc_path = root / scenario_file
    if not xosc_path.is_file():
        return None

    result = parse_xosc(xosc_path)
    if result is None:
        return []

    return [
        ScenarioParam(name=p.name, type=p.type, value=p.value)
        for p in result.params
    ]


# ---------------------------------------------------------------------------
# Parameter presets  (YAML file-based, one file per scenario)
#
# File layout:
#   <project_root>/presets/<scenario_stem>.yaml
#
# YAML structure (top-level keys are preset names):
#   conservative:
#     description: slow settings
#     values:
#       VehicleSpeed: "30.0"
#   aggressive:
#     values:
#       VehicleSpeed: "80.0"
# ---------------------------------------------------------------------------

import re

import yaml


class PresetFileCorruptedError(Exception):
    """Raised when a preset YAML file fails to parse."""

    def __init__(self, path: Path, original: Exception) -> None:
        super().__init__(f"Failed to parse preset file: {path}: {original}")
        self.path = path
        self.original = original


class PresetNameConflictError(Exception):
    """Raised when renaming a preset would overwrite an existing preset."""

    def __init__(self, name: str) -> None:
        super().__init__(f"Preset name already exists: {name}")
        self.name = name


def _scenario_to_preset_stem(scenario_file: str) -> str:
    """Derive a safe filename stem from a scenario path like ``xosc/foo.xosc``."""
    return Path(scenario_file).stem


async def _get_presets_dir(project_id: str) -> Path | None:
    proj = await get_project(project_id)
    if proj is None:
        return None
    return Path(proj.root_path) / "presets"


def _preset_filepath(presets_dir: Path, scenario_file: str) -> Path:
    stem = _scenario_to_preset_stem(scenario_file)
    return presets_dir / f"{stem}.yaml"


def _read_presets_file(filepath: Path) -> dict:
    """Read the entire YAML file and return the raw dict.

    Raises PresetFileCorruptedError when the YAML cannot be parsed so the
    caller can surface the failure instead of silently overwriting it.
    A missing file returns an empty dict (normal "no presets yet" case).
    """
    if not filepath.is_file():
        return {}
    try:
        text = filepath.read_text(encoding="utf-8")
    except OSError as e:
        raise PresetFileCorruptedError(filepath, e) from e
    try:
        data = yaml.safe_load(text)
    except yaml.YAMLError as e:
        raise PresetFileCorruptedError(filepath, e) from e
    if data is None:
        return {}
    if not isinstance(data, dict):
        raise PresetFileCorruptedError(
            filepath, TypeError(f"Top-level YAML must be a mapping, got {type(data).__name__}"),
        )
    return data


def _dump_yaml(data: dict) -> str:
    """Serialize dict to YAML with stable, edit-friendly options.

    width=inf disables auto line-wrapping (so long JSON-like values keep
    a single line and remain amenable to mechanical search/replace).
    sort_keys=False preserves insertion order (preset tab order stability).
    """
    return yaml.dump(
        data,
        allow_unicode=True,
        default_flow_style=False,
        width=float("inf"),
        sort_keys=False,
    )


def _write_presets_file(filepath: Path, data: dict) -> None:
    """Write the entire presets dict to YAML atomically."""
    filepath.parent.mkdir(parents=True, exist_ok=True)
    payload = _dump_yaml(data)
    payload_bytes = payload.encode("utf-8")
    # Mark the upcoming write as self-originated BEFORE issuing it. Pass the
    # exact bytes so the watcher can baseline the new content's hash and
    # suppress not just the immediate echo but also any delayed event whose
    # contents match what we just wrote.
    try:
        from GT_esmini.web.backend.services.preset_watcher import (
            get_preset_watcher_manager,
        )
        get_preset_watcher_manager().mark_self_write(filepath, payload_bytes)
    except Exception:
        # Watcher is best-effort; never fail a save because of it.
        _logger.debug("mark_self_write failed", exc_info=True)
    tmp_fd, tmp_name = tempfile.mkstemp(
        prefix=f".{filepath.name}.",
        suffix=".tmp",
        dir=str(filepath.parent),
    )
    tmp_path = Path(tmp_name)
    try:
        with os.fdopen(tmp_fd, "wb") as f:
            f.write(payload_bytes)
            f.flush()
            os.fsync(f.fileno())
        os.replace(tmp_path, filepath)
    except Exception:
        try:
            tmp_path.unlink(missing_ok=True)
        except Exception:
            pass
        raise


def _parse_preset(name: str, entry: dict) -> ParameterPreset:
    return ParameterPreset(
        preset_id=name,
        name=name,
        description=entry.get("description", ""),
        values=entry.get("values") or {},
    )


async def list_presets(project_id: str, scenario_file: str) -> list[ParameterPreset]:
    presets_dir = await _get_presets_dir(project_id)
    if presets_dir is None:
        return []
    data = _read_presets_file(_preset_filepath(presets_dir, scenario_file))
    return [
        _parse_preset(name, entry)
        for name, entry in data.items()
        if isinstance(entry, dict)
    ]


async def create_preset(
    project_id: str,
    scenario_file: str,
    name: str,
    values: dict[str, str],
    description: str = "",
) -> ParameterPreset:
    presets_dir = await _get_presets_dir(project_id)
    if presets_dir is None:
        raise ValueError("Project not found")

    filepath = _preset_filepath(presets_dir, scenario_file)
    data = _read_presets_file(filepath)

    # Ensure unique key
    key = name
    if key in data:
        counter = 2
        while f"{name}_{counter}" in data:
            counter += 1
        key = f"{name}_{counter}"

    entry: dict = {}
    if description:
        entry["description"] = description
    entry["values"] = values
    data[key] = entry
    _write_presets_file(filepath, data)

    return ParameterPreset(
        preset_id=key, name=key, description=description, values=values,
    )


async def update_preset(
    project_id: str,
    scenario_file: str,
    preset_id: str,
    name: str | None = None,
    values: dict[str, str] | None = None,
    description: str | None = None,
) -> bool:
    presets_dir = await _get_presets_dir(project_id)
    if presets_dir is None:
        return False
    filepath = _preset_filepath(presets_dir, scenario_file)
    data = _read_presets_file(filepath)
    if preset_id not in data:
        return False

    entry = data[preset_id]
    if not isinstance(entry, dict):
        return False

    if values is not None:
        entry["values"] = values
    if description is not None:
        if description:
            entry["description"] = description
        else:
            entry.pop("description", None)

    # Rename: move to new key (guard against silent overwrite of an
    # existing distinct preset)
    if name is not None and name != preset_id:
        if name in data:
            raise PresetNameConflictError(name)
        del data[preset_id]
        data[name] = entry
    else:
        data[preset_id] = entry

    _write_presets_file(filepath, data)
    return True


async def delete_preset(
    project_id: str, scenario_file: str, preset_id: str,
) -> bool:
    presets_dir = await _get_presets_dir(project_id)
    if presets_dir is None:
        return False
    filepath = _preset_filepath(presets_dir, scenario_file)
    data = _read_presets_file(filepath)
    if preset_id not in data:
        return False
    del data[preset_id]
    if data:
        _write_presets_file(filepath, data)
    elif filepath.is_file():
        filepath.unlink()  # Remove empty file
    return True
