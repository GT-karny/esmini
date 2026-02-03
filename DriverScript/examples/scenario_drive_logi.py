#!/usr/bin/env python3
"""
ScenarioDrive Controller with LogiSteer Support

This script extends the standard ScenarioDrive controller to support
Logitech steering wheels (force feedback and input).

Functionality:
1. Calculates steering command via ScenarioDriveController (PID)
2. Sends calculated steering as Spring Force to Logitech wheel
3. Reads actual steering angle from Logitech wheel
4. Sends actual steering, throttle, and brake to esmini RealDriver
"""

import time
import argparse
import socket
import sys
import os

from realdriver import (
    RealDriverClient,
    ScenarioDriveController,
    Waypoint,
    OSIReceiverWrapper
)

# Import LogiSteer module from realdriver package
try:
    from realdriver import logi_steer
except ImportError:
    # Fallback if running from examples directory without package installed
    # Add parent drive to path
    sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
    from DriverScript.realdriver import logi_steer


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
        description="ScenarioDrive + LogiSteer Controller Example"
    )
    parser.add_argument("--ip", type=str, default="127.0.0.1",
                        help="esmini Host IP")
    parser.add_argument("--port", type=int, default=53995,
                        help="RealDriver Base Port")
    parser.add_argument("--osi_port", type=int, default=48198,
                        help="OSI Port")
    parser.add_argument("--target_speed_port", type=int, default=54995,
                        help="UDP port for receiving target speed from esmini")
    parser.add_argument("--id", type=int, default=0,
                        help="Object ID (Ego)")
    parser.add_argument("--lib_path", type=str, default=default_lib_path,
                        help="Path to esminiRMLib.dll")
    parser.add_argument("--gt_lib_path", type=str, default=default_gt_lib_path,
                        help="Path to GT_esminiLib.dll (for routing)")
    parser.add_argument("--xodr_path", type=str, required=True,
                        help="Path to OpenDRIVE map file (.xodr)")
    parser.add_argument("--target_speed", type=float, default=10.0,
                        help="Default target speed in m/s (used if UDP not available)")
    parser.add_argument("--mode", type=str, default="waypoints",
                        choices=["waypoints", "target", "udp"],
                        help="Control mode: waypoints=explicit, target=auto-route, udp=from esmini")
    parser.add_argument("--target_x", type=float, default=300.0,
                        help="Target X coordinate (for target mode)")
    parser.add_argument("--target_y", type=float, default=0.0,
                        help="Target Y coordinate (for target mode)")

    args = parser.parse_args()


    # 0. Initialize LogiSteer
    print("Initializing Logitech Steering Wheel...")
    if not logi_steer.Init():
        print("[ERROR] Failed to initialize Logitech Steering Wheel SDK.")
        print("        Ensure G HUB is running and a supported wheel is connected.")
        # We can choose to exit or continue without FF. Retaining functionality implies exit usually.
        # But for development, maybe we want to fallback?
        # User requested specific function so we should probably warn strongly or exit.
        # Let's try to proceed but warn.
        print("        Proceeding without Force Feedback/Input (Simulation Only).")
    
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
            speed_pid=(0.3, 0.01, 0.0),
            lane_change_time=5.0,
            lookahead_distance=10.0
        )
    except Exception as e:
        print(f"Failed to initialize ScenarioDrive Controller: {e}")
        osi_rx.close()
        client.close()
        # Shutdown Logi if initialized
        logi_steer.Shutdown()
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

    elif args.mode == "udp":
        print("Mode: Waiting for waypoints from UDP")
        print("  (Waypoints will be received from esmini ControllerRealDriver)")

    # Set default target speed
    controller.set_target_speed(args.target_speed)
    print(f"Default target speed: {args.target_speed} m/s")

    print("\nStarting control loop with LogiSteer Feedback. Press Ctrl+C to stop.")
    print("-" * 60)

    try:
        last_time = time.time()
        frame_number = 0
        no_route_warning_shown = False

        while True:
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

            # --- 2. Update Controller (Target Control) ---
            auto_steering = 0.0
            throttle = 0.0
            brake = 0.0
            
            if ground_truth is not None:
                try:
                    # auto_steering is the Calculated Ideal Steering from PID
                    auto_steering_opt, throttle_opt, brake_opt = controller.update(ground_truth, dt)
                    
                    if auto_steering_opt is None:
                        # No route
                        if not no_route_warning_shown:
                            print("[WARN] No route configured - controls not output")
                            no_route_warning_shown = True
                        auto_steering = 0.0
                        throttle = 0.0
                        brake = 0.0 # Or keep previous? Safe to stop.
                    else:
                        no_route_warning_shown = False
                        auto_steering = auto_steering_opt
                        throttle = throttle_opt
                        brake = brake_opt
                        
                except Exception as e:
                    print(f"Controller Error: {e}")
                    auto_steering = 0.0
                    throttle = 0.0
                    brake = 0.0
            else:
                # OSI Timeout - Stop
                if frame_number % 100 == 0:
                    print("Waiting for OSI GroundTruth...")
                auto_steering = 0.0
                throttle = 0.0
                brake = 0.0

            # --- 3. LogiSteer Interaction (ALWAYS RUN) ---
            # Check for Reset Button (Circle Only)
            try:
                is_reset = logi_steer.IsButtonPressed(logi_steer.BUTTON_CIRCLE)
            except Exception as e:
                # Safety if LogiSteer fails
                is_reset = False
            
            if is_reset:
                 # Override Auto Steering force to 0 (Center)
                 auto_steering = 0.0
                 # Force simulation input to 0 (Virtual Center)
                 actual_steer = 0.0
                 print(f" [RESET ACTIVE] Button Pressed. Steering centered.")
            else:
                 # B. Get Actual Physical Steering Angle
                 actual_steer = logi_steer.GetSteerAngle()

            # A. Set Force Feedback (Target Angle)
            # auto_steering is Positive-Left. LogiSteer accepts Positive-Left.
            logi_steer.SetSteerAngle(auto_steering)
            
            # --- 4. Send Controls via RealDriverClient ---
            # RealDriver expects Positive-Left
            # actual_steer is Positive-Left (or 0.0 if reset)
            client.set_controls(throttle, brake, -actual_steer)
            client.set_gear(1)
            client.send_update()

            # --- 5. Print status ---
            if frame_number % 20 == 0 and ground_truth is not None:
                speed = controller._last_speed
                target_spd = controller.target_speed
                reset_str = " [RESET]" if is_reset else ""
                print(f"Speed: {speed:.2f}/{target_spd:.2f} | "
                      f"AutoSteer: {auto_steering:.3f} | "
                      f"PhysSteer: {actual_steer:.3f} | "
                      f"Thr: {throttle:.2f}{reset_str}")
            
            frame_number += 1

    except KeyboardInterrupt:
        print("\nStopping...")
    finally:
        print("Closing connections...")
        # Shutdown Logi
        logi_steer.Shutdown()
        
        controller.close()
        osi_rx.close()
        client.close()
        print("Done.")

    return 0


if __name__ == "__main__":
    sys.exit(main())
