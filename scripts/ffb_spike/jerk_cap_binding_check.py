#!/usr/bin/env python
"""feature:F7 — 躍度上限を有効にして通常運転を実際に拘束するかを分析する（分析専用）。

入力: `GT_esmini/test/headless/f7_jerk_cap_binding_check.py` が生成した
`test_results/f7_jerk_cap_binding_check/{scenario}/cap_{0,10,25,50}/telemetry.jsonl`
（9桁固定小数、dt=0.01、snap上限は常に無効、jerk上限のみ掃引）。

`f7_jerk_distribution_hires.py` と同じ「末尾凍結重複フレーム」を sim_time 単調増加が
途切れた位置で切り詰める。

## 使い方

    python jerk_cap_binding_check.py
"""

from __future__ import annotations

import io
import json
import math
import os
import sys
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
IN_ROOT = REPO_ROOT / "test_results" / "f7_jerk_cap_binding_check"
DLL_PATH = REPO_ROOT / "build" / "GT_esmini" / "Release" / "GT_esminiLib.dll"

SCENARIOS = ("basic", "right_turn", "tljunction")
CAPS = (0.0, 10.0, 25.0, 50.0)
SAT_TOL = 1e-3  # cap値超過の許容誤差(9桁シリアライズ由来の丸め)

# AdSteeringEnvelope の shipped defaults（config/virtual_driver.json）。
# kappa(steer_out)/kappa_max のセルフチェックに使う。書き換えていないので既定値そのまま。
A_LAT_MAX_STEER = 4.3   # m/s^2
YAW_RATE_MAX = 1.0      # rad/s
V_FLOOR = 1.0           # m/s
MAX_STEER_ANGLE = 0.61  # rad
# wheel_base = bbox.length * 0.6 (ControllerVirtualDriver.cpp:312)。シナリオごとの car_white
# 定義がわずかに違う（basic はカタログ参照 length=5.04、他2つはインライン定義 length=5.0）。
WHEEL_BASE = {"basic": 5.04 * 0.6, "right_turn": 5.0 * 0.6, "tljunction": 5.0 * 0.6}


def load_jsonl(path: Path) -> list[dict]:
    rows = []
    for line in io.open(path, encoding="utf-8"):
        line = line.strip()
        if line:
            rows.append(json.loads(line))
    return rows


def truncate_at_freeze(rows: list[dict]) -> list[dict]:
    for i in range(1, len(rows)):
        if rows[i]["sim_time"] <= rows[i - 1]["sim_time"]:
            return rows[:i]
    return rows


def kappa_ratio(steer_out: float, speed: float, wheel_base: float) -> float:
    """|kappa(steer_out)| / kappa_max。AdSteeringEnvelope.cpp と同じ式
    （kappa_max=min(a_lat_max_steer/v_eff^2, yaw_rate_max/v_eff)、
    kappa_cmd=tan(steer_norm*max_steer_angle)/wheel_base）。修正後はこれが恒久的に
    <=1(丸め程度)であることを確認するためのセルフチェック。

    `speed` は呼び出し側が選ぶ責任を負う。製品 C++（ControllerVirtualDriver.cpp）は
    このフレームの物理積分より前に読んだ速度（= 前フレームの積分後速度）を
    envelope に渡す（AdSteeringEnvelope.cpp:42 の v）。一方 telemetry の
    `ego.speed` はこのフレームの物理積分「後」に記録される
    （ControllerVirtualDriver.cpp:488、step 11）ので、行 i の envelope 計算に
    対応する速度は行 i の `ego.speed` ではなく行 i-1 の `ego.speed` である。"""
    v_eff = max(speed, V_FLOOR)
    kappa_max = min(A_LAT_MAX_STEER / (v_eff ** 2), YAW_RATE_MAX / v_eff)
    delta = steer_out * MAX_STEER_ANGLE
    kappa_out = math.tan(delta) / wheel_base
    return abs(kappa_out) / kappa_max if kappa_max > 0 else float("nan")


