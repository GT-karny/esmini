"""feature:F7 — headless probe for per-domain control handover (lateral vs longitudinal).

Runs GT_Sim headless on a handover scenario, optionally feeding ManualDrive a
constant steering command over UDP, and reports the metrics that actually
discriminate a working split from a broken one.

WHY THE SPEED RATIO IS THE HEADLINE METRIC
------------------------------------------
A split-domain implementation that merges at the *state* stage (controller A
writes the pose, controller B writes the speed field) produces a vehicle whose
reported speed and actual ground speed disagree — measured at 9.93 m/s reported
against 13.15 m/s travelled, ratio 1.32. The speed column alone looks perfectly
healthy in that run, and OSI publishes the wrong number downstream. So the
acceptance criterion here is not "the speed column looks right", it is

    ratio = |d(position)/dt| / reported_speed  in [0.98, 1.02]

sampled over >= 4 one-second windows. Anything that merges at the command stage
and integrates once keeps this at ~1.00 by construction; anything that merges
two independently-integrated bodies cannot.

WHY A NON-ZERO STEERING SIGNATURE
---------------------------------
With zero input you cannot tell "ManualDrive's steering never reached the road"
apart from "it did, and it was zero". A constant 0.35 normalized steering into
ManualDrive settles the wheel angle at about -0.213 deg; VirtualDriver driving
the same road settles near -0.10 deg. The two are far enough apart to attribute
lateral control from the wheel-angle column alone.

Usage (venv interpreter, absolute path):
  DriverScript/.venv/Scripts/python.exe GT_esmini/test/headless/vd_domain_split_probe.py \
      --scenario resources/xosc/verification/08_handoff/scenario_split_domain_md_vd.xosc \
      --out <dir>/split.csv --udp-steer 0.35
"""

import argparse
import json
import math
import os
import socket
import struct
import subprocess
import sys
import threading
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
GT_SIM = REPO_ROOT / "build" / "GT_esmini" / "Release" / "GT_Sim.exe"
RELEASE_DIR = REPO_ROOT / "build" / "GT_esmini" / "Release"
PYTHON_EMBED = REPO_ROOT / "thirdparty" / "python-embed" / "python-3.12.10-embed-amd64"

MAGIC_PEDAL_STEER = 0x50535443  # "PSTC"


def build_env():
    """GT_esminiLib.dll pulls in SDL2.dll and python312.dll; both dirs must be on PATH.

    ELECTRON_RUN_AS_NODE is inherited from VSCode-hosted shells and makes the
    Electron-bundled GT_Sim.exe misbehave, so it is stripped here.
    """
    env = os.environ.copy()
    env.pop("ELECTRON_RUN_AS_NODE", None)
    env["PATH"] = (
        f"{PYTHON_EMBED}{os.pathsep}{RELEASE_DIR}{os.pathsep}{env.get('PATH', '')}"
    )
    return env


class SteeringFeeder(threading.Thread):
    """Streams a constant PedalSteerCommand at 100 Hz until stopped.

    NetworkInputBridge holds the last value it received, so a single packet
    would in principle do; streaming keeps the input alive regardless of when
    the controller opens its socket relative to process start.
    """

    def __init__(self, steering, throttle=0.0, brake=0.0, port=9100):
        super().__init__(daemon=True)
        self.packet = struct.pack(
            "<Idddd i I",
            MAGIC_PEDAL_STEER,
            float(steering),
            float(throttle),
            float(brake),
            0.0,  # clutch
            1,  # gear
            0,  # buttons
        )
        self.port = port
        self._stop = threading.Event()

    def run(self):
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            while not self._stop.is_set():
                sock.sendto(self.packet, ("127.0.0.1", self.port))
                time.sleep(0.01)
        finally:
            sock.close()

    def stop(self):
        self._stop.set()


def make_variant(source_xosc, dest_xosc, swap_controller_order=False, md_config=None):
    """Derive a probe scenario from a committed one, into an arbitrary directory.

    Two knobs matter for attributing control:
      * swap_controller_order — reverses the two <ObjectController> blocks.
        ScenarioEngine steps controllers in declaration order, so any
        implementation that is accidentally order-dependent shows up as a
        difference between the two orders. A result is only trustworthy if both
        orders pass.
      * md_config — points ManualDrive at a different ConfigFile, e.g. a
        network-input one so a steering signature can be injected. The committed
        assets deliberately use the socket-free stub so they stay CI-safe and
        cannot collide with another process on port 9100.

    Relative filepaths in the source (RoadNetwork LogicFile, model3d) are
    rewritten to absolute paths so the variant can live outside the repo tree.
    """
    source_xosc = Path(source_xosc).resolve()
    dest_xosc = Path(dest_xosc)
    dest_xosc.parent.mkdir(parents=True, exist_ok=True)
    text = source_xosc.read_text(encoding="utf-8")

    # Absolutize every filepath="..."/model3d="..." that is relative.
    import re

    def absolutize(match):
        attr, value = match.group(1), match.group(2)
        if value.startswith("$") or Path(value).is_absolute():
            return match.group(0)
        resolved = (source_xosc.parent / value).resolve()
        return f'{attr}="{resolved.as_posix()}"'

    text = re.sub(r'\b(filepath|model3d)="([^"]+)"', absolutize, text)

    if md_config:
        text = re.sub(
            r'(<Property\s+name="ConfigFile"\s+value=")[^"]+(")',
            rf"\g<1>{md_config}\g<2>",
            text,
        )

    if swap_controller_order:
        blocks = re.findall(
            r"[ \t]*<ObjectController>.*?</ObjectController>\n", text, re.DOTALL
        )
        if len(blocks) != 2:
            raise RuntimeError(
                f"{source_xosc}: expected exactly 2 <ObjectController> blocks, found {len(blocks)}"
            )
        joined = "".join(blocks)
        text = text.replace(joined, blocks[1] + blocks[0])

    dest_xosc.write_text(text, encoding="utf-8")
    return dest_xosc


