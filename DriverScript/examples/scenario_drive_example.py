#!/usr/bin/env python3
"""
ScenarioDrive Controller Example

Demonstrates using the ScenarioDriveController for autonomous waypoint
following with speed control, similar to esmini's ControllerFollowRoute.

Usage modes:
1. User-specified waypoints: Provide explicit waypoints via set_waypoints()
2. Auto-calculated route: Set a target position and calculate route via set_target()
"""

import time
import argparse
import socket
import sys
import os
from pathlib import Path

from realdriver import (
    RealDriverClient,
    ScenarioDriveController,
    Waypoint,
    OSIReceiverWrapper
)
try:
    from DriverScript.argspec_utils import add_dump_argspec_option, maybe_dump_argspec
except ImportError:
    from argspec_utils import add_dump_argspec_option, maybe_dump_argspec

try:
    from scripts.collect_osi_light_metrics import OsiLightMetricsCollector
except ImportError:
    repo_scripts = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "scripts"))
    if repo_scripts not in sys.path:
        sys.path.insert(0, repo_scripts)
    from collect_osi_light_metrics import OsiLightMetricsCollector


def create_sample_waypoints():
    """Create sample waypoints for testing."""
    return [
        Waypoint(x=50.0, y=0.0, h=0.0, lane_id=-1),
        Waypoint(x=100.0, y=0.0, h=0.0, lane_id=-1),
        Waypoint(x=150.0, y=0.0, h=0.0, lane_id=-1),
        Waypoint(x=200.0, y=0.0, h=0.0, lane_id=-1),
    ]


