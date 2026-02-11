#!/usr/bin/env python3
"""
NaturalDriver Controller Example

Demonstrates how to use NaturalDriverController as an API, similar to
acc_controller.py / scenario_drive.py style controllers.

Features:
1. IDM-based longitudinal control (throttle/brake)
2. Lane-change execution based on LaneChangeController
3. Optional RoadManager-linked lane detection

Usage:
    # from repo root (recommended with venv)
    .\\venv\\Scripts\\python.exe DriverScript\\examples\\natural_driver_example.py --target_speed 18.0

    # with RoadManager (for lane-aware lane-change decisions)
    .\\venv\\Scripts\\python.exe DriverScript\\examples\\natural_driver_example.py ^
        --xodr_path resources\\xodr\\your_map.xodr

Key Arguments:
    --target_speed [m/s]
        Desired cruising speed target for NaturalDriver.
    --desired_distance [m]
        Minimum standstill gap term (IDM s0). Larger value increases following distance.
    --desired_thw [s]
        Desired time headway term (IDM T). Larger value increases speed-dependent gap.
    --xodr_path [path]
        OpenDRIVE map path. Required for lane-change execution with LaneChangeController.
    --lc_ttc_threshold [s]
        Lane-change safety TTC threshold (smaller = easier to allow lane change).
    --lc_min_gap_front / --lc_min_gap_rear [m]
        Lane-change minimum front/rear gap thresholds.
    --lc_steering_gain
        Lane-change steering amplitude.
    --lc_base_blend [0-1]
        Blend ratio of base lane-centering during lane change. Higher value keeps stronger road-following.
    --lc_wp_dt [s] / --lc_wp_horizon [s] / --lc_wp_lookahead [m]
        Time-based target-lane waypoint generation/tracking parameters for lane-change execution.
"""

import argparse
import json
import math
import os
import socket
import sys
import time
from typing import Dict, List, Tuple


def _default_lib_path() -> str:
    script_dir = os.path.dirname(os.path.abspath(__file__))
    return os.path.normpath(os.path.join(script_dir, "..", "bin", "esminiRMLib.dll"))


def _normalize_angle(angle: float) -> float:
    while angle > math.pi:
        angle -= 2.0 * math.pi
    while angle < -math.pi:
        angle += 2.0 * math.pi
    return angle


def _arg_type_name(arg_type) -> str:
    if arg_type is int:
        return "int"
    if arg_type is float:
        return "float"
    if arg_type is bool:
        return "bool"
    return "str"