def run_scenario(
    scenario, out_csv, udp_steer=None, timestep=0.05, extra_args=None, log_path=None
):
    """Run one headless simulation. Returns (returncode, stdout_text)."""
    out_csv = Path(out_csv)
    out_csv.parent.mkdir(parents=True, exist_ok=True)
    if out_csv.exists():
        out_csv.unlink()

    cmd = [
        str(GT_SIM),
        "--osc",
        str(scenario),
        "--headless",
        "--fixed_timestep",
        str(timestep),
        "--csv_logger",
        str(out_csv),
    ]
    if extra_args:
        cmd += list(extra_args)

    feeder = None
    if udp_steer is not None:
        feeder = SteeringFeeder(udp_steer)
        feeder.start()
        # Give the feeder a moment so the very first controller Poll() already
        # has a value to hold, rather than one frame of implicit zero.
        time.sleep(0.2)

    try:
        # encoding must be pinned: on a JP Windows host text=True decodes as
        # cp932 and dies on esmini's UTF-8 log output before the run's own
        # result is ever reported.
        proc = subprocess.run(
            cmd,
            cwd=str(REPO_ROOT),
            env=build_env(),
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=300,
        )
    finally:
        if feeder:
            feeder.stop()

    combined = (proc.stdout or "") + (proc.stderr or "")
    if log_path:
        Path(log_path).write_text(
            (proc.stdout or "") + "\n===== STDERR =====\n" + (proc.stderr or ""),
            encoding="utf-8",
        )
    return proc.returncode, combined


def assert_config_loaded(log_text, expected_config, expected_input_type):
    """Fail loudly when ManualDrive did not load the config the run assumed.

    ConfigLoader resolves to <exe_dir>/../config/, which CMake populates at
    BUILD time — a config file added to the source tree after the last build is
    simply absent there. ManualDrive then logs a warning and silently continues
    on built-in defaults, whose input_type is sdl2_wheel. The run still produces
    a full csv, so without this check the harness happily measures a controller
    it never configured (and, worse, one that would reach for a real wheel).
    """
    if f"Failed to open" in log_text and expected_config in log_text:
        raise RuntimeError(
            f"ManualDrive could not open {expected_config} and fell back to defaults. "
            f"Copy GT_esmini/config/{expected_config} to build/GT_esmini/config/ (or rebuild)."
        )
    marker = f"ManualDriveController: Created (input={expected_input_type}"
    if marker not in log_text:
        created = [
            ln for ln in log_text.splitlines() if "ManualDriveController: Created" in ln
        ]
        raise RuntimeError(
            f"expected ManualDrive input={expected_input_type}, got: {created or '<no Created line>'}"
        )


def read_csv(path):
    """Parse an esmini csv_logger file into a list of per-frame dicts for entity #1.

    The first three lines are a free-form preamble (build version, scenario
    name, vehicle count); the header is line 4.
    """
    rows = []
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        lines = fh.read().splitlines()
    header_idx = None
    for i, line in enumerate(lines[:10]):
        if line.startswith("Index"):
            header_idx = i
            break
    if header_idx is None:
        raise RuntimeError(f"{path}: no csv header found")

    cols = [c.strip() for c in lines[header_idx].split(",")]
    for line in lines[header_idx + 1 :]:
        if not line.strip():
            continue
        vals = [v.strip() for v in line.split(",")]
        rec = dict(zip(cols, vals))

        def num(key, default=math.nan):
            try:
                return float(rec.get(key, ""))
            except (TypeError, ValueError):
                return default

        rows.append(
            {
                "t": num("TimeStamp [s]"),
                "speed": num("#1 Current_Speed [m/s]"),
                "wheel_angle": num("#1 Wheel_Angle [deg]"),
                "x": num("#1 World_Position_X [m]"),
                "y": num("#1 World_Position_Y [m]"),
                "lane_id": num("#1 lane_id"),
                "lane_offset": num("#1 lane_offset[m]", num("#1 lane_offset [m]")),
            }
        )
    return rows


