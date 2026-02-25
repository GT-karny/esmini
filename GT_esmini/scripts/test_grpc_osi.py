"""gRPC OSI サーバー動作確認スクリプト.

使い方:
  1. Web バックエンドを起動 (uvicorn)
  2. Web UI またはAPI経由でシミュレーションを開始 (OSI有効)
  3. このスクリプトを実行:

     DriverScript\\.venv\\Scripts\\python.exe GT_esmini/scripts/test_grpc_osi.py

  オプション:
    --port 50051        gRPC サーバーポート (デフォルト: 50051)
    --max-frames 100    受信フレーム数上限 (デフォルト: 100, 0=無制限)
    --timeout 30        接続タイムアウト秒 (デフォルト: 30)
    --hvd               HostVehicleData も並行ストリーミング受信
"""

from __future__ import annotations

import argparse
import asyncio
import math
import sys
from pathlib import Path

# Ensure DriverScript and repo root are on sys.path for osi3 imports
REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT))
sys.path.insert(0, str(REPO_ROOT / "DriverScript"))

import grpc
import grpc.aio
from google.protobuf import empty_pb2

from GT_esmini.web.backend.grpc_gen.service_groundtruth_pb2_grpc import (
    GroundTruthServiceStub,
)
from GT_esmini.web.backend.grpc_gen.service_hostvehicledata_pb2_grpc import (
    HostVehicleDataServiceStub,
)


def format_gt_frame(gt) -> str:
    """Format a GroundTruth message as a compact summary string."""
    ts = gt.timestamp
    sim_time = ts.seconds + ts.nanos * 1e-9

    lines = [f"  t={sim_time:.3f}s  objects={len(gt.moving_object)}"]
    for obj in gt.moving_object:
        pos = obj.base.position
        ori = obj.base.orientation
        vel = obj.base.velocity
        speed = math.sqrt(vel.x**2 + vel.y**2 + vel.z**2)
        lines.append(
            f"    ID={obj.id.value:3d}  "
            f"x={pos.x:9.2f}  y={pos.y:9.2f}  z={pos.z:6.2f}  "
            f"h={ori.yaw:7.4f}  speed={speed:6.2f} m/s"
        )
    return "\n".join(lines)


def format_hvd_frame(hvd) -> str:
    """Format a HostVehicleData message as a compact summary string."""
    lines = []
    if hvd.HasField("location"):
        loc = hvd.location
        if loc.HasField("position"):
            pos = loc.position
            lines.append(f"    pos=({pos.x:.2f}, {pos.y:.2f}, {pos.z:.2f})")
        if loc.HasField("orientation"):
            ori = loc.orientation
            lines.append(f"    ori=(yaw={ori.yaw:.4f}, pitch={ori.pitch:.4f}, roll={ori.roll:.4f})")
    if hvd.HasField("location_rmse"):
        lines.append("    location_rmse present")
    return "\n".join(lines) if lines else "    (no location data)"


async def stream_ground_truth(stub, max_frames: int, timeout: float) -> int:
    """Stream GroundTruth and print each frame. Returns frame count."""
    print("\n[GroundTruth] Waiting for stream...")
    count = 0
    try:
        call = stub.StreamGroundTruth(
            empty_pb2.Empty(),
            timeout=timeout if max_frames == 0 else None,
        )
        async for gt in call:
            count += 1
            print(f"[GT] Frame {count}")
            print(format_gt_frame(gt))

            if max_frames > 0 and count >= max_frames:
                print(f"\n[GT] Reached {max_frames} frames, stopping.")
                call.cancel()
                break
    except grpc.aio.AioRpcError as e:
        code = e.code()
        if code == grpc.StatusCode.UNAVAILABLE:
            print(f"[GT] Server unavailable: {e.details()}")
        elif code == grpc.StatusCode.DEADLINE_EXCEEDED:
            print(f"[GT] Timeout after {timeout}s ({count} frames received)")
        elif code == grpc.StatusCode.CANCELLED:
            pass  # normal when we cancel after max_frames
        else:
            print(f"[GT] RPC error: {code} - {e.details()}")

    return count


async def stream_host_vehicle_data(stub, max_frames: int, timeout: float) -> int:
    """Stream HostVehicleData and print each frame. Returns frame count."""
    print("\n[HostVehicleData] Waiting for stream...")
    count = 0
    try:
        call = stub.StreamHostVehicleData(
            empty_pb2.Empty(),
            timeout=timeout if max_frames == 0 else None,
        )
        async for hvd in call:
            count += 1
            if count <= 5 or count % 20 == 0:
                print(f"[HVD] Frame {count}")
                print(format_hvd_frame(hvd))
            elif count == 6:
                print("[HVD] (showing every 20th frame...)")

            if max_frames > 0 and count >= max_frames:
                print(f"\n[HVD] Reached {max_frames} frames, stopping.")
                call.cancel()
                break
    except grpc.aio.AioRpcError as e:
        code = e.code()
        if code == grpc.StatusCode.UNAVAILABLE:
            print(f"[HVD] Server unavailable: {e.details()}")
        elif code == grpc.StatusCode.DEADLINE_EXCEEDED:
            print(f"[HVD] Timeout after {timeout}s ({count} frames received)")
        elif code == grpc.StatusCode.CANCELLED:
            pass  # normal when we cancel after max_frames
        else:
            print(f"[HVD] RPC error: {code} - {e.details()}")

    return count


async def run(args):
    target = f"localhost:{args.port}"
    print(f"Connecting to gRPC OSI server at {target} ...")

    channel = grpc.aio.insecure_channel(target)

    # Quick connectivity check
    try:
        await asyncio.wait_for(channel.channel_ready(), timeout=5)
        print(f"[OK] Connected to {target}")
    except asyncio.TimeoutError:
        print(f"[FAIL] Cannot connect to {target} within 5s.")
        print("       Is the web backend running with a simulation active?")
        await channel.close()
        sys.exit(1)

    # Run GT and HVD streams concurrently
    gt_stub = GroundTruthServiceStub(channel)
    tasks = [stream_ground_truth(gt_stub, args.max_frames, args.timeout)]

    if args.hvd:
        hvd_stub = HostVehicleDataServiceStub(channel)
        tasks.append(stream_host_vehicle_data(hvd_stub, args.max_frames, args.timeout))

    results = await asyncio.gather(*tasks)

    await channel.close()

    gt_count = results[0]
    hvd_count = results[1] if args.hvd else 0

    print("\n" + "=" * 50)
    print(f"  GroundTruth frames received: {gt_count}")
    if args.hvd:
        print(f"  HostVehicleData frames received: {hvd_count}")
    print("=" * 50)

    if gt_count == 0:
        print("\n[WARN] No frames received. Possible causes:")
        print("  - No simulation is currently running")
        print("  - OSI output is disabled in execution config")
        print("  - The OSI bridge failed to start")
        sys.exit(1)
    else:
        print("\n[OK] gRPC OSI streaming is working correctly.")


def main():
    parser = argparse.ArgumentParser(description="gRPC OSI server test client")
    parser.add_argument("--port", type=int, default=50051, help="gRPC server port")
    parser.add_argument("--max-frames", type=int, default=100, help="Max frames to receive (0=unlimited)")
    parser.add_argument("--timeout", type=float, default=30.0, help="Connection timeout in seconds")
    parser.add_argument("--hvd", action="store_true", help="Also stream HostVehicleData (concurrent)")
    args = parser.parse_args()

    asyncio.run(run(args))


if __name__ == "__main__":
    main()
