"""authoring_common.py — shared helpers for all scenario-authoring generators.

Generators import this module via:
    import sys
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
    from authoring_common import repo_root, git_short_hash, write_meta_yaml, ...

For a generator at resources/scenario_authoring/<subdir>/gen_*.py, parents[1]
resolves to resources/scenario_authoring/ (this module's directory), making
`authoring_common` importable from any generator subdirectory.
"""
from __future__ import annotations

import subprocess
from pathlib import Path
from typing import Any

import yaml
from scenariogeneration import xosc


# ---------------------------------------------------------------------------
# Repository utilities
# ---------------------------------------------------------------------------

def repo_root() -> Path:
    """Return the repository root as an absolute Path.

    Resolution: this file lives at  resources/scenario_authoring/authoring_common.py
    so parents[0] = scenario_authoring/, parents[1] = resources/, parents[2] = repo root.
    """
    return Path(__file__).resolve().parents[2]


def git_short_hash() -> str:
    """Return the short git commit hash of HEAD, or 'unknown' on failure."""
    try:
        result = subprocess.run(
            ["git", "rev-parse", "--short", "HEAD"],
            capture_output=True,
            text=True,
            cwd=str(repo_root()),
            timeout=10,
        )
        if result.returncode == 0:
            return result.stdout.strip()
    except Exception:
        pass
    return "unknown"


# ---------------------------------------------------------------------------
# Metadata I/O
# ---------------------------------------------------------------------------

def write_meta_yaml(path: Path, data: dict[str, Any]) -> None:
    """Write *data* to *path* as YAML (utf-8, insertion-order preserved).

    Sorted keys are intentionally disabled so the caller controls field order
    (makes the file human-readable in the order that matters logically).
    """
    path.write_text(
        yaml.safe_dump(data, allow_unicode=True, sort_keys=False),
        encoding="utf-8",
    )


# ---------------------------------------------------------------------------
# Vehicle catalog helpers
# ---------------------------------------------------------------------------

def make_ego_vehicle() -> xosc.Vehicle:
    """Return the standard GT_esmini Ego vehicle (car_white, model_id=0).

    Dimensions and axle geometry match the hand-authored verification xoscs
    (e.g. resources/xosc/verification/03_traffic_signals/green_no_stop.xosc).
    """
    bb = xosc.BoundingBox(2.0, 5.0, 1.8, 1.4, 0.0, 0.9)
    front_axle = xosc.Axle(0.52, 0.8, 1.68, 2.98, 0.4)
    rear_axle = xosc.Axle(0.0, 0.8, 1.68, 0.0, 0.4)
    # API order: max_speed, max_acceleration, max_deceleration
    # => 69 m/s top speed, 10 m/s² accel, 30 m/s² decel
    veh = xosc.Vehicle(
        "car_white",
        xosc.VehicleCategory.car,
        bb,
        front_axle,
        rear_axle,
        69,   # max_speed
        10,   # max_acceleration
        30,   # max_deceleration
    )
    veh.add_property("model_id", "0")
    return veh


def make_npc_vehicle(model_id: str = "1", name: str = "car_red") -> xosc.Vehicle:
    """Return a standard NPC vehicle with parameterized model_id and name.

    Shares the same bounding-box / axle geometry as the Ego to keep vehicle
    dynamics consistent across all generated scenarios.
    """
    bb = xosc.BoundingBox(2.0, 5.0, 1.8, 1.4, 0.0, 0.9)
    front_axle = xosc.Axle(0.52, 0.8, 1.68, 2.98, 0.4)
    rear_axle = xosc.Axle(0.0, 0.8, 1.68, 0.0, 0.4)
    veh = xosc.Vehicle(
        name,
        xosc.VehicleCategory.car,
        bb,
        front_axle,
        rear_axle,
        69,   # max_speed
        10,   # max_acceleration
        30,   # max_deceleration
    )
    veh.add_property("model_id", model_id)
    return veh


def make_virtual_driver_controller() -> xosc.Controller:
    """Return the VirtualDriverController ObjectController element.

    Matches the exact pattern in
    resources/xosc/verification/03_traffic_signals/green_no_stop.xosc:

        <Controller name="VirtualDriverController">
            <Properties>
                <Property name="esminiController" value="VirtualDriverController"/>
            </Properties>
        </Controller>
    """
    props = xosc.Properties()
    props.add_property("esminiController", "VirtualDriverController")
    return xosc.Controller("VirtualDriverController", props)
