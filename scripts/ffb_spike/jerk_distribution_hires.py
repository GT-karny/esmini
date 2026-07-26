#!/usr/bin/env python
"""feature:F7 — 通常運転の指令躍度分布を9桁ヘッドレスログで再導出する（分析専用）。

## 入力

`GT_esmini/test/headless/f7_jerk_distribution_hires.py` が生成した
`test_results/f7_jerk_distribution_hires/{basic,right_turn,tljunction}/telemetry.jsonl`
（9桁固定小数、dt=0.01、jerk/snap上限を両方無効化した閉ループヘッドレス実行）。

`VirtualDriverController` はシナリオ終了後も deactivate するだけで
`GT_GetVirtualDriverTelemetry` は最後の有効フレームを凍結して返し続ける（None を返さない）ため、
`gt_sim_test.py run()` の "no-telemetry grace" 終了検出が働かず、末尾に同一 sim_time の
重複フレームが大量に付く。本スクリプトは **sim_time が単調増加でなくなった最初の位置で
切り詰める**（実測: basic 2001/4500, right_turn 2501/4500, tljunction 3501/4500 フレームが実区間）。

## 比較対象（4桁 vs 9桁を同じ表に混ぜない）

実機ログ（4桁固定小数、`test_results/f7_realwheel_frozen_20260726_1616/` 相当の凍結コピー）の
`|jerk(steer_in)|` 分布は `jerk_window_and_ratelimiter_source.py` の Q2 出力（本セッションで
既出）から転記する。列を分けて表示し、混同しない。

## 使い方

    python jerk_distribution_hires.py
"""

from __future__ import annotations

import io
import json
import math
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
HIRES_DIR = REPO_ROOT / "test_results" / "f7_jerk_distribution_hires"
REALWHEEL_FROZEN = Path(
    "C:/Users/Owner/AppData/Local/Temp/claude/E--Repository-GT-esmini-esmini/"
    "9ebc3c1f-5918-4120-b6b2-bdeadc8c3686/scratchpad/f7_frozen"
)

SCENARIOS = ("basic", "right_turn", "tljunction")

# 旧計器(4桁, quantum=1.0/s^2)の |jerk(steer_in)| 分布。jerk_window_and_ratelimiter_source.py の
# Q2 出力から転記（本セッションで実測済み、ここでは再計算しない）。
OLD_4DIGIT_JERK_IN = {
    "basic":      dict(median=0.00, p95=1.00, p99=1.00, max=422.00),
    "right_turn": dict(median=0.00, p95=1.00, p99=2.00, max=698.00),
    "tljunction": dict(median=0.00, p95=1.00, p99=2.00, max=698.00),
}


def load_jsonl(path: Path) -> list[dict]:
    rows = []
    for line in io.open(path, encoding="utf-8"):
        line = line.strip()
        if line:
            rows.append(json.loads(line))
    return rows


def truncate_at_freeze(rows: list[dict]) -> list[dict]:
    """VirtualDriverController deactivate後の凍結重複フレームを切り落とす:
    sim_time が単調増加でなくなった最初の位置で打ち切る。"""
    for i in range(1, len(rows)):
        if rows[i]["sim_time"] <= rows[i - 1]["sim_time"]:
            return rows[:i]
    return rows


