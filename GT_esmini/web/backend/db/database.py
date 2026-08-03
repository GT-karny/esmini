"""SQLite database setup and job management."""

from __future__ import annotations

import aiosqlite

from GT_esmini.web.backend.config import DB_PATH

_SCHEMA = """
CREATE TABLE IF NOT EXISTS simulations (
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
    created_at TEXT NOT NULL DEFAULT (datetime('now')),
    project_id TEXT,
    param_overrides TEXT
);

CREATE TABLE IF NOT EXISTS projects (
    project_id TEXT PRIMARY KEY,
    name TEXT NOT NULL,
    description TEXT DEFAULT '',
    is_builtin INTEGER DEFAULT 0,
    root_path TEXT NOT NULL,
    created_at TEXT DEFAULT (datetime('now')),
    updated_at TEXT DEFAULT (datetime('now'))
);

-- VirtualDriver verification: registry of recorded runs (top-level GUI/CLI runs and
-- batch-nested runs), bridging on-disk results/ into the DB so the annotation UI can
-- list, filter, and join human labels. Populated by annotation_store.scan_registry()
-- (pull-based idempotent upsert). See GT_esmini/docs/virtualdriver/design/verification_environment.md.
CREATE TABLE IF NOT EXISTS verification_runs (
    run_id          TEXT PRIMARY KEY,   -- 'vd_basic' | 'batch/<batch_id>/<stem>'
    source          TEXT NOT NULL,      -- 'toplevel' | 'batch' | 'gui'
    batch_id        TEXT,               -- batch out_root dir name (null for top-level)
    scenario        TEXT,               -- meta.scenario (relative xosc path) when known
    scenario_stem   TEXT,               -- annotations/ foldering + match grouping
    project_id      TEXT,               -- meta.project_id (road geometry; nullable)
    scenario_file   TEXT,               -- meta.scenario_file (road geometry; nullable)
    run_dir         TEXT NOT NULL,      -- absolute path on disk
    frames          INTEGER,
    sim_duration_s  REAL,
    commit_hash     TEXT,
    verdict_overall TEXT,               -- 'pass'|'fail'|'needs-review'|'error'|null (auto)
    verdict_summary TEXT,               -- JSON {pass,fail,skip} as text
    has_compare     INTEGER DEFAULT 0,
    has_verdict     INTEGER DEFAULT 0,
    discovered_at   TEXT NOT NULL DEFAULT (datetime('now')),
    updated_at      TEXT NOT NULL DEFAULT (datetime('now'))
);

-- Human annotations (one label per run; latest wins). DB is the source of truth;
-- a JSON sidecar under ANNOTATIONS_DIR mirrors each row for portable export / recovery.
CREATE TABLE IF NOT EXISTS verification_annotations (
    run_id        TEXT PRIMARY KEY
                    REFERENCES verification_runs(run_id) ON DELETE CASCADE,
    label         TEXT NOT NULL,        -- 'pass' | 'fail' | 'needs-discussion' (human)
    comment       TEXT DEFAULT '',
    labeler       TEXT DEFAULT '',
    scenario_stem TEXT,                 -- denormalized for match grouping + file path
    created_at    TEXT NOT NULL DEFAULT (datetime('now')),
    updated_at    TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE INDEX IF NOT EXISTS idx_vruns_batch ON verification_runs(batch_id);
CREATE INDEX IF NOT EXISTS idx_vruns_scen  ON verification_runs(scenario_stem);
CREATE INDEX IF NOT EXISTS idx_vann_scen   ON verification_annotations(scenario_stem);

"""


async def get_db() -> aiosqlite.Connection:
    db = await aiosqlite.connect(str(DB_PATH))
    db.row_factory = aiosqlite.Row
    return db


async def _migrate_simulations_table(db: aiosqlite.Connection) -> None:
    """Add columns to existing simulations table if missing."""
    cursor = await db.execute("PRAGMA table_info(simulations)")
    columns = {row[1] for row in await cursor.fetchall()}
    if "project_id" not in columns:
        await db.execute("ALTER TABLE simulations ADD COLUMN project_id TEXT")
    if "param_overrides" not in columns:
        await db.execute("ALTER TABLE simulations ADD COLUMN param_overrides TEXT")


async def init_db() -> None:
    DB_PATH.parent.mkdir(parents=True, exist_ok=True)
    async with aiosqlite.connect(str(DB_PATH)) as db:
        await db.executescript(_SCHEMA)
        await _migrate_simulations_table(db)
        # Mark stale "running" jobs from a previous ungraceful shutdown as failed
        cursor = await db.execute("""UPDATE simulations
               SET status = 'failed',
                   error_message = 'Server restarted (previous session did not shut down cleanly)',
                   completed_at = datetime('now'),
                   pid = NULL
               WHERE status = 'running'""")
        if cursor.rowcount and cursor.rowcount > 0:
            import logging

            logging.getLogger(__name__).warning(
                "Cleaned up %d stale 'running' job(s) from previous session",
                cursor.rowcount,
            )
        await db.commit()

    # Warm the verification-run registry from disk so the annotation UI has data on
    # startup. Best-effort: a scan failure must not block server boot.
    try:
        from GT_esmini.web.backend.services import annotation_store

        await annotation_store.scan_registry(force=True)
    except Exception:
        import logging

        logging.getLogger(__name__).warning(
            "verification registry warm scan failed at startup", exc_info=True
        )
