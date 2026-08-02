"""Verification-run registry + human annotation store.

Bridges the on-disk results/ tree (top-level GUI/CLI runs and batch-nested runs)
into the SQLite `verification_runs` table via a pull-based, idempotent scan, and
owns the `verification_annotations` read/write with a JSON sidecar mirror.

Design notes (see GT_esmini/docs/virtualdriver/design/verification_environment.md §6.3):
- Pull, not push: gt_sim_test runs headless in DriverScript/.venv with no backend
  up, so the CLI stays dependency-free; the backend discovers batches by scanning
  RESULTS_DIR for */batch_verdict.json.
- run_id identity is collision-safe: top-level runs keep their bare dir name;
  batch-nested runs use the composite 'batch/<batch_id>/<stem>'. The literal
  'batch/' prefix is a reserved namespace.
- DB is the source of truth; the JSON sidecar under ANNOTATIONS_DIR is a portable
  export and a DB-rebuild source (import_sidecars()).
"""

from __future__ import annotations

import json
import logging
import os
from pathlib import Path
from typing import Any

from GT_esmini.web.backend.config import ANNOTATIONS_DIR, REPO_ROOT, RESULTS_DIR
from GT_esmini.web.backend.db.database import get_db

logger = logging.getLogger(__name__)

RESULTS_ROOT = RESULTS_DIR.resolve()

VALID_LABELS = {"pass", "fail", "needs-discussion"}

# Top-level dirs under RESULTS_DIR that are not runs (and 'batch' is reserved as the
# composite-id namespace so a real top-level dir can never shadow a batch id).
_RESERVED_TOPLEVEL = {
    "baselines",
    "projects",
    "annotations",
    "batch",
    "_temp_scenarios",
    "_temp_roads",
}

# Fingerprint of the last scan, to short-circuit unchanged rescans.
_scan_fingerprint: tuple[float, int] | None = None


# ---------------------------------------------------------------------------
# Path helpers (containment-guarded)
# ---------------------------------------------------------------------------


def _validate_run_id(run_id: str) -> None:
    if not run_id or ".." in run_id.split("/"):
        raise ValueError(f"invalid run id: {run_id!r}")
    for ch in run_id:
        if not (ch.isalnum() or ch in "_-/. "):
            raise ValueError(f"invalid run id: {run_id!r}")


def resolve_run_dir(run_id: str) -> Path:
    """Map a run_id to its on-disk dir, guarded to stay under RESULTS_ROOT.

    'vd_basic'                  -> RESULTS_ROOT/vd_basic
    'batch/<batch_id>/<stem>'   -> RESULTS_ROOT/<batch_id>/<stem>
    """
    _validate_run_id(run_id)
    parts = run_id.split("/")
    if parts[0] == "batch":
        rel = parts[1:]  # [<batch_id>, <stem>]
    else:
        rel = parts
    d = RESULTS_ROOT.joinpath(*rel).resolve()
    if d != RESULTS_ROOT and RESULTS_ROOT not in d.parents:
        raise ValueError(f"run id escapes results root: {run_id!r}")
    return d


def _annotation_path(scenario_stem: str | None, run_id: str) -> Path:
    safe_stem = (scenario_stem or "_unknown").replace("/", "_").replace("\\", "_")
    safe_run = run_id.replace("/", "__").replace("\\", "__")
    return ANNOTATIONS_DIR / safe_stem / f"{safe_run}.json"


# ---------------------------------------------------------------------------
# Disk readers
# ---------------------------------------------------------------------------


def _read_json(path: Path) -> Any:
    if path.is_file():
        try:
            return json.loads(path.read_text(encoding="utf-8"))
        except Exception:
            return None
    return None


def _scenario_fields(meta: dict) -> tuple[str | None, str | None]:
    """(scenario, scenario_stem) from a run's meta.json (gt_sim_test or vd_recorder)."""
    scenario = meta.get("scenario") or meta.get("scenario_path")
    if not scenario:
        return None, None
    stem = Path(str(scenario)).stem
    return str(scenario), stem


