#!/usr/bin/env python3
"""
ScenarioDrive + ACC サンプルスクリプト

ScenarioDriveController によるシナリオルート追従と、
ACCController による先行車追従を組み合わせたサンプルです。

- 横制御: ScenarioDriveController のステアリング出力を使用（ウェイポイント追従）
- 縦制御: ACCController の throttle/brake 出力を使用（先行車追従）
- 目標速度: GT_Sim から UDP 受信（ScenarioDriveController 経由で同期）

scenario_drive_example.py との違い:
    - 縦制御が単純なPID速度制御からACCに置き換わっている
    - 前方に車両がいれば車間距離を保って追従
    - 前方車両がいなければ目標速度でクルーズ

使用方法:
    python scenario_acc_example.py --xodr_path <path_to_xodr> [options]

    # ウェイポイント指定モード（デフォルト）
    python scenario_acc_example.py --xodr_path map.xodr --mode waypoints

    # ターゲット座標への自動ルート計算モード
    python scenario_acc_example.py --xodr_path map.xodr --mode target --target_x 300 --target_y 0

    # esmini からの UDP ウェイポイント受信モード
    python scenario_acc_example.py --xodr_path map.xodr --mode udp
"""

import time
import argparse
import socket
import sys

from realdriver import (
    RealDriverClient,
    ScenarioDriveController,
    ACCController,
    Waypoint,
    OSIReceiverWrapper,
)


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
    import os
    script_dir = os.path.dirname(os.path.abspath(__file__))
    # Go up to DriverScript directory, then to bin
    bin_dir = os.path.normpath(os.path.join(script_dir, "..", "bin"))
    default_lib_path = os.path.join(bin_dir, "esminiRMLib.dll")
    default_gt_lib_path = os.path.join(bin_dir, "GT_esminiLib.dll")

    parser = argparse.ArgumentParser(
        description="ScenarioDrive + ACC Example - シナリオルート追従 + 先行車追従"
    )
    parser.add_argument("--ip", type=str, default="127.0.0.1",
                        help="esmini Host IP")
    parser.add_argument("--port", type=int, default=53995,
                        help="RealDriver Base Port")
    parser.add_argument("--osi_port", type=int, default=48198,
                        help="OSI Port")
    parser.add_argument("--target_speed_port", type=int, default=54995,
                        help="UDP port for receiving target speed from GT_Sim")
    parser.add_argument("--id", type=int, default=0,
                        help="Object ID (Ego)")
    parser.add_argument("--lib_path", type=str, default=default_lib_path,
                        help="Path to esminiRMLib.dll")
    parser.add_argument("--gt_lib_path", type=str, default=default_gt_lib_path,
                        help="Path to GT_esminiLib.dll (for routing)")
    parser.add_argument("--xodr_path", type=str, required=True,
                        help="Path to OpenDRIVE map file (.xodr)")
    parser.add_argument("--target_speed", type=float, default=10.0,
                        help="Default target speed in m/s (used until UDP overrides)")
    parser.add_argument("--mode", type=str, default="waypoints",
                        choices=["waypoints", "target", "udp"],
                        help="Control mode: waypoints=explicit, target=auto-route, udp=from esmini")
    parser.add_argument("--target_x", type=float, default=300.0,
                        help="Target X coordinate (for target mode)")
    parser.add_argument("--target_y", type=float, default=0.0,
                        help="Target Y coordinate (for target mode)")

    args = parser.parse_args()

    # --- 1. Initialize RealDriverClient ---
    print(f"Connecting to RealDriver via UDP at {args.ip}:{args.port}")
    client = RealDriverClient(args.ip, args.port)

    # --- 2. Initialize OSI Receiver ---
    print(f"Initializing OSI Receiver on port {args.osi_port}")
    osi_rx = OSIReceiverWrapper(port=args.osi_port)
    osi_rx.receiver.udp_receiver.sock.settimeout(0.1)

    # --- 3. Initialize ScenarioDriveController (横制御: ルート追従) ---
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
            lookahead_distance=10.0,
        )
    except Exception as e:
        print(f"Failed to initialize ScenarioDrive Controller: {e}")
        osi_rx.close()
        client.close()
        return 1

    # --- 4. Initialize ACC Controller (縦制御: 先行車追従) ---
    # ScenarioDriveController の RoadManager を共有して車線ベースの先行車検出を使用
    print("Initializing ACC Controller (RoadManager-linked)")
    acc = ACCController(ego_id=args.id, rm_lib=controller.rm_lib)

    # --- 5. Set waypoints based on mode ---
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

    # --- 6. Set default target speed ---
    controller.set_target_speed(args.target_speed)
    acc.set_target_speed(args.target_speed)
    print(f"Default target speed: {args.target_speed} m/s")

    print("\nStarting control loop. Press Ctrl+C to stop.")
    print("-" * 60)

    try:
        last_time = time.time()
        frame_number = 0
        no_route_warning_shown = False

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
                try:
                    # --- ScenarioDriveController: ステアリング取得 ---
                    # update() は内部で目標速度のUDP受信も行う
                    steering, _throttle, _brake = controller.update(ground_truth, dt)

                    # --- Handle no-route case ---
                    if steering is None:
                        if not no_route_warning_shown:
                            print("[WARN] No route configured - controls not output")
                            no_route_warning_shown = True
                        client.set_controls(0.0, 0.5, 0.0)
                        client.set_gear(1)
                        client.send_update()
                        frame_number += 1
                        continue

                    no_route_warning_shown = False

                    # --- ACC: 目標速度を同期し、縦制御を実行 ---
                    # ScenarioDriveController が UDP で受信した目標速度を ACC にも反映
                    acc.set_target_speed(controller.target_speed)
                    lon_output = acc.update(ground_truth, dt)

                    # --- Print status ---
                    if frame_number % 20 == 0:
                        speed = controller._last_speed
                        target_spd = controller.target_speed
                        lead = acc.lead_vehicle
                        lead_str = (f"lead: gap={lead.gap_distance:.1f}m, "
                                    f"spd={lead.lead_speed:.1f}m/s"
                                    if lead else "lead: none")
                        print(f"Speed: {speed:.2f}/{target_spd:.2f} m/s | "
                              f"Steer: {steering:.3f} | "
                              f"Thr: {lon_output.throttle:.2f} | "
                              f"Brk: {lon_output.brake:.2f} | "
                              f"{lead_str}")

                    # --- Send Controls ---
                    client.set_controls(lon_output.throttle, lon_output.brake, -steering)
                    client.set_gear(1)
                    client.send_update()

                except Exception as e:
                    print(f"Controller Error: {e}")
                    client.set_controls(0.0, 0.5, 0.0)
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
        controller.close()
        osi_rx.close()
        client.close()
        print("Done.")

    return 0


if __name__ == "__main__":
    sys.exit(main())
