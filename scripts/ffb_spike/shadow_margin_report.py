#!/usr/bin/env python
"""feature:F7 — 実機ログからシャドウ妥当性のマージンを集計する。

## これはモデルの再実装ではない

入力は `GT_VD_TELEMETRY_JSONL` が吐いた**製品テレメトリの毎フレーム記録**である。
`ffb.gates.residual` / `shadow_norm` / `actual_norm` / `effective_force` は
すべて**出荷される `OverrideManager` 自身が計算した値**なので、本スクリプトは
シャドウを一切再実装しない。やるのは区間分類と統計だけである。

以前あった Python 版シャドウ再実装（`shadow_replay.py`）は削除した。
真実源は 1 つ、製品コードだけにする。

## 使い方

    # 走行時（製品側）
    GT_VD_TELEMETRY_JSONL=run.jsonl <GT_Sim を起動>

    # 走行後
    python shadow_margin_report.py --jsonl run.jsonl

## 出す数値

走行を実測軸の動きで区間分類し、各区間で残差の最大 / 平均 / しきい値マージンを出す。

    moving      実測軸が実際に動いている区間（旋回でホイールが回っている）
    stationary  実測軸が静止している区間（手放しで動いていない）

`moving` こそがシャドウの積分が問われる場所であり、静止区間だけの検証では
妥当性の証拠にならない。
"""

from __future__ import annotations

import argparse
import io
import json
import sys
from pathlib import Path


def load(path: Path) -> list[dict]:
    rows = []
    with io.open(path, encoding="utf-8") as fh:
        for ln, line in enumerate(fh, 1):
            line = line.strip()
            if not line:
                continue
            try:
                rows.append(json.loads(line))
            except json.JSONDecodeError:
                print(f"warn: line {ln} is not valid JSON, skipped", file=sys.stderr)
    if not rows:
        raise SystemExit(f"no telemetry frames in {path}")
    return rows


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--jsonl", type=Path, required=True,
                    help="GT_VD_TELEMETRY_JSONL が出力した毎フレーム記録")
    ap.add_argument("--move-eps", type=float, default=0.002,
                    help="区間分類: 1 フレームでこれを超える軸変化を moving とみなす")
    ap.add_argument("--min-margin", type=float, default=5.0,
                    help="この倍率を下回ったら WARN（通っていても忠実度の主張には弱い）")
    args = ap.parse_args()

    frames = load(args.jsonl)

    # servo が動いているフレームだけが判定対象（gates は servo 非活性時ゼロ）。
    active = [f for f in frames
              if f.get("ffb", {}).get("target_active") and f.get("ffb", {}).get("gates")]
    if not active:
        raise SystemExit("no frames with an active FFB servo — was target_track enabled?")

    thr = max(float(f["ffb"]["gates"].get("residual_threshold", 0.0)) for f in active)
    if thr <= 0.0:
        raise SystemExit("residual_threshold missing from telemetry")

    rows = []
    prev_actual = None
    for f in active:
        g = f["ffb"]["gates"]
        actual = float(g.get("actual_norm", 0.0))
        label = "stationary"
        if prev_actual is not None and abs(actual - prev_actual) > args.move_eps:
            label = "moving"
        prev_actual = actual
        rows.append({
            "t":        float(f.get("sim_time", 0.0)),
            "actual":   actual,
            "shadow":   float(g.get("shadow_norm", 0.0)),
            "residual": float(g.get("residual", 0.0)),
            "force":    float(g.get("effective_force", 0.0)),
            "target":   float(f["ffb"].get("target_norm", 0.0)),
            "latched":  bool(f.get("override", {}).get("lateral")),
            "label":    label,
        })

    latches = [r for r in rows if r["latched"]]

    print("=" * 78)
    print("feature:F7  シャドウ妥当性 — 実機ログのマージン集計")
    print("=" * 78)
    print(f"source          : {args.jsonl}")
    print(f"frames          : {len(frames)} total / {len(rows)} with servo active")
    print(f"span            : {rows[0]['t']:.2f}s .. {rows[-1]['t']:.2f}s")
    print(f"latch threshold : residual > {thr}")
    print("NOTE: residual/shadow_norm は製品 OverrideManager の出力そのもの。"
          "本スクリプトはモデルを再実装しない。")
    print()
    print(f"{'segment':<12} {'n':>5} {'residual max':>13} {'residual mean':>14} "
          f"{'margin(x)':>10} {'|f| max':>8} {'|axis| max':>11}")
    print("-" * 78)

    ok = True
    weak = []
    for seg in ("moving", "stationary", "ALL"):
        sub = rows if seg == "ALL" else [r for r in rows if r["label"] == seg]
        if not sub:
            print(f"{seg:<12} {0:>5}   (no samples)")
            if seg == "moving":
                ok = False
                weak.append("moving 区間が 0 サンプル — 動的区間が取れていない。"
                            "これでは妥当性の証拠にならない。")
            continue
        rmax = max(r["residual"] for r in sub)
        rmean = sum(r["residual"] for r in sub) / len(sub)
        fmax = max(abs(r["force"]) for r in sub)
        amax = max(abs(r["actual"]) for r in sub)
        margin = (thr / rmax) if rmax > 0 else float("inf")
        print(f"{seg:<12} {len(sub):>5} {rmax:>13.6f} {rmean:>14.6f} "
              f"{margin:>10.1f} {fmax:>8.4f} {amax:>11.4f}")
        if rmax > thr:
            ok = False
        elif margin < args.min_margin:
            weak.append(f"{seg} 区間のマージンが {margin:.1f}x — "
                        f"{args.min_margin:.0f}x を下回る")

    print()
    if latches:
        ok = False
        print(f"MANUAL ラッチ {len(latches)} 件（手放し走行では 0 でなければならない）:")
        for r in latches[:5]:
            print(f"  t={r['t']:.2f} residual={r['residual']:.4f} shadow={r['shadow']:.4f} "
                  f"actual={r['actual']:.4f} f={r['force']:.4f}")
    else:
        print("MANUAL ラッチ: 0 件")

    for w in weak:
        print(f"WARN: {w}")

    print()
    print(f"RESULT: {'PASS' if ok else 'FAIL'}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
