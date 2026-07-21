#!/usr/bin/env python3
"""
ACC + LKAS サンプルスクリプト（RoadManager連携版）

ACCController（縦制御）と LKASController（横制御）を RoadManager を共有して使用し、
シナリオ上で先行車追従 + 車線維持を行うサンプルです。

- ACC: RoadManagerモードで先行車を検出（車線ベース、高精度）
- LKAS: RoadManagerで車線オフセットを検出し、レーンセンターを維持
- 目標速度: GT_SimからUDP受信

シンプル版 (acc_lkas_example.py) との違い:
    - LKASが初期化したRoadManagerインスタンスをACCにも渡して共有
    - ACCが同じ道路・同じ車線上の先行車を正確に判定可能

使用方法:
    python acc_lkas_rm_example.py --xodr_path <path_to_xodr> [options]
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
    RealDriverClient,
    OSIReceiverWrapper,
    ACCController,
    LKASController,
    LongitudinalProfileReceiver,
)

try:
    from DriverScript.argspec_utils import add_dump_argspec_option, maybe_dump_argspec
except ImportError:
    from argspec_utils import add_dump_argspec_option, maybe_dump_argspec


def main():
    # Calculate default paths
    bin_dir = os.path.normpath(os.path.join(script_dir, "..", "bin"))
    default_lib_path = os.path.join(bin_dir, "esminiRMLib.dll")

    parser = argparse.ArgumentParser(
        description="ACC + LKAS Example (RM-linked) - 先行車追従 + 車線維持（RoadManager連携）"
    )
    parser.add_argument("--ip", type=str, default="127.0.0.1", help="esmini Host IP")
    parser.add_argument("--port", type=int, default=53995, help="RealDriver Base Port")
    parser.add_argument("--osi_port", type=int, default=48198, help="OSI Port")
    parser.add_argument(
        "--target_speed_port",
        type=int,
        default=54995,
        help="UDP port for receiving target speed from GT_Sim",
    )
    parser.add_argument("--id", type=int, default=0, help="Object ID (Ego)")
    parser.add_argument(
        "--lib_path", type=str, default=default_lib_path, help="Path to esminiRMLib.dll"
    )
    parser.add_argument(
        "--xodr_path",
        type=str,
        required=True,
        help="Path to OpenDRIVE map file (.xodr)",
    )
    parser.add_argument(
        "--target_speed",
        type=float,
        default=10.0,
        help="Default target speed in m/s (used until UDP overrides)",
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

    # --- 1. Initialize RealDriverClient ---
    print(f"Connecting to RealDriver via UDP at {args.ip}:{args.port}")
    client = RealDriverClient(args.ip, args.port)

    # --- 2. Initialize OSI Receiver ---
    print(f"Initializing OSI Receiver on port {args.osi_port}")
    osi_rx = OSIReceiverWrapper(port=args.osi_port)
    osi_rx.receiver.udp_receiver.sock.settimeout(0.1)

    # --- 3. Initialize LKAS Controller (横制御: 車線維持) ---
    # LKASControllerは内部でEsminiRMLibを初期化・管理する
    print(f"Initializing LKAS Controller with map: {args.xodr_path}")
    try:
        lkas = LKASController(
            lib_path=args.lib_path,
            xodr_path=args.xodr_path,
            ego_id=args.id,
            kp=0.5,
            ki=0.01,
            kd=0.1,
        )
    except Exception as e:
        print(f"Failed to initialize LKAS Controller: {e}")
        osi_rx.close()
        client.close()
        return 1

    # --- 4. Initialize ACC Controller (縦制御: 先行車追従) ---
    # rm_lib=lkas.rm_lib: LKASのRoadManagerを共有して車線ベースの先行車検出を使用
    # これにより、同じ道路・同じ車線上の車両を正確に判定できる
    print("Initializing ACC Controller (RoadManager-linked mode)")
    acc = ACCController(ego_id=args.id, rm_lib=lkas.rm_lib)
    acc.set_target_speed(args.target_speed)

    # --- 5. Initialize Target Speed Receiver ---
    # GT_SimからUDPで目標速度を受信する
    print(f"Listening for target speed on UDP port {args.target_speed_port}")
    speed_receiver = LongitudinalProfileReceiver(args.target_speed_port)

    print(f"\nDefault target speed: {args.target_speed} m/s")
    print("ACC detection mode: RoadManager (lane-based)")
    print("Starting control loop. Press Ctrl+C to stop.")
    print("-" * 60)

    try:
        last_time = time.time()
        frame_number = 0

        while True:
            # --- Receive OSI GroundTruth ---
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
                # --- Receive target speed from GT_Sim ---
                udp_profile = speed_receiver.receive_all()
                if udp_profile:
                    acc.set_target_speed(udp_profile[0].v_target)

                # --- ACC: 縦制御 (throttle, brake) ---
                lon_output = acc.update(ground_truth, dt)

                # --- LKAS: 横制御 (steering) ---
                try:
                    steering = lkas.update(ground_truth, dt)
                except ValueError:
                    steering = 0.0

                # --- Print status ---
                if frame_number % 20 == 0:
                    lead = acc.lead_vehicle
                    lead_str = (
                        f"lead: gap={lead.gap_distance:.1f}m, "
                        f"spd={lead.lead_speed:.1f}m/s"
                        if lead
                        else "lead: none"
                    )
                    print(
                        f"Speed: {acc.last_speed:.2f}/{acc.target_speed:.2f} m/s | "
                        f"Steer: {steering:.3f} | "
                        f"Thr: {lon_output.throttle:.2f} | "
                        f"Brk: {lon_output.brake:.2f} | "
                        f"{lead_str}"
                    )

                # --- Send Controls ---
                # esmini steering convention: negative = turn left
                # LKAS: positive = steer left → negate for esmini
                client.set_controls(lon_output.throttle, lon_output.brake, -steering)
                client.set_gear(1)  # Drive
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
        osi_rx.close()
        client.close()
        print("Done.")

    return 0


if __name__ == "__main__":
    sys.exit(main())