def _row_from_dir(
    run_id: str, source: str, batch_id: str | None, run_dir: Path
) -> dict | None:
    """Build a verification_runs row dict from a run dir, or None if not a run."""
    if not (run_dir / "telemetry.jsonl").is_file():
        return None
    # Skip runs that haven't finished yet: vd_recorder writes meta.json only on
    # stop(), so telemetry.jsonl without meta.json means the run is mid-flight.
    # Registering it now would persist scenario=None and strand any label in
    # annotations/_unknown/. Wait until meta.json exists.
    meta = _read_json(run_dir / "meta.json")
    if meta is None:
        return None
    verdict = _read_json(run_dir / "verdict.json")
    scenario, stem = _scenario_fields(meta)
    scenario_file = meta.get("scenario_file")
    if not scenario_file and scenario:
        scenario_file = Path(str(scenario)).name
    return {
        "run_id": run_id,
        "source": source,
        "batch_id": batch_id,
        "scenario": scenario,
        "scenario_stem": stem,
        "project_id": meta.get("project_id"),
        "scenario_file": scenario_file,
        "run_dir": str(run_dir),
        "frames": meta.get("frames"),
        "sim_duration_s": meta.get("sim_duration_s"),
        "commit_hash": meta.get("commit") or meta.get("commit_hash"),
        "verdict_overall": (verdict or {}).get("overall"),
        "verdict_summary": (
            json.dumps((verdict or {}).get("summary"))
            if verdict and verdict.get("summary") is not None
            else None
        ),
        "has_compare": 1 if (run_dir / "compare.json").is_file() else 0,
        "has_verdict": 1 if (run_dir / "verdict.json").is_file() else 0,
    }


def _discover_rows() -> list[dict]:
    """Walk RESULTS_ROOT and build registry rows for top-level + batch-nested runs."""
    rows: list[dict] = []
    if not RESULTS_ROOT.is_dir():
        return rows

    batch_roots: set[Path] = set()
    # Batch runs first, so their roots can be excluded from the top-level scan.
    for bv in RESULTS_ROOT.glob("*/batch_verdict.json"):
        batch_root = bv.parent
        batch_roots.add(batch_root.resolve())
        batch_id = batch_root.name
        agg = _read_json(bv) or {}
        for entry in agg.get("scenarios", []):
            run_dir_str = entry.get("run_dir")
            run_dir = Path(run_dir_str).resolve() if run_dir_str else None
            if run_dir is None or not run_dir.is_dir():
                continue
            stem = run_dir.name
            run_id = f"batch/{batch_id}/{stem}"
            row = _row_from_dir(run_id, "batch", batch_id, run_dir)
            if row is None:
                continue
            # batch_verdict.json carries scenario rel path + verdict even if the per-run
            # meta/verdict are sparse — backfill from the aggregate.
            if not row["scenario"] and entry.get("scenario"):
                row["scenario"] = entry["scenario"]
                row["scenario_stem"] = Path(str(entry["scenario"])).stem
                if not row["scenario_file"]:
                    row["scenario_file"] = Path(str(entry["scenario"])).name
            v = entry.get("verdict") or {}
            if row["verdict_overall"] is None and v.get("overall"):
                row["verdict_overall"] = v["overall"]
            if row["verdict_summary"] is None and v.get("summary") is not None:
                row["verdict_summary"] = json.dumps(v["summary"])
            rows.append(row)

    # Top-level runs (skip reserved dirs and batch roots).
    for d in sorted(RESULTS_ROOT.iterdir()):
        if not d.is_dir() or d.name in _RESERVED_TOPLEVEL:
            continue
        if d.resolve() in batch_roots:
            continue
        row = _row_from_dir(d.name, "toplevel", None, d)
        if row is not None:
            rows.append(row)
    return rows


def _fingerprint() -> tuple[float, int]:
    """Cheap change signal: (max mtime of batch_verdict.json, top-level dir count)."""
    if not RESULTS_ROOT.is_dir():
        return (0.0, 0)
    max_mtime = 0.0
    for bv in RESULTS_ROOT.glob("*/batch_verdict.json"):
        try:
            max_mtime = max(max_mtime, bv.stat().st_mtime)
        except OSError:
            pass
    count = sum(
        1
        for d in RESULTS_ROOT.iterdir()
        if d.is_dir() and d.name not in _RESERVED_TOPLEVEL
    )
    return (max_mtime, count)


# ---------------------------------------------------------------------------
# Registry scan (idempotent upsert)
# ---------------------------------------------------------------------------

