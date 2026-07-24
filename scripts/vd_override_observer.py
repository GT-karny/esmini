"""feature:F7 real-hardware companion — VD override state observer.

Listens on UDP 48202 for GT_VirtualDriverReporter packets emitted by any
running GT_Sim.exe / GT_esminiLib scenario that uses ControllerVirtualDriver
(see GT_esmini/src/io/GT_VirtualDriverReporter.cpp — the reporter is always
initialized when VD activates). Parses the framed JSON (int32 counter +
uint32 datasize + N bytes) and prints one line per MANUAL/AUTO transition:

    [t=12.34] MANUAL (lat)   <-- manual_transition edge
    [t=15.67] AUTO           <-- auto_transition edge (RESUME landed)

Meant to be run in a second console alongside GT_Sim.exe during the G29
verification. No parsing dependencies. Ctrl-C to exit.
"""
from __future__ import annotations

import json
import socket
import struct
import sys

PORT = 48202
HEADER = struct.Struct("<iI")  # counter, datasize (little-endian)


def mode_text(ov: dict) -> str:
    lat = bool(ov.get("lateral"))
    lon = bool(ov.get("longitudinal"))
    if lat and lon:
        return "MANUAL (lat+lon)"
    if lat:
        return "MANUAL (lat)"
    if lon:
        return "MANUAL (lon)"
    return "AUTO"


def main() -> int:
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.bind(("127.0.0.1", PORT))
    except OSError as e:
        print(f"bind udp/{PORT} failed: {e}", file=sys.stderr)
        print("Something else is already listening on 48202 — close it "
              "(likely the Web backend's VD bridge) and retry.", file=sys.stderr)
        return 2
    sock.settimeout(1.0)

    print(f"vd_override_observer: listening on udp/{PORT}. Ctrl-C to stop.")

    last_mode: str | None = None
    frames = 0

    try:
        while True:
            try:
                data, _ = sock.recvfrom(65535)
            except socket.timeout:
                continue

            if len(data) < HEADER.size:
                continue
            counter, size = HEADER.unpack_from(data, 0)
            payload = data[HEADER.size : HEADER.size + size]
            try:
                tel = json.loads(payload.decode("utf-8"))
            except (json.JSONDecodeError, UnicodeDecodeError):
                continue

            frames += 1
            ov = tel.get("override") or {}
            mode = mode_text(ov)
            t = tel.get("sim_time", 0.0)

            # Print on every mode change, and additionally on any
            # single-frame edge (manual_transition/auto_transition) so a
            # brief transient (e.g. RESUME while brake is still pressed) is
            # still visible.
            edge_m = bool(ov.get("manual_transition"))
            edge_a = bool(ov.get("auto_transition"))

            if mode != last_mode:
                print(f"[t={t:7.2f}] {mode}")
                last_mode = mode
            elif edge_m:
                print(f"[t={t:7.2f}] (edge) manual_transition while mode={mode}")
            elif edge_a:
                print(f"[t={t:7.2f}] (edge) auto_transition while mode={mode}")
    except KeyboardInterrupt:
        print(f"\nvd_override_observer: stopped after {frames} frames.")
    finally:
        sock.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
