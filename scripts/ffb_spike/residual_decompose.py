#!/usr/bin/env python
"""feature:F7 — 実機ログで残差の発生源を分解する（原因を決め打ちしないための道具）。

## なぜ要るか

手放し走行の残差ピークが 0.089 でラッチしきい値 0.080 を超えた。原因を「慣性」と
決め打ちして 2 次系化に進むのは危険なので、**残差がどの物理レジームで積み上がったか**
を実測から切り分ける。主因が慣性でなければ方針ごと見直す。

## 何をしているか

残差は `|actual - shadow|` であり、その増分は各フレームの
`v_actual - v_shadow` の積分である。そこで 1 フレームごとに
「シャドウがどのレジームにいたか」でラベルを付け、`|Δresidual|` をレジーム別に
合計する。合計に占める割合が大きいレジームが主因である。

レジーム（シャドウ側の状態で決まる。`OverrideManager::Update` と同じ分類）:

  R1 onset-miss   シャドウ静止・実測は動いている
                  -> breakaway の第2アーム（観測併用）が開くのが遅い／開かない
  R2 vmax-clamp   シャドウが v_max に張り付き、実測がそれより速い
                  -> 速度上限の過小
  R3 slope        両方運動中で線形域
                  -> 力→速度の傾きの誤差（定常写像そのものの精度）
  R4 coast        シャドウ停止（|f| < kinetic）・実測はまだ動いている
                  -> 慣性による惰行。ヒステリシス下限の位置ではなく「止まらない」性質
  R5 taper/clamp  ハードストップ taper 域、または |f| が max_force に張り付き
                  -> 力の頭打ちによる予測不能

同時に、残差増分と `d|f|/dt` の相関を出す。慣性が主因なら、力が速く変化する区間で
残差が伸びるはずである（定常写像は力の変化に瞬時に追従するが、実機は追従しない）。

## 使い方

    python residual_decompose.py --jsonl <run>.jsonl [--jsonl <run2>.jsonl ...]
"""

from __future__ import annotations

import argparse
import io
import json
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import _shipped_config  # noqa: E402

# OverrideManager の出荷値（GT_esmini/config/virtual_driver.json から読む —
# 較正でconfigが変わっても追随するよう、ここでは再宣言しない。feature:F7
# wheel autocal要件 sec 6-1/7-5）。ここはシャドウの「モデル」ではなく、
# レジーム分類のための境界値として使う。
_sc = _shipped_config.load()
KINETIC = _sc.kinetic
BRK_HI = _sc.brk_hi
BRK_LEFT = _sc.brk_left
BRK_RIGHT = _sc.brk_right
SLOPE = _sc.slope
VMAX = _sc.vmax
MAX_FORCE = _sc.max_force
HARD_STOP_ZONE = _sc.hard_stop_zone


def load(path: Path) -> list[dict]:
    rows = []
    for line in io.open(path, encoding="utf-8"):
        line = line.strip()
        if line:
            try:
                rows.append(json.loads(line))
            except json.JSONDecodeError:
                pass
    if not rows:
        raise SystemExit(f"no frames in {path}")
    return rows


def model_speed(f: float) -> float:
    return min(SLOPE * max(abs(f) - KINETIC, 0.0), VMAX)


