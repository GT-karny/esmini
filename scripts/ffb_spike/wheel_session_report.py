#!/usr/bin/env python
"""feature:F7 — 有人セッションの自動判定（ユーザーに記憶も記録も要求しない）。

## 解く問題

検出側の確認（介入 → 復帰 → 再介入 → …）は、人が触らないと作れない。しかし
**両手がハンドルにある人に「何回目が入らなかったか」を覚えさせるのは負荷が高く、
しかも間違える**。走行中の記憶に頼る設計をやめて、全部ログから起こす。

## どうやるか

`GT_VD_TELEMETRY_JSONL` の毎フレーム記録を、**ドライバー自身の AUTO_RESUME 押下**で
区切る。押下は `override.resume_pressed` として毎フレーム出ており、**ラッチが
発火したかどうかに関わらず立つ**（ここが肝。`auto_transition` は MANUAL だった時しか
立たないので、「試したが入らなかった回」が痕跡なく消える）。

区切った各試行について、こちらで判定する:

  * MANUAL に入ったか / 入るまでの時間 / 残差ピーク
  * **どの象限の介入だったか**を実測から分類する:
        (b) 追い越し   同符号かつ |actual| > |target|
        (c) 逆方向     異符号
        (d) センター保持 |actual| ~ 0 かつ AD は切ろうとしている
        (a) 内側で押さえ 同符号かつ |actual| < |target|（今回の本番）
    分類まで機械がやるので、**ユーザーは決められた順番を守らなくてよい**。
    5 種類をやりさえすれば、順不同で構わない。

つまりユーザーに要求するのは「5 種類の介入をやる」「各回のあと RESUME を押す」
の 2 つだけで、記憶も記録も要らない。

## 使い方

    python wheel_session_report.py --jsonl test_results/f7_manual_session.jsonl
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
        for line in fh:
            line = line.strip()
            if line:
                try:
                    rows.append(json.loads(line))
                except json.JSONDecodeError:
                    pass
    if not rows:
        raise SystemExit(f"no telemetry frames in {path}")
    return rows


def classify(frames: list[dict], zero_eps: float) -> str:
    """Which quadrant of the acceptance matrix was this attempt?

    Judged at the frame of PEAK |actual - target| within the attempt, i.e.
    where the driver was most clearly asserting something different from AD.
    """
    best = None
    for f in frames:
        g = f.get("ffb", {}).get("gates", {})
        if not f.get("ffb", {}).get("target_active"):
            continue
        actual = float(g.get("actual_norm", 0.0))
        target = float(f["ffb"].get("target_norm", 0.0))
        d = abs(actual - target)
        if best is None or d > best[0]:
            best = (d, actual, target)
    if best is None:
        return "?(servo never active)"
    _, actual, target = best
    if abs(target) < zero_eps and abs(actual) < zero_eps:
        return "-(no meaningful command or motion)"
    if actual * target < 0.0 and abs(actual) >= zero_eps:
        return "(c) countersteer"
    if abs(actual) < zero_eps:
        return "(d) held at centre"
    if abs(actual) > abs(target):
        return "(b) overtook AD"
    return "(a) held INSIDE AD"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--jsonl", type=Path, required=True)
    ap.add_argument("--zero-eps", type=float, default=0.02,
                    help="|axis| below this counts as 'at centre' (0.02 axis ~ 9 deg)")
    args = ap.parse_args()

    frames = load(args.jsonl)

    # ドライバー自身の RESUME 押下で区切る。押下の「後」から次の押下までが 1 試行。
    boundaries = [i for i, f in enumerate(frames)
                  if f.get("override", {}).get("resume_pressed")]
    attempts = []
    start = 0
    for b in boundaries:
        if b > start:
            attempts.append((start, b))
        start = b + 1
    if start < len(frames) - 1:
        attempts.append((start, len(frames)))

    print("=" * 78)
    print("feature:F7  有人セッション自動判定")
    print("=" * 78)
    print(f"source   : {args.jsonl}")
    print(f"frames   : {len(frames)}   span {frames[0].get('sim_time', 0):.2f}s .. "
          f"{frames[-1].get('sim_time', 0):.2f}s")
    print(f"RESUME 押下 : {len(boundaries)} 回  -> 試行 {len(attempts)} 回に分割")
    print("NOTE: 区切りはドライバー自身の RESUME 押下。ラッチの有無に関わらず記録される"
          "ので、\n      「試したが入らなかった回」も落ちない。")
    print()

    if not attempts:
        print("試行が 1 つも切り出せなかった。RESUME を押していないか、"
              "テレメトリに resume_pressed が無い（古いビルド）。")
        return 1

    hdr = f"{'#':>2} {'window [s]':>16} {'quadrant':<24} {'latched':>8} {'latency':>8} {'residual pk':>12}"
    print(hdr)
    print("-" * len(hdr))

    failed = []
    for n, (i, j) in enumerate(attempts, 1):
        seg = frames[i:j]
        t0 = float(seg[0].get("sim_time", 0.0))
        t1 = float(seg[-1].get("sim_time", 0.0))
        latch_t = None
        rpk = 0.0
        for f in seg:
            g = f.get("ffb", {}).get("gates", {})
            rpk = max(rpk, float(g.get("residual", 0.0)))
            if latch_t is None and f.get("override", {}).get("lateral"):
                latch_t = float(f.get("sim_time", 0.0))
        quad = classify(seg, args.zero_eps)
        lat = f"{latch_t - t0:.2f}s" if latch_t is not None else "-"
        print(f"{n:>2} {t0:7.2f}..{t1:7.2f} {quad:<24} "
              f"{'YES' if latch_t is not None else 'NO':>8} {lat:>8} {rpk:>12.4f}")
        # 「意味のある介入だったのに入らなかった」回だけを失敗として拾う。
        if latch_t is None and not quad.startswith(("-", "?")):
            failed.append((n, quad, rpk))

    print()
    quads = {}
    for n, (i, j) in enumerate(attempts, 1):
        q = classify(frames[i:j], args.zero_eps)
        if not q.startswith(("-", "?")):
            quads.setdefault(q, []).append(n)
    print("象限カバレッジ:")
    for q in ("(a) held INSIDE AD", "(b) overtook AD", "(c) countersteer", "(d) held at centre"):
        got = quads.get(q, [])
        print(f"  {q:<24} {'試行 ' + ','.join(map(str, got)) if got else '**未実施**'}")

    cycles = [n for n, (i, j) in enumerate(attempts, 1)
              if any(f.get("override", {}).get("lateral") for f in frames[i:j])]
    print(f"\n多サイクル: {len(cycles)} 回ラッチ（試行 {','.join(map(str, cycles)) or '-'}）")

    if failed:
        print("\n入らなかった回:")
        for n, q, rpk in failed:
            print(f"  試行 {n}  {q}  残差ピーク {rpk:.4f}（しきい値との比較は "
                  f"shadow_margin_report.py 参照）")
        print("\nRESULT: FAIL")
        return 1

    missing = [q for q in ("(a) held INSIDE AD", "(b) overtook AD",
                           "(c) countersteer", "(d) held at centre") if q not in quads]
    if missing:
        print(f"\nWARN: 未実施の象限がある: {', '.join(missing)}")
        print("RESULT: INCOMPLETE")
        return 2

    print("\nRESULT: PASS（4 象限すべて実施・すべてラッチ）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
