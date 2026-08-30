"""The VirtualDriver policy opt-in table exists twice. Keep the copies equal.

``_VD_POLICY_FLAG`` (web backend, simulation_runner.py) and ``_POLICY_FLAG``
(verification harness, gt_sim_test.py) map the same scenario-facing policy names
onto the same virtual_driver.json flags. They are separate literals because the
two sides share no importable module.

They drifted, and the drift was invisible: lane_change_initiation, overtake and
overtake_opposing_lane were added to the harness only. A scenario could therefore
opt into an AD lane change from a batch manifest but not from the GUI -- the GUI
run just quietly left the feature off, and since the VirtualDriver's response to
"off" is to record the route deviation rather than fail, nothing looked broken.

The harness copy is the reference: it is the one the standing regression gates
exercise. This test reads BOTH tables straight out of the source with ``ast`` and
never imports gt_sim_test -- that module pulls in the osi3 bindings and expects a
built DLL, neither of which this test needs.
"""

from __future__ import annotations

import ast
from pathlib import Path

import pytest

from GT_esmini.web.backend.services.simulation_runner import _VD_POLICY_FLAG

# tests/ -> backend/ -> web/ -> GT_esmini/ -> <repo root>
REPO_ROOT = Path(__file__).resolve().parents[4]
GT_SIM_TEST = REPO_ROOT / "GT_esmini" / "scripts" / "verification" / "gt_sim_test.py"


def _literal_dict_from_source(path: Path, name: str) -> dict[str, str]:
    """Return the module-level dict literal assigned to *name*, without importing."""
    tree = ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
    for node in tree.body:
        if not isinstance(node, ast.Assign):
            continue
        for target in node.targets:
            if isinstance(target, ast.Name) and target.id == name:
                return ast.literal_eval(node.value)
    raise AssertionError(f"{name} not found as a module-level assignment in {path}")


@pytest.fixture(scope="module")
def harness_flags() -> dict[str, str]:
    assert GT_SIM_TEST.is_file(), f"{GT_SIM_TEST} missing from checkout"
    return _literal_dict_from_source(GT_SIM_TEST, "_POLICY_FLAG")


def test_web_backend_covers_every_harness_policy(harness_flags):
    """A policy the harness can enable must be enablable from the GUI too.

    This is the direction that actually bit: features shipped default-OFF, wired
    into batch manifests, and unreachable from the web UI.
    """
    missing = sorted(set(harness_flags) - set(_VD_POLICY_FLAG))
    assert not missing, (
        f"policies usable in gt_sim_test but not from the web backend: {missing}. "
        "Add them to _VD_POLICY_FLAG in simulation_runner.py."
    )


def test_web_backend_invents_no_extra_policies(harness_flags):
    """And the reverse: a GUI-only name would write a flag no gate ever covers."""
    extra = sorted(set(_VD_POLICY_FLAG) - set(harness_flags))
    assert not extra, (
        f"policies in the web backend but unknown to gt_sim_test: {extra}. "
        "Add them to _POLICY_FLAG in gt_sim_test.py, or drop them here."
    )


def test_shared_policy_names_map_to_the_same_flag(harness_flags):
    """Same name, same flag. A name that maps to different flags on the two sides
    would make GUI and gate runs of the same scenario diverge silently."""
    mismatched = {
        name: (_VD_POLICY_FLAG[name], harness_flags[name])
        for name in set(_VD_POLICY_FLAG) & set(harness_flags)
        if _VD_POLICY_FLAG[name] != harness_flags[name]
    }
    assert not mismatched, f"name -> flag disagreements (web, harness): {mismatched}"


def test_lane_change_initiation_is_reachable_from_the_web_backend():
    """Names the specific regression, so the fix cannot be reverted unnoticed.

    Without this entry a generated route scenario runs with
    lane_change_initiation_enabled=false and records
    "left road N in lane X but the route requires one of Y" instead of driving
    the route it was built for.
    """
    assert _VD_POLICY_FLAG.get("lane_change_initiation") == (
        "lane_change_initiation_enabled"
    )
