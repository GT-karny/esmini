"""feature:F7 — 逆構成（横=VirtualDriver / 縦=ManualDrive）のヘッドレス確認。

実機に出す前に、次の3つを実行で確かめるためのもの:

  1. 逆構成そのものが成立するか
       AI がカーブに追従して車線内を走り、人（ここでは UDP）のペダルで加減速できる。
       前回の構成とは**積分器が逆**（縦の所有者=ManualDrive が積分する）なので、
       コマンドバスの未検証だった向きを通すことになる。
  2. カーブ途中で止めたときに舵が保持されるか（停止時舵振れ修正の確認）
  3. 再発進で車線内に留まるか

ペダルは UDP で入れる（実機では sdl2_wheel に差し替わるだけで、経路は同じ）。
プロファイル: 惰行 -> ブレーキで停止 -> 停止保持 -> 再加速。

使い方:
  DriverScript/.venv/Scripts/python.exe GT_esmini/test/headless/vd_reverse_split_probe.py --outdir <dir>
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

sys.path.insert(0, str(Path(__file__).resolve().parent))

from vd_domain_split_probe import (  # noqa: E402
    GT_SIM,
    REPO_ROOT,
    MAGIC_PEDAL_STEER,
    assert_config_loaded,
    build_env,
    make_variant,
    read_csv,
)

SCENARIO = (
    REPO_ROOT
    / "resources"
    / "xosc"
    / "verification"
    / "08_handoff"
    / "scenario_realwheel_reverse_split.xosc"
)


class PedalProfileFeeder(threading.Thread):
    """Drive the pedals over UDP on a fixed wall-clock schedule.

    Steering is deliberately held at a large constant (0.5): in this
    configuration ManualDrive does NOT own lateral, so that value must be
    discarded by the command bus. If it ever reached the road the car would
    leave the lane immediately, which makes "steering is ignored" a *positive*
    observation rather than an absence of one.
    """

    def __init__(self, profile, port=9100, steering=0.5):
        super().__init__(daemon=True)
        self.profile = profile  # [(t_from_s, throttle, brake), ...] ascending
        self.port = port
        self.steering = steering
        self._stop = threading.Event()

    def _cmd(self, throttle, brake):
        return struct.pack(
            "<Idddd i I",
            MAGIC_PEDAL_STEER,
            float(self.steering),
            float(throttle),
            float(brake),
            0.0,
            1,
            0,
        )

    def run(self):
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        t0 = time.time()
        try:
            while not self._stop.is_set():
                el = time.time() - t0
                throttle, brake = 0.0, 0.0
                for t_from, th, br in self.profile:
                    if el >= t_from:
                        throttle, brake = th, br
                sock.sendto(self._cmd(throttle, brake), ("127.0.0.1", self.port))
                time.sleep(0.01)
        finally:
            sock.close()

    def stop(self):
        self._stop.set()


# Wall-clock seconds. Headless runs faster than real time, so the sim-time
# mapping is not 1:1 — the profile only has to produce "coast, stop, hold,
# resume" in order, which the verdict then locates in sim time by speed.
DEFAULT_PROFILE = [
    (0.0, 0.12, 0.0),  # gentle throttle: drive long enough to REACH steady cornering
    (6.0, 0.0, 0.6),  # brake to a full stop on the curve
    (10.0, 0.0, 1.0),  # keep the brake on: this is the standstill window
    (16.0, 0.12, 0.0),  # resume, gently
]

# The resume throttle is deliberately gentle. In THIS configuration the AD does
# not own the longitudinal domain, so it cannot slow down for the curve — speed
# is entirely the human's. Flooring it takes the car past the speed at which
# R~49 m can be held at all (14.6 m/s is 4.35 m/s^2 lateral) and it runs wide.
# That is correct behaviour, not a steering failure, so the probe must not drive
# the car into it and then blame the AD.


def run_reverse(
    outdir,
    name="reverse_split",
    md_config="manual_drive_headless_udp.json",
    timestep=0.05,
    profile=None,
):
    outdir = Path(outdir)
    outdir.mkdir(parents=True, exist_ok=True)
    variant = make_variant(SCENARIO, outdir / f"{name}.xosc", md_config=md_config)
    # The committed scenario runs 180 s so a human has time to drive it. GT_Sim
    # headless advances in REAL time, so keeping that here would make the probe
    # a three-minute wait — and the pedal profile below is finished long before.
    # Shorten it for the automated run only; the committed asset is untouched.
    text = variant.read_text(encoding="utf-8")
    text = text.replace(
        '<SimulationTimeCondition value="180" rule="greaterThan"/>',
        '<SimulationTimeCondition value="30" rule="greaterThan"/>',
    )
    variant.write_text(text, encoding="utf-8")
    csv_path = outdir / f"{name}.csv"
    tel_path = outdir / f"{name}.jsonl"
    for p in (csv_path, tel_path):
        if p.exists():
            p.unlink()

    env = build_env()
    env["GT_VD_TELEMETRY_JSONL"] = str(tel_path)

    feeder = PedalProfileFeeder(profile or DEFAULT_PROFILE)
    feeder.start()
    time.sleep(0.2)
    try:
        proc = subprocess.run(
            [
                str(GT_SIM),
                "--osc",
                str(variant),
                "--headless",
                "--fixed_timestep",
                str(timestep),
                "--csv_logger",
                str(csv_path),
            ],
            cwd=str(REPO_ROOT),
            env=env,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=300,
        )
    finally:
        feeder.stop()

    log_text = (proc.stdout or "") + (proc.stderr or "")
    (outdir / f"{name}.log").write_text(log_text, encoding="utf-8")
    if proc.returncode != 0:
        raise RuntimeError(f"GT_Sim exit {proc.returncode}")
    assert_config_loaded(log_text, md_config, "network")
    return csv_path, log_text


def verdict(csv_path, log_text):
    """Judge the three questions this probe exists to answer."""
    rows = [r for r in read_csv(csv_path) if not math.isnan(r["t"])]
    checks = []

    # --- ownership: the ledger must put lateral on VD and make MD the integrator
    want = "lat=VirtualDriverController lon=ManualDriveController integrator=ManualDriveController"
    checks.append(
        (
            "台帳が逆構成になっている",
            want in log_text,
            want if want in log_text else "見つからない",
        )
    )

    moving = [r for r in rows if r["speed"] > 3.0]
    stopped = [r for r in rows if r["speed"] < 0.10]

    # --- 1. lateral is the AD's: it tracks the lane while moving, despite the
    #        human holding a large constant steering input that must be ignored.
    if moving:
        worst = max(
            abs(r["lane_offset"]) for r in moving if not math.isnan(r["lane_offset"])
        )
        checks.append(
            (
                "走行中は AI がカーブに追従（|lane_offset| <= 1.0m）",
                worst <= 1.0,
                f"max |lane_offset| (走行中) = {worst:.3f} m",
            )
        )
    else:
        checks.append(("走行区間が存在する", False, "speed > 3.0 のフレームが無い"))

    # --- 2. longitudinal is the human's: the pedals actually stopped the car
    checks.append(
        (
            "人のブレーキで完全停止した",
            len(stopped) > 10,
            f"speed < 0.10 のフレーム数 = {len(stopped)}",
        )
    )

    # --- 3. steering is HELD at standstill (the fix under test)
    if moving and stopped:
        # Reference = STEADY cornering, not the last moments before the stop.
        # Braking is a transient: the steering is still settling there, so using
        # it as the baseline compares the standstill value against a number that
        # is itself not the correct cornering angle (measured -0.0177 rad mid-brake
        # against a true cornering angle of ~-0.06).
        t_stop = stopped[0]["t"]
        pre = [r["wheel_angle"] for r in rows if r["t"] < t_stop and r["speed"] > 5.0]
        at = [r["wheel_angle"] for r in stopped]
        if pre:
            c, s = sum(pre) / len(pre), sum(at) / len(at)
            checks.append(
                (
                    "停止しても舵が符号を保つ",
                    (c * s) > 0,
                    f"停止直前 {c:+.4f} rad -> 停止中 {s:+.4f} rad",
                )
            )
            ratio = abs(s) / max(abs(c), 1e-6)
            checks.append(
                (
                    "停止中の舵が旋回中の2倍以内",
                    ratio <= 2.0,
                    f"|停止中|/|停止直前| = {ratio:.2f}",
                )
            )
        else:
            checks.append(("停止直前の旋回舵角が取れる", False, "該当フレーム無し"))

    # --- 4. resume keeps the car in lane
    worst_all = max(
        (abs(r["lane_offset"]) for r in rows if not math.isnan(r["lane_offset"])),
        default=0.0,
    )
    checks.append(
        (
            "全区間で車線内（|lane_offset| <= 1.75m）",
            worst_all <= 1.75,
            f"max |lane_offset| = {worst_all:.3f} m",
        )
    )
    return checks


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--outdir", required=True)
    ap.add_argument("--name", default="reverse_split")
    args = ap.parse_args()

    csv_path, log_text = run_reverse(args.outdir, name=args.name)

    rows = [r for r in read_csv(csv_path) if not math.isnan(r["t"])]
    print(f"{'t':>7}{'speed':>8}{'wheel':>10}{'lane_off':>10}{'lane':>6}")
    for r in rows[::20]:
        print(
            f"{r['t']:>7.2f}{r['speed']:>8.3f}{r['wheel_angle']:>10.4f}"
            f"{r['lane_offset']:>10.3f}{r['lane_id']:>6.0f}"
        )

    print("\n----- verdict -----")
    ok_all = True
    for label, ok, detail in verdict(csv_path, log_text):
        print(f"  [{'PASS' if ok else 'FAIL'}] {label} — {detail}")
        ok_all = ok_all and ok
    print(f"\n==== OVERALL: {'PASS' if ok_all else 'FAIL'} ====")
    return 0 if ok_all else 1


if __name__ == "__main__":
    sys.exit(main())
