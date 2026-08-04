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
    "left_turn": ROOT / "resources" / "xosc" / "verification" / "05_anticipation"
                 / "decelerate_for_left_turn.xosc",
    "tljunction": ROOT / "resources" / "xosc" / "verification" / "05_anticipation"
                  / "traffic_lights_junction.xosc",
}

# telemetry の固定9桁シリアライズ1量子。曲率差がこれ未満なら計測器の分解能以下。
KAPPA_EPS_ABS = 1e-9


def kappa_ratio_published(frame: dict):
    """|kappa_out| / kappa_limit を **包絡線が publish した値だけ** から作る。

    以前の実装は比の両辺をここで組み立てていた——左辺は固定ホイールベース定数からの
    `tan(steer_out*max_steer_angle)/wheel_base`、右辺は shipped 定数と `ego.speed` からの
    `min(a_lat_max/v^2, yaw_max/v)`。製品は wheel_base を `boundingbox.length*0.6` で導き、
    クランプは車両の積分**前**に走る一方 `ego.speed` は積分**後**に記録されるので、
    両方とも系統的にずれていた（0.88% の幻の超過の正体）。`kappa_out`/`kappa_limit` の
    publish 後は推測が一切要らない。

    返り値は `(ratio, excess)`、publish されていないフレームは `(None, None)`。
    **未評価は未評価として返す**——推測での穴埋めをしない。
    """
    env = frame.get("envelope") or {}
    k_out, k_lim = env.get("kappa_out"), env.get("kappa_limit")
    if not (isinstance(k_out, (int, float)) and isinstance(k_lim, (int, float))) or k_lim <= 0.0:
        return None, None
    return abs(k_out) / k_lim, abs(k_out) - k_lim


def _write_cfg_jerk(tmpdir: str, jerk_cap: float, tag: str) -> str:
    """base(shipped) config + jerk_cap のみ明示的に上書き（shipped defaultが0に変わったため
    既定に頼らない）。他は一切変えない。steer_snap_maxはコードから撤去済み。"""
    with open(BASE_CFG, encoding="utf-8") as f:
        base = json.load(f)
    base["input_type"] = "headless_ffb"
    base["ffb_target_track_enabled"] = True
    base["ad_steering_envelope_steer_jerk_max"] = jerk_cap
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