def _build_arg_parser() -> Tuple[argparse.ArgumentParser, List[Dict]]:
    parser = argparse.ArgumentParser(description="NaturalDriver Controller Example")
    specs: List[Dict] = []

    def add(name: str, description: str = "", **kwargs) -> None:
        parser.add_argument(name, **kwargs)
        spec = {
            "name": name,
            "type": _arg_type_name(kwargs.get("type", str)),
            "default": kwargs.get("default"),
            "required": bool(kwargs.get("required", False)),
            "help": kwargs.get("help", ""),
            "description": description.strip() or kwargs.get("help", ""),
        }
        choices = kwargs.get("choices")
        if choices is not None:
            spec["choices"] = list(choices)
        if name in ("--xodr_path", "--lib_path"):
            spec["ui"] = "path"
            spec["path_kind"] = "file"
        specs.append(spec)

    add("--ip", type=str, default="127.0.0.1", help="esmini host IP", description="RealDriver UDPの接続先IPアドレスです。")
    add("--port", type=int, default=53995, help="RealDriver base port", description="RealDriver通信用のベースポート番号です。")
    add("--osi_port", type=int, default=48198, help="OSI UDP port", description="OSI GroundTruthを受信するUDPポートです。")
    add("--id", type=int, default=0, help="Ego object ID", description="制御対象車両（Ego）のオブジェクトIDです。")
    add("--target_speed", type=float, default=15.0, help="Desired speed [m/s]", description="目標巡航速度[m/s]。大きいほど速く走行します。")
    add("--desired_distance", type=float, default=20.0, help="IDM desired distance s0 [m]", description="先行車との最低距離[m]（IDM s0）です。")
    add("--desired_thw", type=float, default=2.0, help="IDM desired time headway T [s]", description="目標時間車間[s]（IDM T）です。")
    add("--xodr_path", type=str, default=None, help="OpenDRIVE map path (.xodr)", description="道路形状を使うためのOpenDRIVEファイルパスです。")
    add("--lib_path", type=str, default=None, help="Path to esminiRMLib.dll", description="RoadManager DLL（esminiRMLib.dll）のパスです。")
    add("--lc_ttc_threshold", type=float, default=1.5, help="Lane-change TTC safety threshold [s]", description="車線変更を許可するTTCしきい値[s]です。")
    add("--lc_min_gap_front", type=float, default=8.0, help="Lane-change minimum front gap [m]", description="車線変更先の前方最小ギャップ[m]です。")
    add("--lc_min_gap_rear", type=float, default=6.0, help="Lane-change minimum rear gap [m]", description="車線変更先の後方最小ギャップ[m]です。")
    add("--lc_steering_gain", type=float, default=0.18, help="Lane-change steering gain", description="車線変更時の操舵ゲインです。")
    add("--lc_base_blend", type=float, default=0.15, help="Blend ratio of base lane-centering during lane change [0-1]", description="車線変更中に通常レーン追従を混ぜる比率[0-1]です。")
    add("--lc_wp_dt", type=float, default=0.1, help="Lane-change waypoint time step [s]", description="車線変更ウェイポイントの時間刻み[s]です。")
    add("--lc_wp_horizon", type=float, default=5.0, help="Lane-change waypoint horizon [s]", description="車線変更ウェイポイントの予測時間[s]です。")
    add("--lc_wp_lookahead", type=float, default=10.0, help="Lane-change waypoint lookahead distance [m]", description="車線変更追従の先読み距離[m]です。")
    add("--base_wp_lookahead", type=float, default=12.0, help="Base lane-keeping waypoint lookahead distance [m]", description="通常レーン追従の先読み距離[m]です。")
    add("--base_wp_gain", type=float, default=0.30, help="Base lane-keeping waypoint steering gain", description="通常レーン追従の操舵ゲインです。")
    parser.add_argument("--dump-argspec", action="store_true", help=argparse.SUPPRESS)
    return parser, specs


