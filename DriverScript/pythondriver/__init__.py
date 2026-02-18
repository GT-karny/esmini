"""Embedded PythonDriverController package.

This package contains controller implementations that are called directly
from GT_Sim via Python embedding (no UDP transport).
"""

from .controller_base import EmbeddedControllerBase
from .scenario_drive_embedded import EmbeddedController, ScenarioDriveEmbedded

__all__ = [
    "EmbeddedControllerBase",
    "EmbeddedController",
    "ScenarioDriveEmbedded",
]

