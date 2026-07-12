"""VirtualDriver verification facade for the web backend (compare / baseline / assert).

Thin layer over services/vd_metrics.py — the shared, config-free verification
core (audit WEB-4: this module used to carry a drifted copy of the CLI's
gt_sim_test.py math; the shared module now holds the single, superset
implementation incl. the V2 mid/long and Phase 3 traffic-policy matchers).

This facade adds only the config-dependent piece: generating the Default
baseline by launching GT_Sim the same way simulation_runner does. Dependencies
are all bundled in the backend already: osi3, PyYAML, and bin/GT_Sim.exe
(config.GT_SIM_EXE), so the VERIFY panel works in BOTH the dev tree and the
packaged distribution."""

from __future__ import annotations

import os
import subprocess
from pathlib import Path

from GT_esmini.web.backend.config import GT_SIM_EXE, OSI_GT_PORT, REPO_ROOT
from GT_esmini.web.backend.services.vd_metrics import (
    assert_expectations,
    capture_osi,
    compare,
)

# Public API kept stable for api/verification.py (and any external caller):
# vd_verify.compare / vd_verify.assert_run / vd_verify.generate_baseline.
assert_run = assert_expectations


def generate_baseline(scenario_path: Path, baseline_dir: Path,
                      hz: float = 100.0, idle_timeout: float = 3.0) -> dict:
    """Run the (controller-less) scenario with the Default controller and record
    its OSI GroundTruth to baseline_dir/groundtruth.osi. Blocking — call via
    asyncio.to_thread. Launches GT_Sim like simulation_runner (cwd=REPO_ROOT)."""
    baseline_dir.mkdir(parents=True, exist_ok=True)
    out_osi = baseline_dir / "groundtruth.osi"

    cmd = [
        str(GT_SIM_EXE), "--osc", str(scenario_path),
        "--headless", "--osi", "127.0.0.1", "--hz", str(hz), "--no_realtime",
    ]
    # Inherit env; ensure the exe's own dir is on PATH so its sibling DLLs resolve.
    env = dict(os.environ)
    env["PATH"] = os.pathsep.join([str(Path(GT_SIM_EXE).parent), env.get("PATH", "")])

    with open(baseline_dir / "stdout.txt", "w") as so, open(baseline_dir / "stderr.txt", "w") as se:
        proc = subprocess.Popen(cmd, cwd=str(REPO_ROOT), env=env, stdout=so, stderr=se)
        frames = capture_osi(out_osi, proc, OSI_GT_PORT, idle_timeout)
        proc.wait()

    return {"frames": frames, "osi_file": str(out_osi)}


__all__ = ["assert_run", "compare", "generate_baseline"]