def run_one(scenario_path: Path, jerk_cap: float, plant_extra: dict, tmpdir: str, tag: str,
            sname: str = "") -> dict:
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
    # publish された値だけを見る。未評価フレームは黙って 0 埋めせず数える。
    pub = [kappa_ratio_published(f) for f in frames]
    ratios = [r for r, _ in pub if r is not None]
    excesses = [e for _, e in pub if e is not None]
    max_kappa_ratio = max(ratios, default=float("nan"))
    max_kappa_excess = max(excesses, default=float("nan"))

    return dict(n=len(frames), dt_actual=frames[1]["sim_time"] - frames[0]["sim_time"] if len(frames) > 1 else None,
                residual_peak=max(residuals), residual_threshold=float(frames[0]["ffb"]["gates"].get("residual_threshold", 0.08)),
                sustain_max=max(sustains), jerk_active_n=jerk_active_n, jerk_active_frac=jerk_active_n / len(frames),
                max_abs_jerk_out=max_abs_jerk_out, latched=latched, max_kappa_ratio=max_kappa_ratio,
                max_kappa_excess=max_kappa_excess, n_kappa_unevaluated=len(frames) - len(ratios))


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
                    r = run_one(spath, cap, extra, tmpdir, tag, sname=sname)
                    r["variant"] = extra
                    per_variant.append(r)
                results[sname][cap] = per_variant

        # --- 決定性の抜き打ち確認: 同一設定を2回走らせて残差ピークが一致するか ---
        tag_a = "determinism_A"
        tag_b = "determinism_B"
        rA = run_one(SCENARIOS["tljunction"], 25.0, PLANT_VARIANTS[1][1], tmpdir, tag_a, sname="tljunction")
        rB = run_one(SCENARIOS["tljunction"], 25.0, PLANT_VARIANTS[1][1], tmpdir, tag_b, sname="tljunction")
        determinism_match = (rA["residual_peak"] == rB["residual_peak"] and rA["n"] == rB["n"])

        # --- 自己検証: cap=0発火率0%は致命的ゲート。jerk上限クリップ超過は診断情報として
        # 報告する（曲率安全再クランプがjerk窓の外でsteer_outを動かせるため、cap超過自体が
        # 実システムの挙動でありうる — f7_jerk_cap_binding_check.py で確認済み）。
        print("[自己検証]")
        cap0_ok = True
        for sname in SCENARIOS:
            for vi, r in enumerate(results[sname][0.0]):
                if r["jerk_active_n"] != 0:
                    cap0_ok = False
                    print(f"  NG(fatal): {sname} cap=0 variant{vi}: steer_jerk_active "
                          f"{r['jerk_active_n']}/{r['n']} (0のはず)")
        if not cap0_ok:
            print("  cap=0で発火が0でない=設定不適用の疑い。致命的、ここで打ち切る。")
            return 1
        cap0_total = sum(r["n"] for sname in SCENARIOS for r in results[sname][0.0])
        cap0_active = sum(r["jerk_active_n"] for sname in SCENARIOS for r in results[sname][0.0])
        print(f"  cap=0: steer_jerk_active {cap0_active}/{cap0_total} フレーム (0.00%) — OK")

        jerk_ng = []
        for cap in (25.0, 50.0):
            tol = max(1e-3, cap * 1e-4)
            for sname in SCENARIOS:
                for vi, r in enumerate(results[sname][cap]):
                    if r["max_abs_jerk_out"] > cap + tol:
                        jerk_ng.append((sname, cap, vi, r["max_abs_jerk_out"]))
        if jerk_ng:
            print(f"  NG(非致命・報告対象): jerk(steer_out)がcapを超過: {jerk_ng}")
        else:
            print("  cap=25/50: 全variantでmax|jerk(steer_out)| <= cap — OK")
        self_check_ok = cap0_ok  # cap0のみを致命ゲートとする

        noise_abs = abs(rA["residual_peak"] - rB["residual_peak"])
        noise_rel = noise_abs / rA["residual_peak"] * 100.0 if rA["residual_peak"] else float("nan")
        print(f"  [情報・非ゲート] 決定性抜き打ち確認(tljunction cap=25 variant1を2回実行): "
              f"peak_A={rA['residual_peak']:.9f} peak_B={rB['residual_peak']:.9f} "
              f"完全一致={'YES' if determinism_match else 'NO'}  "
              f"差={noise_abs:.9f}({noise_rel:.4f}%) — これを測定ノイズ床の目安として使う")
        print()

        print("[kappa安全チェック] |kappa_out|/kappa_limit の最大値（全27セル=3走行x3cap"
              "x3プラント変種の中の最悪値。恒久的に<=1のはず）")
        print("  **両辺とも包絡線が publish した値**。ホイールベース・速度サンプル・"
              "タイミングはこの比較に入らない。判定は比ではなく超過(1/m)で行う"
              "——比が 1.000000 では上限に張り付いた状態と本当の超過を見分けられない。")
        print(f"  {'run':<12}{'cap=0':>12}{'cap=25':>12}{'cap=50':>12}")
        kappa_violations = []
        n_uneval = 0
        for sname in SCENARIOS:
            row = []
            for cap in JERK_CAPS:
                cells = results[sname][cap]
                n_uneval += sum(r.get("n_kappa_unevaluated", 0) for r in cells)
                mx = max(r["max_kappa_ratio"] for r in cells)
                ex = max(r["max_kappa_excess"] for r in cells)
                row.append(mx)
                if ex > KAPPA_EPS_ABS:
                    kappa_violations.append((sname, cap, mx, ex))
            print(f"  {sname:<12}" + "".join(f"{v:>12.6f}" for v in row))
        if n_uneval:
            print(f"  注意: {n_uneval} フレームは kappa_out/kappa_limit 未publish のため"
                  "**未評価**として除外（推測での代用はしない）。")
        if kappa_violations:
            print(f"  => 超過あり: {kappa_violations} — 修正が不完全である可能性。期待に合わせず報告する。")
        else:
            print(f"  => 全27セルで超過 0（しきい {KAPPA_EPS_ABS:g} 1/m = telemetry 1量子）。")
        print()

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

        # --- VERDICT -----------------------------------------------------
        #
        # feature:F7 — this swept the jerk cap against the residual detector
        # and returned 0 whatever it found, including a hands-off run that
        # LATCHED. The whole question it exists to answer is whether raising
        # the steering-jerk cap pushes the residual into the latch, so a latch
        # here is the finding, not a footnote.
        #
        # Every run in this sweep is hands-off (no driver is injected), so any
        # latch is a false positive by construction. Margin is reported too:
        # a cap that does not latch but leaves margin < 1.0 has already lost,
        # it just has not been caught yet.
        latched_cells = []
        thin_margin = []
        evaluated = 0
        for sname in SCENARIOS:
            for cap in JERK_CAPS:
                per_variant = results.get(sname, {}).get(cap)
                if not per_variant:
                    continue
                evaluated += 1
                worst_residual = max(r["residual_peak"] for r in per_variant)
                thr = per_variant[0]["residual_threshold"]
                margin = thr / worst_residual if worst_residual > 0 else float("inf")
                if any(r["latched"] for r in per_variant):
                    latched_cells.append((sname, cap))
                elif margin < 1.0:
                    thin_margin.append((sname, cap, margin))

        print("=== VERDICT ===")
        if evaluated == 0:
            print("RESULT: NOT MEASURED — no cells produced results.")
            rc = 2
        elif latched_cells:
            print(f"RESULT: FAIL — {len(latched_cells)} hands-off cell(s) latched MANUAL "
                  f"(false positive by construction): "
                  + ", ".join(f"{s}@cap={c:g}" for s, c in latched_cells))
            rc = 1
        elif thin_margin:
            print(f"RESULT: FAIL — {len(thin_margin)} cell(s) did not latch but sit at or "
                  f"past the threshold: "
                  + ", ".join(f"{s}@cap={c:g} margin={m:.3f}" for s, c, m in thin_margin))
            rc = 1
        else:
            print(f"RESULT: PASS — {evaluated} cell(s), no hands-off latch, all margins >= 1.0")
            rc = 0

    finally:
        shutil.rmtree(tmpdir, ignore_errors=True)

    return rc


if __name__ == "__main__":
    sys.exit(main())
