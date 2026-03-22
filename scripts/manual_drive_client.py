#!/usr/bin/env python3
"""
Racing Wheel Network Client — Reference implementation for sending
PedalSteerCommand to ControllerManualDrive via UDP.

Usage:
    python manual_drive_client.py [--port 9100] [--host 127.0.0.1]

Config required on GT_Sim side (config/manual_drive.json):
    {
        "input_type": "network",
        "input": { "transport_type": "udp", "port": 9100, "level": "pedal_steer" }
    }

Wire format (44 bytes):
    [4B magic 0x50535443][8B steering f64][8B throttle f64]
    [8B brake f64][8B clutch f64][4B gear i32][4B buttons u32]
"""

import argparse
import socket
import struct
import time
import sys

MAGIC = 0x50535443  # "PSTC"
PACK_FORMAT = "<I d d d d i I"  # little-endian: uint32 + 4 doubles + int32 + uint32 = 44 bytes


def send_command(sock, addr, steering, throttle, brake, clutch=0.0, gear=1, buttons=0):
    """Send a PedalSteerCommand packet."""
    data = struct.pack(PACK_FORMAT, MAGIC, steering, throttle, brake, clutch, gear, buttons)
    sock.sendto(data, addr)


def interactive_mode(sock, addr):
    """Keyboard-driven interactive control (simple terminal UI)."""
    print("=== Racing Wheel Network Client (Interactive) ===")
    print("Controls:")
    print("  w/s  : throttle / brake")
    print("  a/d  : steer left / right")
    print("  r    : reverse gear")
    print("  n    : neutral")
    print("  f    : forward gear 1")
    print("  q    : quit")
    print(f"Sending to {addr[0]}:{addr[1]}")
    print()

    steering = 0.0
    throttle = 0.0
    brake = 0.0
    gear = 1

    try:
        import msvcrt  # Windows

        while True:
            if msvcrt.kbhit():
                key = msvcrt.getch().decode("utf-8", errors="ignore").lower()
                if key == "q":
                    break
                elif key == "w":
                    throttle = min(throttle + 0.1, 1.0)
                    brake = 0.0
                elif key == "s":
                    brake = min(brake + 0.1, 1.0)
                    throttle = 0.0
                elif key == "a":
                    steering = max(steering - 0.1, -1.0)
                elif key == "d":
                    steering = min(steering + 0.1, 1.0)
                elif key == "r":
                    gear = -1
                elif key == "n":
                    gear = 0
                elif key == "f":
                    gear = 1
                elif key == " ":
                    steering = 0.0
                    throttle = 0.0
                    brake = 0.0

            # Decay steering toward center
            steering *= 0.95

            send_command(sock, addr, steering, throttle, brake, gear=gear)
            sys.stdout.write(
                f"\r steer={steering:+.2f}  throttle={throttle:.2f}"
                f"  brake={brake:.2f}  gear={gear:+d}    "
            )
            sys.stdout.flush()
            time.sleep(0.02)  # 50 Hz

    except ImportError:
        # Non-Windows: simple loop
        print("(Non-Windows: sending fixed throttle=0.3 for 10 seconds)")
        for _ in range(500):
            send_command(sock, addr, 0.0, 0.3, 0.0, gear=1)
            time.sleep(0.02)


def ramp_test(sock, addr, duration=10.0):
    """Send a throttle ramp from 0 to 1 over the specified duration."""
    print(f"=== Throttle Ramp Test ({duration}s) ===")
    hz = 50
    steps = int(duration * hz)
    for i in range(steps):
        t = i / steps
        throttle = t
        send_command(sock, addr, 0.0, throttle, 0.0, gear=1)
        time.sleep(1.0 / hz)
    print("Done.")


def main():
    parser = argparse.ArgumentParser(description="Racing Wheel Network Client")
    parser.add_argument("--host", default="127.0.0.1", help="Target host (default: 127.0.0.1)")
    parser.add_argument("--port", type=int, default=9100, help="Target port (default: 9100)")
    parser.add_argument("--mode", choices=["interactive", "ramp"], default="interactive",
                        help="Operation mode (default: interactive)")
    parser.add_argument("--duration", type=float, default=10.0, help="Ramp test duration in seconds")
    args = parser.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    addr = (args.host, args.port)

    try:
        if args.mode == "ramp":
            ramp_test(sock, addr, args.duration)
        else:
            interactive_mode(sock, addr)
    except KeyboardInterrupt:
        print("\nStopped.")
    finally:
        sock.close()


if __name__ == "__main__":
    main()
