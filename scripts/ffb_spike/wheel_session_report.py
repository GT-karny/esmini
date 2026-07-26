#!/usr/bin/env python
"""feature:F7 — 有人セッションの自動判定（ユーザーへの要求ゼロ）。

## 解く問題

検出側の確認（介入 → 復帰 → 再介入 → …）は人が触らないと作れない。しかし
**両手がハンドルにある人に「何回目が入らなかったか」を覚えさせるのは負荷が高く、
しかも間違える**。走行中の記憶にも操作手順にも頼らず、全部ログから起こす。

## 区切り方 — なぜ RESUME 押下ではなく |actual − target| なのか

最初の版は AUTO_RESUME 押下で区切っていた。これは**探しているバグを隠す**設計だった。

RESUME を押す動機は「MANUAL に入ったから AUTO に戻したい」である。
**内側で押さえてラッチしなかった回には、その動機が無い。**（AD のままなのだから
戻す必要が無い。）ユーザーが最も自然に省略するのはまさにその回で、省略されると
その試行は次の介入と融合して消える。つまり **(a) が発火しないという、今回いちばん
知りたい失敗の証拠が痕跡なく消える**。手順書に「各回のあと必ず押す」と書いても、
唯一の要求が「覚えることは何もない」と併記された中では埋もれる。

そこで**ドライバーの操作そのもの**で区切る: `|actual − target|` が閾値を超えて
継続している区間を 1 介入エピソードとみなす。ヒステリシス付きで、短すぎる区間は捨てる。
ユーザーへの要求はゼロになり、RESUME を押し忘れても、押す順番を変えても、
順不同でやっても結果は変わらない。

RESUME 押下は**補助的な分割点**としてのみ使う（押されていれば、間に手が戻らない
連続 2 回の介入を分離できる）。押下が無くても判定は成立する。

## 閾値の根拠（実測）

力結合プラントの手放し走行（right_turn、820 フレーム）で `|actual − target|` は
**最大 0.0556 / p99 0.0355 / 中央値 0.0123**。これは AD の追従誤差そのもので、
ドライバーは関与していない。一方、介入時の乖離は設計上どの象限でも大きい
（例: AD 0.20 に対し 0.05 で押さえる → 0.15）。
よって enter を **0.08**（実測最大の 1.4 倍）、exit を 0.04 に置けば、
追従誤差を拾わずに介入だけを切り出せる。

## 出すもの

エピソードごとに「区間 / 象限 / ラッチ有無 / ラッチまでの時間 / 残差ピーク」。
象限は実測から分類するので**ユーザーは決められた順番を守らなくてよい**:

    (b) 追い越し   同符号かつ |actual| > |target|
    (c) 逆方向     異符号
    (d) センター保持 |actual| ~ 0 で AD は切ろうとしている
    (a) 内側で押さえ 同符号かつ |actual| < |target|（今回の本番）
"""

from __future__ import annotations

import argparse
import io
import json
import sys
from pathlib import Path

# 未検出帯の真値: kp*x + friction_ff*tanh(x/eps) = shadow_breakaway を満たす x。
# 出荷値 (4.0 / 0.15 / 0.010 / 0.21) で 0.01729 axis-frac ≈ 7.8 deg。
UNDETECTABLE_BAND = 0.01729


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


def deviation(f: dict) -> float | None:
    """|actual - target| for this frame, or None if the servo is not armed."""
    ffb = f.get("ffb", {})
    if not ffb.get("target_active"):
        return None
    g = ffb.get("gates", {})
    return abs(float(g.get("actual_norm", 0.0)) - float(ffb.get("target_norm", 0.0)))


def find_episodes(frames: list[dict], enter: float, exit_: float,
                  min_dur: float) -> list[tuple[int, int]]:
    """|actual-target| がヒステリシス閾値を超えて継続する区間を切り出す。

    RESUME 押下は補助的な分割点としてのみ使う（押されていれば連続 2 回の介入を
    分離できる）。押されていなくてもエピソードは成立する。
    """
    eps = []
    start = None
    for i, f in enumerate(frames):
        d = deviation(f)
        latched = bool(f.get("override", {}).get("lateral"))
        resumed = bool(f.get("override", {}).get("resume_pressed"))
        if start is None:
            if d is not None and d > enter:
                start = i
        else:
            # 区間の継続条件。`latched` を OR に入れているのは必須で、外すと
            # **成功した介入が全部「非ラッチ」に見える**:
            #   ControllerVirtualDriver::Step は同一フレーム内で
            #   SetSteerTarget(active = !lat_manual) を呼ぶ。ラッチした瞬間に
            #   lat_manual が true になるので target_active は **その同じフレームで**
            #   false になり、gates がゼロ化されて deviation() が None を返す。
            #   継続条件が乖離だけだと、エピソードは override.lateral が立つ
            #   ちょうど 1 フレーム手前で閉じ、ラッチを含まない窓になる。
            #   MANUAL のあいだはドライバーが操作しているのだから、区間は開いたまま
            #   でよい。
            still_open = latched or (d is not None and d >= exit_)
            if (not still_open) or resumed:
                eps.append((start, i + 1 if latched else i))
                start = None
                # RESUME と同時に次の介入が始まっている場合に備える
                if resumed and d is not None and d > enter:
                    start = i
    if start is not None:
        eps.append((start, len(frames)))

    out = []
    for i, j in eps:
        t0 = float(frames[i].get("sim_time", 0.0))
        t1 = float(frames[j - 1].get("sim_time", 0.0))
        if (t1 - t0) >= min_dur:
            out.append((i, j))
    return out


