"""Wire (nested) <-> on-disk (flat) shape translation for /api/manual-drive/config.

feature:F7. The endpoint speaks the NESTED pydantic/TS shape
(``sdl2.button_mapping.*``, ``input_network.*``, ``physics_network.*``,
``override_cfg.*``, top-level ``vehicle_params_file``) while the file
ManualDriveConfig.cpp parses is FLAT (``input.*_button``, ``physics.*``,
``override.*``). Two independent failures came out of that mismatch:

  * gap #2 -- PUT wrote the nested block straight into the flat file. C++'s
    scope-free line scan then never saw the remapped button AND overwrote
    ``keyboard.upshift`` (a scancode name) with a button number, because the
    appended block sorts after ``keyboard`` and the scan takes the last match.
  * the follow-up audit -- the first fix translated ONLY ``sdl2``, and GET had
    no translation at all. Every other nested field landed as an orphan
    top-level key C++ never reads, and nothing the user saved ever pre-filled:
    GET returned flat, the frontend reads nested, found undefined, and silently
    fell back to DEFAULT_MANUAL_CONFIG every session.

The second one is why these tests assert BOTH directions and their inverse
property. A one-directional fix looks correct from the writer's side and is
invisible from the reader's.
"""

from __future__ import annotations

import asyncio
import json

import pytest

from GT_esmini.web.backend.api import manual_drive_api as api


def _run(coro):
    return asyncio.run(coro)


FLAT_ON_DISK = {
    "input_type": "sdl2_wheel",
    "input": {
        "device_index": 0,
        "deadzone": 0.05,
        "upshift_button": 4,
        "downshift_button": 5,
        "auto_resume_button": 3,
        "transport_type": "udp",
        "port": 9000,
        "level": "raw",
    },
    "keyboard": {"upshift": "E", "downshift": "Q"},
    "physics": {
        "vehicle_params_file": "custom_vehicle.json",
        "host": "127.0.0.1",
        "cmd_port": 9200,
        "state_port": 9201,
    },
    "override": {"enabled": True, "steering_threshold": 0.33},
}


@pytest.fixture
def sandbox(tmp_path, monkeypatch):
    cfg = tmp_path / "manual_drive.json"
    cfg.write_text(json.dumps(FLAT_ON_DISK, indent=2), encoding="utf-8")
    monkeypatch.setattr(api, "_config_path", lambda: cfg)
    assert api._config_path() == cfg  # hook must take effect
    return cfg


# --- GET: the frontend must be able to pre-fill --------------------------


def test_get_returns_the_nested_shape_the_frontend_reads(sandbox):
    got = _run(api.get_config())
    assert got["sdl2"]["button_mapping"]["upshift"] == 4
    assert got["sdl2"]["button_mapping"]["auto_resume"] == 3
    assert got["sdl2"]["device_index"] == 0
    assert got["input_network"]["port"] == 9000
    assert got["physics_network"]["cmd_port"] == 9200
    assert got["vehicle_params_file"] == "custom_vehicle.json"
    assert got["override_cfg"]["steering_threshold"] == 0.33


def test_get_drops_the_flat_source_keys(sandbox):
    """Reversed from an earlier draft on purpose: keeping BOTH "input"/
    "physics"/"override" AND their translated nested counterparts in the GET
    response is itself a bug, not a safety net. A client that GETs, edits
    ONLY the nested copy, and PUTs the object back would carry the untouched
    flat copy along too -- and _wire_to_flat_shape's "explicit flat wins
    over translated-from-nested" precedence (deliberate: a request that
    states a flat key by hand must not be silently overridden by an
    unrelated nested default) would then make the STALE flat value win over
    the caller's actual edit, silently discarding it. Every key in these
    three blocks is covered by the nested translation (see
    _flat_to_wire_shape's docstring), so dropping them loses nothing."""
    got = _run(api.get_config())
    assert "input" not in got
    assert "physics" not in got
    assert "override" not in got


# --- PUT: what C++ actually parses ---------------------------------------


def test_put_nested_lands_in_the_flat_keys_cpp_reads(sandbox):
    _run(
        api.update_config(
            {
                "sdl2": {"button_mapping": {"upshift": 7, "auto_resume": 9}},
                "input_network": {"port": 9500},
                "physics_network": {"cmd_port": 9300},
                "vehicle_params_file": "other.json",
                "override_cfg": {"steering_threshold": 0.44},
            }
        )
    )
    after = json.loads(sandbox.read_text(encoding="utf-8"))
    assert after["input"]["upshift_button"] == 7
    assert after["input"]["auto_resume_button"] == 9
    assert after["input"]["port"] == 9500
    assert after["physics"]["cmd_port"] == 9300
    assert after["physics"]["vehicle_params_file"] == "other.json"
    assert after["override"]["steering_threshold"] == 0.44


def test_put_does_not_persist_the_nested_blocks(sandbox):
    """A nested block in the file is what corrupted keyboard.upshift (gap #2)."""
    _run(api.update_config({"sdl2": {"button_mapping": {"upshift": 7}}}))
    after = json.loads(sandbox.read_text(encoding="utf-8"))
    for orphan in ("sdl2", "input_network", "physics_network", "override_cfg"):
        assert orphan not in after, f"{orphan} must not reach the flat file"
    assert after["keyboard"]["upshift"] == "E"  # scancode name, not a number


# --- the property the docstring claims -----------------------------------


def test_flat_to_wire_to_flat_is_lossless(sandbox):
    """The two helpers are documented as exact inverses; hold them to it."""
    wire = api._flat_to_wire_shape(FLAT_ON_DISK)
    back = api._wire_to_flat_shape(wire)
    for section in ("input", "physics", "override"):
        assert (
            back[section] == FLAT_ON_DISK[section]
        ), f"{section} did not survive the round trip"
    assert back["keyboard"] == FLAT_ON_DISK["keyboard"]


def test_save_then_reload_prefills_the_same_values(sandbox):
    """End-to-end of the audit's finding: what the user saved must come back."""
    _run(
        api.update_config(
            {
                "sdl2": {"button_mapping": {"auto_resume": 9}},
                "override_cfg": {"steering_threshold": 0.44},
            }
        )
    )
    got = _run(api.get_config())
    assert got["sdl2"]["button_mapping"]["auto_resume"] == 9
    assert got["override_cfg"]["steering_threshold"] == 0.44
