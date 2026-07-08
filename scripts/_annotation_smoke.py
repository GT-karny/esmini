"""Smoke test for the verification annotation backend (registry scan, DB<->JSON
annotation sync, match). Seeds dummy runs under RESULTS_DIR, exercises
annotation_store end-to-end, then cleans up its own seed data + DB rows.

Run from the repo root with the venv:
    DriverScript/.venv/Scripts/python.exe scripts/_annotation_smoke.py
"""
from __future__ import annotations

import asyncio
import json
import shutil
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO_ROOT))

from GT_esmini.web.backend import config  # noqa: E402
from GT_esmini.web.backend.db.database import init_db, get_db  # noqa: E402
from GT_esmini.web.backend.services import annotation_store  # noqa: E402

RESULTS = config.RESULTS_DIR
SEED_TOP = "seed_top"
SEED_BATCH = "seed_batch"
SEED_IDS = [SEED_TOP, "batch/seed_batch/scen_a", "batch/seed_batch/scen_b"]


def _frames(profile):
    out = []
    for i, sp in enumerate(profile):
        out.append({
            "sim_time": round(i * 0.05, 3),
            "ego": {"x": 300.0 + i, "y": -1.5, "z": 0.0, "h": 0.0, "speed": sp,
                    "track": 0, "lane": -1, "offset": 0.0, "s": 300.0 + i},
            "override": {"lateral": False, "longitudinal": False},
            "driver": {"throttle": 0.2, "brake": 0.0, "steer": 0.0,
                       "lateral_error": 0.0, "heading_error": 0.0, "speed_error": 0.0,
                       "lookahead": 4.0, "valid": True},
            "indicator": {"left": False, "right": False},
            "preview": {"dt": 0.1, "valid": True, "points": []},
        })
    return out


def _write_run(run_dir: Path, scenario: str, profile, verdict_overall, results):
    run_dir.mkdir(parents=True, exist_ok=True)
    frames = _frames(profile)
    (run_dir / "telemetry.jsonl").write_text(
        "\n".join(json.dumps(f) for f in frames), encoding="utf-8")
    (run_dir / "meta.json").write_text(json.dumps({
        "scenario": scenario, "controller": "VirtualDriver", "dt": 0.05,
        "frames": len(frames), "sim_duration_s": round((len(frames) - 1) * 0.05, 3),
        "commit": "deadbeef",
    }), encoding="utf-8")
    summary = {"pass": sum(1 for r in results if r["status"] == "pass"),
               "fail": sum(1 for r in results if r["status"] == "fail"),
               "skip": sum(1 for r in results if r["status"] == "skip")}
    (run_dir / "verdict.json").write_text(json.dumps({
        "overall": verdict_overall, "summary": summary, "results": results,
    }), encoding="utf-8")
    return summary


def seed():
    # top-level run
    _write_run(RESULTS / SEED_TOP, "resources/xosc/seed_top.xosc",
               [0.0, 2.0, 6.0, 9.0, 10.0], "pass",
               [{"event": "speed_above", "status": "pass", "reason": "moves"}])
    # batch with two scenarios
    batch_root = RESULTS / SEED_BATCH
    scen = []
    for name, prof, ov, res in [
        ("scen_a", [10.0, 9.0, 5.0, 2.0, 0.0], "fail",
         [{"event": "deceleration_profile_smooth", "status": "fail", "idx": 3, "t": 0.15,
           "reason": "slam stop"}]),
        ("scen_b", [10.0, 9.5, 8.0, 6.0, 4.0], "pass",
         [{"event": "deceleration_profile_smooth", "status": "pass", "reason": "smooth"}]),
    ]:
        rd = batch_root / name
        summary = _write_run(rd, f"resources/xosc/{name}.xosc", prof, ov, res)
        scen.append({"scenario": f"resources/xosc/{name}.xosc", "run_dir": str(rd),
                     "frames": 5, "compare": None,
                     "verdict": {"overall": ov, "summary": summary},
                     "error": None, "policies": []})
    (batch_root / "batch_verdict.json").write_text(json.dumps({
        "name": "seed_batch", "manifest": "seed.yaml", "commit": "deadbeef",
        "scenarios": scen, "summary": {"pass": 1, "fail": 1, "needs-review": 0, "error": 0},
        "overall": "fail",
    }), encoding="utf-8")


