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

そこで**ドライバーの操作そのもの**で区切る: `|actual − target|` が閾値を超えた状態が
**継続した**区間を 1 介入エピソードとみなす（開始も終了も継続時間で判定する。
理由は find_episodes() の docstring）。ユーザーへの要求はゼロになり、RESUME を
押し忘れても、押す順番を変えても、順不同でやっても結果は変わらない。

RESUME 押下は**補助的な分割点**としてのみ使う（押されていれば、間に手が戻らない
連続 2 回の介入を分離できる）。押下が無くても判定は成立する。

## しきい値は製品の検出下限より「下」に置く

製品の検出下限は **0.01729 axis-frac（約 7.8 度）**。ツールの下限をそれより上に置くと
**測定器が測定対象より鈍い**状態になり、7.8〜36 度の本物の介入を製品は検出するのに
ツールが記録しない。ユーザーが軽く押さえた回は、製品が正しくラッチしても
レポート上は「その象限は未実施」と出る。よって enter = **0.015**。

大きさで足切りできない分は**継続時間**で分離する。手放しの追従誤差は過渡的で
続かない（0.015 超えの連続区間は実測 最長 0.25 s）のに対し、ドライバーが握るのは
秒単位だからである。実測値と導出は find_episodes() を見ること。

製品の検出下限を下回るピーク乖離のエピソードは、**非ラッチが仕様どおり**なので
失敗に数えず、区別して報告する。

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

sys.path.insert(0, str(Path(__file__).resolve().parent))
import _shipped_config  # noqa: E402

# 未検出帯の真値: kp*x + friction_ff*tanh(x/eps) = shadow_breakaway を満たす x。
# config/virtual_driver.json のkp/friction_ff/eps/breakawayから毎回解く —
# 出荷値(4.0/0.15/0.010/0.21)なら0.01729 axis-frac ≈ 7.8 degになるが、較正で
# これらが変わっても追随する(feature:F7 wheel autocal要件 sec 6-1/7-5、
# ハードコードした導出結果を再宣言しない)。
UNDETECTABLE_BAND = _shipped_config.undetectable_band()


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


def find_episodes(frames: list[dict], enter: float, open_dur: float,
                  close_dur: float) -> list[tuple[int, int]]:
    """介入エピソードを切り出す。**大きさではなく継続時間で切る。**

    開始: `|actual-target| > enter` が `open_dur` 秒 連続したとき（開始点は遡る）
    終了: `|actual-target| <= enter` が `close_dur` 秒 連続したとき
          （ただし `override.lateral` が立っている間は開いたまま）

    ## なぜ大きさで切ってはいけないか（PM 判断・監査 D2/D3）

    製品の検出下限は 0.01729 axis-frac（約 7.8 度）。ツールの下限をそれより上
    （旧実装は 0.08 = 約 36 度）に置くと、**7.8〜36 度の本物の介入を製品は検出するのに
    ツールが記録しない**。ユーザーが軽く押さえた回は、製品が正しくラッチしても
    レポート上は「その象限は未実施」と出て、誤った結論を生む。測定器が測定対象より鈍い。

    ## なぜ継続時間なら切れるか（実測）

    手放し走行（力結合プラント・right_turn・820 フレーム）で `|actual-target|` が
    0.015 を超える連続区間は **最長 0.25 s**（0.020 なら 0.15 s、0.025 以上は 0.05 s）。
    AD の追従誤差は「サーボが追いつく途中」なので本質的に過渡的で続かない。対して
    ドライバーが握るのは秒単位。よって enter=0.015 / open_dur=0.50 s（実測最長の 2 倍）で、
    手放しからはエピソードが 1 本も出ない一方、製品が検出できる最小の介入より下から拾える。

    ## 終了もしきい値ではなく継続時間で見る理由（実装中に踏んだ罠）

    最初は「`exit` を下回ったら終了」というヒステリシスにしていたが、これは実機で
    **一度開いたエピソードが二度と閉じない**。手放しの定常乖離は中央値 0.0123 で、
    enter(0.015) より下だが exit(0.010) より **上** だからである。閾値を下げても
    中央値より下には置けない（置けば開始条件と逆転する）。
    「enter 以下が close_dur 秒続いたら閉じる」なら、手放しでは 820 中 762 フレームが
    enter 以下なので確実に閉じ、介入中は閉じない。開始と同じ原理で対称になる。
    """
    eps = []
    start = None          # 確定したエピソードの開始 index
    above_since = None    # enter 超えが続いている区間の開始 (index, time)
    below_since = None    # enter 以下が続いている時刻（エピソード中のみ意味を持つ）
    for i_, f in enumerate(frames):
        d = deviation(f)
        t_now = float(f.get("sim_time", 0.0))
        latched = bool(f.get("override", {}).get("lateral"))
        resumed = bool(f.get("override", {}).get("resume_pressed"))
        above = (d is not None and d > enter)

        if start is None:
            if above:
                if above_since is None:
                    above_since = (i_, t_now)
                elif (t_now - above_since[1]) >= open_dur:
                    start, above_since, below_since = above_since[0], None, None
            else:
                above_since = None
            # 製品がラッチしたなら継続時間を待たずに開く。製品が検出したものを
            # ツールが取りこぼさないための保険。
            if start is None and latched:
                start = above_since[0] if above_since else i_
                above_since, below_since = None, None
        else:
            if latched or above:
                below_since = None          # MANUAL 中／乖離継続中は閉じない
            elif below_since is None:
                below_since = t_now
            closed = (below_since is not None and (t_now - below_since) >= close_dur)
            if closed or resumed:
                # 終端は「最後に乖離があった／ラッチしていた」ところまで。
                end = i_
                while end > start and not (
                        (deviation(frames[end - 1]) or 0.0) > enter
                        or frames[end - 1].get("override", {}).get("lateral")):
                    end -= 1
                eps.append((start, max(end, start + 1)))
                start, below_since = None, None
                above_since = (i_, t_now) if above else None
    if start is not None:
        eps.append((start, len(frames)))
    return eps