_UPSERT_SQL = """
INSERT INTO verification_runs
    (run_id, source, batch_id, scenario, scenario_stem, project_id, scenario_file,
     run_dir, frames, sim_duration_s, commit_hash, verdict_overall, verdict_summary,
     has_compare, has_verdict, discovered_at, updated_at)
VALUES
    (:run_id, :source, :batch_id, :scenario, :scenario_stem, :project_id, :scenario_file,
     :run_dir, :frames, :sim_duration_s, :commit_hash, :verdict_overall, :verdict_summary,
     :has_compare, :has_verdict, datetime('now'), datetime('now'))
ON CONFLICT(run_id) DO UPDATE SET
    source=excluded.source, batch_id=excluded.batch_id, scenario=excluded.scenario,
    scenario_stem=excluded.scenario_stem, project_id=excluded.project_id,
    scenario_file=excluded.scenario_file, run_dir=excluded.run_dir,
    frames=excluded.frames, sim_duration_s=excluded.sim_duration_s,
    commit_hash=excluded.commit_hash, verdict_overall=excluded.verdict_overall,
    verdict_summary=excluded.verdict_summary, has_compare=excluded.has_compare,
    has_verdict=excluded.has_verdict, updated_at=datetime('now')
"""


async def scan_registry(force: bool = False) -> dict:
    """Discover runs on disk and upsert them into verification_runs (idempotent).

    Rows whose dir vanished are NOT deleted, so human labels survive. Returns
    {count, scanned, skipped}. Short-circuits on an unchanged fingerprint unless
    force=True.
    """
    global _scan_fingerprint
    fp = _fingerprint()
    if not force and _scan_fingerprint == fp:
        return {"count": 0, "scanned": 0, "skipped": True}

    rows = _discover_rows()
    db = await get_db()
    try:
        for row in rows:
            await db.execute(_UPSERT_SQL, row)
        await db.commit()
    finally:
        await db.close()

    # Recover any annotations that exist as sidecars but are missing from the DB.
    await import_sidecars()
    # Repair labels stranded with a null scenario_stem now that the run is known.
    await _repair_unknown_annotations()

    _scan_fingerprint = fp
    logger.info("verification registry scan: upserted %d run(s)", len(rows))
    return {"count": len(rows), "scanned": len(rows), "skipped": False}


# ---------------------------------------------------------------------------
# Run list / detail
# ---------------------------------------------------------------------------


def _row_to_item(row) -> dict:
    d = dict(row)
    summary = None
    if d.get("verdict_summary"):
        try:
            summary = json.loads(d["verdict_summary"])
        except Exception:
            summary = None
    return {
        "run_id": d["run_id"],
        "source": d["source"],
        "batch_id": d.get("batch_id"),
        "scenario": d.get("scenario"),
        "scenario_stem": d.get("scenario_stem"),
        "project_id": d.get("project_id"),
        "scenario_file": d.get("scenario_file"),
        "frames": d.get("frames"),
        "sim_duration_s": d.get("sim_duration_s"),
        "verdict_overall": d.get("verdict_overall"),
        "verdict_summary": summary,
        "has_compare": bool(d.get("has_compare")),
        "has_verdict": bool(d.get("has_verdict")),
        "label": d.get("label"),
        "comment": d.get("comment"),
        "labeled": d.get("label") is not None,
        "updated_at": d.get("updated_at"),
    }


async def list_runs(
    status: str | None = None,
    batch_id: str | None = None,
    labeled: bool | None = None,
    source: str | None = None,
) -> list[dict]:
    await scan_registry(force=False)
    sql = """
        SELECT r.*, a.label AS label, a.comment AS comment
        FROM verification_runs r
        LEFT JOIN verification_annotations a ON a.run_id = r.run_id
        WHERE 1=1
    """
    params: list[Any] = []
    if status:
        sql += " AND r.verdict_overall = ?"
        params.append(status)
    if batch_id:
        sql += " AND r.batch_id = ?"
        params.append(batch_id)
    if source:
        sql += " AND r.source = ?"
        params.append(source)
    if labeled is True:
        sql += " AND a.label IS NOT NULL"
    elif labeled is False:
        sql += " AND a.label IS NULL"
    sql += " ORDER BY r.scenario_stem, r.run_id"

    db = await get_db()
    try:
        cur = await db.execute(sql, params)
        rows = await cur.fetchall()
    finally:
        await db.close()
    return [_row_to_item(r) for r in rows]


async def get_run(run_id: str) -> dict | None:
    db = await get_db()
    try:
        cur = await db.execute(
            """SELECT r.*, a.label AS label, a.comment AS comment
               FROM verification_runs r
               LEFT JOIN verification_annotations a ON a.run_id = r.run_id
               WHERE r.run_id = ?""",
            (run_id,),
        )
        row = await cur.fetchone()
    finally:
        await db.close()
    return _row_to_item(row) if row else None


