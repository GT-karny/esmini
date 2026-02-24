"""Pydantic models for Python script management endpoints."""

from __future__ import annotations

from pydantic import BaseModel


class ScriptInfo(BaseModel):
    path: str
    name: str
    category: str  # pythondriver | examples | realdriver
    classes: list[str] = []
    recommended: bool = False


class ScriptListResponse(BaseModel):
    scripts: list[ScriptInfo]
