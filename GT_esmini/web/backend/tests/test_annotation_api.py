"""Tests for the verification annotation API (api/annotation.py), feature:F7
audit #3: "各APIに軽量テスト新設" for scenarios/roads/annotation, with the
retry logic in set_annotation specifically named as untested: "run not yet in
the registry -> force a scan once, then retry."

annotation_store's DB-backed registry internals are out of scope here (that is
a much larger surface than this audit item warrants); we monkeypatch the
service functions annotation.py actually calls and verify the API layer's
control flow and status-code translation, matching this suite's established
direct-function-call convention (no TestClient).
"""

from __future__ import annotations

import pytest
from fastapi import HTTPException

from GT_esmini.web.backend.api import annotation
from GT_esmini.web.backend.api.annotation import AnnotationIn, MatchIn
from GT_esmini.web.backend.services import annotation_store

# ---------------------------------------------------------------------------
# list_runs2 / run_detail / get_annotation
# ---------------------------------------------------------------------------


async def test_list_runs2_wraps_result_and_forwards_filters(monkeypatch):
    captured = {}

    async def _fake_list_runs(status=None, batch_id=None, labeled=None, source=None):
        captured.update(
            status=status, batch_id=batch_id, labeled=labeled, source=source
        )
        return [{"run_id": "r1"}]

    monkeypatch.setattr(annotation_store, "list_runs", _fake_list_runs)

    resp = await annotation.list_runs2(
        status="pass", batch_id="b1", labeled=True, source="gui"
    )

    assert resp == {"runs": [{"run_id": "r1"}]}
    assert captured == {
        "status": "pass",
        "batch_id": "b1",
        "labeled": True,
        "source": "gui",
    }


async def test_run_detail_404_when_unknown(monkeypatch):
    async def _none(run_id):
        return None

    monkeypatch.setattr(annotation_store, "get_run", _none)

    with pytest.raises(HTTPException) as exc_info:
        await annotation.run_detail("nope")

    assert exc_info.value.status_code == 404


async def test_run_detail_returns_run_when_found(monkeypatch):
    async def _fake(run_id):
        return {"run_id": run_id, "source": "toplevel"}

    monkeypatch.setattr(annotation_store, "get_run", _fake)

    result = await annotation.run_detail("vd_basic")
    assert result == {"run_id": "vd_basic", "source": "toplevel"}


async def test_get_annotation_404_when_none(monkeypatch):
    async def _none(run_id):
        return None

    monkeypatch.setattr(annotation_store, "get_annotation", _none)

    with pytest.raises(HTTPException) as exc_info:
        await annotation.get_annotation("nope")

    assert exc_info.value.status_code == 404


# ---------------------------------------------------------------------------
# set_annotation: the retry logic (KeyError -> force scan -> retry)
# ---------------------------------------------------------------------------


async def test_set_annotation_happy_path_no_retry_needed(monkeypatch):
    calls = {"set": 0, "scan": 0}

    async def _fake_set(run_id, label, comment, labeler):
        calls["set"] += 1
        return {"run_id": run_id, "label": label}

    async def _fake_scan(force=False):
        calls["scan"] += 1
        return {}

    monkeypatch.setattr(annotation_store, "set_annotation", _fake_set)
    monkeypatch.setattr(annotation_store, "scan_registry", _fake_scan)

    result = await annotation.set_annotation(
        "vd_basic", AnnotationIn(label="pass", comment="ok")
    )

    assert result == {"run_id": "vd_basic", "label": "pass"}
    assert calls["set"] == 1
    assert calls["scan"] == 0, "must not scan when the first attempt already succeeds"


async def test_set_annotation_retries_after_forced_scan_when_run_unknown(monkeypatch):
    """The exact scenario the audit named: a run just written to disk that the
    registry has not picked up yet. First set_annotation() call raises KeyError
    (unknown run); the endpoint must force a rescan, then retry the SAME call,
    and succeed."""
    order: list[str] = []
    attempt = {"n": 0}

    async def _fake_set(run_id, label, comment, labeler):
        attempt["n"] += 1
        order.append(f"set:{attempt['n']}")
        if attempt["n"] == 1:
            raise KeyError(run_id)
        return {"run_id": run_id, "label": label}

    async def _fake_scan(force=False):
        order.append(f"scan:force={force}")
        return {}

    monkeypatch.setattr(annotation_store, "set_annotation", _fake_set)
    monkeypatch.setattr(annotation_store, "scan_registry", _fake_scan)

    result = await annotation.set_annotation(
        "fresh_run", AnnotationIn(label="fail", comment="")
    )

    assert result == {"run_id": "fresh_run", "label": "fail"}
    assert order == [
        "set:1",
        "scan:force=True",
        "set:2",
    ], "must scan (forced) BETWEEN the failed attempt and the retry, not before/instead"


