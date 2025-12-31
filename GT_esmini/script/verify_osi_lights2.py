#!/usr/bin/env python3
"""
OSI Light State Verification Script (esmini UDP driver compatible)

- Uses esmini/scripts/udp_driver/udp_osi_common.py's OSIReceiver to receive OSI GroundTruth.
- Prints startup banner immediately and surfaces any import/path errors.
- Runs indefinitely until Ctrl+C.
"""

import os
import sys
import argparse
import traceback


def resolve_esmini_paths():
    """
    Resolve paths based on your repository layout:
      E:\Repository\GT_esmini\esmini\GT_esmini\script\verify_osi_lights2.py
    """
    this_dir = os.path.dirname(os.path.abspath(__file__))
    gt_esmini_dir = os.path.dirname(this_dir)      # ...\GT_esmini
    esmini_root = os.path.dirname(gt_esmini_dir)   # ...\esmini

    udp_driver_path = os.path.join(esmini_root, "scripts", "udp_driver")
    scripts_path = os.path.join(esmini_root, "scripts")

    return esmini_root, scripts_path, udp_driver_path


def _enum_name(enum_type, value: int) -> str:
    try:
        return enum_type.Name(value)
    except Exception:
        return f"UNKNOWN({value})"


def print_vehicle_light_state(obj) -> bool:
    obj_id = getattr(obj.id, "value", None)

    if not obj.HasField("vehicle_classification"):
        print(f"  Vehicle ID {obj_id}: No vehicle_classification field")
        return False

    vc = obj.vehicle_classification
    if not vc.HasField("light_state"):
        print(f"  Vehicle ID {obj_id}: No light_state field")
        return False

    ls = vc.light_state

    # Try to access nested enums; if not available, just print raw ints
    try:
        brake_enum = ls.BrakeLightState
        indicator_enum = ls.IndicatorState
        generic_enum = ls.GenericLightState
    except Exception:
        brake_enum = indicator_enum = generic_enum = None

    brake_name = _enum_name(brake_enum, ls.brake_light_state) if brake_enum else str(ls.brake_light_state)
    ind_name = _enum_name(indicator_enum, ls.indicator_state) if indicator_enum else str(ls.indicator_state)

    def gen_name(v: int) -> str:
        return _enum_name(generic_enum, v) if generic_enum else str(v)

    print(f"  Vehicle ID {obj_id}:")
    print(f"    Brake Light:     {ls.brake_light_state} ({brake_name})")
    print(f"    Indicator:       {ls.indicator_state} ({ind_name})")
    print(f"    Reversing Light: {ls.reversing_light} ({gen_name(ls.reversing_light)})")
    print(f"    Head Light:      {ls.head_light} ({gen_name(ls.head_light)})")
    print(f"    High Beam:       {ls.high_beam} ({gen_name(ls.high_beam)})")
    print(f"    Front Fog:       {ls.front_fog_light} ({gen_name(ls.front_fog_light)})")
    print(f"    Rear Fog:        {ls.rear_fog_light} ({gen_name(ls.rear_fog_light)})")

    return True


def run(host: str, port: int, timeout_s: float) -> int:
    # Startup banner (must always print)
    print("verify_osi_lights2.py: starting...", flush=True)

    esmini_root, scripts_path, udp_driver_path = resolve_esmini_paths()
    print(f"  esmini_root      = {esmini_root}", flush=True)
    print(f"  scripts_path     = {scripts_path}", flush=True)
    print(f"  udp_driver_path  = {udp_driver_path}", flush=True)

    # Add paths
    sys.path.insert(0, udp_driver_path)
    sys.path.insert(0, scripts_path)

    # Import here to surface errors after we printed paths
    try:
        from udp_osi_common import OSIReceiver, timeout  # noqa: E402
    except Exception as e:
        print("\nERROR: failed to import udp_osi_common / OSIReceiver", flush=True)
        print(f"  {type(e).__name__}: {e}", flush=True)
        traceback.print_exc()
        return 2

    print(f"\nListening for OSI messages via OSIReceiver on {host}:{port}", flush=True)
    print(f"Waiting for data... (timeout: {timeout_s}s)", flush=True)
    print("-" * 80, flush=True)

    receiver = OSIReceiver()


    message_count = 0
    light_state_found = False

    try:
        while True:
            try:
                msg = receiver.receive()
            except timeout:
                print(f"Timeout after {timeout_s}s (no data yet). Continue waiting...", flush=True)
                continue

            message_count += 1

            ts = 0.0
            if hasattr(msg, "HasField") and msg.HasField("timestamp"):
                ts = msg.timestamp.seconds + msg.timestamp.nanos * 1e-9

            print(f"\n[Message #{message_count}] Time: {ts:.3f}s", flush=True)

            for obj in getattr(msg, "moving_object", []):
                try:
                    is_vehicle = (obj.type == obj.TYPE_VEHICLE)
                except Exception:
                    is_vehicle = (obj.type == 2)  # fallback

                if not is_vehicle:
                    continue

                if print_vehicle_light_state(obj):
                    light_state_found = True

            if not light_state_found and message_count == 1:
                print("  Warning: No light state data found in first message", flush=True)

    except KeyboardInterrupt:
        print("\nInterrupted by user", flush=True)
    finally:
        receiver.close()

    # Ctrl+C 時にここへ来る。light_state を見つけたかで終了コードを返す。
    return 0 if light_state_found else 1


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Verify OSI vehicle light_state output via UDP (esmini OSIReceiver)")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=48198)
    parser.add_argument("--timeout", type=float, default=30.0)
    args = parser.parse_args()

    sys.exit(run(args.host, args.port, args.timeout))