def analyse(path: Path) -> dict:
    rows = load(path)
    S = []
    for r in rows:
        ffb = r["ffb"]
        g = ffb["gates"]
        S.append(dict(t=float(r["sim_time"]), act=float(g["actual_norm"]),
                      sh=float(g["shadow_norm"]), res=float(g["residual"]),
                      f=float(g["effective_force"]), mv=bool(g["shadow_moving"]),
                      sus=float(g["sustain_accum"]), tgt=float(ffb["target_norm"])))

    buckets = {k: 0.0 for k in ("R1 onset-miss", "R2 vmax-clamp", "R3 slope",
                                "R4 coast", "R5 taper/clamp", "R0 residual falling")}
    dres_list, dfdt_list = [], []
    peak = max(S, key=lambda r: r["res"])

    for i in range(1, len(S)):
        a, b = S[i - 1], S[i]
        dt = b["t"] - a["t"]
        if dt <= 0:
            continue
        dres = b["res"] - a["res"]
        v_act = (b["act"] - a["act"]) / dt
        dfdt = (abs(b["f"]) - abs(a["f"])) / dt
        dres_list.append(dres)
        dfdt_list.append(abs(dfdt))
        if dres <= 0:
            buckets["R0 residual falling"] += abs(dres)
            continue

        f = abs(b["f"])
        v_mod = model_speed(b["f"])
        # 分類は上から順に、より特異なレジームを優先する。
        if f >= MAX_FORCE - 1e-6 or abs(b["act"]) > HARD_STOP_ZONE:
            buckets["R5 taper/clamp"] += dres
        elif not b["mv"] and abs(v_act) > 0.02:
            buckets["R1 onset-miss"] += dres
        elif b["mv"] and f < KINETIC and abs(v_act) > 0.02:
            buckets["R4 coast"] += dres
        elif b["mv"] and v_mod >= VMAX - 1e-9 and abs(v_act) > VMAX:
            buckets["R2 vmax-clamp"] += dres
        elif b["mv"]:
            buckets["R3 slope"] += dres
        else:
            buckets["R4 coast"] += dres

    rising = sum(v for k, v in buckets.items() if k != "R0 residual falling")

    # 相関: 残差の増分 vs 力の変化率の大きさ
    up = [(d, g) for d, g in zip(dres_list, dfdt_list) if d > 0]
    corr = float("nan")
    if len(up) > 2:
        xs = [g for _, g in up]
        ys = [d for d, _ in up]
        mx, my = sum(xs) / len(xs), sum(ys) / len(ys)
        sx = math.sqrt(sum((x - mx) ** 2 for x in xs))
        sy = math.sqrt(sum((y - my) ** 2 for y in ys))
        if sx > 0 and sy > 0:
            corr = sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / (sx * sy)

    v_actuals = []
    for i in range(1, len(S)):
        dt = S[i]["t"] - S[i - 1]["t"]
        if dt > 0:
            v_actuals.append(abs((S[i]["act"] - S[i - 1]["act"]) / dt))

    return dict(path=path, n=len(S), buckets=buckets, rising=rising, corr=corr,
                peak=peak, sus_max=max(r["sus"] for r in S),
                res_max=max(r["res"] for r in S),
                v_act_max=max(v_actuals) if v_actuals else 0.0,
                frac_over_vmax=sum(1 for v in v_actuals if v > VMAX) / max(len(v_actuals), 1))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--jsonl", type=Path, action="append", required=True)
    args = ap.parse_args()

    print("=" * 78)
    print("feature:F7  残差の発生源分解（原因を決め打ちしないための分析）")
    print("=" * 78)
    print(f"シャドウ定数: kinetic={KINETIC} breakaway={BRK_HI}(uncond)/"
          f"{BRK_LEFT}L/{BRK_RIGHT}R slope={SLOPE} v_max={VMAX}")
    print()

    for path in args.jsonl:
        a = analyse(path)
        print(f"--- {path.name}  ({a['n']} frames) ---")
        print(f"  residual max = {a['res_max']:.4f}   sustain_accum max = {a['sus_max']:.4f}s")
        p = a["peak"]
        print(f"  ピーク時刻 t={p['t']:.2f}: actual={p['act']:+.4f} shadow={p['sh']:+.4f} "
              f"target={p['tgt']:+.4f} f={p['f']:+.4f} shadow_moving={p['mv']}")
        print(f"  実測軸速度 最大 = {a['v_act_max']:.3f}/s   v_max({VMAX}) 超過フレーム割合 = "
              f"{a['frac_over_vmax']*100:.2f}%")
        print(f"  残差増分 vs |d|f|/dt| の相関 = {a['corr']:+.3f}")
        print(f"  {'レジーム':<20} {'Σ|Δresidual|':>14} {'上昇分に占める割合':>20}")
        for k in ("R1 onset-miss", "R2 vmax-clamp", "R3 slope", "R4 coast", "R5 taper/clamp"):
            v = a["buckets"][k]
            pct = (v / a["rising"] * 100) if a["rising"] > 0 else 0.0
            print(f"  {k:<20} {v:>14.4f} {pct:>19.1f}%")
        print(f"  {'(参考) 下降分':<20} {a['buckets']['R0 residual falling']:>14.4f}")
        print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
