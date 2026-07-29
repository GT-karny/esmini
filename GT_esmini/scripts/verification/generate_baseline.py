#!/usr/bin/env python
"""Generate an OSI baseline trace for a scenario run with the Default controller.

This is the Step-1 baseline-generator *skeleton* of the VirtualDriver verification
environment. It runs GT_Sim headless on a scenario and records the OSI GroundTruth
stream (entity positions/poses/velocities - the "base information" per the
verification design) to ``groundtruth.osi``.

The output is written in esmini's length-delimited ``.osi`` trace format
(``[uint32 size][serialized GroundTruth]`` per frame), so it is directly readable
by ``scripts/osi2csv.py`` and ``scripts/osiviewer.py``.

VirtualDriver-specific telemetry (planner/driver-model snapshots) is a *separate*
channel added in Step 2 (V0) once session A finalises the telemetry struct - this
script intentionally records only the standard OSI base.

GT_Sim emits OSI over UDP (``--osi <ip>``, default port 48198); we bind the
receiver first, launch GT_Sim, then reassemble and persist each frame.

Run via the project venv (system Python is not allowed)::

    DriverScript/.venv/Scripts/python.exe \
        GT_esmini/scripts/verification/generate_baseline.py \
        resources/xosc/verification/01_vehicle_model/straight_constant_speed.xosc
"""

from __future__ import annotations

import argparse
import json
import socket
import struct
import subprocess
import sys
import time
from pathlib import Path

# .../esmini  (this file is GT_esmini/scripts/verification/generate_baseline.py)
REPO_ROOT = Path(__file__).resolve().parents[3]

DEFAULT_EXE = REPO_ROOT / "build" / "GT_esmini" / "Release" / "GT_Sim.exe"
DEFAULT_PY_EMBED = (
    REPO_ROOT / "thirdparty" / "python-embed" / "python-3.12.10-embed-amd64"
)
DEFAULT_OUT_ROOT = REPO_ROOT / "results" / "baselines"
OSI_UDP_PORT = 48198
OSI_BUFFER_SIZE = 8208  # max OSI UDP payload + 8-byte header (contract with esmini)


def _git_commit() -> str:
    try:
        out = subprocess.run(
            ["git", "rev-parse", "--short", "HEAD"],
            cwd=str(REPO_ROOT),
            capture_output=True,
            text=True,
            timeout=10,
        )
        return out.stdout.strip() if out.returncode == 0 else ""
    except Exception:
        return ""


def _build_env(py_embed: Path) -> dict:
    """GT_Sim needs the embedded python312.dll and the Release dir on PATH,
    otherwise it fails to load with 0xC0000135 (see common-pitfalls memory)."""
    import os

    env = dict(os.environ)
    extra = [str(py_embed), str(DEFAULT_EXE.parent)]
    env["PATH"] = os.pathsep.join(extra + [env.get("PATH", "")])
    return env