def main() -> int:
    parser, arg_specs = _build_arg_parser()
    args = parser.parse_args()
    if args.dump_argspec:
        print(json.dumps(arg_specs, ensure_ascii=True))
        return 0

    from realdriver import (
        EsminiRMLib,
        IndicatorMode,
        LaneChangeConfig,
        LaneChangeController,
        NaturalDriverConfig,
        NaturalDriverController,
        OSIReceiverWrapper,
        RealDriverClient,
        VehicleStateExtractor,
    )

    rm_lib = None
    if args.xodr_path:
        lib_path = args.lib_path or _default_lib_path()
        print(f"Initializing RoadManager: {lib_path}")
        rm_lib = EsminiRMLib(lib_path)
        if rm_lib.Init(args.xodr_path) < 0:
            print(f"Failed to initialize RoadManager with map: {args.xodr_path}")
            return 1
    else:
        print("RoadManager disabled (lane-change decision will be limited)")

    print(f"Connecting RealDriver UDP: {args.ip}:{args.port}")
    client = RealDriverClient(args.ip, args.port)

    print(f"Starting OSI receiver on port: {args.osi_port}")
    osi_rx = OSIReceiverWrapper(port=args.osi_port)
    osi_rx.receiver.udp_receiver.sock.settimeout(0.1)

    config = NaturalDriverConfig(
        desired_speed=max(0.0, args.target_speed),
        desired_distance=max(0.0, args.desired_distance),
        desired_thw=max(0.0, args.desired_thw),
    )
    natural = NaturalDriverController(ego_id=args.id, config=config, rm_lib=rm_lib)
    natural.set_desired_speed(args.target_speed)
    state_extractor = VehicleStateExtractor(args.id)

    # Lateral control for road-following (lane centering) when RM is available.
    # Uses waypoint tracking on current lane center (no PID laneOffset feedback).
    lat_pos_handle = -1
    if rm_lib is not None:
        lat_pos_handle = rm_lib.CreatePosition()
        if lat_pos_handle < 0:
            print("Warning: failed to create RM position for lateral control")

    # Lane-change execution controller (requires RM).
    lc_controller = None
    if rm_lib is not None:
        lc_config = LaneChangeConfig(
            ttc_threshold=max(0.1, args.lc_ttc_threshold),
            min_gap_front=max(0.0, args.lc_min_gap_front),
            min_gap_rear=max(0.0, args.lc_min_gap_rear),
            lane_change_duration=config.lane_change_duration,
            steering_gain=max(0.01, args.lc_steering_gain),
            wp_time_step=max(0.05, args.lc_wp_dt),
            wp_horizon_sec=max(1.0, args.lc_wp_horizon),
            wp_lookahead=max(1.0, args.lc_wp_lookahead),
        )
        lc_controller = LaneChangeController(rm_lib=rm_lib, ego_id=args.id, config=lc_config)
        lc_controller.set_base_speed(args.target_speed)

    last_steering_cmd = 0.0
    max_steering_abs = 0.28
    max_steering_rate = 1.4  # steering units per second

    print("NaturalDriver control loop started. Press Ctrl+C to stop.")
    print("-" * 60)

    last_time = time.time()
    frame = 0
    valid_osi_frames = 0
    active_indicator = IndicatorMode.OFF
    indicator_time_left = 0.0
    lc_reject_log_t = 0.0

    try:
        while True:
            try:
                ground_truth = osi_rx.receiver.receive()
            except socket.timeout:
                ground_truth = None

            now = time.time()
            dt = max(0.001, now - last_time)
            last_time = now

            if ground_truth is None:
                valid_osi_frames = 0
                if frame % 100 == 0:
                    print("Waiting for OSI GroundTruth...")
                client.set_controls(0.0, 0.0, 0.0)
                client.set_gear(1)
                client.set_indicators(IndicatorMode.OFF)
                client.send_update()
                frame += 1
                continue

            valid_osi_frames += 1
            out = natural.update(ground_truth, dt)

            # Trigger lane change execution from NaturalDriver request.
            if out.lane_change_request is not None and lc_controller is not None and not lc_controller.is_active:
                req = out.lane_change_request
                safety = lc_controller.check_safety(ground_truth, req.direction)
                if safety.is_safe and lc_controller.trigger_lane_change(req.direction):
                    print(
                        f"[LC_TRIGGER] direction={req.direction}, target_lane={req.target_lane_id}, "
                        f"duration={req.duration:.1f}s"
                    )
                else:
                    # Avoid flooding logs while keeping rejection reason visible.
                    if now - lc_reject_log_t > 1.0:
                        print(f"[LC_SKIP] direction={req.direction}, reason={safety.reason}")
                        lc_reject_log_t = now

            # Base steering: waypoint tracking on current lane center from RM (if available).
            base_steering = 0.0
            ego_state = state_extractor.extract(ground_truth)
            ego_lane_id = None
            if ego_state is not None and rm_lib is not None and lat_pos_handle >= 0:
                res = rm_lib.SetWorldXYZHPosition(
                    lat_pos_handle, ego_state.x, ego_state.y, ego_state.z, ego_state.h
                )
                if res >= 0:
                    res, pos_data = rm_lib.GetPositionData(lat_pos_handle)
                    if res >= 0:
                        ego_lane_id = int(pos_data.laneId)
                        wp_s = float(pos_data.s) + max(2.0, args.base_wp_lookahead)
                        road_len = float(rm_lib.GetRoadLength(pos_data.roadId))
                        if road_len > 0.0:
                            wp_s = min(wp_s, max(0.0, road_len - 0.5))

                        wp_res = rm_lib.SetLanePosition(
                            lat_pos_handle,
                            int(pos_data.roadId),
                            int(pos_data.laneId),
                            0.0,
                            wp_s,
                            True,
                        )
                        if wp_res >= 0:
                            wp_res, wp_pos = rm_lib.GetPositionData(lat_pos_handle)
                            if wp_res >= 0:
                                target_heading = math.atan2(wp_pos.y - ego_state.y, wp_pos.x - ego_state.x)
                                heading_error = _normalize_angle(target_heading - ego_state.h)
                                steer = max(0.01, args.base_wp_gain) * (heading_error / (math.pi / 4.0))
                                base_steering = max(-1.0, min(1.0, steer))

            throttle = out.throttle
            brake = out.brake
            # Steering command in RealDriver/esmini convention.
            # Base lane-centering output needs sign conversion.
            steering_cmd = -base_steering
            if lc_controller is not None:
                lc_out = lc_controller.update(ground_truth, dt)
                if lc_out.is_active:
                    # During lane change, use dedicated lane-change controller outputs.
                    # LaneChangeController direction sign is opposite of this script's base steering path.
                    speed_scale = 1.0
                    if out.current_speed > 14.0:
                        speed_scale = 14.0 / max(out.current_speed, 14.0)
                    # LC lateral control: waypoint tracking only (no base lane-centering blend).
                    steering_cmd = -lc_out.steering * speed_scale
                    # Keep longitudinal control from NaturalDriver (IDM) even during lane change.
                    if lc_out.indicator == 1:
                        client.set_indicators(IndicatorMode.LEFT)
                    elif lc_out.indicator == 2:
                        client.set_indicators(IndicatorMode.RIGHT)
                    else:
                        client.set_indicators(IndicatorMode.OFF)
                else:
                    if lc_out.completed:
                        print("[LC] completed")
                    elif lc_out.aborted:
                        reason = "unknown"
                        if lc_controller.last_safety_check is not None:
                            reason = lc_controller.last_safety_check.reason
                        print(f"[LC] aborted: {reason}")
                    client.set_indicators(IndicatorMode.OFF)
            else:
                # No RM / LaneChangeController: keep indicator behavior from request timing only.
                if out.lane_change_request is not None:
                    indicator_time_left = out.lane_change_request.duration
                    active_indicator = IndicatorMode.LEFT if out.lane_change_request.indicator == 1 else IndicatorMode.RIGHT
                if indicator_time_left > 0.0:
                    indicator_time_left -= dt
                    client.set_indicators(active_indicator)
                else:
                    client.set_indicators(IndicatorMode.OFF)

            # Wait for stable OSI before enabling steering to avoid startup spikes.
            if valid_osi_frames < 8:
                steering_cmd = 0.0

            # Clamp and rate-limit steering to avoid abrupt impulses.
            steering_cmd = max(-max_steering_abs, min(max_steering_abs, steering_cmd))
            max_step = max_steering_rate * dt
            if steering_cmd > last_steering_cmd + max_step:
                steering_cmd = last_steering_cmd + max_step
            elif steering_cmd < last_steering_cmd - max_step:
                steering_cmd = last_steering_cmd - max_step
            last_steering_cmd = steering_cmd

            client.set_controls(throttle, brake, steering_cmd)
            client.set_gear(1)
            client.send_update()

            if frame % 20 == 0:
                lead_str = f"lead_id={out.lead_vehicle_id}" if out.lead_vehicle_id is not None else "lead_id=none"
                lc_lane_str = ""
                if lc_controller is not None and lc_controller.is_active:
                    lc_lane_str = (
                        f", lc_current_lane={ego_lane_id}, "
                        f"lc_target_lane={lc_controller.target_lane_id}"
                    )
                print(
                    f"state={out.state.name}, speed={out.current_speed:.2f}m/s, "
                    f"acc={out.acceleration:.2f}, thr={throttle:.2f}, brk={brake:.2f}, "
                    f"steer={steering_cmd:.3f}, ego_lane={ego_lane_id}, {lead_str}{lc_lane_str}"
                )

            frame += 1

    except KeyboardInterrupt:
        print("\nStopping...")
    finally:
        print("Closing connections...")
        osi_rx.close()
        client.close()
        if rm_lib is not None:
            rm_lib.Close()
        print("Done.")

    return 0


if __name__ == "__main__":
    sys.exit(main())
