"""
RealDriver Package

This package provides UDP client and utilities for controlling esmini
RealDriverController from external applications.

New Modular API (recommended for new code):
    - LateralController: Standalone steering control
    - LongitudinalController: Standalone speed control
    - VehicleStateExtractor: Extract vehicle state from OSI GroundTruth

Legacy Combined API (for backward compatibility):
    - ScenarioDriveController: Combined lateral + longitudinal control
"""

from .client import RealDriverClient, LightMode, IndicatorMode
from .osi_receiver import OSIReceiverWrapper
from .udp_common import UdpSender, UdpReceiver, OSIReceiver
from .pid_controller import PIDController
from .rm_lib import EsminiRMLib
from .lkas import LKASController
from .waypoint import Waypoint, WaypointManager, WaypointStatus
from .gt_rm_lib import GTEsminiRMLib
from .simplified_router import SimplifiedRouter

# New modular controllers
from .vehicle_state import VehicleState, VehicleStateExtractor
from .lateral_controller import LateralController, LateralConfig, DEFAULT_LATERAL_CONFIG
from .longitudinal_controller import (
    LongitudinalController, LongitudinalConfig, LongitudinalOutput,
    DEFAULT_LONGITUDINAL_CONFIG
)
from .udp_receivers import WaypointReceiver, TargetSpeedReceiver

# Combined controller (backward compatible)
from .scenario_drive import (
    ScenarioDriveController, LaneChangeState, ControlOutput,
    SteeringConfig, DEFAULT_STEERING_CONFIG  # Aliases for backward compatibility
)

__all__ = [
    # Communication
    'RealDriverClient',
    'LightMode',
    'IndicatorMode',
    'OSIReceiverWrapper',
    'UdpSender',
    'UdpReceiver',
    'OSIReceiver',
    'PIDController',

    # Road Manager
    'EsminiRMLib',
    'GTEsminiRMLib',
    'SimplifiedRouter',

    # Vehicle State (NEW)
    'VehicleState',
    'VehicleStateExtractor',

    # Lateral Control (NEW)
    'LateralController',
    'LateralConfig',
    'DEFAULT_LATERAL_CONFIG',

    # Longitudinal Control (NEW)
    'LongitudinalController',
    'LongitudinalConfig',
    'LongitudinalOutput',
    'DEFAULT_LONGITUDINAL_CONFIG',

    # UDP Receivers (NEW)
    'WaypointReceiver',
    'TargetSpeedReceiver',

    # Waypoint Management
    'Waypoint',
    'WaypointManager',
    'WaypointStatus',

    # Combined Controller (legacy/backward compatible)
    'ScenarioDriveController',
    'LaneChangeState',
    'ControlOutput',
    'SteeringConfig',  # Alias for LateralConfig
    'DEFAULT_STEERING_CONFIG',  # Alias

    # Standalone LKAS
    'LKASController',
]
