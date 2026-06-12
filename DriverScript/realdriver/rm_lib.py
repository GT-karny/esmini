"""Thin shim: EsminiRMLib has moved to GT_esmini/scripts/rm_lib.py (audit SCR-7).

This file re-exports everything from the canonical location so that frozen
DriverScript examples and tests that do `from realdriver.rm_lib import EsminiRMLib`
continue to work unchanged.
"""

import sys
import pathlib

# Resolve the GT_esmini/scripts/ directory relative to this shim's location:
# DriverScript/realdriver/rm_lib.py -> DriverScript/ -> repo root -> GT_esmini/scripts/
_repo_root = pathlib.Path(__file__).resolve().parents[2]
_gt_scripts = str(_repo_root / "GT_esmini" / "scripts")
if _gt_scripts not in sys.path:
    sys.path.insert(0, _gt_scripts)

from rm_lib import *  # noqa: F401, F403, E402
from rm_lib import EsminiRMLib  # noqa: F401, E402 — explicit re-export for type checkers
