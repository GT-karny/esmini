"""Verification annotation API.

Registry-backed run listing (top-level + batch-nested), human label get/set with a
JSON sidecar mirror, and a rule-based similarity match against past labels. Shares
the /api/verification prefix with verification.py (replay endpoints stay there,
untouched); these endpoints use distinct path segments + {run_id:path} so the
composite batch ids never collide with /runs/{run_id}/telemetry.
"""

from __future__ import annotations

from typing import Literal

from fastapi import APIRouter, HTTPException, Query
from pydantic import BaseModel, Field

from GT_esmini.web.backend.services import annotation_store

router = APIRouter(prefix="/api/verification", tags=["verification"])


# ---------------------------------------------------------------------------
# Models
# ---------------------------------------------------------------------------


class AnnotationIn(BaseModel):
    label: Literal["pass", "fail", "needs-discussion"]
    comment: str = ""
    labeler: str | None = None


class AnnotationOut(BaseModel):
    run_id: str
    label: str
    comment: str = ""
    labeler: str = ""
    scenario: str | None = None
    scenario_stem: str | None = None
    created_at: str | None = None
    updated_at: str | None = None


class RunListItem(BaseModel):
    run_id: str
    source: str
    batch_id: str | None = None
    scenario: str | None = None
    scenario_stem: str | None = None
    project_id: str | None = None
    scenario_file: str | None = None
    frames: int | None = None
    sim_duration_s: float | None = None
    verdict_overall: str | None = None
    verdict_summary: dict | None = None
    has_compare: bool = False
    has_verdict: bool = False
    label: str | None = None
    comment: str | None = None
    labeled: bool = False
    updated_at: str | None = None


class MatchIn(BaseModel):
    run_id: str
    k: int = Field(default=5, ge=1, le=50)


# ---------------------------------------------------------------------------
# Endpoints
# ---------------------------------------------------------------------------


@router.get("/runs2")
async def list_runs2(
    status: str | None = Query(default=None, description="filter by verdict_overall"),
    batch_id: str | None = Query(default=None),
    labeled: bool | None = Query(default=None),
    source: str | None = Query(default=None),
):
    """Registry-backed run list (top-level + batch-nested) joined with annotations.

    Separate from the replay page's GET /runs (which is intentionally untouched)."""
    runs = await annotation_store.list_runs(
        status=status, batch_id=batch_id, labeled=labeled, source=source
    )
    return {"runs": runs}


@router.get("/run-detail/{run_id:path}")
async def run_detail(run_id: str):
    run = await annotation_store.get_run(run_id)
    if run is None:
        raise HTTPException(status_code=404, detail=f"unknown run '{run_id}'")
    return run


@router.get("/annotation/{run_id:path}")
async def get_annotation(run_id: str):
    ann = await annotation_store.get_annotation(run_id)
    if ann is None:
        raise HTTPException(status_code=404, detail=f"no annotation for run '{run_id}'")
    return ann


@router.post("/annotation/{run_id:path}")
async def set_annotation(run_id: str, body: AnnotationIn):
    try:
        rec = await annotation_store.set_annotation(
            run_id, body.label, body.comment, body.labeler or "local"
        )
    except KeyError:
        # Run not yet in the registry — force a scan once, then retry.
        await annotation_store.scan_registry(force=True)
        try:
            rec = await annotation_store.set_annotation(
                run_id, body.label, body.comment, body.labeler or "local"
            )
        except KeyError:
            raise HTTPException(status_code=404, detail=f"unknown run '{run_id}'")
    except ValueError as e:
        raise HTTPException(status_code=400, detail=str(e))
    return rec


@router.post("/match")
async def match(body: MatchIn):
    try:
        return await annotation_store.match_run(body.run_id, body.k)
    except KeyError:
        raise HTTPException(status_code=404, detail=f"unknown run '{body.run_id}'")
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"match failed: {e}")


@router.post("/registry/scan")
async def registry_scan(force: bool = Query(default=True)):
    return await annotation_store.scan_registry(force=force)