def build_series(rows: list[dict], wheel_base: float) -> list[dict]:
    S = []
    for r in rows:
        env = r.get("envelope", {})
        ego = r["ego"]
        S.append(dict(t=float(r["sim_time"]), speed=float(ego["speed"]),
                       x=float(ego["x"]), y=float(ego["y"]),
                       steer_in=float(env.get("steer_in", 0.0)),
                       steer_out=float(env.get("steer_out", 0.0)),
                       jerk_active=bool(env.get("steer_jerk_active", False))))
    n = len(S)
    for i, s in enumerate(S):
        # 同一フレーム版(検算対象・バグ2の疑いがある版): このフレームのtelemetryが
        # 報告する speed をそのまま使う。
        s["kappa_ratio_same"] = kappa_ratio(s["steer_out"], s["speed"], wheel_base)
        # 前フレーム版(製品C++が実際に使う速度に対応): 行0には前行が無いので
        # 自分の値で代用する(注記対象・下のNOTE_ROW0参照)。
        prev_speed = S[i - 1]["speed"] if i > 0 else s["speed"]
        s["kappa_ratio_prev"] = kappa_ratio(s["steer_out"], prev_speed, wheel_base)
        # 後方互換(未使用箇所があれば同一フレーム版を指す)。
        s["kappa_ratio"] = s["kappa_ratio_same"]
    for key in ("steer_in", "steer_out"):
        rate = [0.0] * n
        for i in range(1, n):
            dt = S[i]["t"] - S[i - 1]["t"]
            if dt > 0:
                rate[i] = (S[i][key] - S[i - 1][key]) / dt
        jerk = [0.0] * n
        for i in range(1, n):
            dt = S[i]["t"] - S[i - 1]["t"]
            if dt > 0:
                jerk[i] = (rate[i] - rate[i - 1]) / dt
        for i, s in enumerate(S):
            s[f"rate_{key}"] = rate[i]
            s[f"jerk_{key}"] = jerk[i]
    return S


def percentile(sorted_xs: list[float], p: float) -> float:
    if not sorted_xs:
        return float("nan")
    n = len(sorted_xs)
    k = (n - 1) * p
    f, c = math.floor(k), math.ceil(k)
    if f == c:
        return sorted_xs[int(k)]
    return sorted_xs[f] + (sorted_xs[c] - sorted_xs[f]) * (k - f)


def dist4(xs: list[float]) -> tuple[float, float, float, float]:
    if not xs:
        return (float("nan"),) * 4
    s = sorted(xs)
    return percentile(s, 0.5), percentile(s, 0.95), percentile(s, 0.99), s[-1]


def load_all() -> dict[str, dict[float, list[dict]]]:
    data: dict[str, dict[float, list[dict]]] = {}
    for name in SCENARIOS:
        data[name] = {}
        for cap in CAPS:
            path = IN_ROOT / name / f"cap_{cap:g}" / "telemetry.jsonl"
            rows = truncate_at_freeze(load_jsonl(path))
            data[name][cap] = build_series(rows, WHEEL_BASE[name])
    return data


def trajectory_deviation(baseline: list[dict], other: list[dict]) -> dict:
    n = min(len(baseline), len(other))
    pos_dev = [math.hypot(other[i]["x"] - baseline[i]["x"], other[i]["y"] - baseline[i]["y"])
               for i in range(n)]
    speed_dev = [abs(other[i]["speed"] - baseline[i]["speed"]) for i in range(n)]
    return dict(n=n, n_baseline=len(baseline), n_other=len(other),
                pos_max=max(pos_dev) if pos_dev else float("nan"),
                pos_rms=math.sqrt(sum(d * d for d in pos_dev) / n) if n else float("nan"),
                speed_max=max(speed_dev) if speed_dev else float("nan"),
                speed_rms=math.sqrt(sum(d * d for d in speed_dev) / n) if n else float("nan"))


