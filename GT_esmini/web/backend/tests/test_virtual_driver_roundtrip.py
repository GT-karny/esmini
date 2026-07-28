"""GET -> edit -> PUT round-trip for /api/virtual-driver/config.

feature:F7 2026-07-28. GET returns the whole on-disk config, including keys
this endpoint does not manage (``vehicle_params_file`` is owned by the per-run
writer). PUT rejected any key outside KNOWN_KEYS, so the obvious client
sequence -- GET, change one field, PUT it back -- died with 422 on a key the
caller had only echoed. Found while verifying the packaged app: selecting the
wheel over the documented REST path failed outright.

The rule these tests pin: an unchanged echo of an unmanaged key is accepted;
introducing or changing an unknown key is still a 422. Silently dropping
unknown keys was explicitly rejected as a fix -- that converts a caller's
mistake into a no-op, which is the "I configured it and nothing happened"
shape this project spent 2026-07-27 digging out of.
"""

from __future__ import annotations

import asyncio
import json

import pytest
from fastapi import HTTPException

from GT_esmini.web.backend.api import virtual_driver_api as vd


def _run(coro):
    return asyncio.run(coro)


@pytest.fixture
def sandbox(tmp_path, monkeypatch):
    cfg = tmp_path / "virtual_driver.json"
    cfg.write_text(
        json.dumps(
            {
                "_comment": "self-documenting text the server owns",
                "input_type": "stub",
                "ffb_target_track_enabled": False,
                "vehicle_params_file": "real_vehicle_params.json",
            },
            indent=2,
        ),
        encoding="utf-8",
    )
    monkeypatch.setattr(vd, "_config_path", lambda: cfg)
    assert vd._config_path() == cfg  # hook must take effect
    return cfg


def test_get_then_put_unchanged_roundtrip_is_accepted(sandbox):
    """The exact sequence that used to 422: read it, change one field, put it back."""
    current = _run(vd.get_config())
    current["input_type"] = "sdl2_wheel"
    _run(vd.update_config(current))
    after = json.loads(sandbox.read_text(encoding="utf-8"))
    assert after["input_type"] == "sdl2_wheel"
    # The unmanaged key it merely echoed must survive untouched.
    assert after["vehicle_params_file"] == "real_vehicle_params.json"


def test_changing_an_unmanaged_key_is_still_rejected(sandbox):
    current = _run(vd.get_config())
    current["vehicle_params_file"] = "something_else.json"
    with pytest.raises(HTTPException) as e:
        _run(vd.update_config(current))
    assert e.value.status_code == 422
    assert "not editable" in str(e.value.detail)


def test_a_typo_key_is_still_rejected(sandbox):
    """The protection that actually matters: a key that does not exist on disk."""
    with pytest.raises(HTTPException) as e:
        _run(vd.update_config({"input_typ": "sdl2_wheel"}))
    assert e.value.status_code == 422
    assert "Unknown key" in str(e.value.detail)


def test_unknown_keys_are_never_silently_dropped(sandbox):
    """A no-op accept would be worse than a 422 -- it looks like it worked."""
    with pytest.raises(HTTPException):
        _run(vd.update_config({"brand_new_key": 1}))
    after = json.loads(sandbox.read_text(encoding="utf-8"))
    assert "brand_new_key" not in after


def test_partial_put_still_works(sandbox):
    """Minimal payloads (what the packaged-app check fell back to) keep working."""
    _run(vd.update_config({"input_type": "sdl2_wheel", "ffb_target_track_enabled": True}))
    after = json.loads(sandbox.read_text(encoding="utf-8"))
    assert after["input_type"] == "sdl2_wheel"
    assert after["ffb_target_track_enabled"] is True
    assert after["vehicle_params_file"] == "real_vehicle_params.json"
    assert after["_comment"].startswith("self-documenting")
