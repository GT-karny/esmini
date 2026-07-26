#!/usr/bin/env python
"""feature:F7 — 躍度上限が「シャドウ残差マージン」を動かすかを合成プラントで測る（分析専用）。

## 前提（必ず報告に明記する事実）

**これは合成プラントであって実機ではない。** 実機は動かせないため、
`scripts/vd_ffb_notouch_parity.py` の force-coupled "plant" 合成ホイール
（HeadlessFfbInput.cpp, breakaway/slope/dead_time/velocity_tau を実機同定のばらつき幅で
振る3点セット）を再利用する。実機の新①判定（マージン2.0倍）の証拠には**ならない**。

## ハーネス選定

`scripts/vd_ffb_notouch_parity.py` の `FOLLOWER_MODES` にある3つの "plant"/"parity" 変種
（breakaway 0.170/0.190/0.210 × slope 3.00/3.35/3.70 × dead_time(theta) 0.030/0.041/0.055 ×
velocity_tau(tau) 0.010/0.018/0.025、実機同定ばらつきから採ったセット）をそのまま再利用する。
理由: (1) 既に閉ループ・force-coupled・残差を読める telemetry を吐く、(2) dt=0.01 固定
（`REAL_MACHINE_DT`、コード注記: 実機3走行すべて実測dt=0.01だったことの反映）、(3) INDEPENDENCE
REQUIREMENT（プラント定数とシャドウ定数を別に保つ）が既にコードで保証されている。
自作の新規runnerは書かず、このモジュールの `_write_cfg`/`_write_variant`/`_run_headless`/
`FOLLOWER_MODES`/`REAL_MACHINE_DT`/`DLL`/`BASE_CFG` を import して再利用する。

## 変えるもの・変えないもの

変える: `ad_steering_envelope_steer_jerk_max` ∈ {0, 25, 50}。
固定: `ad_steering_envelope_steer_snap_max=0`（撤去作業中、掃引の混在を避ける）。
一切変えない: residual_threshold(0.08), sustain_time(0.10), シャドウの theta(0.041)/tau(0.018)/
onset_grace(0.05) — シップ済み `GT_esmini/config/virtual_driver.json` の値をそのまま使う
（このスクリプトは読むだけで書き換えない。per-run 一時ConfigFile注入）。

## 使い方

    python residual_vs_jerkcap.py
"""
from __future__ import annotations

import json
import math
import os
import shutil
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

from vd_ffb_notouch_parity import (  # noqa: E402
    DLL, BASE_CFG, FOLLOWER_MODES, REAL_MACHINE_DT,
    _write_variant, _run_headless,
)

JERK_CAPS = (0.0, 25.0, 50.0)
PLANT_VARIANTS = [(m, extra) for m, cls, extra in FOLLOWER_MODES if m == "plant"]

SCENARIOS = {
    "basic": ROOT / "resources" / "xosc" / "virtual_driver_basic.xosc",
    "right_turn": ROOT / "resources" / "xosc" / "verification" / "05_anticipation"
                  / "decelerate_for_right_turn.xosc",
    "tljunction": ROOT / "resources" / "xosc" / "verification" / "05_anticipation"
                  / "traffic_lights_junction.xosc",
}


def _write_cfg_jerk(tmpdir: str, jerk_cap: float, tag: str) -> str:
    """base(shipped) config + jerk_cap のみ上書き。snap は常に0。他は一切変えない。"""
    with open(BASE_CFG, encoding="utf-8") as f:
        base = json.load(f)
    base["input_type"] = "headless_ffb"
    base["ffb_target_track_enabled"] = True
    base["ad_steering_envelope_steer_jerk_max"] = jerk_cap
    base["ad_steering_envelope_steer_snap_max"] = 0.0
    out = os.path.join(tmpdir, f"vd_config_jerk{jerk_cap:g}_{tag}.json")
    with open(out, "w", encoding="utf-8") as f:
        json.dump(base, f, indent=2, ensure_ascii=False)
    return out


def _set_plant_env(extra: dict) -> None:
    os.environ["GT_HEADLESS_FFB_MODE"] = "plant"
    for var in ("GT_HEADLESS_FFB_FROZEN_AT", "GT_HEADLESS_FFB_LAG_TAU",
                "GT_HEADLESS_FFB_PLANT_BREAKAWAY", "GT_HEADLESS_FFB_PLANT_SLOPE",
                "GT_HEADLESS_FFB_PLANT_DEAD_TIME", "GT_HEADLESS_FFB_PLANT_VELOCITY_TAU"):
        os.environ.pop(var, None)
    os.environ.update(extra)