def main():
    # Calculate script directory for relative paths
    script_dir = os.path.dirname(os.path.abspath(__file__))
    # Go up to DriverScript directory, then to bin
    bin_dir = os.path.normpath(os.path.join(script_dir, "..", "bin"))
    default_lib_path = os.path.join(bin_dir, "esminiRMLib.dll")
    default_gt_lib_path = os.path.join(bin_dir, "GT_esminiLib.dll")

    parser = argparse.ArgumentParser(
        description="ScenarioDrive Controller Example - Autonomous waypoint following"
    )
    parser.add_argument("--ip", type=str, default="127.0.0.1",
                        help="esmini Host IP")
    parser.add_argument("--port", type=int, default=53995,
                        help="RealDriver Base Port")
    parser.add_argument("--osi_port", type=int, default=48198,
                        help="OSI Port")
    parser.add_argument("--target_speed_port", type=int, default=54995,
                        help="Deprecated: ignored by embedded-only ScenarioDriveController")
    parser.add_argument("--id", type=int, default=0,
                        help="Object ID (Ego)")
    parser.add_argument("--lib_path", type=str, default=default_lib_path,
                        help="Path to esminiRMLib.dll")
    parser.add_argument("--gt_lib_path", type=str, default=default_gt_lib_path,
                        help="Path to GT_esminiLib.dll (for routing)")
    parser.add_argument("--xodr_path", type=str, required=True,
                        help="Path to OpenDRIVE map file (.xodr)")
    parser.add_argument("--target_speed", type=float, default=10.0,
                        help="Default target speed in m/s")
    parser.add_argument("--mode", type=str, default="waypoints",
                        choices=["waypoints", "target"],
                        help="Control mode: waypoints=explicit, target=auto-route")
    parser.add_argument("--target_x", type=float, default=300.0,
                        help="Target X coordinate (for target mode)")
    parser.add_argument("--target_y", type=float, default=0.0,
                        help="Target Y coordinate (for target mode)")
    parser.add_argument("--allow_reverse_from_profile", action="store_true",
                        help="Allow negative target speed from UDP profile and use reverse gear")
    parser.add_argument("--collect_osi_light_metrics", action="store_true",
                        help="Collect brake/reverse light metrics from received OSI GroundTruth")
    parser.add_argument("--light_metrics_out", type=str, default="",
                        help="Output JSON path for OSI light metrics")
    parser.add_argument("--light_metrics_csv_out", type=str, default="",
                        help="Optional output CSV path for OSI light samples")
    parser.add_argument("--max_runtime_s", type=float, default=0.0,
                        help="Exit loop after this many seconds (0=run until interrupted)")
    add_dump_argspec_option(parser)

    args = parser.parse_args()
    if maybe_dump_argspec(
        args,
        parser,
        ui_hints={
            "--xodr_path": {"ui": "path", "path_kind": "file"},
            "--lib_path": {"ui": "path", "path_kind": "file"},
            "--gt_lib_path": {"ui": "path", "path_kind": "file"},
        },
    ):
        return 0


    # 1. Initialize RealDriverClient
    print(f"Connecting to RealDriver via UDP at {args.ip}:{args.port}")
    client = RealDriverClient(args.ip, args.port)

    # 2. Initialize OSI Receiver
    print(f"Initializing OSI Receiver on port {args.osi_port}")
    osi_rx = OSIReceiverWrapper(port=args.osi_port)
    osi_rx.receiver.udp_receiver.sock.settimeout(0.1)

    # 3. Initialize ScenarioDriveController
    print(f"Initializing ScenarioDrive Controller with map: {args.xodr_path}")
    try:
        controller = ScenarioDriveController(
            lib_path=args.lib_path,
            xodr_path=args.xodr_path,
            ego_id=args.id,
            target_speed_port=args.target_speed_port,
            gt_lib_path=args.gt_lib_path,
            steering_pid=(1.5, 0.01, 0.1),
            speed_pid=(0.3, 0.01, 0.0),  # Reduced gains, removed D-term for stability
            lane_change_time=5.0,
            lookahead_distance=10.0,
            allow_reverse_from_profile=args.allow_reverse_from_profile,
        )
    except Exception as e:
        print(f"Failed to initialize ScenarioDrive Controller: {e}")
        osi_rx.close()
        client.close()
        return 1

    # 4. Set waypoints based on mode
    if args.mode == "waypoints":
        print("Mode: User-specified waypoints")
        waypoints = create_sample_waypoints()
        controller.set_waypoints(waypoints)
        print(f"  Set {len(waypoints)} waypoints")

    elif args.mode == "target":
        print("Mode: Auto-calculated route to target")
        target = Waypoint(x=args.target_x, y=args.target_y)
        controller.set_target(target)
        print(f"  Target: ({args.target_x}, {args.target_y})")

    # Set default target speed
    controller.set_target_speed(args.target_speed)
    print(f"Default target speed: {args.target_speed} m/s")

    print("\nStarting control loop. Press Ctrl+C to stop.")
    print("-" * 60)
    light_metrics = OsiLightMetricsCollector() if args.collect_osi_light_metrics else None

    try:
        last_time = time.time()
        start_time = last_time
        frame_number = 0
        no_route_warning_shown = False
        current_gear = 1
        reverse_deadband_mps = 0.2

        while True:
            if args.max_runtime_s > 0.0 and (time.time() - start_time) >= args.max_runtime_s:
                break
            # --- 1. Get raw OSI GroundTruth ---
            try:
                ground_truth = osi_rx.receiver.receive()
            except socket.timeout:
                ground_truth = None

            current_time = time.time()
            dt = current_time - last_time
            last_time = current_time
            if dt <= 0:
                dt = 0.001

            if ground_truth is not None:
                if light_metrics is not None:
                    light_metrics.observe(ground_truth)
                try:
                    # --- 2. Update ScenarioDrive Controller ---
                    steering, throttle, brake = controller.update(ground_truth, dt)

                    # --- 3. Handle no-route case ---
                    if steering is None:
                        if not no_route_warning_shown:
                            print("[WARN] No route configured - controls not output")
                            no_route_warning_shown = True
                        # Send neutral controls to keep vehicle stopped
                        client.set_controls(0.0, 0.5, 0.0)  # Apply some brake
                        client.set_gear(current_gear)
                        client.send_update()
                        frame_number += 1
                        continue

                    no_route_warning_shown = False  # Reset warning flag

                    # --- 4. Print status ---
                    if frame_number % 20 == 0:
                        speed = controller._last_speed
                        target_spd = controller.target_speed
                        print(f"Speed: {speed:.2f}/{target_spd:.2f} m/s | "
                              f"Steer: {steering:.3f} | "
                              f"Thr: {throttle:.2f} | Brk: {brake:.2f}")

                    # --- 5. Send Controls via RealDriverClient ---
                    # Note: esmini steering convention: +1 = right, -1 = left
                    # But our PID: positive error (target on right) = positive output = want to turn right
                    # So we need to negate: PID +1 -> Steer -1 (left turn in esmini)
                    # Wait, that's backwards. Let's think again:
                    # heading_error = heading_to_target - current_heading
                    # If target is to the LEFT, heading_to_target > current_heading, heading_error > 0
                    # To correct, we want to turn LEFT (negative steering in esmini)
                    # So: steering = -PID_output
                    target_speed = controller.target_speed
                    if args.allow_reverse_from_profile:
                        if target_speed < -reverse_deadband_mps:
                            current_gear = -1
                        elif target_speed > reverse_deadband_mps:
                            current_gear = 1
                    client.set_controls(throttle, brake, -steering)
                    client.set_gear(current_gear)
                    client.send_update()

                except Exception as e:
                    print(f"Controller Error: {e}")
                    client.set_controls(0.0, 0.5, 0.0)
                    client.set_gear(current_gear)
                    client.send_update()

            else:
                # OSI Timeout
                if frame_number % 100 == 0:
                    print("Waiting for OSI GroundTruth...")
                client.set_controls(0.0, 0.0, 0.0)
                client.set_gear(current_gear)
                client.send_update()

            frame_number += 1

    except KeyboardInterrupt:
        print("\nStopping...")
    finally:
        if light_metrics is not None and args.light_metrics_out:
            json_out = os.path.abspath(args.light_metrics_out)
            csv_out = os.path.abspath(args.light_metrics_csv_out) if args.light_metrics_csv_out else None
            light_metrics.write_outputs(
                json_out=Path(json_out),
                csv_out=Path(csv_out) if csv_out else None,
            )
        print("Closing connections...")
        controller.close()
        osi_rx.close()
        client.close()
        print("Done.")

    return 0


if __name__ == "__main__":
    sys.exit(main())
