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
    created_at TEXT NOT NULL DEFAULT (datetime('now'))
);
"""


async def get_db() -> aiosqlite.Connection:
    db = await aiosqlite.connect(str(DB_PATH))
    db.row_factory = aiosqlite.Row
    return db


async def init_db() -> None:
    DB_PATH.parent.mkdir(parents=True, exist_ok=True)
    async with aiosqlite.connect(str(DB_PATH)) as db:
        await db.executescript(_SCHEMA)
        # Mark stale "running" jobs from a previous ungraceful shutdown as failed
        cursor = await db.execute(
            """UPDATE simulations
               SET status = 'failed',
                   error_message = 'Server restarted (previous session did not shut down cleanly)',
                   completed_at = datetime('now'),
                   pid = NULL
               WHERE status = 'running'"""
        )
        if cursor.rowcount and cursor.rowcount > 0:
            import logging
            logging.getLogger(__name__).warning(
                "Cleaned up %d stale 'running' job(s) from previous session",
                cursor.rowcount,
            )
        await db.commit()