def main() -> int:
    data = load_all()

    print("=" * 100)
    print("feature:F7  躍度上限を有効にして通常運転を実際に拘束するかの実測（再測定・安全修正後DLL）")
    print("=" * 100)
    dll_mtime = os.path.getmtime(DLL_PATH) if DLL_PATH.exists() else None
    print(f"DLL: {DLL_PATH}  mtime={time.ctime(dll_mtime) if dll_mtime else 'MISSING'}")
    print("入力: ヘッドレス閉ループ、dt=0.01、jerk上限を0/10/25/50で掃引(明示指定、既定0には頼らない)。"
          "steer_snap_maxはコードから撤去済み。")
    print("GT_esmini/config/virtual_driver.json は不変更（per-run一時ConfigFile注入のみ）。")
    print()

    print("[0: 自己検証 — 格子全体（3走行 x 4cap = 12セル）で判定]")
    cap0_ok = True
    for name in SCENARIOS:
        S0 = data[name][0.0]
        n_active0 = sum(1 for s in S0 if s["jerk_active"])
        if n_active0 != 0:
            cap0_ok = False
            print(f"  NG(fatal): {name} cap=0: steer_jerk_active {n_active0}/{len(S0)} (0のはず)")
    if not cap0_ok:
        print("  cap=0 で発火が0でない = 設定が正しく適用されていない可能性。致命的、ここで打ち切る。")
        return 1
    print("  cap=0 発火: 全3走行で0.00% — OK")

    jerk_bound_cells = {}
    for name in SCENARIOS:
        for cap in (10.0, 25.0, 50.0):
            S = data[name][cap]
            max_j = max(abs(s["jerk_steer_out"]) for s in S)
            ok = max_j <= cap + max(1e-3, cap * 1e-4)
            jerk_bound_cells[(name, cap)] = (ok, max_j)
            if not ok:
                print(f"  NG(非致命・報告対象): {name} cap={cap:g}: max|jerk(steer_out)|={max_j:.6f} > cap "
                      f"— 曲率安全再クランプ(修正後の新ロジック)自体がjerk窓の外で jerk(steer_out) を"
                      f"動かせるため、jerk上限だけでは束縛されない。以下の[kappa安全チェック]で該当セルの"
                      f"kappa比を確認せよ。")
    ng_cells = [(n, c) for (n, c), (ok, _) in jerk_bound_cells.items() if not ok]
    ok_cells = [(n, c) for (n, c), (ok, _) in jerk_bound_cells.items() if ok]
    print(f"  jerk(steer_out)<=cap の自己検証: OK={len(ok_cells)}/9セル, NG={len(ng_cells)}/9セル {ng_cells}")
    print("  NGは曲率安全再クランプ(修正後の新ロジック)がjerk窓の外でsteer_outを動かすために起きる"
          "——ツール自身の警告としてそのまま報告する（除外せず、以降の数値は全セル報告する）。")
    print()

    print("[kappa安全チェック] |kappa(steer_out)|/kappa_max の最大値（修正で恒久的に<=1のはず）")
    print("  一次証拠(ControllerVirtualDriver.cpp:298-311,380,488): envelopeに渡る速度は"
          "このフレームの物理積分より前(=前フレームの積分後速度)。telemetryのego.speedは"
          "このフレームの物理積分より後に記録される。よって行iのenvelope計算に対応する速度は"
          "行iのego.speedではなく行i-1のego.speed —— 二版を両方報告する。")
    print("  [同一フレーム版](旧計算・検算対象。行iのenvelope計算に行iのego.speedを充てる=バグ2)")
    print(f"  {'run':<12}" + "".join(f"{'cap='+str(int(c)):>16}" for c in CAPS))
    kappa_violations_same = []
    for name in SCENARIOS:
        vals = []
        for cap in CAPS:
            S = data[name][cap]
            mx = max(s["kappa_ratio_same"] for s in S)
            vals.append(mx)
            if mx > 1.0 + 1e-4:
                kappa_violations_same.append((name, cap, mx))
        print(f"  {name:<12}" + "".join(f"{v:>16.6f}" for v in vals))
    if kappa_violations_same:
        print(f"  => 超過あり（丸め1e-4を超える）: {kappa_violations_same}")
    else:
        print("  => 全セルで1.0+1e-4以内。")
    print()

    print("  [前フレーム版](製品C++の実装に対応する版。行iのenvelope計算に行i-1のego.speedを充てる)")
    print("  注記: 行0(各走行の最初の1フレーム)は前行が無いため自分の値で代用——検算の結論には影響しない"
          "(発火は加速中盤で起きており、行0は非発火区間)。")
    print(f"  {'run':<12}" + "".join(f"{'cap='+str(int(c)):>16}" for c in CAPS))
    kappa_violations_prev = []
    for name in SCENARIOS:
        vals = []
        for cap in CAPS:
            S = data[name][cap]
            mx = max(s["kappa_ratio_prev"] for s in S)
            vals.append(mx)
            if mx > 1.0 + 1e-4:
                kappa_violations_prev.append((name, cap, mx))
        print(f"  {name:<12}" + "".join(f"{v:>16.6f}" for v in vals))
    if kappa_violations_prev:
        print(f"  => 超過あり（丸め1e-4を超える）: {kappa_violations_prev}")
        print("     => 修正が不完全である可能性——期待に合わせず、そのまま報告する。")
    else:
        print("  => 全セルで1.0+1e-4以内。修正後は恒久的に守られていることを確認。")
    print()

    print("  [cap=10 名指し比較] right_turn / tljunction の同一フレーム版 vs 前フレーム版")
    for name in ("right_turn", "tljunction"):
        S = data[name][10.0]
        mx_same = max(s["kappa_ratio_same"] for s in S)
        mx_prev = max(s["kappa_ratio_prev"] for s in S)
        print(f"    {name:<12} 同一フレーム={mx_same:.7f}  前フレーム={mx_prev:.7f}")
    print()

    print("[1: steer_jerk_active 発火数/発火率]  (走行ごと x cap)")
    print(f"  {'run':<12}" + "".join(f"{'cap='+str(int(c)):>16}" for c in CAPS))
    for name in SCENARIOS:
        counts = []
        for cap in CAPS:
            S = data[name][cap]
            n_active = sum(1 for s in S if s["jerk_active"])
            counts.append(f"{n_active}/{len(S)}({n_active/len(S)*100:.2f}%)")
        print(f"  {name:<12}" + "".join(f"{c:>16}" for c in counts))
    print()

    print("[2: cap=25 発火フレーム 上位10件]  (時刻, speed, |jerk(steer_in)|)"
          " — 巡航中か既知の大イベントのみかを判別")
    for name in SCENARIOS:
        S = data[name][25.0]
        active = [s for s in S if s["jerk_active"]]
        print(f"  --- {name}: 発火{len(active)}件 ---")
        top = sorted(active, key=lambda s: -abs(s["jerk_steer_in"]))[:10]
        if not top:
            print("    (発火なし)")
            continue
        for s in top:
            print(f"    t={s['t']:>7.2f}  speed={s['speed']:>6.3f} m/s  "
                  f"|jerk(steer_in)|={abs(s['jerk_steer_in']):>10.3f} /s^2")
    print()

    print("[3: 軌跡への影響] cap=0 を基準にした ego位置/速度の偏差 (max/RMS)")
    print(f"  {'run':<12}{'cap':>6}{'n(base/other)':>16}{'pos_max[m]':>12}{'pos_rms[m]':>12}"
          f"{'speed_max':>12}{'speed_rms':>12}")
    for name in SCENARIOS:
        base = data[name][0.0]
        for cap in CAPS[1:]:
            other = data[name][cap]
            d = trajectory_deviation(base, other)
            n_str = f"{d['n_baseline']}/{d['n_other']}"
            print(f"  {name:<12}{cap:>6.0f}{n_str:>16}"
                  f"{d['pos_max']:>12.4f}{d['pos_rms']:>12.4f}"
                  f"{d['speed_max']:>12.4f}{d['speed_rms']:>12.4f}")
    print()

    print("[4: |jerk(steer_out)| 分布] cap掃引での変化 (median/p95/p99/max)")
    for name in SCENARIOS:
        print(f"  --- {name} ---")
        for cap in CAPS:
            S = data[name][cap]
            d = dist4([abs(s["jerk_steer_out"]) for s in S])
            print(f"    cap={cap:>4.0f}: {d[0]:.6f} / {d[1]:.6f} / {d[2]:.6f} / {d[3]:.6f}")
    print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
