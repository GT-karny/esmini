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