async def cleanup():
    db = await get_db()
    try:
        for rid in SEED_IDS:
            await db.execute("DELETE FROM verification_annotations WHERE run_id=?", (rid,))
            await db.execute("DELETE FROM verification_runs WHERE run_id=?", (rid,))
        await db.commit()
    finally:
        await db.close()
    shutil.rmtree(RESULTS / SEED_TOP, ignore_errors=True)
    shutil.rmtree(RESULTS / SEED_BATCH, ignore_errors=True)
    for stem in ("seed_top", "scen_a", "scen_b"):
        shutil.rmtree(config.ANNOTATIONS_DIR / stem, ignore_errors=True)


async def main():
    print(f"RESULTS_DIR  = {RESULTS}")
    print(f"DB_PATH      = {config.DB_PATH}")
    print(f"ANNOTATIONS  = {config.ANNOTATIONS_DIR}")
    await init_db()
    seed()

    # 1) scan registry
    res = await annotation_store.scan_registry(force=True)
    print(f"\n[scan] {res}")
    runs = await annotation_store.list_runs()
    ids = {r["run_id"] for r in runs}
    assert SEED_TOP in ids, f"top-level run missing: {ids}"
    assert "batch/seed_batch/scen_a" in ids, f"batch run missing: {ids}"
    a = next(r for r in runs if r["run_id"] == "batch/seed_batch/scen_a")
    assert a["verdict_overall"] == "fail", a
    print(f"[list] {len(runs)} runs; scen_a verdict={a['verdict_overall']} "
          f"summary={a['verdict_summary']}")

    # 2) set annotation -> DB + sidecar
    rec = await annotation_store.set_annotation(
        "batch/seed_batch/scen_a", "fail", "slam-stop, should anticipate", "tester")
    sidecar = annotation_store._annotation_path("scen_a", "batch/seed_batch/scen_a")
    assert sidecar.is_file(), f"sidecar not written: {sidecar}"
    print(f"[label] set fail; sidecar={sidecar.name} exists={sidecar.is_file()}")

    run = await annotation_store.get_run("batch/seed_batch/scen_a")
    assert run["labeled"] and run["label"] == "fail", run
    print(f"[detail] labeled={run['labeled']} label={run['label']}")

    # 3) DB-loss recovery: drop the row, re-import from sidecar
    db = await get_db()
    try:
        await db.execute("DELETE FROM verification_annotations WHERE run_id=?",
                         ("batch/seed_batch/scen_a",))
        await db.commit()
    finally:
        await db.close()
    n = await annotation_store.import_sidecars()
    restored = await annotation_store.get_annotation("batch/seed_batch/scen_a")
    assert restored and restored["label"] == "fail", restored
    print(f"[recover] re-imported {n}; label restored={restored['label']}")

    # 4) label scen_b pass, then match against scen_a (same... different scenario_stem!)
    await annotation_store.set_annotation("batch/seed_batch/scen_b", "pass", "smooth", "tester")
    # match needs same scenario_stem; scen_a/scen_b differ, so expect empty for cross.
    m = await annotation_store.match_run("batch/seed_batch/scen_a", 3)
    print(f"[match] target={m['target']} matches={len(m['matches'])} "
          f"(0 expected: scen_a vs scen_b are different scenarios)")

    # 5) match within the same scenario: add a sibling of scen_a's stem.
    twin = RESULTS / SEED_BATCH / "scen_a"  # same stem 'scen_a'
    # create a top-level run with the SAME scenario_stem as scen_a and label it
    _write_run(RESULTS / "seed_top", "resources/xosc/scen_a.xosc",
               [10.0, 8.0, 4.0, 1.0, 0.0], "fail",
               [{"event": "deceleration_profile_smooth", "status": "fail", "idx": 3,
                 "reason": "slam"}])
    await annotation_store.scan_registry(force=True)
    await annotation_store.set_annotation("seed_top", "fail", "also a slam stop", "tester")
    m2 = await annotation_store.match_run("batch/seed_batch/scen_a", 3)
    print(f"[match-same-stem] matches={len(m2['matches'])}")
    for mm in m2["matches"]:
        print(f"   - {mm['run_id']} label={mm['label']} score={mm['score']} "
              f"reasons={mm['reasons']}")
    assert any(mm["run_id"] == "seed_top" for mm in m2["matches"]), m2

    print("\nALL CHECKS PASSED")


if __name__ == "__main__":
    try:
        asyncio.run(main())
    finally:
        asyncio.run(cleanup())
        print("[cleanup] removed seed data + DB rows")
