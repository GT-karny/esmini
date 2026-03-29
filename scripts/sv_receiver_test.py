#!/usr/bin/env python3
"""Multicast receiver for GT_Sim Scenario Variables (SV) stream.

Joins the multicast group 239.0.0.1:48201 and prints received JSON payloads.
Usage:
    python scripts/sv_receiver_test.py [--group 239.0.0.1] [--port 48201]
"""

from __future__ import annotations

import argparse
import json
import socket
import struct
import sys


def main() -> None:
    parser = argparse.ArgumentParser(description="SV multicast receiver test")
    parser.add_argument("--group", default="239.0.0.1", help="Multicast group (default: 239.0.0.1)")
    parser.add_argument("--port", type=int, default=48201, help="Multicast port (default: 48201)")
    args = parser.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(("", args.port))

    # Join multicast group
    mreq = struct.pack("4s4s", socket.inet_aton(args.group), socket.inet_aton("0.0.0.0"))
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)

    print(f"Listening on multicast {args.group}:{args.port}  (Ctrl+C to stop)")
    print("-" * 60)

    try:
        while True:
            data, addr = sock.recvfrom(65535)
            try:
                payload = json.loads(data)
                sim_time = payload.get("sim_time", "?")
                variables = payload.get("variables", {})
                parts = [f"  {k} = {v!r}" for k, v in variables.items()]
                print(f"[t={sim_time}] from {addr[0]}:{addr[1]}")
                for p in parts:
                    print(p)
            except (json.JSONDecodeError, UnicodeDecodeError):
                print(f"[raw] {len(data)} bytes from {addr[0]}:{addr[1]}")
    except KeyboardInterrupt:
        print("\nStopped.")
    finally:
        sock.close()


if __name__ == "__main__":
    main()