def speed_consistency(rows, window=1.0, t_from=None, t_to=None):
    """Ratio of travelled-distance rate to reported speed, per `window` seconds.

    This is the metric that exposes a state-stage merge. Windows whose reported
    speed averages below 0.5 m/s are skipped: the ratio is meaningless there and
    would swamp the result with divide-by-almost-zero noise.
    """
    rows = [r for r in rows if not math.isnan(r["t"])]
    if t_from is not None:
        rows = [r for r in rows if r["t"] >= t_from]
    if t_to is not None:
        rows = [r for r in rows if r["t"] <= t_to]

    out = []
    if not rows:
        return out

    start = 0
    while start < len(rows) - 1:
        t0 = rows[start]["t"]
        end = start
        while end < len(rows) - 1 and rows[end]["t"] - t0 < window:
            end += 1
        if end == start:
            break
        dt = rows[end]["t"] - t0
        if dt <= 0:
            start = end
            continue
        dist = math.hypot(
            rows[end]["x"] - rows[start]["x"], rows[end]["y"] - rows[start]["y"]
        )
        seg = rows[start : end + 1]
        mean_reported = sum(r["speed"] for r in seg) / len(seg)
        if mean_reported > 0.5:
            out.append(
                {
                    "t_start": round(t0, 3),
                    "t_end": round(rows[end]["t"], 3),
                    "travelled_speed": round(dist / dt, 4),
                    "reported_speed": round(mean_reported, 4),
                    "ratio": round((dist / dt) / mean_reported, 4),
                }
            )
        start = end
    return out


def summarize(rows, label, tail_seconds=2.0):
    """Condense one run into the numbers a reviewer needs to judge it."""
    finite = [r for r in rows if not math.isnan(r["t"])]
    t_end = finite[-1]["t"] if finite else 0.0
    tail = [r for r in finite if r["t"] >= t_end - tail_seconds]

    windows = speed_consistency(finite)
    ratios = [w["ratio"] for w in windows]

    def mean(vals):
        vals = [v for v in vals if not math.isnan(v)]
        return round(sum(vals) / len(vals), 4) if vals else None

    return {
        "label": label,
        "frames": len(finite),
        "t_end": round(t_end, 3),
        "speed_windows": windows,
        "ratio_min": round(min(ratios), 4) if ratios else None,
        "ratio_max": round(max(ratios), 4) if ratios else None,
        "ratio_window_count": len(windows),
        "ratio_within_tolerance": (
            all(0.98 <= r <= 1.02 for r in ratios) and len(ratios) >= 4
            if ratios
            else False
        ),
        "wheel_angle_tail_mean": mean([r["wheel_angle"] for r in tail]),
        "wheel_angle_tail_min": (
            round(min(r["wheel_angle"] for r in tail), 4) if tail else None
        ),
        "wheel_angle_tail_max": (
            round(max(r["wheel_angle"] for r in tail), 4) if tail else None
        ),
        "speed_tail_mean": mean([r["speed"] for r in tail]),
        "lane_offset_tail_mean": mean([r["lane_offset"] for r in tail]),
        "lane_offset_abs_max": (
            round(
                max(
                    abs(r["lane_offset"])
                    for r in finite
                    if not math.isnan(r["lane_offset"])
                ),
                4,
            )
            if any(not math.isnan(r["lane_offset"]) for r in finite)
            else None
        ),
        "lane_id_tail": tail[-1]["lane_id"] if tail else None,
    }


def csv_body_identical(path_a, path_b):
    """Compare two csv_logger outputs ignoring the preamble.

    Line 1 carries the esmini build version and line 2 the scenario path, so a
    byte-compare of the whole file would flag differences that are not
    behavioural. Everything from the header row down must match exactly.
    """

    def body(path):
        lines = Path(path).read_text(encoding="utf-8", errors="replace").splitlines()
        for i, line in enumerate(lines):
            if line.startswith("Index"):
                return lines[i:]
        return lines

    a, b = body(path_a), body(path_b)
    if len(a) != len(b):
        return False, f"line count {len(a)} != {len(b)}"
    for i, (la, lb) in enumerate(zip(a, b)):
        if la != lb:
            return False, f"first difference at body line {i}: {la!r} != {lb!r}"
    return True, f"{len(a)} lines identical"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--scenario", required=True)
    ap.add_argument("--out", required=True, help="csv output path")
    ap.add_argument("--udp-steer", type=float, default=None)
    ap.add_argument("--timestep", type=float, default=0.05)
    ap.add_argument("--label", default=None)
    ap.add_argument("--log", default=None, help="write GT_Sim stdout/stderr here")
    ap.add_argument("--json-out", default=None)
    args = ap.parse_args()

    rc, out = run_scenario(
        args.scenario,
        args.out,
        udp_steer=args.udp_steer,
        timestep=args.timestep,
        log_path=args.log,
    )
    if rc != 0:
        print(f"GT_Sim exited {rc}", file=sys.stderr)
        print(out[-4000:], file=sys.stderr)
        return rc

    rows = read_csv(args.out)
    summary = summarize(rows, args.label or Path(args.scenario).stem)
    text = json.dumps(summary, indent=2)
    print(text)
    if args.json_out:
        Path(args.json_out).write_text(text, encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
