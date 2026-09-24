"""Pytest bootstrap for the web backend unit tests (audit WEB-7/TST-5).

Puts the repository root on sys.path so the backend imports exactly as the
server does: as the PEP 420 namespace package ``GT_esmini.web.backend``.

Run from the repository root:

    DriverScript/.venv/Scripts/python.exe -m pytest GT_esmini/web/backend/tests
"""

from __future__ import annotations

import sys
from pathlib import Path

# tests/ -> backend/ -> web/ -> GT_esmini/ -> <repo root>
REPO_ROOT = Path(__file__).resolve().parents[4]
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))

# The osi3 protobuf bindings are vendored under scripts/, not installed into the
# venv (root CLAUDE.md section 5: "osi3 bindings are vendored under scripts/").
#
# Until 2026-09-25 no conftest said so, and the OSI test modules imported osi3 at
# module scope anyway -- they passed only because some EARLIER test in the same
# process had already put scripts/ on sys.path as a side effect. Running one of
# them on its own failed with ModuleNotFoundError. Make it explicit so the suite
# does not depend on collection order.
SCRIPTS_DIR = REPO_ROOT / "scripts"
if str(SCRIPTS_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPTS_DIR))