def _jerk_out_series(frames: list[dict]) -> list[float]:
    n = len(frames)
    steer_out = [float(f.get("envelope", {}).get("steer_out", 0.0)) for f in frames]
    ts = [float(f["sim_time"]) for f in frames]
    rate = [0.0] * n
    for i in range(1, n):
        dt = ts[i] - ts[i - 1]
        if dt > 0:
            rate[i] = (steer_out[i] - steer_out[i - 1]) / dt
    jerk = [0.0] * n
    for i in range(1, n):
        dt = ts[i] - ts[i - 1]
        if dt > 0:
            jerk[i] = (rate[i] - rate[i - 1]) / dt
    return jerk


def run_one(scenario_path: Path, jerk_cap: float, plant_extra: dict, tmpdir: str, tag: str) -> dict:
    cfg = _write_cfg_jerk(tmpdir, jerk_cap, tag)
    variant = _write_variant(str(scenario_path), tmpdir, cfg, tag)
    _set_plant_env(plant_extra)
    frames = _run_headless(DLL, variant, dt=REAL_MACHINE_DT, max_time_s=45.0)
    if not frames:
        return dict(n=0)

    residuals = [float(f["ffb"]["gates"]["residual"]) for f in frames]
    sustains = [float(f["ffb"]["gates"]["sustain_accum"]) for f in frames]
    jerk_active_n = sum(1 for f in frames if f.get("envelope", {}).get("steer_jerk_active"))
    jerk_out = _jerk_out_series(frames)
    max_abs_jerk_out = max((abs(j) for j in jerk_out), default=0.0)
    latched = any(f.get("override", {}).get("lateral") for f in frames)

    return dict(n=len(frames), dt_actual=frames[1]["sim_time"] - frames[0]["sim_time"] if len(frames) > 1 else None,
                residual_peak=max(residuals), residual_threshold=float(frames[0]["ffb"]["gates"].get("residual_threshold", 0.08)),
                sustain_max=max(sustains), jerk_active_n=jerk_active_n, jerk_active_frac=jerk_active_n / len(frames),
                max_abs_jerk_out=max_abs_jerk_out, latched=latched)