async def test_set_annotation_404_when_still_unknown_after_rescan(monkeypatch):
    """The run genuinely does not exist -- the retry must not loop forever or
    swallow the second failure; it must surface as 404."""

    async def _always_raises(run_id, label, comment, labeler):
        raise KeyError(run_id)

    async def _fake_scan(force=False):
        return {}

    monkeypatch.setattr(annotation_store, "set_annotation", _always_raises)
    monkeypatch.setattr(annotation_store, "scan_registry", _fake_scan)

    with pytest.raises(HTTPException) as exc_info:
        await annotation.set_annotation("truly_missing", AnnotationIn(label="pass"))

    assert exc_info.value.status_code == 404


async def test_set_annotation_400_on_value_error(monkeypatch):
    async def _raise_value_error(run_id, label, comment, labeler):
        raise ValueError("labeler cannot be empty")

    monkeypatch.setattr(annotation_store, "set_annotation", _raise_value_error)

    with pytest.raises(HTTPException) as exc_info:
        await annotation.set_annotation("r1", AnnotationIn(label="pass"))

    assert exc_info.value.status_code == 400


async def test_set_annotation_defaults_labeler_to_local(monkeypatch):
    captured = {}

    async def _fake_set(run_id, label, comment, labeler):
        captured["labeler"] = labeler
        return {"run_id": run_id}

    monkeypatch.setattr(annotation_store, "set_annotation", _fake_set)

    await annotation.set_annotation("r1", AnnotationIn(label="pass", labeler=None))

    assert captured["labeler"] == "local"


# ---------------------------------------------------------------------------
# match
# ---------------------------------------------------------------------------


async def test_match_returns_result_on_success(monkeypatch):
    async def _fake_match(run_id, k):
        return {"matches": [run_id], "k": k}

    monkeypatch.setattr(annotation_store, "match_run", _fake_match)

    result = await annotation.match(MatchIn(run_id="r1", k=3))

    assert result == {"matches": ["r1"], "k": 3}


async def test_match_404_when_run_unknown(monkeypatch):
    async def _raise(run_id, k):
        raise KeyError(run_id)

    monkeypatch.setattr(annotation_store, "match_run", _raise)

    with pytest.raises(HTTPException) as exc_info:
        await annotation.match(MatchIn(run_id="nope"))

    assert exc_info.value.status_code == 404


async def test_match_500_on_unexpected_error(monkeypatch):
    async def _raise(run_id, k):
        raise RuntimeError("similarity backend unavailable")

    monkeypatch.setattr(annotation_store, "match_run", _raise)

    with pytest.raises(HTTPException) as exc_info:
        await annotation.match(MatchIn(run_id="r1"))

    assert exc_info.value.status_code == 500
    assert "similarity backend unavailable" in exc_info.value.detail


# ---------------------------------------------------------------------------
# registry_scan
# ---------------------------------------------------------------------------


async def test_registry_scan_forwards_force_true(monkeypatch):
    # Note: registry_scan's `force` param defaults via FastAPI's Query(...),
    # which only resolves to a plain bool through real request dependency
    # injection -- calling the endpoint directly (this suite's convention)
    # requires passing the value explicitly rather than relying on the
    # decorator's default.
    captured = {}

    async def _fake_scan(force=False):
        captured["force"] = force
        return {"count": 0}

    monkeypatch.setattr(annotation_store, "scan_registry", _fake_scan)

    result = await annotation.registry_scan(force=True)

    assert captured["force"] is True
    assert result == {"count": 0}


async def test_registry_scan_respects_explicit_false(monkeypatch):
    captured = {}

    async def _fake_scan(force=False):
        captured["force"] = force
        return {}

    monkeypatch.setattr(annotation_store, "scan_registry", _fake_scan)

    await annotation.registry_scan(force=False)

    assert captured["force"] is False
