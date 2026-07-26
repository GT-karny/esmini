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
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
IN_ROOT = REPO_ROOT / "test_results" / "f7_jerk_cap_binding_check"

SCENARIOS = ("basic", "right_turn", "tljunction")
CAPS = (0.0, 10.0, 25.0, 50.0)


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
    print("feature:F7  躍度上限を有効にして通常運転を実際に拘束するかの実測")
    print("=" * 100)
    print("入力: ヘッドレス閉ループ、dt=0.01、snap上限は常に無効、jerk上限を0/10/25/50で掃引。")
    print("GT_esmini/config/virtual_driver.json は不変更（per-run一時ConfigFile注入のみ）。")
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
