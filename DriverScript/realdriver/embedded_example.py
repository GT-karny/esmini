"""
Embedded Python controller example for GT_Sim.

This script demonstrates how to use existing realdriver controllers
inside the embedded Python mode (no UDP, called directly from C++).

Usage in .xosc:
    <Controller name="PythonDriverController">
        <Properties>
            <Property name="esminiController" value="PythonDriverController"/>
            <Property name="PythonScript" value="DriverScript/realdriver/embedded_example.py"/>
            <Property name="PythonClass" value="EmbeddedController"/>
        </Properties>
    </Controller>
"""

import os
import sys


class EmbeddedController:
    """Controller class called by C++ PythonDriverBridge.

    C++ calls init() once at Activate, then step() every simulation frame.
    """

    def init(self, config):
        """Initialize controllers with scenario configuration.

        Args:
            config: dict with keys:
                'xodr_path' (str): Path to OpenDRIVE file
                'dt' (float): Nominal time step (seconds)
                'script_dir' (str): Directory containing this script
                'ego_id' (int): Object ID of the ego vehicle
        """
        self.dt = config.get("dt", 0.01)
        self.ego_id = config.get("ego_id", 0)
        script_dir = config.get("script_dir", "")
        xodr_path = config.get("xodr_path", "")

        # Import realdriver controllers
        from realdriver.lateral_controller import LateralController
        from realdriver.longitudinal_controller import LongitudinalController
        from realdriver.waypoint import Waypoint

        self.lateral = LateralController(ego_id=self.ego_id)
        self.longitudinal = LongitudinalController(ego_id=self.ego_id)
        self.Waypoint = Waypoint

        # Optional: Load RoadManager for road-aware lateral control
        try:
            from realdriver.rm_lib import EsminiRMLib

            bin_dir = os.path.join(script_dir, "..", "bin")
            lib_path = os.path.join(bin_dir, "esminiRMLib.dll")
            if os.path.exists(lib_path) and xodr_path:
                self.rm_lib = EsminiRMLib(lib_path)
                self.rm_lib.Init(xodr_path)
                self.lateral = LateralController(
                    rm_lib=self.rm_lib, ego_id=self.ego_id
                )
                print(f"[EmbeddedController] RoadManager loaded: {xodr_path}")
        except Exception as e:
            print(f"[EmbeddedController] RoadManager not available: {e}")

        self._frame_count = 0
        print(
            f"[EmbeddedController] Initialized (ego_id={self.ego_id}, dt={self.dt})"
        )

    def step(self, frame_data):
        """Process one simulation frame.

        Args:
            frame_data: dict with keys:
                'ground_truth_bytes' (bytes): Serialized OSI GroundTruth protobuf
                'waypoints' (list[dict]): Waypoint data from C++
                'waypoint_index' (int): Current waypoint index
                'lon_profile' (list[dict]): Longitudinal speed profile
                'set_speed' (float): Target speed (m/s)
                'current_speed' (float): Current vehicle speed (m/s)
                'dt' (float): Time step for this frame

        Returns:
            dict with keys:
                'throttle' (float): [0.0, 1.0]
                'brake' (float): [0.0, 1.0]
                'steering' (float): Steering wheel angle (radians)
                'gear' (int): 1=drive, 0=neutral, -1=reverse
                'light_mask' (int): Bitmask for vehicle lights
        """
        from osi3.osi_groundtruth_pb2 import GroundTruth

        gt_bytes = frame_data.get("ground_truth_bytes")
        dt = frame_data.get("dt", self.dt)
        set_speed = frame_data.get("set_speed", 0.0)

        # Parse OSI GroundTruth
        gt = None
        if gt_bytes:
            gt = GroundTruth()
            gt.ParseFromString(gt_bytes)

        # Update waypoints if provided
        waypoints_data = frame_data.get("waypoints", [])
        if waypoints_data:
            wps = [
                self.Waypoint(
                    x=w["x"],
                    y=w["y"],
                    h=w["h"],
                    road_id=w.get("road_id", -1),
                    s=w.get("s", 0.0),
                    lane_id=w.get("lane_id", 0),
                    lane_offset=w.get("lane_offset", 0.0),
                )
                for w in waypoints_data
            ]
            wp_index = frame_data.get("waypoint_index", 0)
            self.lateral.set_calculated_waypoints(wps)
            self.lateral.current_waypoint_index = wp_index

        # Determine target speed from lon profile or set_speed
        lon_profile = frame_data.get("lon_profile", [])
        if lon_profile:
            target_speed = lon_profile[-1]["v_target"]
        else:
            target_speed = set_speed
        self.longitudinal.set_target_speed(target_speed)

        # Calculate control outputs
        steering = 0.0
        throttle = 0.0
        brake = 0.0

        if gt is not None:
            steering = self.lateral.update(gt, dt)
            output = self.longitudinal.update(gt, dt)
            throttle = output.throttle
            brake = output.brake

        self._frame_count += 1

        return {
            "throttle": throttle,
            "brake": brake,
            "steering": steering,
            "gear": 1,
            "light_mask": 0,
            "engine_brake": 0.49,
            "frame_count": self._frame_count,
        }

    def close(self):
        """Cleanup (called on shutdown)."""
        print(f"[EmbeddedController] Closed after {self._frame_count} frames")
