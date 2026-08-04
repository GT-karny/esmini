"""feature:F7 — 躍度上限を有効にして通常運転を実際に拘束するかを測る（audit 対応）。

## 目的

前回(`f7_jerk_distribution_hires.py`)は上限を無効にして「拘束されない自然な指令分布」を
測った（循環回避のため正しい）。本スクリプトは逆に上限を**有効にして**、
`ad_steering_envelope_steer_jerk_max` in {0, 10, 25, 50} の4点で同じ3シナリオを再実行し、
`envelope.steer_jerk_active` の発火（＝上限が実際に指令をクリップした事実）を直接観測する。
cap=0 は「無効」扱い（AdSteeringEnvelopeConfig: steer_jerk_max<=0 で無効）で、他3条件との
軌跡差分の基準（baseline）にもなる。

## 循環に関する注意

この測定は「上限が通常運転を拘束するか」を見るためのものなので、今回は循環を避ける必要が
ない——むしろ上限を有効にした状態でのAD挙動の変化（軌跡のずれ）自体が測定対象。
`ad_steering_envelope_steer_snap_max` は撤去作業中で、混在を避けるため常に 0（無効）に固定する
（cap掃引の意味を "speed依存の第2項" で壊さないため）。

`GT_esmini/config/virtual_driver.json` は一切書き換えない（per-run 一時ConfigFile注入）。

## 使い方

    DriverScript\\.venv\\Scripts\\python.exe GT_esmini\\test\\headless\\f7_jerk_cap_binding_check.py
"""

from __future__ import annotations

import json
import os
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "GT_esmini" / "scripts" / "verification"))

from gt_sim_test import run, _prepare_policy_xosc, BASE_VD_CONFIG  # noqa: E402

DT = 0.01
MAX_TIME = 45.0
JERK_CAPS = (0.0, 10.0, 25.0, 50.0)

SCENARIOS = {
    "basic": ROOT / "resources" / "xosc" / "virtual_driver_basic.xosc",
    "left_turn": ROOT
    / "resources"
    / "xosc"
    / "verification"
    / "05_anticipation"
    / "decelerate_for_left_turn.xosc",
    "tljunction": ROOT
    / "resources"
    / "xosc"
    / "verification"
    / "05_anticipation"
    / "traffic_lights_junction.xosc",
}

OUT_ROOT = ROOT / "test_results" / "f7_jerk_cap_binding_check"
DLL_PATH = ROOT / "build" / "GT_esmini" / "Release" / "GT_esminiLib.dll"


def _make_config(tmpdir: Path, jerk_cap: float) -> Path:
    """base(shipped) + jerk_capのみ明示的に上書き。shipped defaultが0(無効)に変わったため
    既定に頼らず必ず明示する。steer_snap_maxはコードから撤去済み（設定に無い/効かない）。"""
    base = json.loads(BASE_VD_CONFIG.read_text(encoding="utf-8"))
    base["ad_steering_envelope_steer_jerk_max"] = jerk_cap
    cfg_path = tmpdir / "virtual_driver.json"
    cfg_path.write_text(json.dumps(base, indent=2), encoding="utf-8")
    return cfg_path


def run_one(name: str, xosc_path: Path, jerk_cap: float) -> Path:
    tag = f"cap_{jerk_cap:g}"
    run_dir = OUT_ROOT / name / tag
    tmpdir = Path(tempfile.mkdtemp(prefix=f"f7_capcheck_{name}_{tag}_"))
    cfg_path = _make_config(tmpdir, jerk_cap)
    variant = _prepare_policy_xosc(xosc_path, tmpdir, cfg_path)
    meta = run(variant, run_dir, DT, MAX_TIME, 0, None)
    print(f"[{name}/{tag}] {meta}", file=sys.stderr)
    return run_dir / "telemetry.jsonl"


def main() -> int:
    print(
        f"DLL: {DLL_PATH}  mtime={os.path.getmtime(DLL_PATH) if DLL_PATH.exists() else 'MISSING'}",
        file=sys.stderr,
    )
    OUT_ROOT.mkdir(parents=True, exist_ok=True)
    paths = {}
    for name, xosc_path in SCENARIOS.items():
        if not xosc_path.exists():
            print(f"SKIP {name}: {xosc_path} not found", file=sys.stderr)
            continue
        for cap in JERK_CAPS:
            paths[f"{name}/cap_{cap:g}"] = str(run_one(name, xosc_path, cap))
    print(json.dumps(paths))

    # feature:F7 — exit code, because "it ran" and "it produced what was asked
    # for" are different claims and this used to report 0 for both. It is a
    # DATA-PRODUCING step (it writes one telemetry capture per scenario x cap
    # for downstream analysis), so its verdict is about coverage: every
    # requested cell must exist, or whatever reads these paths later is
    # analysing a hole it cannot see.
    expected = sum(1 for x in SCENARIOS.values() if x.exists()) * len(JERK_CAPS)
    missing = [k for k, v in paths.items() if not os.path.exists(v)]
    if expected == 0:
        print(
            "RESULT: NOT MEASURED — no scenario file was found; nothing was run.",
            file=sys.stderr,
        )
        return 2
    if missing or len(paths) != expected:
        print(
            f"RESULT: FAIL — {len(paths)}/{expected} cells produced, "
            f"{len(missing)} output file(s) missing: {missing[:5]}",
            file=sys.stderr,
        )
        return 1
    print(f"RESULT: PASS — {len(paths)}/{expected} cells produced.", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
