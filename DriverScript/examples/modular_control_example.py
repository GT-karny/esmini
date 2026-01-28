#!/usr/bin/env python3
"""
Modular Control Example

Demonstrates using the new modular LateralController and LongitudinalController
independently. This example shows how to:

1. Use only lateral control (steering) with external speed control
2. Use only longitudinal control (throttle/brake) with external steering
3. Combine both for full autonomous control

This approach is more flexible than ScenarioDriveController when you need to:
- Swap out just the longitudinal controller
- Use a different steering algorithm
- Integrate with external planning systems
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
    # Modular controllers (NEW)
    LateralController,
    LongitudinalController,
    VehicleStateExtractor,
    LateralConfig,
    LongitudinalConfig,
    # UDP receivers
    WaypointReceiver,
    TargetSpeedReceiver,
    # Communication
    RealDriverClient,
    OSIReceiverWrapper,
    # Road Manager
    EsminiRMLib,
    # Waypoint
    Waypoint,
)


def example_lateral_only():
    """Example: Use only lateral control with fixed throttle."""
    print("\n=== Lateral Control Only Example ===")
    print("Steering follows waypoints, throttle is fixed.")

    # This would be your initialization
    # lateral = LateralController(rm_lib=rm_lib)
    # lateral.set_waypoints(waypoints)
    #
    # In control loop:
    #   steering = lateral.update(state, dt)
    #   throttle = 0.3  # Fixed
    #   brake = 0.0

    print("Code pattern:")
    print("""
    lateral = LateralController(rm_lib=rm_lib)
    lateral.set_waypoints(waypoints)

    while running:
        state = extractor.extract(ground_truth)
        steering = lateral.update(state, dt)

        # Use fixed or external throttle
        client.set_controls(throttle=0.3, brake=0.0, steering=-steering)
    """)


def example_longitudinal_only():
    """Example: Use only longitudinal control with external steering."""
    print("\n=== Longitudinal Control Only Example ===")
    print("Speed follows target, steering is from external source.")

    # This would be your initialization
    # longitudinal = LongitudinalController()
    # longitudinal.set_target_speed(10.0)
    #
    # In control loop:
    #   output = longitudinal.update(current_speed, dt)
    #   steering = external_steering_source()

    print("Code pattern:")
    print("""
    longitudinal = LongitudinalController()
    longitudinal.set_target_speed(10.0)  # 10 m/s

    while running:
        state = extractor.extract(ground_truth)
        output = longitudinal.update(state.speed, dt)

        # Use external steering (e.g., human input, other controller)
        steering = get_external_steering()
        client.set_controls(output.throttle, output.brake, steering)
    """)


def example_combined():
    """Example: Combine lateral and longitudinal control."""
    print("\n=== Combined Modular Control Example ===")
    print("Both steering and speed are controlled independently.")

    print("Code pattern:")
    print("""
    # Initialize
    rm_lib = EsminiRMLib(lib_path)
    rm_lib.Init(xodr_path)

    extractor = VehicleStateExtractor(ego_id=0)
    lateral = LateralController(rm_lib=rm_lib)
    longitudinal = LongitudinalController()

    lateral.set_waypoints(waypoints)
    longitudinal.set_target_speed(10.0)

    while running:
        # Extract vehicle state
        state = extractor.extract(ground_truth)
        state = extractor.enrich_with_road_data(state, rm_lib)

        # Calculate control outputs independently
        steering = lateral.update(state, dt)
        lon_out = longitudinal.update(state.speed, dt)

        # Send to vehicle
        client.set_controls(lon_out.throttle, lon_out.brake, -steering)
    """)


def main():
    parser = argparse.ArgumentParser(
        description="Modular Control Example - Demonstrates independent lateral/longitudinal control"
    )
    parser.add_argument("--ip", type=str, default="127.0.0.1", help="esmini Host IP")
    parser.add_argument("--port", type=int, default=53995, help="RealDriver Base Port")
    parser.add_argument("--osi_port", type=int, default=48198, help="OSI Port")
    parser.add_argument("--target_speed_port", type=int, default=54995, help="Target speed UDP port")
    parser.add_argument("--waypoint_port", type=int, default=54996, help="Waypoint UDP port")
    parser.add_argument("--id", type=int, default=0, help="Object ID (Ego)")
    parser.add_argument("--lib_path", type=str, default=None, help="Path to esminiRMLib.dll")
    parser.add_argument("--xodr_path", type=str, default=None, help="Path to OpenDRIVE map file")
    parser.add_argument("--target_speed", type=float, default=10.0, help="Target speed in m/s")
    parser.add_argument("--demo", action="store_true", help="Run demonstration without connecting")
    args = parser.parse_args()

    if args.demo:
        print("=" * 60)
        print("Modular Control API Demonstration")
        print("=" * 60)
        example_lateral_only()
        example_longitudinal_only()
        example_combined()
        print("\n" + "=" * 60)
        print("Run with actual connection by providing --lib_path and --xodr_path")
        print("=" * 60)
        return 0

    # Calculate default paths
    bin_dir = os.path.normpath(os.path.join(script_dir, "..", "bin"))
    lib_path = args.lib_path or os.path.join(bin_dir, "esminiRMLib.dll")

    if not args.xodr_path:
        print("Error: --xodr_path is required for actual operation")
        return 1

    # Initialize components
    print(f"Initializing RoadManager with: {args.xodr_path}")
    rm_lib = EsminiRMLib(lib_path)
    if rm_lib.Init(args.xodr_path) < 0:
        print("Failed to initialize RoadManager")
        return 1

    print(f"Connecting to RealDriver at {args.ip}:{args.port}")
    client = RealDriverClient(args.ip, args.port)

    print(f"Initializing OSI Receiver on port {args.osi_port}")
    osi_rx = OSIReceiverWrapper(port=args.osi_port)
    osi_rx.receiver.udp_receiver.sock.settimeout(0.1)

    # Initialize modular components
    extractor = VehicleStateExtractor(ego_id=args.id)
    lateral = LateralController(rm_lib=rm_lib)
    longitudinal = LongitudinalController()

    # Optional: UDP receivers for external control
    speed_receiver = TargetSpeedReceiver(args.target_speed_port)
    waypoint_receiver = WaypointReceiver(args.waypoint_port)

    # Set initial target speed
    longitudinal.set_target_speed(args.target_speed)
    print(f"Target speed: {args.target_speed} m/s")

    print("\nStarting control loop. Press Ctrl+C to stop.")
    print("-" * 60)

    try:
        last_time = time.time()
        frame_number = 0

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
                # Extract vehicle state
                state = extractor.extract(ground_truth)
                if state is None:
                    if frame_number % 100 == 0:
                        print("Waiting for ego vehicle...")
                    frame_number += 1
                    continue

                state = extractor.enrich_with_road_data(state, rm_lib)

                # Check for UDP updates
                udp_speed = speed_receiver.receive_all()
                if udp_speed is not None:
                    longitudinal.set_target_speed(udp_speed)

                udp_wp = waypoint_receiver.receive_all()
                if udp_wp is not None:
                    index, waypoints = udp_wp
                    lateral.set_waypoints(waypoints[index:])

                # Calculate control
                steering = 0.0
                if lateral.has_route:
                    steering = lateral.update(state, dt)

                lon_out = longitudinal.update(state.speed, dt)

                # Print status
                if frame_number % 20 == 0:
                    print(f"Speed: {state.speed:.2f}/{longitudinal.target_speed:.2f} m/s | "
                          f"Steer: {steering:.3f} | "
                          f"Thr: {lon_out.throttle:.2f} | Brk: {lon_out.brake:.2f}")

                # Send controls
                client.set_controls(lon_out.throttle, lon_out.brake, -steering)
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
        speed_receiver.close()
        waypoint_receiver.close()
        osi_rx.close()
        client.close()
        rm_lib.Close()
        print("Done.")

    return 0


if __name__ == "__main__":
    sys.exit(main())
