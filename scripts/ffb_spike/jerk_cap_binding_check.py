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

MAX_STEER_ANGLE = 0.61  # rad — rate/jerk 系列の rad 換算にのみ使う（曲率検査には不要）
KAPPA_EPS_ABS = 1e-9    # telemetry の固定9桁シリアライズ1量子。これ未満は計測器の分解能以下


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


def build_series(rows: list[dict]) -> list[dict]:
    """曲率比は **telemetry が publish した値だけ** で作る（推測計算はしない）。

    以前ここには `kappa_ratio(steer_out, speed, wheel_base)` があり、比の
    **両辺**をハーネス側で組み立てていた: 左辺は固定ホイールベース定数からの
    `tan(steer_norm*max_steer_angle)/wheel_base`、右辺は shipped 定数と
    `ego.speed` からの `min(a_lat_max/v^2, yaw_max/v)`。ところが製品は
    wheel_base を `boundingbox.length*0.6` で導き、クランプは車両を積分する
    **前**に走る一方 `ego.speed` は積分**後**に記録される。この二重の取り違えが
    0.88% の幻の超過を生み、`kappa_ratio_same` / `kappa_ratio_prev`（同一フレーム版と
    前フレーム版）という 2 系列を並記する回避策まで作らせた。

    包絡線が `kappa_out` / `kappa_limit` を publish するようになったので、
    その 2 系列も推測計算も不要になった。**両辺とも製品の値**であり、
    どちらの速度サンプルを充てるかという問いがそもそも消えている。
    publish されていない古いキャプチャは `None`（未評価）にする——推測はしない。
    """
    S = []
    for r in rows:
        env = r.get("envelope", {})
        ego = r["ego"]
        k_out = env.get("kappa_out")
        k_lim = env.get("kappa_limit")
        if isinstance(k_out, (int, float)) and isinstance(k_lim, (int, float)) and k_lim > 0.0:
            ratio = abs(k_out) / k_lim
            excess = abs(k_out) - k_lim
        else:
            ratio = excess = None  # 未評価。0.0 でも nan でもなく「測っていない」
        S.append(dict(t=float(r["sim_time"]), speed=float(ego["speed"]),
                       x=float(ego["x"]), y=float(ego["y"]),
                       steer_in=float(env.get("steer_in", 0.0)),
                       steer_out=float(env.get("steer_out", 0.0)),
                       jerk_active=bool(env.get("steer_jerk_active", False)),
                       kappa_ratio=ratio, kappa_excess=excess))
    n = len(S)
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
            data[name][cap] = build_series(rows)
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

    print("[kappa安全チェック] |kappa_out|/kappa_limit の最大値（恒久的に<=1のはず）")
    print("  **両辺とも包絡線が publish した値**（AdSteeringEnvelope.cpp）。ホイールベースも"
          "速度サンプルもタイミングも、この比較には入らない。")
    print("  以前ここには「同一フレーム版 / 前フレーム版」の2表があった。あれは"
          "ハーネスが速度を推測していた時代の産物で、どちらの版が正しいかという問い自体が"
          "publish によって消えている。超過(1/m)も併記する——比が 1.000000 と出たとき、"
          "上限に張り付いている(=0)のか本当に超えているのかは比では見分けられない。")
    print(f"  {'run':<12}" + "".join(f"{'cap='+str(int(c)):>16}" for c in CAPS))
    kappa_violations = []
    n_unevaluated = 0
    for name in SCENARIOS:
        vals = []
        for cap in CAPS:
            S = data[name][cap]
            rs = [s["kappa_ratio"] for s in S if s["kappa_ratio"] is not None]
            n_unevaluated += len(S) - len(rs)
            if not rs:
                vals.append(None)
                continue
            mx = max(rs)
            ex = max(s["kappa_excess"] for s in S if s["kappa_excess"] is not None)
            vals.append(mx)
            if ex > KAPPA_EPS_ABS:
                kappa_violations.append((name, cap, mx, ex))
        print(f"  {name:<12}" + "".join(
            (f"{'未評価':>16}" if v is None else f"{v:>16.6f}") for v in vals))
    if n_unevaluated:
        print(f"  注意: {n_unevaluated} フレームは kappa_out/kappa_limit を持たない"
              "（publish 前のキャプチャ）——**未評価**として除外した。推測での代用はしない。")
    if kappa_violations:
        print(f"  => 超過あり（{KAPPA_EPS_ABS:g} 1/m 超）: {kappa_violations}")
        print("     => 期待に合わせず、そのまま報告する。")
    else:
        print(f"  => 全セルで超過 0（しきい {KAPPA_EPS_ABS:g} 1/m = telemetry 1量子）。")
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
