#!/usr/bin/env python3
"""
LKAS (Lane Keeping Assist System) Example

Demonstrates using the RealDriver client with LKAS controller
to maintain lane position while driving in esmini.
"""

import time
import argparse
import socket

from realdriver import RealDriverClient, LKASController, OSIReceiverWrapper


def main():
    parser = argparse.ArgumentParser(description="Test script for ALKS LKAS Module with RealDriver & OSI")
    parser.add_argument("--ip", type=str, default="127.0.0.1", help="esmini Host IP")
    parser.add_argument("--port", type=int, default=53995, help="RealDriver Base Port")
    parser.add_argument("--osi_port", type=int, default=48198, help="OSI Port")
    parser.add_argument("--id", type=int, default=0, help="Object ID (Ego)")
    parser.add_argument("--lib_path", type=str, default="./bin/esminiRMLib.dll", help="Path to esminiRMLib.dll")
    parser.add_argument("--xodr_path", type=str, required=True, help="Path to OpenDRIVE map file (.xodr)")

    args = parser.parse_args()

    # 1. Initialize RealDriverClient (OSI HostVehicleData + LightMask protocol)
    print(f"Connecting to RealDriver via UDP at {args.ip}:{args.port} for Object ID {args.id}")
    client = RealDriverClient(args.ip, args.port)

    # 2. Initialize OSI Receiver
    print(f"Initializing OSI Receiver on port {args.osi_port}")
    osi_rx = OSIReceiverWrapper(port=args.osi_port)
    osi_rx.receiver.udp_receiver.sock.settimeout(0.1)

    # 3. Initialize LKAS Controller (now self-contained with RoadManager)
    print(f"Initializing LKAS Controller with map: {args.xodr_path}")
    try:
        lkas = LKASController(
            lib_path=args.lib_path,
            xodr_path=args.xodr_path,
            ego_id=args.id,
            model_type='ReferenceDriver',
            kp=1.5, ki=0.01, kd=0.1
        )
    except Exception as e:
        print(f"Failed to initialize LKAS: {e}")
        osi_rx.close()
        client.close()
        return

    print("\nStarting control loop. Press Ctrl+C to stop.")

    try:
        # Initial control values
        throttle = 0.0
        brake = 0.0
        steering_angle = 0.0

        last_time = time.time()
        frame_number = 0

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

            if ground_truth is not None:
                try:
                    # --- 2. Update LKAS with raw GroundTruth ---
                    speed = lkas.last_ego_speed
                    if speed > 0.1:
                        steering_angle = lkas.update(ground_truth, dt)
                    else:
                        # First update to get speed
                        steering_angle = lkas.update(ground_truth, dt)
                    speed = lkas.last_ego_speed

                    # --- 3. Simple Speed Control (Cruise) ---
                    target_speed = 10.0  # m/s (36km/h)

                    if speed < target_speed:
                        throttle = 0.5
                        brake = 0.0
                    else:
                        throttle = 0.0
                        brake = 0.0

                    # Print status
                    if frame_number % 20 == 0:
                        print(f"Speed: {speed:.2f} m/s | Steer: {steering_angle:.3f} rad | Thr: {throttle:.2f} | Brk: {brake:.2f}")

                except ValueError as e:
                    print(f"LKAS Error: {e}")
                    throttle = 0.0
                    brake = 0.0
                    steering_angle = 0.0

            else:
                # OSI Timeout
                print("Waiting for OSI GroundTruth... (sending keep-alive controls)")
                throttle = 0.0
                brake = 0.0
                steering_angle = 0.0

            # --- 4. Send Controls via RealDriverClient ---
            # Note: Negate steering_angle to compensate for sign inversion in RealVehicle.cpp
            client.set_controls(throttle, brake, -steering_angle)
            client.set_gear(1)  # Drive
            client.send_update()
            frame_number += 1

    except KeyboardInterrupt:
        print("\nStopping...")
    finally:
        print("Closing connections...")
        osi_rx.close()
        # Note: lkas destructor will call rm_lib.Close() automatically
        if 'client' in locals():
            client.close()
        print("Done.")


if __name__ == "__main__":
    main()
