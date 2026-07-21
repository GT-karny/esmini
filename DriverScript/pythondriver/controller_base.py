"""Base types for embedded controllers."""

from __future__ import annotations

from abc import ABC, abstractmethod
from typing import Any, Dict


class EmbeddedControllerBase(ABC):
    """Contract expected by C++ PythonDriverBridge."""

    @abstractmethod
    def init(self, config: Dict[str, Any]) -> None:
        """Initialize controller from scenario/runtime config."""

    @abstractmethod
    def step(self, frame_data: Dict[str, Any]) -> Dict[str, Any]:
        """Calculate one control frame and return control dict."""

    def close(self) -> None:
        """Optional shutdown hook."""
        return None