def build_series(rows: list[dict]) -> list[dict]:
    S = []
    for r in rows:
        env = r.get("envelope", {})
        S.append(dict(t=float(r["sim_time"]), speed=float(r["ego"]["speed"]),
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


def full_dist(xs: list[float]) -> dict:
    if not xs:
        return {k: float("nan") for k in ("median", "p50", "p90", "p95", "p99", "p999", "max")}
    s = sorted(xs)
    return dict(median=percentile(s, 0.5), p50=percentile(s, 0.5), p90=percentile(s, 0.90),
                p95=percentile(s, 0.95), p99=percentile(s, 0.99), p999=percentile(s, 0.999),
                max=s[-1])


def pearson(xs: list[float], ys: list[float]) -> float:
    n = len(xs)
    if n < 3:
        return float("nan")
    mx, my = sum(xs) / n, sum(ys) / n
    sx = math.sqrt(sum((x - mx) ** 2 for x in xs))
    sy = math.sqrt(sum((y - my) ** 2 for y in ys))
    if sx == 0 or sy == 0:
        return float("nan")
    return sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / (sx * sy)


def validate_against_realwheel(name: str, hires_S: list[dict]) -> dict:
    """headless閉ループ実行が対応する実機ログと同じAD挙動を再現しているかを、
    速度プロファイルの相関と総走行距離で確認する（前提を検証してから使う）。"""
    real_path = REALWHEEL_FROZEN / f"f7_realwheel_{name}.jsonl"
    if not real_path.exists():
        return dict(available=False)
    real_rows = load_jsonl(real_path)
    real_speed = [float(r["ego"]["speed"]) for r in real_rows]
    hires_speed = [s["speed"] for s in hires_S]
    n = min(len(real_speed), len(hires_speed))
    corr = pearson(real_speed[:n], hires_speed[:n])
    dist_real = sum(real_speed[i] * 0.01 for i in range(len(real_speed)))
    dist_hires = sum(hires_speed[i] * 0.01 for i in range(len(hires_speed)))
    return dict(available=True, n_real=len(real_speed), n_hires=len(hires_speed),
                speed_corr=corr, dist_real=dist_real, dist_hires=dist_hires,
                dist_pct_diff=(dist_hires - dist_real) / dist_real * 100.0 if dist_real else float("nan"))


def log_hist(values: list[float], n_bins: int = 40) -> tuple[list[float], list[int], int]:
    """|jerk| の対数ビンヒストグラム。0はビン化できないので別カウントする。
    戻り値: (bin_edges(log10), counts, n_zero)"""
    nonzero = [v for v in values if v > 0]
    n_zero = len(values) - len(nonzero)
    if not nonzero:
        return [], [], n_zero
    lo, hi = math.log10(min(nonzero)), math.log10(max(nonzero))
    if hi <= lo:
        hi = lo + 1e-6
    width = (hi - lo) / n_bins
    counts = [0] * n_bins
    for v in nonzero:
        idx = min(n_bins - 1, int((math.log10(v) - lo) / width))
        counts[idx] += 1
    edges = [lo + i * width for i in range(n_bins + 1)]
    return edges, counts, n_zero


def find_valley(edges: list[float], counts: list[int]) -> dict:
    """countsの中で「山→谷→山」になっている箇所を探す（単純な局所最小探索、
    両側に自分より大きいビンがあることを条件にする）。無ければ無しと報告。"""
    n = len(counts)
    candidates = []
    for i in range(1, n - 1):
        if counts[i] == 0:
            continue
        left_max = max(counts[:i]) if i > 0 else 0
        right_max = max(counts[i + 1:]) if i < n - 1 else 0
        if left_max > counts[i] and right_max > counts[i]:
            candidates.append((counts[i], i))
    if not candidates:
        return dict(found=False)
    # 最も深い(相対的に低い)谷を採用
    _, idx = min(candidates, key=lambda c: c[0])
    return dict(found=True, bin_idx=idx, count=counts[idx],
                jerk_lo=10 ** edges[idx], jerk_hi=10 ** edges[idx + 1])


def analyse(name: str) -> dict:
    path = HIRES_DIR / name / "telemetry.jsonl"
    rows = load_jsonl(path)
    n_total = len(rows)
    rows = truncate_at_freeze(rows)
    S = build_series(rows)

    jerk_in = [abs(s["jerk_steer_in"]) for s in S]
    jerk_out = [abs(s["jerk_steer_out"]) for s in S]
    dist_in = full_dist(jerk_in)
    dist_out = full_dist(jerk_out)
    edges, counts, n_zero = log_hist(jerk_in)
    valley = find_valley(edges, counts)
    jerk_active_frac = sum(1 for s in S if s["jerk_active"]) / len(S) if S else float("nan")

    val = validate_against_realwheel(name, S)

    return dict(name=name, n_total=n_total, n_trunc=len(S), duration=S[-1]["t"] if S else 0.0,
                dist_in=dist_in, dist_out=dist_out, jerk_active_frac=jerk_active_frac,
                edges=edges, counts=counts, n_zero=n_zero, valley=valley, validation=val)


def print_report(analyses: list[dict]) -> None:
    print("=" * 100)
    print("feature:F7  通常運転の指令躍度分布 — 9桁ヘッドレスログ再導出")
    print("=" * 100)
    print("入力: ヘッドレス閉ループ、dt=0.01、jerk/snap上限を両方無効化（循環回避）。")
    print("GT_esmini/config/virtual_driver.json は不変更（per-run一時ConfigFile注入のみ）。")
    print()

    print("[検証] ヘッドレス再現 vs 実機ログ（採用前の前提確認）")
    for a in analyses:
        v = a["validation"]
        if not v.get("available"):
            print(f"  {a['name']}: 実機ログ比較対象なし")
            continue
        print(f"  {a['name']}: n_real={v['n_real']} n_hires={v['n_hires']}  "
              f"speed相関r={v['speed_corr']:+.4f}  "
              f"距離 実機={v['dist_real']:.2f}m headless={v['dist_hires']:.2f}m "
              f"差={v['dist_pct_diff']:+.2f}%")
    print()

    print("[新規: 9桁ヘッドレス] |jerk(steer_in)| 分布  (median/p50/p90/p95/p99/p99.9/max)")
    all_in: list[float] = []
    for a in analyses:
        d = a["dist_in"]
        print(f"  {a['name']:<12} n={a['n_trunc']:>5} ({a['duration']:.2f}s, "
              f"末尾凍結重複除去 {a['n_total']}->{a['n_trunc']})")
        print(f"    {d['median']:.6f} / {d['p50']:.6f} / {d['p90']:.6f} / {d['p95']:.6f} / "
              f"{d['p99']:.6f} / {d['p999']:.6f} / {d['max']:.6f}")
    print()

    print("[参考: 9桁ヘッドレス] |jerk(steer_out)| 分布（包絡線が何をしているかの対比。"
          "jerk/snap上限は無効化済みなので差はレート制限器のみの効果）")
    for a in analyses:
        d = a["dist_out"]
        print(f"  {a['name']:<12} {d['median']:.6f} / {d['p50']:.6f} / {d['p90']:.6f} / "
              f"{d['p95']:.6f} / {d['p99']:.6f} / {d['p999']:.6f} / {d['max']:.6f}   "
              f"(steer_jerk_active frac={a['jerk_active_frac']*100:.2f}% — 0のはず、上限無効化の確認)")
    print()

    print("[旧計器(4桁, quantum=1.0/s^2)] |jerk(steer_in)| 分布（実機ログ、本セッション既出の転記）")
    print(f"  {'run':<12} {'median':>8} {'p95':>8} {'p99':>8} {'max':>8}")
    for name in SCENARIOS:
        o = OLD_4DIGIT_JERK_IN[name]
        print(f"  {name:<12} {o['median']:>8.2f} {o['p95']:>8.2f} {o['p99']:>8.2f} {o['max']:>8.2f}")
    print()

    print("[新/旧倍率] p99 と max が何倍ずれたか (9桁 ÷ 4桁、4桁側が0の場合は算出不能)")
    for a in analyses:
        o = OLD_4DIGIT_JERK_IN[a["name"]]
        d = a["dist_in"]
        p99_ratio = (d["p99"] / o["p99"]) if o["p99"] else float("nan")
        max_ratio = (d["max"] / o["max"]) if o["max"] else float("nan")
        print(f"  {a['name']:<12} p99: {d['p99']:.6f}/{o['p99']:.2f} = {p99_ratio:.4f}x   "
              f"max: {d['max']:.6f}/{o['max']:.2f} = {max_ratio:.4f}x")
    print()

    print("[二峰性チェック] |jerk(steer_in)| 対数ビンヒストグラム（0は別カウント）")
    for a in analyses:
        print(f"  --- {a['name']} (0の割合={a['n_zero']/(a['n_zero']+sum(a['counts']))*100:.2f}% "
              f"of non-negative samples) ---")
        edges, counts = a["edges"], a["counts"]
        if not counts:
            print("    (非ゼロ値なし)")
            continue
        cmax = max(counts) or 1
        for i, c in enumerate(counts):
            if c == 0:
                continue
            bar = "#" * max(1, int(c / cmax * 40))
            print(f"    10^{edges[i]:+.2f}..10^{edges[i+1]:+.2f}  n={c:<6} {bar}")
        v = a["valley"]
        if v["found"]:
            print(f"    => 谷候補: {v['jerk_lo']:.4f}..{v['jerk_hi']:.4f} /s^2 (bin count={v['count']})")
        else:
            print("    => 谷なし（このデータでは二峰の谷という値決めの方法論は成立しない）")
    print()


def main() -> int:
    analyses = [analyse(name) for name in SCENARIOS]
    print_report(analyses)
    return 0


if __name__ == "__main__":
    sys.exit(main())
