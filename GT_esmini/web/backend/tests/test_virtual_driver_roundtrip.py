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

Audit follow-up (2026-07-28): the five tests below this docstring all drove
the fix through exactly ONE key -- ``vehicle_params_file`` -- and the module
docstring's claim was broader than that: "GET returns the whole on-disk
config, including keys this endpoint does not manage". ``input_port`` /
``input_transport`` (same _EXCLUDED_KEYS family) and the three
``ffb_safety_*`` unattended-run-watchdog keys (a DIFFERENT unmanaged
category -- never added to _NUMBER_KEYS at all, rather than explicitly
excluded) were never exercised. The ``Roundtrip*`` class further down
enumerates every such key MECHANICALLY, straight off the real shipped
config file, rather than naming them by hand -- a key added to either
unmanaged category later is covered automatically, with no test to remember
to update.
"""

from __future__ import annotations

import asyncio
import json
from pathlib import Path

import pytest
from fastapi import HTTPException

from GT_esmini.web.backend.api import virtual_driver_api as vd
from GT_esmini.web.backend import config as backend_config

REAL_VD_CONFIG = Path(backend_config.REPO_ROOT) / "GT_esmini" / "config" / "virtual_driver.json"


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


# ---------------------------------------------------------------------------
# Audit follow-up: every unmanaged key, enumerated mechanically, not by hand.
# ---------------------------------------------------------------------------

_REAL_CONFIG_ON_DISK: dict = json.loads(REAL_VD_CONFIG.read_text(encoding="utf-8"))

# Every non-comment key that is genuinely present in the shipped config but
# not in KNOWN_KEYS -- covers BOTH unmanaged categories in one pass:
# _EXCLUDED_KEYS (runner-owned, explicitly listed: input_port/input_transport/
# vehicle_params_file) AND the ffb_safety_* watchdog keys (a key that simply
# was never added to _NUMBER_KEYS, listed nowhere as a set -- the comment in
# virtual_driver_api.py is the only place they are named at all). A key added
# to config/virtual_driver.json later that is not wired into KNOWN_KEYS shows
# up here automatically, with no test file to remember to touch.
UNMANAGED_KEYS_ON_DISK = sorted(
    k for k in _REAL_CONFIG_ON_DISK if not k.startswith("_") and k not in vd.KNOWN_KEYS
)


@pytest.fixture
def sandbox_full(tmp_path, monkeypatch):
    """Sandbox seeded from the REAL shipped config, not a hand-picked subset
    -- so every key in UNMANAGED_KEYS_ON_DISK is actually present to test
    against, matching how the real endpoint behaves against the real file."""
    cfg = tmp_path / "virtual_driver.json"
    cfg.write_text(json.dumps(_REAL_CONFIG_ON_DISK, indent=2), encoding="utf-8")
    monkeypatch.setattr(vd, "_config_path", lambda: cfg)
    assert vd._config_path() == cfg  # hook must take effect
    return cfg


def test_unmanaged_keys_on_disk_is_not_accidentally_empty():
    """Guards the guard: if this list were ever empty (e.g. a refactor moves
    every one of these into KNOWN_KEYS), the parametrized tests below would
    silently collect zero cases and this whole file would stop meaning
    anything without a single test going red. Pin a floor, not an exact
    count, so legitimately adding a new KNOWN key doesn't need this touched."""
    assert len(UNMANAGED_KEYS_ON_DISK) >= 4  # 3 _EXCLUDED_KEYS + >=1 ffb_safety_*
    assert "vehicle_params_file" in UNMANAGED_KEYS_ON_DISK
    assert "input_port" in UNMANAGED_KEYS_ON_DISK
    assert "input_transport" in UNMANAGED_KEYS_ON_DISK
    assert any(k.startswith("ffb_safety_") for k in UNMANAGED_KEYS_ON_DISK)


@pytest.mark.parametrize("key", UNMANAGED_KEYS_ON_DISK)
def test_unchanged_echo_of_every_unmanaged_key_is_accepted(key, sandbox_full):
    """The exact GET -> edit something ELSE -> PUT sequence that used to 422
    on ANY unmanaged key present in the payload, run once per key instead of
    once for whichever single key a test author happened to pick."""
    current = _run(vd.get_config())
    assert key in current  # sandbox_full must actually carry this key
    current["ffb_target_track_enabled"] = not current["ffb_target_track_enabled"]  # touch something else
    _run(vd.update_config(current))
    after = json.loads(sandbox_full.read_text(encoding="utf-8"))
    assert after[key] == _REAL_CONFIG_ON_DISK[key], f"{key} must survive an unchanged echo"


@pytest.mark.parametrize("key", UNMANAGED_KEYS_ON_DISK)
def test_changing_every_unmanaged_key_is_still_rejected(key, sandbox_full):
    current = _run(vd.get_config())
    original = current[key]
    # A value guaranteed to differ regardless of the key's type (bool/int/
    # float/str): stringify-and-mutate rather than assume a type per key.
    current[key] = f"__definitely_changed__{original}"
    with pytest.raises(HTTPException) as e:
        _run(vd.update_config(current))
    assert e.value.status_code == 422
    after = json.loads(sandbox_full.read_text(encoding="utf-8"))
    assert after[key] == original, f"{key} must not change even though the request 422'd"