# ---------------------------------------------------------------------------
# Annotation read / write (DB + JSON sidecar)
# ---------------------------------------------------------------------------


async def get_annotation(run_id: str) -> dict | None:
    db = await get_db()
    try:
        cur = await db.execute(
            "SELECT * FROM verification_annotations WHERE run_id = ?", (run_id,)
        )
        row = await cur.fetchone()
    finally:
        await db.close()
    return dict(row) if row else None


async def set_annotation(
    run_id: str, label: str, comment: str = "", labeler: str = ""
) -> dict:
    if label not in VALID_LABELS:
        raise ValueError(
            f"invalid label {label!r}; expected one of {sorted(VALID_LABELS)}"
        )

    db = await get_db()
    try:
        cur = await db.execute(
            "SELECT scenario, scenario_stem FROM verification_runs WHERE run_id = ?",
            (run_id,),
        )
        run_row = await cur.fetchone()
        if run_row is None:
            raise KeyError(run_id)
        scenario = run_row["scenario"]
        scenario_stem = run_row["scenario_stem"]

        # Backfill from meta.json if the registry row was created before meta was
        # written (run labeled the instant it finished). Prevents annotations/_unknown/.
        if not scenario_stem:
            md = _read_json(resolve_run_dir(run_id) / "meta.json") or {}
            scenario2, stem2 = _scenario_fields(md)
            if stem2:
                scenario, scenario_stem = scenario2, stem2
                await db.execute(
                    "UPDATE verification_runs SET scenario=?, scenario_stem=? WHERE run_id=?",
                    (scenario, scenario_stem, run_id),
                )

        await db.execute(
            """INSERT INTO verification_annotations
                   (run_id, label, comment, labeler, scenario_stem, created_at, updated_at)
               VALUES (?, ?, ?, ?, ?, datetime('now'), datetime('now'))
               ON CONFLICT(run_id) DO UPDATE SET
                   label=excluded.label, comment=excluded.comment,
                   labeler=excluded.labeler, scenario_stem=excluded.scenario_stem,
                   updated_at=datetime('now')""",
            (run_id, label, comment, labeler, scenario_stem),
        )
        await db.commit()

        cur = await db.execute(
            "SELECT * FROM verification_annotations WHERE run_id = ?", (run_id,)
        )
        rec = dict(await cur.fetchone())
    finally:
        await db.close()

    rec["scenario"] = scenario
    _write_sidecar(run_id, scenario, scenario_stem, rec)
    return rec


def _write_sidecar(
    run_id: str, scenario: str | None, scenario_stem: str | None, rec: dict
) -> None:
    path = _annotation_path(scenario_stem, run_id)
    path.parent.mkdir(parents=True, exist_ok=True)
    payload = {
        "run_id": run_id,
        "scenario": scenario,
        "scenario_stem": scenario_stem,
        "label": rec["label"],
        "comment": rec.get("comment", ""),
        "labeler": rec.get("labeler", ""),
        "created_at": rec.get("created_at"),
        "updated_at": rec.get("updated_at"),
    }
    tmp = path.with_suffix(".json.tmp")
    tmp.write_text(json.dumps(payload, indent=2, ensure_ascii=False), encoding="utf-8")
    os.replace(tmp, path)


async def import_sidecars() -> int:
    """Re-import JSON sidecars for runs that have no annotation row (DB-loss recovery).

    Only fills gaps; never overwrites an existing DB annotation.
    """
    if not ANNOTATIONS_DIR.is_dir():
        return 0
    db = await get_db()
    imported = 0
    try:
        cur = await db.execute("SELECT run_id FROM verification_annotations")
        have = {r["run_id"] for r in await cur.fetchall()}
        cur = await db.execute("SELECT run_id FROM verification_runs")
        known_runs = {r["run_id"] for r in await cur.fetchall()}
        for f in ANNOTATIONS_DIR.glob("*/*.json"):
            data = _read_json(f)
            if not data:
                continue
            run_id = data.get("run_id")
            if (
                not run_id
                or run_id in have
                or run_id not in known_runs
                or data.get("label") not in VALID_LABELS
            ):
                continue
            await db.execute(
                """INSERT OR IGNORE INTO verification_annotations
                       (run_id, label, comment, labeler, scenario_stem,
                        created_at, updated_at)
                   VALUES (?, ?, ?, ?, ?, ?, ?)""",
                (
                    run_id,
                    data["label"],
                    data.get("comment", ""),
                    data.get("labeler", ""),
                    data.get("scenario_stem"),
                    data.get("created_at"),
                    data.get("updated_at"),
                ),
            )
            imported += 1
            have.add(run_id)
        if imported:
            await db.commit()
    finally:
        await db.close()
    if imported:
        logger.info("recovered %d annotation(s) from JSON sidecars", imported)
    return imported