def classify(frames: list[dict], zero_eps: float, min_peak: float) -> tuple[str, float]:
    """Which quadrant was this episode? Judged at the frame of PEAK deviation.

    A minimum peak is required. Without it, a stretch with essentially no
    deviation (e.g. two RESUME presses back to back leaving an empty window)
    gets bucketed into (a) or (b) by sign alone and shows up as a bogus failed
    attempt.
    """
    best = None
    for f in frames:
        d = deviation(f)
        if d is None:
            continue
        if best is None or d > best[0]:
            g = f["ffb"]["gates"]
            best = (d, float(g.get("actual_norm", 0.0)), float(f["ffb"].get("target_norm", 0.0)))
    if best is None:
        return "-(servo never armed)", 0.0
    peak, actual, target = best
    if peak < min_peak:
        return f"-(peak deviation {peak:.4f} below floor)", peak
    if actual * target < 0.0 and abs(actual) >= zero_eps:
        return "(c) countersteer", peak
    if abs(actual) < zero_eps:
        return "(d) held at centre", peak
    if abs(actual) > abs(target):
        return "(b) overtook AD", peak
    return "(a) held INSIDE AD", peak


QUADRANTS = ("(a) held INSIDE AD", "(b) overtook AD",
             "(c) countersteer", "(d) held at centre")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--jsonl", type=Path, required=True)
    ap.add_argument("--enter", type=float, default=0.08,
                    help="エピソード開始: |actual-target| がこれを超える（実測の手放し最大 0.0556 の 1.4 倍）")
    ap.add_argument("--exit", dest="exit_", type=float, default=0.04,
                    help="エピソード終了: これを下回る（ヒステリシス）")
    ap.add_argument("--min-duration", type=float, default=0.15,
                    help="これより短い区間は捨てる [s]")
    ap.add_argument("--min-peak", type=float, default=0.08,
                    help="象限を判定するのに必要なピーク乖離の下限")
    ap.add_argument("--zero-eps", type=float, default=0.02,
                    help="|axis| がこれ未満なら『センター』とみなす（0.02 axis ≈ 9 deg）")
    args = ap.parse_args()

    frames = load(args.jsonl)
    episodes = find_episodes(frames, args.enter, args.exit_, args.min_duration)
    resumes = sum(1 for f in frames if f.get("override", {}).get("resume_pressed"))

    print("=" * 78)
    print("feature:F7  有人セッション自動判定")
    print("=" * 78)
    print(f"source   : {args.jsonl}")
    print(f"frames   : {len(frames)}   span {frames[0].get('sim_time', 0):.2f}s .. "
          f"{frames[-1].get('sim_time', 0):.2f}s")
    print(f"区切り   : |actual-target| > {args.enter} で開始 / < {args.exit_} で終了 "
          f"/ 最短 {args.min_duration}s")
    print(f"介入エピソード : {len(episodes)} 件   （参考: RESUME 押下 {resumes} 回）")
    print("NOTE: 区切りはドライバーの操作そのもの。RESUME を押し忘れても、順番を変えても、")
    print("      順不同でも結果は変わらない。ユーザーへの要求はゼロ。")
    print(f"      未検出帯は {UNDETECTABLE_BAND:.5f} axis-frac ≈ 7.8 deg（これ以下の抵抗は仕様として非検出）")
    print()

    if not episodes:
        print("介入エピソードが 1 つも検出できなかった。")
        print("ハンドルを握っていないか、握りが未検出帯以下だった可能性がある。")
        return 1

    hdr = (f"{'#':>2} {'window [s]':>16} {'quadrant':<24} {'peak dev':>9} "
           f"{'latched':>8} {'latency':>8} {'residual pk':>12}")
    print(hdr)
    print("-" * len(hdr))

    failed, quads = [], {}
    for n, (i, j) in enumerate(episodes, 1):
        seg = frames[i:j]
        t0 = float(seg[0].get("sim_time", 0.0))
        t1 = float(seg[-1].get("sim_time", 0.0))
        latch_t, rpk = None, 0.0
        for f in seg:
            g = f.get("ffb", {}).get("gates", {})
            rpk = max(rpk, float(g.get("residual", 0.0)))
            if latch_t is None and f.get("override", {}).get("lateral"):
                latch_t = float(f.get("sim_time", 0.0))
        quad, peak = classify(seg, args.zero_eps, args.min_peak)
        lat = f"{latch_t - t0:.2f}s" if latch_t is not None else "-"
        print(f"{n:>2} {t0:7.2f}..{t1:7.2f} {quad:<24} {peak:>9.4f} "
              f"{'YES' if latch_t is not None else 'NO':>8} {lat:>8} {rpk:>12.4f}")
        if not quad.startswith("-"):
            quads.setdefault(quad, []).append(n)
            if latch_t is None:
                failed.append((n, quad, peak, rpk))

    print("\n象限カバレッジ:")
    for q in QUADRANTS:
        got = quads.get(q, [])
        print(f"  {q:<24} {'エピソード ' + ','.join(map(str, got)) if got else '**未実施**'}")

    latched_eps = [n for n, (i, j) in enumerate(episodes, 1)
                   if any(f.get("override", {}).get("lateral") for f in frames[i:j])]
    print(f"\n多サイクル: {len(latched_eps)} 回ラッチ（エピソード "
          f"{','.join(map(str, latched_eps)) or '-'}）")

    if failed:
        print("\n入らなかった介入:")
        for n, q, peak, rpk in failed:
            print(f"  エピソード {n}  {q}  ピーク乖離 {peak:.4f}  残差ピーク {rpk:.4f}")
        print("\nRESULT: FAIL")
        return 1

    missing = [q for q in QUADRANTS if q not in quads]
    if missing:
        print(f"\nWARN: 未実施の象限がある: {', '.join(missing)}")
        print("RESULT: INCOMPLETE")
        return 2

    print("\nRESULT: PASS（4 象限すべて実施・すべてラッチ）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
