"""Result management: DAT→CSV conversion, metrics calculation, timeseries."""

from __future__ import annotations

import csv
import logging
import sys
from pathlib import Path
from typing import Any

from GT_esmini.web.backend.config import GT_SCRIPTS_DIR, SCRIPTS_DIR
from GT_esmini.web.backend.models.result import ResultFileInfo, ResultMeta

logger = logging.getLogger(__name__)

# Ensure scripts/ is importable for dat.py
if str(SCRIPTS_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPTS_DIR))
if str(GT_SCRIPTS_DIR) not in sys.path:
    sys.path.insert(0, str(GT_SCRIPTS_DIR))


# dat.py (GT_esmini/scripts) understands DAT format major version 4 ONLY, and calls
# exit(-1) -- i.e. raises SystemExit INSIDE THIS SERVER PROCESS -- on any other major
# version. Upstream esmini >= v3.4.0 records version 5. Guard explicitly (P9b known-debt
# mitigation, SCR-1-shaped silent death; full v5 support is a separate task).
_DAT_SUPPORTED_MAJOR = 4


class DatFormatUnsupported(RuntimeError):
    """sim.dat has a DAT major version dat.py cannot read (surfaced as HTTP 409)."""


def _dat_major_version(dat_path: Path) -> int | None:
    """First uint32 of a DAT file = major format version (dat.py reads it the same way)."""
    try:
        with open(dat_path, "rb") as f:
            raw = f.read(4)
        if len(raw) < 4:
            return None
        return int.from_bytes(raw, "little", signed=False)
    except OSError:
        return None


def _ensure_csv(output_dir: Path) -> Path | None:
    """Convert sim.dat to sim.csv if not already done.

    Raises DatFormatUnsupported (explicit, user-visible) when sim.dat is a DAT
    version dat.py cannot read -- NEVER let dat.py's exit(-1) reach the server.
    """
    dat_path = output_dir / "sim.dat"
    csv_path = output_dir / "sim.csv"
    if csv_path.exists():
        return csv_path
    if not dat_path.exists():
        return None

    major = _dat_major_version(dat_path)
    if major is not None and major != _DAT_SUPPORTED_MAJOR:
        raise DatFormatUnsupported(
            f"sim.dat is DAT format v{major}; the bundled dat.py reader supports "
            f"v{_DAT_SUPPORTED_MAJOR} only (esmini >= v3.4.0 records v5). "
            "DAT-derived metrics/timeseries are unavailable for this run."
        )

    try:
        from dat import DATFile
        dat = DATFile(str(dat_path), extended=True)
        dat.save_csv(extended=True, include_file_refs=True)
        dat.close()
        return csv_path if csv_path.exists() else None
    except SystemExit as exc:
        # dat.py exit(-1) escape hatch (header/version paths we did not pre-detect):
        # convert to a caught, logged failure instead of killing the worker.
        logger.error("dat.py aborted (SystemExit %s) converting %s", exc.code, dat_path)
        raise DatFormatUnsupported(
            "sim.dat could not be read by the bundled dat.py reader (it aborted); "
            "the file may be a newer DAT format."
        ) from exc
    except Exception as exc:
        logger.warning("DAT→CSV conversion failed for %s: %s", dat_path, exc, exc_info=True)
        return None


def list_result_files(output_dir: Path) -> list[ResultFileInfo]:
    """List available result files in the output directory."""
    if not output_dir.is_dir():
        return []

    type_map = {
        ".dat": "dat",
        ".csv": "csv",
        ".txt": "log",
        ".jsonl": "jsonl",
        ".xosc": "xosc",
        ".png": "image",
    }

    files: list[ResultFileInfo] = []
    for f in sorted(output_dir.iterdir()):
        if f.is_file():
            ext = f.suffix.lower()
            ftype = type_map.get(ext, "other")
            files.append(ResultFileInfo(name=f.name, size=f.stat().st_size, type=ftype))
    return files


def get_result_meta(job_id: str, scenario_id: str, output_dir: str) -> ResultMeta:
    """Build result metadata for a completed job."""
    out_path = Path(output_dir)
    files = list_result_files(out_path)
    return ResultMeta(job_id=job_id, scenario_id=scenario_id, files=files)


def compute_metrics(output_dir: str) -> dict[str, Any] | None:
    """Compute KPI metrics from sim.csv."""
    out_path = Path(output_dir)
    csv_path = _ensure_csv(out_path)
    if csv_path is None:
        return None

    # For single-run metrics we return basic stats from the CSV
    try:
        rows = _parse_csv(csv_path)
        if not rows:
            return None

        ego_rows = [r for r in rows if r.get("id") == 0 or r.get("name") == "Ego"]
        if not ego_rows:
            ego_rows = rows

        times = [r["time"] for r in ego_rows if "time" in r]
        speeds = [r["speed"] for r in ego_rows if "speed" in r]
        xs = [r["x"] for r in ego_rows if "x" in r]
        ys = [r["y"] for r in ego_rows if "y" in r]

        metrics: dict[str, Any] = {
            "summary": {
                "duration": max(times) - min(times) if times else 0,
                "num_frames": len(ego_rows),
                "avg_speed": sum(speeds) / len(speeds) if speeds else 0,
                "max_speed": max(speeds) if speeds else 0,
                "min_speed": min(speeds) if speeds else 0,
                "total_distance_x": max(xs) - min(xs) if xs else 0,
                "total_distance_y": max(ys) - min(ys) if ys else 0,
            }
        }

        # Add final state
        if ego_rows:
            last = ego_rows[-1]
            metrics["final_state"] = {
                "time": last.get("time"),
                "x": last.get("x"),
                "y": last.get("y"),
                "speed": last.get("speed"),
                "road_id": last.get("roadId"),
                "lane_id": last.get("laneId"),
                "s": last.get("s"),
            }

        return metrics

    except Exception as e:
        return {"error": str(e)}


def get_timeseries(
    output_dir: str,
    fields: list[str] | None = None,
    entity: str = "Ego",
) -> list[dict[str, Any]]:
    """Extract timeseries data as JSON-friendly list."""
    out_path = Path(output_dir)
    csv_path = _ensure_csv(out_path)
    if csv_path is None:
        return []

    rows = _parse_csv(csv_path)
    if not rows:
        return []

    # Filter by entity
    filtered = [r for r in rows if r.get("name") == entity]
    if not filtered:
        # Try by id=0 (ego)
        filtered = [r for r in rows if r.get("id") == 0]
    if not filtered:
        filtered = rows

    # Select fields
    if fields:
        wanted = set(fields)
        return [{k: v for k, v in r.items() if k in wanted} for r in filtered]
    return filtered


def _parse_csv(csv_path: Path) -> list[dict[str, Any]]:
    """Parse esmini CSV (1st line = metadata, 2nd line = headers)."""
    rows: list[dict[str, Any]] = []
    with open(csv_path, "r", encoding="utf-8") as f:
        lines = f.readlines()

    if len(lines) < 2:
        return []

    # First line is metadata (Version, OpenDRIVE, etc.) — skip it
    # Second line is the actual header
    header_line = lines[1]
    headers = [h.strip() for h in header_line.split(",")]

    for line in lines[2:]:
        values = [v.strip() for v in line.split(",")]
        if len(values) != len(headers):
            continue
        parsed: dict[str, Any] = {}
        for k, v in zip(headers, values):
            if not v:
                continue
            try:
                if "." in v:
                    parsed[k] = float(v)
                else:
                    parsed[k] = int(v)
            except ValueError:
                parsed[k] = v
        rows.append(parsed)
    return rows
