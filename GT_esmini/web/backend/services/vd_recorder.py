"""Records live VirtualDriver telemetry (and optionally the OSI scene) to files.

During a VirtualDriver GUI run this writes two recordings into the run dir:
  - telemetry.jsonl : per-frame VD telemetry from the always-on VD bridge (48202)
                      — the same stream the live view uses.
  - scene.jsonl     : per-frame OSI GroundTruth as {sim_time, objects,
                      traffic_lights} from the per-job OSI bridge (only when OSI
                      streaming is enabled) — lets the VERIFY replay reconstruct
                      the full scene (other traffic + signal phases).
On stop it writes meta.json in the shape gt_sim_test produces, so the run shows
up in /api/verification/runs and replays/compares/asserts like a CLI recording."""

from __future__ import annotations

import asyncio
import json
import logging
from pathlib import Path

from GT_esmini.web.backend.services.vd_bridge import get_global_vd_bridge
from GT_esmini.web.backend.services.osi_bridge import get_bridge as get_osi_bridge

logger = logging.getLogger(__name__)

# job_id -> {"tasks": [...], "out_dir": Path, "counter": dict}
_recordings: dict[str, dict] = {}

_SCENE_MIN_DT = 0.04  # throttle scene recording to ~25 Hz


async def _telemetry_loop(jsonl_path: Path, counter: dict) -> None:
    bridge = get_global_vd_bridge()
    if bridge is None or not bridge.running:
        logger.warning("VD recorder: no global VD bridge — telemetry not recorded")
        return
    sub_id, queue = bridge.subscribe(f"rec-tel-{jsonl_path.parent.name}")
    try:
        with open(jsonl_path, "w", encoding="utf-8") as f:
            while True:
                raw = await queue.get()
                try:
                    line = raw.decode("utf-8")
                    frame = json.loads(line)
                except (UnicodeDecodeError, json.JSONDecodeError):
                    continue
                f.write(line + "\n")
                f.flush()
                counter["frames"] += 1
                counter["last_sim_time"] = frame.get("sim_time", counter["last_sim_time"])
    except asyncio.CancelledError:
        pass
    except Exception as e:  # pragma: no cover
        logger.warning("VD telemetry recorder error: %s", e)
    finally:
        bridge.unsubscribe(sub_id)


async def _scene_loop(job_id: str, jsonl_path: Path) -> None:
    bridge = get_osi_bridge(job_id)
    if bridge is None or not bridge.running:
        logger.info("VD recorder: no OSI bridge for %s — scene not recorded", job_id)
        return
    # Lazy import to avoid a services<->api import cycle.
    from GT_esmini.web.backend.api.osi_stream import _gt_to_json

    sub_id, queue = bridge.subscribe_gt(f"rec-scene-{job_id}")
    last_t = -1.0
    try:
        with open(jsonl_path, "w", encoding="utf-8") as f:
            while True:
                raw = await queue.get()
                data = _gt_to_json(raw)
                if data is None:
                    continue
                t = data.get("sim_time", 0.0)
                if t - last_t < _SCENE_MIN_DT:
                    continue
                last_t = t
                f.write(json.dumps({
                    "sim_time": t,
                    "objects": data.get("objects", []),
                    "traffic_lights": data.get("traffic_lights", []),
                }, separators=(",", ":")) + "\n")
                f.flush()
    except asyncio.CancelledError:
        pass
    except Exception as e:  # pragma: no cover
        logger.warning("VD scene recorder error for %s: %s", job_id, e)
    finally:
        bridge.unsubscribe(sub_id)


def start(job_id: str, out_dir: Path, record_scene: bool = False) -> None:
    """Begin recording telemetry (and the OSI scene if record_scene) for this job."""
    if job_id in _recordings:
        return
    out_dir.mkdir(parents=True, exist_ok=True)
    counter = {"frames": 0, "last_sim_time": 0.0}
    tasks = [asyncio.create_task(_telemetry_loop(out_dir / "telemetry.jsonl", counter))]
    if record_scene:
        tasks.append(asyncio.create_task(_scene_loop(job_id, out_dir / "scene.jsonl")))
    _recordings[job_id] = {"tasks": tasks, "out_dir": out_dir, "counter": counter}
    logger.info("VD recorder started for %s (scene=%s) -> %s", job_id, record_scene, out_dir)


async def stop(job_id: str, meta_extra: dict | None = None) -> dict | None:
    """Stop recording and write meta.json. Returns the meta dict (or None)."""
    entry = _recordings.pop(job_id, None)
    if entry is None:
        return None
    for task in entry["tasks"]:
        task.cancel()
    for task in entry["tasks"]:
        try:
            await task
        except (asyncio.CancelledError, Exception):
            pass

    counter = entry["counter"]
    out_dir = entry["out_dir"]
    meta = dict(meta_extra or {})
    meta.setdefault("controller", "VirtualDriver")
    meta["frames"] = counter["frames"]
    meta["sim_duration_s"] = round(counter["last_sim_time"], 3)
    (out_dir / "meta.json").write_text(json.dumps(meta, indent=2), encoding="utf-8")
    logger.info("VD recorder stopped for %s (%d frames)", job_id, counter["frames"])
    return meta
