#!/usr/bin/env python3
"""
Lane Change Controller Example

Demonstrates using the LaneChangeController for event-driven lane changes
with TTC-based safety checking.

Features:
1. Safety check before lane change
2. Get adjacent vehicle information
3. Trigger lane change on keyboard input
4. Combine with other controllers when idle

Usage:
    python lane_change_example.py --lib_path path/to/esminiRMLib.dll --xodr_path path/to/map.xodr
"""

import time
import argparse
import socket
import sys
import os

# Add parent directory to path for imports
script_dir = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(script_dir))

from realdriver import (
    # Lane Change Controller (NEW)
    LaneChangeController,
    LaneChangeConfig,
    LCState,
    # Other controllers for idle state
    LongitudinalController,
    LateralController,
    # Communication
    RealDriverClient,
    OSIReceiverWrapper,
    IndicatorMode,
    # Road Manager
    EsminiRMLib,
)

try:
    from DriverScript.argspec_utils import add_dump_argspec_option, maybe_dump_argspec
except ImportError:
    from argspec_utils import add_dump_argspec_option, maybe_dump_argspec


def print_safety_check(safety, direction):
    """Print safety check result."""
    print(f"\n=== Safety Check ({direction}) ===")
    print(f"  Safe: {safety.is_safe}")
    print(f"  Reason: {safety.reason}")
    print(
        f"  Front: gap={safety.min_gap_front:.1f}m, ttc={safety.min_ttc_front:.1f}s, count={len(safety.front_vehicles)}"
    )
    print(
        f"  Rear:  gap={safety.min_gap_rear:.1f}m, ttc={safety.min_ttc_rear:.1f}s, count={len(safety.rear_vehicles)}"
    )


def print_adjacent_vehicles(vehicles, direction):
    """Print adjacent vehicle information."""
    print(f"\n=== Adjacent Vehicles ({direction}) ===")
    if not vehicles:
        print("  No vehicles detected")
        return

    for veh in sorted(vehicles, key=lambda v: v.longitudinal_dist):
        position = "front" if veh.longitudinal_dist > 0 else "rear"
        print(
            f"  [{position}] ID={veh.obj_id}: "
            f"dist={veh.longitudinal_dist:.1f}m, "
            f"speed={veh.speed:.1f}m/s, "
            f"ttc={veh.ttc:.1f}s"
        )