def main() -> int:
    dll_mtime = os.path.getmtime(DLL) if os.path.exists(DLL) else None
    import time
    print("=" * 100)
    print("feature:F7  躍度上限のシャドウ残差マージンへの影響（合成プラント、実機ではない）")
    print("=" * 100)
    print(f"DLL: {DLL}  mtime={time.ctime(dll_mtime) if dll_mtime else 'MISSING'}")
    print(f"dt = {REAL_MACHINE_DT} (REAL_MACHINE_DT, 実機3走行と同じ刻み)")
    print("変更: ad_steering_envelope_steer_jerk_max のみ掃引。snap_max=0固定。"
          "residual_threshold/sustain_time/シャドウtheta/tau/onset_graceは一切変更なし(シップ済み値)。")
    print("**注意: 合成プラント+シャドウの閉ループであり実機ではない。実機の新①判定(マージン2.0倍)の証拠にはならない。**")
    print()

    tmpdir = tempfile.mkdtemp(prefix="f7_residual_jerkcap_")
    results: dict[str, dict[float, list[dict]]] = {}
    self_check_ok = True

    try:
        for sname, spath in SCENARIOS.items():
            results[sname] = {}
            for cap in JERK_CAPS:
                per_variant = []
                for vi, (mode, extra) in enumerate(PLANT_VARIANTS):
                    tag = f"{sname}_cap{cap:g}_v{vi}"
                    r = run_one(spath, cap, extra, tmpdir, tag)
                    r["variant"] = extra
                    per_variant.append(r)
                results[sname][cap] = per_variant

        # --- 決定性の抜き打ち確認: 同一設定を2回走らせて残差ピークが一致するか ---
        tag_a = "determinism_A"
        tag_b = "determinism_B"
        rA = run_one(SCENARIOS["tljunction"], 25.0, PLANT_VARIANTS[1][1], tmpdir, tag_a)
        rB = run_one(SCENARIOS["tljunction"], 25.0, PLANT_VARIANTS[1][1], tmpdir, tag_b)
        determinism_match = (rA["residual_peak"] == rB["residual_peak"] and rA["n"] == rB["n"])

        # --- 自己検証 (これが通るまで残差の数値は報告しない方針) ---
        print("[自己検証: これに全て通らない限り残差の数値は使わない]")
        for sname in SCENARIOS:
            for vi, r in enumerate(results[sname][0.0]):
                if r["jerk_active_n"] != 0:
                    self_check_ok = False
                    print(f"  NG: {sname} cap=0 variant{vi}: steer_jerk_active "
                          f"{r['jerk_active_n']}/{r['n']} (0のはず)")
            for cap in (25.0, 50.0):
                # 許容誤差: steer_out は9桁固定小数でシリアライズされ、そこから外部で
                # 有限差分再計算した jerk には量子化起因の丸め誤差が乗る。実測では
                # cap超過が高々2e-5程度だったので、cap比0.01%+1e-3を許容とする
                # （実値の張り付き=クリップ機能の確認にはこれで十分な精度）。
                tol = max(1e-3, cap * 1e-4)
                for vi, r in enumerate(results[sname][cap]):
                    if r["max_abs_jerk_out"] > cap + tol:
                        self_check_ok = False
                        print(f"  NG: {sname} cap={cap} variant{vi}: max|jerk(steer_out)|="
                              f"{r['max_abs_jerk_out']:.6f} > cap+tol({cap+tol:.6f})")
        cap0_total = sum(r["n"] for sname in SCENARIOS for r in results[sname][0.0])
        cap0_active = sum(r["jerk_active_n"] for sname in SCENARIOS for r in results[sname][0.0])
        print(f"  cap=0: steer_jerk_active {cap0_active}/{cap0_total} フレーム (0.00%のはず)")
        for cap in (25.0, 50.0):
            maxes = [r["max_abs_jerk_out"] for sname in SCENARIOS for r in results[sname][cap]]
            print(f"  cap={cap:g}: 全run中の max|jerk(steer_out)| の最大 = {max(maxes):.6f} "
                  f"(<= {cap:g} のはず)")
        noise_abs = abs(rA["residual_peak"] - rB["residual_peak"])
        noise_rel = noise_abs / rA["residual_peak"] * 100.0 if rA["residual_peak"] else float("nan")
        print(f"  [情報・非ゲート] 決定性抜き打ち確認(tljunction cap=25 variant1を2回実行): "
              f"peak_A={rA['residual_peak']:.9f} peak_B={rB['residual_peak']:.9f} "
              f"完全一致={'YES' if determinism_match else 'NO'}  "
              f"差={noise_abs:.9f}({noise_rel:.4f}%) — これを測定ノイズ床の目安として使う")
        print(f"  自己検証(cap=0発火率とjerk上限クリップの2点のみ判定): "
              f"{'PASS' if self_check_ok else 'FAIL — 以下の残差数値は無効扱い'}")
        print()

        if not self_check_ok:
            print("自己検証NGのため残差の数値報告を中止する。")
            return 1

        print("[残差ピーク / sustain_accum 最大 — worst case (3プラント変種中の最悪値)]")
        print(f"  {'run':<12}{'cap':>6}{'residual_peak(worst)':>22}{'margin(thr/peak)':>18}"
              f"{'sustain_max(worst)':>20}{'latch(any variant)':>20}")
        for sname in SCENARIOS:
            for cap in JERK_CAPS:
                per_variant = results[sname][cap]
                worst_residual = max(r["residual_peak"] for r in per_variant)
                worst_sustain = max(r["sustain_max"] for r in per_variant)
                thr = per_variant[0]["residual_threshold"]
                margin = thr / worst_residual if worst_residual > 0 else float("inf")
                any_latch = any(r["latched"] for r in per_variant)
                print(f"  {sname:<12}{cap:>6.0f}{worst_residual:>22.6f}{margin:>18.3f}"
                      f"{worst_sustain:>20.6f}{str(any_latch):>20}")
            print()

        print("[内訳: プラント変種ごとの残差ピーク（中央値で語らない・全点を出す）]")
        for sname in SCENARIOS:
            print(f"  --- {sname} ---")
            for cap in JERK_CAPS:
                vals = ", ".join(f"v{vi}(brk={r['variant']['GT_HEADLESS_FFB_PLANT_BREAKAWAY']})="
                                  f"{r['residual_peak']:.6f}" for vi, r in enumerate(results[sname][cap]))
                print(f"    cap={cap:g}: {vals}")
        print()

    finally:
        shutil.rmtree(tmpdir, ignore_errors=True)

    return 0


if __name__ == "__main__":
    sys.exit(main())