async def _repair_unknown_annotations() -> int:
    """Fix annotations saved with a null scenario_stem (labeled before the run's
    meta.json existed). Backfills scenario_stem from the now-known run and moves
    the JSON sidecar out of annotations/_unknown/ into the scenario folder.
    """
    db = await get_db()
    fixed = 0
    try:
        cur = await db.execute("""SELECT a.run_id, r.scenario, r.scenario_stem
               FROM verification_annotations a
               JOIN verification_runs r ON r.run_id = a.run_id
               WHERE (a.scenario_stem IS NULL OR a.scenario_stem = '')
                 AND r.scenario_stem IS NOT NULL AND r.scenario_stem != ''""")
        targets = await cur.fetchall()
        for t in targets:
            run_id, scenario, stem = t["run_id"], t["scenario"], t["scenario_stem"]
            await db.execute(
                "UPDATE verification_annotations SET scenario_stem=? WHERE run_id=?",
                (stem, run_id),
            )
            cur2 = await db.execute(
                "SELECT * FROM verification_annotations WHERE run_id=?", (run_id,)
            )
            rec = dict(await cur2.fetchone())
            # Rewrite the sidecar into the correct folder, drop the _unknown one.
            old = _annotation_path(None, run_id)
            _write_sidecar(run_id, scenario, stem, rec)
            try:
                new = _annotation_path(stem, run_id)
                if old.is_file() and old.resolve() != new.resolve():
                    old.unlink()
            except Exception:
                pass
            fixed += 1
        if fixed:
            await db.commit()
    finally:
        await db.close()
    if fixed:
        logger.info("repaired %d annotation(s) with missing scenario_stem", fixed)
    return fixed


# ---------------------------------------------------------------------------
# Similarity match against past labels (rule-based)
# ---------------------------------------------------------------------------


def _load_annotation_match():
    """Import the annotation_match module from GT_esmini/scripts/verification.

    config.py only puts REPO_ROOT/scripts + DriverScript on sys.path, so load the
    verification module by explicit file path (works in dev and packaged layouts)."""
    import importlib.util

    candidates = [
        REPO_ROOT / "GT_esmini" / "scripts" / "verification" / "annotation_match.py",
        REPO_ROOT / "scripts" / "verification" / "annotation_match.py",
    ]
    for path in candidates:
        if path.is_file():
            spec = importlib.util.spec_from_file_location("annotation_match", path)
            if spec and spec.loader:
                mod = importlib.util.module_from_spec(spec)
                spec.loader.exec_module(mod)
                return mod
    raise ModuleNotFoundError(
        "annotation_match.py not found under scripts/verification"
    )


async def match_run(run_id: str, k: int = 5) -> dict:
    """Rank past labeled runs of the same scenario by similarity to this run."""
    annotation_match = _load_annotation_match()

    target = await get_run(run_id)
    if target is None:
        raise KeyError(run_id)
    target_dir = resolve_run_dir(run_id)
    target_feat = annotation_match.extract_features(
        run_dir=target_dir,
        verdict=_read_json(target_dir / "verdict.json"),
        meta=_read_json(target_dir / "meta.json") or {},
    )

    db = await get_db()
    try:
        cur = await db.execute(
            """SELECT r.run_id, r.run_dir, a.label, a.comment
               FROM verification_annotations a
               JOIN verification_runs r ON r.run_id = a.run_id
               WHERE a.scenario_stem IS ? AND a.run_id != ?""",
            (target["scenario_stem"], run_id),
        )
        rows = await cur.fetchall()
    finally:
        await db.close()

    candidates = []
    for r in rows:
        cdir = Path(r["run_dir"])
        feat = annotation_match.extract_features(
            run_dir=cdir,
            verdict=_read_json(cdir / "verdict.json"),
            meta=_read_json(cdir / "meta.json") or {},
        )
        candidates.append((r["run_id"], r["label"], r["comment"] or "", feat))

    matches = annotation_match.rank(target_feat, candidates, k)
    return {
        "target": {"run_id": run_id, "scenario_stem": target["scenario_stem"]},
        "matches": matches,
    }
