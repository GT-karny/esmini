"""Pydantic models for result-related endpoints."""

from __future__ import annotations

from typing import Any

from pydantic import BaseModel


class ResultFileInfo(BaseModel):
    name: str
    size: int
    type: str  # dat, csv, log, jsonl


class ResultMeta(BaseModel):
    job_id: str
    scenario_id: str
    files: list[ResultFileInfo] = []
    metrics: dict[str, Any] | None = None


class TimeseriesResponse(BaseModel):
    data: list[dict[str, Any]]
    entity: str
    fields: list[str]
