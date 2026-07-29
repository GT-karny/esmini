"""Tests for db/database.py: schema init, legacy migration, stale-job cleanup,
and basic CRUD through get_db(). Uses a tmp DB via monkeypatched DB_PATH."""

from __future__ import annotations

import aiosqlite
import pytest

from GT_esmini.web.backend.db import database
from GT_esmini.web.backend.services import annotation_store


@pytest.fixture
def tmp_db(monkeypatch, tmp_path):
    """Redirect the module-level DB_PATH to a per-test file and disable the
    best-effort registry warm scan (it walks the real results dirs)."""
    db_path = tmp_path / "data" / "gt_sim.db"
    monkeypatch.setattr(database, "DB_PATH", db_path)

    async def _noop_scan(force: bool = False):
        return None

    monkeypatch.setattr(annotation_store, "scan_registry", _noop_scan)
    return db_path


async def test_init_db_creates_schema(tmp_db):
    await database.init_db()
    assert tmp_db.is_file()

    async with aiosqlite.connect(str(tmp_db)) as db:
        cur = await db.execute(
            "SELECT name FROM sqlite_master WHERE type='table' ORDER BY name"
        )
        tables = {row[0] for row in await cur.fetchall()}
    assert {
        "simulations",
        "projects",
        "verification_runs",
        "verification_annotations",
    } <= tables


async def test_init_db_is_idempotent(tmp_db):
    await database.init_db()
    await database.init_db()  # must not raise on existing schema


async def test_init_db_migrates_legacy_simulations_table(tmp_db):
    # Pre-create the pre-migration simulations table (no project_id /
    # param_overrides), as older deployments shipped it.
    tmp_db.parent.mkdir(parents=True, exist_ok=True)
    async with aiosqlite.connect(str(tmp_db)) as db:
        await db.execute("""CREATE TABLE simulations (
                   job_id TEXT PRIMARY KEY,
                   scenario_id TEXT NOT NULL,
                   status TEXT NOT NULL DEFAULT 'queued',
                   controller_type TEXT NOT NULL DEFAULT 'default',
                   options_json TEXT DEFAULT '{}',
                   pid INTEGER,
                   output_dir TEXT,
                   exit_code INTEGER,
                   started_at TEXT,
                   completed_at TEXT,
                   error_message TEXT,
                   created_at TEXT NOT NULL DEFAULT (datetime('now'))
               )""")
        await db.execute(
            "INSERT INTO simulations (job_id, scenario_id) VALUES ('j1', 's1')"
        )
        await db.commit()

    await database.init_db()

    async with aiosqlite.connect(str(tmp_db)) as db:
        cur = await db.execute("PRAGMA table_info(simulations)")
        cols = {row[1] for row in await cur.fetchall()}
        assert {"project_id", "param_overrides"} <= cols
        cur = await db.execute("SELECT project_id FROM simulations WHERE job_id='j1'")
        assert (await cur.fetchone())[0] is None  # existing row survived


async def test_init_db_fails_stale_running_jobs(tmp_db):
    await database.init_db()
    async with aiosqlite.connect(str(tmp_db)) as db:
        await db.execute(
            "INSERT INTO simulations (job_id, scenario_id, status, pid) "
            "VALUES ('stale', 's1', 'running', 1234)"
        )
        await db.execute(
            "INSERT INTO simulations (job_id, scenario_id, status) "
            "VALUES ('done', 's1', 'completed')"
        )
        await db.commit()

    await database.init_db()  # simulates server restart

    async with aiosqlite.connect(str(tmp_db)) as db:
        cur = await db.execute(
            "SELECT status, pid, error_message FROM simulations WHERE job_id='stale'"
        )
        status, pid, err = await cur.fetchone()
        assert status == "failed"
        assert pid is None
        assert "restarted" in err
        cur = await db.execute("SELECT status FROM simulations WHERE job_id='done'")
        assert (await cur.fetchone())[0] == "completed"  # untouched


async def test_get_db_row_factory_and_crud(tmp_db):
    await database.init_db()
    db = await database.get_db()
    try:
        await db.execute(
            "INSERT INTO simulations (job_id, scenario_id, status, options_json) "
            "VALUES (?, ?, ?, ?)",
            ("job-42", "scn-1", "queued", '{"hz": 60}'),
        )
        await db.commit()
        cur = await db.execute(
            "SELECT * FROM simulations WHERE job_id = ?", ("job-42",)
        )
        row = await cur.fetchone()
        # get_db must hand back key-addressable rows (aiosqlite.Row)
        assert row["scenario_id"] == "scn-1"
        assert row["status"] == "queued"
        assert row["controller_type"] == "default"  # schema default applied

        await db.execute(
            "UPDATE simulations SET status='running', pid=999 WHERE job_id='job-42'"
        )
        await db.commit()
        cur = await db.execute(
            "SELECT status, pid FROM simulations WHERE job_id='job-42'"
        )
        row = await cur.fetchone()
        assert (row["status"], row["pid"]) == ("running", 999)

        await db.execute("DELETE FROM simulations WHERE job_id='job-42'")
        await db.commit()
        cur = await db.execute("SELECT COUNT(*) AS n FROM simulations")
        assert (await cur.fetchone())["n"] == 0
    finally:
        await db.close()


async def test_verification_runs_upsert_and_index(tmp_db):
    await database.init_db()
    db = await database.get_db()
    try:
        await db.execute(
            "INSERT INTO verification_runs (run_id, source, scenario_stem, run_dir) "
            "VALUES ('vd_basic', 'toplevel', 'vd_basic', '/x/results/vd_basic')"
        )
        await db.execute(
            "INSERT INTO verification_annotations (run_id, label, scenario_stem) "
            "VALUES ('vd_basic', 'pass', 'vd_basic')"
        )
        await db.commit()
        cur = await db.execute("""SELECT r.run_id, a.label FROM verification_runs r
               JOIN verification_annotations a USING (run_id)""")
        row = await cur.fetchone()
        assert (row["run_id"], row["label"]) == ("vd_basic", "pass")
    finally:
        await db.close()