def main():
    parser = argparse.ArgumentParser(description="Lane Change Controller Example")
    parser.add_argument("--ip", type=str, default="127.0.0.1", help="esmini Host IP")
    parser.add_argument("--port", type=int, default=53995, help="RealDriver Base Port")
    parser.add_argument("--osi_port", type=int, default=48198, help="OSI Port")
    parser.add_argument("--id", type=int, default=0, help="Object ID (Ego)")
    parser.add_argument(
        "--lib_path", type=str, default=None, help="Path to esminiRMLib.dll"
    )
    parser.add_argument(
        "--xodr_path", type=str, default=None, help="Path to OpenDRIVE map file"
    )
    parser.add_argument(
        "--target_speed", type=float, default=15.0, help="Target speed in m/s"
    )
    parser.add_argument(
        "--demo", action="store_true", help="Run demonstration without connecting"
    )
    add_dump_argspec_option(parser)
    args = parser.parse_args()
    if maybe_dump_argspec(
        args,
        parser,
        ui_hints={
            "--xodr_path": {"ui": "path", "path_kind": "file"},
            "--lib_path": {"ui": "path", "path_kind": "file"},
        },
    ):
        return 0

    if args.demo:
        print("=" * 60)
        print("Lane Change Controller API Demonstration")
        print("=" * 60)
        print("""
# Initialize
config = LaneChangeConfig(
    ttc_threshold=3.0,           # Abort if TTC < 3 seconds
    min_gap_front=15.0,          # Minimum 15m gap to front vehicle
    min_gap_rear=10.0,           # Minimum 10m gap to rear vehicle
    lane_change_duration=4.0,    # 4 seconds to complete
    steering_gain=0.3            # Max steering amplitude
)
lc = LaneChangeController(rm_lib=rm_lib, ego_id=0, config=config)
lc.set_base_speed(20.0)

# Safety check API (can call anytime)
safety = lc.check_safety(ground_truth, 'left')
if safety.is_safe:
    print(f"Safe! Min TTC: {safety.min_ttc_front:.1f}s")
else:
    print(f"Unsafe: {safety.reason}")

# Get adjacent vehicles (for visualization/debugging)
vehicles = lc.get_adjacent_vehicles(ground_truth, 'left')
for v in vehicles:
    print(f"Vehicle {v.obj_id}: dist={v.longitudinal_dist:.1f}m, ttc={v.ttc:.1f}s")

# Trigger lane change
if lc.trigger_lane_change('left'):
    print("Lane change started!")

# Main loop
while True:
    output = lc.update(ground_truth, dt)

    if output.is_active:
        # Lane change in progress
        client.set_controls(output.throttle, output.brake, output.steering)
        set_indicator(output.indicator)  # 1=left, 2=right

    elif output.completed:
        print("Lane change completed!")
        # Switch to normal controller

    elif output.aborted:
        print("Lane change aborted!")
        # Handle abort

    else:
        # IDLE - use other controllers
        pass
""")
        print("=" * 60)
        return 0

    # Calculate default paths
    bin_dir = os.path.normpath(os.path.join(script_dir, "..", "bin"))
    lib_path = args.lib_path or os.path.join(bin_dir, "esminiRMLib.dll")

    if not args.xodr_path:
        print("Error: --xodr_path is required for actual operation")
        return 1

    # Initialize RoadManager
    print(f"Initializing RoadManager with: {args.xodr_path}")
    print(f"Using library: {lib_path}")
    rm_lib = EsminiRMLib(lib_path)
    if rm_lib.Init(args.xodr_path) < 0:
        print("Failed to initialize RoadManager")
        return 1

    print(f"Connecting to RealDriver at {args.ip}:{args.port}")
    client = RealDriverClient(args.ip, args.port)

    print(f"Initializing OSI Receiver on port {args.osi_port}")
    osi_rx = OSIReceiverWrapper(port=args.osi_port)
    osi_rx.receiver.udp_receiver.sock.settimeout(0.1)

    # Initialize lane change controller
    lc_config = LaneChangeConfig(
        ttc_threshold=3.0,
        min_gap_front=15.0,
        min_gap_rear=10.0,
        lane_change_duration=4.0,
        steering_gain=0.3,
    )
    lc_controller = LaneChangeController(
        rm_lib=rm_lib, ego_id=args.id, config=lc_config
    )
    lc_controller.set_base_speed(args.target_speed)
    lc_controller.enable_debug(True)

    # Fallback controllers for IDLE state
    lon_controller = LongitudinalController(ego_id=args.id)
    lon_controller.set_target_speed(args.target_speed)

    print("\n" + "=" * 60)
    print("Lane Change Controller Started")
    print("=" * 60)
    print("Commands:")
    print("  Press Ctrl+C to stop")
    print("  (Lane change is triggered programmatically in this example)")
    print("-" * 60)

    try:
        last_time = time.time()
        frame_number = 0
        lane_change_triggered = False
        trigger_delay = 5.0  # Wait 5 seconds before first lane change attempt
        start_time = time.time()

        while True:
            # Get OSI GroundTruth
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
                # Trigger lane change after delay (demo)
                elapsed = current_time - start_time
                if not lane_change_triggered and elapsed > trigger_delay:
                    # Check safety first
                    safety = lc_controller.check_safety(ground_truth, "left")
                    print_safety_check(safety, "left")

                    if safety.is_safe:
                        if lc_controller.trigger_lane_change("left"):
                            print("\n>>> Lane change triggered!")
                            lane_change_triggered = True
                    else:
                        # Try again next frame
                        pass

                # Update controller
                output = lc_controller.update(ground_truth, dt)

                if output.is_active:
                    # Lane change in progress
                    client.set_controls(output.throttle, output.brake, output.steering)

                    # Set indicator
                    if output.indicator == 1:
                        client.set_indicators(IndicatorMode.LEFT)
                    elif output.indicator == 2:
                        client.set_indicators(IndicatorMode.RIGHT)

                    # Print status
                    if frame_number % 20 == 0:
                        print(
                            f"[LC] Progress={lc_controller.progress:.2f}, "
                            f"State={output.state.name}, "
                            f"Steer={output.steering:.3f}"
                        )

                elif output.completed:
                    print("\n>>> Lane change COMPLETED!")
                    client.set_indicators(IndicatorMode.OFF)
                    # Could trigger another lane change here...

                elif output.aborted:
                    print(f"\n>>> Lane change ABORTED!")
                    client.set_indicators(IndicatorMode.OFF)
                    lane_change_triggered = False  # Allow retry

                else:
                    # IDLE - use fallback longitudinal controller
                    lon_out = lon_controller.update(ground_truth, dt)
                    client.set_controls(lon_out.throttle, lon_out.brake, 0.0)
                    client.set_indicators(IndicatorMode.OFF)

                    if frame_number % 50 == 0:
                        print(
                            f"[IDLE] Speed={lon_controller.last_speed:.2f}/"
                            f"{lon_controller.target_speed:.2f} m/s"
                        )

                client.set_gear(1)
                client.send_update()

            else:
                # OSI Timeout
                if frame_number % 100 == 0:
                    print("Waiting for OSI GroundTruth...")
                client.set_controls(0.0, 0.0, 0.0)
                client.set_gear(1)
                client.send_update()

            frame_number += 1

    except KeyboardInterrupt:
        print("\nStopping...")
    finally:
        print("Closing connections...")
        osi_rx.close()
        client.close()
        rm_lib.Close()
        print("Done.")

    return 0


if __name__ == "__main__":
    sys.exit(main())