def _capture_osi(
    out_osi: Path, proc: subprocess.Popen, port: int, idle_timeout: float
) -> int:
    """Reassemble multi-packet GroundTruth frames from UDP and write them to a
    length-delimited .osi file. Stops once GT_Sim has exited and the stream has
    been idle for ``idle_timeout`` seconds."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1 << 20)
    sock.bind(("127.0.0.1", port))
    sock.settimeout(0.5)

    frames = 0
    complete = b""
    next_index = 1
    last_data = time.time()

    try:
        with open(out_osi, "wb") as f:
            while True:
                try:
                    msg, _ = sock.recvfrom(OSI_BUFFER_SIZE)
                except socket.timeout:
                    if (
                        proc.poll() is not None
                        and (time.time() - last_data) > idle_timeout
                    ):
                        break
                    continue

                last_data = time.time()
                if len(msg) < 8:
                    continue
                counter, size = struct.unpack("iI", msg[:8])
                frame = msg[8:]

                if counter == 1:  # new message
                    complete = b""
                    next_index = 1

                if counter == 1 or abs(counter) == next_index:
                    complete += frame
                    next_index += 1
                    if counter < 0:  # negative counter = final packet
                        f.write(struct.pack("I", len(complete)))
                        f.write(complete)
                        frames += 1
                        complete = b""
                        next_index = 1
                else:
                    next_index = 1  # out of sync, reset
    finally:
        sock.close()

    return frames


def generate(
    xosc: Path,
    out_dir: Path,
    exe: Path,
    py_embed: Path,
    hz: float,
    fast: bool,
    port: int,
    timeout: float,
) -> dict:
    out_dir.mkdir(parents=True, exist_ok=True)
    out_osi = out_dir / "groundtruth.osi"

    cmd = [
        str(exe),
        "--osc",
        str(xosc),
        "--headless",
        "--osi",
        "127.0.0.1",
        "--hz",
        str(hz),
    ]
    if fast:
        cmd.append("--no_realtime")

    stdout_path = out_dir / "stdout.txt"
    stderr_path = out_dir / "stderr.txt"

    print(f"[baseline] scenario : {xosc}")
    print(f"[baseline] exe      : {exe}")
    print(f"[baseline] out      : {out_osi}")
    print(f"[baseline] cmd      : {' '.join(cmd)}")

    start = time.time()
    with open(stdout_path, "w") as so, open(stderr_path, "w") as se:
        proc = subprocess.Popen(
            cmd,
            cwd=str(REPO_ROOT),
            env=_build_env(py_embed),
            stdout=so,
            stderr=se,
        )
        frames = _capture_osi(out_osi, proc, port, timeout)
        exit_code = proc.wait()
    duration = time.time() - start

    meta = {
        "scenario": (
            str(xosc.relative_to(REPO_ROOT)) if xosc.is_absolute() else str(xosc)
        ),
        "controller": "Default",
        "exe": str(exe),
        "hz": hz,
        "fast": fast,
        "frames": frames,
        "exit_code": exit_code,
        "duration_s": round(duration, 2),
        "commit": _git_commit(),
        "osi_file": out_osi.name,
    }
    (out_dir / "meta.json").write_text(json.dumps(meta, indent=2), encoding="utf-8")

    print(f"[baseline] frames   : {frames}")
    print(f"[baseline] exit     : {exit_code}  ({duration:.1f}s)")
    if frames == 0:
        print(
            "[baseline] WARNING: no OSI frames captured - check the --osi port "
            "and that GT_Sim emitted OSI.",
            file=sys.stderr,
        )
    return meta


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    p.add_argument("scenario", type=Path, help="path to the .xosc scenario")
    p.add_argument(
        "--out",
        type=Path,
        default=None,
        help="output dir (default: results/baselines/<scenario-stem>/)",
    )
    p.add_argument("--exe", type=Path, default=DEFAULT_EXE, help="GT_Sim.exe path")
    p.add_argument(
        "--py-embed",
        type=Path,
        default=DEFAULT_PY_EMBED,
        help="embedded python dir (for python312.dll on PATH)",
    )
    p.add_argument("--hz", type=float, default=100.0, help="simulation frequency")
    p.add_argument(
        "--fast",
        action="store_true",
        help="run with --no_realtime (faster, but may drop UDP frames)",
    )
    p.add_argument(
        "--port", type=int, default=OSI_UDP_PORT, help="OSI UDP port to bind"
    )
    p.add_argument(
        "--timeout",
        type=float,
        default=3.0,
        help="idle seconds after GT_Sim exit before stopping capture",
    )
    args = p.parse_args(argv)

    xosc = args.scenario.resolve()
    if not xosc.is_file():
        print(f"ERROR: scenario not found: {xosc}", file=sys.stderr)
        return 2
    if not args.exe.is_file():
        print(
            f"ERROR: GT_Sim.exe not found: {args.exe} (build Protocol A first)",
            file=sys.stderr,
        )
        return 2

    out_dir = args.out or (DEFAULT_OUT_ROOT / xosc.stem)
    meta = generate(
        xosc,
        out_dir.resolve(),
        args.exe.resolve(),
        args.py_embed.resolve(),
        args.hz,
        args.fast,
        args.port,
        args.timeout,
    )
    return 0 if meta["frames"] > 0 and meta["exit_code"] == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