def classify(frames: list[dict], zero_eps: float) -> tuple[str, float]:
    """このエピソードはどの象限の介入だったか。ピーク乖離のフレームで判定する。

    **ピーク乖離の大きさで足切りはしない**（PM 判断）。大きさで切ると、製品が検出
    できる小さな介入をツールが捨ててしまう。旧実装が持っていた `--min-peak 0.08` が
    解決していた「空区間が符号だけで (a)/(b) に誤分類される」問題は、
    find_episodes() 側の**最小継続時間**が構造的に解決している（乖離の無い区間は
    そもそもエピソードとして開かない）。
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
    ap.add_argument("--enter", type=float, default=0.015,
                    help="エピソード開始しきい値。製品の検出下限 0.01729 より下に置く")
    ap.add_argument("--min-duration", type=float, default=0.50,
                    help="開始に必要な継続時間 [s]。実測の手放し最長 0.25s の 2 倍")
    ap.add_argument("--close-duration", type=float, default=0.50,
                    help="終了に必要な『enter 以下』の継続時間 [s]")
    ap.add_argument("--zero-eps", type=float, default=0.02,
                    help="|axis| がこれ未満なら『センター』とみなす（0.02 axis ≈ 9 deg）")
    args = ap.parse_args()

    frames = load(args.jsonl)
    episodes = find_episodes(frames, args.enter, args.min_duration, args.close_duration)
    resumes = sum(1 for f in frames if f.get("override", {}).get("resume_pressed"))

    print("=" * 78)
    print("feature:F7  有人セッション自動判定")
    print("=" * 78)
    print(f"source   : {args.jsonl}")
    print(f"frames   : {len(frames)}   span {frames[0].get('sim_time', 0):.2f}s .. "
          f"{frames[-1].get('sim_time', 0):.2f}s")
    print(f"区切り   : |actual-target| > {args.enter} が {args.min_duration}s 継続で開始 "
          f"/ 以下が {args.close_duration}s 継続で終了")
    print(f"           （製品の検出下限 {UNDETECTABLE_BAND:.5f} より下に置いてある。"
          f"ツールが製品より鈍くならないようにするため）")
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

    failed, below_floor, quads = [], [], {}
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
        quad, peak = classify(seg, args.zero_eps)
        lat = f"{latch_t - t0:.2f}s" if latch_t is not None else "-"
        print(f"{n:>2} {t0:7.2f}..{t1:7.2f} {quad:<24} {peak:>9.4f} "
              f"{'YES' if latch_t is not None else 'NO':>8} {lat:>8} {rpk:>12.4f}")
        if not quad.startswith("-"):
            quads.setdefault(quad, []).append(n)
            if latch_t is None:
                # 製品の検出下限 (0.01729 axis-frac ≈ 7.8 deg) を下回る
                # 押さえは、サーボ力が breakaway に届かず無負荷ホイールも
                # 動かないので **非ラッチが仕様どおり**。失敗に数えない。
                # ツールの下限を製品より下げた結果ここが見えるように
                # なったので、区別して報告する。
                if peak < UNDETECTABLE_BAND:
                    below_floor.append((n, quad, peak))
                else:
                    failed.append((n, quad, peak, rpk))

    print("\n象限カバレッジ:")
    for q in QUADRANTS:
        got = quads.get(q, [])
        print(f"  {q:<24} {'エピソード ' + ','.join(map(str, got)) if got else '**未実施**'}")

    latched_eps = [n for n, (i, j) in enumerate(episodes, 1)
                   if any(f.get("override", {}).get("lateral") for f in frames[i:j])]
    print(f"\n多サイクル: {len(latched_eps)} 回ラッチ（エピソード "
          f"{','.join(map(str, latched_eps)) or '-'}）")

    if below_floor:
        print("\n製品の検出下限を下回った介入（非ラッチは仕様どおり・失敗ではない）:")
        for n_, q, peak in below_floor:
            print(f"  エピソード {n_}  {q}  ピーク乖離 {peak:.4f} < "
                  f"{UNDETECTABLE_BAND:.5f}（約 {peak*450:.1f} deg）")

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
